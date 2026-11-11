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
// Each element owns its clips (Elem::an): tracks of eased keys, times in ms, saved with the layout as "aclip=" /
// "akey=" lines. Played: In from when the element shows, then Idle (looping); the other clips' triggers are planned.
// In animation mode every element shows the clip being edited (by name) at the playhead, so a whole screen's In can
// be scrubbed together. Everything else on the animation page is a placeholder for a planned feature, marked
// "planned".

namespace anim {

// (the soft ones: quadratic - Flash's own tween easing, which the game's menus use)
enum Ease { EZ_LINEAR, EZ_IN, EZ_OUT, EZ_INOUT, EZ_BACK_IN, EZ_BACK_OUT, EZ_ELASTIC, EZ_BOUNCE, EZ_HOLD, EZ_BEZIER, EZ_QIN, EZ_QOUT, EZ_QINOUT, EZ_NEASE };
static const char* kEase[EZ_NEASE] = { "Linear", "Ease in", "Ease out", "Ease in-out", "Back in (anticipate)",
                                    "Back out (overshoot)", "Elastic out", "Bounce out", "Hold (step)", "Bezier (handles)", "Soft in", "Soft out", "Soft in-out" };

// A key: a value at a time; `ease` shapes the way to the next key. Bezier: two handles - h1 leaves this key, h2 comes
// into the next; their x is a fraction (0..1) of the way in time, their y an offset in value from their own key (h1 from
// this one, h2 from the next), so a flat stretch can curve too and the handles move with their keys. (0, 0): flat
// tangents, a smooth ease in-out.
struct Key { float t = 0, v = 0; int ease = EZ_LINEAR; float h1x = 0.33f, h1y = 0.f, h2x = 0.67f, h2y = 0.f; bool sel = false; };

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
// (live, not saved) what's playing over In / Idle: the state layer (Focus, Unfocus, Selected, Press) and Out
struct Run {
    bool inited = false;          // (set up as it comes up: its resting state for its focus)
    int state = -1; double stateAt = 0;   // the state layer's clip (-1: none) and when it started (s)
    bool focus = false, press = false;    // its trigger values last frame
    double outAt = -1;            // its Out started (s): it's leaving
    bool frozen = false;          // its group's motion is leaving: kept as it is (no triggers) till that ends
    float from[P_NPROP] = {}; double blendAt = -1;   // a clip change: the pose it blends from, and since when
    float last[P_NPROP] = {}; bool hasLast = false;  // the pose drawn last frame
};
struct ElemAnim {
    std::vector<Clip> clips; Planned p;
    std::string focusIf, pressIf;  // triggers: Focus / Unfocus / Selected follow focusIf ("menu.sel:2"), Press plays when pressIf comes on
    float blend = 120;            // ms a change of clip eases over (from the pose it had)
    Run run;
};

static const char* kClips[][2] = {
    { "In", "when it shows" }, { "Idle", "while shown (after In)" },
    { "Out", "before it hides (it stays drawn till Out ends; on top of how it is: scale / alpha multiply, moves add)" },
    { "Focus", "when its focus value comes on" }, { "Unfocus", "when its focus value goes off (its last frame: how it rests unfocused)" },
    { "Press", "when its press value comes on" }, { "Selected", "while focused, after Focus (loop it)" },
    { "Locked", "while it's locked (planned)" }, { "Value change", "when its live value changes (planned)" },
};

// Easing: 0..1 -> 0..1 (back / elastic go past the ends).
static float BezierS(const Key& k, float u) {
    // the curve's parameter where its time is u: x(s) = u for (0) (h1x) (h2x) (1), bisection on s
    float lo = 0, hi = 1, s = u;
    for (int i = 0; i < 30; ++i) {
        s = (lo + hi) * 0.5f;
        float x = 3 * (1 - s) * (1 - s) * s * k.h1x + 3 * (1 - s) * s * s * k.h2x + s * s * s;
        (x < u ? lo : hi) = s;
    }
    return s;
}
static float BezierV(const Key& k, const Key& n, float u) {   // the value on the way from k to n (u: 0..1 of the time)
    const float s = BezierS(k, u), r = 1 - s;
    return r * r * r * k.v + 3 * r * r * s * (k.v + k.h1y) + 3 * r * s * s * (n.v + k.h2y) + s * s * s * n.v;
}
// a key turned bezier: handles that keep (about) the shape it had - the eases' standard cubic forms
static void ToBezier(Key& k, const Key* n) {
    float x1 = 0.33f, y1 = 0, x2 = 0.67f, y2 = 1;   // (normalised; linear and the rest: a smooth ease in-out)
    switch (k.ease) {
    case EZ_IN: x1 = 0.32f; y1 = 0; x2 = 0.67f; y2 = 0; break;
    case EZ_OUT: x1 = 0.33f; y1 = 1; x2 = 0.68f; y2 = 1; break;
    case EZ_INOUT: x1 = 0.65f; y1 = 0; x2 = 0.35f; y2 = 1; break;
    case EZ_QIN: x1 = 1 / 3.f; y1 = 0; x2 = 2 / 3.f; y2 = 1 / 3.f; break;
    case EZ_QOUT: x1 = 1 / 3.f; y1 = 2 / 3.f; x2 = 2 / 3.f; y2 = 1; break;
    case EZ_QINOUT: x1 = 0.45f; y1 = 0; x2 = 0.55f; y2 = 1; break;
    case EZ_BACK_IN: x1 = 0.36f; y1 = 0; x2 = 0.66f; y2 = -0.56f; break;
    case EZ_BACK_OUT: x1 = 0.34f; y1 = 1.56f; x2 = 0.64f; y2 = 1; break;
    case EZ_BEZIER: return;
    default: break;
    }
    const float dv = n ? n->v - k.v : 0.f;
    k.h1x = x1; k.h1y = y1 * dv; k.h2x = x2; k.h2y = (y2 - 1) * dv;
    k.ease = EZ_BEZIER;
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
    case EZ_QIN: return u * u;
    case EZ_QOUT: return 1 - (1 - u) * (1 - u);
    case EZ_QINOUT: return u < 0.5f ? 2 * u * u : 1 - 2 * (1 - u) * (1 - u);
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
        if (k[i].ease == EZ_BEZIER) return BezierV(k[i], k[i + 1], (t - k[i].t) / span);
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
    a.clips[1].loop = LOOP; a.clips[1].length = 1200;
    return a;
}
// an example to start from: the game's focus pop (its buttons scale 1.00 -> 1.10 in 4 Flash frames (30 fps) with a
// brightness flash, then settle at 1.02 as the flash fades; 8 and 18 frames at 60 fps)
static void ExampleFocusPop(Clip& f) {
    for (auto& tr : f.tracks) tr.keys.clear();
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
}

// ---------------------------------------------------------------- the workspace's state

struct View { float t0 = -50, t1 = 650, v0 = 0, v1 = 1.2f; bool fit = true; };
struct State {
    bool on = false;
    std::string elemKey;          // the focused element (mode / group / name)
    int clip = 0;                 // the clip being edited (In: what a screen shows first)
    enum { F_NONE, F_ELEMENT, F_CLIP, F_TRACK, F_PLANNED } focus = F_NONE;
    int track = -1;               // focus == F_TRACK
    std::string planned;          // focus == F_PLANNED: which placeholder
    float time = 0; bool playing = false, loop = true, frames = true, autoKey = false, expanded = false;
    bool bare = false;            // the side panels and graph hidden (H): the whole screen to watch
    View graph, big; float tl0 = -50, tl1 = 650;
    char filter[64] = "";
    int dragKey = -1, dragTrack = -1, dragHandle = 0; ImVec2 dragStart; float dragT = 0, dragV = 0;
    // live: the clips play as in the game (In, triggers, Out, blends) with stand-in menu values instead of the playhead
    bool live = false, simShown = true;
    int simRow = 0;                    // the focused menu row (-1: none)
    double simPressAt = -1e9, simBackAt = -1e9;
};
static State g;

static std::string KeyOf(int mode, const Elem& e) { return std::to_string(mode) + "|" + e.group + "|" + e.name; }
static int FocusedIndex() {
    auto& els = Elems();
    for (int i = 0; i < (int)els.size(); ++i) if (KeyOf(g_editMode, els[i]) == g.elemKey) return i;
    return -1;
}
static ElemAnim* Focused() {   // (made when first focused: every standard clip, empty)
    const int i = FocusedIndex();
    if (i < 0) return nullptr;
    Elem& e = Elems()[i];
    if (!e.an.p) e.an.p = new ElemAnim(NewAnim());
    return e.an.p;
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

// ---------------------------------------------------------------- playing (the engine)

static const Clip* ClipNamed(const ElemAnim* a, const std::string& name) {
    if (a) for (auto& c : a->clips) if (c.on && c.name == name && Keys(c)) return &c;
    return nullptr;
}
static float ClipTime(const Clip& c, float t) {   // a time from the clip's start -> its own time (once: holds its end)
    const float len = std::max(1.f, c.length);
    if (t < 0) t = 0;
    if (c.loop == LOOP) return fmodf(t, len);
    if (c.loop == PINGPONG) { float u = fmodf(t, 2 * len); return u > len ? 2 * len - u : u; }
    return std::min(t, len);
}
static void Sample(const Clip& c, float t, Pose& p) {
    t = ClipTime(c, t);
    auto v = [&](int prop) { return Value(c.tracks[prop], t); };
    p.dx = v(P_X); p.dy = v(P_Y); p.sx = v(P_SX); p.sy = v(P_SY); p.rot = v(P_ROT); p.a = std::max(0.f, v(P_ALPHA));
    p.mul[0] = v(P_MR); p.mul[1] = v(P_MG); p.mul[2] = v(P_MB);
    p.add[0] = std::max(0.f, v(P_AR)); p.add[1] = std::max(0.f, v(P_AG)); p.add[2] = std::max(0.f, v(P_AB));
}
// the clip animation mode shows on every element: the one picked on the timeline (by name)
static std::string PreviewName() { const Clip* c = CurClip(); return c ? c->name : std::string(kClips[std::max(0, std::min(8, g.clip))][0]); }
// ---- delay and stagger: how long an element's In waits - its own delay, and its place in the staggered groups over it
// (a group's motion element: astagger ms a step, its members top to bottom; a group inside counts as one member)
static float StaggerStep(const Elem& m, const Elem& e) {   // e's place among m's group's members (0: first)
    struct Units { int frame = -1; std::vector<std::pair<std::string, float>> order; };   // (member key, its top)
    static std::unordered_map<const Elem*, Units> cache;
    Units& u = cache[&m];
    auto keyOf = [&](const Elem& o) {   // an element of the group itself, or the group inside it that holds it
        if (o.group == m.group) return "#" + std::to_string((size_t)&o);
        const std::string rest = o.group.substr(m.group.size() + 1);
        return rest.substr(0, rest.find('/'));
    };
    if (u.frame != ImGui::GetFrameCount()) {
        u.frame = ImGui::GetFrameCount(); u.order.clear();
        std::vector<Elem>* l = LayoutOf(m);
        if (l) for (auto& o : *l) {
            if (o.type == E_MOTION || !InGroup(o.group, m.group)) continue;
            ImVec2 a, b; ElemRect(o, a, b);
            const std::string k = keyOf(o);
            auto it = std::find_if(u.order.begin(), u.order.end(), [&](const std::pair<std::string, float>& x) { return x.first == k; });
            if (it == u.order.end()) u.order.push_back({ k, std::min(a.y, b.y) }); else it->second = std::min(it->second, std::min(a.y, b.y));
        }
        std::stable_sort(u.order.begin(), u.order.end(), [](const std::pair<std::string, float>& x, const std::pair<std::string, float>& y) { return x.second < y.second - 0.5f; });
    }
    const std::string k = keyOf(e);
    for (size_t i = 0; i < u.order.size(); ++i) if (u.order[i].first == k) return (float)i;
    return 0;
}
static float InDelay(const Elem& e) {
    float d = e.an.p ? e.an.p->p.delay : 0.f;
    if (e.type == E_MOTION && e.group.empty()) return d;
    static std::vector<Elem*> cs; MotionsOf(e, cs);
    std::vector<Elem*> ms(cs);
    for (Elem* m : ms) if (m->an.p && m->an.p->p.stagger > 0) d += StaggerStep(*m, e) * m->an.p->p.stagger;   // (MotionsOf: not its own)
    return d;
}
static float PreviewLen() {   // the longest clip of that name in the layout (the timeline's range; In: with its wait)
    const std::string n = PreviewName();
    float len = 0;
    for (auto& e : Elems()) if (const Clip* c = ClipNamed(e.an.p, n)) len = std::max(len, c->length + (n == "In" ? InDelay(e) : 0.f));
    if (len <= 0) { const int i = FocusedIndex(); if (i >= 0 && Elems()[i].an.p) for (auto& c : Elems()[i].an.p->clips) if (c.name == n) len = c.length; }
    return len > 0 ? len : 1000.f;
}
static bool Previewing() { return g.on && !g.live; }
// ---- live (animation mode): stand-in menu values (menu.sel / unsel / row / on / press / back) and the layout shown or left
static bool SimToken(const std::string& key, std::string& out) {
    if (!g.on || !g.live || key.rfind("menu.", 0) != 0) return false;
    const double now = ImGui::GetTime();
    const bool pressed = now - g.simPressAt < 0.2;
    if (key == "menu.on") out = g.simShown ? "1" : "0";
    else if (key == "menu.row") out = g.simRow >= 0 ? std::to_string(g.simRow) : "";
    else if (key.rfind("menu.sel:", 0) == 0) out = g.simShown && g.simRow >= 0 && atoi(key.c_str() + 9) == g.simRow ? "1" : "0";
    else if (key.rfind("menu.unsel:", 0) == 0) out = g.simShown && atoi(key.c_str() + 11) != g.simRow ? "1" : "0";
    else if (key == "menu.press") out = pressed ? "1" : "0";
    else if (key.rfind("menu.press:", 0) == 0) out = pressed && atoi(key.c_str() + 11) == g.simRow ? "1" : "0";
    else if (key == "menu.back") out = now - g.simBackAt < 0.2 ? "1" : "0";
    else return false;
    return true;
}
static bool ShownNow(const Elem& e) {   // (live: everything shows, but for menu values, till Leave)
    if (!g.on || !g.live) return ShowIfOk(e);
    if (!g.simShown) return false;
    const std::string& c = e.showIf;
    const bool menu = c.rfind("menu.", 0) == 0 || c.rfind("!menu.", 0) == 0;
    return !menu || ShowIfOk(e);
}
static void SimRestart() {   // every element of the layout up again from the start: In plays
    for (auto& e : Elems()) { if (e.an.p) e.an.p->run = Run(); e.shownSince = -1; }
    g.simShown = true;
}

// ---- live: In / Idle underneath, the state layer's keyed tracks over them, Out's over everything; a clip change blends
static double g_speed = 1;   // live clips' speed (AnimSpeed: slow motion to check them; 1 normally)
static float MsSince(double since, double now) { return (float)((now - since) * 1000.0 * g_speed); }
static int ClipIdx(const ElemAnim& a, const char* name) {
    for (int i = 0; i < (int)a.clips.size(); ++i) if (a.clips[i].on && a.clips[i].name == name && Keys(a.clips[i])) return i;
    return -1;
}
static void SampleVals(const Clip& c, float t, float v[P_NPROP]) { t = ClipTime(c, t); for (int p = 0; p < P_NPROP; ++p) v[p] = Value(c.tracks[p], t); }
static void ToPose(const float* v, Pose& p) {
    p.dx = v[P_X]; p.dy = v[P_Y]; p.sx = v[P_SX]; p.sy = v[P_SY]; p.rot = v[P_ROT]; p.a = std::max(0.f, v[P_ALPHA]);
    p.mul[0] = v[P_MR]; p.mul[1] = v[P_MG]; p.mul[2] = v[P_MB];
    p.add[0] = std::max(0.f, v[P_AR]); p.add[1] = std::max(0.f, v[P_AG]); p.add[2] = std::max(0.f, v[P_AB]);
}
static void StartBlend(ElemAnim& a, double now) {   // the next frames ease from the pose it has now
    Run& r = a.run;
    if (r.hasLast && a.blend > 0) { for (int p = 0; p < P_NPROP; ++p) r.from[p] = r.last[p]; r.blendAt = now; }
}
static void Switch(ElemAnim& a, int clip, double now, bool blend = true) {   // the state layer to `clip` (from its start)
    if (blend) StartBlend(a, now);
    a.run.state = clip; a.run.stateAt = now;
}
static void Rest(ElemAnim& a, int clip, double now, bool blend = true) {     // ... held at its end (a once clip's last frame)
    Switch(a, clip, now, blend);
    if (clip >= 0) a.run.stateAt = now - (a.clips[clip].length / 1000.0 + 1) / g_speed;
}
static void Triggers(const Elem& e, ElemAnim& a, double now) {
    Run& r = a.run;
    const int iFocus = ClipIdx(a, "Focus"), iUnf = ClipIdx(a, "Unfocus"), iSel = ClipIdx(a, "Selected"), iPress = ClipIdx(a, "Press");
    const bool f = !a.focusIf.empty() && CondOk(a.focusIf, e.group), pr = !a.pressIf.empty() && CondOk(a.pressIf, e.group);
    auto focusedRest = [&](bool blend) { if (iSel >= 0) Switch(a, iSel, now, blend); else Rest(a, iFocus, now, blend); };
    if (!r.inited) {   // coming up: rests as its focus is (no Focus / Unfocus played, no blend)
        r.inited = true; r.focus = f; r.press = pr; r.blendAt = -1;
        if (f) { if (iFocus >= 0) Switch(a, iFocus, now, false); else focusedRest(false); }
        else Rest(a, iUnf, now, false);
        return;
    }
    if (f != r.focus) {
        r.focus = f;
        if (f) { if (iFocus >= 0) Switch(a, iFocus, now); else focusedRest(true); }
        else Switch(a, iUnf, now);
    }
    if (pr && !r.press && iPress >= 0) Switch(a, iPress, now);
    r.press = pr;
    if (r.state >= 0 && r.state < (int)a.clips.size()) {   // a once clip that's done moves on
        const Clip& c = a.clips[r.state];
        if (c.loop == ONCE && MsSince(r.stateAt, now) >= c.length) {
            if (r.state == iFocus && iSel >= 0 && r.focus) Switch(a, iSel, now, false);   // (Selected carries on from Focus)
            else if (r.state == iPress) { if (r.focus) focusedRest(true); else Rest(a, iUnf, now); }
        }
    }
}
static void Hidden(ElemAnim& a) { a.run = Run(); }
static bool Leaving(Elem& e, bool kept) {   // kept: a group motion over it is leaving (it stays till that's done)
    if (e.shownSince < 0) { if (e.an.p) Hidden(*e.an.p); return false; }   // (it wasn't up, or its Out is done: stays gone)
    ElemAnim* a = e.an.p;
    if (!a) return kept;
    const Clip* out = ClipNamed(a, "Out");
    Run& r = a->run;
    // gone: also not "shown" any more, at once - a group motion is asked from its members' draws too, and one still
    // counted as shown would start its Out over for the next member
    if (!out) {   // no Out of its own: gone now, or (kept) as it is, frozen
        if (kept) { r.frozen = true; return true; }
        Hidden(*a); e.shownSince = -1; return false;
    }
    const double now = ImGui::GetTime();
    if (r.outAt < 0) { r.outAt = now; StartBlend(*a, now); }
    if (MsSince(r.outAt, now) >= out->length && !kept) { Hidden(*a); e.shownSince = -1; return false; }   // (kept: holds Out's end)
    return true;
}
static void Showing(Elem& e) {
    ElemAnim* a = e.an.p;
    if (!a || (a->run.outAt < 0 && !a->run.frozen)) return;
    const double now = ImGui::GetTime();   // back during an Out: In again, from the pose it had
    a->run.outAt = -1; a->run.frozen = false;
    StartBlend(*a, now);
    e.shownSince = now;
}
static bool PoseOf(Elem& e, bool editing, Pose& p) {
    ElemAnim* a = e.an.p;
    if (!a || editing) return false;
    if (g.on && !g.live) {   // animation mode: the clip being edited, at the playhead (In: after its wait)
        const Clip* c = ClipNamed(a, PreviewName());
        if (!c) return false;
        Sample(*c, g.time - (c->name == "In" ? InDelay(e) : 0.f), p); return true;
    }
    Run& r = a->run;
    const double now = ImGui::GetTime();
    const bool leaving = r.outAt >= 0;
    if (!leaving && !r.frozen) Triggers(e, *a, now);   // (frozen while it leaves)
    float v[P_NPROP], s[P_NPROP];
    for (int i = 0; i < P_NPROP; ++i) v[i] = kProp[i].def;
    bool any = false;
    const float t = MsSince(e.shownSince >= 0 ? e.shownSince : now, now) - InDelay(e);   // (In waits its delay / stagger)
    const Clip* in = ClipNamed(a, "In"), *idle = ClipNamed(a, "Idle");
    if (in && (!idle || t < in->length)) { SampleVals(*in, t, v); any = true; }
    else if (idle) { SampleVals(*idle, t - (in ? in->length : 0.f), v); any = true; }
    // a layer: its keyed tracks over what's below - in place of it, or (relative: Out, leaving from wherever it is) on top
    // of it: scale, alpha and colour x multiply, move, rotation and colour + add
    auto over = [&](const Clip& c, double since, bool relative) {
        SampleVals(c, MsSince(since, now), s);
        for (int i = 0; i < P_NPROP; ++i) {
            if (c.tracks[i].keys.empty()) continue;
            const bool mul = i == P_SX || i == P_SY || i == P_ALPHA || i == P_MR || i == P_MG || i == P_MB;
            v[i] = !relative ? s[i] : mul ? v[i] * s[i] : v[i] + s[i];
        }
        any = true;
    };
    if (r.state >= 0 && r.state < (int)a->clips.size()) over(a->clips[r.state], r.stateAt, false);
    if (leaving) if (const Clip* o = ClipNamed(a, "Out")) over(*o, r.outAt, true);
    if (r.blendAt >= 0) {
        float u = MsSince(r.blendAt, now) / std::max(1.f, a->blend);
        if (u >= 1) r.blendAt = -1;
        else { u = u * u * (3 - 2 * u); for (int i = 0; i < P_NPROP; ++i) v[i] = r.from[i] + (v[i] - r.from[i]) * u; any = true; }
    }
    for (int i = 0; i < P_NPROP; ++i) r.last[i] = v[i];
    r.hasLast = true;
    if (!any) return false;
    ToPose(v, p);
    return true;
}
static std::string StateNow(const ElemAnim& a) {   // (live: what's playing on it now)
    const Run& r = a.run;
    if (r.outAt >= 0) return "Out";
    if (r.frozen) return "kept (its group is leaving)";
    if (!r.inited) return "-";
    std::string s = r.state >= 0 && r.state < (int)a.clips.size() ? a.clips[r.state].name : "In / Idle";
    if (!a.focusIf.empty()) s += r.focus ? " (focused)" : " (not focused)";
    return s;
}
// ---------------------------------------------------------------- saving: "aclip=<clip>;<ms>;<once|loop|pingpong>"
// and "akey=<clip>;<track>;<ms> <value> <ease>[ <h1x> <h1y> <h2x> <h2y>];..." (a line per keyed track)

static const char* kPropKey[P_NPROP] = { "x", "y", "sx", "sy", "rot", "alpha", "mr", "mg", "mb", "ar", "ag", "ab" };
static const char* kEaseKey[EZ_NEASE] = { "linear", "in", "out", "inout", "backin", "backout", "elastic", "bounce", "hold", "bezier", "qin", "qout", "qinout" };
static const char* kLoopKey[3] = { "once", "loop", "pingpong" };
static void PutAnim(std::string& o, const ElemAnim& a) {
    if (!a.focusIf.empty()) Pf(o, "afocus=%s\n", a.focusIf.c_str());
    if (!a.pressIf.empty()) Pf(o, "apress=%s\n", a.pressIf.c_str());
    if (a.blend != 120) Pf(o, "ablend=%g\n", a.blend);
    if (a.p.delay > 0) Pf(o, "adelay=%g\n", a.p.delay);
    if (a.p.stagger > 0) Pf(o, "astagger=%g\n", a.p.stagger);
    for (auto& c : a.clips) {
        if (!Keys(c)) continue;
        Pf(o, "aclip=%s;%g;%s%s\n", c.name.c_str(), c.length, kLoopKey[std::max(0, std::min(2, c.loop))], c.on ? "" : ";off");
        for (auto& tr : c.tracks) {
            if (tr.keys.empty()) continue;
            Pf(o, "akey=%s;%s", c.name.c_str(), kPropKey[tr.prop]);
            for (auto& k : tr.keys) {
                Pf(o, ";%g %g %s", k.t, k.v, kEaseKey[std::max(0, std::min((int)EZ_NEASE - 1, k.ease))]);
                if (k.ease == EZ_BEZIER) Pf(o, " %g %g %g %g", k.h1x, k.h1y, k.h2x, k.h2y);
            }
            Pf(o, "\n");
        }
    }
}
static std::vector<std::string> SplitSemi(const std::string& v) {
    std::vector<std::string> out; size_t p = 0;
    while (p <= v.size()) { size_t q = v.find(';', p); if (q == std::string::npos) q = v.size(); out.push_back(v.substr(p, q - p)); p = q + 1; }
    return out;
}
static Clip& ClipFor(Elem& e, const std::string& name) {   // (a name that isn't a standard clip: a custom one)
    if (!e.an.p) e.an.p = new ElemAnim(NewAnim());
    for (auto& c : e.an.p->clips) if (c.name == name) return c;
    Clip k; k.name = name; k.trigger = "(custom)"; for (int p = 0; p < P_NPROP; ++p) k.tracks[p].prop = p;
    e.an.p->clips.push_back(k);
    return e.an.p->clips.back();
}
static void AnimKey(Elem& e, const std::string& k, const std::string& v) {
    if (k == "afocus" || k == "apress" || k == "ablend" || k == "adelay" || k == "astagger") {
        if (!e.an.p) e.an.p = new ElemAnim(NewAnim());
        ElemAnim& a = *e.an.p;
        const float f = std::max(0.f, (float)atof(v.c_str()));
        if (k == "afocus") a.focusIf = v; else if (k == "apress") a.pressIf = v; else if (k == "ablend") a.blend = f;
        else if (k == "adelay") a.p.delay = f; else a.p.stagger = f;
        return;
    }
    std::vector<std::string> f = SplitSemi(v);
    if (f.empty() || f[0].empty()) return;
    Clip& c = ClipFor(e, f[0]);
    if (k == "aclip") {
        if (f.size() > 1) c.length = std::max(1.f, (float)atof(f[1].c_str()));
        if (f.size() > 2) for (int i = 0; i < 3; ++i) if (f[2] == kLoopKey[i]) c.loop = i;
        c.on = !(f.size() > 3 && f[3] == "off");
        return;
    }
    if (f.size() < 2) return;
    int prop = -1; for (int i = 0; i < P_NPROP; ++i) if (f[1] == kPropKey[i]) prop = i;
    if (prop < 0) return;
    Track& tr = c.tracks[prop]; tr.keys.clear();
    for (size_t i = 2; i < f.size(); ++i) {
        Key key; char ez[16] = "linear";
        if (sscanf(f[i].c_str(), "%f %f %15s %f %f %f %f", &key.t, &key.v, ez, &key.h1x, &key.h1y, &key.h2x, &key.h2y) < 2) continue;
        for (int j = 0; j < EZ_NEASE; ++j) if (!strcmp(ez, kEaseKey[j])) key.ease = j;
        tr.keys.push_back(key);
    }
    SortKeys(tr);
    c.on = true;
}
static std::string AnimText(const ElemAnim* a) { std::string o; if (a) PutAnim(o, *a); return o; }

// ---------------------------------------------------------------- interpolation buttons (linear / constant / bezier)

// the keys an easing change goes to: the picked ones, or (none picked) every key of the tracks the graph shows
static bool EaseTarget(const Clip& c, bool anyPicked, const std::vector<int>& shown, int p, const Key& k) {
    return anyPicked ? k.sel : std::find(shown.begin(), shown.end(), p) != shown.end();
}
static void ApplyEase(Clip& c, int ease) {
    bool any = false; for (auto& tr : c.tracks) for (auto& k : tr.keys) any |= k.sel;
    const std::vector<int> shown = GraphTracks(&c);
    for (int p = 0; p < P_NPROP; ++p) {
        Track& tr = c.tracks[p];
        for (size_t i = 0; i < tr.keys.size(); ++i) {
            Key& k = tr.keys[i];
            if (!EaseTarget(c, any, shown, p, k)) continue;
            if (ease == EZ_BEZIER) ToBezier(k, i + 1 < tr.keys.size() ? &tr.keys[i + 1] : nullptr);   // (keeps the shape)
            else k.ease = ease;
        }
    }
}
static void EaseButtons(Clip* c) {
    if (!c) return;
    bool any = false; for (auto& tr : c->tracks) for (auto& k : tr.keys) any |= k.sel;
    const std::vector<int> shown = GraphTracks(c);
    int common = -1; bool first = true;   // the targets' one easing (its button lit), -1: mixed / none
    for (int p = 0; p < P_NPROP; ++p) {
        const Track& tr = c->tracks[p];
        for (size_t i = 0; i + 1 < tr.keys.size(); ++i)   // (a track's last key leads nowhere: its easing doesn't count)
            if (EaseTarget(*c, any, shown, p, tr.keys[i])) { if (first) common = tr.keys[i].ease; else if (common != tr.keys[i].ease) common = -1; first = false; }
    }
    struct B { int ease; const char* id; const char* tip; };
    static const B bs[] = {
        { EZ_LINEAR, "##ezlin", "Linear: straight on to the next key." },
        { EZ_HOLD, "##ezconst", "Constant: holds its value, then jumps at the next key." },
        { EZ_BEZIER, "##ezbez", "Bezier: a curve with two handles to drag in the graph (it starts from the key's easing,\nor a smooth ease in-out from linear)." },
    };
    const float h = ImGui::GetTextLineHeight() + 2;
    for (int bi = 0; bi < 3; ++bi) {
        ImGui::SameLine(0, bi ? 2.f : 10.f);
        const bool on = common == bs[bi].ease;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        const bool click = ImGui::Button(bs[bi].id, ImVec2(h * 1.7f, h));
        if (on) ImGui::PopStyleColor();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const float x0 = a.x + h * 0.35f, x1 = b.x - h * 0.35f, y0 = a.y + h * 0.2f, y1 = b.y - h * 0.2f, xm = (x0 + x1) * 0.5f;
        const ImU32 col = on ? IM_COL32(255, 255, 255, 255) : IM_COL32(110, 170, 255, 255);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (bs[bi].ease == EZ_LINEAR) dl->AddLine(ImVec2(x0, y1), ImVec2(x1, y0), col, 2.f);
        else if (bs[bi].ease == EZ_HOLD) { ImVec2 q[4] = { ImVec2(x0, y1), ImVec2(xm, y1), ImVec2(xm, y0), ImVec2(x1, y0) }; dl->AddPolyline(q, 4, col, 0, 2.f); }
        else dl->AddBezierCubic(ImVec2(x0, y1), ImVec2(xm + h * 0.15f, y1), ImVec2(xm - h * 0.15f, y0), ImVec2(x1, y0), col, 2.f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n(%s)", bs[bi].tip, any ? "the picked keys" : "no keys picked: every key of the curves shown");
        if (click) ApplyEase(*c, bs[bi].ease);
    }
}

// ---------------------------------------------------------------- copy / cut / paste keys (Ctrl + C / X / V)
// The picked keys of the clip being edited, kept with their times from the first one, their tracks and easing (bezier
// handles too). Pasted at the playhead into the clip being edited now - this element or another, this clip or another;
// keys copied from one track go onto the track the graph shows, if it shows one (Scale X's keys onto Scale Y).

struct KeyClip { struct K { int prop; float dt; Key k; }; std::vector<K> keys; std::string from; };
static KeyClip g_kclip;
static std::string g_note; static double g_noteAt = -1e9;   // (a line under the graph's header for a few seconds)
static void Note(const std::string& m) { g_note = m; g_noteAt = ImGui::GetTime(); }
static int PickedKeys(const Clip& c) { int n = 0; for (auto& tr : c.tracks) for (auto& k : tr.keys) n += k.sel; return n; }
static void CopyKeys(bool cut) {
    Clip* c = CurClip();
    if (!c) return;
    float t0 = 1e9f;
    for (auto& tr : c->tracks) for (auto& k : tr.keys) if (k.sel) t0 = std::min(t0, k.t);
    if (t0 > 1e8f) { Note("Pick keys first: click them in the graph or the timeline (Shift: several, Ctrl + A: all shown)."); return; }
    g_kclip.keys.clear();
    for (int p = 0; p < P_NPROP; ++p) for (auto& k : c->tracks[p].keys) if (k.sel) { Key kk = k; kk.sel = false; g_kclip.keys.push_back({ p, k.t - t0, kk }); }
    const int fi = FocusedIndex();
    g_kclip.from = (fi >= 0 ? Elems()[fi].name : std::string("?")) + ", " + c->name;
    if (cut) for (auto& tr : c->tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end());
    Note(std::string(cut ? "Cut " : "Copied ") + std::to_string(g_kclip.keys.size()) + " key" + (g_kclip.keys.size() == 1 ? "" : "s") + " (" + g_kclip.from + ").");
}
static void PasteKeys() {
    Clip* c = CurClip();
    if (!c) { Note("Pick an element to paste into."); return; }
    if (g_kclip.keys.empty()) { Note("Nothing copied yet: pick keys and press Ctrl + C."); return; }
    bool single = true;
    for (auto& k : g_kclip.keys) single &= k.prop == g_kclip.keys[0].prop;
    const int onto = single && g.focus == State::F_TRACK && g.track >= 0 ? g.track : -1;   // (one track's keys: the shown track)
    for (auto& tr : c->tracks) for (auto& k : tr.keys) k.sel = false;
    const float at = std::max(0.f, Snap(g.time));
    float last = 0;
    for (auto& ck : g_kclip.keys) {
        Track& tr = c->tracks[onto >= 0 ? onto : ck.prop];
        Key k = ck.k; k.t = std::max(0.f, Snap(at + ck.dt)); k.sel = true;
        bool put = false;
        for (auto& o : tr.keys) if (fabsf(o.t - k.t) < 0.5f) { o = k; put = true; break; }   // (a key there: replaced)
        if (!put) tr.keys.push_back(k);
        last = std::max(last, k.t);
    }
    for (auto& tr : c->tracks) SortKeys(tr);
    c->on = true;
    if (last > c->length) c->length = last;
    g.graph.fit = g.big.fit = true;
    Note("Pasted " + std::to_string(g_kclip.keys.size()) + " key" + (g_kclip.keys.size() == 1 ? "" : "s") + " at " + TimeText(at) +
         (onto >= 0 ? std::string(" onto ") + kProp[onto].name : std::string()) + " (from " + g_kclip.from + ").");
}
static void PickAllShown() {   // every key of the tracks the graph shows
    Clip* c = CurClip();
    if (!c) return;
    const std::vector<int> shown = GraphTracks(c);
    for (int p = 0; p < P_NPROP; ++p) for (auto& k : c->tracks[p].keys) k.sel = std::find(shown.begin(), shown.end(), p) != shown.end();
}
static void KeyClipItems() {   // (the key menus' copy / paste part)
    Clip* c = CurClip();
    const int n = c ? PickedKeys(*c) : 0;
    if (ImGui::MenuItem("Copy keys", "Ctrl+C", false, n > 0)) CopyKeys(false);
    if (ImGui::MenuItem("Cut keys", "Ctrl+X", false, n > 0)) CopyKeys(true);
    char lb[96]; snprintf(lb, sizeof lb, "Paste %d key%s at the playhead", (int)g_kclip.keys.size(), g_kclip.keys.size() == 1 ? "" : "s");
    if (ImGui::MenuItem(g_kclip.keys.empty() ? "Paste at the playhead" : lb, "Ctrl+V", false, !g_kclip.keys.empty() && c)) PasteKeys();
    if (ImGui::MenuItem("Pick all shown", "Ctrl+A", false, c != nullptr)) PickAllShown();
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
    float hitD = 1e9f;   // (the nearest key or handle under the mouse wins)
    auto tryHit = [&](ImVec2 q, float reach, int p, int i, int h) {
        if (!hov && g.dragKey < 0) return;
        const float dx = m.x - q.x, dy = m.y - q.y, d = dx * dx + dy * dy;
        if (fabsf(dx) <= reach && fabsf(dy) <= reach && d < hitD) { hitD = d; hitTrack = p; hitKey = i; hitHandle = h; }
    };
    const bool dragging = g.dragKey >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left);
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
            // bezier: the handles of the way to the next key (h1 off this key, h2 into the next), dragged to shape it
            if (k.ease == EZ_BEZIER && i + 1 < (int)tr.keys.size()) {
                const Key& n = tr.keys[i + 1];
                const ImVec2 np(X(n.t), Yv(n.v));
                const ImVec2 h1(X(k.t + k.h1x * (n.t - k.t)), Yv(k.v + k.h1y));
                const ImVec2 h2(X(k.t + k.h2x * (n.t - k.t)), Yv(n.v + k.h2y));
                const bool act1 = dragging && g.dragTrack == p && g.dragKey == i && g.dragHandle == 1;
                const bool act2 = dragging && g.dragTrack == p && g.dragKey == i && g.dragHandle == 2;
                dl->AddLine(kp, h1, (col & 0x00FFFFFF) | 0xA0000000, 1.5f); dl->AddLine(np, h2, (col & 0x00FFFFFF) | 0xA0000000, 1.5f);
                for (int h = 0; h < 2; ++h) {
                    const ImVec2 hp = h ? h2 : h1; const bool act = h ? act2 : act1;
                    dl->AddCircleFilled(hp, act ? 5.5f : 4.5f, act ? IM_COL32(255, 200, 60, 255) : IM_COL32(245, 245, 245, 255));
                    dl->AddCircle(hp, act ? 5.5f : 4.5f, IM_COL32(0, 0, 0, 200), 0, 1.f);
                }
                tryHit(h1, 8, p, i, 1); tryHit(h2, 8, p, i, 2);
            }
            const float r = 5;
            ImVec2 q[4] = { ImVec2(kp.x, kp.y - r), ImVec2(kp.x + r, kp.y), ImVec2(kp.x, kp.y + r), ImVec2(kp.x - r, kp.y) };
            dl->AddConvexPolyFilled(q, 4, k.sel ? IM_COL32(255, 200, 60, 255) : IM_COL32(235, 235, 235, 255));
            dl->AddPolyline(q, 4, IM_COL32(0, 0, 0, 200), ImDrawFlags_Closed, 1.f);
            tryHit(kp, 7, p, i, 0);
        }
    }
    if (hov && !dragging && hitKey >= 0) {   // what a click would grab
        const Track& tr = c->tracks[hitTrack]; const Key& k = tr.keys[hitKey];
        ImVec2 q(X(k.t), Yv(k.v));
        if (hitHandle && hitKey + 1 < (int)tr.keys.size()) {
            const Key& n = tr.keys[hitKey + 1];
            q = hitHandle == 1 ? ImVec2(X(k.t + k.h1x * (n.t - k.t)), Yv(k.v + k.h1y)) : ImVec2(X(k.t + k.h2x * (n.t - k.t)), Yv(n.v + k.h2y));
        }
        dl->AddCircle(q, 9, IM_COL32(255, 200, 60, 200), 0, 1.5f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
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
                // (time: kept inside the way, so the curve never turns back on itself; value: free; Shift: value only)
                const Key& n = tr.keys[g.dragKey + 1];
                const float u = std::max(0.f, std::min(1.f, (T(m.x) - k.t) / std::max(0.001f, n.t - k.t)));
                if (g.dragHandle == 1) { if (!io.KeyShift) k.h1x = u; k.h1y = V(m.y) - k.v; }
                else { if (!io.KeyShift) k.h2x = u; k.h2y = V(m.y) - n.v; }
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
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hitKey < 0) ImGui::OpenPopup("##graphmenu");
    if (ImGui::BeginPopup("##graphmenu")) { KeyClipItems(); ImGui::EndPopup(); }
    if (ImGui::BeginPopup("##keymenu")) {
        ImGui::TextDisabled("Way to the next key");
        for (int e = 0; e < EZ_NEASE; ++e)
            if (ImGui::MenuItem(kEase[e])) ApplyEase(*c, e);
        ImGui::Separator();
        KeyClipItems();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete key")) for (auto& tr : c->tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end());
        ImGui::EndPopup();
    }
    if (hov && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X)) && !io.WantTextInput)
        for (auto& tr : c->tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end());
    if (hov) ImGui::SetTooltip("Drag keys (Shift: time only). Round dots: bezier handles - drag to shape the curve (Shift: height only).\nThe buttons above: linear / constant / bezier for the picked keys. Right-click a key: every easing, delete.\nCtrl + C / X / V: copy / cut / paste the picked keys (at the playhead), Ctrl + A: pick all shown.\nDouble-click: add a key (one track shown). Click: move the playhead. Wheel: zoom time, Ctrl+wheel: values,\nmiddle / Alt drag: pan, F: fit.");
}


