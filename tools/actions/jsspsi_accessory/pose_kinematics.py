#!/usr/bin/env python3
"""离线运动学公共库。捕获、身体差表、蒙皮目录由调用者显式传入。

相位 p=(u-.126)/.189，周期 1.071063 s。局部旋转 xyzw、位置 m。
FK 包含完整祖先；端点为蒙皮链末梢的定位标记，不统计末端自身旋转。
"""
from pathlib import Path
import csv
import hashlib
import json
import re
import struct
import sys
import numpy as np
from scipy.interpolate import CubicSpline

sys.dont_write_bytecode = True
from capture_reader import Capture

A, B, CLIP = .126, .315, 5.667
SPAN = B - A
PERIOD = SPAN * CLIP
GROUPS = {0: '尾巴', 1: '后发', 2: '两袖', 3: '袖飘带', 4: '前发', 5: '裙子与裙飘带', 6: '右臂'}
SOLVER = {'L_collar_01_jnt', 'R_collar_01_jnt', 'ribbonH_01_jnt', 'L_clothA_01_jnt', 'R_clothA_01_jnt', 'L_ribbonG_01_jnt', 'R_ribbonG_01_jnt', 'ribbonI_01_jnt'}
TIPS = {
    0: ['tail_10_jnt'],
    1: [f'{s}_hairE_10_jnt' for s in 'LR'],
    2: [f'{s}_sleeveD_07_jnt' for s in 'LR'],
    3: ['ribbonF_06_jnt', 'ribbonB_05_jnt', 'ribbonC_07_jnt'],
    4: ['M_hairA_03_jnt'] + [f'{s}_hairB_03_jnt' for s in 'LR'] + [f'{s}_hairC_04_jnt' for s in 'LR'],
    5: [f'{s}_skirt{c}_05_jnt' for s in 'LR' for c in 'ABC'] + [f'{s}_ribbonA_04_jnt' for s in 'LR'],
    6: ['Bip001_R_Hand'],
}


def norm(q):
    return q / np.linalg.norm(q, axis=-1, keepdims=True)


def conj(q):
    return q * np.array([-1, -1, -1, 1])


def mul(a, b):
    ax, ay, az, aw = np.moveaxis(a, -1, 0)
    bx, by, bz, bw = np.moveaxis(b, -1, 0)
    return np.stack([aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx,
                     aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz], axis=-1)


def rotate(q, p):
    t = 2 * np.cross(q[..., :3], p)
    return p + q[..., 3:] * t + np.cross(q[..., :3], t)


def logq(q):
    q = norm(q)
    q = np.where(q[..., 3:] < 0, -q, q)
    v = np.linalg.norm(q[..., :3], axis=-1, keepdims=True)
    return q[..., :3] * (2*np.arctan2(v, q[..., 3:]) / np.maximum(v, 1e-30))


def expq(v):
    r = np.linalg.norm(v, axis=-1, keepdims=True)
    return np.concatenate([v * (.5*np.sinc(r/(2*np.pi))), np.cos(r/2)], axis=-1)


def angle(a, b):
    return np.linalg.norm(logq(mul(a, conj(b))), axis=-1) * 180/np.pi


def slerp(a, b, f, module=False):
    f = np.asarray(f)
    while f.ndim < a.ndim:
        f = f[..., None]
    dot = np.sum(a*b, axis=-1, keepdims=True)
    b = np.where(dot < 0, -b, b)
    dot = np.clip(np.abs(dot), 0, 1)
    theta = np.arccos(dot)
    den = np.maximum(np.sin(theta), 1e-30)
    result = (np.sin((1-f)*theta)*a + np.sin(f*theta)*b)/den
    return norm(np.where(dot > (.9995 if module else .999999999), (1-f)*a+f*b, result))


def smooth5(x):
    return x**3 * (10 - 15*x + 6*x*x)


def dump(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    def encode(v):
        if isinstance(v, np.ndarray): return v.tolist()
        if isinstance(v, np.generic): return v.item()
        if isinstance(v, Path): return str(v)
        raise TypeError(type(v))
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, default=encode, allow_nan=False)+'\n')


