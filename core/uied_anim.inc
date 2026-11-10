// Animation mode (included by uied.cpp): a workspace for animating elements.
//
//   +--------------------------------------------+--------------+-----------+
//   |                                            | animation    | elements  |
//   |   the game, with the layout being edited   | page (the    | (the      |
//   |                                            | focused      | layout's) |
//   |                                            | element)     |           |
//   |                                            +--------------+-----------+
//   |                                            | graph: keys + curves of   |
//   |                                            | the last focused entry    |
//   +--------------------------------------------+---------------------------+
//   | timeline: play controls, ruler, playhead, a row of keys per track      |
//   | (expand: taller, with a big keys + curves graph)                       |
//   +------------------------------------------------------------------------+
//
// The keys / curves are the planned animation model (clips of tracks of eased keys, times in ms) and edit for real;
// they are kept for this session only and don't move the elements yet. Everything else on the animation page is a
// placeholder for a planned feature, marked "planned".

namespace anim {

enum Ease { EZ_LINEAR, EZ_IN, EZ_OUT, EZ_INOUT, EZ_BACK_IN, EZ_BACK_OUT, EZ_ELASTIC, EZ_BOUNCE, EZ_HOLD, EZ_BEZIER, EZ_NEASE };
static const char* kEase[EZ_NEASE] = { "Linear", "Ease in", "Ease out", "Ease in-out", "Back in (anticipate)",
                                    "Back out (overshoot)", "Elastic out", "Bounce out", "Hold (step)", "Bezier (handles)" };

// A key: a value at a time; `ease` shapes the way to the next key (bezier: handles in 0..1 of that segment).
struct Key { float t = 0, v = 0; int ease = EZ_LINEAR; float h1x = 0.33f, h1y = 0.f, h2x = 0.67f, h2y = 1.f; bool sel = false; };

enum Prop { P_X, P_Y, P_SX, P_SY, P_ROT, P_ALPHA, P_MR, P_MG, P_MB, P_AR, P_AG, P_AB, P_NPROP };
struct PropDef { const char* name; const char* unit; float def, lo, hi; ImU32 col; };
static const PropDef kProp[P_NPROP] = {
    { "Move X", "units", 0, -100, 100, IM_COL32(235, 90, 90, 255) },     { "Move Y", "units", 0, -100, 100, IM_COL32(110, 210, 90, 255) },
    { "Scale X", "x", 1, 0.8f, 1.2f, IM_COL32(240, 160, 60, 255) },      { "Scale Y", "x", 1, 0.8f, 1.2f, IM_COL32(230, 210, 70, 255) },
    { "Rotation", "deg", 0, -15, 15, IM_COL32(90, 170, 250, 255) },      { "Alpha", "0-1", 1, 0, 1, IM_COL32(220, 220, 220, 255) },
    { "Colour x red", "x", 1, 0, 1, IM_COL32(255, 110, 110, 255) },     { "Colour x green", "x", 1, 0, 1, IM_COL32(110, 255, 110, 255) },
    { "Colour x blue", "x", 1, 0, 1, IM_COL32(120, 150, 255, 255) },    { "Colour + red", "0-1", 0, 0, 0.6f, IM_COL32(255, 160, 160, 255) },
    { "Colour + green", "0-1", 0, 0, 0.6f, IM_COL32(160, 255, 160, 255) }, { "Colour + blue", "0-1", 0, 0, 0.6f, IM_COL32(170, 190, 255, 255) },
};

struct Track { int prop = P_X; std::vector<Key> keys; };
enum Loop { ONCE, LOOP, PINGPONG };
struct Clip {
    std::string name, trigger;   // trigger: a placeholder description (planned)
    float length = 500; int loop = ONCE; bool on = false;
    Track tracks[P_NPROP];
};
// planned per-element settings (placeholders for now)
struct Planned {
    int pivot = 4; bool followGroup = true;
    float delay = 0, stagger = 0, blendIn = 120; bool outBeforeHide = true, waitPrevOut = false;
    bool glow = false; float glowSize = 8, glowStrength = 1; float glowCol[4] = { 1, 1, 1, 0.8f };
    bool shadow = false; float shadowDx = 3, shadowDy = 3, shadowBlur = 4; float shadowCol[4] = { 0, 0, 0, 0.6f };
    float blur = 0; int blend = 0; int mask = 0;
    bool flipbook = false; int flipFrames = 8; float flipFps = 30;
    bool countUp = false; float countMs = 600;
    int flashMovie = 0, flashClip = 0;
};
struct ElemAnim { std::vector<Clip> clips; Planned p; };

static const char* kClips[][2] = {
    { "In", "when it shows" }, { "Idle", "while shown (after In)" }, { "Out", "before it hides" },
    { "Focus", "when its menu row gets focus" }, { "Unfocus", "when the focus leaves" }, { "Press", "when it's pressed" },
    { "Selected", "while its row is the picked one" }, { "Locked", "while it's locked" }, { "Value change", "when its live value changes" },
};

// Easing: 0..1 -> 0..1 (back / elastic go past the ends).
static float BezierY(const Key& k, float u) {
    // x(s) = u for the curve (0,0) (h1x,h1y) (h2x,h2y) (1,1): bisection on s
    float lo = 0, hi = 1, s = u;
    for (int i = 0; i < 30; ++i) {
        s = (lo + hi) * 0.5f;
        float x = 3 * (1 - s) * (1 - s) * s * k.h1x + 3 * (1 - s) * s * s * k.h2x + s * s * s;
        (x < u ? lo : hi) = s;
    }
    return 3 * (1 - s) * (1 - s) * s * k.h1y + 3 * (1 - s) * s * s * k.h2y + s * s * s;
}
static float BounceOut(float u) {
    const float n = 7.5625f, d = 2.75f;
    if (u < 1 / d) return n * u * u;
    if (u < 2 / d) { u -= 1.5f / d; return n * u * u + 0.75f; }
    if (u < 2.5f / d) { u -= 2.25f / d; return n * u * u + 0.9375f; }
    u -= 2.625f / d; return n * u * u + 0.984375f;
}
static float Eased(const Key& k, float u) {
    const float c1 = 1.70158f, c3 = c1 + 1;
    switch (k.ease) {
    case EZ_IN: return u * u * u;
    case EZ_OUT: { float r = 1 - u; return 1 - r * r * r; }
    case EZ_INOUT: return u < 0.5f ? 4 * u * u * u : 1 - powf(-2 * u + 2, 3) / 2;
    case EZ_BACK_IN: return c3 * u * u * u - c1 * u * u;
    case EZ_BACK_OUT: { float r = u - 1; return 1 + c3 * r * r * r + c1 * r * r; }
    case EZ_ELASTIC: return u <= 0 ? 0 : u >= 1 ? 1 : powf(2, -10 * u) * sinf((u * 10 - 0.75f) * 2.0943951f) + 1;
    case EZ_BOUNCE: return BounceOut(u);
    case EZ_HOLD: return u >= 1 ? 1.f : 0.f;
    case EZ_BEZIER: return BezierY(k, u);
    default: return u;
    }
}
static float Value(const Track& tr, float t) {
    const auto& k = tr.keys;
    if (k.empty()) return kProp[tr.prop].def;
    if (t <= k.front().t) return k.front().v;
    if (t >= k.back().t) return k.back().v;
    for (size_t i = 0; i + 1 < k.size(); ++i) {
        if (t > k[i + 1].t) continue;
        const float span = std::max(0.001f, k[i + 1].t - k[i].t);
        return k[i].v + (k[i + 1].v - k[i].v) * Eased(k[i], (t - k[i].t) / span);
    }
    return k.back().v;
}
static void SortKeys(Track& tr) { std::stable_sort(tr.keys.begin(), tr.keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; }); }
static void SetKey(Track& tr, float t, float v) {
    for (auto& k : tr.keys) if (fabsf(k.t - t) < 0.5f) { k.v = v; return; }
    Key k; k.t = t; k.v = v; tr.keys.push_back(k); SortKeys(tr);
}
static int Keys(const Clip& c) { int n = 0; for (auto& tr : c.tracks) n += (int)tr.keys.size(); return n; }

