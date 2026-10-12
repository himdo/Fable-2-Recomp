import sys; import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); from xt import load
from collections import Counter, defaultdict
import statistics as st
c,r=load('mst/metal-gpu-intervals.xml'); ix={k:i for i,k in enumerate(c)}
g=lambda row,k: row[ix[k]]
procs=Counter(); ch=Counter(); dur_by=defaultdict(int)
iv_all=[]; iv_f=[]; cb=defaultdict(lambda:[1e30,0]); enc_by_cb=defaultdict(set)
for row in r:
    s=int(g(row,'start')[0]); d=int(g(row,'duration')[0]); p=g(row,'process')[1]; cn=g(row,'channel-name')[1]
    procs[p]+=1; dur_by[(p,cn)]+=d
    iv_all.append((s,s+d))
    if p and 'fable' in p:
        iv_f.append((s,s+d)); cid=g(row,'cmdbuffer-id')[0]
        cb[cid][0]=min(cb[cid][0],s); cb[cid][1]=max(cb[cid][1],s+d)
        enc_by_cb[cid].add((g(row,'encoder-id')[0],cn))
print('procs',procs.most_common(5))
for k,v in sorted(dur_by.items(),key=lambda x:-x[1])[:8]: print('busy-sum',k,round(v/1e6,1),'ms')
def union(iv):
    iv.sort(); out=[]
    for a,b in iv:
        if out and a<=out[-1][1]: out[-1][1]=max(out[-1][1],b)
        else: out.append([a,b])
    return out
def pct(xs,p): xs=sorted(xs); return xs[min(len(xs)-1,int(p/100*len(xs)))]
for name,iv in (('ALL',iv_all),('fable',iv_f)):
    u=union(iv); t0=u[0][0]; t1=u[-1][1]; busy=sum(b-a for a,b in u); W=t1-t0
    gaps=[u[i+1][0]-u[i][1] for i in range(len(u)-1)]
    print(name,'win %.3fs busy %.1f ms util %.1f%%'%(W/1e9,busy/1e6,100*busy/W),'n_gaps',len(gaps),
          'gap mean %.3f ms p50 %.3f p90 %.3f max %.2f ms'%(st.mean(gaps)/1e6,pct(gaps,50)/1e6,pct(gaps,90)/1e6,max(gaps)/1e6))
    for th in (0.1e6,1e6,5e6,10e6,30e6):
        gs=[x for x in gaps if x>=th]; print('  gaps>=%.1fms: n=%d sum=%.1f ms'%(th/1e6,len(gs),sum(gs)/1e6))
    if name=='ALL': ALL=(t0,t1)
# fable busy within global window 
print('trace window guess', ALL[0]/1e9, ALL[1]/1e9)
# per-cb gpu span
spans=[b-a for a,b in cb.values()]
print('fable cbs with gpu work',len(spans),'span p50 %.2f p90 %.2f max %.2f ms'%(pct(spans,50)/1e6,pct(spans,90)/1e6,max(spans)/1e6))
ec=[len(v) for v in enc_by_cb.values()]
print('gpu encoders/cb (encoder-id,channel) p50',pct(ec,50),'p90',pct(ec,90),'max',max(ec))