// ---------------------------------------------------------------- elements list (right edge)


// ---------------------------------------------------------------- the workspace's windows: moved and sized by hand,
// their edges snapping to each other's and the screen's; kept (as fractions of the screen) in <REUI>\anim_windows.txt

// (the animation page and the element list: one window, a divider between them - `split`, the page's share of its width)
enum { WIN_SIDE, WIN_GRAPH, WIN_TL, WIN_N };
static const char* kWinKey[WIN_N] = { "panels", "graph", "timeline" };
struct WinRect { float x = 0, y = 0, w = 0, h = 0; bool set = false; };
struct WinState {
    WinRect r[WIN_N];             // where each one is (fractions of the screen)
    bool loaded = false, force[WIN_N] = {}, dirty = false;
    ImVec2 disp;                  // the screen they were last placed on
    ImVec2 now[WIN_N][2]; bool shown[WIN_N] = {}, shownLast[WIN_N] = {};   // this frame's (pixels), for snapping
    ImVec2 last[WIN_N][2];        // last frame's
    bool drag[WIN_N] = {}; ImVec2 dragTo[WIN_N];   // moved by our own grip (a window without a title bar): where to
    float split = -1;             // the side window's divider (-1: the standard one)
};
static WinState w_;
static std::string WinFile() { return dataDir + "anim_windows.txt"; }
static void WinLoad() {
    w_.loaded = true;
    FILE* f = fopen(WinFile().c_str(), "r");
    if (!f) return;
    char line[256], key[64]; float x, y, w, h;
    WinRect oldPage, oldList;   // (before 2026-11-10's merge: "animation" and "elements" were two windows)
    while (fgets(line, sizeof line, f)) {
        const int n = sscanf(line, "%63s %f %f %f %f", key, &x, &y, &w, &h);
        if (n == 2 && !strcmp(key, "split")) { w_.split = x; continue; }
        if (n != 5) continue;
        for (int i = 0; i < WIN_N; ++i) if (!strcmp(key, kWinKey[i])) w_.r[i] = { x, y, w, h, true };
        if (!strcmp(key, "animation")) oldPage = { x, y, w, h, true };
        if (!strcmp(key, "elements")) oldList = { x, y, w, h, true };
    }
    fclose(f);
    if (!w_.r[WIN_SIDE].set && (oldPage.set || oldList.set)) {   // the two old windows' area, the divider where they met
        const WinRect& p = oldPage.set ? oldPage : oldList; const WinRect& l = oldList.set ? oldList : oldPage;
        const float x0 = std::min(p.x, l.x), y0 = std::min(p.y, l.y), x1 = std::max(p.x + p.w, l.x + l.w), y1 = std::max(p.y + p.h, l.y + l.h);
        w_.r[WIN_SIDE] = { x0, y0, x1 - x0, y1 - y0, true };
        if (oldPage.set && oldList.set && x1 > x0) w_.split = (p.x + p.w - x0) / (x1 - x0);
        w_.dirty = true;
    }
}
static void WinSave() {
    w_.dirty = false;
    FILE* f = fopen(WinFile().c_str(), "w");
    if (!f) return;
    for (int i = 0; i < WIN_N; ++i) if (w_.r[i].set) fprintf(f, "%s %.5f %.5f %.5f %.5f\n", kWinKey[i], w_.r[i].x, w_.r[i].y, w_.r[i].w, w_.r[i].h);
    if (w_.split > 0) fprintf(f, "split %.5f\n", w_.split);
    fclose(f);
}
static void WinReset() {   // back to the standard layout (Frame's defaults)
    for (auto& r : w_.r) r.set = false;
    for (bool& f : w_.force) f = true;
    w_.split = -1;
    remove(WinFile().c_str());
}
// a window of the workspace: placed at its own rect (or `defPos` / `defSize` until it has one); when the person drags
// or sizes it, the moving edges snap (within 10 px) to the screen's edges and the other windows' edges
static bool WinBegin(int id, const char* title, ImVec2 defPos, ImVec2 defSize, ImGuiWindowFlags extra = 0) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 D = io.DisplaySize;
    if (!w_.loaded) WinLoad();
    WinRect& r = w_.r[id];
    const bool dispChanged = D.x != w_.disp.x || D.y != w_.disp.y;
    if (!r.set) { r = { defPos.x / D.x, defPos.y / D.y, defSize.x / D.x, defSize.y / D.y, true }; w_.force[id] = true; }
    if (w_.force[id] || dispChanged || !w_.shownLast[id]) {   // (placed by us: first showing, a reset, a new screen size)
        ImGui::SetNextWindowPos(ImVec2(r.x * D.x, r.y * D.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(r.w * D.x, r.h * D.y), ImGuiCond_Always);
    }
    const bool placed = w_.force[id] || dispChanged || !w_.shownLast[id];
    if (!placed && w_.drag[id]) ImGui::SetNextWindowPos(w_.dragTo[id], ImGuiCond_Always);   // (then snapped as any move)
    w_.force[id] = false;
    const bool open = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | extra);
    ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
    const ImVec2 lp = w_.last[id][0], ls = w_.last[id][1];
    const bool moved = p.x != lp.x || p.y != lp.y, sized = sz.x != ls.x || sz.y != ls.y;
    if (!placed && (moved || sized) && ImGui::IsMouseDown(0)) {   // the person is moving / sizing it: snap
        const float T = 10.f * ImGui::GetFontSize() / 13.f;
        std::vector<float> xs = { 0.f, D.x }, ys = { 0.f, D.y };
        std::vector<std::pair<float, float>> xr = { { -1e9f, 1e9f }, { -1e9f, 1e9f } }, yr = xr;   // (each edge's span along the other axis)
        for (int j = 0; j < WIN_N; ++j) {
            if (j == id || !w_.shownLast[j]) continue;
            const ImVec2 jp = w_.last[j][0], js = w_.last[j][1];
            for (float x : { jp.x, jp.x + js.x }) { xs.push_back(x); xr.push_back({ jp.y, jp.y + js.y }); }
            for (float y : { jp.y, jp.y + js.y }) { ys.push_back(y); yr.push_back({ jp.x, jp.x + js.x }); }
        }
        // the nearest target for an edge at `e` (0 if none within T); spans must meet (with T to spare)
        auto nearest = [&](float e, const std::vector<float>& ts, const std::vector<std::pair<float, float>>& span, float lo, float hi) {
            float best = 0, bd = T;
            for (size_t k = 0; k < ts.size(); ++k) {
                if (span[k].second < lo - T || span[k].first > hi + T) continue;
                const float d = ts[k] - e;
                if (fabsf(d) < bd) { bd = fabsf(d); best = d; }
            }
            return best;
        };
        if (moved && !sized) {   // dragged: the whole window shifts to the closer of its two edges' snaps
            float dl = nearest(p.x, xs, xr, p.y, p.y + sz.y), dr = nearest(p.x + sz.x, xs, xr, p.y, p.y + sz.y);
            float dt = nearest(p.y, ys, yr, p.x, p.x + sz.x), db = nearest(p.y + sz.y, ys, yr, p.x, p.x + sz.x);
            const float dx = dl == 0 ? dr : dr == 0 ? dl : fabsf(dl) < fabsf(dr) ? dl : dr;
            const float dy = dt == 0 ? db : db == 0 ? dt : fabsf(dt) < fabsf(db) ? dt : db;
            p = ImVec2(p.x + dx, p.y + dy);
        } else if (sized) {   // sized: only the edges that moved snap
            float L = p.x, R = p.x + sz.x, Tp = p.y, B = p.y + sz.y;
            if (p.x != lp.x) L += nearest(L, xs, xr, Tp, B);
            if (p.x + sz.x != lp.x + ls.x) R += nearest(R, xs, xr, Tp, B);
            if (p.y != lp.y) Tp += nearest(Tp, ys, yr, L, R);
            if (p.y + sz.y != lp.y + ls.y) B += nearest(B, ys, yr, L, R);
            p = ImVec2(L, Tp); sz = ImVec2(std::max(60.f, R - L), std::max(40.f, B - Tp));
            ImGui::SetWindowSize(sz);
        }
        ImGui::SetWindowPos(p);
        r = { p.x / D.x, p.y / D.y, sz.x / D.x, sz.y / D.y, true };
        w_.dirty = true;
    }
    w_.now[id][0] = p; w_.now[id][1] = sz; w_.shown[id] = true;
    return open;
}
static void WinFrameEnd() {   // (after every window of the frame)
    const ImVec2 D = ImGui::GetIO().DisplaySize;
    w_.disp = D;
    for (int i = 0; i < WIN_N; ++i) {
        w_.shownLast[i] = w_.shown[i]; w_.shown[i] = false;
        w_.last[i][0] = w_.now[i][0]; w_.last[i][1] = w_.now[i][1];
    }
    if (w_.dirty && !ImGui::IsMouseDown(0)) WinSave();
}
// a group's motion element (focused; made if it has none: at the middle of the group's elements, shown as they are)
static void FocusMotion(const std::string& grp) {
    auto& els = Elems();
    for (int i = 0; i < (int)els.size(); ++i)
        if (els[i].type == E_MOTION && els[i].group == grp) { g.elemKey = KeyOf(g_editMode, els[i]); g_sel = i; g.focus = State::F_ELEMENT; g.graph.fit = g.big.fit = true; return; }
    ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
    std::string show; bool first = true;
    for (auto& o : els) {
        if (!InGroup(o.group, grp) || o.type == E_MOTION) continue;
        ImVec2 a, b; ElemRect(o, a, b);
        lo = ImVec2(std::min(lo.x, std::min(a.x, b.x)), std::min(lo.y, std::min(a.y, b.y)));
        hi = ImVec2(std::max(hi.x, std::max(a.x, b.x)), std::max(hi.y, std::max(a.y, b.y)));
        if (first) { show = o.showIf; first = false; } else if (o.showIf != show && !show.empty()) show = "";   // (shared one only)
    }
    if (first) return;
    Elem c;
    c.type = E_MOTION; c.mode = g_editMode; c.texIdx = c.fontIdx = -2; c.shownSince = -1;
    c.name = GroupLeaf(grp) + " motion"; c.group = grp; c.showIf = show;
    c.anchor = 4; c.x = c.y = 0; c.w = c.h = 12; c.visible = true;
    ImVec2 a, b; ElemRect(c, a, b);
    c.x = ((lo.x + hi.x) / 2 - (a.x + b.x) / 2) / Unit(); c.y = ((lo.y + hi.y) / 2 - (a.y + b.y) / 2) / Unit();
    els.push_back(c);
    g_dirty = true;
    g.elemKey = KeyOf(g_editMode, els.back()); g_sel = (int)els.size() - 1; g.focus = State::F_ELEMENT; g.graph.fit = g.big.fit = true;
}
static void ElementsList() {   // (the right side of the panels window)
    ImGui::TextUnformatted("Elements");
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
            if (!e.group.empty()) {   // a group: click to animate it as one (its motion element)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.65f, 0.3f, 1));
                const bool click = ImGui::Selectable((e.group + "##ag" + std::to_string(i)).c_str(), false);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click: animate the group as one - its motion element (made at the group's middle if it has none).\nIts clips move, scale, turn, fade and tint every element in it, about that point.");
                if (click) FocusMotion(e.group);
            }
        }
        const std::string key = KeyOf(g_editMode, e);
        int nk = 0; if (e.an.p) for (auto& c : e.an.p->clips) nk += Keys(c);
        char label[256];
        snprintf(label, sizeof label, "%s[%s] %s%s##ae%d", e.group.empty() ? "" : "   ", TypeKey(e.type), e.name.c_str(), nk ? "  *" : "", i);
        if (ImGui::Selectable(label, key == g.elemKey)) {
            g.elemKey = key; g_sel = i; g.focus = State::F_ELEMENT; g.graph.fit = g.big.fit = true;
        }
        if (nk && ImGui::IsItemHovered()) ImGui::SetTooltip("%d keys", nk);
    }
    if (order.empty()) ImGui::TextDisabled("No elements in this layout.");
    ImGui::EndChild();
}