static ElemAnim NewAnim() {
    ElemAnim a;
    for (auto& c : kClips) { Clip k; k.name = c[0]; k.trigger = c[1]; for (int p = 0; p < P_NPROP; ++p) k.tracks[p].prop = p; a.clips.push_back(k); }
    a.clips[1].loop = PINGPONG; a.clips[1].length = 1200;
    // an example to start from: the game's focus pop (its buttons scale 1.00 -> 1.10 in 4 Flash frames (30 fps) with a
    // brightness flash, then settle at 1.02 as the flash fades; 8 and 18 frames at 60 fps)
    Clip& f = a.clips[3];
    f.on = true; f.length = 300;
    for (int p : { P_SX, P_SY }) {
        Track& tr = f.tracks[p];
        Key a0; a0.t = 0; a0.v = 1.0f; a0.ease = EZ_OUT; tr.keys.push_back(a0);
        Key a1; a1.t = 133; a1.v = 1.10f; a1.ease = EZ_INOUT; tr.keys.push_back(a1);
        Key a2; a2.t = 300; a2.v = 1.02f; tr.keys.push_back(a2);
    }
    for (int p : { P_AR, P_AG, P_AB }) {
        Track& tr = f.tracks[p];
        Key a0; a0.t = 0; a0.v = 0; tr.keys.push_back(a0);
        Key a1; a1.t = 133; a1.v = 0.5f; a1.ease = EZ_OUT; tr.keys.push_back(a1);
        Key a2; a2.t = 300; a2.v = 0.1f; tr.keys.push_back(a2);
    }
    return a;
}

// ---------------------------------------------------------------- the workspace's state

struct View { float t0 = -50, t1 = 650, v0 = 0, v1 = 1.2f; bool fit = true; };
struct State {
    bool on = false;
    std::string elemKey;          // the focused element (mode / group / name)
    int clip = 3;                 // the clip being edited (Focus: the example)
    enum { F_NONE, F_ELEMENT, F_CLIP, F_TRACK, F_PLANNED } focus = F_NONE;
    int track = -1;               // focus == F_TRACK
    std::string planned;          // focus == F_PLANNED: which placeholder
    float time = 0; bool playing = false, loop = true, frames = true, autoKey = false, expanded = false;
    View graph, big; float tl0 = -50, tl1 = 650;
    char filter[64] = "";
    int dragKey = -1, dragTrack = -1, dragHandle = 0; ImVec2 dragStart; float dragT = 0, dragV = 0;
};
static State g;
static std::map<std::string, ElemAnim> g_data;   // by element key (session only for now)

static std::string KeyOf(int mode, const Elem& e) { return std::to_string(mode) + "|" + e.group + "|" + e.name; }
static int FocusedIndex() {
    auto& els = Elems();
    for (int i = 0; i < (int)els.size(); ++i) if (KeyOf(g_editMode, els[i]) == g.elemKey) return i;
    return -1;
}
static ElemAnim* Focused() {
    if (FocusedIndex() < 0) return nullptr;
    auto it = g_data.find(g.elemKey);
    if (it == g_data.end()) it = g_data.emplace(g.elemKey, NewAnim()).first;
    return &it->second;
}
static Clip* CurClip() { ElemAnim* a = Focused(); if (!a || a->clips.empty()) return nullptr; g.clip = std::max(0, std::min(g.clip, (int)a->clips.size() - 1)); return &a->clips[g.clip]; }
static float FrameMs() { return 1000.f / 60.f; }   // the timeline's frames: 60 fps (playback is continuous)
static float Snap(float t) { return g.frames ? roundf(t / FrameMs()) * FrameMs() : roundf(t); }
static std::string TimeText(float t) {
    char b[32];
    if (g.frames) snprintf(b, sizeof b, "%d", (int)lroundf(t / FrameMs())); else snprintf(b, sizeof b, "%d ms", (int)lroundf(t));
    return b;
}
static void PlannedTag() { ImGui::SameLine(); ImGui::TextDisabled("(planned)"); }
static void FocusPlanned(const char* what) { if (ImGui::IsItemActivated() || ImGui::IsItemClicked()) { g.focus = State::F_PLANNED; g.planned = what; } }

// tracks the graph shows: the focused one, or every keyed track of the clip
static std::vector<int> GraphTracks(Clip* c) {
    std::vector<int> out;
    if (!c) return out;
    if (g.focus == State::F_TRACK && g.track >= 0) { out.push_back(g.track); return out; }
    for (int p = 0; p < P_NPROP; ++p) if (!c->tracks[p].keys.empty()) out.push_back(p);
    return out;
}

// ---------------------------------------------------------------- keys + curves graph

static void FitView(View& v, Clip* c, const std::vector<int>& tracks) {
    float lo = 1e9f, hi = -1e9f;
    for (int p : tracks) for (auto& k : c->tracks[p].keys) { lo = std::min(lo, k.v); hi = std::max(hi, k.v); }
    for (int p : tracks) { const Track& tr = c->tracks[p]; for (int i = 0; i <= 60; ++i) { float vv = Value(tr, c->length * i / 60.f); lo = std::min(lo, vv); hi = std::max(hi, vv); } }
    if (lo > hi) { lo = 0; hi = 1; }
    if (hi - lo < 0.05f) { lo -= 0.5f; hi += 0.5f; }
    const float pad = (hi - lo) * 0.15f;
    v.v0 = lo - pad; v.v1 = hi + pad;
    v.t0 = -c->length * 0.08f; v.t1 = c->length * 1.08f;
}

