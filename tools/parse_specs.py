import re, sys, html
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = os.path.join(_ROOT, 'downloads/specs-pages/prodpage-enus.html')
data = open(path, encoding="utf-8", errors="replace").read()
# Facet objects look like: {facet:"Name", values:[{value:"...", ...}], category? ...}
pat = re.compile(r'facet:"((?:[^"\\]|\\.)*)"(?:,\s*(?:cat(?:egory)?)?:"((?:[^"\\]|\\.)*)")?', re.S)
# simpler: iterate over each {...facet..} object by scanning balanced braces
objs = []
idx = 0
key = '{facet:'
pos = data.find(key)
while pos != -1:
    # find end of this object by brace balancing
    depth = 0
    i = pos
    instr = False
    esc = False
    while i < len(data):
        ch = data[i]
        if instr:
            if esc: esc = False
            elif ch == '\\': esc = True
            elif ch == '"': instr = False
        else:
            if ch == '"': instr = True
            elif ch == '{': depth += 1
            elif ch == '}':
                depth -= 1
                if depth == 0:
                    break
        i += 1
    objs.append(data[pos:i+1])
    pos = data.find(key, i+1)
print("OBJECTS:", len(objs))
for o in objs:
    m = re.search(r'facet:"((?:[^"\\]|\\.)*)"', o)
    vals = re.findall(r'value:"((?:[^"\\]|\\.)*)"', o)
    if m:
        vals = sorted(set(vals))
        f = m.group(1).encode().decode('unicode_escape', errors='replace')
        vv = "; ".join(v.encode().decode('unicode_escape', errors='replace') for v in vals)
        vv = html.unescape(html.unescape(vv)).replace('<','<').replace('>','>')
        print(f"{f} :: {vv[:400]}")
