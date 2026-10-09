/* 众灵总线舵机调试器 WebUI —— 前端逻辑 */
(function () {
  "use strict";

  // 必须通过 http://127.0.0.1:5000 访问；直接双击打开 HTML（file://）会导致
  // Socket.IO 客户端无法加载，整页功能失效。这里给出明确提示。
  if (typeof io === "undefined") {
    // file:// 直接打开：明确提示，且不自动刷新（刷新也救不了）。
    if (location.protocol === "file:") {
      document.body.innerHTML =
        '<div style="padding:24px;color:#fff;background:#1e1e22;font-family:sans-serif;line-height:1.8">' +
        '<h2>无法连接到调试器后端</h2>' +
        '<p>请确认：</p>' +
        '<ol><li>已运行 <code>python app.py</code>（或 <code>python run.py</code>）启动服务端；</li>' +
        '<li>在浏览器地址栏输入 <b>http://127.0.0.1:5000</b> 打开本页面，' +
        '<b>不要直接双击 HTML 文件</b>（file:// 方式无法加载 Socket.IO 客户端）。</li></ol></div>';
      return;
    }
    // 通过 http 访问但脚本尚未就绪（服务端刚启动、首屏加载竞态）：
    // 给出“正在连接”提示并自动重试，最多几次，避免“先报错再显示界面”的闪烁。
    var tries = parseInt(sessionStorage.getItem("sock_retry") || "0", 10);
    if (tries < 6) {
      sessionStorage.setItem("sock_retry", String(tries + 1));
      document.body.innerHTML =
        '<div style="padding:24px;color:#cfd2dc;background:#1e1e22;font-family:sans-serif">' +
        '正在连接调试器后端…（若长时间停留请手动刷新页面）</div>';
      setTimeout(function () { location.reload(); }, 800);
      return;
    }
    document.body.innerHTML =
      '<div style="padding:24px;color:#fff;background:#1e1e22;font-family:sans-serif">' +
      '无法加载 Socket.IO 客户端，请刷新页面或确认 <code>static/socket.io.min.js</code> 存在。</div>';
    return;
  }
  sessionStorage.removeItem("sock_retry");

  const socket = io();
  const $ = (id) => document.getElementById(id);

  // 本地 UI 状态（连接参数）—— 用 localStorage 持久化，下次打开自动恢复上次选择
  const store = {
    _k: (k) => "zlservo_" + k,
    get(k, def) { try { const v = localStorage.getItem(this._k(k)); return v === null ? def : v; } catch (e) { return def; } },
    set(k, v) { try { localStorage.setItem(this._k(k), String(v)); } catch (e) {} },
    getNum(k, def) { const v = this.get(k, null); if (v === null) return def; const n = parseInt(v, 10); return isNaN(n) ? def : n; },
  };

  let baudIndex = store.getNum("baudIndex", 5);   // 1~8 档位
  let portName = store.get("portName", "COM3");
  let servoId = store.getNum("servoId", 0);
  let info = null;         // /api/command_info 的结果
  let syncRows = [
    { id: 0, pwm: 1500, time: 1000 },
    { id: 1, pwm: 1500, time: 1000 },
  ];

  // ===== 工具 =====
  function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

  function sendCommand(action, params) {
    socket.emit("send_command", { action: action, params: params || {} });
  }

  let _previewTimer = null;
  function previewAction(action, params, cb) {
    clearTimeout(_previewTimer);
    _previewTimer = setTimeout(() => {
      fetch("/api/preview", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: action, params: params || {} }),
      })
        .then((r) => r.json())
        .then((d) => { if (cb) cb(d.command); })
        .catch(() => { if (cb) cb("—"); });
    }, 120);
  }

  // ===== 连接栏 =====
  function refreshPorts() {
    socket.emit("get_ports");
  }

  socket.on("ports", (d) => {
    const sel = $("port-select");
    const ports = d.ports || [];
    sel.innerHTML = "";
    if (ports.length === 0) {
      const o = document.createElement("option");
      o.text = "（未检测到串口）";
      o.value = "";
      sel.appendChild(o);
      return;
    }
    // 若当前选择的端口不在列表中，自动选中第一个
    if (ports.indexOf(portName) < 0) {
      portName = ports[0];
      updateConnText();
    }
    ports.forEach((p) => {
      const o = document.createElement("option");
      o.text = p; o.value = p;
      if (p === portName) o.selected = true;
      sel.appendChild(o);
    });
  });

  $("port-select").addEventListener("change", (e) => {
    portName = e.target.value;
    store.set("portName", portName);
    updateConnText();
  });
  $("refresh-ports").addEventListener("click", refreshPorts);

  $("baud-select").addEventListener("change", (e) => {
    baudIndex = parseInt(e.target.value, 10);
    store.set("baudIndex", baudIndex);
    updateConnText();
  });

  $("toggle-conn").addEventListener("click", () => {
    if ($("toggle-conn").textContent.includes("打开")) {
      socket.emit("connect_port", { port: portName, baud_index: baudIndex });
    } else {
      socket.emit("disconnect_port");
    }
  });

  function updateConnText() {
    $("st-port").textContent = portName || "—";
    $("st-baud").textContent = info ? info.baud_rates[baudIndex - 1] : "—";
  }

  // ===== 舵机 ID 行 =====
  function setServoId(v, isBroadcast) {
    // 广播地址 255 是合法的，非广播时普通 ID 才被限制在 [0, max_id]
    const hi = isBroadcast ? 255 : (info ? info.max_id : 254);
    servoId = clamp(v, 0, hi);
    store.set("servoId", servoId);
    $("servo-id").value = servoId;
    // 同步到服务端：否则服务端 status 里的 servo_id 仍为 0，HTTP 轮询会把输入框改回 0
    socket.emit("set_servo_id", { id: servoId });
    refreshControlPreview();
    refreshIdPreview();
  }
  $("servo-id").addEventListener("change", (e) => setServoId(parseInt(e.target.value, 10) || 0));
  $("id-minus").addEventListener("click", () => setServoId(servoId - 1));
  $("id-plus").addEventListener("click", () => setServoId(servoId + 1));
  $("id-broadcast").addEventListener("click", () => setServoId(255, true));
  $("goto-params").addEventListener("click", () => switchTab(2));

  socket.on("set_servo_id_ack", () => {}); // 占位

  // ===== Tabs =====
  function switchTab(idx) {
    document.querySelectorAll("#tabs button").forEach((b) => {
      b.classList.toggle("active", parseInt(b.dataset.tab, 10) === idx);
    });
    document.querySelectorAll(".tab-panel").forEach((p) => {
      p.classList.toggle("active", parseInt(p.dataset.panel, 10) === idx);
    });
  }
  document.querySelectorAll("#tabs button").forEach((b) => {
    b.addEventListener("click", () => switchTab(parseInt(b.dataset.tab, 10)));
  });

  // ===== 状态渲染 =====
  // 注意：flask-socketio 的 threading 模式下，从后台线程（ticker）发出的 status
  // 事件可能被静默丢弃，导致“关闭串口”后按钮不复位等问题。因此这里同时：
  //   1) 继续接收 Socket.IO 推送的 status（命令触发的即时更新）；
  //   2) 额外用 HTTP 轮询 /api/status 作为可靠来源，保证连接状态/读数始终正确。
  function renderStatus(s) {
    const connected = s.connected;
    const btn = $("toggle-conn");
    if (btn) btn.textContent = connected ? "关闭串口" : "打开串口";

    const st = $("conn-status");
    if (st) {
      if (connected) {
        st.classList.add("ok");
        st.textContent = `● 已连接：${s.port} · ${info ? info.baud_rates[s.baud_index - 1] : ""} bps · 8-N-1`;
      } else {
        st.classList.remove("ok");
        st.textContent = `○ 未连接（将使用 ${portName} @ ${info ? info.baud_rates[baudIndex - 1] : ""} bps · 8-N-1，点击右侧“打开串口”）`;
      }
    }

    // 故障框
    const fb = $("fault-box");
    if (fb) {
      if (s.connected === false && s.has_fault) {
        fb.style.display = "block";
        fb.textContent = "串口通信异常：" + s.fault_msg;
      } else {
        fb.style.display = "none";
      }
    }

    // 舵机 ID（仅在服务端值与本地不一致时同步，且输入框未聚焦时才改，避免打断用户输入）
    if (typeof s.servo_id === "number" && s.servo_id !== servoId) {
      const sid = $("servo-id");
      if (sid && document.activeElement !== sid) {
        servoId = s.servo_id;
        sid.value = servoId;
      }
    }

    // 读数
    const r = s.last_reading || {};
    setText("m-ver", s.last_version ? "V" + s.last_version : "—");
    setText("m-pos", s.last_position >= 0 ? s.last_position : "—");
    setText("v-pos", s.last_position >= 0 ? s.last_position : "—");
    setText("v-mode", s.last_mode >= 0 ? `${s.last_mode} - ${modeShort(s.last_mode)}` : "—");
    setText("v-ver", s.last_version ? "V" + s.last_version : "—");
    setText("v-id", s.last_reported_id >= 0 ? s.last_reported_id : "—");
    setText("v-temp", r.valid ? r.temperature_c.toFixed(1) + " ℃" : "—");
    setText("v-volt", r.has_voltage ? r.voltage_v.toFixed(1) + " V" : "—");
    setText("v-ad", r.valid ? r.ad : "—");
    setText("v-reply", s.last_reply || "—");

    // 往复状态
    const sw = s.sweep || {};
    const sStart = $("s-start"), sStop = $("s-stop"), sStatus = $("s-status");
    if (sStart) sStart.disabled = !!sw.running;
    if (sStop) sStop.disabled = !sw.running;
    if (sStatus) sStatus.textContent = sw.running
      ? `运行中：当前 ${sw.current}，已完成 ${sw.half_cycles} 段` : "";

    // 自动轮询
    setChecked("ap-enable", !!s.auto_poll.enabled);
    setChecked("ap-pos", !!s.auto_poll.position);
    setChecked("ap-tv", !!s.auto_poll.temp_voltage);
    setVal("ap-interval", s.auto_poll.interval_ms);
    setText("ap-interval-val", s.auto_poll.interval_ms);
    let apMsg = "未启用：打开串口后不会有任何自动发送。";
    if (s.auto_poll.enabled && !connected) apMsg = "已启用，但串口未打开，暂不发送。";
    else if (s.auto_poll.enabled && !s.auto_poll.position && !s.auto_poll.temp_voltage) apMsg = "已启用，但没有勾选任何读取项。";
    else if (s.auto_poll.enabled) apMsg = `轮询中：每 ${s.auto_poll.interval_ms} ms 发送一次读取指令。`;
    setText("ap-status", apMsg);

    // 应答超时
    setVal("v-timeout", s.reply_timeout_ms);
    setText("v-pending", (s.pending_count > 0) ? `等待应答中：${s.pending_count} 条` : "");

    // 日志选项
    setChecked("log-hex", !!s.log_hex);
    setChecked("log-parsed", !!s.log_show_parsed);
  }

  function setText(id, t) { const e = $(id); if (e) e.textContent = t; }
  function setVal(id, v) { const e = $(id); if (e && document.activeElement !== e) e.value = v; }
  function setChecked(id, v) { const e = $(id); if (e) e.checked = v; }

  socket.on("status", renderStatus);

  // 可靠的 HTTP 轮询：即使 Socket.IO 后台推送丢失，UI 也能正确反映连接状态/读数
  function pollStatus() {
    fetch("/api/status")
      .then((r) => r.json())
      .then(renderStatus)
      .catch(() => {});
  }
  setInterval(pollStatus, 250);
  pollStatus();

  function modeShort(m) {
    if (!info) return "";
    if (m >= 1 && m <= info.mode_labels.length) return info.mode_labels[m - 1].split(" - ")[1] || "";
    return "未知模式";
  }

  // ===== 页 1：控制 =====
  function bindSlider(id, valId, onChange) {
    const el = $(id), v = $(valId);
    el.addEventListener("input", () => {
      v.textContent = el.value;
      onChange(parseInt(el.value, 10));
    });
  }

  function refreshControlPreview() {
    const pwm = parseInt($("c-pwm").value, 10);
    const time = parseInt($("c-time").value, 10);
    previewAction("move", { id: servoId, pwm: pwm, time: time }, (c) => {
      $("c-preview").textContent = c || "—";
    });
  }

  bindSlider("c-pwm", "c-pwm-val", () => {
    const pwm = parseInt($("c-pwm").value, 10);
    const time = parseInt($("c-time").value, 10);
    refreshControlPreview();
    if ($("c-live").checked && $("toggle-conn").textContent.includes("关闭")) {
      sendCommand("move", { id: servoId, pwm: pwm, time: time });
    }
  });
  bindSlider("c-time", "c-time-val", refreshControlPreview);

  $("c-mid").addEventListener("click", () => { $("c-pwm").value = 1500; $("c-pwm-val").textContent = 1500; refreshControlPreview(); });
  $("c-min").addEventListener("click", () => { $("c-pwm").value = 500; $("c-pwm-val").textContent = 500; refreshControlPreview(); });
  $("c-max").addEventListener("click", () => { $("c-pwm").value = 2500; $("c-pwm-val").textContent = 2500; refreshControlPreview(); });
  $("c-send").addEventListener("click", () => {
    sendCommand("move", { id: servoId, pwm: parseInt($("c-pwm").value, 10), time: parseInt($("c-time").value, 10) });
  });

  $("btn-ver").addEventListener("click", () => sendCommand("read_version", { id: servoId }));
  $("btn-pos").addEventListener("click", () => sendCommand("read_position", { id: servoId }));

  // 往复
  ["s-from", "s-to", "s-step", "s-interval", "s-time"].forEach((id) => {
    const valId = id + "-val";
    bindSlider(id, valId, () => {});
  });
  $("s-start").addEventListener("click", () => {
    socket.emit("start_sweep", {
      from: parseInt($("s-from").value, 10),
      to: parseInt($("s-to").value, 10),
      step: parseInt($("s-step").value, 10),
      interval_ms: parseInt($("s-interval").value, 10),
      time_ms: parseInt($("s-time").value, 10),
      repeat: parseInt($("s-repeat").value, 10),
    });
  });
  $("s-stop").addEventListener("click", () => socket.emit("stop_sweep"));

  // ===== 页 2：监控 =====
  $("m2-pos").addEventListener("click", () => sendCommand("read_position", { id: servoId }));
  $("m2-mode").addEventListener("click", () => sendCommand("read_mode", { id: servoId }));
  $("m2-ver").addEventListener("click", () => sendCommand("read_version", { id: servoId }));
  $("m2-id").addEventListener("click", () => sendCommand("read_id", { id: servoId }));
  $("m2-tv").addEventListener("click", () => sendCommand("read_temp_voltage", { id: servoId }));
  $("m2-stb").addEventListener("click", () => sendCommand("read_protect", { id: servoId }));
  $("m2-all").addEventListener("click", () => {
    sendCommand("read_id", { id: servoId });
    sendCommand("read_version", { id: servoId });
    sendCommand("read_mode", { id: servoId });
    sendCommand("read_position", { id: servoId });
    sendCommand("read_temp_voltage", { id: servoId });
  });

  $("v-timeout").addEventListener("change", (e) => {
    socket.emit("set_reply_timeout", { ms: parseInt(e.target.value, 10) || 800 });
  });

  function pushAutoPoll() {
    socket.emit("set_autopoll", {
      enabled: $("ap-enable").checked,
      position: $("ap-pos").checked,
      temp_voltage: $("ap-tv").checked,
      interval_ms: parseInt($("ap-interval").value, 10),
    });
  }
  ["ap-enable", "ap-pos", "ap-tv"].forEach((id) => $(id).addEventListener("change", pushAutoPoll));
  bindSlider("ap-interval", "ap-interval-val", pushAutoPoll);

  // ===== 页 3：参数 =====
  function refreshIdPreview() {
    const newId = parseInt($("p-newid").value, 10) || 0;
    previewAction("write_id", { id: servoId, new_id: newId }, (c) => {
      $("p-id-preview").textContent = "将发送：" + (c || "—");
    });
  }
  $("p-newid").addEventListener("change", refreshIdPreview);
  $("p-newid-plus").addEventListener("click", () => {
    $("p-newid").value = clamp(servoId + 1, 0, info.max_id);
    refreshIdPreview();
  });
  $("p-id-do").addEventListener("click", () => {
    if (confirm(`确定要把 ID ${servoId} 的舵机修改为 ID ${parseInt($("p-newid").value, 10)} 吗？\n同一总线上多个舵机改 ID 时，请只接一个，或确保要改的那个 ID 不冲突。`)) {
      sendCommand("write_id", { id: servoId, new_id: parseInt($("p-newid").value, 10) });
    }
  });

  $("p-mode-read").addEventListener("click", () => sendCommand("read_mode", { id: servoId }));
  $("p-mode-set").addEventListener("click", () => sendCommand("write_mode", { id: servoId, mode: parseInt($("p-mode").value, 10) }));

  $("p-ulk").addEventListener("click", () => sendCommand("torque_off", { id: servoId }));
  $("p-ulr").addEventListener("click", () => sendCommand("torque_on", { id: servoId }));
  $("p-dpt").addEventListener("click", () => sendCommand("pause", { id: servoId }));
  $("p-dct").addEventListener("click", () => sendCommand("resume", { id: servoId }));
  $("p-dst").addEventListener("click", () => sendCommand("stop_here", { id: servoId }));

  $("p-sck").addEventListener("click", () => sendCommand("set_mid", { id: servoId }));
  $("p-sck-off").addEventListener("click", () => sendCommand("set_mid_offset", { id: servoId, offset: parseInt($("p-offset").value, 10) || 0 }));
  $("p-csd").addEventListener("click", () => sendCommand("write_init_pos", { id: servoId }));
  $("p-csm").addEventListener("click", () => sendCommand("clear_init_pos", { id: servoId }));
  $("p-csr").addEventListener("click", () => sendCommand("restore_init_pos", { id: servoId }));
  $("p-min").addEventListener("click", () => sendCommand("write_min", { id: servoId }));
  $("p-max").addEventListener("click", () => sendCommand("write_max", { id: servoId }));

  bindSlider("p-stb", "p-stb-val", () => {});
  $("p-stb-read").addEventListener("click", () => sendCommand("read_protect", { id: servoId }));
  $("p-stb-set").addEventListener("click", () => sendCommand("write_protect", { id: servoId, value: parseInt($("p-stb").value, 10) }));
  $("p-pid-set").addEventListener("click", () => sendCommand("write_pid", { id: servoId, kp: parseInt($("p-kp").value, 10) || 0, ki: parseInt($("p-ki").value, 10) || 0 }));
  $("p-ulm").addEventListener("click", () => sendCommand("torque_off_free", { id: servoId }));
  $("p-ln").addEventListener("click", () => sendCommand("led_on", { id: servoId }));
  $("p-lf").addEventListener("click", () => sendCommand("led_off", { id: servoId }));

  $("p-baud-set").addEventListener("click", () => {
    if (confirm(`确定把 ID ${servoId} 的波特率改为 ${info.baud_rates[parseInt($("p-baud").value, 10) - 1]} 吗？\n修改后需重新连接串口。`)) {
      sendCommand("write_baud", { id: servoId, index: parseInt($("p-baud").value, 10) });
    }
  });
  $("p-cle0").addEventListener("click", () => {
    if (confirm(`确定恢复 ID ${servoId} 的出厂设置（保留 ID）吗？`)) sendCommand("factory_keep_id", { id: servoId });
  });
  $("p-cle").addEventListener("click", () => {
    if (confirm(`确定恢复 ID ${servoId} 的全部出厂设置吗？\nID 将被重置为 000。`)) sendCommand("factory", { id: servoId });
  });

  // ===== 页 4：多机同步 =====
  function renderSync() {
    const list = $("sync-list");
    list.innerHTML = "";
    syncRows.forEach((row, i) => {
      const div = document.createElement("div");
      div.className = "sync-row";
      div.innerHTML =
        `<span>#${i + 1}</span><span>ID</span>` +
        `<input type="number" class="sr-id" min="0" max="254" value="${row.id}" style="width:54px;">` +
        `<span>PWM</span><input type="number" class="sr-pwm" min="500" max="2500" value="${row.pwm}" style="width:60px;">` +
        `<span>T</span><input type="number" class="sr-time" min="0" max="9999" value="${row.time}" style="width:60px;">` +
        `<button class="small sr-del">删除</button>`;
      list.appendChild(div);
      div.querySelector(".sr-id").addEventListener("change", (e) => { row.id = clamp(parseInt(e.target.value, 10) || 0, 0, 254); refreshSyncPreview(); });
      div.querySelector(".sr-pwm").addEventListener("change", (e) => { row.pwm = clamp(parseInt(e.target.value, 10) || 1500, 500, 2500); refreshSyncPreview(); });
      div.querySelector(".sr-time").addEventListener("change", (e) => { row.time = clamp(parseInt(e.target.value, 10) || 0, 0, 9999); refreshSyncPreview(); });
      div.querySelector(".sr-del").addEventListener("click", () => { syncRows.splice(i, 1); renderSync(); refreshSyncPreview(); });
    });
    refreshSyncPreview();
  }
  function refreshSyncPreview() {
    previewAction("sync", { moves: syncRows }, (c) => { $("sync-preview").textContent = c || "—"; });
  }
  $("sync-add").addEventListener("click", () => { syncRows.push({ id: syncRows.length, pwm: 1500, time: 1000 }); renderSync(); });
  $("sync-mid").addEventListener("click", () => { syncRows.forEach((r) => (r.pwm = 1500)); renderSync(); });
  $("sync-clear").addEventListener("click", () => { syncRows = []; renderSync(); });
  $("sync-send").addEventListener("click", () => {
    socket.emit("send_command", { action: "sync", params: { moves: syncRows } });
  });

  // ===== 页 5：原始指令 =====
  const rawExamples = [
    ["控制到中位 1000ms", "move", { id: 0, pwm: 1500, time: 1000 }],
    ["释放扭矩", "torque_off", { id: 0 }],
    ["恢复扭矩", "torque_on", { id: 0 }],
    ["读取位置", "read_position", { id: 0 }],
    ["读取工作模式", "read_mode", { id: 0 }],
    ["读取固件版本", "read_version", { id: 0 }],
    ["读取电压温度", "read_temp_voltage", { id: 0 }],
    ["读取保护值", "read_protect", { id: 0 }],
    ["RGB 灯开启", "led_on", { id: 0 }],
    ["RGB 灯关闭", "led_off", { id: 0 }],
  ];
  function renderExamples() {
    const box = $("raw-examples");
    box.innerHTML = "";
    rawExamples.forEach(([label, action, params]) => {
      const b = document.createElement("button");
      b.className = "small";
      b.style.margin = "2px";
      b.textContent = label;
      b.addEventListener("click", () => {
        previewAction(action, Object.assign({ id: servoId }, params), (c) => {
          const ta = $("raw-input");
          ta.value = ta.value ? ta.value + "\n" + c : c;
        });
      });
      box.appendChild(b);
    });
  }
  $("raw-send").addEventListener("click", () => socket.emit("send_raw", { text: $("raw-input").value }));
  $("raw-clear").addEventListener("click", () => ($("raw-input").value = ""));

  // ===== 日志 =====
  let logCount = 0;
  function addLogItem(item) {
    const list = $("log-list");
    const div = document.createElement("div");
    div.className = "log-item " + (item.dim ? "log-dim" : (item.tx ? "log-tx" : "log-rx"));
    const prefix = item.dim ? "      " : (item.tx ? "TX ➜ " : "RX ⬅ ");
    div.innerHTML = `<span class="log-time">${item.time}</span>${prefix}${escapeHtml(item.text)}`;
    list.appendChild(div);
    logCount++;
    $("log-count").textContent = "通信日志（" + logCount + "）";
    if ($("log-autoscroll").checked) list.scrollTop = list.scrollHeight;
  }
  function escapeHtml(s) {
    return s.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]));
  }
  socket.on("log", addLogItem);
  socket.on("log_cleared", () => { $("log-list").innerHTML = ""; logCount = 0; $("log-count").textContent = "通信日志（0）"; });

  $("log-clear").addEventListener("click", () => socket.emit("clear_log"));
  $("log-hex").addEventListener("change", (e) => socket.emit("set_log_options", { hex: e.target.checked }));
  $("log-parsed").addEventListener("change", (e) => socket.emit("set_log_options", { show_parsed: e.target.checked }));

  // ===== 初始化 =====
  fetch("/api/command_info")
    .then((r) => r.json())
    .then((d) => {
      info = d;
      // 波特率下拉
      const bs = $("baud-select");
      d.baud_rates.forEach((rate, i) => {
        const o = document.createElement("option");
        o.value = i + 1; o.text = rate.toString();
        if (i + 1 === baudIndex) o.selected = true;
        bs.appendChild(o);
      });
      // 模式下拉
      const ms = $("p-mode");
      d.mode_labels.forEach((label, i) => {
        const o = document.createElement("option");
        o.value = i + 1; o.text = label;
        ms.appendChild(o);
      });
      // 波特率（参数页）
      const pb = $("p-baud");
      d.baud_labels.forEach((label, i) => {
        const o = document.createElement("option");
        o.value = i + 1; o.text = label;
        pb.appendChild(o);
      });
      updateConnText();
      refreshControlPreview();
      refreshIdPreview();
    });

  refreshPorts();
  renderSync();
  renderExamples();
  setServoId(servoId);   // 恢复上次保存的舵机 ID
})();
