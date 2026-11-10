// See bpk.h. The LZX decoder is written from the format description (Microsoft's LZX DELTA / CAB LZX documentation);
// it is the joyride-turbo-re repo's tools/bpk/unbpk.c, checked against every menu movie's zlib checksum.
// XMemCompress specifics: frames restart the bit reader at their own chunk, stored blocks have no pad byte, a match
// may run past a block end (it counts towards the next block), and input is not re-aligned inside stored data.
#include "bpk.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace bpk {
namespace {

struct Bits {
    const uint8_t* in; size_t size, pos;
    uint64_t buf;  // MSB-aligned
    int bits;
    void Fill() {
        while (bits <= 48) {
            uint32_t w = 0;
            if (pos + 1 < size) w = in[pos] | (uint32_t)in[pos + 1] << 8;
            pos += 2;
            buf |= (uint64_t)w << (48 - bits);
            bits += 16;
        }
    }
    uint32_t Peek(int n) { Fill(); return n ? (uint32_t)(buf >> (64 - n)) : 0; }
    void Drop(int n) { buf <<= n; bits -= n; }
    uint32_t Read(int n) { uint32_t v = Peek(n); Drop(n); return v; }
};

// Canonical Huffman codes through a full 16-bit lookup table: sym | (len - 1) << 12 (0xFFFF: no code).
struct Tree {
    std::vector<uint16_t> table = std::vector<uint16_t>(1 << 16);
    uint8_t len[656] = {};
    int n = 0;
    bool Build() {
        int count[17] = {}, next[17] = {};
        for (int i = 0; i < n; ++i) count[len[i]]++;
        count[0] = 0;
        int code = 0;
        for (int l = 1; l <= 16; ++l) { code = (code + count[l - 1]) << 1; next[l] = code; }
        std::fill(table.begin(), table.end(), (uint16_t)0xFFFF);
        for (int i = 0; i < n; ++i) {
            const int l = len[i];
            if (!l) continue;
            const int c = next[l]++;
            if (c >= (1 << l)) return false;
            const uint32_t first = (uint32_t)c << (16 - l), span = 1u << (16 - l);
            for (uint32_t k = 0; k < span; ++k) table[first + k] = (uint16_t)(i | (l - 1) << 12);
        }
        return true;
    }
    int Sym(Bits& b) const {
        const uint16_t e = table[b.Peek(16)];
        if (e == 0xFFFF) return -1;
        b.Drop((e >> 12) + 1);
        return e & 0xFFF;
    }
};

const uint8_t kExtra[51] = {0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,
                            7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
                            16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17};

class Lzx {
public:
    Lzx() { for (int i = 0, base = 0; i < 51; ++i) { base_[i] = (uint32_t)base; base += 1 << kExtra[i]; } }

