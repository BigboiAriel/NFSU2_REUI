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
// ht: its handles' type (bezier) - free (each on its own), auto (smooth through its neighbours, never overshooting),
// aligned (in and out on one line), vector (each pointing at its neighbour key: a corner). Its in handle is the previous
// key's h2.
enum HandleType { HT_FREE, HT_AUTO, HT_ALIGNED, HT_VECTOR, HT_N };
static const char* kHandleType[HT_N] = { "Free", "Auto", "Aligned", "Vector" };
static const char* kHandleKey[HT_N] = { "free", "auto", "aligned", "vector" };
struct Key { float t = 0, v = 0; int ease = EZ_LINEAR; float h1x = 0.33f, h1y = 0.f, h2x = 0.67f, h2y = 0.f; bool sel = false; int ht = HT_FREE; };

enum Prop { P_X, P_Y, P_SX, P_SY, P_ROT, P_ALPHA, P_MR, P_MG, P_MB, P_AR, P_AG, P_AB, P_FX, P_BLUR, P_REVEAL, P_NPROP };
struct PropDef { const char* name; const char* unit; float def, lo, hi; ImU32 col; };
static const PropDef kProp[P_NPROP] = {
    { "Move X", "units", 0, -100, 100, IM_COL32(235, 90, 90, 255) },     { "Move Y", "units", 0, -100, 100, IM_COL32(110, 210, 90, 255) },
    { "Scale X", "x", 1, 0.8f, 1.2f, IM_COL32(240, 160, 60, 255) },      { "Scale Y", "x", 1, 0.8f, 1.2f, IM_COL32(230, 210, 70, 255) },
    { "Rotation", "deg", 0, -15, 15, IM_COL32(90, 170, 250, 255) },      { "Alpha", "0-1", 1, 0, 1, IM_COL32(220, 220, 220, 255) },
    { "Colour x red", "x", 1, 0, 1, IM_COL32(255, 110, 110, 255) },     { "Colour x green", "x", 1, 0, 1, IM_COL32(110, 255, 110, 255) },
    { "Colour x blue", "x", 1, 0, 1, IM_COL32(120, 150, 255, 255) },    { "Colour + red", "0-1", 0, 0, 0.6f, IM_COL32(255, 160, 160, 255) },
    { "Colour + green", "0-1", 0, 0, 0.6f, IM_COL32(160, 255, 160, 255) }, { "Colour + blue", "0-1", 0, 0, 0.6f, IM_COL32(170, 190, 255, 255) },
    { "Effects (glow / shadow)", "x", 1, 0, 2, IM_COL32(250, 200, 120, 255) }, { "Blur", "units", 0, 0, 8, IM_COL32(170, 140, 230, 255) },
    { "Wipe (part shown)", "0-1", 1, 0, 1, IM_COL32(120, 220, 220, 255) },
};

struct Track { int prop = P_X; std::vector<Key> keys; };
// a clip's event (a named time that runs a host command as the clip passes it in the game) or marker (a note: no command)
struct Event { float t = 0; std::string cmd, name; bool marker = false, sel = false; };
// a transition of the state graph's own (aflow=): while `from` plays on the state layer ("*": any), `to` starts when
// `cond` comes on (empty: when `from` ends), easing over `blend` ms
struct Flow { std::string from, to, cond; float blend = 120; };
enum Loop { ONCE, LOOP, PINGPONG };
struct Clip {
    std::string name, trigger;   // trigger: a placeholder description (planned)
    float length = 500; int loop = ONCE; bool on = false;
    Track tracks[P_NPROP];
    std::vector<Event> events;   // events and markers, by time
};
// planned per-element settings (placeholders for now)
struct Planned {
    int pivot = 4; bool followGroup = true;
    float delay = 0, stagger = 0, blendIn = 120; bool outBeforeHide = true, waitPrevOut = false;
    bool glow = false; float glowSize = 8, glowStrength = 1; float glowCol[4] = { 1, 1, 1, 0.8f };
    bool shadow = false; float shadowDx = 3, shadowDy = 3, shadowBlur = 4; float shadowCol[4] = { 0, 0, 0, 0.6f };
    float blur = 0; int blend = 0; int mask = 0;   // (mask: the wipe - 0 none, 1 left -> right ... 5 from the middle out)
    float waitOverlap = 0;                          // waitPrevOut: its In starts this long before the other screen's Out ends
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
    float blendMs = -1;           // ... over this long (-1: its element's blend; a transition of its own sets it)
    float last[P_NPROP] = {}; bool hasLast = false;  // the pose drawn last frame
    bool locked = false; double lockAt = -1;          // Locked: its value last frame, and when it came on (-1: not playing)
    std::string value; bool hasValue = false; double valueAt = -1;   // Value change: the value last frame, when it changed
    double waitUntil = -1;        // "wait for the previous screen's Out": its In starts then (s; -1: no Out seen)
    struct EvLayer { double since = -1e9; float last = -1; };
    std::map<std::string, EvLayer> ev;   // events: each layer's clip time last frame (a new start: from -1)
    std::vector<char> flowWas;    // its own transitions' conditions last frame
    int prevState = -1; double switchAt = -1;   // the state layer's last change (the state graph lights its arrow)
};
struct ElemAnim {
    std::vector<Clip> clips; Planned p;
    std::string focusIf, pressIf;  // triggers: Focus / Unfocus / Selected follow focusIf ("menu.sel:2"), Press plays when pressIf comes on
    std::string lockedIf;          // ... Locked plays (then holds or loops) while this is on, over the focus layer
    std::string valueOf;           // ... Value change plays each time this value changes (on top: relative, as Out)
    std::vector<Flow> flows;       // the state graph's own transitions (after the standard ones)
    float blend = 120;            // ms a change of clip eases over (from the pose it had)
    Run run;
};

