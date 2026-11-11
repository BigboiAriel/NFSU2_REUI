// See flash_motion.h. Follows tools/fe/flashplay.py (the Joy Ride port) closely.
#include "flash_motion.h"
#include "swf_fonts.h"
#include "uied.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace flash_motion {
namespace {

std::string g_dir;   // <REUI>\cache\movies\

uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t U32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

struct Bits {   // most significant bit first
    const uint8_t* d; size_t n, p, b = 0;
    Bits(const uint8_t* d_, size_t n_, size_t p_) : d(d_), n(n_), p(p_) {}
    uint32_t U(int k) {
        uint32_t v = 0;
        for (int i = 0; i < k; ++i) {
            const size_t at = p + (b >> 3);
            const uint8_t byte = at < n ? d[at] : 0;
            v = (v << 1) | ((byte >> (7 - (b & 7))) & 1); ++b;
        }
        return v;
    }
    int32_t S(int k) { if (!k) return 0; uint32_t v = U(k); return (v & (1u << (k - 1))) ? (int32_t)(v - (1ull << k)) : (int32_t)v; }
    size_t End() const { return p + (b + 7) / 8; }
};

struct M { double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0; };   // x' = a x + c y + tx, y' = b x + d y + ty
struct CX { double m[4] = { 1, 1, 1, 1 }, add[4] = { 0, 0, 0, 0 }; };
M Mul(const M& p, const M& c) {   // p after c
    return { p.a * c.a + p.c * c.b, p.b * c.a + p.d * c.b, p.a * c.c + p.c * c.d, p.b * c.c + p.d * c.d,
             p.a * c.tx + p.c * c.ty + p.tx, p.b * c.tx + p.d * c.ty + p.ty };
}
CX CxMul(const CX& p, const CX& c) { CX o; for (int i = 0; i < 4; ++i) { o.m[i] = p.m[i] * c.m[i]; o.add[i] = p.m[i] * c.add[i] + p.add[i]; } return o; }
size_t ReadMatrix(const uint8_t* d, size_t n, size_t p, M& m) {
    Bits b(d, n, p); m = M();
    if (b.U(1)) { int k = b.U(5); m.a = b.S(k) / 65536.0; m.d = b.S(k) / 65536.0; }
    if (b.U(1)) { int k = b.U(5); m.b = b.S(k) / 65536.0; m.c = b.S(k) / 65536.0; }
    int k = b.U(5); m.tx = b.S(k) / 20.0; m.ty = b.S(k) / 20.0;
    return b.End();
}
size_t ReadCx(const uint8_t* d, size_t n, size_t p, CX& c) {   // (with alpha)
    Bits b(d, n, p); c = CX();
    const bool add = b.U(1), mult = b.U(1); const int k = b.U(4);
    if (mult) for (int i = 0; i < 4; ++i) c.m[i] = b.S(k) / 256.0;
    if (add) for (int i = 0; i < 4; ++i) c.add[i] = b.S(k) / 255.0;
    return b.End();
}

struct Tag { int code; const uint8_t* d; size_t n; };
using Frame = std::vector<Tag>;
struct Char {
    enum { NONE, SPRITE, SHAPE, EDIT, IMAGE } kind = NONE;
    std::vector<Frame> frames;            // sprite
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // shape / edit bounds
    int bitmap = -1;                      // shape: its fill's image
    std::string image;                    // image: its name (as the picture cache names it)
    std::string name;                     // sprite: an export / instance name
};
struct Movie {
    std::vector<uint8_t> body;
    double fps = 30, stageH = 720;
    std::map<int, Char> chars;
    std::vector<Frame> root;
    std::vector<uied::MotionClipInfo> clips;
    std::vector<std::pair<int, std::pair<int, int>>> clipAt;   // each clip: its sprite (-1: the movie's root), frames
};

std::vector<Tag> Tags(const uint8_t* d, size_t n, size_t p) {
    std::vector<Tag> out;
    while (p + 2 <= n) {
        const uint16_t h = U16(d + p); p += 2;
        const int code = h >> 6; size_t len = h & 0x3F;
        if (len == 0x3F) { if (p + 4 > n) break; len = U32(d + p); p += 4; }
        if (p + len > n) break;
        out.push_back({ code, d + p, len });
        p += len;
        if (code == 0) break;
    }
    return out;
}
std::string PicName(std::string n) {   // a GFx external image's name -> the picture cache's ("hangoutmainmenu_i22")
    const size_t s = n.find_last_of("\\/"); if (s != std::string::npos) n = n.substr(s + 1);
    for (auto& ch : n) ch = (char)tolower((unsigned char)ch);
    if (n.rfind("bpk_fe_", 0) == 0) n = n.substr(7);
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".dds") == 0) n.resize(n.size() - 4);
    return n;
}
void Shape(Movie& m, int code, const uint8_t* x, size_t n) {
    if (n < 3) return;
    Char c; c.kind = Char::SHAPE;
    const int id = U16(x);
    Bits b(x, n, 2); int k = b.U(5);
    double r[4]; for (double& v : r) v = b.S(k) / 20.0;
    c.x0 = r[0]; c.x1 = r[1]; c.y0 = r[2]; c.y1 = r[3];
    size_t q = b.End();
    if (code == 83) { Bits e(x, n, q); k = e.U(5); for (int i = 0; i < 4; ++i) e.S(k); q = e.End() + 1; }
    if (q < n) {
        int cnt = x[q++];
        if (cnt == 0xFF && q + 2 <= n) { cnt = U16(x + q); q += 2; }
        for (int i = 0; i < cnt && q < n; ++i) {
            const int t = x[q++];
            if (t == 0) q += code >= 32 ? 4 : 3;
            else if (t >= 0x40 && t <= 0x43) {
                if (q + 2 > n) break;
                const int bid = U16(x + q); q += 2;
                M mm; q = ReadMatrix(x, n, q, mm);
                if (bid != 0xFFFF && c.bitmap < 0) c.bitmap = bid;
            } else break;
        }
    }
    m.chars[id] = c;
}
std::vector<Frame> Frames(Movie& m, const std::vector<Tag>& tl, std::map<int, std::string>& names) {
    std::vector<Frame> frames(1);
    for (const Tag& t : tl) {
        if (t.code == 1) { frames.emplace_back(); continue; }
        if (t.code == 0) break;
        frames.back().push_back(t);
        if (t.code == 39 && t.n >= 4) {   // a sprite
            const int id = U16(t.d);
            Char c; c.kind = Char::SPRITE;
            c.frames = Frames(m, Tags(t.d, t.n, 4), names);
            m.chars[id] = std::move(c);
        } else if (t.code == 2 || t.code == 22 || t.code == 32 || t.code == 83) Shape(m, t.code, t.d, t.n);
        else if (t.code == 37 && t.n >= 3) {   // edit text: its bounds
            Char c; c.kind = Char::EDIT;
            Bits b(t.d, t.n, 2); int k = b.U(5);
            double r[4]; for (double& v : r) v = b.S(k) / 20.0;
            c.x0 = r[0]; c.x1 = r[1]; c.y0 = r[2]; c.y1 = r[3];
            m.chars[U16(t.d)] = c;
        } else if (t.code == 1009 && t.n >= 12) {   // GFx DefineExternalImage2: id, ..., name length (11), name
            Char c; c.kind = Char::IMAGE;
            const size_t len = t.d[11];
            if (12 + len <= t.n) c.image = PicName(std::string((const char*)t.d + 12, len));
            m.chars[U16(t.d)] = c;
        } else if (t.code == 56 && t.n >= 2) {   // ExportAssets: id, name pairs
            size_t q = 2; const int cnt = U16(t.d);
            for (int i = 0; i < cnt && q + 2 < t.n; ++i) {
                const int id = U16(t.d + q); q += 2;
                const char* s = (const char*)t.d + q; const size_t l = strnlen(s, t.n - q);
                names[id] = std::string(s, l); q += l + 1;
            }
        } else if ((t.code == 26 || t.code == 70) && t.n >= 3) {   // PlaceObject2/3 with a name: the sprite's instance name
            const uint8_t f = t.d[0]; size_t q = t.code == 26 ? 1 : 2; q += 2;
            if (t.code == 70 && (t.d[1] & 0x08 || (t.d[1] & 0x10 && f & 0x02))) { while (q < t.n && t.d[q]) ++q; ++q; }
            int cid = -1;
            if (f & 0x02 && q + 2 <= t.n) { cid = U16(t.d + q); q += 2; }
            if (cid < 0 || !(f & 0x20)) continue;
            M mm; CX cx;
            if (f & 0x04) q = ReadMatrix(t.d, t.n, q, mm);
            if (f & 0x08) q = ReadCx(t.d, t.n, q, cx);
            if (f & 0x10) q += 2;
            if (q < t.n && !names.count(cid)) { const char* s = (const char*)t.d + q; names[cid] = std::string(s, strnlen(s, t.n - q)); }
        }
    }
    if (frames.back().empty() && frames.size() > 1) frames.pop_back();
    return frames;
}
bool HasStop(const Frame& fr) {   // a DoAction with stop() at its top level (function bodies skipped)
    for (const Tag& t : fr) {
        if (t.code != 12) continue;
        size_t i = 0;
        while (i < t.n && t.d[i]) {
            const uint8_t c = t.d[i++];
            if (c >= 0x80) {
                if (i + 2 > t.n) break;
                const size_t len = U16(t.d + i); i += 2;
                if ((c == 0x9B || c == 0x8E) && len >= 2 && i + len <= t.n) i += len + U16(t.d + i + len - 2);
                else i += len;
            }
            if (c == 0x07) return true;
        }
    }
    return false;
}