// ---------------------------------------------------------------- copy motion from the game
// A piece's motion in one of the game's Flash clips (uied::motionSource: frame by frame), made into eased keys on the
// clip being edited: relative to its rest frame (its place there is the element's own place), fitted per track - split
// where the best ease is furthest off, then every key the neighbours can do without dropped (tools/fe/fe2reui.py).

static const int kFitEases[] = { EZ_LINEAR, EZ_QOUT, EZ_QIN, EZ_QINOUT, EZ_OUT, EZ_IN, EZ_INOUT, EZ_BACK_OUT, EZ_BACK_IN, EZ_HOLD };
static float FitEase(int e, float u) { Key k; k.ease = e; return Eased(k, u); }
struct FitBest { float err; int ease; };
static FitBest FitSeg(const std::vector<float>& v, int i, int j) {
    if (j <= i + 1) return { 0.f, EZ_LINEAR };
    FitBest b{ 1e30f, EZ_LINEAR };
    for (int e : kFitEases) {
        float m = 0;
        for (int k = i; k <= j; ++k) m = std::max(m, fabsf(v[k] - (v[i] + (v[j] - v[i]) * FitEase(e, (float)(k - i) / (j - i)))));
        if (m < b.err - 1e-9f) b = { m, e };
    }
    return b;
}
static void FitSplit(const std::vector<float>& v, int i, int j, float tol, std::vector<int>& starts) {
    const FitBest b = FitSeg(v, i, j);
    if (b.err <= tol || j - i <= 1) { starts.push_back(i); return; }
    int worst = i + 1; float wv = -1;   // split at the frame it's furthest off
    for (int k = i + 1; k < j; ++k) {
        const float d = fabsf(v[k] - (v[i] + (v[j] - v[i]) * FitEase(b.ease, (float)(k - i) / (j - i))));
        if (d > wv) { wv = d; worst = k; }
    }
    FitSplit(v, i, worst, tol, starts); FitSplit(v, worst, j, tol, starts);
}
static std::vector<Key> FitKeys(const std::vector<float>& v, float tol, float msPerFrame) {
    std::vector<Key> out;
    const int n = (int)v.size();
    if (!n) return out;
    const float lo = *std::min_element(v.begin(), v.end()), hi = *std::max_element(v.begin(), v.end());
    if (n == 1 || hi - lo <= tol * 0.25f) { Key k; k.t = 0; k.v = v[0]; out.push_back(k); return out; }
    std::vector<int> idx; FitSplit(v, 0, n - 1, tol, idx); idx.push_back(n - 1);
    for (bool changed = true; changed && idx.size() > 2;) {   // drop the keys the keys around them can do without
        changed = false;
        for (size_t k = 1; k + 1 < idx.size(); ++k)
            if (FitSeg(v, idx[k - 1], idx[k + 1]).err <= tol) { idx.erase(idx.begin() + k); changed = true; break; }
    }
    for (size_t k = 0; k < idx.size(); ++k) {
        Key key; key.t = idx[k] * msPerFrame; key.v = v[idx[k]];
        key.ease = k + 1 < idx.size() ? FitSeg(v, idx[k], idx[k + 1]).ease : EZ_LINEAR;
        out.push_back(key);
    }
    return out;
}
// the piece's frames as track values (relative to its rest frame), fitted; false if it never shows
static bool MotionToClip(const MotionPiece& pc, int rest, float ppu, float fps, Clip& c, int& keys, int& tracks) {
    const int n = (int)pc.frames.size();
    if (!n) return false;
    int r = rest < 0 ? 0 : std::min(rest, n - 1);   // (a rest frame it isn't on: the nearest one it's on)
    if (!pc.frames[r].on) {
        int best = -1;
        for (int d = 1; d < n && best < 0; ++d) { if (r + d < n && pc.frames[r + d].on) best = r + d; else if (r - d >= 0 && pc.frames[r - d].on) best = r - d; }
        if (best < 0) return false;
        r = best;
    }
    const MotionFrame r0 = pc.frames[r];
    std::vector<MotionFrame> seq(pc.frames);
    for (auto& f : seq) if (!f.on) { f = r0; f.a = 0; }
    for (int i = n - 1; i >= 0; --i)   // while it's hidden it doesn't move: the values of the next frame it shows on
        if (seq[i].a <= 0.004f) { const float a = seq[i].a; seq[i] = i + 1 < n ? seq[i + 1] : r0; seq[i].a = a; }
    std::vector<float> v[P_NPROP];
    float rotPrev = 0;
    for (int i = 0; i < n; ++i) {
        const MotionFrame& f = seq[i];
        v[P_X].push_back((f.x - r0.x) / ppu); v[P_Y].push_back((f.y - r0.y) / ppu);
        v[P_SX].push_back(r0.sx != 0 ? f.sx / r0.sx : f.sx); v[P_SY].push_back(r0.sy != 0 ? f.sy / r0.sy : f.sy);
        float rot = f.rot - r0.rot;   // (unwrapped: no jumps of 360)
        if (i) { while (rot - rotPrev > 180) rot -= 360; while (rot - rotPrev < -180) rot += 360; }
        rotPrev = rot; v[P_ROT].push_back(rot);
        v[P_ALPHA].push_back(r0.a > 0 ? std::min(1.f, f.a / r0.a) : f.a);
        v[P_MR].push_back(1); v[P_MG].push_back(1); v[P_MB].push_back(1);
        v[P_AR].push_back(f.add[0]); v[P_AG].push_back(f.add[1]); v[P_AB].push_back(f.add[2]);
    }
    const float tol0[P_NPROP] = { 0.5f, 0.5f, 0.008f, 0.008f, 0.3f, 0.025f, 0.01f, 0.01f, 0.01f, 0.03f, 0.03f, 0.03f };
    for (auto& tr : c.tracks) tr.keys.clear();
    keys = tracks = 0;
    const float msPerFrame = 1000.f / std::max(1.f, fps);
    for (int p = 0; p < P_NPROP; ++p) {
        if (p == P_MR || p == P_MG || p == P_MB) continue;   // (Flash's colour multiply: not copied)
        const float lo = *std::min_element(v[p].begin(), v[p].end()), hi = *std::max_element(v[p].begin(), v[p].end());
        const float tol = std::max(tol0[p], 0.03f * (hi - lo));   // (3% of its own range: a big move needn't follow every pixel)
        std::vector<Key> ks = FitKeys(v[p], tol, msPerFrame);
        if (ks.size() == 1 && fabsf(ks[0].v - kProp[p].def) < tol * 0.25f) continue;   // (it stays as it is)
        c.tracks[p].keys = ks; keys += (int)ks.size(); ++tracks;
    }
    c.length = std::max(33.f, (n - 1) * msPerFrame);
    c.on = true;
    return true;
}