static float NiceStep(float range, float px, float minPx) {
    float raw = range * minPx / std::max(1.f, px), p = powf(10, floorf(log10f(std::max(1e-6f, raw))));
    for (float m : { 1.f, 2.f, 5.f, 10.f }) if (p * m >= raw) return p * m;
    return p * 10;
}

static void Graph(const char* id, ImVec2 size, View& v) {
    Clip* c = CurClip();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + size.x, a.y + size.y);
    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hov = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    dl->AddRectFilled(a, b, IM_COL32(28, 30, 36, 255));
    if (!c) { dl->AddText(ImVec2(a.x + 10, a.y + 10), IM_COL32(150, 150, 150, 255), "Pick an element on the right to animate it."); return; }
    const std::vector<int> tracks = GraphTracks(c);
    if (v.fit) { FitView(v, c, tracks); v.fit = false; }
    auto X = [&](float t) { return a.x + (t - v.t0) / (v.t1 - v.t0) * size.x; };
    auto Yv = [&](float val) { return b.y - (val - v.v0) / (v.v1 - v.v0) * size.y; };
    auto T = [&](float x) { return v.t0 + (x - a.x) / size.x * (v.t1 - v.t0); };
    auto V = [&](float y) { return v.v0 + (b.y - y) / size.y * (v.v1 - v.v0); };
    dl->PushClipRect(a, b, true);
    // grid
    {
        float st = NiceStep(v.t1 - v.t0, size.x, 70);
        if (g.frames) st = std::max(1.f, roundf(st / FrameMs())) * FrameMs();
        for (float t = floorf(v.t0 / st) * st; t <= v.t1; t += st) {
            dl->AddLine(ImVec2(X(t), a.y), ImVec2(X(t), b.y), IM_COL32(255, 255, 255, 18));
            dl->AddText(ImVec2(X(t) + 3, b.y - 16), IM_COL32(150, 150, 160, 255), TimeText(t).c_str());
        }
        float sv = NiceStep(v.v1 - v.v0, size.y, 32);
        for (float val = floorf(v.v0 / sv) * sv; val <= v.v1; val += sv) {
            dl->AddLine(ImVec2(a.x, Yv(val)), ImVec2(b.x, Yv(val)), IM_COL32(255, 255, 255, 18));
            char t[32]; snprintf(t, sizeof t, "%g", roundf(val * 1000) / 1000);
            dl->AddText(ImVec2(a.x + 3, Yv(val) - 14), IM_COL32(150, 150, 160, 255), t);
        }
        // the clip's span
        dl->AddRectFilled(ImVec2(X(0), a.y), ImVec2(X(c->length), b.y), IM_COL32(255, 255, 255, 8));
        dl->AddLine(ImVec2(X(c->length), a.y), ImVec2(X(c->length), b.y), IM_COL32(255, 255, 255, 50));
    }
    if (tracks.empty()) {
        const char* msg = g.focus == State::F_PLANNED ? nullptr : "No keys in this clip yet: set a value on the animation page (it adds a key at the playhead), or double-click here on a track's line.";
        std::string m = msg ? msg : ("\"" + g.planned + "\" is a planned feature: its settings will get curves here once it exists.");
        dl->AddText(ImVec2(a.x + 10, a.y + 10), IM_COL32(170, 170, 170, 255), m.c_str());
    }
    // curves and keys
    int hitTrack = -1, hitKey = -1, hitHandle = 0;
    const ImVec2 m = io.MousePos;
    for (int p : tracks) {
        Track& tr = c->tracks[p];
        const ImU32 col = kProp[p].col;
        // the curve: sampled every 2 pixels
        std::vector<ImVec2> pts;
        for (float x = a.x; x <= b.x; x += 2) pts.push_back(ImVec2(x, Yv(Value(tr, T(x)))));
        if (pts.size() > 1) dl->AddPolyline(pts.data(), (int)pts.size(), col, 0, tracks.size() == 1 ? 2.f : 1.5f);
        for (int i = 0; i < (int)tr.keys.size(); ++i) {
            Key& k = tr.keys[i];
            ImVec2 kp(X(k.t), Yv(k.v));
            // bezier handles of the segment to the next key
            if (k.ease == EZ_BEZIER && i + 1 < (int)tr.keys.size()) {
                const Key& n = tr.keys[i + 1];
                ImVec2 h1(X(k.t + k.h1x * (n.t - k.t)), Yv(k.v + k.h1y * (n.v - k.v)));
                ImVec2 h2(X(k.t + k.h2x * (n.t - k.t)), Yv(k.v + k.h2y * (n.v - k.v)));
                dl->AddLine(kp, h1, IM_COL32(255, 255, 255, 90)); dl->AddLine(ImVec2(X(n.t), Yv(n.v)), h2, IM_COL32(255, 255, 255, 90));
                dl->AddCircleFilled(h1, 4, IM_COL32(255, 255, 255, 200)); dl->AddCircleFilled(h2, 4, IM_COL32(255, 255, 255, 200));
                if (hov && hitKey < 0) {
                    if (fabsf(m.x - h1.x) < 6 && fabsf(m.y - h1.y) < 6) { hitTrack = p; hitKey = i; hitHandle = 1; }
                    else if (fabsf(m.x - h2.x) < 6 && fabsf(m.y - h2.y) < 6) { hitTrack = p; hitKey = i; hitHandle = 2; }
                }
            }
            const float r = 5;
            ImVec2 q[4] = { ImVec2(kp.x, kp.y - r), ImVec2(kp.x + r, kp.y), ImVec2(kp.x, kp.y + r), ImVec2(kp.x - r, kp.y) };
            dl->AddConvexPolyFilled(q, 4, k.sel ? IM_COL32(255, 200, 60, 255) : IM_COL32(235, 235, 235, 255));
            dl->AddPolyline(q, 4, IM_COL32(0, 0, 0, 200), ImDrawFlags_Closed, 1.f);
            if (hov && hitKey < 0 && fabsf(m.x - kp.x) <= 7 && fabsf(m.y - kp.y) <= 7) { hitTrack = p; hitKey = i; hitHandle = 0; }
        }
    }
    // playhead
    dl->AddLine(ImVec2(X(g.time), a.y), ImVec2(X(g.time), b.y), IM_COL32(80, 150, 255, 255), 2.f);
    // legend
    {
        float ly = a.y + 6;
        for (int p : tracks) {
            char t[64]; snprintf(t, sizeof t, "%s  %.3f", kProp[p].name, Value(c->tracks[p], g.time));
            ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddRectFilled(ImVec2(b.x - ts.x - 22, ly - 1), ImVec2(b.x - 4, ly + ts.y + 1), IM_COL32(20, 20, 24, 200));
            dl->AddRectFilled(ImVec2(b.x - ts.x - 18, ly + 4), ImVec2(b.x - ts.x - 10, ly + ts.y - 4), kProp[p].col);
            dl->AddText(ImVec2(b.x - ts.x - 6, ly), IM_COL32(220, 220, 220, 255), t);
            ly += ts.y + 2;
        }
    }
    dl->PopClipRect();
    if (!hov && g.dragKey < 0) return;
    // ---- input
    if (hov && io.MouseWheel != 0) {
        const float f = io.MouseWheel > 0 ? 0.85f : 1.f / 0.85f;
        if (io.KeyCtrl) { float mv = V(m.y); v.v0 = mv + (v.v0 - mv) * f; v.v1 = mv + (v.v1 - mv) * f; }
        else { float mt = T(m.x); v.t0 = mt + (v.t0 - mt) * f; v.t1 = mt + (v.t1 - mt) * f; }
    }
    if (hov && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left)))) {
        const float dt = -io.MouseDelta.x / size.x * (v.t1 - v.t0), dv = io.MouseDelta.y / size.y * (v.v1 - v.v0);
        v.t0 += dt; v.t1 += dt; v.v0 += dv; v.v1 += dv;
        return;
    }
    if (hov && ImGui::IsKeyPressed(ImGuiKey_F)) v.fit = true;
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hitKey >= 0) {
            Key& k = c->tracks[hitTrack].keys[hitKey];
            if (hitHandle == 0) {
                if (!io.KeyShift && !k.sel) for (auto& tr : c->tracks) for (auto& kk : tr.keys) kk.sel = false;
                k.sel = io.KeyShift ? !k.sel : true;
            }
            g.dragTrack = hitTrack; g.dragKey = hitKey; g.dragHandle = hitHandle; g.dragStart = m; g.dragT = k.t; g.dragV = k.v;
            if (g.focus != State::F_TRACK || g.track != hitTrack) { g.focus = State::F_TRACK; g.track = hitTrack; }
        } else {
            for (auto& tr : c->tracks) for (auto& kk : tr.keys) kk.sel = false;
            if (!io.KeyAlt) g.time = std::max(0.f, Snap(T(m.x)));   // click: move the playhead
        }
    }
    if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hitKey < 0) {
        const int p = !tracks.empty() ? (tracks.size() == 1 ? tracks[0] : -1) : (g.focus == State::F_TRACK ? g.track : -1);
        if (p >= 0) { SetKey(c->tracks[p], std::max(0.f, Snap(T(m.x))), V(m.y)); c->on = true; }
    }
    if (g.dragKey >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        Track& tr = c->tracks[g.dragTrack];
        if (g.dragKey < (int)tr.keys.size()) {
            Key& k = tr.keys[g.dragKey];
            if (g.dragHandle == 0) {
                const float nt = std::max(0.f, Snap(g.dragT + (m.x - g.dragStart.x) / size.x * (v.t1 - v.t0)));
                const float nv = g.dragV - (m.y - g.dragStart.y) / size.y * (v.v1 - v.v0);
                const float dt = nt - k.t, dv = (io.KeyShift ? 0.f : nv - k.v);
                for (auto& t2 : c->tracks) for (auto& kk : t2.keys) if (kk.sel) { kk.t = std::max(0.f, kk.t + dt); kk.v += dv; }
            } else if (g.dragKey + 1 < (int)tr.keys.size()) {
                const Key& n = tr.keys[g.dragKey + 1];
                const float u = (T(m.x) - k.t) / std::max(0.001f, n.t - k.t);
                const float w = fabsf(n.v - k.v) > 1e-6f ? (V(m.y) - k.v) / (n.v - k.v) : 0.f;
                if (g.dragHandle == 1) { k.h1x = std::max(0.f, std::min(1.f, u)); k.h1y = w; }
                else { k.h2x = std::max(0.f, std::min(1.f, u)); k.h2y = w; }
            }
        }
    } else if (g.dragKey >= 0) {
        for (auto& t2 : c->tracks) SortKeys(t2);
        g.dragKey = -1;
    }
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hitKey >= 0) {
        g.dragTrack = hitTrack; g.dragKey = -1;
        g.track = hitTrack; g.focus = State::F_TRACK;
        for (auto& tr : c->tracks) for (auto& kk : tr.keys) kk.sel = false;
        c->tracks[hitTrack].keys[hitKey].sel = true;
        ImGui::OpenPopup("##keymenu");
    }
    if (ImGui::BeginPopup("##keymenu")) {
        ImGui::TextDisabled("Way to the next key");
        for (int e = 0; e < EZ_NEASE; ++e)
            if (ImGui::MenuItem(kEase[e])) for (auto& tr : c->tracks) for (auto& kk : tr.keys) if (kk.sel) kk.ease = e;
        ImGui::Separator();
        if (ImGui::MenuItem("Delete key")) for (auto& tr : c->tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end());
        ImGui::EndPopup();
    }
    if (hov && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X)) && !io.WantTextInput)
        for (auto& tr : c->tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end());
    if (hov) ImGui::SetTooltip("Drag keys (Shift: time only), white dots: bezier handles. Right-click a key: its easing / delete.\nDouble-click: add a key (one track shown). Click: move the playhead. Wheel: zoom time, Ctrl+wheel: values,\nmiddle / Alt drag: pan, F: fit.");
}

