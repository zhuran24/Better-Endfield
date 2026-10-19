#!/usr/bin/env python3
"""附属件循环曲线、骨架覆盖与全蒙皮评分。"""
import os
os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
os.environ.setdefault('OMP_NUM_THREADS', '1')
import sys
from pathlib import Path
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from pose_kinematics import *
from numpy.polynomial import Polynomial as Poly
from scipy.linalg import solve

HASHES = {'right': -445642423, 'left': 528430122}
SKIN_TIPS = {0:['tail_09_jnt'], 1:[f'{s}_hairE_09_jnt' for s in 'LR'],
    2:[f'{s}_sleeveD_06_jnt' for s in 'LR'],
    3:['ribbonF_05_jnt','ribbonB_04_jnt','ribbonC_06_jnt'], 6:['Bip001_R_Hand']}

class RigV2(Rig):
    def __init__(self, capture, state_hash, seam, catalog):
        super().__init__(capture, seam, state_hash, catalog)
        self.state_hash = state_hash
        self.skin_groups = {g: np.intersect1d(ix, self.skin) for g,ix in self.bones.items()}
        self.gs = {g:CubicSpline(self.su, self.spline(self.su)[:,ix]) for g,ix in self.bones.items()}
        for g in GROUPS:
            ns = SKIN_TIPS.get(g)
            if ns is None:
                ids = []
                for i in self.tips[g]:
                    while self.names[i] not in self.palette: i=self.parent[i]
                    ids.append(i)
                self.tips[g]=np.unique(ids)
            else: self.tips[g]=np.array([self.nm[n] for n in ns])
            assert all(i in self.skin for i in self.tips[g])
        self._source_caps={}

    def base(self, phase, groups, first=False):
        """仅所选组免于旧修正；其余骨仍执行原修正。"""
        ph=np.atleast_1d(phase)
        q,p=self.corrected(ph,first=first)
        ix=np.concatenate([self.bones[g] for g in groups]) if groups else np.array([],int)
        rq,rp=self.raw(A+SPAN*ph)
        q[:,ix],p[:,ix]=rq[:,ix],rp[:,ix]
        return q,p

    def caps(self,g,u3,uk,hz):
        key=(g,u3,uk,hz)
        if key not in self._source_caps:
            ix=self.skin_groups[g];vs=[];ds=[]
            for lo,hi in [(B,u3),(uk,B)]:
                t=np.arange(0,(hi-lo)*CLIP,1/hz)
                _,p=self.fk(self.raw(lo+t/CLIP),ix)
                v=np.diff(p[:,ix],axis=0)*hz*100
                vs.append(np.linalg.norm(v,axis=-1).max(axis=0) if len(v) else np.zeros(len(ix)))
                ds.append(np.linalg.norm(np.diff(v,axis=0),axis=-1).max(axis=0) if len(v)>1 else np.zeros(len(ix)))
            self._source_caps[key]=(np.max(vs,axis=0),np.max(ds,axis=0))
        return self._source_caps[key]

def hermite(x,y0,y1,v0,v1):
    x=np.asarray(x)[:,None,None]
    return (2*x**3-3*x*x+1)*y0+(x**3-2*x*x+x)*v0+(-2*x**3+3*x*x)*y1+(x**3-x*x)*v1

