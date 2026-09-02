"""numpy ray-casting 渲染器（Unity 风格坐标：左手系，+Y 向上，+Z 为前）。

支持的几何体（均支持四元数旋转 q=[x,y,z,w]，w>=0 规范）:
    box      : 长方体 (cx,cy,cz,q, sx,sy,sz)
    sphere   : 球体   (cx,cy,cz,q, r)                 旋转不变，q 恒等
    cylinder : 圆柱   (cx,cy,cz,q, r,h)               局部轴向 +Y
    ellipsoid: 椭球   (cx,cy,cz,q, rx,ry,rz)          局部半轴
    cone     : 圆锥   (cx,cy,cz,q, r,h)               底面局部 -Y，尖端 +Y
    capsule  : 胶囊   (cx,cy,cz,q, r,h)               h 为圆柱段高度

光照: 随机方向光 + Lambert 漫反射；
      硬/软阴影（光源方向锥形采样 + shadow ray）；
      AO（法线半球采样遮挡，物体与地面均参与遮挡）。
地面: 棋盘格 / 纯色（白、灰等），仅作背景，不进入标签。
所有几何体均为凸体，shadow/AO 射线沿法线偏移即可避免自相交。
"""

from __future__ import annotations

import numpy as np

IMG_SIZE = 256
UP = np.array([0.0, 1.0, 0.0])
EPS = 1e-3
BG = 0.14

DEFAULT_CAMERA = {"eye": [0.0, 1.9, 4.4], "target": [0.0, 0.55, 0.0],
                  "fov_y_deg": 50.0}
DEFAULT_LIGHT = {"direction": [0.45, 0.85, 0.35], "ambient": 0.25,
                 "intensity": 0.75, "shadow_softness": 0.0,
                 "shadow_samples": 4, "ao_strength": 0.0,
                 "ao_samples": 4, "ao_distance": 1.2}
DEFAULT_GROUND = {"style": "checker", "c0": 0.60, "c1": 0.42}


# ============================== 四元数 ==============================
def quat_rotate(q, v):
    """四元数 q=[x,y,z,w] 旋转向量 v (...,3)，向量化。"""
    qv = np.asarray(q[:3], dtype=np.float64)
    qw = float(q[3])
    t = 2.0 * np.cross(qv, v)
    return v + qw * t + np.cross(qv, t)


def quat_inv_rotate(q, v):
    qi = (-q[0], -q[1], -q[2], q[3])
    return quat_rotate(qi, v)


def random_quaternion(rng: np.random.Generator) -> np.ndarray:
    """Shoemake 均匀随机单位四元数，规范 w >= 0。"""
    u1, u2, u3 = rng.uniform(0.0, 1.0, 3)
    s1, s2 = np.sqrt(1.0 - u1), np.sqrt(u1)
    a, b = 2.0 * np.pi * u2, 2.0 * np.pi * u3
    q = np.array([s1 * np.sin(a), s1 * np.cos(a), s2 * np.sin(b), s2 * np.cos(b)])
    if q[3] < 0.0:
        q = -q
    return q


# ============================== 相机 ==============================
def camera_rays(camera: dict, size: int):
    """Unity 风格相机光线：right = up × forward，up = forward × right。

    返回每像素光线原点 o (H,W,3) 与单位方向 d (H,W,3)。
    像素 x 增大 -> 世界 +X（右），y 增大（图像上方）-> 世界 +Y（上）。
    """
    eye = np.asarray(camera["eye"], dtype=np.float64)
    target = np.asarray(camera["target"], dtype=np.float64)
    fov = float(camera.get("fov_y_deg", 50.0))

    f = target - eye
    f = f / np.linalg.norm(f)
    r = np.cross(UP, f)                    # Unity: 右 = up × forward
    r = r / np.linalg.norm(r)
    u = np.cross(f, r)

    tan_half = np.tan(np.deg2rad(fov / 2.0))
    ys, xs = np.mgrid[0:size, 0:size]
    ndc_x = (xs + 0.5) / size * 2 - 1      # 左 -> 右
    ndc_y = 1 - (ys + 0.5) / size * 2      # 上 -> 下

    d = (ndc_x[..., None] * tan_half * r
         + ndc_y[..., None] * tan_half * u
         + f)
    d = d / np.linalg.norm(d, axis=-1, keepdims=True)
    o = np.broadcast_to(eye, d.shape).copy()
    return o, d


