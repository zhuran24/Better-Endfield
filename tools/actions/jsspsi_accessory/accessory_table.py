#!/usr/bin/env python3
"""BEACC001 分组、原生采样重采样及 8192 相位误差核对。"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
from pose_kinematics import read_table, write_table, dump


def build_native(work):
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    tool_dir = Path(__file__).resolve().parent
    source = tool_dir / 'native/accessory_table_tool.cpp'
    include_dir = tool_dir.parents[2] / 'native/modules/actions'
    for name in ['accessory_table.h', 'pose_policy.h']:
        if not (include_dir / name).is_file():
            raise FileNotFoundError(f'仓库模块头文件不存在：{include_dir / name}')
    binary = work / 'accessory_table_tool'
    # 每次运行重编译；临时文件也限制在指定工作目录内。
    temp = work / 'tmp'
    temp.mkdir(exist_ok=True)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-O2',
                    '-I', str(include_dir), str(source), '-o', str(binary)], check=True,
                   env=dict(os.environ, TMPDIR=str(temp)))
    return binary


def compare(native, reference, candidate):
    return json.loads(subprocess.check_output(
        [str(native), 'compare', str(reference), str(candidate)], text=True))


def resample(native, source, output, groups, samples):
    entries = read_table(source)
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    if all(len(e['data']) == samples for e in entries):
        # 原 N 的回退保留原始 float32 样本，包括其四元数舍入。
        for e in entries:
            keep = [i for i, g in enumerate(e['groups']) if g in groups]
            if not keep:
                raise ValueError('所选组没有骨')
            e['names'] = [e['names'][i] for i in keep]
            e['groups'] = [e['groups'][i] for i in keep]
            e['data'] = e['data'][:, keep]
        write_table(output, entries)
    else:
        subprocess.run([str(native), 'resample', str(source), str(output),
                        ','.join(map(str, groups)), str(samples)], check=True)


def parse_groups(text):
    values = [int(x) for x in text.split(',')]
    if not values or len(set(values)) != len(values) or any(x not in range(7) for x in values):
        raise argparse.ArgumentTypeError('groups 须为 0..6 中不重复的逗号分隔组号')
    return values


def compact(source, output, work, groups=(0, 1)):
    native = build_native(work)
    entries = read_table(source)
    ns = {len(e['data']) for e in entries}
    if len(ns) != 1:
        raise ValueError('自动选 N 要求两侧原始样本数相同')
    original_n = ns.pop()
    candidates = []
    n = 128
    while n < original_n:
        candidates.append(n)
        n *= 2
    candidates.append(original_n)
    attempts = []
    for n in candidates:
        candidate = Path(work) / f'candidate-{n}.bin'
        resample(native, source, candidate, groups, n)
        metrics = compare(native, source, candidate)
        dump(Path(work) / f'candidate-{n}.json', metrics)
        attempts.append(metrics)
        if metrics['passed']:
            Path(output).parent.mkdir(parents=True, exist_ok=True)
            Path(output).write_bytes(candidate.read_bytes())
            report = dict(samples=n, original_samples=original_n, groups=list(groups),
                          output_bytes=Path(output).stat().st_size, attempts=attempts)
            dump(Path(work) / 'compact.json', report)
            return report
    raise RuntimeError('原样本回退也未通过，检查解析或采样代码')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    c = sub.add_parser('compact')
    c.add_argument('--source', type=Path, required=True)
    c.add_argument('--output', type=Path, required=True)
    c.add_argument('--work', type=Path, required=True)
    c.add_argument('--groups', type=parse_groups, default=[0, 1])
    c = sub.add_parser('compare')
    c.add_argument('--source', type=Path, required=True)
    c.add_argument('--candidate', type=Path, required=True)
    c.add_argument('--work', type=Path, required=True)
    c = sub.add_parser('inspect')
    c.add_argument('--source', type=Path, required=True)
    c.add_argument('--work', type=Path, required=True)
    args = p.parse_args()
    if args.command == 'compact':
        result = compact(args.source.resolve(), args.output.resolve(), args.work, args.groups)
        print(json.dumps({k:v for k,v in result.items() if k != 'attempts'}, ensure_ascii=False))
    elif args.command == 'compare':
        result = compare(build_native(args.work), args.source.resolve(), args.candidate.resolve())
        dump(args.work / 'comparison.json', result)
        print(json.dumps(result, ensure_ascii=False))
    else:
        native = build_native(args.work)
        subprocess.run([str(native), 'inspect', str(args.source.resolve())], check=True)


if __name__ == '__main__':
    main()
