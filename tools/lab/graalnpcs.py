# Extract NPC scripts from binary .graal levels (GR-V1.0x).
# Rough: finds lines "<x><y>image#script" after the board; good enough for a scan.
# usage: python3 -I tools/lab/graalnpcs.py level.graal ...
import sys, re, os
def npcs(path):
    data = open(path,'rb').read()
    out = []
    for m in re.finditer(rb'\n([\x20-\xff])([\x20-\xff])([^#\n]{0,64})#([^\n]*)', data):
        img = m.group(3).decode('latin1'); code = m.group(4).decode('latin1')
        if ';' not in code and 'if' not in code: continue
        out.append((ord(m.group(1))-32, ord(m.group(2))-32, img, code.replace('\xa7','\n')))
    return out
if __name__ == '__main__':
    for p in sys.argv[1:]:
        for n in npcs(p):
            print('==', os.path.basename(p), n[0], n[1], n[2]); print(n[3])