struct Inst;
struct Kid { int cid = 0; M m; CX cx; std::string name; std::unique_ptr<Inst> inst; bool fresh = false; };
struct Inst {
    const Movie* mv; const std::vector<Frame>* frames; int f = 0; bool stopped = false;
    std::map<int, Kid> kids;   // by depth
    Inst(const Movie* m, const std::vector<Frame>* fr) : mv(m), frames(fr) {}
    void Apply(const Frame& fr) {
        for (const Tag& t : fr) {
            if (t.code == 26 || t.code == 70) {
                if (t.n < 3) continue;
                const uint8_t f = t.d[0]; size_t q = t.code == 26 ? 1 : 2;
                const int dep = U16(t.d + q); q += 2;
                if (t.code == 70 && (t.d[1] & 0x08 || (t.d[1] & 0x10 && f & 0x02))) { while (q < t.n && t.d[q]) ++q; ++q; }
                auto it = kids.find(dep);
                Kid* e = it != kids.end() ? &it->second : nullptr;
                if (f & 0x02 && q + 2 <= t.n) {
                    const int cid = U16(t.d + q); q += 2;
                    if (!(f & 0x01) || !e || e->cid != cid) {
                        Kid k; k.cid = cid;
                        auto c = mv->chars.find(cid);
                        if (c != mv->chars.end() && c->second.kind == Char::SPRITE) { k.inst = std::make_unique<Inst>(mv, &c->second.frames); k.fresh = true; }
                        kids[dep] = std::move(k); e = &kids[dep];
                    }
                }
                if (!e) continue;
                if (f & 0x04) q = ReadMatrix(t.d, t.n, q, e->m);
                if (f & 0x08) q = ReadCx(t.d, t.n, q, e->cx);
                if (f & 0x10) q += 2;
                if (f & 0x20 && q < t.n) { const char* s = (const char*)t.d + q; e->name = std::string(s, strnlen(s, t.n - q)); }
            } else if (t.code == 5 && t.n >= 4) kids.erase(U16(t.d + 2));
            else if (t.code == 28 && t.n >= 2) kids.erase(U16(t.d));
        }
    }
    void Advance(bool force = false) {   // force: the playhead goes on past stop() (the clip being sampled)
        for (auto& kv : kids) { Kid& k = kv.second; if (k.inst && !k.fresh) k.inst->Advance(); }
        for (auto& kv : kids) kv.second.fresh = false;
        const int n = (int)frames->size();
        int nf;
        if (f == 0) nf = 1; else if (stopped && !force) nf = f; else nf = f < n ? f + 1 : 1;
        if (nf != f && n > 0) {
            if (nf == 1 && f) kids.clear();
            Apply((*frames)[nf - 1]);
            for (auto& kv : kids) { Kid& k = kv.second; if (k.fresh && k.inst) { k.inst->Advance(); k.fresh = false; } }
            f = nf;
            if (HasStop((*frames)[nf - 1])) stopped = true;
        }
    }
};