def tsv(path, rows):
    if not rows: return
    with Path(path).open('w') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]), delimiter='\t')
        w.writeheader()
        w.writerows(rows)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_seam(path, state_hash):
    data = Path(path).read_bytes()
    assert data[:8] == b'BESEAM03'
    count, = struct.unpack_from('<I', data, 8)
    offset, found = 12, None
    for _ in range(count):
        h, a, b, n = struct.unpack_from('<iffI', data, offset)
        offset += 16
        bones = {}
        for _ in range(n):
            ln, = struct.unpack_from('<H', data, offset)
            offset += 2
            name = data[offset:offset+ln].decode('utf-8')
            offset += ln
            v = np.array(struct.unpack_from('<7f', data, offset))
            offset += 28
            bones[name] = (v[:4], v[4:])
        if h == state_hash:
            found = (a, b, bones)
    assert offset == len(data)
    if found is None: raise ValueError(f'差表没有状态 {state_hash}')
    return found


def group_for(name):
    if re.fullmatch(r'tail_\d\d_jnt', name): return 0
    if re.fullmatch(r'[LR]_hairE_\d\d_jnt', name): return 1
    if re.fullmatch(r'[LR]_sleeveD_\d\d_jnt', name): return 2
    if re.fullmatch(r'ribbon[FBC]_\d\d_jnt', name): return 3
    if re.fullmatch(r'(M_hairA|[LR]_hair[BC])_\d\d_jnt', name): return 4
    if re.fullmatch(r'[LR]_(skirt[ABC]|ribbonA)_\d\d_jnt', name): return 5
    if re.fullmatch(r'Bip001_R_(Clavicle|UpperArm|Forearm|ForeTwist|ForeTwist1|Hand)(_[tyz]+_(minus|plus))?', name): return 6
    return -1