static const char* kClips[][2] = {
    { "In", "when it shows" }, { "Idle", "while shown (after In)" },
    { "Out", "before it hides (it stays drawn till Out ends; on top of how it is: scale / alpha multiply, moves add)" },
    { "Focus", "when its focus value comes on" }, { "Unfocus", "when its focus value goes off (its last frame: how it rests unfocused)" },
    { "Press", "when its press value comes on" }, { "Selected", "while focused, after Focus (loop it)" },
    { "Locked", "while its locked value is on (over its focus; once: holds its end, or loops)" },
    { "Value change", "each time its value changes (on top of how it is, as Out: scale / alpha multiply, moves add)" },
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
    Key k; k.t = t; k.v = v;
    const Key* nb = nullptr;   // (a new key: the easing of the one before it - the first: the one after)
    for (auto& o : tr.keys) if (o.t < t) nb = &o;
    if (!nb && !tr.keys.empty()) nb = &tr.keys.front();
    if (nb) { k.ease = nb->ease; if (k.ease == EZ_BEZIER) { k.ht = HT_AUTO; k.h1x = 0.33f; k.h2x = 0.67f; k.h1y = k.h2y = 0; } }
    tr.keys.push_back(k); SortKeys(tr);
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
    bool simLocked = false; int simValue = 0;   // stand-ins for the Locked value and the Value change value (L / V)
    bool dragOn = false; ImVec2 dragFrom; float dragX0 = 0, dragY0 = 0;   // the canvas: a handle being dragged (or G / S / R)
    // the timeline's Screen mode: the dope sheet - a row per group and element, the clip being edited on each
    bool screen = false, keyedOnly = false; char sheetFilter[64] = "";
    std::set<std::string> openRows, shutGroups;   // element rows showing their tracks; groups folded
    int pageTab = 0;              // the panels window's left side: 0 the page, 1 the state graph
    int graphTab = 0; bool normalise = false;   // the graph: 0 values, 1 speed; every track scaled to 0-1
    bool onion = false, onionGroup = false, onionKeys = true, path = false; int onionStep = 4;   // onion skin, motion path
    bool playEvents = false;      // events run as the playhead passes them (playing)
    bool gameInput = false;       // the game gets the keyboard / controller while animating
    std::string layoutName = "Standard";
    std::string focusTrigger;     // (the state graph: a trigger's field to go to on the page)
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
    float d = (e.an.p ? e.an.p->p.delay : 0.f) + (g_tileCtx.on ? g_tileCtx.inDelay : 0.f);   // (a grid tile: the grid's stagger)
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
    p.fx = std::max(0.f, v[P_FX]); p.blur = std::max(0.f, v[P_BLUR]); p.reveal = std::max(0.f, std::min(1.f, v[P_REVEAL]));
    p.dx = v[P_X]; p.dy = v[P_Y]; p.sx = v[P_SX]; p.sy = v[P_SY]; p.rot = v[P_ROT]; p.a = std::max(0.f, v[P_ALPHA]);
    p.mul[0] = v[P_MR]; p.mul[1] = v[P_MG]; p.mul[2] = v[P_MB];
    p.add[0] = std::max(0.f, v[P_AR]); p.add[1] = std::max(0.f, v[P_AG]); p.add[2] = std::max(0.f, v[P_AB]);
}
static void StartBlend(ElemAnim& a, double now, float ms = -1) {   // the next frames ease from the pose it has now
    Run& r = a.run;
    const float len = ms >= 0 ? ms : a.blend;
    if (r.hasLast && len > 0) { for (int p = 0; p < P_NPROP; ++p) r.from[p] = r.last[p]; r.blendAt = now; r.blendMs = ms; }
}
static void Switch(ElemAnim& a, int clip, double now, bool blend = true, float blendMs = -1) {   // the state layer to `clip` (from its start)
    if (blend) StartBlend(a, now, blendMs);
    if (a.run.state != clip) { a.run.prevState = a.run.state; a.run.switchAt = now; }
    a.run.state = clip; a.run.stateAt = now;
}
// any clip by name that has keys (a custom one too)
static int ClipIdxAny(const ElemAnim& a, const std::string& name) {
    for (int i = 0; i < (int)a.clips.size(); ++i) if (a.clips[i].name == name && Keys(a.clips[i])) return i;
    return -1;
}
static void Rest(ElemAnim& a, int clip, double now, bool blend = true) {     // ... held at its end (a once clip's last frame)
    Switch(a, clip, now, blend);
    if (clip >= 0) a.run.stateAt = now - (a.clips[clip].length / 1000.0 + 1) / g_speed;
}
static void Triggers(const Elem& e, ElemAnim& a, double now) {
    Run& r = a.run;
    const int iFocus = ClipIdx(a, "Focus"), iUnf = ClipIdx(a, "Unfocus"), iSel = ClipIdx(a, "Selected"), iPress = ClipIdx(a, "Press");
    bool f = !a.focusIf.empty() && CondOk(a.focusIf, e.group), pr = !a.pressIf.empty() && CondOk(a.pressIf, e.group);
    if (g_tileCtx.on) { f = g_tileCtx.sel; pr = g_tileCtx.press; }   // (a grid tile's piece: its tile selected / accepted)
    auto focusedRest = [&](bool blend) { if (iSel >= 0) Switch(a, iSel, now, blend); else Rest(a, iFocus, now, blend); };
    if (!r.inited) {   // coming up: rests as its focus is (no Focus / Unfocus played, no blend)
        r.inited = true; r.focus = f; r.press = pr; r.blendAt = -1;
        r.locked = g.on && g.live ? g.simLocked : !a.lockedIf.empty() && CondOk(a.lockedIf, e.group);
        if (r.locked) if (const Clip* lc = ClipNamed(&a, "Locked")) r.lockAt = now - (lc->length / 1000.0 + 1) / g_speed;
        if (f) { if (iFocus >= 0) Switch(a, iFocus, now, false); else focusedRest(false); }
        else Rest(a, iUnf, now, false);
        r.flowWas.assign(a.flows.size(), 0);
        for (size_t i = 0; i < a.flows.size(); ++i) r.flowWas[i] = !a.flows[i].cond.empty() && CondOk(a.flows[i].cond, e.group);
        r.prevState = -1; r.switchAt = -1;
        return;
    }
    if (f != r.focus) {
        r.focus = f;
        if (f) { if (iFocus >= 0) Switch(a, iFocus, now); else focusedRest(true); }
        else Switch(a, iUnf, now);
    }
    if (pr && !r.press && iPress >= 0) Switch(a, iPress, now);
    r.press = pr;
    {   // Locked: while its value is on (coming up locked: at rest - its end - with no play); Value change: each change
        const bool lk = g.on && g.live ? g.simLocked : !a.lockedIf.empty() && CondOk(a.lockedIf, e.group);
        if (lk != r.locked) { StartBlend(a, now); r.lockAt = lk ? now : -1; r.locked = lk; }
        std::string v;
        if (g.on && g.live) v = std::to_string(g.simValue);
        else if (!a.valueOf.empty()) v = a.valueOf.find('{') != std::string::npos ? Expand(a.valueOf) : Expand("{" + a.valueOf + "}");
        if (!a.valueOf.empty() || (g.on && g.live)) {
            if (r.hasValue && v != r.value) r.valueAt = now;
            r.value = v; r.hasValue = true;
        }
    }
    auto curName = [&]() { return r.state >= 0 && r.state < (int)a.clips.size() ? a.clips[r.state].name : std::string(); };
    if (r.state >= 0 && r.state < (int)a.clips.size()) {   // a once clip that's done moves on
        const Clip& c = a.clips[r.state];
        if (c.loop == ONCE && MsSince(r.stateAt, now) >= c.length) {
            const Flow* own = nullptr;   // (a transition of its own when it ends: first)
            for (auto& fl : a.flows) if (fl.cond.empty() && fl.from == c.name && ClipIdxAny(a, fl.to) >= 0 && fl.to != c.name) { own = &fl; break; }
            if (own) Switch(a, ClipIdxAny(a, own->to), now, true, own->blend);
            else if (r.state == iFocus && iSel >= 0 && r.focus) Switch(a, iSel, now, false);   // (Selected carries on from Focus)
            else if (r.state == iPress) { if (r.focus) focusedRest(true); else Rest(a, iUnf, now); }
        }
    }
    if (!a.flows.empty()) {   // the state graph's own transitions: their condition coming on, while their clip plays
        r.flowWas.resize(a.flows.size(), 0);
        for (size_t i = 0; i < a.flows.size(); ++i) {
            const Flow& fl = a.flows[i];
            const bool on = !fl.cond.empty() && CondOk(fl.cond, e.group);
            const bool rising = on && !r.flowWas[i];
            r.flowWas[i] = on;
            if (!rising || (fl.from != "*" && fl.from != curName())) continue;
            const int to = ClipIdxAny(a, fl.to);
            if (to >= 0) Switch(a, to, now, true, fl.blend);
        }
    }
}
static void Hidden(ElemAnim& a) { a.run = Run(); }
// menu pages / grid tiles draw copies made afresh each frame: their clip state is kept here, per source and tile
struct CopyState { Run run; double since = -1; bool playWas = false; int frame = -10; };
static std::map<std::pair<const Elem*, int>, CopyState> g_copies;
static void CopyLoad(const Elem& src, int tile, Elem& e) {
    auto it = g_copies.find({ &src, tile });
    const int frame = ImGui::GetFrameCount();
    if (it == g_copies.end() || it->second.frame < frame - 1) {   // (not drawn last frame: up again from the start)
        e.shownSince = -1; if (e.an.p) e.an.p->run = Run();
        if (it != g_copies.end()) g_copies.erase(it);
        return;
    }
    e.shownSince = it->second.since; e.playWas = it->second.playWas;
    if (e.an.p) e.an.p->run = it->second.run;
}
static void CopySave(const Elem& src, int tile, const Elem& e) {
    CopyState& c = g_copies[{ &src, tile }];
    c.since = e.shownSince; c.playWas = e.playWas; c.frame = ImGui::GetFrameCount();
    if (e.an.p) c.run = e.an.p->run;
    if (g_copies.size() > 4096) {   // (sources gone: edits, reloads)
        const int frame = c.frame;
        for (auto i = g_copies.begin(); i != g_copies.end();) if (i->second.frame < frame - 1) i = g_copies.erase(i); else ++i;
    }
}
// the Outs playing now, per top group: when they end (s) and the frame that was seen - "wait for the previous screen's Out"
static std::map<std::string, std::pair<double, int>> g_outEnds;
static double OtherOutsEnd(const Elem& e) {   // the latest end of the Outs of the other top groups (seen this frame or last)
    const std::string top = e.group.substr(0, e.group.find('/'));
    const int frame = ImGui::GetFrameCount();
    double end = -1;
    for (auto& [grp, v] : g_outEnds) if (grp != top && v.second >= frame - 1) end = std::max(end, v.first);
    return end;
}
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
    {   // (its screen's Out: when it ends, for elements waiting on it)
        auto& oe = g_outEnds[e.group.substr(0, e.group.find('/'))];
        const double end = r.outAt + out->length / 1000.0 / g_speed;
        if (oe.second != ImGui::GetFrameCount()) oe = { end, ImGui::GetFrameCount() }; else oe.first = std::max(oe.first, end);
    }
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
// ---- events: a clip's events run (the host's command) as its playhead passes them in the game
static int g_evFired = 0; static std::string g_evLast;   // (tests)
static void RunEvent(const Event& ev) {
    ++g_evFired; g_evLast = ev.cmd;
    if (ev.cmd.empty()) return;
    if (animEvent) animEvent(ev.cmd); else if (menuAction) menuAction(ev.cmd);
}
static bool EventsRun() { return !g.on || (g.live && g.playEvents); }   // (animation mode: Live with "play events" only)
// layer `layer` of r plays c from `since`, `t` ms into it now: the events it passed since last frame
static void PassEvents(Run& r, const char* layer, const Clip& c, double since, float t) {
    if (c.events.empty() || !EventsRun()) return;
    Run::EvLayer& L = r.ev[layer];
    const float len = std::max(1.f, c.length);
    if (L.since != since) { L.since = since; L.last = c.loop == ONCE && t > len + 50 ? t : -1.f; }   // (started at rest: none)
    const float t0 = L.last;
    if (t <= t0) return;
    L.last = t;
    for (auto& ev : c.events) {
        if (ev.marker) continue;
        if (c.loop == ONCE) { if (ev.t > t0 && ev.t <= t && ev.t <= len) RunEvent(ev); continue; }
        const float per = c.loop == PINGPONG ? 2 * len : len;
        const float ts[2] = { ev.t, c.loop == PINGPONG && ev.t < len ? 2 * len - ev.t : -1.f };
        for (float et : ts) {
            if (et < 0) continue;
            const int n0 = std::max(0, (int)floorf((t0 - et) / per) + 1);
            for (int n = n0; n < n0 + 4 && n * per + et <= t; ++n) RunEvent(ev);
        }
    }
}
// ---- onion skin: a ghost of an element being drawn - every clip at another time, tinted and faded, without effects
static struct Ghost { bool on = false; float t = 0; float tint[3] = { 1, 1, 1 }; float alpha = 0.25f; } g_ghost;
static bool Ghosting() { return g_ghost.on; }
static void GhostTint(ImU32& col, Elem& e) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    c.x = c.x * 0.3f + g_ghost.tint[0] * 0.7f; c.y = c.y * 0.3f + g_ghost.tint[1] * 0.7f; c.z = c.z * 0.3f + g_ghost.tint[2] * 0.7f; c.w *= g_ghost.alpha;
    col = ImGui::ColorConvertFloat4ToU32(c);
    for (int i = 0; i < 3; ++i) e.fillCol[i] = e.fillCol[i] * 0.3f + g_ghost.tint[i] * 0.7f;
    e.fillCol[3] *= g_ghost.alpha;
}
static float PreviewTime() { return g_ghost.on ? g_ghost.t : g.time; }
static bool PoseOf(Elem& e, bool editing, Pose& p) {
    ElemAnim* a = e.an.p;
    if (!a || editing) return false;
    if (g.on && !g.live) {   // animation mode: the clip being edited, at the playhead (In: after its wait)
        const Clip* c = ClipNamed(a, PreviewName());
        if (!c) return false;
        Sample(*c, PreviewTime() - (c->name == "In" ? InDelay(e) : 0.f), p); return true;
    }
    Run& r = a->run;
    const double now = ImGui::GetTime();
    const bool leaving = r.outAt >= 0;
    if (!leaving && !r.frozen) Triggers(e, *a, now);   // (frozen while it leaves)
    float v[P_NPROP], s[P_NPROP];
    for (int i = 0; i < P_NPROP; ++i) v[i] = kProp[i].def;
    bool any = false;
    double since = e.shownSince >= 0 ? e.shownSince : now;
    if (a->p.waitPrevOut && !g.live) {   // its In waits for another screen's Out to end (less the overlap)
        const float t0 = MsSince(since, now) - InDelay(e);
        const Clip* in0 = ClipNamed(a, "In");
        if (!in0 || t0 <= in0->length) {   // (not once In is done: an Out seen later doesn't take it back)
            const double end = OtherOutsEnd(e);
            if (end >= 0) r.waitUntil = std::max(r.waitUntil, end - a->p.waitOverlap / 1000.0 / g_speed);
        }
        if (r.waitUntil > since) since = std::min(now, r.waitUntil);   // (held at In's first frame till then)
    }
    const float t = MsSince(since, now) - InDelay(e);   // (In waits its delay / stagger)
    const Clip* in = ClipNamed(a, "In"), *idle = ClipNamed(a, "Idle");
    if (in && (!idle || t < in->length)) { SampleVals(*in, t, v); any = true; }
    else if (idle) { SampleVals(*idle, t - (in ? in->length : 0.f), v); any = true; }
    if (!leaving) {   // (events: the layers' playheads)
        if (in) PassEvents(r, "In", *in, since, t);
        if (idle && t >= (in ? in->length : 0.f)) PassEvents(r, "Idle", *idle, since, t - (in ? in->length : 0.f));
        if (r.state >= 0 && r.state < (int)a->clips.size()) PassEvents(r, "state", a->clips[r.state], r.stateAt, MsSince(r.stateAt, now));
        if (r.locked && r.lockAt >= 0) if (const Clip* lc = ClipNamed(a, "Locked")) PassEvents(r, "Locked", *lc, r.lockAt, MsSince(r.lockAt, now));
        if (r.valueAt >= 0) if (const Clip* vc = ClipNamed(a, "Value change")) PassEvents(r, "Value change", *vc, r.valueAt, MsSince(r.valueAt, now));
    } else if (const Clip* o = ClipNamed(a, "Out")) PassEvents(r, "Out", *o, r.outAt, MsSince(r.outAt, now));
    // a layer: its keyed tracks over what's below - in place of it, or (relative: Out, leaving from wherever it is) on top
    // of it: scale, alpha and colour x multiply, move, rotation and colour + add
    auto over = [&](const Clip& c, double since, bool relative) {
        SampleVals(c, MsSince(since, now), s);
        for (int i = 0; i < P_NPROP; ++i) {
            if (c.tracks[i].keys.empty()) continue;
            const bool mul = i == P_SX || i == P_SY || i == P_ALPHA || i == P_MR || i == P_MG || i == P_MB || i == P_FX || i == P_REVEAL;
            v[i] = !relative ? s[i] : mul ? v[i] * s[i] : v[i] + s[i];
        }
        any = true;
    };
    if (r.state >= 0 && r.state < (int)a->clips.size()) over(a->clips[r.state], r.stateAt, false);
    if (r.locked && r.lockAt >= 0) if (const Clip* lc = ClipNamed(a, "Locked")) over(*lc, r.lockAt, false);
    if (r.valueAt >= 0) if (const Clip* vc = ClipNamed(a, "Value change")) {
        if (vc->loop == ONCE && MsSince(r.valueAt, now) >= vc->length) r.valueAt = -1;
        else over(*vc, r.valueAt, true);
    }
    if (leaving) if (const Clip* o = ClipNamed(a, "Out")) over(*o, r.outAt, true);
    if (r.blendAt >= 0) {
        float u = MsSince(r.blendAt, now) / std::max(1.f, r.blendMs >= 0 ? r.blendMs : a->blend);
        if (u >= 1) r.blendAt = -1;
        else { u = u * u * (3 - 2 * u); for (int i = 0; i < P_NPROP; ++i) v[i] = r.from[i] + (v[i] - r.from[i]) * u; any = true; }
    }
    for (int i = 0; i < P_NPROP; ++i) r.last[i] = v[i];
    r.hasLast = true;
    if (!any) return false;
    ToPose(v, p);
    return true;
}
static bool FxOf(const Elem& e, const Pose* pose, Fx& fx) {   // its effects now (the Effects track scales glow / shadow)
    if (g_ghost.on) return false;   // (onion skin ghosts: none)
    const ElemAnim* a = e.an.p;
    const float m = pose ? pose->fx : 1.f;
    if (a) {
        const Planned& q = a->p;
        fx.shadow = q.shadow && q.shadowCol[3] * m > 0.004f; fx.sdx = q.shadowDx; fx.sdy = q.shadowDy; fx.sblur = q.shadowBlur;
        for (int i = 0; i < 4; ++i) { fx.scol[i] = q.shadowCol[i]; fx.gcol[i] = q.glowCol[i]; }
        fx.scol[3] = std::min(1.f, fx.scol[3] * m);
        fx.glow = q.glow && q.glowStrength * m > 0.004f && q.glowSize > 0.01f; fx.gsize = q.glowSize; fx.gstr = q.glowStrength * m;
        fx.blur = q.blur; fx.wipe = q.mask;
    }
    if (pose) { fx.blur += pose->blur; if (pose->reveal < 0.999f && fx.wipe == 0) fx.wipe = 1; }
    return fx.shadow || fx.glow || fx.blur > 0.01f || (fx.wipe > 0 && (!pose || pose->reveal < 0.999f));
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

static const char* kPropKey[P_NPROP] = { "x", "y", "sx", "sy", "rot", "alpha", "mr", "mg", "mb", "ar", "ag", "ab", "fx", "blur", "reveal" };
static const char* kEaseKey[EZ_NEASE] = { "linear", "in", "out", "inout", "backin", "backout", "elastic", "bounce", "hold", "bezier", "qin", "qout", "qinout" };
static const char* kLoopKey[3] = { "once", "loop", "pingpong" };
static void PutAnim(std::string& o, const ElemAnim& a) {
    if (!a.focusIf.empty()) Pf(o, "afocus=%s\n", a.focusIf.c_str());
    if (!a.pressIf.empty()) Pf(o, "apress=%s\n", a.pressIf.c_str());
    if (a.blend != 120) Pf(o, "ablend=%g\n", a.blend);
    if (a.p.delay > 0) Pf(o, "adelay=%g\n", a.p.delay);
    if (a.p.stagger > 0) Pf(o, "astagger=%g\n", a.p.stagger);
    if (!a.lockedIf.empty()) Pf(o, "alocked=%s\n", a.lockedIf.c_str());
    if (!a.valueOf.empty()) Pf(o, "avalue=%s\n", a.valueOf.c_str());
    if (a.p.waitPrevOut) Pf(o, "await=%g\n", a.p.waitOverlap);
    {   // effects: "afx=shadow dx dy blur r g b a;glow size strength r g b a;blur r;wipe mode"
        std::string fx;
        auto add = [&](const char* f, ...) { char b[256]; va_list ap; va_start(ap, f); vsnprintf(b, sizeof b, f, ap); va_end(ap); if (!fx.empty()) fx += ";"; fx += b; };
        const Planned& q = a.p;
        if (q.shadow) add("shadow %g %g %g %g %g %g %g", q.shadowDx, q.shadowDy, q.shadowBlur, q.shadowCol[0], q.shadowCol[1], q.shadowCol[2], q.shadowCol[3]);
        if (q.glow) add("glow %g %g %g %g %g %g", q.glowSize, q.glowStrength, q.glowCol[0], q.glowCol[1], q.glowCol[2], q.glowCol[3]);
        if (q.blur > 0) add("blur %g", q.blur);
        if (q.mask > 0) add("wipe %d", q.mask);
        if (!fx.empty()) Pf(o, "afx=%s\n", fx.c_str());
    }
    for (auto& c : a.clips) {
        if (!Keys(c)) continue;
        Pf(o, "aclip=%s;%g;%s%s\n", c.name.c_str(), c.length, kLoopKey[std::max(0, std::min(2, c.loop))], c.on ? "" : ";off");
        for (auto& tr : c.tracks) {
            if (tr.keys.empty()) continue;
            Pf(o, "akey=%s;%s", c.name.c_str(), kPropKey[tr.prop]);
            for (auto& k : tr.keys) {
                Pf(o, ";%g %g %s", k.t, k.v, kEaseKey[std::max(0, std::min((int)EZ_NEASE - 1, k.ease))]);
                if (k.ease == EZ_BEZIER) Pf(o, " %g %g %g %g", k.h1x, k.h1y, k.h2x, k.h2y);
                if (k.ht > HT_FREE && k.ht < HT_N) Pf(o, " %s", kHandleKey[k.ht]);
            }
            Pf(o, "\n");
        }
    }
    for (auto& c : a.clips)   // events and markers (a clip without keys keeps them too)
        for (auto& ev : c.events) {
            if (ev.marker) Pf(o, "amark=%s;%g;%s\n", c.name.c_str(), ev.t, ev.name.c_str());
            else if (ev.name.empty()) Pf(o, "aevent=%s;%g;%s\n", c.name.c_str(), ev.t, ev.cmd.c_str());
            else Pf(o, "aevent=%s;%g;%s;%s\n", c.name.c_str(), ev.t, ev.cmd.c_str(), ev.name.c_str());
        }
    for (auto& f : a.flows) Pf(o, "aflow=%s;%s;%s;%g\n", f.from.c_str(), f.to.c_str(), f.cond.c_str(), f.blend);
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
    if (k == "afocus" || k == "apress" || k == "ablend" || k == "adelay" || k == "astagger" || k == "alocked" || k == "avalue" || k == "await" || k == "afx") {
        if (!e.an.p) e.an.p = new ElemAnim(NewAnim());
        ElemAnim& a = *e.an.p;
        const float f = std::max(0.f, (float)atof(v.c_str()));
        if (k == "afocus") a.focusIf = v; else if (k == "apress") a.pressIf = v; else if (k == "ablend") a.blend = f;
        else if (k == "adelay") a.p.delay = f; else if (k == "astagger") a.p.stagger = f;
        else if (k == "alocked") a.lockedIf = v; else if (k == "avalue") a.valueOf = v;
        else if (k == "await") { a.p.waitPrevOut = true; a.p.waitOverlap = f; }
        else for (auto& part : SplitSemi(v)) {
            Planned& q = a.p; char what[16] = "";
            float n[8] = {}; const int got = sscanf(part.c_str(), "%15s %f %f %f %f %f %f %f %f", what, &n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &n[6], &n[7]);
            if (!strcmp(what, "shadow") && got >= 8) { q.shadow = true; q.shadowDx = n[0]; q.shadowDy = n[1]; q.shadowBlur = n[2]; for (int i = 0; i < 4; ++i) q.shadowCol[i] = n[3 + i]; }
            else if (!strcmp(what, "glow") && got >= 7) { q.glow = true; q.glowSize = n[0]; q.glowStrength = n[1]; for (int i = 0; i < 4; ++i) q.glowCol[i] = n[2 + i]; }
            else if (!strcmp(what, "blur") && got >= 2) q.blur = n[0];
            else if (!strcmp(what, "wipe") && got >= 2) q.mask = (int)n[0];
        }
        return;
    }
    std::vector<std::string> f = SplitSemi(v);
    if (f.empty() || f[0].empty()) return;
    if (k == "aflow") {   // from;to;condition;blend
        if (f.size() < 2) return;
        ClipFor(e, f[1]);   // (a custom clip it goes to)
        Flow fl; fl.from = f[0]; fl.to = f[1]; if (f.size() > 2) fl.cond = f[2]; if (f.size() > 3) fl.blend = std::max(0.f, (float)atof(f[3].c_str()));
        e.an.p->flows.push_back(fl);
        return;
    }
    Clip& c = ClipFor(e, f[0]);
    if (k == "aevent" || k == "amark") {   // clip;ms;command[;name]  /  clip;ms;name
        if (f.size() < 3) return;
        Event ev; ev.t = std::max(0.f, (float)atof(f[1].c_str())); ev.marker = k == "amark";
        if (ev.marker) ev.name = f[2]; else { ev.cmd = f[2]; if (f.size() > 3) ev.name = f[3]; }
        c.events.push_back(ev);
        std::stable_sort(c.events.begin(), c.events.end(), [](const Event& x, const Event& y) { return x.t < y.t; });
        return;
    }
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
    for (size_t i = 2; i < f.size(); ++i) {   // "<ms> <value> <ease>[ <h1x> <h1y> <h2x> <h2y>][ <handle type>]"
        Key key; char w[8][24] = {};
        const int n = sscanf(f[i].c_str(), "%23s %23s %23s %23s %23s %23s %23s %23s", w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
        if (n < 2) continue;
        key.t = (float)atof(w[0]); key.v = (float)atof(w[1]);
        if (n > 2) for (int j = 0; j < EZ_NEASE; ++j) if (!strcmp(w[2], kEaseKey[j])) key.ease = j;
        int at = 3;
        if (key.ease == EZ_BEZIER && n >= 7) { key.h1x = (float)atof(w[3]); key.h1y = (float)atof(w[4]); key.h2x = (float)atof(w[5]); key.h2y = (float)atof(w[6]); at = 7; }
        if (n > at) for (int j = 0; j < HT_N; ++j) if (!strcmp(w[at], kHandleKey[j])) key.ht = j;
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
    if (cut) g_dirty = true;
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
    g_dirty = true;   // (an undo step, even when the focus changed this frame)
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

static void HandleTypeItems(Clip* c); static void SetHandleType(Clip& c, int ht);   // (uied_anim_tools.inc)
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
    // the speed view (each track's rate of change, per second; read-only) and the 0-1 view (every track scaled to fit)
    const bool speed = g.graphTab == 1, norm = g.normalise && !speed;
    float nlo[P_NPROP], nhi[P_NPROP];
    for (int p : tracks) {
        float lo = 1e9f, hi = -1e9f; const Track& tr = c->tracks[p];
        for (auto& k : tr.keys) { lo = std::min(lo, k.v); hi = std::max(hi, k.v); }
        for (int i = 0; i <= 60; ++i) { const float vv = Value(tr, c->length * i / 60.f); lo = std::min(lo, vv); hi = std::max(hi, vv); }
        if (hi - lo < 1e-4f) { lo -= 0.5f; hi += 0.5f; }
        nlo[p] = lo; nhi[p] = hi;
    }
    auto N = [&](int p, float val) { return norm ? (val - nlo[p]) / (nhi[p] - nlo[p]) : val; };   // a track's value -> drawn
    auto Dn = [&](int p, float y) { return norm ? nlo[p] + y * (nhi[p] - nlo[p]) : y; };          // drawn -> its value
    auto Rate = [&](const Track& tr, float t) { const float h = 2.f; return (Value(tr, t + h) - Value(tr, t - h)) / (2 * h) * 1000.f; };
    if (v.fit) {
        FitView(v, c, tracks); v.fit = false;
        if (speed || norm) {
            float lo = 1e9f, hi = -1e9f;
            for (int p : tracks) for (int i = 0; i <= 120; ++i) { const float t = c->length * i / 120.f, vv = speed ? Rate(c->tracks[p], t) : N(p, Value(c->tracks[p], t)); lo = std::min(lo, vv); hi = std::max(hi, vv); }
            if (lo > hi) { lo = 0; hi = 1; }
            if (hi - lo < 0.05f) { lo -= 0.5f; hi += 0.5f; }
            v.v0 = lo - (hi - lo) * 0.15f; v.v1 = hi + (hi - lo) * 0.15f;
        }
    }
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
        if ((!hov && g.dragKey < 0) || speed) return;
        const float dx = m.x - q.x, dy = m.y - q.y, d = dx * dx + dy * dy;
        if (fabsf(dx) <= reach && fabsf(dy) <= reach && d < hitD) { hitD = d; hitTrack = p; hitKey = i; hitHandle = h; }
    };
    const bool dragging = g.dragKey >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    for (int p : tracks) {
        Track& tr = c->tracks[p];
        const ImU32 col = kProp[p].col;
        // the curve: sampled every 2 pixels
        std::vector<ImVec2> pts;
        for (float x = a.x; x <= b.x; x += 2) pts.push_back(ImVec2(x, Yv(speed ? Rate(tr, T(x)) : N(p, Value(tr, T(x))))));
        if (pts.size() > 1) dl->AddPolyline(pts.data(), (int)pts.size(), col, 0, tracks.size() == 1 ? 2.f : 1.5f);
        for (int i = 0; i < (int)tr.keys.size(); ++i) {
            Key& k = tr.keys[i];
            ImVec2 kp(X(k.t), Yv(speed ? Rate(tr, k.t) : N(p, k.v)));
            // bezier: the handles of the way to the next key (h1 off this key, h2 into the next), dragged to shape it
            if (k.ease == EZ_BEZIER && i + 1 < (int)tr.keys.size() && !speed) {
                const Key& n = tr.keys[i + 1];
                const ImVec2 np(X(n.t), Yv(N(p, n.v)));
                const ImVec2 h1(X(k.t + k.h1x * (n.t - k.t)), Yv(N(p, k.v + k.h1y)));
                const ImVec2 h2(X(k.t + k.h2x * (n.t - k.t)), Yv(N(p, n.v + k.h2y)));
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
            const float r = speed ? 3.5f : 5;
            ImVec2 q[4] = { ImVec2(kp.x, kp.y - r), ImVec2(kp.x + r, kp.y), ImVec2(kp.x, kp.y + r), ImVec2(kp.x - r, kp.y) };
            dl->AddConvexPolyFilled(q, 4, k.sel ? IM_COL32(255, 200, 60, 255) : IM_COL32(235, 235, 235, 255));
            dl->AddPolyline(q, 4, IM_COL32(0, 0, 0, 200), ImDrawFlags_Closed, 1.f);
            if (k.ht == HT_AUTO || k.ht == HT_ALIGNED || k.ht == HT_VECTOR) {   // (its handle type: a letter by it)
                const char* l = k.ht == HT_AUTO ? "A" : k.ht == HT_ALIGNED ? "L" : "V";
                dl->AddText(ImVec2(kp.x + 6, kp.y - 15), IM_COL32(200, 200, 210, 200), l);
            }
            tryHit(kp, 7, p, i, 0);
        }
    }
    if (hov && !dragging && hitKey >= 0) {   // what a click would grab
        const Track& tr = c->tracks[hitTrack]; const Key& k = tr.keys[hitKey];
        ImVec2 q(X(k.t), Yv(N(hitTrack, k.v)));
        if (hitHandle && hitKey + 1 < (int)tr.keys.size()) {
            const Key& n = tr.keys[hitKey + 1];
            q = hitHandle == 1 ? ImVec2(X(k.t + k.h1x * (n.t - k.t)), Yv(N(hitTrack, k.v + k.h1y))) : ImVec2(X(k.t + k.h2x * (n.t - k.t)), Yv(N(hitTrack, n.v + k.h2y)));
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
            char t[96];
            if (speed) snprintf(t, sizeof t, "%s  %.3f %s/s", kProp[p].name, Rate(c->tracks[p], g.time), kProp[p].unit);
            else snprintf(t, sizeof t, "%s  %.3f", kProp[p].name, Value(c->tracks[p], g.time));
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
    if (hov && ImGui::IsKeyPressed(ImGuiKey_F) && !io.WantTextInput) {
        bool framed = false;
        if (io.KeyShift) {   // the picked keys only
            float t0 = 1e9f, t1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
            for (int p : tracks) for (auto& k : c->tracks[p].keys) if (k.sel) { t0 = std::min(t0, k.t); t1 = std::max(t1, k.t); const float y = N(p, k.v); v0 = std::min(v0, y); v1 = std::max(v1, y); }
            if (t0 <= t1) {
                if (t1 - t0 < 50) { t0 -= 50; t1 += 50; } if (v1 - v0 < 0.05f) { v0 -= 0.25f; v1 += 0.25f; }
                const float pt = (t1 - t0) * 0.15f, pv = (v1 - v0) * 0.2f;
                v.t0 = t0 - pt; v.t1 = t1 + pt; if (!speed) { v.v0 = v0 - pv; v.v1 = v1 + pv; } framed = true;
            }
        }
        if (!framed) v.fit = true;
    }
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
    if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hitKey < 0 && !speed) {
        const int p = !tracks.empty() ? (tracks.size() == 1 ? tracks[0] : -1) : (g.focus == State::F_TRACK ? g.track : -1);
        if (p >= 0) { SetKey(c->tracks[p], std::max(0.f, Snap(T(m.x))), norm ? Dn(p, V(m.y)) : V(m.y)); c->on = true; }
    }
    if (g.dragKey >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        Track& tr = c->tracks[g.dragTrack];
        if (g.dragKey < (int)tr.keys.size()) {
            Key& k = tr.keys[g.dragKey];
            if (g.dragHandle == 0) {
                const float nt = std::max(0.f, Snap(g.dragT + (m.x - g.dragStart.x) / size.x * (v.t1 - v.t0)));
                float nvd = N(g.dragTrack, g.dragV) - (m.y - g.dragStart.y) / size.y * (v.v1 - v.v0);   // (drawn units)
                if (io.KeyCtrl) { const float st = NiceStep(v.v1 - v.v0, size.y, 32) / 10; nvd = roundf(nvd / st) * st; }   // (Ctrl: round values)
                const float dt = nt - k.t, dvd = (io.KeyShift ? 0.f : nvd - N(g.dragTrack, k.v));
                for (int p2 = 0; p2 < P_NPROP; ++p2) for (auto& kk : c->tracks[p2].keys) if (kk.sel) {
                    kk.t = std::max(0.f, kk.t + dt);
                    kk.v += norm && std::find(tracks.begin(), tracks.end(), p2) != tracks.end() ? dvd * (nhi[p2] - nlo[p2]) : dvd;
                }
            } else if (g.dragKey + 1 < (int)tr.keys.size()) {
                // (time: kept inside the way, so the curve never turns back on itself; value: free; Shift: value only)
                const int p = g.dragTrack, i = g.dragKey;
                Key& n = tr.keys[i + 1];
                const float u = std::max(0.f, std::min(1.f, (T(m.x) - k.t) / std::max(0.001f, n.t - k.t)));
                if (g.dragHandle == 1) { if (!io.KeyShift) k.h1x = u; k.h1y = Dn(p, V(m.y)) - k.v; }
                else { if (!io.KeyShift) k.h2x = u; k.h2y = Dn(p, V(m.y)) - n.v; }
                // the key the handle belongs to: auto -> aligned, vector -> free; aligned turns its other handle to match
                const int owner = g.dragHandle == 1 ? i : i + 1;
                Key& ok = tr.keys[owner];
                if (ok.ht == HT_AUTO) ok.ht = HT_ALIGNED; else if (ok.ht == HT_VECTOR) ok.ht = HT_FREE;
                if (ok.ht == HT_ALIGNED) {
                    auto& K = tr.keys;
                    const ImVec2 kp(X(ok.t), Yv(N(p, ok.v)));
                    auto outPx = [&](int j) { const float sp = K[j + 1].t - K[j].t; return ImVec2(X(K[j].t + K[j].h1x * sp) - X(K[j].t), Yv(N(p, K[j].v + K[j].h1y)) - Yv(N(p, K[j].v))); };
                    auto inPx = [&](int j) { const float sp = K[j].t - K[j - 1].t; return ImVec2(X(K[j - 1].t + K[j - 1].h2x * sp) - X(K[j].t), Yv(N(p, K[j].v + K[j - 1].h2y)) - Yv(N(p, K[j].v))); };
                    const bool hasIn = owner > 0 && K[owner - 1].ease == EZ_BEZIER, hasOut = owner + 1 < (int)K.size() && K[owner].ease == EZ_BEZIER;
                    if (hasIn && hasOut) {
                        const ImVec2 dragged = g.dragHandle == 1 ? outPx(owner) : inPx(owner), other = g.dragHandle == 1 ? inPx(owner) : outPx(owner);
                        const float ld = sqrtf(dragged.x * dragged.x + dragged.y * dragged.y), lo = sqrtf(other.x * other.x + other.y * other.y);
                        if (ld > 0.5f) {
                            const ImVec2 want(kp.x - dragged.x / ld * lo, kp.y - dragged.y / ld * lo);   // (opposite, its own length)
                            if (g.dragHandle == 1) {   // the in handle: the previous key's h2
                                Key& pk = K[owner - 1]; const float sp = std::max(0.001f, ok.t - pk.t);
                                pk.h2x = std::max(0.f, std::min(1.f, (T(want.x) - pk.t) / sp)); pk.h2y = Dn(p, V(want.y)) - ok.v;
                            } else {
                                const float sp = std::max(0.001f, K[owner + 1].t - ok.t);
                                ok.h1x = std::max(0.f, std::min(1.f, (T(want.x) - ok.t) / sp)); ok.h1y = Dn(p, V(want.y)) - ok.v;
                            }
                        }
                    }
                }
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
        HandleTypeItems(c);
        ImGui::Separator();
        KeyClipItems();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete key")) for (auto& tr : c->tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end());
        ImGui::EndPopup();
    }
    if (hov && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X)) && !io.WantTextInput)
        for (auto& tr : c->tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end());
    if (hov && speed) ImGui::SetTooltip("Speed: each track's rate of change (per second) - a jump at a key is a jerk. Read-only: edit in Values.");
    else if (hov) ImGui::SetTooltip("Drag keys (Shift: time only, Ctrl: round values). Round dots: bezier handles - drag to shape the curve (Shift: height only).\nHandles: Auto (A) smooth, Aligned (L) in line, Free, Vector (V) a corner - the buttons above, V, or right-click a key. F / Shift + F: fit all / the picked.\nThe buttons above: linear / constant / bezier for the picked keys. Right-click a key: every easing, delete.\nCtrl + C / X / V: copy / cut / paste the picked keys (at the playhead), Ctrl + A: pick all shown.\nDouble-click: add a key (one track shown). Click: move the playhead. Wheel: zoom time, Ctrl+wheel: values,\nmiddle / Alt drag: pan, F: fit.");
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
struct NamedLayout { WinRect r[WIN_N]; float split = -1; bool screen = false; };
static std::map<std::string, NamedLayout> g_layouts;   // the person's own window layouts (anim_windows.txt "layout" lines)
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
        if (!strcmp(key, "layout")) {   // "layout <window> x y w h <name>" / "layout split <x> <name>" / "layout screen <0|1> <name>"
            char wk[32] = "", nm[64] = ""; float v[4] = {};
            if (sscanf(line, "layout %31s %f %f %f %f %63[^\n]", wk, &v[0], &v[1], &v[2], &v[3], nm) == 6) { for (int i = 0; i < WIN_N; ++i) if (!strcmp(wk, kWinKey[i])) g_layouts[nm].r[i] = { v[0], v[1], v[2], v[3], true }; }
            else if (sscanf(line, "layout %31s %f %63[^\n]", wk, &v[0], nm) == 3) { if (!strcmp(wk, "split")) g_layouts[nm].split = v[0]; else if (!strcmp(wk, "screen")) g_layouts[nm].screen = v[0] != 0; }
            continue;
        }
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
    for (auto& [nm, l] : g_layouts) {
        for (int i = 0; i < WIN_N; ++i) if (l.r[i].set) fprintf(f, "layout %s %.5f %.5f %.5f %.5f %s\n", kWinKey[i], l.r[i].x, l.r[i].y, l.r[i].w, l.r[i].h, nm.c_str());
        fprintf(f, "layout split %.5f %s\nlayout screen %d %s\n", l.split, nm.c_str(), l.screen ? 1 : 0, nm.c_str());
    }
    fclose(f);
}
static void WinReset() {   // back to the standard layout (Frame's defaults)
    for (auto& r : w_.r) r.set = false;
    for (bool& f : w_.force) f = true;
    w_.split = -1;
    w_.dirty = true;   // (rewritten: the named layouts stay)
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
static struct MotionPreview { float frame = 0; bool play = true; std::string boundsFor; ImVec2 lo, hi; int startAt = -1; } g_mpv;   // (startAt: tests)
static void PreviewCorners(const MotionPiece& pc, const MotionFrame& f, ImVec2 c[4], ImVec2 o, float k) {   // (box corners on screen)
    const float r = f.rot * 3.14159265f / 180, cs = cosf(r), sn = sinf(r);
    const float hw = std::max(2.f, pc.w) * f.sx / 2, hh = std::max(2.f, pc.h) * f.sy / 2;
    const float xs[4] = { -hw, hw, hw, -hw }, ys[4] = { -hh, -hh, hh, hh };
    for (int i = 0; i < 4; ++i) c[i] = ImVec2(o.x + (f.x + xs[i] * cs - ys[i] * sn) * k, o.y + (f.y + xs[i] * sn + ys[i] * cs) * k);
}
static void MotionPreviewBox(std::vector<MotionPiece>& ps, int& picked, int frames, float fps, const std::string& key, MotionPreview& v = g_mpv,
                             const char* clickHint = "copy this piece's motion") {
    if (v.boundsFor != key) {   // the clip's extent over all its frames (the view fits it)
        v.boundsFor = key; v.frame = v.startAt >= 0 ? (float)std::min(v.startAt, frames - 1) : g_cmShow ? frames / 2.f : 0; v.startAt = -1; v.lo = ImVec2(1e9f, 1e9f); v.hi = ImVec2(-1e9f, -1e9f);
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
    if (hov && under >= 0) ImGui::SetTooltip("%s\n(click: %s)", ps[under].name.c_str(), clickHint);
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
    if (g_cmShow) ImGui::SetKeyboardFocusHere();   // (tests: typing goes into the filter, scrolled to)
    ImGui::InputTextWithHint("##cmfilter", "filter the movies (e.g. jra_menus, medal)", u.filter, sizeof u.filter);
    if (g_cmShow) ImGui::SetScrollHereY(0.f);
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
        g_cmShow = false;
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

// ---------------------------------------------------------------- the editor's "From the game" spot (next to Prefabs)
// A movie's clip played in a box, as in copy motion; its pictures added to the layout as image elements, placed, sized,
// turned and faded as they are on the clip's rest frame (a movie's own timeline: where the game shows them; a sprite's:
// about the screen's middle), optionally each with its motion over the clip as one of its clips (In...).
static int RestOf(const MotionPiece& pc, int rest) {   // the frame its rest values come from: the nearest it's on (-1: never)
    const int n = (int)pc.frames.size();
    if (!n) return -1;
    const int r = std::max(0, std::min(rest, n - 1));
    for (int d = 0; d < n; ++d) { if (r + d < n && pc.frames[r + d].on) return r + d; if (r - d >= 0 && pc.frames[r - d].on) return r - d; }
    return -1;
}
static std::string PicTexName(const std::string& image, const std::string& movie, std::string& pack) {   // the texture list's own spelling
    int any = -1;
    for (size_t i = 0; i < g_tex.size(); ++i) {
        if (LowerS(g_tex[i].name) != image) continue;
        if (LowerS(g_tex[i].pack) == LowerS(movie)) { pack = g_tex[i].pack; return g_tex[i].name; }
        if (any < 0) any = (int)i;
    }
    if (any >= 0) { pack = g_tex[any].pack; return g_tex[any].name; }
    pack = movie; return image;
}
static struct FromGameUi {
    std::vector<std::string> movies; bool listed = false;
    std::string movie; char filter[64] = "";
    std::vector<MotionClipInfo> clips; float fps = 30, ppu = 1.5f; std::string clipsFor;
    int clip = -1; std::vector<MotionPiece> pieces; std::string piecesFor;
    int piece = -1;
    int place = 0;                // 0 auto (its clip below: Out its first frame, the rest their last), 1 first, 2 last, 3 the frame shown
    bool motion = true; int motionClip = 0;   // each added element gets its piece's motion as this clip (kClips)
    bool whereShown = false;      // (test hook: scrolled to)
    bool scrollList = false;      // the movie list scrolled to the picked movie (it was picked from outside the list)
    std::string err, msg; double msgAt = -10;
    MotionPreview pv;
} g_fg;
static int FgRest(const FromGameUi& u, int n) {
    const bool out = u.motion && !strcmp(kClips[u.motionClip][0], "Out");
    return u.place == 1 ? 0 : u.place == 2 ? n - 1 : u.place == 3 ? std::max(0, std::min(n - 1, (int)u.pv.frame)) : (out ? 0 : n - 1);
}
// which: piece indices (all of them: the clip's pictures). Returns elements added; into a new top-level group named for
// the clip (one piece: into the group picked in the list, if any)
static int AddFromGame(FromGameUi& u, const std::vector<int>& which, bool one) {
    if (u.clip < 0 || u.clip >= (int)u.clips.size()) return 0;
    const MotionClipInfo& ci = u.clips[u.clip];
    const int n = ci.last - ci.first + 1, rest = FgRest(u, n);
    // where the clip's pixels go: a movie's timeline - the stage's middle on the screen's (the game fits its height);
    // a sprite's - the middle of what's added
    float ox = ci.stageW / 2, oy = ci.stageH / 2;
    if (!ci.root || ci.stageH <= 0) {
        float lx = 1e9f, ly = 1e9f, hx = -1e9f, hy = -1e9f;
        for (int i : which) { const int r = RestOf(u.pieces[i], rest); if (r < 0 || u.pieces[i].image.empty()) continue; const MotionFrame& f = u.pieces[i].frames[r];
            lx = std::min(lx, f.x); ly = std::min(ly, f.y); hx = std::max(hx, f.x); hy = std::max(hy, f.y); }
        if (lx > hx) return 0;
        ox = (lx + hx) / 2; oy = (ly + hy) / 2;
    }
    std::string clipShort = ci.name.substr(0, ci.name.find("  ("));   // ("the movie: in" -> "in", "sprite 30 (mcTagSmall): animateIn" -> "sprite 30 mcTagSmall animateIn")
    if (clipShort.compare(0, 11, "the movie: ") == 0) clipShort = clipShort.substr(11);
    { std::string t; for (char c : clipShort) { if (c == ':' || c == '/' || c == '(' || c == ')') c = ' '; if (c != ' ' || (!t.empty() && t.back() != ' ')) t += c; } clipShort = t; }
    std::string top;
    if (one && !g_selGroup.empty()) top = g_selGroup;
    else {
        std::string base = IntoMenu(GroupLeaf(u.movie) + " " + clipShort);
        while (!base.empty() && base.back() == ' ') base.pop_back();
        top = base;
        auto taken = [&](const std::string& g) { for (auto& e : Elems()) if (e.group == g || InGroup(e.group, g)) return true; return false; };
        for (int i = 2; taken(top); ++i) top = base + " " + std::to_string(i);
    }
    const size_t first = Elems().size();
    std::vector<int> from;
    for (int i : which) {
        const MotionPiece& pc = u.pieces[i];
        const int r = RestOf(pc, rest);
        if (r < 0 || pc.image.empty()) continue;
        const MotionFrame& f = pc.frames[r];
        Elem e; e.type = E_IMAGE; e.anchor = 4; e.mode = g_editMode; e.group = top;
        e.tex = PicTexName(pc.image, u.movie, e.pack); e.name = e.tex;
        e.x = roundf((f.x - ox) / u.ppu * 100) / 100; e.y = roundf((f.y - oy) / u.ppu * 100) / 100;
        e.w = std::max(1.f, roundf(std::max(1.f, pc.w) * f.sx / u.ppu * 100) / 100); e.h = std::max(1.f, roundf(std::max(1.f, pc.h) * f.sy / u.ppu * 100) / 100);
        e.rot = roundf(f.rot * 100) / 100;
        e.col[3] = std::max(0.f, std::min(1.f, f.a));
        e.texIdx = e.fontIdx = -2; e.shownSince = -1;
        Elems().push_back(e); from.push_back(i);
    }
    int keys = 0;
    if (u.motion) for (size_t k = 0; k < from.size(); ++k) {
        int kk = 0, t = 0;
        Clip& c = ClipFor(Elems()[first + k], kClips[u.motionClip][0]);
        if (MotionToClip(u.pieces[from[k]], rest, u.ppu, u.fps, c, kk, t)) keys += kk;
    }
    if (from.empty()) return 0;
    ClearMulti(); g_dirty = true;
    if (one) { g_sel = (int)first; g_selGroup.clear(); } else { g_selGroup = top; g_sel = -1; }
    u.msg = "added " + std::to_string(from.size()) + (from.size() == 1 ? " picture" : " pictures") + " as " + top
          + (u.motion ? " (" + std::to_string(keys) + " keys on their " + kClips[u.motionClip][0] + " clips)" : "");
    u.msgAt = ImGui::GetTime();
    Log("ui editor: from the game: %s (%s, %s, rest frame %d)", u.msg.c_str(), u.movie.c_str(), ci.name.c_str(), rest + 1);
    return (int)from.size();
}
static void FromGameSection() {
    FromGameUi& u = g_fg;
    if (u.whereShown) ImGui::SetNextItemOpen(true);
    if (!ImGui::CollapsingHeader("From the game")) return;
    if (u.whereShown) { ImGui::SetScrollHereY(0.f); u.whereShown = false; }
    MotionSource& ms = motionSource;
    if (!ms.movies || !ms.clips || !ms.sample) { ImGui::TextDisabled("This game doesn't give its movies to the editor."); return; }
    if (!u.listed) { u.movies = ms.movies(); u.listed = true; }
    if (u.movies.empty()) {
        ImGui::TextDisabled("No movies yet: the picture cache keeps them (it's rebuilt once after an update - restart the game).");
        if (ImGui::SmallButton("look again##fg")) u.listed = false;
        return;
    }
    if (u.movie.empty() && g_sel >= 0 && g_sel < (int)Elems().size()) {   // (to start: the movie the picked element's picture is from)
        const std::string pk = LowerS(Elems()[g_sel].pack);
        for (auto& m : u.movies) if (LowerS(m) == pk) { u.movie = m; u.scrollList = true; break; }
    }
    ImGui::PushID("fromgame");
    ImGui::TextDisabled("The game's Flash screens: preview a clip, then add its pictures where the game puts them.");
    const float h = 330;
    ImGui::BeginGroup();   // left: the movies, filtered
    ImGui::SetNextItemWidth(230);
    ImGui::InputTextWithHint("##fgfilter", "filter (e.g. medal, jra_menus)", u.filter, sizeof u.filter);
    ImGui::BeginChild("##fglist", ImVec2(230, h - ImGui::GetFrameHeightWithSpacing()), REUI_CHILD_BORDERS);
    {
        const std::string f = LowerS(u.filter);
        std::vector<int> rows;
        for (int i = 0; i < (int)u.movies.size(); ++i) if (f.empty() || LowerS(u.movies[i]).find(f) != std::string::npos) rows.push_back(i);
        const float rh = ImGui::GetTextLineHeightWithSpacing();
        if (u.scrollList) {   // (picked from outside the list: scrolled to it)
            for (size_t r = 0; r < rows.size(); ++r) if (u.movies[rows[r]] == u.movie) { ImGui::SetScrollY(std::max(0.f, r * rh - ImGui::GetWindowHeight() / 2)); break; }
            u.scrollList = false;
        }
        ImGuiListClipper clip; clip.Begin((int)rows.size(), rh);
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const std::string& m = u.movies[rows[r]];
                const std::string dir = m.find('/') != std::string::npos ? GroupLeaf(m.substr(0, m.rfind('/'))) : "";
                if (ImGui::Selectable((GroupLeaf(m) + "##" + m).c_str(), m == u.movie)) { u.movie = m; u.clip = -1; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.c_str());
                if (!dir.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", dir.c_str()); }
            }
        if (rows.empty()) ImGui::TextDisabled("(none match)");
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();   // right: the clip, its preview, Add
    if (u.movie.empty()) ImGui::TextDisabled("Pick a movie to see its clips here (%d movies).", (int)u.movies.size());
    else {
        if (u.clipsFor != u.movie) {
            u.clipsFor = u.movie; u.clips.clear(); u.err.clear(); u.piecesFor.clear();
            if (!ms.clips(u.movie, u.clips, u.fps, u.ppu, u.err)) u.clips.clear();
            if (u.clip >= (int)u.clips.size()) u.clip = -1;
            if (u.clip < 0 && !u.clips.empty()) u.clip = 0;   // (the movie's first clip, to see something at once)
        }
        if (!u.err.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", u.err.c_str());
        else if (u.clips.empty()) ImGui::TextDisabled("It has no clips (no labels or frames to play).");
        else {
            ImGui::SetNextItemWidth(-60);
            if (ImGui::BeginCombo("clip##fg", u.clips[u.clip].name.c_str(), ImGuiComboFlags_HeightLarge)) {
                for (int i = 0; i < (int)u.clips.size(); ++i)
                    if (ImGui::Selectable((u.clips[i].name + "##c" + std::to_string(i)).c_str(), i == u.clip)) { u.clip = i; u.piecesFor.clear(); }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A label of the movie (or of a sprite in it) up to the next one: the frames it plays.\nThe movie's own labels place pictures where the game shows them; a sprite's go about the screen's middle.");
            const std::string pf = u.movie + "#" + std::to_string(u.clip);
            if (u.piecesFor != pf) {
                u.piecesFor = pf; u.pieces.clear(); u.piece = -1;
                if (!ms.sample(u.movie, u.clip, u.pieces, u.err)) u.pieces.clear();
                for (int i = 0; i < (int)u.pieces.size() && u.piece < 0; ++i) if (!u.pieces[i].image.empty()) u.piece = i;
            }
            const MotionClipInfo& ci = u.clips[u.clip];
            const int n = ci.last - ci.first + 1;
            if (!u.err.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", u.err.c_str());
            else if (u.pieces.empty()) ImGui::TextDisabled("Nothing shows in that clip.");
            else {
                int pics = 0; for (auto& pc : u.pieces) pics += !pc.image.empty() && RestOf(pc, 0) >= 0;
                ImGui::TextDisabled("%d frames, %.0f ms - %d pictures (%d other pieces: text and shapes aren't added)", n, (n - 1) * 1000.f / u.fps, pics, (int)u.pieces.size() - pics);
                MotionPreviewBox(u.pieces, u.piece, n, u.fps, "fg:" + pf, u.pv, "pick this picture to add on its own");
                const char* places[] = { "auto (an Out clip: its first frame, else its last)", "its first frame", "its last frame", "the frame shown" };
                ImGui::SetNextItemWidth(-60); ImGui::Combo("place##fg", &u.place, places, 4);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("The frame the pictures are placed as (where, how big, turned, faded). In, Focus, Press... end at rest; Out starts there.\n\"The frame shown\": pause the preview where you want it.");
                ImGui::Checkbox("with their motion as##fg", &u.motion);
                ImGui::SameLine(); ImGui::SetNextItemWidth(110);
                ImGui::BeginDisabled(!u.motion);
                if (ImGui::BeginCombo("##fgclip", kClips[u.motionClip][0])) {
                    for (int i = 0; i < 7; ++i) if (ImGui::Selectable(kClips[i][0], i == u.motionClip)) u.motionClip = i;   // (not the planned ones)
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Each picture added gets its motion over the clip as eased keys on this clip (as \"Copy motion from the game\" makes them).");
                std::vector<int> all; for (int i = 0; i < (int)u.pieces.size(); ++i) if (!u.pieces[i].image.empty()) all.push_back(i);
                ImGui::BeginDisabled(!pics);
                if (ImGui::Button(("Add all " + std::to_string(pics) + " pictures").c_str())) AddFromGame(u, all, false);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("As a new group (\"%s %s\"), in this game mode (%s).", GroupLeaf(u.movie).c_str(), ci.name.substr(0, ci.name.find("  (")).c_str(), ModeName(g_editMode));
                const bool pickPic = u.piece >= 0 && u.piece < (int)u.pieces.size() && !u.pieces[u.piece].image.empty();
                ImGui::SameLine();
                ImGui::BeginDisabled(!pickPic);
                if (ImGui::Button("Add the picked one")) AddFromGame(u, { u.piece }, true);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(pickPic ? "%s\n(click a picture in the preview to pick it)%s" : "Click a picture in the preview to pick it.%s%s",
                                      pickPic ? u.pieces[u.piece].name.c_str() : "", g_selGroup.empty() ? "" : ("\nInto the group picked in the list: " + g_selGroup).c_str());
            }
        }
    }
    ImGui::EndGroup();
    if (ImGui::GetTime() - u.msgAt < 6.0) ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.f, 1.f), "%s", u.msg.c_str());
    ImGui::PopID();
}

// ---------------------------------------------------------------- animation page (left of the list)

#include "uied_anim_tools.inc"

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
        CustomClipButton(*an);
        ImGui::TextDisabled("Live: In, then Idle. Focus / Unfocus / Selected / Press by the triggers below, over them;");
        ImGui::TextDisabled("Locked and Value change on top; Out before it hides. Changes blend. Custom clips: the state graph's arrows.");
    }
    // ---- triggers
    if (!g.focusTrigger.empty()) ImGui::SetNextItemOpen(true); else ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Triggers")) {
        auto goHere = [](const char* which) { if (g.focusTrigger == which) { ImGui::SetKeyboardFocusHere(); ImGui::SetScrollHereY(0.3f); g.focusTrigger.clear(); } };
        char fb[128], pb[128];   // (ImGui keeps its own copy while one's being typed in)
        snprintf(fb, sizeof fb, "%s", an->focusIf.c_str()); snprintf(pb, sizeof pb, "%s", an->pressIf.c_str());
        ImGui::SetNextItemWidth(-60); goHere("focus");
        if (ImGui::InputTextWithHint("focus##trig", "a value, e.g. menu.sel:2", fb, sizeof fb)) an->focusIf = fb;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("While this is on it's focused: Focus plays, then Selected loops. When it goes off, Unfocus plays,\nand Unfocus's last frame is how it rests while unfocused. A show-only-while value (! for not).\nMenus REUI drives: menu.sel:<row> (0 is the top row).");
        ImGui::SetNextItemWidth(-60); goHere("press");
        if (ImGui::InputTextWithHint("press##trig", "a value, e.g. menu.press:2", pb, sizeof pb)) an->pressIf = pb;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Press plays each time this comes on (then it goes back to how it rests).\nMenus REUI drives: menu.press:<row> (select on that row), menu.press (any row), menu.back (back).");
        static int row = 1;
        ImGui::SetNextItemWidth(80); ImGui::InputInt("##trow", &row); row = std::max(1, std::min(64, row));
        ImGui::SameLine();
        if (ImGui::Button("Use menu row")) { an->focusIf = "menu.sel:" + std::to_string(row - 1); an->pressIf = "menu.press:" + std::to_string(row - 1); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Focus follows that row of the menu (1: the top one), Press its select.");
        char lb[128], vb[256];
        snprintf(lb, sizeof lb, "%s", an->lockedIf.c_str()); snprintf(vb, sizeof vb, "%s", an->valueOf.c_str());
        ImGui::SetNextItemWidth(-60); goHere("locked");
        if (ImGui::InputTextWithHint("locked##trig", "a value, e.g. flash:...IS_LOCKED", lb, sizeof lb)) an->lockedIf = lb;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("While this is on, Locked plays over its focus (once: it holds its end; or it loops). Coming up\nlocked it starts at Locked's end. A show-only-while value (! for not; key=value compares).");
        ImGui::SetNextItemWidth(-60); goHere("value");
        if (ImGui::InputTextWithHint("value##trig", "a value, e.g. {score}", vb, sizeof vb)) an->valueOf = vb;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Value change plays each time this value changes (a token's name, or text with {tokens}).\nIt plays on top of how it is, as Out does: scale / alpha multiply, moves add - a pop or a flash.");
        ImGui::SetNextItemWidth(120); ImGui::DragFloat("blend##trig", &an->blend, 2, 0, 2000, "%.0f ms");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A change of clip (Focus, Unfocus, Press, Out...) eases from the pose it had over this long. 0: it jumps.");
        ImGui::TextDisabled("Out plays when its show-only-while goes off; it stays drawn till Out ends.");
        ImGui::TextDisabled("Preview a clip: pick it above (the timeline plays it on every element).");
    }
    // ---- tracks
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (cur && ImGui::CollapsingHeader("Tracks", ImGuiTreeNodeFlags_None)) {
        ImGui::TextDisabled("%s clip, at %s. Set a value: a key at the playhead. The diamond: a key here (click: add / remove). I: key them all.", cur->name.c_str(), TimeText(g.time).c_str());
        if (ImGui::BeginTable("##tracks", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("##key"); ImGui::TableSetupColumn("Value"); ImGui::TableSetupColumn("Keys");
            for (int p = 0; p < P_NPROP; ++p) {
                Track& tr = cur->tracks[p];
                ImGui::PushID(p);
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 cp = ImGui::GetCursorScreenPos();
                const float hh = ImGui::GetTextLineHeight();
                dl->AddRectFilled(ImVec2(cp.x, cp.y + hh * 0.3f), ImVec2(cp.x + 6, cp.y + hh * 0.8f), kProp[p].col);
                ImGui::Dummy(ImVec2(9, hh)); ImGui::SameLine();
                if (ImGui::Selectable(kProp[p].name, g.focus == State::F_TRACK && g.track == p)) { g.focus = State::F_TRACK; g.track = p; g.graph.fit = g.big.fit = true; }
                ImGui::TableNextColumn(); KeyDiamond(*cur, p);
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
        ImGui::Checkbox("Wait for the previous screen's Out", &p.waitPrevOut);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its In waits (held at its first frame) while elements of other groups play their Out:\nthe screen going away finishes first. Group motions' Outs count too.");
        if (p.waitPrevOut) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(110); ImGui::DragFloat("overlap##tm", &p.waitOverlap, 5, 0, 3000, "%.0f ms");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start this long before that Out ends (0: right after it).");
        }
    }
    if (ImGui::CollapsingHeader("Effects")) {
        ImGui::TextDisabled("Drawn as copies of it (the game window has no blur shader): sizes in units.");
        ImGui::Checkbox("Drop shadow", &p.shadow);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its silhouette in this colour, offset and soft (pictures, text and shapes).");
        if (p.shadow) {
            ImGui::SetNextItemWidth(150); ImGui::DragFloat2("offset##sh", &p.shadowDx, 0.1f, -40, 40, "%.1f");
            ImGui::SameLine(); ImGui::SetNextItemWidth(70); ImGui::DragFloat("soft##sh", &p.shadowBlur, 0.1f, 0, 24, "%.1f"); p.shadowBlur = std::max(0.f, p.shadowBlur);
            ImGui::SameLine(); ImGui::ColorEdit4("##shc", p.shadowCol, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        }
        ImGui::Checkbox("Glow", &p.glow);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its silhouette in this colour all round it, this wide.");
        if (p.glow) {
            ImGui::SetNextItemWidth(110); ImGui::DragFloat("size##gl", &p.glowSize, 0.1f, 0, 32, "%.1f"); p.glowSize = std::max(0.f, p.glowSize);
            ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::DragFloat("strength##gl", &p.glowStrength, 0.02f, 0, 4, "%.2f"); p.glowStrength = std::max(0.f, p.glowStrength);
            ImGui::SameLine(); ImGui::ColorEdit4("##glowc", p.glowCol, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        }
        ImGui::SetNextItemWidth(110); ImGui::DragFloat("blur##fx", &p.blur, 0.05f, 0, 16, "%.2f"); p.blur = std::max(0.f, p.blur);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Itself softened by this much (units). The Blur track adds to it.");
        const char* wp[] = { "none", "left to right", "right to left", "top to bottom", "bottom to top", "from the middle out" };
        ImGui::SetNextItemWidth(160); ImGui::Combo("wipe##fx", &p.mask, wp, 6);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A part of its box shown, by the Wipe track (0: none of it, 1: all) - key it to reveal or hide it.\n(To clip it to another element's box: the editor's Mask.)");
        ImGui::TextDisabled("Tracks: Effects scales the shadow / glow, Blur adds blur, Wipe shows its part.");
        const char* bm[] = { "normal", "add", "screen", "multiply", "overlay", "lighten", "darken" };
        ImGui::SetNextItemWidth(110); ImGui::Combo("blend mode", &p.blend, bm, 7); FocusPlanned("Blend mode"); PlannedTag();
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
    {   // the page | the state graph (a click on the graph's trigger arrow comes back to the page)
        static int lastTab = -1;
        const bool force = g.pageTab != lastTab && lastTab >= 0;
        if (ImGui::BeginTabBar("##ptabs")) {
            if (ImGui::BeginTabItem("Page", nullptr, force && g.pageTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) { if (!force) g.pageTab = 0; AnimPage(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("State graph", nullptr, force && g.pageTab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) { if (!force) g.pageTab = 1; StateGraph(); ImGui::EndTabItem(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The focused element's clips and what starts them; your own transitions.");
            ImGui::EndTabBar();
        }
        lastTab = g.pageTab;
    }
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
    if (ImGui::BeginTabBar("##gtabs")) {
        if (ImGui::BeginTabItem("Values")) { if (g.graphTab != 0) { g.graphTab = 0; g.graph.fit = true; } ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Speed")) { if (g.graphTab != 1) { g.graphTab = 1; g.graph.fit = true; } ImGui::EndTabItem(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each track's rate of change (units, scale, degrees per second): a jump shows a jerk at a key.");
        ImGui::EndTabBar();
    }
    std::string what = !c ? "-" : g.focus == State::F_TRACK && g.track >= 0 ? std::string(kProp[g.track].name)
                     : g.focus == State::F_PLANNED ? g.planned + " (planned)" : c->name + " clip: all its keyed tracks";
    ImGui::Text("Showing: %s", what.c_str());
    ImGui::SameLine(); if (ImGui::SmallButton("fit")) g.graph.fit = true;
    if (c && g.focus == State::F_TRACK) { ImGui::SameLine(); if (ImGui::SmallButton("all tracks")) { g.focus = State::F_CLIP; g.graph.fit = true; } }
    EaseButtons(c);
    if (c) {   // handle types
        ImGui::SameLine(0, 10);
        for (int h : { HT_AUTO, HT_ALIGNED, HT_FREE, HT_VECTOR }) {
            ImGui::SameLine(0, 2);
            if (ImGui::SmallButton(kHandleType[h])) SetHandleType(*c, h);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(h == HT_AUTO ? "Auto handles: smooth through the keys beside it, never overshooting (V)." : h == HT_ALIGNED ? "Aligned: its two handles stay on one line." : h == HT_FREE ? "Free: each handle on its own." : "Vector: each handle points at its neighbour key - a sharp corner.");
        }
        ImGui::SameLine(0, 8); if (ImGui::Checkbox("0-1", &g.normalise)) g.graph.fit = g.big.fit = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every track shown scaled to 0-1, so Move and Scale compare on one graph.");
    }
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
    ImGui::SameLine(); ImGui::Checkbox("frames", &g.frames);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show and snap key times to frames at 60 fps - off: milliseconds.\nPlayback isn't stepped: curves are read at the exact time of each drawn frame, so 120 / 144 Hz get the in-between values.\n(The game's Flash menus run at 30 fps: Flash frame N is frame 2N here.)");
    ImGui::SameLine();
    if (g.autoKey) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.6f, 0.15f, 0.15f, 1.f));
    ImGui::Checkbox("auto key", &g.autoKey);
    if (g.autoKey) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: the focused element's handle box on screen, each change keyed at the playhead (this clip):\n  drag inside it: Move X / Y (Shift: one axis)\n  its corners / edges: Scale (Shift on a corner: both alike)\n  just outside a corner: Rotation (Shift: 15 degree steps)\n  the cross: its pivot (not keyed)\n  Ctrl / Shift / Alt + wheel over it: Scale / Rotation / Alpha\nOff: clicking the screen only picks elements; values set on the page are keyed as always.");
    ImGui::SameLine(); ImGui::BeginDisabled(UndoCount() == 0);
    if (ImGui::Button("Undo")) Undo();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Undo the last change (Ctrl + Z): keys, clips, copied motion, triggers, effects. %d step%s.", (int)UndoCount(), UndoCount() == 1 ? "" : "s");
    ImGui::SameLine(); ImGui::BeginDisabled(RedoCount() == 0);
    if (ImGui::Button("Redo")) Redo();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Redo (Ctrl + Y or Ctrl + Shift + Z).");
    if (an) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(130);
        std::vector<const char*> names; for (auto& k : an->clips) names.push_back(k.name.c_str());
        if (ImGui::Combo("clip", &g.clip, names.data(), (int)names.size())) { g.focus = State::F_CLIP; g.graph.fit = g.big.fit = true; }
    }
    {   // Element / Screen (Tab), the view options, the window layouts, the keys' help
        ImGui::SameLine();
        for (int i = 0; i < 2; ++i) {
            const bool on = g.screen == (i == 1);
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(i ? "Screen" : "Element")) g.screen = i == 1;
            if (on) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(i ? "Every element's clip on its own row, by group - time a whole screen in one place (Tab)." : "The focused element's tracks (Tab).");
            if (!i) ImGui::SameLine(0, 1);
        }
        ImGui::SameLine(); if (ImGui::Button("View")) ImGui::OpenPopup("##tlview");
        if (ImGui::BeginPopup("##tlview")) {
            ImGui::Checkbox("Onion skin", &g.onion);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Faint ghosts of the focused element before (blue) and after (orange) the playhead (paused only).");
            ImGui::Indent();
            int om = g.onionKeys ? 0 : 1;
            if (ImGui::RadioButton("at keys", &om, 0)) g.onionKeys = true; ImGui::SameLine();
            if (ImGui::RadioButton("every", &om, 1)) g.onionKeys = false; ImGui::SameLine();
            const int steps[] = { 2, 4, 8 }; int si = g.onionStep == 2 ? 0 : g.onionStep == 8 ? 2 : 1; const char* sn[] = { "2", "4", "8" };
            ImGui::SetNextItemWidth(50); if (ImGui::Combo("frames", &si, sn, 3)) g.onionStep = steps[si];
            ImGui::Checkbox("its whole group", &g.onionGroup);
            ImGui::Unindent();
            ImGui::Checkbox("Motion path", &g.path);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The focused element's centre over the whole clip, a dot per frame (close dots: slow).");
            ImGui::Separator();
            ImGui::Checkbox("Play events", &g.playEvents);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Events run as the playhead passes them while playing (and in Live) - to check sounds against the motion.");
            ImGui::Checkbox("Game gets the keys", &g.gameInput);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off (normally): while animating, the keyboard / controller don't move the game's menus underneath.");
            ImGui::Separator();
            ImGui::Checkbox("Screen: keyed rows only", &g.keyedOnly);
            ImGui::SetNextItemWidth(160); ImGui::InputTextWithHint("##shf", "Screen: filter rows", g.sheetFilter, sizeof g.sheetFilter);
            ImGui::EndPopup();
        }
        ImGui::SameLine(); LayoutCombo();
        ImGui::SameLine(); if (ImGui::Button(" ? ")) g_openHelp = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The keys of animation mode.");
        ShortcutPopups();
    }
    const float bw = ImGui::CalcTextSize(" Exit ").x + 12;
    const float rightX = ImGui::GetWindowContentRegionMax().x - bw - ImGui::CalcTextSize(" Collapse ").x - ImGui::CalcTextSize(" Hide panels ").x - ImGui::CalcTextSize(" Live ").x - 64;
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
    ImGui::SameLine(); if (ImGui::Button(" Exit ")) { g.on = false; if (g.live) { g.live = false; SimRestart(); } }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Leave animation mode (F8).");
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
        ImGui::SameLine(); if (ImGui::Checkbox("locked##sim", &g.simLocked)) {}
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stands in for every element's locked value: Locked plays while it's on. L.");
        ImGui::SameLine(); if (ImGui::Button("Change value##sim")) ++g.simValue;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stands in for a change of every element's value: Value change plays. V.");
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
    if (c && !g.screen) for (int p = 0; p < P_NPROP; ++p) if (!c->tracks[p].keys.empty() || (g.focus == State::F_TRACK && g.track == p)) rows.push_back(p);
    if (g.screen) for (size_t i = 0, n = SheetRows().size(); i < n; ++i) rows.push_back(-1);   // (its height)
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
    static bool scrubbing = false; static int dRow = -1, dKey = -1; static float dStartX = 0; static std::vector<float> dOrig;
    if (g.screen) ScreenSheet(dl, o, ra, rb, rulerH, rowH, hov, scrubbing, rowScroll);
    dl->PushClipRect(ImVec2(o.x, ra.y + rulerH), rb, true);
    if (g.screen) {}
    else if (!c) dl->AddText(ImVec2(o.x + 8, y + 6), IM_COL32(150, 150, 150, 255), "Pick an element on the right.");
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
    const bool evTook = !g.screen && RulerEvents(dl, ra, rb, rulerH, hov);
    if (hov && io.MouseWheel != 0 && m.x < ra.x) rowScroll -= io.MouseWheel * rowH * 2;   // over the names: scroll the rows
    else if (hov && io.MouseWheel != 0) { const float f = io.MouseWheel > 0 ? 0.85f : 1.f / 0.85f, mt = T(m.x); g.tl0 = mt + (g.tl0 - mt) * f; g.tl1 = mt + (g.tl1 - mt) * f; }
    if (hov && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) { const float dt = -io.MouseDelta.x / (rb.x - ra.x) * (g.tl1 - g.tl0); g.tl0 += dt; g.tl1 += dt; }
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m.x >= ra.x && !g.screen && !evTook) {
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
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m.x < ra.x && c && !g.screen) {   // a row's name: focus that track
        float yy = ra.y + rulerH + rowH - rowScroll;
        for (int p : rows) { if (m.y >= yy && m.y < yy + rowH && m.y >= ra.y + rulerH) { g.focus = State::F_TRACK; g.track = p; g.graph.fit = g.big.fit = true; } yy += rowH; }
        if (m.y >= ra.y + rulerH - rowScroll && m.y < ra.y + rulerH + rowH - rowScroll && m.y >= ra.y + rulerH) { g.focus = State::F_CLIP; g.graph.fit = g.big.fit = true; }
    }
    if (scrubbing) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float t = std::max(0.f, Snap(T(m.x)));
            if (Clip* cc = CurClip()) for (auto& ev : cc->events) if (fabsf(X(ev.t) - m.x) < 6) t = ev.t;   // (markers catch it)
            g.time = t; g.playing = false;
        }
        else scrubbing = false;
    }
    if (dKey >= 0 && c) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float dt = (m.x - dStartX) / (rb.x - ra.x) * (g.tl1 - g.tl0);
            size_t n = 0;
            for (auto& tr : c->tracks) for (auto& kk : tr.keys) { if (n < dOrig.size() && kk.sel) kk.t = std::max(0.f, Snap(dOrig[n] + dt)); ++n; }
        } else { for (auto& tr : c->tracks) SortKeys(tr); dKey = -1; }
    }
    if (hov && c && ImGui::IsKeyPressed(ImGuiKey_X) && !io.WantTextInput && !io.KeyCtrl && !g.screen)
        for (auto& tr : c->tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end());
    if (hov && c && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m.x >= ra.x && !g.screen && !evTook && m.y >= ra.y + rulerH) {   // a key under it: picked first (as a click)
        if (hitKey >= 0 && !c->tracks[hitRow].keys[hitKey].sel) { for (auto& tr : c->tracks) for (auto& kk : tr.keys) kk.sel = false; c->tracks[hitRow].keys[hitKey].sel = true; }
        ImGui::OpenPopup("##tlkeys");
    }
    if (ImGui::BeginPopup("##tlkeys")) { KeyClipItems(); ImGui::EndPopup(); }
    if (hov && m.x >= ra.x && !g.screen && !evTook && !ImGui::IsPopupOpen("##evedit")) ImGui::SetTooltip("Click / drag: move the playhead. Drag keys (Shift: pick several), Delete removes. Wheel: zoom, middle drag: pan.\nRight-click / Ctrl + C, X, V: copy, cut, paste keys at the playhead; Ctrl + A picks all shown.\nRight-click the ruler: add an event or a marker (M: a marker at the playhead).");
    if (hov && m.x < ra.x && !g.screen) ImGui::SetTooltip("Click a track: graph it (the top row: the whole clip). Wheel: scroll the rows.");
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

// auto key: the focused element changed on screen, keyed at the playhead - drag: Move X / Y; Ctrl / Shift / Alt + wheel
// over it: Scale / Rotation / Alpha
static void AutoKey() {   // (dragging: the canvas's handle box, uied_anim_tools.inc)
    ImGuiIO& io = ImGui::GetIO();
    Clip* c = CurClip(); const int fi = FocusedIndex();
    if (!c || fi < 0) return;
    Elem& e = Elems()[fi];
    const float k = Unit(), t = Snap(g.time);
    auto over = [&]() {   // the mouse over it as it's posed now
        ImVec2 a, b; ElemRect(e, a, b);
        Pose p; Sample(*c, g.time, p);
        const ImVec2 m((a.x + b.x) / 2 + p.dx * k, (a.y + b.y) / 2 + p.dy * k);
        const float hw = std::max(8.f, fabsf(b.x - a.x) / 2 * p.sx), hh = std::max(8.f, fabsf(b.y - a.y) / 2 * p.sy);
        return fabsf(io.MousePos.x - m.x) <= hw && fabsf(io.MousePos.y - m.y) <= hh;
    };
    auto key = [&](int prop, float v) { SetKey(c->tracks[prop], t, v); c->on = true; g.focus = State::F_TRACK; g.track = prop; };
    (void)k;
    if (io.MouseWheel != 0 && !io.WantCaptureMouse && (io.KeyCtrl || io.KeyShift || io.KeyAlt) && over()) {
        const float w = io.MouseWheel;
        if (io.KeyCtrl) { const float f = powf(1.05f, w); key(P_SX, Value(c->tracks[P_SX], t) * f); key(P_SY, Value(c->tracks[P_SY], t) * f); }
        else if (io.KeyShift) key(P_ROT, Value(c->tracks[P_ROT], t) + 2.f * w);
        else key(P_ALPHA, std::max(0.f, std::min(1.f, Value(c->tracks[P_ALPHA], t) + 0.05f * w)));
    }
}
static void Frame() {
    ImGuiIO& io = ImGui::GetIO();
    UndoCommit(false);   // (a finished change: an undo step - the editor's own snapshots, also with its panel closed)
    if (!io.WantTextInput && io.KeyCtrl && !g.live && !g.dragOn) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) Redo(); else Undo(); }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
    }
    if (g.autoKey && !g.live) AutoKey();
    ApplyAllHandles();   // (auto / vector handles follow their keys)
    TestTick();
    // playback
    {   // a change to the focused element's clips: the layout has unsaved changes
        static std::string lastKey, lastText;
        const int fi = FocusedIndex();
        std::string text = fi >= 0 ? AnimText(Elems()[fi].an.p) : std::string();
        if (fi >= 0 && g.elemKey == lastKey && text != lastText) g_dirty = true;
        lastKey = fi >= 0 ? g.elemKey : std::string(); lastText.swap(text);
    }
    if (g.playing && !g.live) {
        const float len = PreviewLen(), was = g.time;
        g.time += io.DeltaTime * 1000.f;
        if (g.time > len) {
            PreviewEvents(was, len);
            if (g.loop) { g.time = fmodf(g.time, std::max(1.f, len)); PreviewEvents(-1, g.time); } else { g.time = len; g.playing = false; }
        } else PreviewEvents(was, g.time);
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
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) g.simLocked = !g.simLocked;
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) ++g.simValue;
    }
    else Shortcuts();   // (Live's keys above: Esc leaves it)
    if (g.live && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { g.live = false; SimRestart(); }
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
void FromGameTest(const std::string& spec) {   // (tests) "<movie name>.<clip>.<motion clip | none>": as "Add all"
    char mv[128] = "", cn[64] = ""; int clip = -1;
    if (sscanf(spec.c_str(), "%127[^.].%d.%63s", mv, &clip, cn) != 3 || !motionSource.movies) { Log("from the game test: bad spec %s", spec.c_str()); return; }
    anim::FromGameUi& u = anim::g_fg;
    if (g_nowMode >= 0 && g_nowMode < NumModes()) g_editMode = g_nowMode;   // (into the layout being shown)
    u.movies = motionSource.movies(); u.listed = true; u.movie.clear();
    for (auto& m : u.movies) if (anim::LowerS(GroupLeaf(m)) == anim::LowerS(mv)) { u.movie = m; break; }
    std::string err;
    if (u.movie.empty() || !motionSource.clips(u.movie, u.clips, u.fps, u.ppu, err) || clip < 0 || clip >= (int)u.clips.size()
        || !motionSource.sample(u.movie, clip, u.pieces, err)) { Log("from the game test: no movie / clip %s %s", spec.c_str(), err.c_str()); return; }
    u.clipsFor = u.movie; u.clip = clip; u.piecesFor = u.movie + "#" + std::to_string(clip); u.err.clear();
    u.piece = -1; for (int i = 0; i < (int)u.pieces.size() && u.piece < 0; ++i) if (!u.pieces[i].image.empty()) u.piece = i;
    u.motion = strcmp(cn, "none") != 0; u.place = 0;
    for (int i = 0; i < 7; ++i) if (!strcmp(anim::kClips[i][0], cn)) u.motionClip = i;
    std::vector<int> all; for (int i = 0; i < (int)u.pieces.size(); ++i) if (!u.pieces[i].image.empty()) all.push_back(i);
    const size_t first = Elems().size();
    const int added = anim::AddFromGame(u, all, false);
    FILE* f = fopen((dataDir + "anim_test.txt").c_str(), "w");
    auto out = [&](const char* fmt, ...) {
        char line[512]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
        Log("%s", line); if (f) fprintf(f, "%s\n", line);
    };
    out("from the game test: %s / %s: %d added - %s", u.movie.c_str(), u.clips[clip].name.c_str(), added, u.msg.c_str());
    out("from the game test: modes now %d, edit %d (of %d); show %d, edit on screen %d, index %d", g_nowMode, g_editMode, NumModes(), (int)g_show, (int)g_edit, (int)g_state);
    for (size_t i = first; i < Elems().size(); ++i) {
        const Elem& e = Elems()[i];
        int keys = 0; if (e.an.p) for (auto& c : e.an.p->clips) keys += anim::Keys(c);
        out("from the game test:   %s [%s] at %.1f, %.1f size %.1f x %.1f rot %.1f alpha %.2f, %d keys", e.name.c_str(), e.pack.c_str(), e.x, e.y, e.w, e.h, e.rot, e.col[3], keys);
    }
    if (f) fclose(f);
    u.whereShown = true; u.scrollList = true; u.pv.play = false; u.pv.boundsFor.clear(); u.pv.startAt = u.clips[clip].last - u.clips[clip].first;
}
void AnimKeysTest(float at) {   // (tests: the focused element's shown keys copied, pasted at `at` ms)
    {   // (no keys on the focused one: the first element with keys, in the first layout that has one)
        auto keys = [](const Elem& e) { int n = 0; if (e.an.p) for (auto& c : e.an.p->clips) n += anim::Keys(c); return n; };
        const int fi = anim::FocusedIndex();
        if (fi < 0 || !keys(Elems()[fi]))
            for (int m = 0, done = 0; m < NumModes() && !done; ++m)
                for (int i = 0; i < (int)g_layout[m].size(); ++i)
                    if (keys(g_layout[m][i])) { g_editMode = m; AnimFocus(i); done = 1; break; }
        if (anim::ElemAnim* fa = anim::Focused())   // (its clip with keys: the one being edited)
            for (int k = 0; k < (int)fa->clips.size(); ++k) if (anim::Keys(fa->clips[k])) { anim::g.clip = k; break; }
    }
    anim::PickAllShown(); anim::CopyKeys(false); anim::g.time = at; anim::PasteKeys();
}
void AnimUndoTest() {   // (tests) undo, then redo: the focused element's key count each time, to <REUI>\anim_test.txt
    auto count = []() { const int fi = anim::FocusedIndex(); int n = 0; if (fi >= 0 && Elems()[fi].an.p) for (auto& c : Elems()[fi].an.p->clips) n += anim::Keys(c); return n; };
    const int before = count(), steps = (int)UndoCount();
    Undo(); const int undone = count();
    Redo(); const int redone = count();
    if (FILE* f = fopen((dataDir + "anim_test.txt").c_str(), "w")) {
        fprintf(f, "undo test: %d undo steps; keys %d, after undo %d, after redo %d\n", steps, before, undone, redone); fclose(f);
    }
}
void AnimLive(bool live) { if (anim::g.live != live) { anim::g.live = live; anim::SimRestart(); } }
std::function<bool(const std::string& cmd)> animEvent;
bool AnimTakesInput() { return anim::g.on && !anim::g.gameInput; }
void AnimTestScript(const std::string& spec) {   // (tests) "step|step|..." or a file in REUI's folder (a step a line): a step
    std::string steps = spec;                       // every few frames, results to anim_test.txt
    if (spec.size() > 4 && spec.compare(spec.size() - 4, 4, ".txt") == 0) {
        steps.clear();
        if (FILE* f = fopen((dataDir + spec).c_str(), "r")) {
            char line[1024];
            while (fgets(line, sizeof line, f)) {
                std::string l = line; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
                if (!l.empty() && l[0] != '#') { if (!steps.empty()) steps += "|"; steps += l; }
            }
            fclose(f);
        }
    }
    if (FILE* f = anim::TestOut(true)) fclose(f);
    anim::g_test.clear(); anim::g_testWait = 0;
    size_t p = 0;
    while (p <= steps.size()) { size_t q = steps.find('|', p); if (q == std::string::npos) q = steps.size(); if (q > p) anim::g_test.push_back(steps.substr(p, q - p)); p = q + 1; }
    anim::TestLog("script: %d steps", (int)anim::g_test.size());
}
void AnimPlayhead(float ms, bool play) { anim::g.time = std::max(0.f, ms); anim::g.playing = play; anim::g.loop = true; }
void AnimFocus(int index) {
    if (index < 0 || index >= (int)Elems().size()) return;
    anim::g.elemKey = anim::KeyOf(g_editMode, Elems()[index]); g_sel = index; anim::g.focus = anim::State::F_ELEMENT;
}