std::mutex g_mu;
std::map<std::string, std::shared_ptr<Movie>> g_movies;

std::string Path(const std::string& movie) {
    std::string p = g_dir + movie + ".gfx";
#ifdef _WIN32
    for (auto& c : p) if (c == '/') c = '\\';
#endif
    return p;
}
std::shared_ptr<Movie> Load(const std::string& movie, std::string& err) {
    std::lock_guard<std::mutex> l(g_mu);
    auto it = g_movies.find(movie);
    if (it != g_movies.end()) return it->second;
    std::vector<uint8_t> file;
    if (FILE* f = fopen(Path(movie).c_str(), "rb")) {
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n > 0) { file.resize((size_t)n); if (fread(file.data(), 1, file.size(), f) != file.size()) file.clear(); }
        fclose(f);
    }
    if (file.empty()) { err = "no " + Path(movie) + " (the picture cache keeps the movies: rebuilt once with this version)"; return nullptr; }
    auto m = std::make_shared<Movie>();
    if (!swf_fonts::Body(file, m->body, err)) return nullptr;
    const uint8_t* d = m->body.data(); const size_t n = m->body.size();
    if (n < 8) { err = "too short"; return nullptr; }
    Bits b(d, n, 0); const int k = b.U(5);
    double r[4]; for (double& v : r) v = b.S(k) / 20.0;
    m->stageH = std::max(1.0, r[3] - r[2]);
    const size_t p = b.End();
    if (p + 4 > n) { err = "too short"; return nullptr; }
    m->fps = std::max(1.0, d[p + 1] + d[p] / 256.0);
    std::map<int, std::string> names;
    m->root = Frames(*m, Tags(d, n, p + 4), names);
    for (auto& kv : names) { auto c = m->chars.find(kv.first); if (c != m->chars.end()) c->second.name = kv.second; }
    // the clips: each label to the frame before the next one (a sprite, or the root); sprites with no labels, whole
    auto addClips = [&](int sprite, const std::vector<Frame>& frames, const std::string& who) {
        std::vector<std::pair<int, std::string>> labels;
        for (size_t i = 0; i < frames.size(); ++i)
            for (const Tag& t : frames[i]) if (t.code == 43 && t.n) labels.push_back({ (int)i + 1, std::string((const char*)t.d, strnlen((const char*)t.d, t.n)) });
        if (labels.empty()) {
            if (frames.size() > 1) {
                uied::MotionClipInfo c; c.first = 1; c.last = (int)frames.size();
                c.name = who + ": all " + std::to_string(frames.size()) + " frames";
                m->clips.push_back(c); m->clipAt.push_back({ sprite, { c.first, c.last } });
            }
            return;
        }
        for (size_t i = 0; i < labels.size(); ++i) {
            uied::MotionClipInfo c; c.first = labels[i].first;
            c.last = i + 1 < labels.size() ? labels[i + 1].first - 1 : (int)frames.size();
            if (c.last < c.first) c.last = c.first;
            c.name = who + ": " + labels[i].second + "  (frames " + std::to_string(c.first) + "-" + std::to_string(c.last) + ")";
            m->clips.push_back(c); m->clipAt.push_back({ sprite, { c.first, c.last } });
        }
    };
    addClips(-1, m->root, "the movie");
    for (auto& kv : m->chars)
        if (kv.second.kind == Char::SPRITE)
            addClips(kv.first, kv.second.frames, "sprite " + std::to_string(kv.first) + (kv.second.name.empty() ? "" : " (" + kv.second.name + ")"));
    g_movies[movie] = m;
    return m;
}

