"""本地 Web GUI：加载 checkpoint，在网页上传图片测试模型。

用法（在项目根目录运行）:
    python -m src.webui --ckpt outputs/best.pt
    python -m src.webui --ckpt outputs/best.pt --port 8000 --device cuda

浏览器打开 http://127.0.0.1:8000 ，上传 256x256 灰度渲染图（或任意图片，
自动转灰度并缩放），模型预测几何体序列，页面展示重建渲染图与参数表。
光照方位/仰角/强度、地面样式可在左侧调节，用于对齐重建渲染。
"""

from __future__ import annotations

import argparse
import base64
import io
import json
from pathlib import Path

import numpy as np
import torch
from fastapi import FastAPI, File, Form, HTTPException
from fastapi.responses import HTMLResponse
from PIL import Image
from uvicorn import run as uvicorn_run

from src.model import build_model
from src.renderer import render_scene
from src.tokenizer import EOS, PAD, GeoTokenizer

ALBEDO = 0.7     # 预测物体固定反照率（与 infer.py 一致）
SEED = 0         # 渲染阴影/AO 采样随机源，固定保证可复现


def to_b64(img: np.ndarray) -> str:
    buf = io.BytesIO()
    Image.fromarray(img).save(buf, format="PNG")
    return base64.b64encode(buf.getvalue()).decode()


def azimuth_elevation_to_dir(az_deg: float, el_deg: float) -> list:
    """与 renderer.py 随机光照采样同一约定：方位角/仰角 -> 方向向量。"""
    az, el = np.radians(az_deg), np.radians(el_deg)
    d = [np.cos(el) * np.sin(az), np.sin(el), np.cos(el) * np.cos(az)]
    return [round(float(v), 4) for v in d]


class Predictor:
    def __init__(self, ckpt_path: str, device: str | None = None):
        self.device = torch.device(
            device or ("cuda" if torch.cuda.is_available() else "cpu"))
        ck = torch.load(ckpt_path, map_location=self.device, weights_only=False)
        self.cfg = ck["cfg"]
        self.tok = GeoTokenizer(self.cfg["tokenizer"]["n_bins"],
                                self.cfg["tokenizer"]["params"])
        self.model = build_model(self.cfg, self.tok)
        self.model.load_state_dict(ck["model"])
        self.model.to(self.device).eval()
        self.img_size = self.cfg["data"]["image_size"]
        info = (f"model loaded: {ckpt_path} (epoch {ck.get('epoch', '?')}, "
                f"device {self.device})")
        print(info)

    def preprocess(self, raw: bytes) -> torch.Tensor:
        img = Image.open(io.BytesIO(raw)).convert("L")
        if img.size != (self.img_size, self.img_size):
            img = img.resize((self.img_size, self.img_size), Image.BILINEAR)
        x = torch.from_numpy(np.asarray(img, dtype=np.float32) / 255.0)
        return ((x - 0.5) / 0.5)[None, None].to(self.device)

    @torch.no_grad()
    def predict(self, raw: bytes, light: dict, ground: dict) -> dict:
        x = self.preprocess(raw)
        tokens = self.model.generate(x, eos_id=EOS, pad_id=PAD)[0].tolist()

        objs = self.tok.decode_tokens(tokens)
        for o in objs:
            o.setdefault("albedo", ALBEDO)
        camera = self.tok.decode_camera(tokens)

        input_img = (x[0, 0].cpu().numpy() * 0.5 + 0.5) * 255.0
        recon = render_scene(objs, size=self.img_size, camera=camera,
                             light=light, ground=ground,
                             rng=np.random.default_rng(SEED))
        pair = np.concatenate([input_img.astype(np.uint8),
                               recon.astype(np.uint8)], axis=1)

        return {
            "objects": objs,
            "camera": {"eye": [round(v, 3) for v in camera["eye"]],
                       "target": [round(v, 3) for v in camera["target"]],
                       "fov_y_deg": round(camera["fov_y_deg"], 1)},
            "n_tokens": len(tokens),
            "pair_png": to_b64(pair),
        }


app = FastAPI(title="ImageToScene WebUI")
P: Predictor | None = None