class Rig:
    def __init__(self, capture, seam, state_hash, catalog):
        self.capture = Path(capture)
        c = Capture(str(capture))
        assert np.all(np.diff(c.time) > 0), '动画时间必须严格递增；不能按 seconds 去重'
        self.u, self.seconds = c.time, c.seconds
        ids = [i for i, g in enumerate(c.groups) if g == 0]
        ids.sort(key=lambda i: (c.paths[i].count('/'), i))
        self.paths = [c.paths[i] for i in ids]
        self.names = [p.rsplit('/', 1)[-1] for p in self.paths]
        self.nm = {n: i for i, n in enumerate(self.names)}
        ip = {p: i for i, p in enumerate(self.paths)}
        self.parent = np.array([-1 if p == '.' else ip[p.rsplit('/', 1)[0] if '/' in p else '.'] for p in self.paths])
        assert all(p < i for i, p in enumerate(self.parent))
        self.q, self.p = norm(c.rotation[:, ids]), c.position[:, ids]
        self.palette = set(n for v in json.loads(Path(catalog).read_text())['components'].values() for n in v['bone_names'])
        assert len(self.palette) == 375
        assert all(self.names.count(n) == 1 for n in self.palette)
        self.skin = np.array([i for i, n in enumerate(self.names) if n in self.palette])
        self.group = np.array([group_for(n) for n in self.names])
        self.bones = {g: np.flatnonzero(self.group == g) for g in GROUPS}
        self.tips = {g: np.array([self.nm[n] for n in ns]) for g, ns in TIPS.items()}
        self.acc = np.concatenate(list(self.bones.values()))
        assert len(set(self.acc)) == len(self.acc)
        assert not (set(self.names[i] for i in self.acc) & SOLVER)
        assert not any('IK_' in self.names[i] or '_Collider' in self.names[i] for i in self.acc)
        self.dq = np.zeros((len(ids), 4)); self.dq[:, 3] = 1
        self.dp = np.zeros((len(ids), 3))
        self.correct = np.zeros(len(ids), dtype=bool)
        if seam is not None:
            a, b, table = load_seam(seam, state_hash)
            assert abs(a-A) < 1e-6 and abs(b-B) < 1e-6
            for i, n in enumerate(self.names):
                if n in table:
                    dq, dp = table[n]
                    self.dq[i], self.dp[i] = norm(dq), dp
                    self.correct[i] = not (abs(dq[3]) > .999999 and np.sum(dp**2) < 1e-8)
        # 用 B 附近的姿态为每骨独立 SO(3) 对数图原点。
        # 只拟合所需局部区段，避免全冲刺的大旋转穿越 log 的 pi 分支。
        self.ref = self.raw([B])[0][0]
        sel = (self.u >= .09) & (self.u <= .40)
        self.su = self.u[sel]
        rv = logq(mul(conj(self.ref[None]), self.q[sel]))
        self.chart_max_deg = np.max(np.linalg.norm(rv[:, self.acc], axis=-1))*180/np.pi
        assert self.chart_max_deg < 175, '对数图接近分支，需要分段重建'
        self.spline = CubicSpline(self.su, np.concatenate([rv, self.p[sel]], axis=-1), axis=0)

    def raw(self, u):
        u = np.atleast_1d(u)
        assert np.all((u >= self.u[0]) & (u <= self.u[-1]))
        j = np.clip(np.searchsorted(self.u, u, side='right')-1, 0, len(self.u)-2)
        f = (u-self.u[j])/(self.u[j+1]-self.u[j])
        return slerp(self.q[j], self.q[j+1], f), self.p[j]+f[:, None, None]*(self.p[j+1]-self.p[j])

    def source(self, u):
        u = np.atleast_1d(u)
        z = self.spline(u)
        q, p = norm(mul(self.ref[None], expq(z[..., :3]))), z[..., 3:]
        # .40 之后只作收拢检查，回到原始 slerp/lerp；合成系数不使用 .40 之后的帧。
        outside = (u < self.su[0]) | (u > self.su[-1])
        if np.any(outside): q[outside], p[outside] = self.raw(u[outside])
        return q, p

    def corrected(self, phase, first=False):
        phase = np.atleast_1d(phase)
        q, p = self.raw(A+SPAN*phase)
        s = np.zeros_like(phase)
        hi = phase > .5
        x = 2*phase[hi]-1
        s[hi] = .5*x*x*(3-2*x)
        if not first:
            lo = phase < .5
            x = 2*phase[lo]
            s[lo] = -.5*(1-x*x*(3-2*x))
        ident = np.zeros_like(self.dq); ident[:, 3] = 1
        turn = slerp(np.broadcast_to(ident, q.shape), np.broadcast_to(self.dq, q.shape), abs(s), module=True)
        turn = np.where((s < 0)[:, None, None], conj(turn), turn)
        q[:, self.correct] = norm(mul(turn[:, self.correct], q[:, self.correct]))
        p[:, self.correct] += s[:, None, None]*self.dp[None, self.correct]
        return q, p

    def fk(self, pose, wanted=None):
        q, p = pose
        wq, wp = q.copy(), p.copy()
        if wanted is None:
            needed = range(len(self.names))
        else:
            needed = set(int(i) for i in wanted)
            for i in sorted(needed):
                while self.parent[i] >= 0:
                    i = int(self.parent[i]); needed.add(i)
            needed = sorted(needed)
        for i in needed:
            j = self.parent[i]
            if j >= 0:
                wp[:, i] = wp[:, j] + rotate(wq[:, j], p[:, i])
                wq[:, i] = mul(wq[:, j], q[:, i])
        return wq, wp