std::string Describe(const Movie& mv, int cid, const std::string& path) {
    auto c = mv.chars.find(cid);
    std::string what = "?";
    if (c != mv.chars.end()) {
        const Char& ch = c->second;
        if (ch.kind == Char::SHAPE) {
            auto im = ch.bitmap >= 0 ? mv.chars.find(ch.bitmap) : mv.chars.end();
            if (im != mv.chars.end() && im->second.kind == Char::IMAGE) what = im->second.image;
            else { char b[64]; snprintf(b, sizeof b, "shape %.0fx%.0f", ch.x1 - ch.x0, ch.y1 - ch.y0); what = b; }
        } else if (ch.kind == Char::EDIT) what = "text field";
        else if (ch.kind == Char::SPRITE) what = "sprite " + std::to_string(cid);
    } else what = "imported " + std::to_string(cid);
    return what + "  [" + path + "]";
}

// every leaf (not a sprite) under inst, with its world matrix / colour (relative to the sampled sprite)
struct Leaf { std::string key, desc, image; M m; CX cx; double px = 0, py = 0; };
void Leaves(const Movie& mv, const Inst& in, const M& m0, const CX& c0, const std::string& path, const std::string& names, std::vector<Leaf>& out) {
    for (auto& kv : in.kids) {
        const Kid& k = kv.second;
        const M m = Mul(m0, k.m); const CX cx = CxMul(c0, k.cx);
        const std::string p = path + (path.empty() ? "" : "/") + std::to_string(kv.first);
        const std::string nm = names + (k.name.empty() ? "" : (names.empty() ? "" : ".") + k.name);
        if (k.inst) { Leaves(mv, *k.inst, m, cx, p, nm, out); continue; }
        Leaf l; l.key = p; l.m = m; l.cx = cx;
        l.desc = Describe(mv, k.cid, nm.empty() ? p : nm);
        auto c = mv.chars.find(k.cid);
        if (c != mv.chars.end() && (c->second.kind == Char::SHAPE || c->second.kind == Char::EDIT)) {
            l.px = (c->second.x0 + c->second.x1) / 2; l.py = (c->second.y0 + c->second.y1) / 2;
            if (c->second.kind == Char::SHAPE && c->second.bitmap >= 0) { auto im = mv.chars.find(c->second.bitmap); if (im != mv.chars.end()) l.image = im->second.image; }
        }
        out.push_back(l);
    }
}