@app.get("/", response_class=HTMLResponse)
def index():
    return HTML


@app.get("/api/ready")
def ready():
    if P is None:
        raise HTTPException(503, "model not loaded")
    return {"ok": True, "device": str(P.device)}


@app.post("/api/predict")
def predict(file: bytes = File(...),
            light_az: float = Form(45.0),
            light_el: float = Form(40.0),
            light_intensity: float = Form(0.75),
            ambient: float = Form(0.25),
            shadow_softness: float = Form(0.08),
            ground_style: str = Form("checker")):
    if P is None:
        raise HTTPException(503, "model not loaded")
    if ground_style not in ("checker", "plain"):
        raise HTTPException(422, "ground_style must be checker|plain")
    light = {"direction": azimuth_elevation_to_dir(light_az, light_el),
             "ambient": ambient, "intensity": light_intensity,
             "shadow_softness": shadow_softness, "shadow_samples": 4,
             "ao_strength": 0.5, "ao_samples": 4, "ao_distance": 1.2}
    ground = {"style": ground_style, "c0": 0.60, "c1": 0.42}
    try:
        result = P.predict(file, light, ground)
    except Exception as e:                                   # noqa: BLE001
        raise HTTPException(500, f"推理失败: {e}") from e
    result["light"] = light
    result["ground"] = ground
    return result


