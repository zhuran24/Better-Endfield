#!/usr/bin/env python3
"""按侧/录制自动选参，写可选分组与样本数的 BEACC001。"""
import argparse
import os
import time

# 必须在导入数值库前固定线程数，保持搜索与线性求解的复现环境。
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
from accessory_cycle import *
from accessory_table import build_native, resample, compare, parse_groups

DEFAULT_SAMPLES = 1024
SCORE_DECIMALS = 9
PARAMETER_FIELDS = ('method', 'u3', 'uk', 'tau', 'K', 'ridge', 'accel_soft', 'carrier_soft')

def stable_suffix(r,g):
    # 从 A 开始搜索平稳后缀。
    u=np.linspace(A,.40,701);ix=r.tips[g];_,p=r.fk(r.raw(u),ix);p=p[:,ix]
    um=(u[:-1]+u[1:])/2;v=np.linalg.norm(np.diff(p,axis=0),axis=-1)*100/((u[1]-u[0])*CLIP)
    ref=(um>=B)&(um<=.38);rh=(u>=B)&(u<=.38)
    cap=np.quantile(v[ref],.99,axis=0)*1.2;lo=p[rh,:,1].min(axis=0)-.05;hi=p[rh,:,1].max(axis=0)+.05
    valid=[]
    for uk in np.r_[np.arange(.225,.310,.01),.310]:
        m=(um>=uk)&(um<B);h=(u>=uk)&(u<=B)
        if np.all(np.quantile(v[m],.95,axis=0)<=cap) and np.all(np.mean((p[h,:,1]>=lo)&(p[h,:,1]<=hi),axis=0)>=.9):valid.append(round(float(uk),6))
    return valid

def configs(uks):
    for u3 in [.365,.375,.385,.395]:
        for uk in uks:
            base=dict(u3=u3,uk=uk)
            for method in ['cubic','quintic']:yield dict(base,method=method)
            for tau in [.06,.10,.16,.22,.32,.50,.80]:yield dict(base,method='damped',tau=tau)
            for k in [2,3,4]:
                for ridge in [.0001,.003,.03]:yield dict(base,method='hybrid',K=k,ridge=ridge)
            for alpha,beta in [(0.,0.),(.01,1.),(1.,1.),(100.,1.),(1.,10000.)]:
                yield dict(base,method='minjerk',K=3,ridge=.003,accel_soft=alpha,carrier_soft=beta)

def raw_score(m):
    speed=max(v['speed'] for v in m.values());dv=max(v['dv'] for v in m.values())
    return max(speed/1.2,dv/1.5),speed,dv

def score_key(values):
    """固定 1e-9 评分网格形成可传递的全序，避免成对 isclose 的顺序依赖。"""
    if not all(np.isfinite(v) for v in values):
        raise ValueError('候选评分必须是有限数')
    return tuple(round(float(v), SCORE_DECIMALS) for v in values)

def key_score(m):
    return score_key(raw_score(m))

def parameter_key(c):
    # 只编码曲线参数；不让字段插入顺序或诊断字段参与破同分。
    return json.dumps({k:c[k] for k in PARAMETER_FIELDS if k in c},
                      sort_keys=True, separators=(',', ':'), allow_nan=False)

def candidate_key(m,c):
    return key_score(m)+(parameter_key(c),)

def row_key(row):
    return score_key((row['score'], max(row['speed_60'],row['speed_120']),
                      max(row['dv_60'],row['dv_120'])))+(parameter_key(row),)

def refinement_seeds(rows):
    seeds={}
    for row in rows:
        method=row['method']
        if method not in seeds or row_key(row)<row_key(seeds[method]):
            seeds[method]=row
    return [seeds[method] for method in sorted(seeds)]

def capture_key(meta):
    return score_key(meta['score'])+(meta['capture_sha256'],)

