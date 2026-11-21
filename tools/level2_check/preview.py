# Программный предпросмотр кэша геометрии Level 2: повторяет формулу шейдера игры (текстура * оттенок * (свет + локальная лампа), туман).
# Это НЕ скриншот с телефона: проверяет ориентацию текста/стрелок, ступени, рамки, видимость и локальный голубой свет.
import struct, sys, math, os
import numpy as np
from PIL import Image

D = sys.argv[1]
def load_scene(name):
    d = open(os.path.join(D, name), 'rb').read()
    n = struct.unpack_from('i', d, 0)[0]
    rec = np.dtype([('tex', '<i4'), ('v', [('x','<f4'),('y','<f4'),('z','<f4'),('u','<f4'),('vv','<f4'),('r','u1'),('g','u1'),('b','u1'),('a','u1')], (4,))])
    a = np.frombuffer(d, dtype=rec, count=n, offset=4)
    pos = np.stack([a['v']['x'], a['v']['y'], a['v']['z']], axis=-1)           # n,4,3
    uv = np.stack([a['v']['u'], a['v']['vv']], axis=-1)                        # n,4,2
    col = np.stack([a['v']['r'], a['v']['g'], a['v']['b'], a['v']['a']], axis=-1).astype(np.float32) / 255.0
    return a['tex'].copy(), pos.astype(np.float32), uv.astype(np.float32), col
def load_tex(i):
    p = os.path.join(D, 'tex%d.rgba' % i)
    if not os.path.exists(p): return None
    d = open(p, 'rb').read(); w, h = struct.unpack_from('ii', d, 0)
    return np.frombuffer(d, dtype=np.uint8, offset=8).reshape(h, w, 4).astype(np.float32) / 255.0
TEX = [load_tex(i) for i in range(10)]
REPEAT = {1, 2, 3}
scene = [load_scene('scene.bin')]

def sample(t, u, v):
    img = TEX[t]
    if img is None: return np.ones(u.shape + (4,), np.float32)
    h, w = img.shape[:2]
    if t in REPEAT: u = u - np.floor(u); v = v - np.floor(v)
    else: u = np.clip(u, 0, 0.99999); v = np.clip(v, 0, 0.99999)
    return img[(v * h).astype(int), (u * w).astype(int)]