# ============================== 局部空间求交 ==============================
# 约定：o、d 已变换到物体局部空间（中心为原点、旋转已消除）。
def _sphere_local(o, d, r):
    b = np.sum(o * d, axis=-1)
    c = np.sum(o * o, axis=-1) - r * r
    disc = b * b - c
    t = np.where(disc > 0.0, -b - np.sqrt(np.maximum(disc, 0.0)), np.inf)
    t = np.where(t > EPS, t, np.inf)
    n = np.zeros_like(d)
    hit = np.isfinite(t)
    if hit.any():
        n[hit] = (o + t[..., None] * d)[hit] / r
    return t, n


def _box_local(o, d, sx, sy, sz):
    half = np.array([sx / 2, sy / 2, sz / 2])
    inv = 1.0 / np.where(np.abs(d) < 1e-12, 1e-12, d)
    tb0 = (-half[None, None, :] - o) * inv
    tb1 = (half[None, None, :] - o) * inv
    tmin = np.minimum(tb0, tb1)
    tmax = np.maximum(tb0, tb1)
    t_enter = tmin.max(axis=-1)
    t_exit = tmax.min(axis=-1)
    t = np.where((t_enter < t_exit) & (t_enter > EPS), t_enter, np.inf)
    n = np.zeros_like(d)
    hit = np.isfinite(t)
    if hit.any():
        axis = np.argmax(tmin == t_enter[..., None], axis=-1)
        ii, jj = np.nonzero(hit)
        a = axis[ii, jj]
        s = -np.sign(d[ii, jj, a])
        s = np.where(s == 0.0, 1.0, s)
        n[ii, jj, a] = s
    return t, n


def _ellipsoid_local(o, d, rx, ry, rz):
    rr = np.array([rx, ry, rz])
    a = np.sum((d / rr) ** 2, axis=-1)
    b = np.sum(o * d / rr ** 2, axis=-1)
    c = np.sum((o / rr) ** 2, axis=-1) - 1.0
    disc = b * b - a * c
    sq = np.sqrt(np.maximum(disc, 0.0))
    a = np.where(np.abs(a) < 1e-12, 1e-12, a)
    t = np.where(disc > 0.0, (-b - sq) / a, np.inf)
    t = np.where(t > EPS, t, np.inf)
    n = np.zeros_like(d)
    hit = np.isfinite(t)
    if hit.any():
        p = (o + t[..., None] * d)[hit]
        nl = p / (rr ** 2)
        n[hit] = nl / np.linalg.norm(nl, axis=-1, keepdims=True)
    return t, n