def optimize(r,directory):
    directory.mkdir(parents=True,exist_ok=True);params={};rows=[];stable={};family=[]
    # 右臂先确定，袖/袖飘带评分包含该真实父链覆盖。
    for g in [6,0,1,2,3,4,5]:
        uks=stable_suffix(r,g);stable[g]=dict(valid_uk=uks,fallback=not bool(uks))
        if not uks:uks=[.310]
        evaluator=Evaluator(r,g,{6:params[6]} if g in [2,3] else {})
        best=None;family_best={}
        for c in configs(uks):
            m=evaluator.evaluate(c);key=candidate_key(m,c)
            row=dict(group=g,**c,score=raw_score(m)[0],speed_60=m[60]['speed'],speed_120=m[120]['speed'],dv_60=m[60]['dv'],dv_120=m[120]['dv'],speed_bone_120=m[120]['speed_bone'],dv_bone_120=m[120]['dv_bone'])
            rows.append(row)
            f=(c['method'],c['u3'])
            if f not in family_best or key<family_best[f][0]:family_best[f]=(key,row)
            if best is None or key<best[0]:best=(key,c,m)
        # 每个方法的粗网格赢家再在相邻 u3/uk 和形状参数细化；同一规则适用于两侧。
        for row0 in refinement_seeds(row for _,row in family_best.values()):
            base={k:row0[k] for k in ['method','u3','uk','tau','K','ridge','accel_soft','carrier_soft'] if k in row0}
            for u3 in sorted(set([base['u3'],max(.36,base['u3']-.005),min(.395,base['u3']+.005)])):
                for uk in sorted(set([base['uk'],max(min(uks),base['uk']-.005),min(.310,base['uk']+.005)])):
                    c=dict(base,u3=u3,uk=uk);m=evaluator.evaluate(c);key=candidate_key(m,c)
                    row=dict(group=g,**c,score=raw_score(m)[0],speed_60=m[60]['speed'],speed_120=m[120]['speed'],dv_60=m[60]['dv'],dv_120=m[120]['dv'],speed_bone_120=m[120]['speed_bone'],dv_bone_120=m[120]['dv_bone'])
                    rows.append(row)
                    if best is None or key<best[0]:best=(key,c,m)
        params[g]=best[1];family.extend(v[1] for v in family_best.values())
        print(f'GROUP {g} {GROUPS[g]} score={best[0][0]:.4f} speed={best[0][1]:.4f} dv={best[0][2]:.4f} {best[1]}',flush=True)
    # 不齐的候选参数列补空，便于 TSV 审计。
    fields=list(dict.fromkeys(k for row in rows for k in row))
    tsv(directory/'candidates.tsv',[{k:row.get(k,'') for k in fields} for row in rows])
    tsv(directory/'family_best.tsv',[{k:row.get(k,'') for k in fields} for row in family])
    dump(directory/'stable.json',stable);dump(directory/'params.json',params)
    return params

def evaluate_table(r,e,params,directory,label='full',groups=None):
    groups=list(params) if groups is None else groups
    rows=[];tips=[];summary=[]
    for hz in [60,120]:
        ph=np.arange(0,PERIOD,1/hz)/PERIOD
        _,p=r.fk(table_pose_v2(r,e,ph,groups),r.skin)
        for g in groups:
            ix=r.skin_groups[g];m,values=score_arrays(r,g,params[g],ph,p[:,ix],hz)
            summary.append(dict(group=g,hz=hz,**m,passed=m['speed']<=1.2 and m['dv']<=1.5))
            for j,i in enumerate(ix):
                rows.append(dict(group=g,hz=hz,bone=r.names[i],**{key:val[j] for key,val in zip(['bridge_peak_cm_s','source_peak_cm_s','speed_ratio','bridge_dv_cm_s','source_dv_cm_s','dv_ratio'],values)}))
        # 四圈精确 Hz 网格，包含回绕残差；路程按实际覆盖时长折算。
        time_grid=np.arange(0,4*PERIOD,1/hz);phi=np.mod(time_grid/PERIOD,1)
        wanted=np.concatenate([r.tips[g] for g in groups]);_,P=r.fk(table_pose_v2(r,e,phi,groups),wanted)
        _,old=r.fk(r.corrected(phi),wanted)
        for g in groups:
            for i in r.tips[g]:
                d=np.linalg.norm(np.diff(P[:,i],axis=0),axis=-1)*100
                od=np.linalg.norm(np.diff(old[:,i],axis=0),axis=-1)*100
                tips.append(dict(group=g,hz=hz,bone=r.names[i],path_cm=d.sum()*PERIOD/time_grid[-1],peak_cm_s=d.max()*hz,old_path_cm=od.sum()*PERIOD/time_grid[-1],old_peak_cm_s=od.max()*hz))
    tsv(directory/f'{label}.bones.tsv',rows);tsv(directory/f'{label}.tips.tsv',tips);tsv(directory/f'{label}.summary.tsv',summary)
    return summary

def run_capture(path,side,directory,seam,catalog):
    start=time.perf_counter();r=RigV2(path,HASHES[side],seam,catalog);params=optimize(r,directory);e=make_entry(r,params)
    file=directory/'selected.bin';write_table(file,[e]);e=read_table(file)[0]
    summary=evaluate_table(r,e,params,directory)
    scores=[max(x['speed']/1.2,x['dv']/1.5) for x in summary]
    meta=dict(capture=str(path),capture_sha256=sha(path),side=side,state_hash=HASHES[side],parameters=params,
        score=[max(scores),float(np.mean(scores))],elapsed_s=time.perf_counter()-start,
        skinned_bones=375,default_mask='0x03',source_stability='tip suffix gate; all skin final acceptance')
    dump(directory/'result.json',meta)
    return r,e,params,meta,summary

