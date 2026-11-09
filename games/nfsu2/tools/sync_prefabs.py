"""Bring the game's prefab folder into the repo before a build.
Usage: python3 -I tools/sync_prefabs.py <copy of scripts\\NFSU2_REUI_prefabs> [prefabs dir]
 * every name in _deleted.log (one per line, written by the editor's Delete) is removed from the repo
 * every <name>.txt there is copied in (new and changed prefabs)
Prints what it did. Afterwards the game's _deleted.log can be emptied (the names are gone from the build)."""
import os, shutil, sys

src = sys.argv[1]
dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'prefabs')
os.makedirs(dst, exist_ok=True)
log = os.path.join(src, '_deleted.log')
deleted = []
if os.path.exists(log):
    for line in open(log, encoding='latin1'):
        n = line.strip()
        if n and n not in deleted: deleted.append(n)
copied = []
for f in sorted(os.listdir(src)):
    if f.lower().endswith('.txt') and not f.startswith('_') and f[:-4] not in deleted:
        a, b = os.path.join(src, f), os.path.join(dst, f)
        if not os.path.exists(b) or open(a, 'rb').read() != open(b, 'rb').read():
            shutil.copyfile(a, b); copied.append(f[:-4])
removed = []
for n in deleted:
    p = os.path.join(dst, n + '.txt')
    if os.path.exists(p): os.remove(p); removed.append(n)
print('prefabs: copied %s; removed %s' % (copied or 'none', removed or 'none'))
