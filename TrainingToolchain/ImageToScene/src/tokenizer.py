"""几何场景 <-> 离散 token 序列 的双向转换。

序列格式（每个场景）:
    BOS  [相机条件 7 token]  [物体的 11 个 token] x K  EOS  PAD...

相机条件 token (7):
    eye_x, eye_y, eye_z, target_x, target_y, target_z, fov_y
    相机为 Unity 坐标（左手系，+Y 向上）。相机作为模型输入条件，
    使随机视角训练/推理成为良定问题。

每个物体 11 个 token:
    [类别, cx, cy, cz, q0, q1, q2, q3, s0, s1, s2]
    旋转为归一化四元数 (x,y,z,w)，规范 w>=0 消除双覆盖歧义；
    解码后重新归一化。

尺寸槽位按类型解释:
    box      : s0,s1,s2 = sx, sy, sz
    sphere   : s0 = r
    cylinder : s0 = r, s1 = h
    ellipsoid: s0,s1,s2 = rx, ry, rz
    cone     : s0 = r, s1 = h
    capsule  : s0 = r, s1 = h      (h 为圆柱段高度)
连续参数线性量化到 n_bins 个 bin，每个 bin 对应一个 vocab token。
"""

from __future__ import annotations

# ---------------------------- 特殊 token ----------------------------
PAD = 0       # 填充
BOS = 1       # 序列开始
EOS = 2       # 序列结束
IGNORE = 3    # 无效参数槽位（loss 中被 mask 掉）

CLS_IDS = {"box": 4, "sphere": 5, "cylinder": 6,
           "ellipsoid": 7, "cone": 8, "capsule": 9}
CLS_NAMES = {v: k for k, v in CLS_IDS.items()}
N_SPECIAL = 10                                  # 4 特殊 + 6 类别

CAM_TOKENS = 7                                  # 相机条件 token 数
OBJ_TOKENS = 11                                 # 每个物体的 token 数

CAM_SLOTS = ["cam_ex", "cam_ey", "cam_ez",
             "cam_tx", "cam_ty", "cam_tz", "cam_fov"]

DEFAULT_RANGES = {
    # 相机（Unity 坐标）
    "cam_ex": (-7.0, 7.0), "cam_ey": (-7.0, 7.0), "cam_ez": (-7.0, 7.0),
    "cam_tx": (-2.0, 2.0), "cam_ty": (-2.0, 2.0), "cam_tz": (-2.0, 2.0),
    "cam_fov": (30.0, 90.0),
    # 物体位置
    "cx": (-2.0, 2.0), "cy": (0.0, 2.5), "cz": (-2.0, 2.0),
    # 四元数 (x,y,z,w)
    "q0": (-1.0, 1.0), "q1": (-1.0, 1.0), "q2": (-1.0, 1.0), "q3": (-1.0, 1.0),
    # 尺寸槽位
    "s0": (0.05, 1.5), "s1": (0.05, 1.5), "s2": (0.05, 1.5),
}

DEFAULT_CAMERA = {"eye": [0.0, 1.9, 4.4], "target": [0.0, 0.55, 0.0],
                  "fov_y_deg": 50.0}


def _size_values(obj: dict) -> list:
    """物体 -> [s0, s1, s2]，无效槽位为 IGNORE。"""
    t = obj["type"]
    if t == "box":
        return [obj["sx"], obj["sy"], obj["sz"]]
    if t == "sphere":
        return [obj["r"], IGNORE, IGNORE]
    if t in ("cylinder", "cone", "capsule"):
        return [obj["r"], obj["h"], IGNORE]
    if t == "ellipsoid":
        return [obj["rx"], obj["ry"], obj["rz"]]
    raise ValueError(f"unknown object type: {t}")


def _normalize_quat(q) -> list:
    q = [float(v) for v in q]
    n = sum(v * v for v in q) ** 0.5
    if n < 1e-8:
        return [0.0, 0.0, 0.0, 1.0]
    q = [v / n for v in q]
    if q[3] < 0.0:
        q = [-v for v in q]
    return q


