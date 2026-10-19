#!/usr/bin/env python3
"""读取 jsspsi_capture=true 的骨骼录制（格式见 native/modules/actions/capture.inl）。"""
import struct
import numpy as np


class Capture:
    def __init__(self, path):
        data = open(path, 'rb').read()
        if data[:8] != b'BECAP001':
            raise ValueError('不是 BECAP001 文件')
        bones, frames = struct.unpack_from('<II', data, 8)
        p = 16
        self.groups, self.paths = [], []
        for _ in range(bones):
            group = data[p]; length = struct.unpack_from('<H', data, p + 1)[0]
            self.groups.append(group)
            self.paths.append(data[p + 3:p + 3 + length].decode('utf-8', 'replace'))
            p += 3 + length
        stride = 2 + bones * 7
        values = np.frombuffer(data, dtype='<f4', count=frames * stride, offset=p).reshape(frames, stride)
        self.time = values[:, 0].astype(np.float64)      # 角色动画归一化时间
        self.seconds = values[:, 1].astype(np.float64)
        poses = values[:, 2:].reshape(frames, bones, 7).astype(np.float64)
        self.rotation = poses[..., :4]
        self.position = poses[..., 4:]
        self.groups = np.array(self.groups)
