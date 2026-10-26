import struct, json, sys
def load(path, off=8, size=None):
    d=open(path,'rb').read()
    if size is None: size=struct.unpack_from('<I',d,4)[0]
    return d[off:off+size]
def tag4(buf,off,end):
    while off+8<=end:
        t=buf[off:off+4]; sz=struct.unpack_from('<I',buf,off+4)[0]
        yield t,buf[off+8:off+8+sz]; off+=8+sz
def sub2(buf):
    o=0
    while o+4<=len(buf):
        t=buf[o:o+2].decode('latin1'); n=struct.unpack_from('<H',buf,o+2)[0]
        yield t,buf[o+4:o+4+n]; o+=4+n
def parse(blk):
    top={}
    for t,b in tag4(blk,8,len(blk)): top.setdefault(t,b)
    res=top[b'Res\xcc']; names=[]; 
    for t,b in tag4(res,0,len(res)):
        if t==b'RsNm': names=[s.decode('latin1') for s in b.split(b'\0')]
        if t==b'RsRq': rq=b
    # RsRq: count, then per resource 24 bytes?
    cnt=struct.unpack_from('<I',rq,0)[0]; per=(len(rq)-4)//cnt
    resl=[]
    for i in range(cnt):
        e=rq[4+i*per:4+(i+1)*per]
        h,noff,rtype=struct.unpack_from('<III',e,0)
        nm=names_blob(res)[noff:].split(b'\0')[0].decode('latin1') if True else ''
        resl.append((nm,rtype,e.hex()))
    objs=[]
    for t,b in tag4(top[b'Obj\xcc'],0,len(top[b'Obj\xcc'])):
        if t!=b'FOb\xea': continue
        o={'scripts':[]}
        for t2,b2 in tag4(b,0,len(b)):
            if t2==b'ObjD':
                for k,v in sub2(b2):
                    if k=='Ot': o['type']=struct.unpack('<I',v)[0]
                    elif k=='Oh': o['hash']=struct.unpack('<I',v)[0]
                    elif k=='OP': o['guid'],_,o['flags'],o['res']=struct.unpack('<IIII',v)
                    elif k=='PA': o['parent']=struct.unpack('<I',v)[0]
                    elif k=='St': o['text']=v.decode('utf-16le','replace').rstrip('\0')
                    elif k=='SH': o['lang']=struct.unpack('<I',v)[0]
                    elif k=='Sj': o['just']=struct.unpack('<I',v)[0]
                    elif k=='SA':
                        f=struct.unpack('<4I',v[:16]); o['col']=f  # b g r a
                        fl=struct.unpack('<%df'%((len(v)-16)//4),v[16:])
                        o['pivot']=fl[0:3]; o['pos']=fl[3:6]; o['quat']=fl[6:10]; o['size']=fl[10:13]
                        if len(fl)>=17: o['uv']=fl[13:17]
                    else: o.setdefault('other',{})[k]=v.hex()
            elif t2==b'Scrp':
                o['scripts'].append(struct.unpack_from('<I',b2,0)[0])
        objs.append(o)
    return names,resl,objs
_nb=None
def names_blob(res):
    for t,b in tag4(res,0,len(res)):
        if t==b'RsNm': return b
if __name__=='__main__':
    blk=load(sys.argv[1]) if len(sys.argv)>1 else load('/mnt/user-data/uploads/NFSU2/GLOBAL/InGameFreeRoam.bun')
    names,resl,objs=parse(blk)
    for i,r in enumerate(resl): print(i,r[0],r[1])
    print(len(objs),'objects')