class CycleV2:
    def __init__(self,r,params):
        self.rig=r;self.params={int(g):dict(c) for g,c in params.items()};self.coef={}
        for g,c in self.params.items():
            if c['method'] in ['hybrid','minjerk']:
                self.coef[g]=self.fit_carrier(g,c)
            if c['method']=='minjerk': self.coef[g,'jerk']=self.fit_jerk(g,c)

    def endpoints(self,g,c):
        s=self.rig.gs[g]; h=c['uk']-A-(c['u3']-B)
        return [s(c['u3']),s(c['uk']),s(c['u3'],1)*h,s(c['uk'],1)*h]

    def fit_carrier(self,g,c):
        # 周期 Fourier 在 phi=0/1 锚定同一个真实 B 值与导数。
        k=c.get('K',3); p3=(c['u3']-B)/SPAN;pk=(c['uk']-A)/SPAN
        ph=np.r_[np.linspace(0,p3,48),np.linspace(pk,1,24)]
        y=self.rig.gs[g](np.where(ph<=p3,B+SPAN*ph,A+SPAN*ph)).reshape(len(ph),-1)
        mat=Cycle.design(ph,k);pen=np.array([0]+[n**4 for n in range(1,k+1) for _ in range(2)])
        h=mat.T@mat/len(ph)+c.get('ridge',.003)*np.diag(pen)
        con=np.vstack([Cycle.design(np.array([0.]),k,d) for d in [0,1]])
        target=np.stack([self.rig.gs[g](B,d).reshape(-1)*SPAN**d for d in [0,1]])
        coef=solve(np.block([[h,con.T],[con,np.zeros((2,2))]]),np.vstack([mat.T@y/len(ph),target]),assume_a='sym')[:len(h)]
        return coef.reshape(2*k+1,len(self.rig.bones[g]),6)

    def carrier(self,g,c,ph,d=0):
        return np.einsum('fk,kbc->fbc',Cycle.design(np.atleast_1d(ph),c.get('K',3),d),self.coef[g])

    def hybrid(self,g,c,x):
        p3=(c['u3']-B)/SPAN;pk=(c['uk']-A)/SPAN;h=pk-p3
        y0,y1,v0,v1=self.endpoints(g,c)
        ends=self.carrier(g,c,[p3,pk]);dv=self.carrier(g,c,[p3,pk],1)*h
        return self.carrier(g,c,p3+h*x)+hermite(x,y0-ends[0],y1-ends[1],v0-dv[0],v1-dv[1])

    def fit_jerk(self,g,c):
        # z=cubic Hermite + sum beta_j x²(1-x)² P_j(2x-1)。硬 C1，软二阶导/载体。
        y0,y1,v0,v1=self.endpoints(g,c)
        cubic=np.stack([y0,v0,3*(y1-y0)-2*v0-v1,2*(y0-y1)+v0+v1])
        envelope=Poly([0,0,1,-2,1]);basis=[envelope*Poly([-.5,1])**j for j in range(6)]
        xx=np.linspace(0,1,81)
        mat=np.stack([p(xx) for p in basis],axis=1)
        jerk=np.stack([p.deriv(3)(xx) for p in basis],axis=1)
        d2=np.stack([p.deriv(2)(np.array([0.,1.])) for p in basis],axis=1)
        value=hermite(xx,y0,y1,v0,v1)
        h=c['uk']-A-(c['u3']-B)
        target=np.stack([self.rig.gs[g](c['u3'],2),self.rig.gs[g](c['uk'],2)])*h*h
        base2=np.stack([2*cubic[2],2*cubic[2]+6*cubic[3]])
        alpha=c.get('accel_soft',1.);beta=c.get('carrier_soft',1.)
        M=np.concatenate([jerk/np.sqrt(len(xx)),np.sqrt(alpha)*d2,np.sqrt(beta)*mat/np.sqrt(len(xx))])
        Y=np.concatenate([-np.broadcast_to(6*cubic[3],value.shape)/np.sqrt(len(xx)),np.sqrt(alpha)*(target-base2),np.sqrt(beta)*(self.hybrid(g,c,xx)-value)/np.sqrt(len(xx))])
        coeff=np.linalg.lstsq(M,Y.reshape(len(M),-1),rcond=None)[0].reshape(6,*y0.shape)
        return cubic,basis,coeff

    def group_z(self,phase,g):
        ph=np.atleast_1d(phase);c=self.params[g];p3=(c['u3']-B)/SPAN;pk=(c['uk']-A)/SPAN
        z=self.rig.gs[g](np.where(ph<=p3,B+SPAN*ph,A+SPAN*ph))
        mid=(ph>p3)&(ph<pk)
        if not mid.any():return z
        x=(ph[mid]-p3)/(pk-p3); y0,y1,v0,v1=self.endpoints(g,c)
        if c['method']=='hybrid': z[mid]=self.hybrid(g,c,x)
        elif c['method']=='minjerk':
            cubic,basis,coeff=self.coef[g,'jerk']
            z[mid]=hermite(x,y0,y1,v0,v1)+np.einsum('fj,jbc->fbc',np.stack([p(x) for p in basis],axis=1),coeff)
        elif c['method']=='cubic':z[mid]=hermite(x,y0,y1,v0,v1)
        else:
            h=c['uk']-A-(c['u3']-B);a0=self.rig.gs[g](c['u3'],2)*h*h;a1=self.rig.gs[g](c['uk'],2)*h*h
            xx=x[:,None,None]
            if c['method']=='quintic':
                c0,c1,c2=y0,v0,a0/2;d=y1-c0-c1-c2;v=v1-c1-2*c2;a=a1-2*c2
                c3,c4,c5=10*d-4*v+.5*a,-15*d+7*v-a,6*d-3*v+.5*a
                z[mid]=c0+xx*(c1+xx*(c2+xx*(c3+xx*(c4+xx*c5))))
            elif c['method']=='damped':
                tau=c['tau'];rr=xx-1
                e0=y0+(v0*xx+.5*a0*xx*xx)*np.exp(-(xx/tau)**2)
                e1=y1+(v1*rr+.5*a1*rr*rr)*np.exp(-(rr/tau)**2)
                z[mid]=(1-smooth5(xx))*e0+smooth5(xx)*e1
            else:raise ValueError(c['method'])
        return z

    def local(self,phase,groups=None):
        groups=list(self.params) if groups is None else list(groups)
        ix=np.concatenate([self.rig.bones[g] for g in groups]);z=np.concatenate([self.group_z(phase,g) for g in groups],axis=1)
        return ix,norm(mul(self.rig.ref[None,ix],expq(z[...,:3]))),z[...,3:]

    def pose(self,phase,groups=None):
        groups=list(self.params) if groups is None else list(groups)
        q,p=self.rig.base(phase,groups);ix,aq,ap=self.local(phase,groups);q[:,ix],p[:,ix]=aq,ap
        return q,p