// ---- every element at once: the clip's pieces paired with the layout's elements that show the same picture. The
// piece picked and the focused element are the anchor (they fix where the clip's pixels are on screen); every other
// element takes the nearest unused piece with its picture (in units, after that offset)
static std::string LowerS(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
struct MotionPair { int elem, piece; float dist; };
static ImVec2 PieceAt(const MotionPiece& pc, int rest, float ppu) {   // its rest position (units; the nearest frame it's on)
    const int n = (int)pc.frames.size();
    int r = std::max(0, std::min(rest, n - 1));
    for (int d = 0; d < n; ++d) { if (r + d < n && pc.frames[r + d].on) { r += d; break; } if (r - d >= 0 && pc.frames[r - d].on) { r -= d; break; } }
    return ImVec2(pc.frames[r].x / ppu, pc.frames[r].y / ppu);
}
static ImVec2 ElemAt(Elem& e) {   // its centre (units from the screen's middle)
    ImVec2 a, b; ElemRect(e, a, b);
    const ImVec2 D = ImGui::GetIO().DisplaySize; const float k = Unit();
    return ImVec2(((a.x + b.x) / 2 - D.x / 2) / k, ((a.y + b.y) / 2 - D.y / 2) / k);
}
static std::string TopGroup(const std::string& g) { return g.substr(0, g.find('/')); }
static std::vector<MotionPair> MatchPieces(int anchorElem, int anchorPiece, const std::vector<MotionPiece>& ps, int rest, float ppu, bool wholeLayout) {
    std::vector<MotionPair> out;
    auto& els = Elems();
    if (anchorElem < 0 || anchorElem >= (int)els.size() || anchorPiece < 0 || anchorPiece >= (int)ps.size()) return out;
    const ImVec2 ea = ElemAt(els[anchorElem]), pa = PieceAt(ps[anchorPiece], rest, ppu);
    const ImVec2 off(ea.x - pa.x, ea.y - pa.y);
    const std::string scope = TopGroup(els[anchorElem].group);
    std::vector<bool> usedP(ps.size(), false), usedE(els.size(), false);
    out.push_back({ anchorElem, anchorPiece, 0.f }); usedP[anchorPiece] = usedE[anchorElem] = true;
    std::vector<MotionPair> cand;
    for (int i = 0; i < (int)els.size(); ++i) {
        Elem& e = els[i];
        if (usedE[i] || e.type == E_MOTION || e.tex.empty()) continue;
        if (!wholeLayout && TopGroup(e.group) != scope) continue;
        const std::string tex = LowerS(e.tex);
        const ImVec2 at = ElemAt(e);
        for (int j = 0; j < (int)ps.size(); ++j) {
            if (ps[j].image != tex) continue;
            const ImVec2 p = PieceAt(ps[j], rest, ppu);
            cand.push_back({ i, j, hypotf(p.x + off.x - at.x, p.y + off.y - at.y) });
        }
    }
    std::sort(cand.begin(), cand.end(), [](const MotionPair& a, const MotionPair& b) { return a.dist < b.dist; });
    for (auto& c : cand) if (!usedE[c.elem] && !usedP[c.piece]) { usedE[c.elem] = usedP[c.piece] = true; out.push_back(c); }
    return out;
}
static int ApplyPairs(const std::vector<MotionPair>& pairs, const std::vector<MotionPiece>& ps, const std::string& clipName, int rest, float ppu, float fps, int& keys) {
    int done = 0; keys = 0;
    for (auto& pr : pairs) {
        Clip& c = ClipFor(Elems()[pr.elem], clipName);
        int k = 0, t = 0;
        if (MotionToClip(ps[pr.piece], rest, ppu, fps, c, k, t)) { ++done; keys += k; }
    }
    if (done) g_dirty = true;
    return done;
}

static struct CopyMotionUi {
    std::vector<std::string> movies; bool listed = false;
    std::string movie, forElem;
    char filter[64] = "";
    std::vector<MotionClipInfo> clips; float fps = 30, ppu = 1.5f; std::string clipsFor;
    int clip = -1; std::vector<MotionPiece> pieces; std::string piecesFor;
    int piece = -1, rest = 0;   // rest: 0 auto (Out: its first frame, the others: their last), 1 first, 2 last
    bool wholeLayout = false;     // every element at once: in the whole layout (else the focused element's outermost group)
    std::string err;
} g_cm;
// the Flash clip as the game draws it, in a box on the page: each piece's picture where it is on the frame (turned,
// scaled, faded); the picked piece outlined; a click picks the piece under the mouse
static bool g_cmShow = false;   // (tests: the section opened and scrolled to once)
static struct MotionPreview { float frame = 0; bool play = true; std::string boundsFor; ImVec2 lo, hi; } g_mpv;
static void PreviewCorners(const MotionPiece& pc, const MotionFrame& f, ImVec2 c[4], ImVec2 o, float k) {   // (box corners on screen)
    const float r = f.rot * 3.14159265f / 180, cs = cosf(r), sn = sinf(r);
    const float hw = std::max(2.f, pc.w) * f.sx / 2, hh = std::max(2.f, pc.h) * f.sy / 2;
    const float xs[4] = { -hw, hw, hw, -hw }, ys[4] = { -hh, -hh, hh, hh };
    for (int i = 0; i < 4; ++i) c[i] = ImVec2(o.x + (f.x + xs[i] * cs - ys[i] * sn) * k, o.y + (f.y + xs[i] * sn + ys[i] * cs) * k);
}
static void MotionPreviewBox(std::vector<MotionPiece>& ps, int& picked, int frames, float fps, const std::string& key) {
    MotionPreview& v = g_mpv;
    if (v.boundsFor != key) {   // the clip's extent over all its frames (the view fits it)
        v.boundsFor = key; v.frame = g_cmShow ? frames / 2.f : 0; v.lo = ImVec2(1e9f, 1e9f); v.hi = ImVec2(-1e9f, -1e9f);
        for (auto& pc : ps) for (auto& f : pc.frames) {
            if (!f.on || f.a <= 0.004f) continue;
            ImVec2 c[4]; PreviewCorners(pc, f, c, ImVec2(0, 0), 1.f);
            for (auto& q : c) { v.lo = ImVec2(std::min(v.lo.x, q.x), std::min(v.lo.y, q.y)); v.hi = ImVec2(std::max(v.hi.x, q.x), std::max(v.hi.y, q.y)); }
        }
        if (v.lo.x > v.hi.x) { v.lo = ImVec2(0, 0); v.hi = ImVec2(100, 100); }
    }
    if (ImGui::SmallButton(v.play ? " || ##mpv" : "  >  ##mpv")) v.play = !v.play;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / pause the game's clip (looping, at %g fps).", fps);
    ImGui::SameLine(); ImGui::SetNextItemWidth(-60);
    if (ImGui::SliderFloat("##mpvf", &v.frame, 0, (float)std::max(0, frames - 1), "frame %.0f")) v.play = false;
    if (v.play && frames > 1) { v.frame += ImGui::GetIO().DeltaTime * fps * (float)g_speed; if (v.frame >= frames) v.frame = fmodf(v.frame, (float)frames); }
    const int fi = std::max(0, std::min(frames - 1, (int)v.frame));
    const float W = ImGui::GetContentRegionAvail().x - 4, H = std::max(110.f, std::min(W * 0.56f, 280.f));
    const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + W, a.y + H);
    ImGui::InvisibleButton("##mpvbox", ImVec2(W, H));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, IM_COL32(18, 20, 26, 255)); dl->AddRect(a, b, IM_COL32(255, 255, 255, 40));
    const float bw = std::max(1.f, v.hi.x - v.lo.x), bh = std::max(1.f, v.hi.y - v.lo.y);
    const float k = std::min((W - 16) / bw, (H - 16) / bh);
    const ImVec2 o(a.x + W / 2 - (v.lo.x + bw / 2) * k, a.y + H / 2 - (v.lo.y + bh / 2) * k);
    dl->PushClipRect(a, b, true);
    int under = -1;
    const ImVec2 m = ImGui::GetIO().MousePos;
    for (int i = 0; i < (int)ps.size(); ++i) {   // (in the clip's order: the later on top)
        const MotionPiece& pc = ps[i];
        const MotionFrame& f = pc.frames[fi];
        if (!f.on || f.a <= 0.004f) continue;
        ImVec2 c[4]; PreviewCorners(pc, f, c, o, k);
        float su = 1, sv = 1;
        gfx::Tex tx = pc.image.empty() ? nullptr : NamedTex(pc.image, (int)(std::max(pc.w, pc.h) * k), su, sv);
        const int al = (int)(255 * std::min(1.f, f.a));
        if (tx) dl->AddImageQuad(gfx::Id(tx), c[0], c[1], c[2], c[3], ImVec2(0, 0), ImVec2(su, 0), ImVec2(su, sv), ImVec2(0, sv), IM_COL32(255, 255, 255, al));
        else dl->AddPolyline(c, 4, IM_COL32(150, 160, 190, al / 2), ImDrawFlags_Closed, 1.f);   // (a shape / text: its box)
        if (hov) {   // (under the mouse: inside its box)
            bool in = true;
            for (int j = 0; j < 4 && in; ++j) { const ImVec2 p = c[j], q = c[(j + 1) % 4]; in = (q.x - p.x) * (m.y - p.y) - (q.y - p.y) * (m.x - p.x) >= 0; }
            if (!in) { in = true; for (int j = 0; j < 4 && in; ++j) { const ImVec2 p = c[j], q = c[(j + 1) % 4]; in = (q.x - p.x) * (m.y - p.y) - (q.y - p.y) * (m.x - p.x) <= 0; } }
            if (in) under = i;
        }
    }
    if (picked >= 0 && picked < (int)ps.size() && ps[picked].frames[fi].on) {   // the picked piece
        ImVec2 c[4]; PreviewCorners(ps[picked], ps[picked].frames[fi], c, o, k);
        dl->AddPolyline(c, 4, IM_COL32(255, 170, 60, 255), ImDrawFlags_Closed, 2.f);
    }
    if (under >= 0 && under != picked) { ImVec2 c[4]; PreviewCorners(ps[under], ps[under].frames[fi], c, o, k); dl->AddPolyline(c, 4, IM_COL32(110, 170, 255, 220), ImDrawFlags_Closed, 1.5f); }
    dl->PopClipRect();
    if (hov && under >= 0) ImGui::SetTooltip("%s\n(click: copy this piece's motion)", ps[under].name.c_str());
    if (hov && under >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) picked = under;
}