class GeoTokenizer:
    def __init__(self, n_bins: int = 256, ranges: dict | None = None):
        self.n_bins = int(n_bins)
        self.ranges = dict(DEFAULT_RANGES)
        if ranges:
            self.ranges.update(ranges)
        self.bin_offset = N_SPECIAL
        self.vocab_size = N_SPECIAL + self.n_bins

    # ------------------------ 标量 <-> token ------------------------
    def quantize(self, slot: str, value: float) -> int:
        lo, hi = self.ranges[slot]
        t = (float(value) - lo) / (hi - lo)
        b = int(round(min(max(t, 0.0), 1.0) * (self.n_bins - 1)))
        return self.bin_offset + b

    def dequantize(self, token: int, slot: str) -> float:
        lo, hi = self.ranges[slot]
        b = min(max(int(token) - self.bin_offset, 0), self.n_bins - 1)
        return lo + b / (self.n_bins - 1) * (hi - lo)

    def is_bin(self, token: int) -> bool:
        return self.bin_offset <= int(token) < self.bin_offset + self.n_bins

    # ------------------------ 序列长度 ------------------------
    def seq_len(self, max_objects: int) -> int:
        """BOS + 7 相机 + max_objects * 11 + EOS"""
        return 2 + CAM_TOKENS + OBJ_TOKENS * max_objects

    # ------------------------ 场景 <-> 序列 ------------------------
    def encode_scene(self, camera: dict | None, objects: list[dict],
                     max_objects: int):
        """场景 -> (tokens, labels)。labels 中 PAD/IGNORE 位置为 -100。"""
        cam = camera or DEFAULT_CAMERA
        tokens = [BOS]
        cam_vals = [cam["eye"][0], cam["eye"][1], cam["eye"][2],
                    cam["target"][0], cam["target"][1], cam["target"][2],
                    cam["fov_y_deg"]]
        tokens += [self.quantize(slot, v) for slot, v in zip(CAM_SLOTS, cam_vals)]

        for obj in objects[:max_objects]:
            tokens.append(CLS_IDS[obj["type"]])
            tokens.append(self.quantize("cx", obj["cx"]))
            tokens.append(self.quantize("cy", obj["cy"]))
            tokens.append(self.quantize("cz", obj["cz"]))
            q = _normalize_quat(obj.get("q", [0.0, 0.0, 0.0, 1.0]))
            tokens += [self.quantize(f"q{i}", q[i]) for i in range(4)]
            for i, v in enumerate(_size_values(obj)):
                tokens.append(IGNORE if v == IGNORE else self.quantize(f"s{i}", v))

        tokens.append(EOS)
        total = self.seq_len(max_objects)
        tokens = tokens + [PAD] * (total - len(tokens))
        labels = [-100 if (t == PAD or t == IGNORE) else t for t in tokens]
        return tokens, labels

    def decode_camera(self, tokens) -> dict:
        """解析序列中的相机条件 token -> 相机参数 dict。"""
        tokens = [int(t) for t in tokens]
        if tokens and tokens[0] == BOS:
            tokens = tokens[1:]
        vals = []
        for slot, tok in zip(CAM_SLOTS, tokens[:CAM_TOKENS]):
            vals.append(self.dequantize(tok, slot) if self.is_bin(tok)
                        else sum(self.ranges[slot]) / 2.0)
        return {"eye": vals[0:3], "target": vals[3:6], "fov_y_deg": vals[6]}

    def decode_tokens(self, tokens) -> list[dict]:
        """token 序列 -> 几何体列表（跳过相机段，容忍非法 token）。"""
        tokens = [int(t) for t in tokens]
        objs: list[dict] = []
        i = 1 if tokens and tokens[0] == BOS else 0
        i += CAM_TOKENS                     # 跳过相机条件段
        while i + OBJ_TOKENS - 1 < len(tokens):
            head = tokens[i]
            if head in (EOS, PAD):
                break
            if head not in CLS_NAMES:       # 非法类别，跳一个 token 重同步
                i += 1
                continue
            typ = CLS_NAMES[head]
            raw = tokens[i + 1: i + OBJ_TOKENS]     # 3 pos + 4 quat + 3 size

            def take(slot, tok):
                return self.dequantize(tok, slot) if self.is_bin(tok) else None

            cx, cy, cz = (take("cx", raw[0]), take("cy", raw[1]),
                          take("cz", raw[2]))
            quat = [take(f"q{k}", raw[3 + k]) for k in range(4)]
            if None in (cx, cy, cz) or None in quat:
                i += OBJ_TOKENS
                continue
            obj = {"type": typ, "cx": cx, "cy": cy, "cz": cz,
                   "q": _normalize_quat(quat)}
            sizes = [take(f"s{k}", raw[7 + k]) for k in range(3)]
            if typ == "sphere":
                if sizes[0] is None:
                    i += OBJ_TOKENS
                    continue
                obj["r"] = sizes[0]
            elif typ in ("cylinder", "cone", "capsule"):
                if sizes[0] is None or sizes[1] is None:
                    i += OBJ_TOKENS
                    continue
                obj.update(r=sizes[0], h=sizes[1])
            else:  # box / ellipsoid
                if None in sizes:
                    i += OBJ_TOKENS
                    continue
                if typ == "box":
                    obj.update(sx=sizes[0], sy=sizes[1], sz=sizes[2])
                else:
                    obj.update(rx=sizes[0], ry=sizes[1], rz=sizes[2])
            objs.append(obj)
            i += OBJ_TOKENS
        return objs