class Cycle:
    """每组一个 C1 bridge 或周期 Fourier；params 是 group -> 配置。"""
    def __init__(self, rig, params):
        self.rig = rig
        self.params = {int(g): dict(v) for g, v in params.items()}
        self.fourier = {}
        for g, cfg in self.params.items():
            if cfg['method'].startswith('fourier'):
                self.fourier[g] = self.fit_fourier(g, cfg)

    @staticmethod
    def design(x, k, derivative=0):
        cols = [np.ones_like(x) if derivative == 0 else np.zeros_like(x)]
        for n in range(1, k+1):
            w = 2*np.pi*n
            cols += [w**derivative*np.cos(w*x+derivative*np.pi/2), w**derivative*np.sin(w*x+derivative*np.pi/2)]
        return np.stack(cols, axis=-1)

    def fit_fourier(self, g, cfg):
        k = int(cfg.get('K', 3)); lam = cfg.get('ridge', .003)
        p3, pk = (cfg['u3']-B)/SPAN, (cfg['uk']-A)/SPAN
        x = np.r_[np.linspace(0, p3, max(20, int(240*p3))), np.linspace(pk, 1, max(20, int(240*(1-pk))))]
        u = np.where(x <= p3, B+SPAN*x, A+SPAN*x)
        y = self.rig.spline(u)[:, self.rig.bones[g]].reshape(len(x), -1)
        mat = self.design(x, k)
        penalty = np.array([0]+[n**4 for n in range(1, k+1) for _ in range(2)])
        h = mat.T@mat/len(x) + lam*np.diag(penalty)
        rhs = mat.T@y/len(x)
        if cfg.get('anchor', False):
            # 可选严格固定 B 的局部值、导数；无约束版本也保留作比较。
            con = np.vstack([self.design(np.array([0.]), k, d) for d in [0, 1]])
            target = np.stack([self.rig.spline(B, d)[self.rig.bones[g]].reshape(-1)*SPAN**d for d in [0, 1]])
            system = np.block([[h, con.T], [con, np.zeros((2, 2))]])
            coef = np.linalg.solve(system, np.vstack([rhs, target]))[:len(h)]
        else:
            coef = np.linalg.solve(h, rhs)
        return coef.reshape(2*k+1, len(self.rig.bones[g]), 6)

    def group_z(self, phase, g):
        phase = np.atleast_1d(phase)
        cfg = self.params[g]
        ix = self.rig.bones[g]
        p3, pk = (cfg['u3']-B)/SPAN, (cfg['uk']-A)/SPAN
        if g in self.fourier:
            return np.einsum('fk,kbc->fbc', self.design(phase, int(cfg.get('K', 3))), self.fourier[g])
        z = self.rig.spline(np.where(phase <= p3, B+SPAN*phase, A+SPAN*phase))[:, ix]
        middle = (phase > p3) & (phase < pk)
        if not np.any(middle): return z
        h = pk-p3
        x = ((phase[middle]-p3)/h)[:, None, None]
        y0 = self.rig.spline(cfg['u3'])[ix]
        y1 = self.rig.spline(cfg['uk'])[ix]
        v0 = self.rig.spline(cfg['u3'], 1)[ix]*SPAN*h
        v1 = self.rig.spline(cfg['uk'], 1)[ix]*SPAN*h
        a0 = self.rig.spline(cfg['u3'], 2)[ix]*(SPAN*h)**2
        a1 = self.rig.spline(cfg['uk'], 2)[ix]*(SPAN*h)**2
        method = cfg['method']
        if method == 'cubic':
            z[middle] = (2*x**3-3*x*x+1)*y0 + (x**3-2*x*x+x)*v0 + (-2*x**3+3*x*x)*y1 + (x**3-x*x)*v1
        elif method == 'quintic':
            c0, c1, c2 = y0, v0, a0/2
            d, v, a = y1-c0-c1-c2, v1-c1-2*c2, a1-2*c2
            c3, c4, c5 = 10*d-4*v+.5*a, -15*d+7*v-a, 6*d-3*v+.5*a
            z[middle] = c0+x*(c1+x*(c2+x*(c3+x*(c4+x*c5))))
        elif method.startswith('damped'):
            tau = cfg.get('tau', .22)
            e0 = y0+(v0*x+.5*a0*x*x)*np.exp(-(x/tau)**2)
            r = x-1
            e1 = y1+(v1*r+.5*a1*r*r)*np.exp(-(r/tau)**2)
            z[middle] = (1-smooth5(x))*e0+smooth5(x)*e1
        else:
            raise ValueError(method)
        return z

    def local(self, phase, groups=None):
        phase = np.atleast_1d(phase)
        groups = list(self.params) if groups is None else list(groups)
        ix = np.concatenate([self.rig.bones[g] for g in groups])
        z = np.concatenate([self.group_z(phase, g) for g in groups], axis=1)
        return ix, norm(mul(self.rig.ref[None, ix], expq(z[..., :3]))), z[..., 3:]

    def pose(self, phase, groups=None):
        q, p = self.rig.corrected(phase)
        ix, aq, ap = self.local(phase, groups)
        q[:, ix], p[:, ix] = aq, ap
        return q, p