bool Sample(const std::string& movie, int clip, std::vector<uied::MotionPiece>& pieces, std::string& err) {
    auto mv = Load(movie, err);
    if (!mv) return false;
    if (clip < 0 || clip >= (int)mv->clips.size()) { err = "no such clip"; return false; }
    const int sprite = mv->clipAt[clip].first, first = mv->clipAt[clip].second.first, last = mv->clipAt[clip].second.second;
    const std::vector<Frame>* frames = &mv->root;
    if (sprite >= 0) { auto c = mv->chars.find(sprite); if (c == mv->chars.end()) { err = "no such sprite"; return false; } frames = &c->second.frames; }
    if (frames->empty() || first > (int)frames->size()) { err = "the clip has no frames"; return false; }
    Inst in(mv.get(), frames);
    for (int guard = 0; in.f < first && guard < 100000; ++guard) in.Advance(true);   // (to the label's frame, past stops)
    pieces.clear();
    std::map<std::string, size_t> at;
    const int n = last - first + 1;
    for (int i = 0; i < n; ++i) {
        if (i) in.Advance(true);
        std::vector<Leaf> ls; Leaves(*mv, in, M(), CX(), "", "", ls);
        for (const Leaf& l : ls) {
            auto it = at.find(l.key);
            if (it == at.end()) {
                uied::MotionPiece p; p.name = l.desc; p.image = l.image; p.frames.assign(n, uied::MotionFrame());
                at[l.key] = pieces.size(); pieces.push_back(p); it = at.find(l.key);
            }
            uied::MotionFrame& f = pieces[it->second].frames[i];
            f.on = true;
            f.x = (float)(l.m.a * l.px + l.m.c * l.py + l.m.tx); f.y = (float)(l.m.b * l.px + l.m.d * l.py + l.m.ty);
            f.sx = (float)std::hypot(l.m.a, l.m.b); f.sy = (float)std::hypot(l.m.c, l.m.d);
            f.rot = (float)(std::atan2(l.m.b, l.m.a) * 180.0 / 3.14159265358979);
            f.a = (float)std::max(0.0, std::min(1.0, l.cx.m[3] + l.cx.add[3]));
            for (int j = 0; j < 3; ++j) f.add[j] = (float)l.cx.add[j];
        }
    }
    return true;
}

} // namespace

void Install(const std::string& movieDir) {
    g_dir = movieDir;
    uied::motionSource.movies = [] {
        std::vector<std::string> out;
        std::error_code ec;
        const std::filesystem::path root(g_dir);
        if (!std::filesystem::exists(root, ec)) return out;
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec) || it->path().extension() != ".gfx") continue;
            std::string rel = std::filesystem::relative(it->path(), root, ec).string();
            rel = rel.substr(0, rel.size() - 4);
            for (auto& c : rel) if (c == '\\') c = '/';
            out.push_back(rel);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    uied::motionSource.clips = [](const std::string& movie, std::vector<uied::MotionClipInfo>& clips, float& fps, float& pxPerUnit, std::string& err) {
        auto mv = Load(movie, err);
        if (!mv) return false;
        clips = mv->clips; fps = (float)mv->fps; pxPerUnit = (float)(mv->stageH / 480.0);
        return true;
    };
    uied::motionSource.sample = [](const std::string& movie, int clip, std::vector<uied::MotionPiece>& pieces, std::string& err) {
        return Sample(movie, clip, pieces, err);
    };
}

} // namespace flash_motion
