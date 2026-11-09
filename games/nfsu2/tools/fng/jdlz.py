import struct
def jdlz(inp):
    assert inp[:4]==b'JDLZ', inp[:4]
    outLen=struct.unpack_from('<I',inp,8)[0]
    out=bytearray(outLen); f1=1; f2=1; i=16; o=0
    while i<len(inp) and o<outLen:
        if f1==1: f1=inp[i]|0x100; i+=1
        if f2==1: f2=inp[i]|0x100; i+=1
        if f1&1:
            if f2&1:
                ln=(inp[i+1]|((inp[i]&0xF0)<<4))+3; t=(inp[i]&0x0F)+1
            else:
                t=(inp[i+1]|((inp[i]&0xE0)<<3))+17; ln=(inp[i]&0x1F)+3
            i+=2
            for k in range(ln):
                if o>=outLen: break
                out[o]=out[o-t]; o+=1
            f2>>=1
        else:
            out[o]=inp[i]; o+=1; i+=1
        f1>>=1
    return bytes(out)