def write_table(path, entries):
    """共用协议 BEACC001：每条骨元数据后紧跟 sample-major float32[N,M,7]。"""
    assert 1 <= len(entries) <= 8
    data = bytearray(b'BEACC001'+struct.pack('<II', 1, len(entries)))
    seen = set()
    for e in entries:
        names, groups, values = e['names'], e['groups'], e['data']
        n, m, seven = values.shape
        assert 8 <= n <= 4096 and 1 <= m <= 512 and seven == 7
        assert len(names) == len(groups) == m and len(set(names)) == m
        assert e['state_hash'] not in seen
        seen.add(e['state_hash'])
        assert np.all(np.isfinite(values))
        assert np.max(abs(np.linalg.norm(values[..., :4], axis=-1)-1)) < 1e-5
        data += struct.pack('<iffII', e['state_hash'], e.get('begin', B), e.get('target', A), n, m)
        for group, name in zip(groups, names):
            enc = name.encode('utf-8')
            assert 0 <= group <= 31 and 1 <= len(enc) <= 255
            data += struct.pack('<BH', int(group), len(enc)) + enc
        data += np.asarray(values, dtype='<f4').tobytes(order='C')
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(data)


def read_table(path):
    data = Path(path).read_bytes()
    assert data[:8] == b'BEACC001'
    version, count = struct.unpack_from('<II', data, 8)
    assert version == 1 and 1 <= count <= 8
    offset, entries = 16, []
    for _ in range(count):
        state_hash, begin, target, n, m = struct.unpack_from('<iffII', data, offset)
        offset += 20
        assert 8 <= n <= 4096 and 1 <= m <= 512
        assert np.isfinite(begin+target) and begin > target
        names, groups = [], []
        for _ in range(m):
            group, size = struct.unpack_from('<BH', data, offset)
            offset += 3
            assert group <= 31 and 1 <= size <= 255
            names.append(data[offset:offset+size].decode('utf-8')); groups.append(group)
            offset += size
        assert len(set(names)) == m
        values = np.frombuffer(data, dtype='<f4', count=n*m*7, offset=offset).copy().reshape(n, m, 7).astype(float)
        offset += n*m*28
        assert np.all(np.isfinite(values))
        assert np.max(abs(np.linalg.norm(values[..., :4], axis=-1)-1)) < 1e-5
        entries.append(dict(state_hash=state_hash, begin=begin, target=target, names=names, groups=groups, data=values))
    assert offset == len(data), '额外尾字节'
    assert len(set(e['state_hash'] for e in entries)) == count
    return entries


def sample_table(entry, phase):
    x = np.mod(np.atleast_1d(phase), 1)*len(entry['data'])
    k = np.floor(x).astype(int)
    f = x-k
    left, right = entry['data'][k], entry['data'][(k+1) % len(entry['data'])]
    return slerp(left[..., :4], right[..., :4], f), left[..., 4:]+f[:, None, None]*(right[..., 4:]-left[..., 4:])


def table_pose(rig, entry, phase):
    q, p = rig.corrected(np.mod(np.atleast_1d(phase), 1))
    ix = [rig.nm[n] for n in entry['names']]
    aq, ap = sample_table(entry, phase)
    q[:, ix], p[:, ix] = aq, ap
    return q, p