static void CopyMotionSection(Elem& e, Clip* cur) {
    if (g_cmShow) ImGui::SetNextItemOpen(true);
    if (!ImGui::CollapsingHeader("Copy motion from the game")) return;
    MotionSource& ms = motionSource;
    if (!ms.movies || !ms.clips || !ms.sample) { ImGui::TextDisabled("This game doesn't give its movies to the editor."); return; }
    CopyMotionUi& u = g_cm;
    if (!u.listed) { u.movies = ms.movies(); u.listed = true; }
    if (u.movies.empty()) {
        ImGui::TextDisabled("No movies yet: the picture cache keeps them (it's rebuilt once after an update - restart the game).");
        if (ImGui::SmallButton("look again##cm")) u.listed = false;
        return;
    }
    const std::string key = KeyOf(g_editMode, e);
    if (u.forElem != key) {   // another element: its own movie (the one its picture is from), its piece
        u.forElem = key; u.piecesFor.clear();
        const std::string pk = LowerS(e.pack);
        for (auto& m : u.movies) if (LowerS(m) == pk) { if (u.movie != m) { u.movie = m; u.clip = -1; } break; }
    }
    // the movie: a filter and the list
    ImGui::SetNextItemWidth(-60);
    ImGui::InputTextWithHint("##cmfilter", "filter the movies (e.g. jra_menus, medal)", u.filter, sizeof u.filter);
    ImGui::SetNextItemWidth(-60);
    if (ImGui::BeginCombo("movie##cm", u.movie.empty() ? "(pick a movie)" : u.movie.c_str(), ImGuiComboFlags_HeightLarge)) {
        const std::string f = LowerS(u.filter);
        int shown = 0;
        for (auto& m : u.movies) {
            if (!f.empty() && LowerS(m).find(f) == std::string::npos) continue;
            if (++shown > 400) { ImGui::TextDisabled("(more: filter them)"); break; }
            if (ImGui::Selectable(m.c_str(), m == u.movie)) { u.movie = m; u.clip = -1; }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game's Flash movies (%d). The one this element's picture is from is picked for you.", (int)u.movies.size());
    if (u.movie.empty()) return;
    if (u.clipsFor != u.movie) {
        u.clipsFor = u.movie; u.clips.clear(); u.err.clear(); u.piecesFor.clear();
        if (!ms.clips(u.movie, u.clips, u.fps, u.ppu, u.err)) u.clips.clear();
        if (u.clip >= (int)u.clips.size()) u.clip = -1;
    }
    if (!u.err.empty()) { ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", u.err.c_str()); return; }
    if (u.clips.empty()) { ImGui::TextDisabled("It has no clips (no labels or frames to play)."); return; }
    ImGui::SetNextItemWidth(-60);
    if (ImGui::BeginCombo("clip##cm", u.clip >= 0 ? u.clips[u.clip].name.c_str() : "(pick a clip)", ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < (int)u.clips.size(); ++i)
            if (ImGui::Selectable((u.clips[i].name + "##c" + std::to_string(i)).c_str(), i == u.clip)) { u.clip = i; u.piecesFor.clear(); }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A label of the movie (or of a sprite in it) up to the next one: the frames it plays (%g fps).", u.fps);
    if (u.clip < 0) return;
    const std::string pf = u.movie + "#" + std::to_string(u.clip);
    if (u.piecesFor != pf) {   // the clip played: its pieces
        u.piecesFor = pf; u.pieces.clear(); u.piece = -1;
        if (!ms.sample(u.movie, u.clip, u.pieces, u.err)) u.pieces.clear();
        const std::string tex = LowerS(e.tex);   // (the piece with this element's picture, if it's there)
        for (int i = 0; i < (int)u.pieces.size() && u.piece < 0; ++i) if (!tex.empty() && u.pieces[i].image == tex) u.piece = i;
        if (u.piece < 0 && !u.pieces.empty()) u.piece = 0;
    }
    if (!u.err.empty()) { ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", u.err.c_str()); return; }
    if (u.pieces.empty()) { ImGui::TextDisabled("Nothing shows in that clip."); return; }
    ImGui::SetNextItemWidth(-60);
    if (ImGui::BeginCombo("piece##cm", u.pieces[u.piece].name.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < (int)u.pieces.size(); ++i) {
            int on = 0; for (auto& f : u.pieces[i].frames) on += f.on;
            const std::string lb = u.pieces[i].name + (on < (int)u.pieces[i].frames.size() ? "  (on " + std::to_string(on) + " frames)" : "") + "##p" + std::to_string(i);
            if (ImGui::Selectable(lb.c_str(), i == u.piece)) u.piece = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What moves: a picture (by its name - the one this element shows is picked for you), a text field or a shape,\n[its place in the clip: instance names or depths].");
    const char* rests[] = { "auto (Out: its first frame, the others: their last)", "its first frame", "its last frame" };
    ImGui::SetNextItemWidth(-60); ImGui::Combo("rest##cm", &u.rest, rests, 3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The frame where it's where the element is now: the keys are moves, scales and turns from there.\nIn, Focus, Press... end at rest (a pop lands on it); Out starts at rest.");
    const MotionClipInfo& ci = u.clips[u.clip];
    const int n = ci.last - ci.first + 1;
    ImGui::TextDisabled("%d frames, %.0f ms", n, (n - 1) * 1000.f / u.fps);
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNode("preview##cm")) {   // the game's clip itself
        MotionPreviewBox(u.pieces, u.piece, n, u.fps, pf);
        if (g_cmShow) { ImGui::SetScrollHereY(1.f); g_cmShow = false; }
        ImGui::TreePop();
    }
    if (!cur) return;
    const bool has = Keys(*cur) > 0;
    const std::string bl = (has ? "Replace the " : "Copy into the ") + cur->name + " clip";
    if (ImGui::Button(bl.c_str())) {
        const int rest = u.rest == 1 ? 0 : u.rest == 2 ? n - 1 : (cur->name == "Out" ? 0 : n - 1);
        int keys = 0, tracks = 0;
        if (MotionToClip(u.pieces[u.piece], rest, u.ppu, u.fps, *cur, keys, tracks)) {
            g.graph.fit = g.big.fit = true; g.focus = State::F_CLIP; g.tl1 = cur->length * 1.08f; g.tl0 = -cur->length * 0.08f;
            Note("Copied " + std::to_string(keys) + " keys on " + std::to_string(tracks) + " tracks from " + GroupLeaf(u.movie) + ", " + ci.name.substr(0, ci.name.find("  (")) + ".");
        } else Note("That piece never shows in the clip.");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The piece's motion over the clip as eased keys (move, scale, turn, alpha, colour +) on this element's\n%s clip%s.", cur->name.c_str(), has ? " - its keys are replaced" : "");
    {   // ---- every element at once
        ImGui::SeparatorText("Every element at once");
        const int rest = u.rest == 1 ? 0 : u.rest == 2 ? n - 1 : (cur->name == "Out" ? 0 : n - 1);
        const std::string scope = TopGroup(e.group);
        int sc = u.wholeLayout ? 1 : 0;
        const std::string g0 = "its group (" + (scope.empty() ? std::string("no group") : scope) + ")";
        const char* scopes[] = { g0.c_str(), "the whole layout" };
        ImGui::SetNextItemWidth(-60); if (ImGui::Combo("in##cmall", &sc, scopes, 2)) u.wholeLayout = sc == 1;
        const int fi = FocusedIndex();
        const std::vector<MotionPair> pairs = MatchPieces(fi, u.piece, u.pieces, rest, u.ppu, u.wholeLayout);
        int withPic = 0, usedPic = 0;
        for (auto& pc : u.pieces) withPic += !pc.image.empty();
        for (auto& pr : pairs) usedPic += !u.pieces[pr.piece].image.empty();
        ImGui::TextDisabled("%d element%s match the clip's pictures (%d of its %d picture pieces left over).", (int)pairs.size(), pairs.size() == 1 ? "" : "s",
                            withPic - usedPic, withPic);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Elements showing a picture the clip moves, each with the nearest piece of it - placed by the piece picked\nabove and this element (they're the pair that fixes where the clip is on screen). Text and shapes aren't matched.");
        if (ImGui::TreeNode("the pairs##cmall")) {
            for (auto& pr : pairs) ImGui::TextDisabled("%s  <-  %s%s", Elems()[pr.elem].name.c_str(), u.pieces[pr.piece].name.c_str(), pr.dist > 20 ? "  (far: check it)" : "");
            ImGui::TreePop();
        }
        const std::string al = "Copy into all " + std::to_string(pairs.size()) + " (their " + cur->name + " clips)";
        ImGui::BeginDisabled(pairs.empty());
        if (ImGui::Button(al.c_str())) {
            int keys = 0;
            const int done = ApplyPairs(pairs, u.pieces, cur->name, rest, u.ppu, u.fps, keys);
            g.graph.fit = g.big.fit = true; g.focus = State::F_CLIP; g.tl1 = cur->length * 1.08f; g.tl0 = -cur->length * 0.08f;
            Note("Copied into " + std::to_string(done) + " elements' " + cur->name + " clips (" + std::to_string(keys) + " keys) from " + GroupLeaf(u.movie) + ".");
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Each of them gets its piece's motion on its own %s clip (their keys there are replaced).\nOpen \"the pairs\" to see who gets what.", cur->name.c_str());
    }
    if (ImGui::GetTime() - g_noteAt < 4.0) ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.f, 1.f), "%s", g_note.c_str());
}

// ---------------------------------------------------------------- animation page (left of the list)

static void AnimPage() {   // (the left side of the panels window)
    const int idx = FocusedIndex();
    ElemAnim* an = Focused();
    if (idx < 0 || !an) { ImGui::TextDisabled("Pick an element in the list to animate it."); return; }
    Elem& e = Elems()[idx];
    ImGui::Text("%s", e.name.c_str()); ImGui::SameLine(); ImGui::TextDisabled("%s%s%s", TypeKey(e.type), e.group.empty() ? "" : "  in ", e.group.c_str());
    if (e.type == E_MOTION) {   // a group's motion: its point is the pivot
        ImGui::TextColored(ImVec4(1.f, 0.65f, 0.3f, 1), "Group motion");
        ImGui::SameLine(); ImGui::TextDisabled("its clips move every element of \"%s\" about its point (the orange cross)", GroupLeaf(e.group).c_str());
        ImGui::SetNextItemWidth(160);
        if (ImGui::DragFloat2("point##mot", &e.x, 0.5f, -2000.f, 2000.f, "%.1f")) g_dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where the group scales and turns about (units from the screen's middle).");
        ImGui::SameLine();
        if (ImGui::SmallButton("group's middle")) {
            ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f); bool any = false;
            for (auto& o : Elems()) {
                if (!InGroup(o.group, e.group) || o.type == E_MOTION) continue;
                ImVec2 a, b; ElemRect(o, a, b); any = true;
                lo = ImVec2(std::min(lo.x, std::min(a.x, b.x)), std::min(lo.y, std::min(a.y, b.y)));
                hi = ImVec2(std::max(hi.x, std::max(a.x, b.x)), std::max(hi.y, std::max(a.y, b.y)));
            }
            if (any) { ImVec2 a, b; ElemRect(e, a, b); e.x += ((lo.x + hi.x) / 2 - (a.x + b.x) / 2) / Unit(); e.y += ((lo.y + hi.y) / 2 - (a.y + b.y) / 2) / Unit(); e.pivX = e.pivY = 0; g_dirty = true; }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its point to the middle of the group's elements.");
    }
    ImGui::TextDisabled("Saved with the layout. In plays when it shows, then Idle loops.");
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
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays %s", c.trigger.c_str());
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
        ImGui::TextDisabled("Live: In, then Idle. Focus / Unfocus / Selected / Press by the triggers below, over them;");
        ImGui::TextDisabled("Out before it hides. Changes blend. Locked, Value change: planned.");
    }
    // ---- triggers
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Triggers")) {
        char fb[128], pb[128];   // (ImGui keeps its own copy while one's being typed in)
        snprintf(fb, sizeof fb, "%s", an->focusIf.c_str()); snprintf(pb, sizeof pb, "%s", an->pressIf.c_str());
        ImGui::SetNextItemWidth(-60);
        if (ImGui::InputTextWithHint("focus##trig", "a value, e.g. menu.sel:2", fb, sizeof fb)) an->focusIf = fb;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("While this is on it's focused: Focus plays, then Selected loops. When it goes off, Unfocus plays,\nand Unfocus's last frame is how it rests while unfocused. A show-only-while value (! for not).\nMenus REUI drives: menu.sel:<row> (0 is the top row).");
        ImGui::SetNextItemWidth(-60);
        if (ImGui::InputTextWithHint("press##trig", "a value, e.g. menu.press:2", pb, sizeof pb)) an->pressIf = pb;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Press plays each time this comes on (then it goes back to how it rests).\nMenus REUI drives: menu.press:<row> (select on that row), menu.press (any row), menu.back (back).");
        static int row = 1;
        ImGui::SetNextItemWidth(80); ImGui::InputInt("##trow", &row); row = std::max(1, std::min(64, row));
        ImGui::SameLine();
        if (ImGui::Button("Use menu row")) { an->focusIf = "menu.sel:" + std::to_string(row - 1); an->pressIf = "menu.press:" + std::to_string(row - 1); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Focus follows that row of the menu (1: the top one), Press its select.");
        ImGui::SetNextItemWidth(120); ImGui::DragFloat("blend##trig", &an->blend, 2, 0, 2000, "%.0f ms");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A change of clip (Focus, Unfocus, Press, Out...) eases from the pose it had over this long. 0: it jumps.");
        ImGui::TextDisabled("Out plays when its show-only-while goes off; it stays drawn till Out ends.");
        ImGui::TextDisabled("Preview a clip: pick it above (the timeline plays it on every element).");
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
        ImGui::TextDisabled("Rotation: images and shapes. Colour +: images. Scale: about the pivot.");
        if (ImGui::SmallButton("Example: the game's focus pop")) { ExampleFocusPop(*cur); g.graph.fit = g.big.fit = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replaces this clip's keys with the game's button focus (scale 1 -> 1.1 -> 1.02 and a brightness flash).");
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
            if (ImGui::Combo("way to the next key", &e2, kEase, EZ_NEASE)) ApplyEase(*cur, e2);
            Key* bz = nullptr;   // the first picked bezier key's handles
            for (auto& tr : cur->tracks) for (auto& k : tr.keys) if (!bz && k.sel && k.ease == EZ_BEZIER) bz = &k;
            if (bz) {
                ImGui::SetNextItemWidth(220); ImGui::DragFloat2("handle 1 (time 0-1, value +)", &bz->h1x, 0.01f);
                ImGui::SetNextItemWidth(220); ImGui::DragFloat2("handle 2 (time 0-1, value +)", &bz->h2x, 0.01f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Handle 1 leaves this key, handle 2 comes into the next: time as a part of the way,\nvalue added to its key's. Easier: drag them in the graph.");
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
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Timing")) {
        ImGui::SetNextItemWidth(120); ImGui::DragFloat("delay##tm", &p.delay, 5, 0, 5000, "%.0f ms"); p.delay = std::max(0.f, p.delay);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its In waits this long after it shows (held at In's first frame).");
        if (e.type == E_MOTION) {
            ImGui::SetNextItemWidth(120); ImGui::DragFloat("stagger##tm", &p.stagger, 2, 0, 2000, "%.0f ms a step"); p.stagger = std::max(0.f, p.stagger);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its group's members start their In this much apart, top to bottom (a group inside it counts as one member).\nThe game's lists: about 100 ms (3 Flash frames).");
        } else ImGui::TextDisabled("Stagger: on a group's motion element (click the group's heading in the list).");
        const float w = InDelay(e);
        if (w > 0) ImGui::TextDisabled("Its In waits %.0f ms in all (its delay and its groups' stagger).", w);
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
    CopyMotionSection(e, cur);
}

// ---------------------------------------------------------------- the panels window: the animation page | the element
// list, a divider between them (dragged; kept with the windows)

static void SidePanel(ImVec2 pos, ImVec2 size, float defSplit) {
    WinBegin(WIN_SIDE, "Animation##anim", pos, size);
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float u = ImGui::GetFontSize() / 13.f, divW = 8 * u, minW = 90 * u;
    const float split = w_.split > 0 ? w_.split : defSplit;
    const float leftW = std::max(std::min(minW, avail.x * 0.5f), std::min(avail.x * split - divW * 0.5f, avail.x - divW - minW));
    ImGui::BeginChild("##apage", ImVec2(std::max(1.f, leftW), avail.y));
    AnimPage();
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    // the divider: drag it to share the width
    const ImVec2 d0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##asplit", ImVec2(divW, std::max(1.f, avail.y)));
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("Drag to share the width between the page and the list (double-click: the standard split).");
    if (ImGui::IsItemActive() && avail.x > 0) {
        const float left = d0.x - leftW;   // (the page's left edge)
        w_.split = std::max(0.05f, std::min(0.95f, (io.MousePos.x - left) / avail.x));
        w_.dirty = true;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { w_.split = -1; w_.dirty = true; }
    ImGui::GetWindowDrawList()->AddLine(ImVec2(d0.x + divW * 0.5f, d0.y), ImVec2(d0.x + divW * 0.5f, d0.y + avail.y),
                                        hot ? IM_COL32(110, 170, 255, 255) : ImGui::GetColorU32(ImGuiCol_Separator), hot ? 2.f : 1.f);
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##aelems", ImVec2(0, avail.y));
    ElementsList();
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------- graph window (under both panels)

static void GraphPanel(ImVec2 pos, ImVec2 size) {
    WinBegin(WIN_GRAPH, "Keys and curves##anim", pos, size);
    Clip* c = CurClip();
    std::string what = !c ? "-" : g.focus == State::F_TRACK && g.track >= 0 ? std::string(kProp[g.track].name)
                     : g.focus == State::F_PLANNED ? g.planned + " (planned)" : c->name + " clip: all its keyed tracks";
    ImGui::Text("Showing: %s", what.c_str());
    ImGui::SameLine(); if (ImGui::SmallButton("fit")) g.graph.fit = true;
    if (c && g.focus == State::F_TRACK) { ImGui::SameLine(); if (ImGui::SmallButton("all tracks")) { g.focus = State::F_CLIP; g.graph.fit = true; } }
    EaseButtons(c);
    ImGui::SameLine(0, 12); if (ImGui::SmallButton("copy##k")) CopyKeys(false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the picked keys (Ctrl + C; Ctrl + X cuts).");
    ImGui::SameLine(); ImGui::BeginDisabled(g_kclip.keys.empty());
    if (ImGui::SmallButton("paste##k")) PasteKeys();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Paste at the playhead, into this element's clip being edited (Ctrl + V).\nKeys copied from one track go onto the track shown here, if one is.");
    if (ImGui::GetTime() - g_noteAt < 4.0) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.f, 1.f), "%s", g_note.c_str()); }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    Graph("##graph", ImVec2(avail.x, std::max(40.f, avail.y)), g.graph);
    ImGui::End();
}

// ---------------------------------------------------------------- timeline (bottom)

static void Timeline(ImVec2 pos, ImVec2 size) {
    if (g.bare) ImGui::SetNextWindowBgAlpha(0.35f);   // (hidden panels: the timeline is see-through and short)
    WinBegin(WIN_TL, "Timeline##anim", pos, size, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);
    Clip* c = CurClip();
    ElemAnim* an = Focused();
    const float len = PreviewLen();
    // ---- header: transport, time, clip, options. No title bar (room for the rows): it's moved by the grip at its left
    // end or the empty part of this row (its edges size it, as the other windows')
    ImGuiIO& hio = ImGui::GetIO();
    ImDrawList* dl0 = ImGui::GetWindowDrawList();
    auto moveGrip = [&](bool dots) {
        static ImVec2 startWin, startMouse;
        if (ImGui::IsItemActivated()) { startWin = ImGui::GetWindowPos(); startMouse = hio.MousePos; }
        if (ImGui::IsItemActive()) { w_.drag[WIN_TL] = true; w_.dragTo[WIN_TL] = ImVec2(startWin.x + hio.MousePos.x - startMouse.x, startWin.y + hio.MousePos.y - startMouse.y); }
        else if (ImGui::IsItemDeactivated()) w_.drag[WIN_TL] = false;
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("Drag to move the timeline (its edges size it).");
        if (dots) {   // a grip: 2 x 3 dots
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            const ImU32 col = ImGui::IsItemHovered() || ImGui::IsItemActive() ? IM_COL32(220, 220, 230, 255) : IM_COL32(130, 130, 145, 255);
            const float cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f, d = ImGui::GetFontSize() * 0.28f;
            for (int i = -1; i <= 1; ++i) for (int j = 0; j < 2; ++j) dl0->AddCircleFilled(ImVec2(cx + (j ? d : -d) * 0.5f, cy + i * d), 1.6f, col);
        }
    };
    ImGui::InvisibleButton("##tlgrip", ImVec2(ImGui::GetFontSize() * 0.8f, ImGui::GetFrameHeight()));
    moveGrip(true);
    ImGui::SameLine(0, 4);
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
    const float rightX = ImGui::GetWindowContentRegionMax().x - bw - ImGui::CalcTextSize(" Collapse ").x - ImGui::CalcTextSize(" Hide panels ").x - ImGui::CalcTextSize(" Reset windows ").x - ImGui::CalcTextSize(" Live ").x - 90;
    {   // the empty part of the row: a move handle too
        ImGui::SameLine();
        const float gapW = rightX - ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x;
        if (gapW > 8) { ImGui::InvisibleButton("##tlmove", ImVec2(gapW, ImGui::GetFrameHeight())); moveGrip(false); }
    }
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), rightX));
    {   // live: the clips as in the game (triggers, blends, Out), with stand-in menu values
        const bool was = g.live;
        if (was) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.85f, 1.f));
        if (ImGui::Button(" Live ")) { g.live = !g.live; g.playing = false; SimRestart(); }
        if (was) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play the layout as the game does - In, the triggers (Focus, Press...), blends, stagger and Out -\nwith stand-in menu values you set below, instead of one clip at the playhead.");
    }
    ImGui::SameLine();
    if (ImGui::Button(g.bare ? " Panels " : " Hide panels ")) g.bare = !g.bare;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide / show the element list, animation page and graph (H): the whole screen to watch.");
    ImGui::SameLine(); if (ImGui::Button(g.expanded ? " Collapse " : " Expand ")) { g.expanded = !g.expanded; g.big.fit = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A taller timeline with a big keys + curves graph.");
    ImGui::SameLine(); if (ImGui::Button(" Reset windows ")) WinReset();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The windows back where they start. Drag a window by its title bar (the timeline: the grip\nat its left end or the empty part of its top row), size it from its edges; its edges snap to\nthe other windows' and the screen's.");
    ImGui::SameLine(); if (ImGui::Button("Exit animation mode")) { g.on = false; if (g.live) { g.live = false; SimRestart(); } }
    if (g.live) {   // ---- the stand-in menu
        ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.f, 1.f), "Live"); ImGui::SameLine();
        int row = g.simRow + 1;
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputInt("focus row##sim", &row)) g.simRow = std::max(0, std::min(64, row)) - 1;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The menu row that's selected (menu.sel:<row>): 1 is the top row, 0 none. Up / Down arrows.");
        ImGui::SameLine(); if (ImGui::Button("Select##sim")) g.simPressAt = ImGui::GetTime();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A select on the focused row (menu.press, menu.press:<row>): Press plays. Enter.");
        ImGui::SameLine(); if (ImGui::Button("Back##sim")) g.simBackAt = ImGui::GetTime();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back (menu.back). Backspace.");
        ImGui::SameLine(); if (ImGui::Button(g.simShown ? "Leave##sim" : "Show##sim")) g.simShown = !g.simShown;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Leave: the screen goes (Out plays, groups' too). Show: it comes back (In again). Q / E.");
        ImGui::SameLine(); if (ImGui::Button("Restart##sim")) SimRestart();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everything from its In again (with its delay / stagger).");
        ImGui::SameLine(); ImGui::SetNextItemWidth(80);
        {
            const float speeds[] = { 1.f, 0.5f, 0.25f, 0.1f }; const char* names[] = { "100%", "50%", "25%", "10%" };
            int cur = 0; for (int i = 0; i < 4; ++i) if (fabs(g_speed - speeds[i]) < 0.001) cur = i;
            if (ImGui::Combo("speed##sim", &cur, names, 4)) g_speed = speeds[cur];
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow motion, to see a clip or a blend closely (also in the game till you change it back).");
        }
        if (an) { ImGui::SameLine(); ImGui::TextDisabled("%s: %s", Elems()[FocusedIndex()].name.c_str(), StateNow(*an).c_str()); }
    }
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
    dl->AddRectFilled(o, ImVec2(ra.x, rb.y), IM_COL32(34, 36, 42, g.bare ? 90 : 255));
    dl->AddRectFilled(ra, rb, IM_COL32(26, 28, 33, g.bare ? 90 : 255));
    dl->AddRectFilled(ImVec2(X(0), ra.y), ImVec2(X(len), rb.y), IM_COL32(255, 255, 255, 10));
    // ruler
    dl->AddRectFilled(ra, ImVec2(rb.x, ra.y + rulerH), IM_COL32(44, 46, 54, g.bare ? 90 : 255));
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
    if (hov && c && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m.x >= ra.x) {   // a key under it: picked first (as a click)
        if (hitKey >= 0 && !c->tracks[hitRow].keys[hitKey].sel) { for (auto& tr : c->tracks) for (auto& kk : tr.keys) kk.sel = false; c->tracks[hitRow].keys[hitKey].sel = true; }
        ImGui::OpenPopup("##tlkeys");
    }
    if (ImGui::BeginPopup("##tlkeys")) { KeyClipItems(); ImGui::EndPopup(); }
    if (hov && m.x >= ra.x) ImGui::SetTooltip("Click / drag: move the playhead. Drag keys (Shift: pick several), Delete removes. Wheel: zoom, middle drag: pan.\nRight-click / Ctrl + C, X, V: copy, cut, paste keys at the playhead; Ctrl + A picks all shown.");
    if (hov && m.x < ra.x) ImGui::SetTooltip("Click a track: graph it (the top row: the whole clip). Wheel: scroll the rows.");
    // ---- expanded: the big graph under the rows
    if (g.expanded) {
        ImGui::Spacing();
        ImGui::Text("Keys and curves: %s", !c ? "-" : g.focus == State::F_TRACK && g.track >= 0 ? kProp[g.track].name : (c->name + " clip").c_str());
        ImGui::SameLine(); if (ImGui::SmallButton("fit##big")) g.big.fit = true;
        ImGui::PushID("big"); EaseButtons(c); ImGui::PopID();
        ImVec2 av = ImGui::GetContentRegionAvail();
        Graph("##biggraph", ImVec2(av.x, std::max(60.f, av.y)), g.big);
    }
    ImGui::End();
}

// ---------------------------------------------------------------- the workspace

static void Frame() {
    ImGuiIO& io = ImGui::GetIO();
    // playback
    {   // a change to the focused element's clips: the layout has unsaved changes
        static std::string lastKey, lastText;
        const int fi = FocusedIndex();
        std::string text = fi >= 0 ? AnimText(Elems()[fi].an.p) : std::string();
        if (fi >= 0 && g.elemKey == lastKey && text != lastText) g_dirty = true;
        lastKey = fi >= 0 ? g.elemKey : std::string(); lastText.swap(text);
    }
    if (g.playing && !g.live) {
        const float len = PreviewLen();
        g.time += io.DeltaTime * 1000.f;
        if (g.time > len) { if (g.loop) g.time = fmodf(g.time, std::max(1.f, len)); else { g.time = len; g.playing = false; } }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) g.playing = !g.playing;
    if (ImGui::IsKeyPressed(ImGuiKey_H, false) && !io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) g.bare = !g.bare;
    if (!io.WantTextInput && io.KeyCtrl && !g.live) {   // keys: copy / cut / paste / pick all
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) CopyKeys(false);
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) CopyKeys(true);
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteKeys();
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) PickAllShown();
    }
    if (g.live && !io.WantTextInput) {   // the stand-in menu's keys
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) g.simRow = std::max(0, g.simRow - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) g.simRow = std::min(63, g.simRow + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) g.simPressAt = ImGui::GetTime();
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) g.simBackAt = ImGui::GetTime();
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) g.simShown = false;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) g.simShown = true;
    }
    if (g.focus == State::F_NONE && FocusedIndex() < 0 && g_sel >= 0 && g_sel < (int)Elems().size()) {   // start on the editor's pick
        g.elemKey = KeyOf(g_editMode, Elems()[g_sel]); g.focus = State::F_ELEMENT;
    }
    const float W = io.DisplaySize.x, H = io.DisplaySize.y, u = ImGui::GetFontSize() / 13.f;
    // the standard layout (fractions of the screen; Reset windows): the person's own, 2026-11-10 - the timeline along the
    // bottom, the graph above it on the right, the animation page and the element list over that
    const float kTl = 0.11435f, kGraph = 0.24858f, kList = 0.10242f, kPage = 0.17905f;
    const float tlH = g.expanded ? H * 0.5f : g.bare ? std::min(H * 0.3f, 80 * u) : H * kTl;
    {   // the timeline's height follows Expand / Hide panels (it keeps its place, width and bottom edge)
        static int was = -1;
        static float normalH = -1;   // (its height as the person left it, back when Expand / Hide panels are undone)
        const int now = (g.expanded ? 1 : 0) | (g.bare ? 2 : 0);
        if (was >= 0 && now != was && w_.r[WIN_TL].set) {
            WinRect& r = w_.r[WIN_TL];
            const float bottom = r.y + r.h;
            if (was == 0) normalH = r.h;
            r.h = now == 0 && normalH > 0 ? normalH : tlH / H; r.y = std::max(0.f, bottom - r.h);
            w_.force[WIN_TL] = true; w_.dirty = true;
        }
        was = now;
    }
    const bool titleOnly = io.ConfigWindowsMoveFromTitleBarOnly;   // (dragging inside a window - the graph, the rows - isn't a move)
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    if (g.bare) Timeline(ImVec2(0, H - tlH), ImVec2(W, tlH));
    else {
        const float graphH = g.expanded ? 0.f : H * kGraph;
        const float listW = W * kList, pageW = W * kPage;
        const float topH = H - tlH - graphH;
        SidePanel(ImVec2(W - listW - pageW, 0), ImVec2(listW + pageW, topH), kPage / (kPage + kList));
        if (graphH > 0) GraphPanel(ImVec2(W - listW - pageW, topH), ImVec2(listW + pageW, graphH));
        Timeline(ImVec2(0, H - tlH), ImVec2(W, tlH));
    }
    WinFrameEnd();
    io.ConfigWindowsMoveFromTitleBarOnly = titleOnly;
}

} // namespace anim

