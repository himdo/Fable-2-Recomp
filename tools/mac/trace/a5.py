import sys,re; import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); from xt import load
from collections import Counter, defaultdict
def pct(xs,p): xs=sorted(xs); return xs[min(len(xs)-1,int(p/100*len(xs)))]
def T(path):
    c,r=load(path); ix={k:i for i,k in enumerate(c)}; return r,(lambda row,k,j=0: row[ix[k]][j])
D=sys.argv[1]
r,g=T(D+'/metal-application-encoders-list.xml')
cb=defaultdict(list); lab={}
for x in r:
    if 'fable' in (g(x,'process',1) or ''):
        cb[g(x,'cmdbuffer-id')].append((int(g(x,'start')),g(x,'encoder-label') or '',g(x,'encoder-id'))); lab[g(x,'encoder-id')]=g(x,'encoder-label') or ''
pass_of={}; segs=[]
for c,L in cb.items():
    L.sort(); cur=None
    for s,l,e in L:
        if 'RenderEncoder' in l and 'restart' not in l: cur=e; segs.append([e,0])
        elif 'restart' in l and cur: segs[-1][1]+=1; pass_of[e]=cur
        if 'RenderEncoder' in l and cur: pass_of[e]=cur
wr=[n for _,n in segs if n]; print('passes with restarts',len(wr),'restarts in them',sum(wr),'per such pass p50',pct(wr,50),'p90',pct(wr,90),'max',max(wr))
r,g=T(D+'/metal-gpu-intervals.xml'); fd=defaultdict(list); pt=defaultdict(int)
for x in r:
    if 'fable' not in (g(x,'process',1) or ''): continue
    e=g(x,'encoder-id'); l=lab.get(e,'?'); ch=g(x,'channel-name'); d=int(g(x,'duration'))
    k=('restart' if 'restart' in l else 'begin' if 'BeginRendering' in l else 'tess' if 'tess' in l else 'other',ch); fd[k].append(d)
    if e in pass_of: pt[pass_of[e]]+=d
for k in sorted(fd,key=lambda k:-sum(fd[k]))[:6]:
    v=fd[k]; print(k,'n',len(v),'sum %.0f ms p10 %.1f p50 %.1f p90 %.1f us'%(sum(v)/1e6,pct(v,10)/1e3,pct(v,50)/1e3,pct(v,90)/1e3))
heavy={e for e,n in segs if n}; a=sum(v for e,v in pt.items() if e in heavy); b=sum(pt.values())
print('GPU time in passes-with-restarts %.0f ms of %.0f ms render-pass time'%(a/1e6,b/1e6))
