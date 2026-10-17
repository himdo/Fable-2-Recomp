# Usage: phases.py <game log>... -> 3D entry time, FPS and ms/frame in [+3,+40] and [+28,+40]
import re,sys
from datetime import datetime
def load(p):
    t0=None; s=[]
    for l in open(p,errors='replace'):
        m=re.match(r'\[(\S+ \S+)\]',l)
        if m and t0 is None: t0=datetime.strptime(m.group(1),'%Y-%m-%d %H:%M:%S.%f')
        m=re.match(r'\[(\S+ \S+)\].*guest frame \(gate\) ~([0-9.]+) Hz',l)
        if m: s.append(((datetime.strptime(m.group(1),'%Y-%m-%d %H:%M:%S.%f')-t0).total_seconds(),float(m.group(2))))
    return s
def win(s,a,b):
    v=[h for t,h in s if a<=t<=b]
    if not v: return 'n/a'
    f=sum(v)/len(v); return f"{f:.2f} FPS {1000/f:.0f} ms (n={len(v)})"
for p in sys.argv[1:]:
    s=load(p); e=None
    for i in range(1,len(s)):
        nxt=[h for _,h in s[i:i+8]]
        if s[i][1]<20 and s[i-1][1]>=20 and sum(h<20 for h in nxt)>=6: e=s[i][0]; break
    if e is None: print(p,'no 3D entry'); continue
    print(f"{p.split('/')[-1]} entry={e:.0f}s end={s[-1][0]:.0f}s [+3,+40]: {win(s,e+3,e+40)} | [+28,+40]: {win(s,e+28,e+40)}")