    // One block: `total` bytes to out from the frames' payloads (chunk[k] = where frame k starts in `in`).
    // `history` bytes before out are the earlier output. Returns false if it does not decode.
    bool Decode(const uint8_t* in, size_t inSize, const std::vector<size_t>& chunk, uint8_t* out, size_t total,
                size_t history) {
        const int slots = 34;   // a 128 KB window
        Bits b{in, inSize, 0, 0, 0};
        uint32_t R0 = 1, R1 = 1, R2 = 1;
        main_.n = 256 + slots * 8; len_.n = 249; align_.n = 8;
        memset(main_.len, 0, sizeof main_.len);
        memset(len_.len, 0, sizeof len_.len);
        if (b.Read(1)) return false;   // E8 translation: not used by the game
        size_t o = 0, frameEnd = 32768;
        int type = 0;
        long remaining = 0;
        while (o < total) {
            if (remaining <= 0) {
                const long carry = remaining < 0 ? -remaining : 0;
                type = (int)b.Read(3);
                remaining = (long)(b.Read(16) << 8 | b.Read(8)) - carry;
                if (type == 2) {
                    for (int i = 0; i < 8; ++i) align_.len[i] = (uint8_t)b.Read(3);
                    if (!align_.Build()) return false;
                }
                if (type == 1 || type == 2) {
                    if (!Lengths(b, main_.len, 0, 256) || !Lengths(b, main_.len, 256, main_.n) || !main_.Build()) return false;
                    if (!Lengths(b, len_.len, 0, 249)) return false;
                    bool any = false;
                    for (int i = 0; i < 249; ++i) any |= len_.len[i] != 0;
                    if (any && !len_.Build()) return false;
                } else if (type == 3) {
                    // align to 16 bits (a whole padding word when already aligned), R0-R2, then the raw bytes
                    b.Fill();
                    const int r = b.bits & 15;
                    b.Drop(r ? r : 16);
                    size_t p = b.pos - (size_t)(b.bits / 8);
                    b.bits = 0; b.buf = 0;
                    if (p + 12 > inSize) return false;
                    auto U32 = [&](size_t q) { return in[q] | in[q + 1] << 8 | in[q + 2] << 16 | (uint32_t)in[q + 3] << 24; };
                    R0 = U32(p); R1 = U32(p + 4); R2 = U32(p + 8);
                    p += 12;
                    size_t n = (size_t)remaining;
                    if (n > total - o) n = total - o;
                    if (p + n > inSize) return false;
                    memcpy(out + o, in + p, n);
                    o += n; p += n;
                    remaining = 0;
                    b.pos = p;
                    // frames wholly inside the stored bytes are skipped; ending on a frame end restarts below
                    while (o > frameEnd) frameEnd += 32768;
                } else {
                    return false;
                }
            }
            while (remaining > 0 && o < frameEnd && o < total) {
                int s = main_.Sym(b);
                if (s < 0) return false;
                if (s < 256) { out[o++] = (uint8_t)s; remaining--; continue; }
                s -= 256;
                int len = s & 7;
                if (len == 7) {
                    const int l = len_.Sym(b);
                    if (l < 0) return false;
                    len += l;
                }
                len += 2;
                const int slot = s >> 3;
                uint32_t off;
                if (slot == 0) {
                    off = R0;
                } else if (slot == 1) {
                    off = R1; R1 = R0; R0 = off;
                } else if (slot == 2) {
                    off = R2; R2 = R0; R0 = off;
                } else {
                    const int extra = kExtra[slot];
                    off = base_[slot] - 2;
                    if (type == 2 && extra >= 3) {
                        if (extra > 3) off += b.Read(extra - 3) << 3;
                        const int a = align_.Sym(b);
                        if (a < 0) return false;
                        off += (uint32_t)a;
                    } else if (extra) {
                        off += b.Read(extra);
                    }
                    R2 = R1; R1 = R0; R0 = off;
                }
                if (off == 0 || off > o + history) return false;
                for (int i = 0; i < len && o < total; ++i, ++o) out[o] = out[(ptrdiff_t)o - (ptrdiff_t)off];
                remaining -= len;
            }
            if (o >= frameEnd) {
                const size_t k = frameEnd / 32768;   // the next frame's bits start at its own chunk
                if (k < chunk.size()) { b.pos = chunk[k]; b.bits = 0; b.buf = 0; }
                frameEnd += 32768;
            }
        }
        return true;
    }

private:
    Tree pre_, main_, len_, align_;
    uint32_t base_[51];

    // delta-coded lengths for symbols [first, last) through the 20-symbol pretree
    bool Lengths(Bits& b, uint8_t* len, int first, int last) {
        pre_.n = 20;
        for (int i = 0; i < 20; ++i) pre_.len[i] = (uint8_t)b.Read(4);
        if (!pre_.Build()) return false;
        for (int x = first; x < last;) {
            const int z = pre_.Sym(b);
            if (z < 0) return false;
            if (z == 17) {
                int run = 4 + (int)b.Read(4);
                while (run-- && x < last) len[x++] = 0;
            } else if (z == 18) {
                int run = 20 + (int)b.Read(5);
                while (run-- && x < last) len[x++] = 0;
            } else if (z == 19) {
                int run = 4 + (int)b.Read(1);
                const int z2 = pre_.Sym(b);
                if (z2 < 0) return false;
                const uint8_t v = (uint8_t)((len[x] + 17 - z2) % 17);
                while (run-- && x < last) len[x++] = v;
            } else {
                len[x] = (uint8_t)((len[x] + 17 - z) % 17);
                ++x;
            }
        }
        return true;
    }
};

uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
unsigned Be16(const uint8_t* p) { return (unsigned)p[0] << 8 | p[1]; }

#ifndef _WIN32
int _fseeki64(FILE* f, int64_t o, int w) { return fseeko(f, (off_t)o, w); }
#endif
struct FileCloser { void operator()(FILE* f) const { if (f) fclose(f); } };

} // namespace