// ---------------------------------------------------------------- elements list (right edge)

static void ElementsPanel(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::Begin("Elements##anim", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextDisabled("%s layout - %d elements", ModeName(g_editMode), (int)Elems().size());
    {   // which layout (a dropdown: it follows the editor's pick, tabs would pick their first one)
        std::vector<const char*> names;
        for (int mm = 0; mm < NumModes(); ++mm) names.push_back(ModeName(mm));
        int cur = g_editMode;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##amode", &cur, names.data(), (int)names.size()) && cur != g_editMode) { g_editMode = cur; g_sel = -1; }
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##afilter", "filter", g.filter, sizeof g.filter);
    ImGui::BeginChild("##alist", ImVec2(0, 0), REUI_CHILD_BORDERS);
    auto& els = Elems();
    // by group (sorted by group path, then list order), indented by depth
    std::vector<int> order;
    for (int i = 0; i < (int)els.size(); ++i) {
        if (g.filter[0] && (els[i].group + "/" + els[i].name).find(g.filter) == std::string::npos) continue;
        order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return els[x].group < els[y].group; });
    std::string lastGroup = "\x01";
    for (int i : order) {
        const Elem& e = els[i];
        if (e.group != lastGroup) {
            lastGroup = e.group;
            if (!e.group.empty()) ImGui::TextColored(ImVec4(1.f, 0.65f, 0.3f, 1), "%s", e.group.c_str());
        }
        const std::string key = KeyOf(g_editMode, e);
        auto it = g_data.find(key);
        int nk = 0; if (it != g_data.end()) for (auto& c : it->second.clips) nk += Keys(c);
        char label[256];
        snprintf(label, sizeof label, "%s[%s] %s%s##ae%d", e.group.empty() ? "" : "   ", TypeKey(e.type), e.name.c_str(), nk ? "  *" : "", i);
        if (ImGui::Selectable(label, key == g.elemKey)) {
            g.elemKey = key; g_sel = i; g.focus = State::F_ELEMENT; g.graph.fit = g.big.fit = true;
        }
        if (nk && ImGui::IsItemHovered()) ImGui::SetTooltip("%d keys", nk);
    }
    if (order.empty()) ImGui::TextDisabled("No elements in this layout.");
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------- animation page (left of the list)

static void AnimPage(ImVec2 pos, ImVec2 size) {
    const int idx = FocusedIndex();
    ElemAnim* an = Focused();
    if (idx < 0 || !an) return;
    Elem& e = Elems()[idx];
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::Begin("Animation##anim", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("%s", e.name.c_str()); ImGui::SameLine(); ImGui::TextDisabled("%s%s%s", TypeKey(e.type), e.group.empty() ? "" : "  in ", e.group.c_str());
    ImGui::TextDisabled("Keys edit for real (this session only - not saved, not played on the element yet).");
    Clip* cur = CurClip();
    // ---- states
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("States (clips)")) {
        if (ImGui::BeginTable("##clips", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Clip", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Length"); ImGui::TableSetupColumn("Plays"); ImGui::TableSetupColumn("Keys");
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)an->clips.size(); ++i) {
                Clip& c = an->clips[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Checkbox("##on", &c.on); ImGui::SameLine();
                if (ImGui::Selectable(c.name.c_str(), g.clip == i)) { g.clip = i; g.focus = State::F_CLIP; g.graph.fit = g.big.fit = true; g.tl1 = c.length * 1.08f; g.tl0 = -c.length * 0.08f; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays %s (planned: the trigger)", c.trigger.c_str());
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(70);
                if (ImGui::DragFloat("##len", &c.length, 5, 33, 20000, "%.0f ms")) { g.clip = i; }
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(90);
                const char* lp[] = { "once", "loop", "ping-pong" }; ImGui::Combo("##loop", &c.loop, lp, 3);
                ImGui::TableNextColumn(); ImGui::Text("%d", Keys(c));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::BeginDisabled(); ImGui::Button("+ Custom clip"); ImGui::EndDisabled(); PlannedTag();
        ImGui::TextDisabled("Triggers, blending between clips and playing on the element: planned.");
    }
    // ---- tracks
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (cur && ImGui::CollapsingHeader("Tracks", ImGuiTreeNodeFlags_None)) {
        ImGui::TextDisabled("%s clip, at %s. Set a value: a key at the playhead. Click a name: graph it.", cur->name.c_str(), TimeText(g.time).c_str());
        if (ImGui::BeginTable("##tracks", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Value"); ImGui::TableSetupColumn("Keys");
            for (int p = 0; p < P_NPROP; ++p) {
                Track& tr = cur->tracks[p];
                ImGui::PushID(p);
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 cp = ImGui::GetCursorScreenPos();
                const float hh = ImGui::GetTextLineHeight();
                dl->AddRectFilled(ImVec2(cp.x, cp.y + hh * 0.3f), ImVec2(cp.x + 6, cp.y + hh * 0.8f), kProp[p].col);
                ImGui::Dummy(ImVec2(9, hh)); ImGui::SameLine();
                if (ImGui::Selectable(kProp[p].name, g.focus == State::F_TRACK && g.track == p)) { g.focus = State::F_TRACK; g.track = p; g.graph.fit = g.big.fit = true; }
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(90);
                float val = Value(tr, g.time);
                if (ImGui::DragFloat("##v", &val, p == P_ROT || p <= P_Y ? 0.5f : 0.005f, 0, 0, "%.3f")) { SetKey(tr, Snap(g.time), val); cur->on = true; g.focus = State::F_TRACK; g.track = p; }
                ImGui::TableNextColumn();
                ImGui::Text("%d", (int)tr.keys.size());
                if (!tr.keys.empty()) { ImGui::SameLine(); if (ImGui::SmallButton("clear")) tr.keys.clear(); }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Rotation and the colour tracks don't draw on elements yet (planned with the engine).");
    }
    // ---- easing of the picked keys
    if (cur && ImGui::CollapsingHeader("Easing (picked keys)")) {
        int n = 0, ease = -1;
        for (auto& tr : cur->tracks) for (auto& k : tr.keys) if (k.sel) { ++n; ease = ease == -1 || ease == k.ease ? k.ease : -2; }
        if (!n) ImGui::TextDisabled("Pick keys in the graph or the timeline (Shift: several).");
        else {
            ImGui::Text("%d key%s picked", n, n == 1 ? "" : "s");
            int e2 = ease < 0 ? 0 : ease;
            ImGui::SetNextItemWidth(220);
            if (ImGui::Combo("way to the next key", &e2, kEase, EZ_NEASE)) for (auto& tr : cur->tracks) for (auto& k : tr.keys) if (k.sel) k.ease = e2;
            Key* bz = nullptr;   // the first picked bezier key's handles
            for (auto& tr : cur->tracks) for (auto& k : tr.keys) if (!bz && k.sel && k.ease == EZ_BEZIER) bz = &k;
            if (bz) {
                ImGui::SetNextItemWidth(220); ImGui::DragFloat2("handle 1 (x, y)", &bz->h1x, 0.01f);
                ImGui::SetNextItemWidth(220); ImGui::DragFloat2("handle 2 (x, y)", &bz->h2x, 0.01f);
                bz->h1x = std::max(0.f, std::min(1.f, bz->h1x)); bz->h2x = std::max(0.f, std::min(1.f, bz->h2x));
            }
        }
    }
    Planned& p = an->p;
    // ---- placeholders for what's planned
    if (ImGui::CollapsingHeader("Pivot and group (planned)")) {
        ImGui::TextDisabled("Scale / rotate about:");
        for (int i = 0; i < 9; ++i) {
            if (i % 3) ImGui::SameLine();
            ImGui::PushID(100 + i);
            if (ImGui::RadioButton("##pv", p.pivot == i)) { p.pivot = i; } FocusPlanned("Pivot");
            ImGui::PopID();
        }
        ImGui::SameLine(); ImGui::TextDisabled("(corner / edge / centre)");
        ImGui::Checkbox("Follow its group's animation", &p.followGroup); FocusPlanned("Group transform"); PlannedTag();
        ImGui::TextDisabled("Group: %s", e.group.empty() ? "(none)" : e.group.c_str());
    }
    if (ImGui::CollapsingHeader("Timing (planned)")) {
        ImGui::SetNextItemWidth(120); ImGui::DragFloat("delay", &p.delay, 5, 0, 5000, "%.0f ms"); FocusPlanned("Delay"); PlannedTag();
        ImGui::SetNextItemWidth(120); ImGui::DragFloat("stagger per child", &p.stagger, 5, 0, 2000, "%.0f ms"); FocusPlanned("Stagger"); PlannedTag();
        ImGui::SetNextItemWidth(120); ImGui::DragFloat("blend into the next clip", &p.blendIn, 5, 0, 2000, "%.0f ms"); FocusPlanned("Clip blending"); PlannedTag();
        ImGui::Checkbox("Play Out before hiding", &p.outBeforeHide); FocusPlanned("Out before hide"); PlannedTag();
        ImGui::Checkbox("Wait for the previous screen's Out", &p.waitPrevOut); FocusPlanned("Screen transitions"); PlannedTag();
    }
    if (ImGui::CollapsingHeader("Effects (planned)")) {
        ImGui::Checkbox("Glow", &p.glow); FocusPlanned("Glow"); PlannedTag();
        ImGui::SetNextItemWidth(110); ImGui::DragFloat("glow size", &p.glowSize, 0.2f, 0, 64); FocusPlanned("Glow size");
        ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::DragFloat("strength", &p.glowStrength, 0.02f, 0, 4); FocusPlanned("Glow strength");
        ImGui::SameLine(); ImGui::ColorEdit4("##glowc", p.glowCol, ImGuiColorEditFlags_NoInputs); FocusPlanned("Glow colour");
        ImGui::Checkbox("Drop shadow", &p.shadow); FocusPlanned("Drop shadow"); PlannedTag();
        ImGui::SetNextItemWidth(160); ImGui::DragFloat2("offset", &p.shadowDx, 0.2f); FocusPlanned("Shadow offset");
        ImGui::SameLine(); ImGui::SetNextItemWidth(70); ImGui::DragFloat("blur##sh", &p.shadowBlur, 0.2f, 0, 32); FocusPlanned("Shadow blur");
        ImGui::SameLine(); ImGui::ColorEdit4("##shc", p.shadowCol, ImGuiColorEditFlags_NoInputs); FocusPlanned("Shadow colour");
        ImGui::SetNextItemWidth(110); ImGui::DragFloat("blur", &p.blur, 0.2f, 0, 32); FocusPlanned("Blur"); PlannedTag();
        const char* bm[] = { "normal", "add", "screen", "multiply", "overlay", "lighten", "darken" };
        ImGui::SetNextItemWidth(110); ImGui::Combo("blend mode", &p.blend, bm, 7); FocusPlanned("Blend mode"); PlannedTag();
        const char* mk[] = { "none", "its box", "another element", "wipe left to right", "wipe top to bottom", "radial reveal" };
        ImGui::SetNextItemWidth(160); ImGui::Combo("mask", &p.mask, mk, 6); FocusPlanned("Mask"); PlannedTag();
    }
    if (ImGui::CollapsingHeader("Extras (planned)")) {
        ImGui::Checkbox("Flipbook (texture frames)", &p.flipbook); FocusPlanned("Flipbook"); PlannedTag();
        ImGui::SetNextItemWidth(90); ImGui::DragInt("frames", &p.flipFrames, 0.2f, 1, 256); FocusPlanned("Flipbook");
        ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::DragFloat("fps", &p.flipFps, 0.2f, 1, 120); FocusPlanned("Flipbook");
        ImGui::Checkbox("Count numbers up (text)", &p.countUp); FocusPlanned("Count up"); PlannedTag();
        ImGui::SetNextItemWidth(110); ImGui::DragFloat("over", &p.countMs, 5, 0, 5000, "%.0f ms"); FocusPlanned("Count up");
    }
    if (ImGui::CollapsingHeader("Copy motion from the game (planned)")) {
        const char* mv[] = { "(pick a movie)", "screens/jra_menus/hangoutmainmenu", "screens/jra_ingame/ingamepausemenu", "screens/online/pregamelobby" };
        const char* cl[] = { "(pick a clip)", "in", "idle", "out", "onRollOver", "onRollOut", "onPress" };
        ImGui::SetNextItemWidth(260); ImGui::Combo("movie", &p.flashMovie, mv, 4); FocusPlanned("Copy motion");
        ImGui::SetNextItemWidth(160); ImGui::Combo("its clip", &p.flashClip, cl, 7); FocusPlanned("Copy motion");
        ImGui::BeginDisabled(); ImGui::Button("Copy into this clip"); ImGui::EndDisabled(); PlannedTag();
        ImGui::TextDisabled("Reads the Flash clip's frames, makes keys with fitted easing.");
    }
    ImGui::End();
}

// ---------------------------------------------------------------- graph window (under both panels)

static void GraphPanel(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::Begin("Keys and curves##anim", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    Clip* c = CurClip();
    std::string what = !c ? "-" : g.focus == State::F_TRACK && g.track >= 0 ? std::string(kProp[g.track].name)
                     : g.focus == State::F_PLANNED ? g.planned + " (planned)" : c->name + " clip: all its keyed tracks";
    ImGui::Text("Showing: %s", what.c_str());
    ImGui::SameLine(); if (ImGui::SmallButton("fit")) g.graph.fit = true;
    if (c && g.focus == State::F_TRACK) { ImGui::SameLine(); if (ImGui::SmallButton("all tracks")) { g.focus = State::F_CLIP; g.graph.fit = true; } }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    Graph("##graph", ImVec2(avail.x, std::max(40.f, avail.y)), g.graph);
    ImGui::End();
}

// ---------------------------------------------------------------- timeline (bottom)

static void Timeline(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::Begin("Timeline##anim", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    Clip* c = CurClip();
    ElemAnim* an = Focused();
    const float len = c ? c->length : 1000;
    // ---- header: transport, time, clip, options
    auto keyTimes = [&]() { std::vector<float> ts; if (c) for (auto& tr : c->tracks) for (auto& k : tr.keys) ts.push_back(k.t); std::sort(ts.begin(), ts.end()); return ts; };
    if (ImGui::Button("|<")) g.time = 0;
    ImGui::SameLine(); if (ImGui::Button("<k")) { float best = 0; for (float t : keyTimes()) if (t < g.time - 0.5f) best = t; g.time = best; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous key");
    ImGui::SameLine(); if (ImGui::Button(g.playing ? " || " : "  >  ")) g.playing = !g.playing;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / pause (Space)");
    ImGui::SameLine(); if (ImGui::Button("k>")) { for (float t : keyTimes()) if (t > g.time + 0.5f) { g.time = t; break; } }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next key");
    ImGui::SameLine(); if (ImGui::Button(">|")) g.time = len;
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    if (g.frames) { int f = (int)lroundf(g.time / FrameMs()); if (ImGui::DragInt("##frame", &f, 0.2f, 0, 100000)) g.time = f * FrameMs(); }
    else ImGui::DragFloat("##ms", &g.time, 2, 0, 100000, "%.0f ms");
    ImGui::SameLine(); ImGui::TextDisabled("/ %s", TimeText(len).c_str());
    ImGui::SameLine(); ImGui::Checkbox("loop", &g.loop);
    ImGui::SameLine(); ImGui::Checkbox("frames (60 fps)", &g.frames);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show and snap key times to frames at 60 fps - off: milliseconds.\nPlayback isn't stepped: curves are read at the exact time of each drawn frame, so 120 / 144 Hz get the in-between values.\n(The game's Flash menus run at 30 fps: Flash frame N is frame 2N here.)");
    ImGui::SameLine(); ImGui::Checkbox("auto key", &g.autoKey); FocusPlanned("Auto key"); PlannedTag();
    if (an) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(130);
        std::vector<const char*> names; for (auto& k : an->clips) names.push_back(k.name.c_str());
        if (ImGui::Combo("clip", &g.clip, names.data(), (int)names.size())) { g.focus = State::F_CLIP; g.graph.fit = g.big.fit = true; }
    }
    const float bw = ImGui::CalcTextSize("Exit animation mode").x + 20;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw - ImGui::CalcTextSize(" Collapse ").x - 34));
    if (ImGui::Button(g.expanded ? " Collapse " : " Expand ")) { g.expanded = !g.expanded; g.big.fit = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A taller timeline with a big keys + curves graph.");
    ImGui::SameLine(); if (ImGui::Button("Exit animation mode")) g.on = false;
    // ---- rows: names | ruler + keys
    const float nameW = 190, rowH = ImGui::GetTextLineHeight() + 6, rulerH = ImGui::GetTextLineHeight() + 8;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    std::vector<int> rows;
    if (c) for (int p = 0; p < P_NPROP; ++p) if (!c->tracks[p].keys.empty() || (g.focus == State::F_TRACK && g.track == p)) rows.push_back(p);
    float sheetH = g.expanded ? std::min(avail.y * 0.38f, rulerH + rowH * (rows.size() + 1) + 4) : avail.y;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 ra(o.x + nameW, o.y), rb(o.x + avail.x, o.y + sheetH);
    ImGui::InvisibleButton("##sheet", ImVec2(avail.x, sheetH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hov = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    auto X = [&](float t) { return ra.x + (t - g.tl0) / (g.tl1 - g.tl0) * (rb.x - ra.x); };
    auto T = [&](float x) { return g.tl0 + (x - ra.x) / (rb.x - ra.x) * (g.tl1 - g.tl0); };
    dl->AddRectFilled(o, ImVec2(ra.x, rb.y), IM_COL32(34, 36, 42, 255));
    dl->AddRectFilled(ra, rb, IM_COL32(26, 28, 33, 255));
    dl->AddRectFilled(ImVec2(X(0), ra.y), ImVec2(X(len), rb.y), IM_COL32(255, 255, 255, 10));
    // ruler
    dl->AddRectFilled(ra, ImVec2(rb.x, ra.y + rulerH), IM_COL32(44, 46, 54, 255));
    {
        float st = NiceStep(g.tl1 - g.tl0, rb.x - ra.x, 60);
        if (g.frames) st = std::max(1.f, roundf(st / FrameMs())) * FrameMs();
        dl->PushClipRect(ra, rb, true);
        for (float t = floorf(g.tl0 / st) * st; t <= g.tl1; t += st) {
            dl->AddLine(ImVec2(X(t), ra.y + rulerH * 0.55f), ImVec2(X(t), rb.y), IM_COL32(255, 255, 255, 22));
            dl->AddText(ImVec2(X(t) + 3, ra.y + 2), IM_COL32(170, 170, 180, 255), TimeText(t).c_str());
        }
        dl->PopClipRect();
    }
    // rows
    int hitRow = -2, hitKey = -1;
    const ImVec2 m = io.MousePos;
    auto diamond = [&](ImVec2 p, float r, ImU32 col) {
        ImVec2 q[4] = { ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y) };
        dl->AddConvexPolyFilled(q, 4, col); dl->AddPolyline(q, 4, IM_COL32(0, 0, 0, 200), ImDrawFlags_Closed, 1.f);
    };
    static float rowScroll = 0;   // the rows scroll (wheel over the names) when they don't fit
    {
        const float rowsH = rowH * (rows.size() + 1), roomH = sheetH - rulerH;
        rowScroll = std::max(0.f, std::min(rowScroll, std::max(0.f, rowsH - roomH)));
    }
    float y = ra.y + rulerH - rowScroll;
    dl->PushClipRect(ImVec2(o.x, ra.y + rulerH), rb, true);
    if (!c) dl->AddText(ImVec2(o.x + 8, y + 6), IM_COL32(150, 150, 150, 255), "Pick an element on the right.");
    else {
        // summary: every key of the clip
        dl->AddRectFilled(ImVec2(o.x, y), ImVec2(rb.x, y + rowH), IM_COL32(255, 255, 255, 12));
        dl->AddText(ImVec2(o.x + 8, y + 3), IM_COL32(230, 230, 230, 255), (c->name + " (all)").c_str());
        for (auto& tr : c->tracks) for (auto& k : tr.keys) diamond(ImVec2(X(k.t), y + rowH * 0.5f), 4, IM_COL32(200, 200, 200, 255));
        y += rowH;
        for (int ri = 0; ri < (int)rows.size(); ++ri) {
            const int p = rows[ri];
            Track& tr = c->tracks[p];
            const bool foc = g.focus == State::F_TRACK && g.track == p;
            if (foc) dl->AddRectFilled(ImVec2(o.x, y), ImVec2(rb.x, y + rowH), IM_COL32(80, 150, 255, 30));
            dl->AddRectFilled(ImVec2(o.x + 6, y + 5), ImVec2(o.x + 10, y + rowH - 5), kProp[p].col);
            dl->AddText(ImVec2(o.x + 16, y + 3), IM_COL32(220, 220, 220, 255), kProp[p].name);
            for (int i = 0; i < (int)tr.keys.size(); ++i) {
                Key& k = tr.keys[i];
                ImVec2 kp(X(k.t), y + rowH * 0.5f);
                // the way to the next key: a line, dashed for hold
                if (i + 1 < (int)tr.keys.size()) dl->AddLine(kp, ImVec2(X(tr.keys[i + 1].t), kp.y), k.ease == EZ_HOLD ? IM_COL32(255, 255, 255, 40) : (kProp[p].col & 0x00FFFFFF) | 0x70000000, 2.f);
                diamond(kp, 5, k.sel ? IM_COL32(255, 200, 60, 255) : IM_COL32(235, 235, 235, 255));
                if (hov && m.y >= ra.y + rulerH && fabsf(m.x - kp.x) <= 6 && fabsf(m.y - kp.y) <= rowH * 0.5f) { hitRow = p; hitKey = i; }
            }
            if (hov && hitRow == -2 && m.y >= y && m.y < y + rowH) hitRow = -1 - 100 - p;   // a row, no key
            y += rowH;
        }
    }
    dl->PopClipRect();
    dl->PushClipRect(o, rb, true);
    // playhead
    dl->AddLine(ImVec2(X(g.time), ra.y), ImVec2(X(g.time), rb.y), IM_COL32(80, 150, 255, 255), 2.f);
    {
        const std::string t = TimeText(g.time); ImVec2 ts = ImGui::CalcTextSize(t.c_str());
        dl->AddRectFilled(ImVec2(X(g.time) - ts.x * 0.5f - 4, ra.y), ImVec2(X(g.time) + ts.x * 0.5f + 4, ra.y + rulerH), IM_COL32(80, 150, 255, 255), 3.f);
        dl->AddText(ImVec2(X(g.time) - ts.x * 0.5f, ra.y + 3), IM_COL32(255, 255, 255, 255), t.c_str());
    }
    dl->PopClipRect();
    // input: scrub on the ruler (or empty space), drag keys, wheel zoom, middle pan
    static bool scrubbing = false; static int dRow = -1, dKey = -1; static float dStartX = 0; static std::vector<float> dOrig;
    if (hov && io.MouseWheel != 0 && m.x < ra.x) rowScroll -= io.MouseWheel * rowH * 2;   // over the names: scroll the rows
    else if (hov && io.MouseWheel != 0) { const float f = io.MouseWheel > 0 ? 0.85f : 1.f / 0.85f, mt = T(m.x); g.tl0 = mt + (g.tl0 - mt) * f; g.tl1 = mt + (g.tl1 - mt) * f; }
    if (hov && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) { const float dt = -io.MouseDelta.x / (rb.x - ra.x) * (g.tl1 - g.tl0); g.tl0 += dt; g.tl1 += dt; }
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m.x >= ra.x) {
        if (c && hitKey >= 0) {
            Key& k = c->tracks[hitRow].keys[hitKey];
            if (!io.KeyShift && !k.sel) for (auto& tr : c->tracks) for (auto& kk : tr.keys) kk.sel = false;
            k.sel = io.KeyShift ? !k.sel : true;
            g.focus = State::F_TRACK; g.track = hitRow;
            dRow = hitRow; dKey = hitKey; dStartX = m.x; dOrig.clear();
            for (auto& tr : c->tracks) for (auto& kk : tr.keys) dOrig.push_back(kk.t);
        } else {
            scrubbing = true;
            if (c) for (auto& tr : c->tracks) for (auto& kk : tr.keys) kk.sel = false;
        }
    }
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m.x < ra.x && c) {   // a row's name: focus that track
        float yy = ra.y + rulerH + rowH - rowScroll;
        for (int p : rows) { if (m.y >= yy && m.y < yy + rowH && m.y >= ra.y + rulerH) { g.focus = State::F_TRACK; g.track = p; g.graph.fit = g.big.fit = true; } yy += rowH; }
        if (m.y >= ra.y + rulerH - rowScroll && m.y < ra.y + rulerH + rowH - rowScroll && m.y >= ra.y + rulerH) { g.focus = State::F_CLIP; g.graph.fit = g.big.fit = true; }
    }
    if (scrubbing) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) { g.time = std::max(0.f, Snap(T(m.x))); g.playing = false; }
        else scrubbing = false;
    }
    if (dKey >= 0 && c) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float dt = (m.x - dStartX) / (rb.x - ra.x) * (g.tl1 - g.tl0);
            size_t n = 0;
            for (auto& tr : c->tracks) for (auto& kk : tr.keys) { if (n < dOrig.size() && kk.sel) kk.t = std::max(0.f, Snap(dOrig[n] + dt)); ++n; }
        } else { for (auto& tr : c->tracks) SortKeys(tr); dKey = -1; }
    }
    if (hov && c && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X)) && !io.WantTextInput)
        for (auto& tr : c->tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end());
    if (hov && m.x >= ra.x) ImGui::SetTooltip("Click / drag: move the playhead. Drag keys (Shift: pick several), Delete removes. Wheel: zoom, middle drag: pan.");
    if (hov && m.x < ra.x) ImGui::SetTooltip("Click a track: graph it (the top row: the whole clip). Wheel: scroll the rows.");
    // ---- expanded: the big graph under the rows
    if (g.expanded) {
        ImGui::Spacing();
        ImGui::Text("Keys and curves: %s", !c ? "-" : g.focus == State::F_TRACK && g.track >= 0 ? kProp[g.track].name : (c->name + " clip").c_str());
        ImGui::SameLine(); if (ImGui::SmallButton("fit##big")) g.big.fit = true;
        ImVec2 av = ImGui::GetContentRegionAvail();
        Graph("##biggraph", ImVec2(av.x, std::max(60.f, av.y)), g.big);
    }
    ImGui::End();
}

