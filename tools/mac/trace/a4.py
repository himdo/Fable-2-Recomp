import sys,re; import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); from xt import load
from collections import Counter, defaultdict
def pct(xs,p): xs=sorted(xs); return xs[min(len(xs)-1,int(p/100*len(xs)))] if xs else None
def T(path):
    c,r=load(path); ix={k:i for i,k in enumerate(c)}; return r,(lambda row,k,j=0: row[ix[k]][j])
D=sys.argv[1]
r,g=T(D+'/metal-resource-allocations.xml')
print('resalloc rows',len(r),Counter((g(x,'resource-type'),g(x,'event-type')) for x in r))
for x in r:
    if g(x,'resource-type')!='Buffer': print('  ',g(x,'event-label',1),g(x,'label'),g(x,'resource-size',1))
r,g=T(D+'/metal-object-label.xml')
print('objlabel types',Counter(g(x,'object-type') for x in r)); 
print(' non-encoder labels',Counter(g(x,'label') for x in r if not re.search('Encoder|MTLCommandBuffer',g(x,'label') or '')).most_common(10))
# encoder sequence per CB (ordered by start)
r,g=T(D+'/metal-application-encoders-list.xml')
cb=defaultdict(list); fr={}
for x in r:
    if 'fable' not in (g(x,'process',1) or ''): continue
    cb[g(x,'cmdbuffer-id')].append((int(g(x,'start')),g(x,'encoder-label') or '')); fr[g(x,'cmdbuffer-id')]=g(x,'frame-number')
seg=[]; prev=Counter(); passes_f=Counter(); restarts_f=Counter(); between=Counter()
for c,L in cb.items():
    L.sort(); cur=None; n=0
    for i,(s,l) in enumerate(L):
        if 'RenderEncoder' in l and 'restart' not in l:
            if cur is not None: seg.append(n)
            cur=1; n=0; passes_f[fr[c]]+=1
        elif 'restart' in l:
            n+=1; restarts_f[fr[c]]+=1; prev[L[i-1][1][:45] if i else '-']+=1
        elif 'Render' not in l and cur is not None:
            between[l[:45]]+=1
    if cur is not None: seg.append(n)
print('BeginRendering passes',len(seg),'restarts per pass mean %.2f p50 %s p90 %s max %s; passes w/ 0 restarts %.0f%%'%(sum(seg)/len(seg),pct(seg,50),pct(seg,90),max(seg),100*sum(1 for s in seg if s==0)/len(seg)))
print('encoder before restart',prev.most_common(3))
fs=[k for k in passes_f if k and k!='4294967295']
pp=[passes_f[k] for k in fs]; rr=[restarts_f[k] for k in fs]
print('per frame: passes p50',pct(pp,50),'restarts p50',pct(rr,50),'frames',len(fs))
