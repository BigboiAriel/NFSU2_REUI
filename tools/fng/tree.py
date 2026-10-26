import sys; sys.path.insert(0,'.')
from parse import *
H={}
for l in open('/home/claude/nfsu2-re/docs/dumps/hashes.txt',encoding='latin1'):
    p=l.rstrip('\n').split('\t')
    if len(p)>=3:
        try: H[int(p[2],16)]=p[1]
        except: pass
def bh(s):
    h=0xFFFFFFFF
    for c in s.encode('latin1'): h=(h*33+c)&0xFFFFFFFF
    return h
for v in list(H.values()):
    for s in (v,v.upper(),v.lower()): H.setdefault(bh(s),s)
blk=load(sys.argv[1]) if len(sys.argv)>1 else load('/mnt/user-data/uploads/NFSU2/GLOBAL/InGameFreeRoam.bun')
names,resl,objs=parse(blk)
for n in names: H.setdefault(bh(n.upper()),n); H.setdefault(bh(n),n)
by={o['guid']:o for o in objs}
kids={}
for o in objs: kids.setdefault(o.get('parent',0),[]).append(o)
T={1:'IMG',2:'TXT',5:'GRP',7:'MULTI',9:'CIMG',3:'MOVIE',4:'COLIMG'}
def show(o,depth,ox,oy):
    x=ox+o['pos'][0]; y=oy+o['pos'][1]
    c=o['col']; r=resl[o['res']][0] if o['res']<len(resl) else '?'
    nm=H.get(o['hash'],'%08X'%o['hash'])
    extra=''
    if o['type']==2: extra=' text=%r lang=%s just=%s'%(o.get('text'),H.get(o.get('lang',0),'%08X'%o.get('lang',0)),o.get('just'))
    uv=o.get('uv')
    print('%s%s %s guid=%X res=%s @(%.1f,%.1f z%.1f) size(%.2f,%.2f) rgba(%d,%d,%d,%d) piv(%.1f,%.1f) q%s%s%s flags=%X scr=%s'%('  '*depth,T.get(o['type'],o['type']),nm,o['guid'],r,x,y,o['pos'][2],o['size'][0],o['size'][1],c[2],c[1],c[0],c[3],o['pivot'][0],o['pivot'][1],
        '' if o['quat']==(0,0,0,1) else tuple(round(q,3) for q in o['quat']), ' uv%s'%(tuple(round(u,3) for u in uv),) if uv and uv!=(0,0,1,1) else '', extra, o['flags'], [H.get(s,'%X'%s) for s in o['scripts']]))
    for k in kids.get(o['guid'],[]): show(k,depth+1,x,y)
for o in kids.get(0,[]): show(o,0,0,0)
