import re, sys, collections
WAIT = re.compile(r'__psynch_cvwait|__semwait_signal|__workq_kernreturn|mach_msg2_trap|__accept|semaphore_wait|semaphore_timedwait|__psynch_mutexwait|swtch_pri|start_wqthread|__select|kevent|__ulock_wait|nanosleep|usleep')
lines = open(sys.argv[1]).read().split('\n')
start = next(i for i,l in enumerate(lines) if l.startswith('Call graph:'))
end = next(i for i,l in enumerate(lines) if l.startswith('Total number in stack'))
rx = re.compile(r'^(\s*)[+!:| ]*(\d+) (.*)$')
nodes = []
for l in lines[start+1:end]:
    m = rx.match(l)
    if not m: continue
    depth = len(l) - len(l.lstrip(' +!:|'))
    nodes.append((depth, int(m.group(2)), m.group(3)))
thread=None; busy=collections.Counter(); leafsym=collections.defaultdict(collections.Counter)
for i,(d,c,s) in enumerate(nodes):
    if s.startswith('Thread_'):
        thread = s[:60]; continue
    nxt = nodes[i+1][0] if i+1 < len(nodes) else -1
    # leaf self time = count minus children counts
    children = 0; j=i+1
    while j < len(nodes) and nodes[j][0] > d:
        if nodes[j][0] == min(n[0] for n in nodes[i+1:j+1] if n[0] > d): pass
        j += 1
    kids = [n for n in nodes[i+1:j] if n[0] == (nodes[i+1][0] if i+1<j else -1)]
    self_ = c - sum(k[1] for k in kids)
    if self_ > 0 and not WAIT.search(s):
        busy[thread] += self_
        leafsym[thread][re.sub(r'\s+\(in .*','',s)[:70]] += self_
for t,n in busy.most_common(8):
    print(f"{n:6d}  {t}")
    for s,k in leafsym[t].most_common(5):
        print(f"          {k:5d} {s}")