def table_pose_v2(r,e,phase,groups=None):
    groups=sorted(set(e['groups'])) if groups is None else groups
    ph=np.mod(np.atleast_1d(phase),1);q,p=r.base(ph,groups);aq,ap=sample_table(e,ph)
    keep=np.array([g in groups for g in e['groups']]);ix=[r.nm[n] for n,k in zip(e['names'],keep) if k]
    q[:,ix],p[:,ix]=aq[:,keep],ap[:,keep]
    return q,p

def make_entry(r,params,groups=None,n=1152):
    cy=CycleV2(r,params);ix,q,p=cy.local(np.arange(n)/n,groups)
    # 周期表的左右首邻段共切线；内部表节点仍是分段 slerp/lerp，不声称处处 C1。
    v=(logq(mul(conj(q[0]),q[1]))-logq(mul(conj(q[0]),q[-1])))/2
    q[1]=norm(mul(q[0],expq(v)));q[-1]=norm(mul(q[0],expq(-v)))
    d=(p[1]-p[-1])/2;p[1]=p[0]+d;p[-1]=p[0]-d
    return dict(state_hash=r.state_hash,names=[r.names[i] for i in ix],groups=[int(r.group[i]) for i in ix],data=np.concatenate([q,p],axis=-1))

def score_arrays(r,g,c,ph,P,hz):
    ix=r.skin_groups[g];v=np.diff(P,axis=0)*hz*100;sp=np.linalg.norm(v,axis=-1);dv=np.linalg.norm(np.diff(v,axis=0),axis=-1)
    p3=(c['u3']-B)/SPAN;pk=(c['uk']-A)/SPAN
    mid=(ph[:-1]+ph[1:])/2;bm=(mid>p3)&(mid<pk);dm=(ph[1:-1]>p3)&(ph[1:-1]<pk)
    cap,dcap=r.caps(g,c['u3'],c['uk'],hz)
    peak=sp[bm].max(axis=0);change=dv[dm].max(axis=0)
    # 分母为零时不给静止骨伪造通过。
    sr=peak/np.maximum(cap,1e-12);dr=change/np.maximum(dcap,1e-12)
    return dict(speed=float(sr.max()),dv=float(dr.max()),speed_bone=r.names[ix[sr.argmax()]],dv_bone=r.names[ix[dr.argmax()]]), (peak,cap,sr,change,dcap,dr)

class Evaluator:
    """缓存完整祖先的基础姿态，候选只替换本组，避免每次生成整棵树。"""
    def __init__(self,r,g,parents=None):
        self.r=r;self.g=g;self.cache={};wanted=r.skin_groups[g]
        needed=set(map(int,wanted))
        for i in sorted(needed):
            while r.parent[i]>=0:i=int(r.parent[i]);needed.add(i)
        self.ids=np.array(sorted(needed));mp={j:i for i,j in enumerate(self.ids)}
        self.parent=[mp.get(int(r.parent[i]),-1) for i in self.ids]
        self.out=[mp[i] for i in wanted]
        self.glocal=[mp[i] for i in r.bones[g] if i in mp];self.gkeep=[j for j,i in enumerate(r.bones[g]) if i in mp]
        self.parents=parents or {}
        for hz in [60,120]:
            ph=np.arange(0,PERIOD,1/hz)/PERIOD
            groups=sorted({g, *self.parents})
            q,p=r.base(ph,groups)
            if self.parents:
                ix,aq,ap=CycleV2(r,self.parents).local(ph);q[:,ix],p[:,ix]=aq,ap
            self.cache[hz]=(ph,q[:,self.ids],p[:,self.ids])

    def evaluate(self,c):
        cy=CycleV2(self.r,{self.g:c});result={}
        for hz,(ph,q0,p0) in self.cache.items():
            z=cy.group_z(ph,self.g);q=q0.copy();p=p0.copy()
            q[:,self.glocal]=norm(mul(self.r.ref[None,self.r.bones[self.g][self.gkeep]],expq(z[:,self.gkeep,:3])))
            p[:,self.glocal]=z[:,self.gkeep,3:]
            for i,j in enumerate(self.parent):
                if j>=0:p[:,i]=p[:,j]+rotate(q[:,j],p[:,i]);q[:,i]=mul(q[:,j],q[:,i])
            result[hz]=score_arrays(self.r,self.g,c,ph,p[:,self.out],hz)[0]
        return result