// ---------------------------------------------------------------- the workspace

static void Frame() {
    ImGuiIO& io = ImGui::GetIO();
    // playback
    if (g.playing) {
        Clip* c = CurClip();
        const float len = c ? c->length : 1000;
        g.time += io.DeltaTime * 1000.f;
        if (g.time > len) { if (g.loop) g.time = fmodf(g.time, std::max(1.f, len)); else { g.time = len; g.playing = false; } }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) g.playing = !g.playing;
    if (g.focus == State::F_NONE && FocusedIndex() < 0 && g_sel >= 0 && g_sel < (int)Elems().size()) {   // start on the editor's pick
        g.elemKey = KeyOf(g_editMode, Elems()[g_sel]); g.focus = State::F_ELEMENT;
    }
    const float W = io.DisplaySize.x, H = io.DisplaySize.y, u = ImGui::GetFontSize() / 13.f;
    const float tlH = g.expanded ? H * 0.5f : std::min(H * 0.3f, 170 * u);
    const float graphH = g.expanded ? 0.f : std::min(H * 0.32f, 280 * u);
    const float listW = std::min(W * 0.22f, 300 * u), pageW = std::min(W * 0.28f, 400 * u);
    const float topH = H - tlH - graphH;
    ElementsPanel(ImVec2(W - listW, 0), ImVec2(listW, topH));
    if (FocusedIndex() >= 0) AnimPage(ImVec2(W - listW - pageW, 0), ImVec2(pageW, topH));
    if (graphH > 0) GraphPanel(ImVec2(W - listW - pageW, topH), ImVec2(listW + pageW, graphH));
    Timeline(ImVec2(0, H - tlH), ImVec2(W, tlH));
}

} // namespace anim

bool AnimModeOn() { return anim::g.on; }
void SetAnimMode(bool on) {
    anim::g.on = on;
    if (!on) return;
    anim::g.graph.fit = anim::g.big.fit = true;
    if (Elems().empty() && g_nowMode >= 0 && g_nowMode < NumModes() && !g_layout[g_nowMode].empty()) { g_editMode = g_nowMode; g_sel = -1; }
}
void AnimFocus(int index) {
    if (index < 0 || index >= (int)Elems().size()) return;
    anim::g.elemKey = anim::KeyOf(g_editMode, Elems()[index]); g_sel = index; anim::g.focus = anim::State::F_ELEMENT;
}