LAMP = dict(pos=np.array([89.0, 3.5, 19.4]), rad=7.0, col=np.array([0.07, 0.26, 0.72]), box=(88.0, 90.0, 15.0, 37.8))
def render(name, cam, yaw, pitch, doors, W=960, H=540, view=34.0, fovy=75.0, lamp=True):
    cam = np.array(cam, np.float32)
    f = np.array([math.cos(pitch) * math.sin(yaw), math.sin(pitch), math.cos(pitch) * math.cos(yaw)], np.float32)
    r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r); u = np.cross(r, f)
    tf = 1.0 / math.tan(math.radians(fovy) / 2); sc = (H / 2) * tf
    img = np.zeros((H, W, 3), np.float32); zbuf = np.full((H, W), 1e9, np.float32)
    fogc = np.array([0.105, 0.135, 0.16], np.float32); fogd = 1.28 / (view * 1.4142)
    for (tex, pos, uv, col) in [scene[0], doors]:
        for i in range(len(tex)):
            P = pos[i]
            if np.min(np.linalg.norm(P - cam, axis=1)) > view * 1.5 + 8 and np.linalg.norm(P.mean(0) - cam) > view * 1.5 + 8: continue
            rel = P - cam; cz = rel @ f; cx = rel @ r; cy = rel @ u
            if np.all(cz < 0.08): continue
            for tri in ((0, 1, 2), (0, 2, 3)):
                pts = [(cx[k], cy[k], cz[k], uv[i][k][0], uv[i][k][1], k) for k in tri]
                # отсечение по ближней плоскости
                poly = []
                for a in range(3):
                    A, B = pts[a], pts[(a + 1) % 3]
                    ina, inb = A[2] >= 0.08, B[2] >= 0.08
                    if ina: poly.append(A + (None,))
                    if ina != inb:
                        t = (0.08 - A[2]) / (B[2] - A[2])
                        poly.append(tuple(A[j] + (B[j] - A[j]) * t for j in range(5)) + (A[5], ))
                        poly[-1] = poly[-1][:5] + (A[5], None) if False else poly[-1]
                if len(poly) < 3: continue
                for k in range(1, len(poly) - 1):
                    tv = [poly[0], poly[k], poly[k + 1]]
                    sx = np.array([W / 2 + v[0] / v[2] * sc for v in tv]); sy = np.array([H / 2 - v[1] / v[2] * sc for v in tv])
                    x0, x1 = int(max(0, math.floor(sx.min()))), int(min(W - 1, math.ceil(sx.max())))
                    y0, y1 = int(max(0, math.floor(sy.min()))), int(min(H - 1, math.ceil(sy.max())))
                    if x1 < x0 or y1 < y0: continue
                    den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
                    if abs(den) < 1e-9: continue
                    gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
                    l0 = ((sy[1] - sy[2]) * (gx - sx[2]) + (sx[2] - sx[1]) * (gy - sy[2])) / den
                    l1 = ((sy[2] - sy[0]) * (gx - sx[2]) + (sx[0] - sx[2]) * (gy - sy[2])) / den
                    l2 = 1 - l0 - l1
                    m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
                    if not m.any(): continue
                    iz = np.array([1.0 / v[2] for v in tv])
                    w0, w1, w2 = l0 * iz[0], l1 * iz[1], l2 * iz[2]; ws = w0 + w1 + w2
                    z = 1.0 / ws
                    ys, xs = np.nonzero(m); zz = z[ys, xs]
                    ok = zz < zbuf[y0 + ys, x0 + xs]
                    if not ok.any(): continue
                    ys, xs, zz = ys[ok], xs[ok], zz[ok]
                    a0, a1, a2 = w0[ys, xs] * zz, w1[ys, xs] * zz, w2[ys, xs] * zz
                    uu = a0 * tv[0][3] + a1 * tv[1][3] + a2 * tv[2][3]; vv = a0 * tv[0][4] + a1 * tv[1][4] + a2 * tv[2][4]
                    # цвет вершины (для вершин, созданных отсечением берём индекс исходной)
                    cc = np.zeros((len(zz), 4), np.float32)
                    for (aa, vtx) in ((a0, tv[0]), (a1, tv[1]), (a2, tv[2])):
                        cc += aa[:, None] * col[i][vtx[5]][None, :]
                    texel = sample(int(tex[i]), uu, vv)
                    keep = texel[:, 3] >= 0.05
                    wp = cam + (f[None, :] * zz[:, None]) + 0  # приближённая мировая позиция для расстояния/лампы
                    # точная мировая позиция по барицентрикам
                    wp = a0[:, None] * P[tri[0]] + a1[:, None] * P[tri[1]] + a2[:, None] * P[tri[2]] if True else wp
                    d = np.linalg.norm(wp - cam, axis=1)
                    light = cc[:, 3:4] * 0.95 * np.array([1.0, 0.97, 0.85]) + 0.0
                    extra = np.zeros_like(light)
                    if lamp:
                        dl = np.linalg.norm(wp - LAMP['pos'], axis=1); fall = np.clip(1 - dl / LAMP['rad'], 0, 1) ** 2
                        b = LAMP['box']; inb = (wp[:, 0] >= b[0]) & (wp[:, 0] <= b[1]) & (wp[:, 2] >= b[2]) & (wp[:, 2] <= b[3])
                        extra = (fall * inb)[:, None] * LAMP['col'][None, :]
                    colr = texel[:, :3] * cc[:, :3] * (light + extra + 0.02)
                    kf = np.clip(1 - np.exp(-(d * fogd) ** 2), 0, 1)[:, None]
                    colr = colr * (1 - kf) + fogc[None, :] * kf
                    sel = keep
                    img[y0 + ys[sel], x0 + xs[sel]] = colr[sel]; zbuf[y0 + ys[sel], x0 + xs[sel]] = zz[sel]
    out = np.clip(img ** (1 / 1.0) * 255 * 1.0, 0, 255).astype(np.uint8)
    Image.fromarray(out).save(os.path.join(D, name))
    print('saved', name)

closed = load_scene('doors_closed.bin'); opened = load_scene('doors_open.bin')
shots = {
 'v1_trunk_to_niche.png': ((80.0, 1.6, 43.4), math.atan2(9.0, -4.0), 0.0, closed),
 'v2_niche_front.png': ((89.0, 1.6, 42.0), math.pi, 0.0, closed),
 'v3_niche_close.png': ((89.0, 1.6, 39.4), math.pi, -0.02, closed),
 'v4_passage_open.png': ((89.0, 1.6, 37.0), math.pi, 0.0, opened),
 'v5_passage_far.png': ((89.0, 1.6 + 0.51 * 2, 27.0), math.pi, 0.0, opened),
 'v6_final_door.png': ((89.0, 1.6 + 1.53, 19.0), math.pi, 0.0, opened),
 'v7_arrow_north.png': ((31.0, 1.6, 43.0), math.pi, 0.0, closed),
 'v8_arrow_south.png': ((17.0, 1.6, 43.0), 0.0, 0.0, closed),
 'v9_trunk_start.png': ((9.0, 1.6, 42.0), math.pi / 2, 0.0, closed),
 'v10_pump.png': ((20.0, 1.6 - 0.51, 14.6), math.pi / 2 + 0.6, 0.0, closed),
 'v11_blood_wall.png': ((86.9, 1.6, 43.0), math.pi, 0.0, closed),
}
which = sys.argv[2:] or list(shots)
for k in which:
    c, y, p, dr = shots[k]
    render(k, c, y, p, dr, lamp=True)