def _cylinder_local(o, d, r, h):
    hh = h / 2
    ox, oy, oz = o[..., 0], o[..., 1], o[..., 2]
    dx, dy, dz = d[..., 0], d[..., 1], d[..., 2]
    a = dx * dx + dz * dz
    a = np.where(a < 1e-12, 1e-12, a)
    b = ox * dx + oz * dz
    c = ox * ox + oz * oz - r * r
    disc = b * b - a * c
    sq = np.sqrt(np.maximum(disc, 0.0))
    t_side = np.where(disc > 0.0, (-b - sq) / a, np.inf)
    y_side = oy + t_side * dy
    ok = (t_side > EPS) & (np.abs(y_side) <= hh)
    t_side = np.where(ok, t_side, np.inf)

    t_cap = np.full_like(t_side, np.inf)
    cap_sign = np.zeros_like(t_side)
    for cap_y, sgn in ((-hh, -1.0), (hh, 1.0)):
        with np.errstate(divide="ignore", invalid="ignore"):
            tc = (cap_y - oy) / dy
        px = ox + tc * dx
        pz = oz + tc * dz
        okc = (tc > EPS) & (px * px + pz * pz <= r * r)
        tc = np.where(okc, tc, np.inf)
        better = tc < t_cap
        t_cap = np.where(better, tc, t_cap)
        cap_sign = np.where(better, sgn, cap_sign)

    t = np.minimum(t_side, t_cap)
    n = np.zeros_like(d)
    hit = np.isfinite(t)
    if hit.any():
        ii, jj = np.nonzero(hit)
        side = t_side[ii, jj] == t[ii, jj]
        px = ox[ii, jj] + t[ii, jj] * dx[ii, jj]
        pz = oz[ii, jj] + t[ii, jj] * dz[ii, jj]
        n[ii, jj, 0] = np.where(side, px / r, 0.0)
        n[ii, jj, 1] = np.where(side, 0.0, cap_sign[ii, jj])
        n[ii, jj, 2] = np.where(side, pz / r, 0.0)
    return t, n


def _cone_local(o, d, r, h):
    """圆锥：底面在局部 -h/2（半径 r），尖端在 +h/2。"""
    hh = h / 2
    k = r / h
    ox, oy, oz = o[..., 0], o[..., 1], o[..., 2]
    dx, dy, dz = d[..., 0], d[..., 1], d[..., 2]

    A = dx * dx + dz * dz - (k * dy) ** 2
    A = np.where(np.abs(A) < 1e-12, 1e-12, A)
    B = 2.0 * (ox * dx + oz * dz + (k * k) * dy * (hh - oy))
    C = ox * ox + oz * oz - (k * (hh - oy)) ** 2
    disc = B * B - 4.0 * A * C
    sq = np.sqrt(np.maximum(disc, 0.0))
    t_a = np.where(disc > 0.0, (-B - sq) / (2.0 * A), np.inf)
    t_b = np.where(disc > 0.0, (-B + sq) / (2.0 * A), np.inf)
    lo = np.minimum(t_a, t_b)
    hi = np.maximum(t_a, t_b)
    t_side = np.inf
    for cand in (lo, hi):                  # 检查两根（近根可能在锥面延伸段）
        y = oy + cand * dy
        ok = (cand > EPS) & (y >= -hh) & (y <= hh)
        t_side = np.minimum(t_side, np.where(ok, cand, np.inf))

    # 底盖
    with np.errstate(divide="ignore", invalid="ignore"):
        tc = (-hh - oy) / dy
    pcx = ox + tc * dx
    pcz = oz + tc * dz
    okc = (tc > EPS) & (pcx * pcx + pcz * pcz <= r * r)
    tc = np.where(okc, tc, np.inf)

    t = np.minimum(t_side, tc)
    n = np.zeros_like(d)
    hit = np.isfinite(t)
    if hit.any():
        ii, jj = np.nonzero(hit)
        px = ox[ii, jj] + t[ii, jj] * dx[ii, jj]
        py = oy[ii, jj] + t[ii, jj] * dy[ii, jj]
        pz = oz[ii, jj] + t[ii, jj] * dz[ii, jj]
        m = t_side[ii, jj] == t[ii, jj]
        kk = k * k
        nx = px.copy()
        ny = kk * (hh - py)
        nz = pz.copy()
        nl = np.sqrt(nx * nx + ny * ny + nz * nz)
        nl = np.where(nl < 1e-12, 1.0, nl)
        n[ii, jj, 0] = np.where(m, nx / nl, 0.0)
        n[ii, jj, 1] = np.where(m, ny / nl, -1.0)     # 底盖朝下
        n[ii, jj, 2] = np.where(m, nz / nl, 0.0)
    return t, n