AnimBox::AnimBox(const AnimBox& o) : p(o.p ? new anim::ElemAnim(*o.p) : nullptr) {}
AnimBox& AnimBox::operator=(const AnimBox& o) { if (this != &o) { delete p; p = o.p ? new anim::ElemAnim(*o.p) : nullptr; } return *this; }
AnimBox& AnimBox::operator=(AnimBox&& o) noexcept { if (this != &o) { delete p; p = o.p; o.p = nullptr; } return *this; }
AnimBox::~AnimBox() { delete p; }

bool AnimModeOn() { return anim::g.on; }
bool ShowIfUsed(const char* prefix) {
    const size_t n = strlen(prefix);
    auto uses = [&](const std::string& c) { const std::string& s = !c.empty() && c[0] == '!' ? c.substr(1) : c; return s.compare(0, n, prefix) == 0; };
    for (auto& l : g_layout) for (auto& e : l) {   // (its show-only-while, or its clips' triggers)
        if (uses(e.showIf)) return true;
        if (e.an.p && (uses(e.an.p->focusIf) || uses(e.an.p->pressIf))) return true;
    }
    return false;
}
void SetAnimMode(bool on) {
    anim::g.on = on;
    if (!on) return;
    anim::g.graph.fit = anim::g.big.fit = true;
    if (Elems().empty() && g_nowMode >= 0 && g_nowMode < NumModes() && !g_layout[g_nowMode].empty()) { g_editMode = g_nowMode; g_sel = -1; }
}
void AnimBare(bool bare) { anim::g.bare = bare; }
void AnimSpeed(float speed) { anim::g_speed = std::max(0.01f, std::min(10.f, speed)); }
void AnimCopyMotionTest(const std::string& spec) {   // (tests) "<element>.<movie clip>.<clip name>": as the page's button
    int ei = -1, mc = -1; char cn[64] = "";
    if (sscanf(spec.c_str(), "%d.%d.%63s", &ei, &mc, cn) != 3 || ei < 0 || ei >= (int)Elems().size()) { Log("anim test: bad copy-motion spec %s", spec.c_str()); return; }
    bool all = false;   // "<...>.all": every element at once (the page's second button)
    { const size_t l = strlen(cn); if (l > 4 && !strcmp(cn + l - 4, ".all")) { cn[l - 4] = 0; all = true; } }
    AnimFocus(ei);
    anim::ElemAnim* a = anim::Focused();
    if (!a || !motionSource.movies) return;
    for (int i = 0; i < (int)a->clips.size(); ++i) if (a->clips[i].name == cn) anim::g.clip = i;
    anim::Clip* c = anim::CurClip();
    Elem& e = Elems()[ei];
    std::string movie, err;
    for (auto& m : motionSource.movies()) if (anim::LowerS(m) == anim::LowerS(e.pack)) movie = m;
    std::vector<MotionClipInfo> clips; float fps = 30, ppu = 1.5f;
    std::vector<MotionPiece> ps;
    if (movie.empty() || !motionSource.clips(movie, clips, fps, ppu, err) || mc >= (int)clips.size() || !motionSource.sample(movie, mc, ps, err) || ps.empty()) {
        Log("anim test: copy motion: no movie / clip for %s (%s) %s", e.name.c_str(), e.pack.c_str(), err.c_str()); return;
    }
    int pi = 0; for (int i = 0; i < (int)ps.size(); ++i) if (ps[i].image == anim::LowerS(e.tex)) { pi = i; break; }
    const int n = clips[mc].last - clips[mc].first + 1;
    int keys = 0, tracks = 0;
    const int rest = c->name == "Out" ? 0 : n - 1;
    std::vector<anim::MotionPair> pairs;
    if (all) pairs = anim::MatchPieces(ei, pi, ps, rest, ppu, false);
    const bool ok = all ? anim::ApplyPairs(pairs, ps, c->name, rest, ppu, fps, keys) > 0 : anim::MotionToClip(ps[pi], rest, ppu, fps, *c, keys, tracks);
    FILE* f = fopen((dataDir + "anim_test.txt").c_str(), "w");   // (also in a file: the game's log may not be flushed)
    auto out = [&](const char* fmt, ...) {
        char line[512]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
        Log("%s", line); if (f) fprintf(f, "%s\n", line);
    };
    out("anim test: copy motion %s / %s / %s -> %s %s: %s, %d keys on %d tracks", movie.c_str(), clips[mc].name.c_str(), ps[pi].name.c_str(),
        e.name.c_str(), c->name.c_str(), ok ? "ok" : "never shows", keys, tracks);
    for (auto& pr : pairs) out("anim test:   pair %s <- %s (%.1f units off)", Elems()[pr.elem].name.c_str(), ps[pr.piece].name.c_str(), pr.dist);
    for (auto& tr : c->tracks) for (auto& k : tr.keys) out("anim test:   %s %g ms %g (%s)", anim::kPropKey[tr.prop], k.t, k.v, anim::kEaseKey[k.ease]);
    if (f) fclose(f);
    anim::g.graph.fit = anim::g.big.fit = true; anim::g.focus = anim::State::F_CLIP;
    // the section on this movie / clip / piece, its preview paused half way (for a screenshot)
    anim::g_cm.listed = false; anim::g_cm.forElem = anim::KeyOf(g_editMode, e); anim::g_cm.movie = movie; anim::g_cm.clipsFor.clear();
    anim::g_cm.clip = mc; anim::g_cm.piecesFor.clear();
    anim::g_mpv.play = false; anim::g_mpv.boundsFor.clear();
    anim::g_cmShow = true;
}
void AnimKeysTest(float at) {   // (tests: the focused element's shown keys copied, pasted at `at` ms)
    anim::PickAllShown(); anim::CopyKeys(false); anim::g.time = at; anim::PasteKeys();
}
void AnimLive(bool live) { if (anim::g.live != live) { anim::g.live = live; anim::SimRestart(); } }
void AnimPlayhead(float ms, bool play) { anim::g.time = std::max(0.f, ms); anim::g.playing = play; anim::g.loop = true; }
void AnimFocus(int index) {
    if (index < 0 || index >= (int)Elems().size()) return;
    anim::g.elemKey = anim::KeyOf(g_editMode, Elems()[index]); g_sel = index; anim::g.focus = anim::State::F_ELEMENT;
}
