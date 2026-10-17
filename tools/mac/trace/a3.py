import sys,re,bisect; import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); from xt import load
from collections import Counter, defaultdict
def pct(xs,p): xs=sorted(xs); return xs[min(len(xs)-1,int(p/100*len(xs)))]
def T(path):
    c,r=load(path); ix={k:i for i,k in enumerate(c)}; return r,(lambda row,k,j=0: row[ix[k]][j])
sl,gs=T('mst/metal-shader-profiler-shader-list.xml')
print('shader list by type',Counter(gs(x,'shader-type') for x in sl), 'unique ids',len({gs(x,'id') for x in sl}))
# encoder labels
re_,ge=T('mst/metal-application-encoders-list.xml'); elab={ge(x,'encoder-id'):re.sub(r'\s+',' ',ge(x,'encoder-label',1) or '') for x in re_}
r,g=T('mst/metal-gpu-intervals.xml'); r=[x for x in r if 'fable' in (g(x,'process',1) or '')]
t=defaultdict(int); fbusy=defaultdict(list); n=Counter()
for x in r:
    s=int(g(x,'start')); d=int(g(x,'duration')); k=(elab.get(g(x,'encoder-id'),'?')[:60],g(x,'channel-name')); t[k]+=d; n[k]+=1
    fbusy[g(x,'frame-number')].append((s,s+d))
pass
if 0:
 for k,v in sorted(t.items(),key=lambda z:-z[1])[:10]: print('  ',k,round(v/1e6,1),n[k],round(v/n[k]/1e3,1))
durs=[int(g(x,'duration')) for x in r]; print('fable gpu intervals',len(durs),'p50 %.1f us p90 %.1f us'%(pct(durs,50)/1e3,pct(durs,90)/1e3), 'share <50us of time %.1f%%'%(100*sum(d for d in durs if d<50000)/sum(durs)))
def union(iv):
    iv=sorted(iv); out=[]
    for a,b in iv:
        if out and a<=out[-1][1]: out[-1][1]=max(out[-1][1],b)
        else: out.append([a,b])
    return out
# per frame
rows=[]
for f,iv in fbusy.items():
    if f is None: continue
    u=union(iv); rows.append((int(f),u[0][0],u[-1][1],sum(b-a for a,b in u)))
rows.sort(); bus=[x[3] for x in rows[1:-1]]; span=[x[2]-x[1] for x in rows[1:-1]]
st=[b[1]-a[1] for a,b in zip(rows,rows[1:])][1:-1]
print('frames',len(rows),'GPU busy/frame p50 %.1f p90 %.1f ms; GPU span/frame p50 %.1f; frame-to-frame start p50 %.1f ms'%(pct(bus,50)/1e6,pct(bus,90)/1e6,pct(span,50)/1e6,pct(st,50)/1e6))
# gaps vs pending CBs
rs,g2=T('mst/metal-application-command-buffer-submissions.xml'); sub={g2(x,'cmdbuffer-id'):int(g2(x,'start'))+int(g2(x,'duration')) for x in rs if 'fable' in (g2(x,'process',1) or '') and int(g2(x,'num-encoders'))>0}
rc,g3=T('mst/metal-command-buffer-completed.xml'); comp={g3(x,'cmdbuffer-id'):int(g3(x,'timestamp')) for x in rc}
# first gpu start per cb
first={}; last={}
for x in r:
    c=g(x,'cmdbuffer-id'); s=int(g(x,'start')); e=s+int(g(x,'duration')); first[c]=min(first.get(c,1<<62),s); last[c]=max(last.get(c,0),e)
qw=[first[c]-sub[c] for c in sub if c in first]; tail=[comp[c]-last[c] for c in sub if c in comp and c in last]
print('queue wait (commit->first GPU) p50 %.1f p90 %.1f ms; last GPU->completed p50 %.2f ms'%(pct(qw,50)/1e6,pct(qw,90)/1e6,pct(tail,50)/1e6))
# allg union for all procs
u=union([(int(g(x,'start')),int(g(x,'start'))+int(g(x,'duration'))) for x in r])
pend=[(sub[c],last.get(c,comp.get(c))) for c in sub]
cat=Counter(); cs=Counter()
for (a,b),(c,d) in zip(u,u[1:]):
    gap=c-b
    if gap<100000: k='<0.1ms'
    else:
        p=any(s<=b and e>=c for s,e in pend)  # a CB already committed whose GPU work not finished spans the whole gap
        k='>=0.1ms pending-CB' if p else '>=0.1ms no-CB-pending(CPU starve)'
    cat[k]+=1; cs[k]+=gap
print({k:(cat[k],round(cs[k]/1e6,1)) for k in cat})