def _capsule_local(o, d, r, h):
    """胶囊：圆柱段 |y| <= h/2，上下半球心 (0,±h/2,0)，半径 r。"""
    hh = h / 2
    ox, oy, oz = o[..., 0], o[..., 1], o[..., 2]
    dx, dy, dz = d[..., 0], d[..., 1], d[..., 2]

    # 圆柱侧面
    a = dx * dx + dz * dz
    a = np.where(a < 1e-12, 1e-12, a)
    b = ox * dx + oz * dz
    c = ox * ox + oz * oz - r * r
    disc = b * b - a * c
    sq = np.sqrt(np.maximum(disc, 0.0))
    t_side = np.where(disc > 0.0, (-b - sq) / a, np.inf)
    y_side = oy + t_side * dy
    ok = (t_side > EPS) & (np.abs(y_side) <= hh)
    t_side = np.where(ok, t_side, np.inf)

    # 上、下半球（只接受圆柱段之外的命中）
    def _cap(ocy):
        oy2 = oy - ocy
        b2 = ox * dx + oy2 * dy + oz * dz
        c2 = ox * ox + oy2 * oy2 + oz * oz - r * r
        disc2 = b2 * b2 - c2
        sq2 = np.sqrt(np.maximum(disc2, 0.0))
        t = np.where(disc2 > 0.0, -b2 - sq2, np.inf)
        yh = oy + t * dy
        okk = (t > EPS) & (np.abs(yh - ocy) >= hh)
        return np.where(okk, t, np.inf)

    t_up = _cap(hh)
    t_dn = _cap(-hh)

    t = np.minimum(np.minimum(t_side, t_up), t_dn)
    n = np.zeros_like(d)
    hit = np.isfinite(t)
    if hit.any():
        ii, jj = np.nonzero(hit)
        px = ox[ii, jj] + t[ii, jj] * dx[ii, jj]
        py = oy[ii, jj] + t[ii, jj] * dy[ii, jj]
        pz = oz[ii, jj] + t[ii, jj] * dz[ii, jj]
        m_side = t_side[ii, jj] == t[ii, jj]
        m_up = (~m_side) & (t_up[ii, jj] == t[ii, jj])
        nx = px / r
        ny = np.where(m_side, 0.0,
                      np.where(m_up, (py - hh) / r, (py + hh) / r))
        nz = pz / r
        n[ii, jj, 0] = nx
        n[ii, jj, 1] = ny
        n[ii, jj, 2] = nz
    return t, n


# ============================== 物体级求交 ==============================
def hit_object(o, d, obj):
    """世界空间求交（含四元数旋转）。返回 t (H,W), n (H,W,3)。"""
    typ = obj["type"]
    c = np.array([obj["cx"], obj["cy"], obj["cz"]])
    # inf*0 会产生 nan（未命中光线），后续比较恒为 False，数值无害，仅抑制警告
    with np.errstate(invalid="ignore", divide="ignore"):
        if typ == "sphere":                # 旋转不变
            return _sphere_local(o - c, d, obj["r"])
        q = np.asarray(obj.get("q", [0.0, 0.0, 0.0, 1.0]), dtype=np.float64)
        ol = quat_inv_rotate(q, o - c)
        dl = quat_inv_rotate(q, d)
        if typ == "box":
            t, nl = _box_local(ol, dl, obj["sx"], obj["sy"], obj["sz"])
        elif typ == "cylinder":
            t, nl = _cylinder_local(ol, dl, obj["r"], obj["h"])
        elif typ == "ellipsoid":
            t, nl = _ellipsoid_local(ol, dl, obj["rx"], obj["ry"], obj["rz"])
        elif typ == "cone":
            t, nl = _cone_local(ol, dl, obj["r"], obj["h"])
        elif typ == "capsule":
            t, nl = _capsule_local(ol, dl, obj["r"], obj["h"])
        else:
            raise ValueError(f"unknown object type: {typ}")
        return t, quat_rotate(q, nl)


