# Static scan of NPC scripts in binary .graal levels against the fork's GS1 lists.
# usage: python3 -I tools/lab/scan.py <folder with .graal files>
# Prints scripts count, then commands and functions with: status, uses, levels.
# Rough: statement starts are found by regex, so garbled lines show up as UNKNOWN.
import sys, glob, os, re, collections
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import graalnpcs
gs1 = os.path.join(here, '..', '..', 'server', 'src', 'scripting', 'gs1')
c = open(os.path.join(gs1, 'GS1Commands.cpp')).read()
cmds = set(re.findall(r'\{hash\("([^"]+)"\), &fn_', c))
client = set(re.findall(r'"([^"]+)"sv', re.search(r'constexpr std::array clientsideCommands =\s*\{(.*?)\};', c, re.S).group(1)))
funcs = set(re.findall(r'\{hash\("([^"]+)"\), &fn_', open(os.path.join(gs1, 'GS1Functions.cpp')).read()))
# commands taking a trailing space in the map ("say ", "set ", ...) are stored with spaces
cmds={c.strip() for c in cmds}
kw={'if','else','while','for','with','function','return','break','continue','true','false','this','int'}
cmd_use=collections.Counter(); cmd_lv=collections.defaultdict(set)
fn_use=collections.Counter(); fn_lv=collections.defaultdict(set)
nscripts=0; nclientmark=0
for p in sorted(glob.glob(sys.argv[1]+'/*.graal')):
    name=os.path.basename(p)
    for (x,y,img,code) in graalnpcs.npcs(p):
        nscripts+=1
        if '//#CLIENTSIDE' in code: nclientmark+=1
        c=re.sub(r'//[^\n]*','',code)
        for m in re.finditer(r'(?:^|[;{}]|\belse\b|\)\s)\s*([a-z][a-z0-9]*)\b(?!\s*(?:[=+\-*/\[.<>!&|%]|\())', c, re.M|re.I):
            w=m.group(1).lower()
            if w in kw: continue
            cmd_use[w]+=1; cmd_lv[w].add(name)
        for m in re.finditer(r'\b([a-z][a-z0-9]*)\s*\(', c, re.I):
            w=m.group(1).lower()
            if w in kw: continue
            fn_use[w]+=1; fn_lv[w].add(name)
print('scripts',nscripts,'with //#CLIENTSIDE',nclientmark)
print('\n# commands: status, uses, levels')
for w,n in cmd_use.most_common():
    st='ok' if w in cmds else ('CLIENTSIDE-ONLY' if w in client else 'UNKNOWN')
    print(f'{w}\t{st}\t{n}\t{len(cmd_lv[w])}')
print('\n# functions')
for w,n in fn_use.most_common():
    st='ok' if w in funcs else 'UNKNOWN'
    print(f'{w}\t{st}\t{n}\t{len(fn_lv[w])}')
