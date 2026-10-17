import xml.etree.ElementTree as ET, sys
def load(path):
    """Yield (cols, rows) where each row = list of (text, fmt) resolved via id/ref."""
    ids={}; cols=[]; rows=[]
    def val(e):
        r=e.get('ref')
        if r is not None: return ids[r]
        v=(e.text, e.get('fmt'), e)
        # for container nodes, keep children resolved
        if len(e): v=(e.text, e.get('fmt'), [val(c) for c in e])
        if e.get('id') is not None: ids[e.get('id')]=v
        return v
    for ev,e in ET.iterparse(path, events=('end',)):
        if e.tag=='col':
            cols.append(e.findtext('mnemonic'))
        elif e.tag=='row':
            rows.append([val(c) for c in e]); e.clear()
    return cols, rows
if __name__=='__main__':
    c,r=load(sys.argv[1]); print(len(r), c)
    for row in r[:int(sys.argv[2]) if len(sys.argv)>2 else 2]:
        print([(x[0],x[1]) for x in row])