bool Stream(const std::filesystem::path& file,
            const std::function<bool(const uint8_t* data, size_t size, uint64_t at)>& fn, std::string& err) {
#ifdef _WIN32
    std::unique_ptr<FILE, FileCloser> f(_wfopen(file.wstring().c_str(), L"rb"));
#else
    std::unique_ptr<FILE, FileCloser> f(fopen(file.string().c_str(), "rb"));
#endif
    if (!f) { err = "cannot open " + file.string(); return false; }
    std::error_code ec;
    const uint64_t fileSize = std::filesystem::file_size(file, ec);
    if (ec || fileSize < 16) { err = "cannot read " + file.string(); return false; }
    uint8_t head[16];
    if (fread(head, 1, 16, f.get()) != 16 || Be32(head) != 0x0FF512EDu) { err = "not a BigPark file"; return false; }
    const size_t blocks = (size_t)((fileSize + 0x1FFFF) >> 17);
    std::vector<uint8_t> table(blocks * 4);
    if (fread(table.data(), 1, table.size(), f.get()) != table.size()) { err = "short block table"; return false; }
    uint32_t maxOut = 0;
    for (size_t i = 0; i < blocks; ++i) maxOut = std::max(maxOut, Be32(&table[i * 4]));

    const size_t kHist = 1 << 17;
    std::vector<uint8_t> buf(kHist + maxOut);   // [history][this block's output]
    std::vector<uint8_t> raw(1 << 17), payload((1 << 17) + 16);
    std::vector<size_t> chunk;
    auto lzx = std::make_unique<Lzx>();
    size_t hist = 0;
    uint64_t at = 0;
    for (size_t i = 0; i < blocks; ++i) {
        const uint32_t size = Be32(&table[i * 4]);
        const uint64_t start = i ? (uint64_t)i << 17 : 16 + table.size();
        const uint64_t end = std::min<uint64_t>(((uint64_t)i + 1) << 17, fileSize);
        const size_t rawSize = (size_t)(end - start);
        if (_fseeki64(f.get(), (int64_t)start, SEEK_SET) || fread(raw.data(), 1, rawSize, f.get()) != rawSize) {
            err = "read error in block " + std::to_string(i); return false;
        }
        uint8_t* out = buf.data() + hist;
        if (rawSize >= 2 && Be16(raw.data()) == 0) {
            // a stored block (incompressible): a zero word, then the bytes
            if (2 + (size_t)size > rawSize) { err = "stored block " + std::to_string(i) + " past its end"; return false; }
            memcpy(out, raw.data() + 2, size);
        } else {
            size_t off = 0, in = 0;
            chunk.clear();
            for (uint32_t k = 0; k < (size + 32767) / 32768 && off + 2 <= rawSize; ++k) {
                unsigned c;
                if (raw[off] == 0xFF) { if (off + 5 > rawSize) break; c = Be16(&raw[off + 3]); off += 5; }
                else { c = Be16(&raw[off]); off += 2; }
                if (off + c > rawSize || in + c > payload.size()) { err = "bad frame in block " + std::to_string(i); return false; }
                chunk.push_back(in);
                memcpy(&payload[in], &raw[off], c);
                in += c; off += c;
            }
            if (!lzx->Decode(payload.data(), in, chunk, out, size, hist)) { err = "block " + std::to_string(i) + " did not decode"; return false; }
        }
        if (!fn(out, size, at)) return true;
        at += size;
        // keep the last 128 KB as the next block's history
        const size_t have = hist + size, keep = std::min(have, kHist);
        memmove(buf.data(), buf.data() + have - keep, keep);
        hist = keep;
    }
    return true;
}

} // namespace bpk