HTML = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ImageToScene 测试台</title>
<style>
  :root {
    --bg: #0b0f17; --panel: #131a26; --panel2: #1a2334; --line: #26314a;
    --text: #e6ecf7; --dim: #8b98b3; --accent: #4f8cff; --ok: #3ddc97;
  }
  * { box-sizing: border-box; margin: 0; }
  body {
    background: radial-gradient(1200px 600px at 70% -10%, #16233d 0%, var(--bg) 60%);
    color: var(--text); font: 14px/1.6 "Segoe UI", "Microsoft YaHei", sans-serif;
    min-height: 100vh; padding: 28px;
  }
  h1 { font-size: 20px; letter-spacing: .5px; }
  h1 span { color: var(--accent); }
  .sub { color: var(--dim); font-size: 12.5px; margin: 4px 0 20px; }
  .layout { display: grid; grid-template-columns: 340px 1fr; gap: 20px; max-width: 1280px; margin: 0 auto; }
  @media (max-width: 900px) { .layout { grid-template-columns: 1fr; } }
  .card {
    background: linear-gradient(180deg, var(--panel) 0%, var(--panel2) 100%);
    border: 1px solid var(--line); border-radius: 14px; padding: 18px;
  }
  .card h2 { font-size: 13px; color: var(--dim); text-transform: uppercase;
             letter-spacing: 1px; margin-bottom: 12px; }
  /* 上传区 */
  #drop {
    border: 1.5px dashed var(--line); border-radius: 10px; padding: 26px 12px;
    text-align: center; color: var(--dim); cursor: pointer; transition: .15s;
  }
  #drop:hover, #drop.over { border-color: var(--accent); color: var(--text); background: #18243c; }
  #drop b { color: var(--accent); }
  #file { display: none; }
  .thumb { max-width: 100%; max-height: 180px; border-radius: 8px; margin-top: 10px; display: none; }
  /* 控件 */
  .row { display: flex; align-items: center; gap: 10px; margin: 9px 0; }
  .row label { width: 76px; color: var(--dim); font-size: 12.5px; flex: none; }
  .row output { width: 46px; text-align: right; font-variant-numeric: tabular-nums; font-size: 12.5px; }
  input[type=range] { flex: 1; accent-color: var(--accent); }
  select {
    width: 100%; background: var(--panel2); color: var(--text);
    border: 1px solid var(--line); border-radius: 8px; padding: 7px 10px;
  }
  button {
    width: 100%; margin-top: 14px; padding: 11px; border: 0; border-radius: 10px;
    background: linear-gradient(90deg, #3f76f0, #4f8cff); color: #fff;
    font-size: 14.5px; font-weight: 600; cursor: pointer; transition: .15s;
  }
  button:hover { filter: brightness(1.12); }
  button:disabled { opacity: .5; cursor: wait; }
  /* 结果 */
  #result { display: none; }
  #pair { width: 100%; border-radius: 10px; border: 1px solid var(--line); }
  .meta { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 12px; }
  .chip {
    background: #182238; border: 1px solid var(--line); border-radius: 999px;
    padding: 3px 12px; font-size: 12.5px; color: var(--dim);
  }
  .chip b { color: var(--ok); font-weight: 600; }
  table { width: 100%; border-collapse: collapse; margin-top: 12px; font-size: 12.8px; }
  th, td { padding: 6px 9px; text-align: left; border-bottom: 1px solid var(--line); }
  th { color: var(--dim); font-weight: 500; white-space: nowrap; }
  td code { color: var(--accent); font-family: Consolas, monospace; }
  tr:first-child td { color: inherit; }
  .type { display: inline-block; background: #1c2a46; border-radius: 6px;
          padding: 1px 8px; font-size: 12px; color: var(--accent); }
  #err { color: #ff7a7a; margin-top: 10px; font-size: 13px; display: none; }
  #empty { color: var(--dim); text-align: center; padding: 60px 0; }
  .spin {
    display: inline-block; width: 14px; height: 14px; border: 2px solid #fff5;
    border-top-color: #fff; border-radius: 50%; animation: r 0.7s linear infinite;
    vertical-align: -2px; margin-right: 8px;
  }
  @keyframes r { to { transform: rotate(360deg); } }
</style>
</head>
<body>
<div class="layout">
  <div class="card">
    <h2>输入图片</h2>
    <div id="drop">拖拽图片到此处，或 <b>点击选择</b><br>
      <span style="font-size:12px">任意尺寸，自动转灰度并缩放到 256×256</span>
      <img id="thumb" class="thumb" alt="">
    </div>
    <input id="file" type="file" accept="image/*">

    <h2 style="margin-top:20px">渲染参数</h2>
    <div class="row"><label>光方位角</label><input id="az" type="range" min="0" max="360" value="45" step="1"><output id="az_v">45°</output></div>
    <div class="row"><label>光仰角</label><input id="el" type="range" min="5" max="85" value="40" step="1"><output id="el_v">40°</output></div>
    <div class="row"><label>光强</label><input id="it" type="range" min="0.2" max="1.2" value="0.75" step="0.05"><output id="it_v">0.75</output></div>
    <div class="row"><label>环境光</label><input id="am" type="range" min="0" max="0.6" value="0.25" step="0.05"><output id="am_v">0.25</output></div>
    <div class="row"><label>阴影柔和</label><input id="sh" type="range" min="0" max="0.2" value="0.08" step="0.01"><output id="sh_v">0.08</output></div>
    <div class="row"><label>地面</label>
      <select id="gs"><option value="checker">棋盘格</option><option value="plain">纯色</option></select>
    </div>

    <button id="go" disabled>加载模型后可用</button>
    <div id="err"></div>
  </div>

  <div class="card">
    <h2>结果（左：输入 · 右：预测几何体重建）</h2>
    <div id="empty">上传图片并点击「开始推理」</div>
    <div id="result">
      <img id="pair" alt="result">
      <div class="meta" id="chips"></div>
      <table id="tbl">
        <thead><tr><th>#</th><th>类型</th><th>位置 cx,cy,cz</th><th>四元数 x,y,z,w</th><th>尺寸</th></tr></thead>
        <tbody></tbody>
      </table>
    </div>
  </div>
</div>

<script>
const $ = id => document.getElementById(id);
const fileInput = $("file"), drop = $("drop"), thumb = $("thumb");
let file = null;

drop.onclick = () => fileInput.click();
drop.ondragover = e => { e.preventDefault(); drop.classList.add("over"); };
drop.ondragleave = () => drop.classList.remove("over");
drop.ondrop = e => { e.preventDefault(); drop.classList.remove("over"); setFile(e.dataTransfer.files[0]); };
fileInput.onchange = () => setFile(fileInput.files[0]);

function setFile(f) {
  if (!f || !f.type.startsWith("image/")) return;
  file = f;
  const url = URL.createObjectURL(f);
  thumb.src = url; thumb.style.display = "block";
}

for (const id of ["az","el","it","am","sh"])
  $(id).oninput = () => { $(id + "_v").textContent = $(id).value + (id==="az"||id==="el" ? "°" : ""); };

fetch("/api/ready").then(r => r.ok ? enable() : setTimeout(()=>location.reload(), 3000))
  .catch(() => setTimeout(()=>location.reload(), 3000));
function enable() { $("go").disabled = false; $("go").textContent = "开始推理"; }

$("go").onclick = async () => {
  if (!file) { showErr("请先选择图片"); return; }
  hideErr();
  $("go").disabled = true; $("go").innerHTML = "<span class='spin'></span>推理中…（1B 模型首次较慢）";
  const fd = new FormData();
  fd.append("file", file);
  fd.append("light_az", $("az").value);
  fd.append("light_el", $("el").value);
  fd.append("light_intensity", $("it").value);
  fd.append("ambient", $("am").value);
  fd.append("shadow_softness", $("sh").value);
  fd.append("ground_style", $("gs").value);
  try {
    const r = await fetch("/api/predict", { method: "POST", body: fd });
    if (!r.ok) throw new Error((await r.json()).detail || r.statusText);
    render(await r.json());
  } catch (e) { showErr(e.message); }
  $("go").disabled = false; $("go").textContent = "开始推理";
};

function showErr(m) { $("err").textContent = m; $("err").style.display = "block"; }
function hideErr() { $("err").style.display = "none"; }

function fmt(v) { return Array.isArray(v) ? v.map(x => (+x).toFixed(2)).join(", ") : (+v).toFixed(2); }
function sizes(o) {
  if (o.type === "sphere") return "r=" + o.r.toFixed(2);
  if (["cylinder","cone","capsule"].includes(o.type)) return "r=" + o.r.toFixed(2) + ", h=" + o.h.toFixed(2);
  if (o.type === "box") return fmt([o.sx, o.sy, o.sz]);
  return fmt([o.rx, o.ry, o.rz]);
}

function render(d) {
  $("empty").style.display = "none";
  $("result").style.display = "block";
  $("pair").src = "data:image/png;base64," + d.pair_png;
  const c = d.camera;
  $("chips").innerHTML =
    `<span class="chip">物体数 <b>${d.objects.length}</b></span>` +
    `<span class="chip">生成 token <b>${d.n_tokens}</b></span>` +
    `<span class="chip">预测相机 eye <b>(${fmt(c.eye)})</b></span>` +
    `<span class="chip">fov <b>${c.fov_y_deg}°</b></span>`;
  const tb = $("tbl").querySelector("tbody");
  tb.innerHTML = d.objects.map((o, i) =>
    `<tr><td>${i + 1}</td><td><span class="type">${o.type}</span></td>` +
    `<td>(${fmt([o.cx, o.cy, o.cz])})</td><td>(${fmt(o.q)})</td>` +
    `<td>${sizes(o)}</td></tr>`).join("") ||
    `<tr><td colspan="5" style="color:var(--dim)">未解码出几何体</td></tr>`;
}
</script>
</body>
</html>"""


def main():
    ap = argparse.ArgumentParser(description="ImageToScene 本地 Web 测试台")
    ap.add_argument("--ckpt", default="outputs/best.pt")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--device", default=None, help="cuda / cpu（默认自动）")
    args = ap.parse_args()

    if not Path(args.ckpt).exists():
        raise SystemExit(f"checkpoint 不存在: {args.ckpt}")
    global P
    P = Predictor(args.ckpt, args.device)

    # 预热一次，避免首个请求等待 CUDA kernel 初始化
    print("warming up ...")
    dummy = torch.zeros(1, 1, P.img_size, P.img_size, device=P.device)
    with torch.no_grad():
        P.model.encode_image(dummy)
    print(f"ready -> http://{args.host}:{args.port}")

    uvicorn_run(app, host=args.host, port=args.port, log_level="warning")


if __name__ == "__main__":
    main()