def hit_ground(o, d, ground: dict):
    style = ground.get("style", "checker")
    c0 = float(ground.get("c0", 0.60))
    c1 = float(ground.get("c1", 0.42))
    dy = d[..., 1]
    with np.errstate(divide="ignore", invalid="ignore"):
        t = np.where(dy < -1e-6, -o[..., 1] / dy, np.inf)
    t = np.where(t > EPS, t, np.inf)
    n = np.zeros_like(d)
    n[..., 1] = 1.0
    if style == "checker":
        with np.errstate(invalid="ignore"):
            px = o[..., 0] + t * d[..., 0]
            pz = o[..., 2] + t * d[..., 2]
            ch = (np.floor(px * 1.2) + np.floor(pz * 1.2)) % 2
        alb = np.where(ch == 0, c0, c1)
        alb = np.where(np.isfinite(t), alb, 0.0)
    else:                                   # plain
        alb = np.full(t.shape, c0)
    return t, n, alb


# ============================== 遮挡查询（阴影/AO） ==============================
def _cone_sample_dirs(rng, axis, half_angle, n):
    """在以 axis 为轴、半角 half_angle 的锥内采样 n 个方向，返回 (n,3)。"""
    axis = np.asarray(axis, dtype=np.float64)
    axis = axis / np.linalg.norm(axis)
    if n <= 1 or half_angle <= 0.0:
        return axis[None, :]
    a = np.array([0.0, 1.0, 0.0]) if abs(axis[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    t1 = np.cross(a, axis)
    t1 = t1 / np.linalg.norm(t1)
    t2 = np.cross(axis, t1)
    dirs = []
    for _ in range(n):
        ct = rng.uniform(np.cos(half_angle), 1.0)
        st = np.sqrt(max(0.0, 1.0 - ct * ct))
        phi = rng.uniform(0.0, 2.0 * np.pi)
        dirs.append(st * np.cos(phi) * t1 + st * np.sin(phi) * t2 + ct * axis)
    return np.stack(dirs)


def _hemisphere_sample_dirs(rng, n, samples):
    """法线 n (H,W,3) 上半球均匀采样，返回 (samples,H,W,3)。"""
    H, W, _ = n.shape
    a = np.where(np.abs(n[..., 1:2]) < 0.9,
                 np.array([0.0, 1.0, 0.0]), np.array([1.0, 0.0, 0.0]))
    t1 = np.cross(a, n)
    nl = np.linalg.norm(t1, axis=-1, keepdims=True)
    t1 = t1 / np.where(nl < 1e-12, 1.0, nl)      # 零法线像素（背景）安全处理
    t2 = np.cross(n, t1)
    z = rng.uniform(0.0, 1.0, (samples, H, W))
    phi = rng.uniform(0.0, 2.0 * np.pi, (samples, H, W))
    r = np.sqrt(1.0 - z * z)
    lx, ly = r * np.cos(phi), r * np.sin(phi)
    out = np.empty((samples, H, W, 3))
    for s in range(samples):
        out[s] = (lx[s][..., None] * t1 + ly[s][..., None] * t2
                  + z[s][..., None] * n)
    return out


def _occluded_fraction(p, dirs, objects, ground, t_max):
    """p (H,W,3) 起点阵；dirs (S,3) 或 (S,H,W,3)；返回 (H,W) 平均可见率。"""
    S = dirs.shape[0]
    vis = np.zeros(p.shape[:2])
    for s in range(S):
        d = dirs[s]
        if d.ndim == 1:                     # (3,) -> 全场一致方向
            d = np.broadcast_to(d, p.shape)
        occ = np.zeros(p.shape[:2], dtype=bool)
        for obj in objects:
            t, _ = hit_object(p, d, obj)
            occ |= t < t_max
        if ground is not None:
            dy = d[..., 1]
            with np.errstate(divide="ignore", invalid="ignore"):
                tg = np.where(dy < -1e-6, -p[..., 1] / dy, np.inf)
            occ |= tg < t_max
        vis += (~occ).astype(np.float64)
    return vis / S


# ============================== 渲染入口 ==============================
def render_scene(objects, size: int = IMG_SIZE, camera: dict | None = None,
                 light: dict | None = None, ground: dict | None = None,
                 rng: np.random.Generator | None = None) -> np.ndarray:
    """渲染几何体场景 -> uint8 灰度图 (size, size)。

    camera: {"eye":[x,y,z], "target":[x,y,z], "fov_y_deg": float}  Unity 坐标
    light:  {"direction":[x,y,z], "ambient", "intensity",
             "shadow_softness"(弧度锥半角, 0=硬阴影), "shadow_samples",
             "ao_strength"(0~1), "ao_samples", "ao_distance"}
    ground: {"style":"checker"|"plain", "c0", "c1"}
    rng:    阴影/AO 采样的随机源（固定 seed 可复现）
    """
    camera = camera or DEFAULT_CAMERA
    light = {**DEFAULT_LIGHT, **(light or {})}
    ground = {**DEFAULT_GROUND, **(ground or {})}
    if rng is None:
        rng = np.random.default_rng(0)

    o, d = camera_rays(camera, size)

    # ---- 最近命中（z-buffer）----
    best_t = np.full((size, size), np.inf)
    best_n = np.zeros((size, size, 3))
    best_a = np.zeros((size, size))
    for obj in objects:
        t, n = hit_object(o, d, obj)
        m = t < best_t
        best_t[m] = t[m]
        best_n[m] = n[m]
        best_a[m] = obj.get("albedo", 0.7)
    tg, ng, ag = hit_ground(o, d, ground)
    m = tg < best_t
    best_t[m] = tg[m]
    best_n[m] = ng[m]
    best_a[m] = ag[m]

    hit = np.isfinite(best_t)
    if not hit.any():
        return np.full((size, size), int(np.clip(BG, 0, 1) * 255), np.uint8)

    p = o + best_t[..., None] * d
    # 沿法线偏移避免数值自相交；未命中像素（坐标为 inf）置 0，避免 nan 污染
    p_off = np.where(hit[..., None], p + best_n * (10.0 * EPS), 0.0)

    L = np.asarray(light["direction"], dtype=np.float64)
    L = L / np.linalg.norm(L)
    ndotl = np.clip(np.sum(best_n * L, axis=-1), 0.0, None)

    # ---- 阴影（软/硬）----
    soft = float(light.get("shadow_softness", 0.0))
    ns = int(light.get("shadow_samples", 4)) if soft > 0 else 1
    ldirs = _cone_sample_dirs(rng, L, soft, ns)          # (ns,3)
    vis = _occluded_fraction(p_off, ldirs, objects, ground, np.inf)

    # ---- AO ----
    ao_s = float(light.get("ao_strength", 0.0))
    if ao_s > 0:
        ns_ao = int(light.get("ao_samples", 4))
        ao_dist = float(light.get("ao_distance", 1.2))
        hdirs = _hemisphere_sample_dirs(rng, best_n, ns_ao)
        occ = 1.0 - _occluded_fraction(p_off, hdirs, objects, ground, ao_dist)
        ao = 1.0 - ao_s * occ
    else:
        ao = 1.0

    shade = light["ambient"] * ao + light["intensity"] * vis * ndotl
    gray = best_a * shade
    gray = np.where(hit, gray, BG)
    return (np.clip(gray, 0.0, 1.0) * 255).astype(np.uint8)


# ============================== 随机生成 ==============================
def random_scene(rng: np.random.Generator, max_objects: int = 6) -> list[dict]:
    """随机场景：2~max_objects 个带随机旋转的几何体（保证不穿地）。"""
    n = int(rng.integers(2, max_objects + 1))
    types = ["box", "sphere", "cylinder", "ellipsoid", "cone", "capsule"]
    weights = [0.20, 0.15, 0.16, 0.17, 0.16, 0.16]
    objs = []
    for _ in range(n):
        typ = str(rng.choice(types, p=weights))
        cx, cz = rng.uniform(-1.4, 1.4, size=2)
        if typ == "box":
            sx, sy, sz = rng.uniform(0.18, 1.0, size=3)
            params = {"sx": float(sx), "sy": float(sy), "sz": float(sz)}
            bound = 0.5 * float(np.sqrt(sx * sx + sy * sy + sz * sz))
        elif typ == "sphere":
            r = float(rng.uniform(0.16, 0.7))
            params = {"r": r}
            bound = r
        elif typ == "cylinder":
            r, h = float(rng.uniform(0.14, 0.6)), float(rng.uniform(0.35, 1.2))
            params = {"r": r, "h": h}
            bound = float(np.sqrt(r * r + (h / 2) ** 2))
        elif typ == "ellipsoid":
            rx, ry, rz = rng.uniform(0.15, 0.8, size=3)
            params = {"rx": float(rx), "ry": float(ry), "rz": float(rz)}
            bound = float(max(rx, ry, rz))
        elif typ == "cone":
            r, h = float(rng.uniform(0.15, 0.7)), float(rng.uniform(0.35, 1.3))
            params = {"r": r, "h": h}
            bound = float(np.sqrt(r * r + (h / 2) ** 2))
        else:  # capsule
            r, h = float(rng.uniform(0.12, 0.5)), float(rng.uniform(0.3, 1.0))
            params = {"r": r, "h": h}
            bound = h / 2 + r
        cy = float(rng.uniform(min(bound, 1.5), 1.9))
        q = [0.0, 0.0, 0.0, 1.0] if typ == "sphere" else random_quaternion(rng).tolist()
        obj = {"type": typ, "cx": float(cx), "cy": float(cy), "cz": float(cz),
               "q": q, **params, "albedo": float(rng.uniform(0.35, 0.95))}
        objs.append(obj)
    return objs


def random_camera(rng: np.random.Generator) -> dict:
    """随机相机：绕目标球面采样（仰角 8°~65°），roll 恒为 0。Unity 坐标。"""
    az = rng.uniform(0.0, 2.0 * np.pi)
    el = rng.uniform(np.deg2rad(8.0), np.deg2rad(65.0))
    dist = rng.uniform(3.5, 6.5)
    target = np.array([rng.uniform(-0.3, 0.3), rng.uniform(0.35, 0.9),
                       rng.uniform(-0.3, 0.3)])
    eye = target + dist * np.array([np.cos(el) * np.sin(az), np.sin(el),
                                    np.cos(el) * np.cos(az)])
    return {"eye": eye.tolist(), "target": target.tolist(),
            "fov_y_deg": float(rng.uniform(40.0, 65.0))}


def random_light(rng: np.random.Generator) -> dict:
    """随机光照：方向、强度、软/硬阴影、AO 强度。"""
    az = rng.uniform(0.0, 2.0 * np.pi)
    el = rng.uniform(np.deg2rad(15.0), np.deg2rad(80.0))
    direction = np.array([np.cos(el) * np.sin(az), np.sin(el),
                          np.cos(el) * np.cos(az)])
    soft = 0.0 if rng.uniform() < 0.3 else float(rng.uniform(0.04, 0.12))
    return {
        "direction": direction.tolist(),
        "ambient": float(rng.uniform(0.15, 0.35)),
        "intensity": float(rng.uniform(0.55, 0.9)),
        "shadow_softness": soft,           # 0 = 硬阴影
        "shadow_samples": 4,
        "ao_strength": float(rng.uniform(0.3, 0.7)),
        "ao_samples": 4,
        "ao_distance": 1.2,
    }


def random_ground(rng: np.random.Generator) -> dict:
    """随机地面：棋盘格（双色调）或纯色（白/灰）。"""
    if rng.uniform() < 0.5:
        c0 = float(rng.uniform(0.45, 0.80))
        c1 = float(max(0.10, c0 - rng.uniform(0.12, 0.30)))
        return {"style": "checker", "c0": c0, "c1": c1}
    if rng.uniform() < 0.5:                # 纯白
        c = float(rng.uniform(0.82, 0.95))
    else:                                  # 纯灰
        c = float(rng.uniform(0.30, 0.65))
    return {"style": "plain", "c0": c, "c1": c}