def short_summary(results,path,elapsed):
    lines=['# 附属件双侧源曲线全组评测','',
        f'耗时 {elapsed:.2f} s；每侧独立全组搜索。下列为重采样前全组曲线的运动学评分，精简表误差另见 JSON。速度/Δv 门槛 1.2/1.5。', '',
        '| 侧 | 组 | 60 Hz 速度/Δv | 120 Hz 速度/Δv | 达标 |','|---|---|---|---|---|']
    for side,result in results.items():
        for g in GROUPS:
            a,b=[next(x for x in result[4] if x['group']==g and x['hz']==hz) for hz in [60,120]]
            lines.append(f"| {side} | {g} {GROUPS[g]} | {a['speed']:.3f}/{a['dv']:.3f} | {b['speed']:.3f}/{b['dv']:.3f} | {'是' if a['passed'] and b['passed'] else '否'} |")
    lines+=['','BECAP001 无状态 hash，侧别依调用者声明；这里只验证离线运动学，不证明游戏最终画面。']
    path.write_text('\n'.join(lines)+'\n')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--right',nargs='+',type=Path,required=True)
    p.add_argument('--left',nargs='+',type=Path,required=True)
    p.add_argument('--catalog',type=Path,required=True,help='上游 chr_0036_jsspsi.json')
    p.add_argument('--seam',type=Path,required=True,help='BESEAM03 身体差表')
    p.add_argument('--groups',type=parse_groups,default=[0,1])
    p.add_argument('--samples',type=int,default=DEFAULT_SAMPLES,help='最终表节点数；原曲线始终先生成 1152 节点')
    p.add_argument('--output',type=Path,default=Path('accessory_jsspsi.bin'))
    p.add_argument('--work',type=Path,default=Path('jsspsi-work'))
    args=p.parse_args();start=time.perf_counter()
    if not 8<=args.samples<=4096:p.error('--samples 须在 8..4096')
    work=args.work.resolve();output=args.output.resolve()
    inputs={'right':args.right,'left':args.left}
    source_paths=[x.resolve() for paths in inputs.values() for x in paths]+[args.catalog.resolve(),args.seam.resolve()]
    for path in source_paths:
        if not path.is_file():p.error(f'输入不存在：{path}')
    if output in source_paths or any(work==path or work in path.parents for path in source_paths):
        p.error('输出/工作目录不能覆盖或包含输入')
    if output.exists() or work.exists():p.error('输出文件和工作目录须不存在，避免覆盖已有结果')
    for side,paths in inputs.items():
        if not all(side in x.name.lower() for x in paths):p.error(f'{side} 文件名须带 {side}')
    if {sha(x) for x in args.right}&{sha(x) for x in args.left}:p.error('左右录制内容相同')
    results={};all_meta={}
    for side,paths in inputs.items():
        candidates=[]
        for i,path in enumerate(paths):
            print(f'CAPTURE {side} {i} {path}',flush=True)
            result=run_capture(path.resolve(),side,work/side/str(i),args.seam.resolve(),args.catalog.resolve())
            candidates.append(result)
        result=min(candidates,key=lambda v:capture_key(v[3]))
        results[side]=result;all_meta[side]=[v[3] for v in candidates]
    # 保留全组搜索和录制选择口径，再从实际序列化的原 N 表采样。
    # 直接用新 N 合成连续曲线会改变首邻切线修正，不能复现精简现用表。
    full=work/'accessory_jsspsi.full.bin'
    write_table(full,[v[1] for v in results.values()])
    native=build_native(work/'native')
    resample(native,full,output,args.groups,args.samples)
    metrics=compare(native,full,output)
    elapsed=time.perf_counter()-start
    short_summary(results,Path(str(output)+'.summary.md'),elapsed)
    dump(Path(str(output)+'.json'),dict(elapsed_s=elapsed,sha256=sha(output),
        groups=args.groups,samples=args.samples,source_samples=1152,
        selection_policy=dict(score_decimals=SCORE_DECIMALS,
            candidate_order=['normalized_worst','speed','dv','canonical_parameters'],
            capture_order=['normalized_worst','mean','capture_sha256']),
        external_inputs={name:dict(path=str(path.resolve()),sha256=sha(path))
                         for name,path in [('catalog',args.catalog),('seam',args.seam)]},
        inputs=all_meta,selected={side:v[3] for side,v in results.items()},
        resampling_error=metrics))
    print(f'DONE {output} elapsed_s={elapsed:.3f}',flush=True)

if __name__=='__main__':main()
