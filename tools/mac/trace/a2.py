import sys,re; import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); from xt import load
from collections import Counter, defaultdict
def pct(xs,p): xs=sorted(xs); return xs[min(len(xs)-1,int(p/100*len(xs)))]
def T(path):
    c,r=load(path); ix={k:i for i,k in enumerate(c)}; return r,(lambda row,k,j=0: row[ix[k]][j])
W=5.454
# submissions
r,g=T('mst/metal-application-command-buffer-submissions.xml')
r=[x for x in r if 'fable' in (g(x,'process',1) or '')]
ne=[int(g(x,'num-encoders')) for x in r]; et=[int(g(x,'encoder-time') or 0) for x in r if g(x,'encoder-time')]
print('fable CB submissions',len(r),'-> %.1f/s'%(len(r)/W),'with encoders',sum(1 for n in ne if n),'encoders total',sum(ne))
nz=[n for n in ne if n]; print(' encoders/CB (non-empty) p50',pct(nz,50),'p90',pct(nz,90),'max',max(nz))
print(' CPU encode time/CB p50 %.2f p90 %.2f max %.2f ms, sum %.0f ms'%(pct(et,50)/1e6,pct(et,90)/1e6,max(et)/1e6,sum(et)/1e6))
fr=Counter(g(x,'frame-number') for x in r); print(' distinct frame-numbers',len(fr),'CB/frame p50',pct(list(fr.values()),50))
sub={g(x,'cmdbuffer-id'):int(g(x,'start'))+int(g(x,'duration')) for x in r}
big=sorted(nz)[-5:]; print(' top enc counts',big)
# encoders list
r2,g2=T('mst/metal-application-encoders-list.xml')
lab=Counter(); kind=Counter(); dsum=defaultdict(int)
for x in r2:
    l=g2(x,'encoder-label',1) or ''; k='Render' if 'RenderEncoder' in l else 'Blit' if 'Blit' in l else 'Compute' if 'Compute' in l else 'other'
    kind[k]+=1; lab[re.sub(r'\s+',' ',l)[:70]]+=1; dsum[k]+=int(g2(x,'duration'))
print('encoders by kind',dict(kind),'CPU encode ms',{k:round(v/1e6) for k,v in dsum.items()})
print('top labels',lab.most_common(8))
# completions latency
r3,g3=T('mst/metal-command-buffer-completed.xml')
lat=[int(g3(x,'timestamp'))-sub[g3(x,'cmdbuffer-id')] for x in r3 if g3(x,'cmdbuffer-id') in sub]
print('submit->complete lat n',len(lat),'p50 %.2f p90 %.2f max %.2f ms'%(pct(lat,50)/1e6,pct(lat,90)/1e6,max(lat)/1e6))
# display
r4,g4=T('mst/displayed-surfaces-per-second.xml'); print('surfaces/s',[g4(x,'count') for x in r4])
r5,g5=T('mst/display-surface-swap.xml'); ts=sorted(int(g5(x,'timestamp')) for x in r5)
d=[b-a for a,b in zip(ts,ts[1:])]; print('swaps',len(ts),'%.1f/s'%(len(ts)/W),'interval p50 %.1f p90 %.1f max %.1f ms'%(pct(d,50)/1e6,pct(d,90)/1e6,max(d)/1e6))
print(' surface-ids',Counter(g5(x,'surface-id',1) for x in r5).most_common(6))
r6,g6=T('mst/display-vsyncs-interval.xml'); vs=sorted(int(g6(x,'timestamp')) for x in r6); dv=[b-a for a,b in zip(vs,vs[1:])]
print('vsyncs',len(vs),'%.1f/s'%(len(vs)/W),'interval p50 %.2f ms'%(pct(dv,50)/1e6), Counter(g6(x,'event') for x in r6))
# perf state
r7,g7=T('mst/gpu-performance-state-intervals.xml'); ps=defaultdict(int)
for x in r7: ps[(g7(x,'gpu-performance-state'),g7(x,'is-induced',1))]+=int(g7(x,'duration'))
print('perf state ms',{k:round(v/1e6,1) for k,v in ps.items()})
print(Counter(g7(x,'narrative',1) for x in r7).most_common(3))
# gpu-state
r8,g8=T('mst/metal-gpu-state-intervals.xml'); gs=defaultdict(int)
for x in r8: gs[g8(x,'state')]+=int(g8(x,'duration'))
print('gpu-state ms',{k:round(v/1e6,1) for k,v in gs.items()})
