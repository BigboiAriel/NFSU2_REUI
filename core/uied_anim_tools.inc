// Animation mode's tools (included by uied_anim.inc, inside namespace anim): the canvas (elements picked on screen, the
// handle box, G / S / R, onion skin and the motion path), key diamonds and keying (I / Alt + I), the timeline's Screen
// mode (the dope sheet), events and markers on the ruler, the state graph, the keyboard shortcuts, saved window layouts
// and the test script. Plan: "Animation editor improvements: plan" (a Claude doc in the Joy Ride project).

// ================================================================ the canvas: where each element was drawn

struct Drawn { ImVec2 a, b, piv; float rot = 0; int frame = -10; };
static std::unordered_map<const Elem*, Drawn> g_drawn;
static void NoteDrawn(const Elem& e, ImVec2 a, ImVec2 b, float rot, ImVec2 piv) {
    if (!g.on || g_tileCtx.on) return;
    auto& els = Elems();
    if (els.empty() || &e < &els.front() || &e > &els.back()) return;   // (the layout's own elements: not page / tile copies)
    Drawn& d = g_drawn[&e]; d.a = a; d.b = b; d.piv = piv; d.rot = rot; d.frame = ImGui::GetFrameCount();
}
// an element's box on screen as drawn: centre, half size, turned `rot` degrees about `piv`
struct Box { ImVec2 c, h, piv; float rot = 0; };
static ImVec2 Turn(ImVec2 v, float deg) { const float r = deg * 3.14159265f / 180, cs = cosf(r), sn = sinf(r); return ImVec2(v.x * cs - v.y * sn, v.x * sn + v.y * cs); }
static ImVec2 BoxPoint(const Box& b, float ux, float uy) {   // (ux, uy: -1..1 across it)
    const ImVec2 q(b.c.x + ux * b.h.x - b.piv.x, b.c.y + uy * b.h.y - b.piv.y), t = Turn(q, b.rot);
    return ImVec2(b.piv.x + t.x, b.piv.y + t.y);
}
static ImVec2 BoxLocal(const Box& b, ImVec2 m) {   // a screen point -> from its centre, unturned
    const ImVec2 t = Turn(ImVec2(m.x - b.piv.x, m.y - b.piv.y), -b.rot);
    return ImVec2(b.piv.x + t.x - b.c.x, b.piv.y + t.y - b.c.y);
}
static bool BoxOf(int i, Box& out) {
    auto& els = Elems();
    if (i < 0 || i >= (int)els.size()) return false;
    const int frame = ImGui::GetFrameCount();
    Elem& e = els[i];
    if (e.type == E_MOTION) {   // a group's motion: its members' boxes together, about its point
        ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
        for (auto& o : els) {
            if (o.type == E_MOTION || !InGroup(o.group, e.group)) continue;
            auto it = g_drawn.find(&o);
            if (it == g_drawn.end() || it->second.frame < frame - 1) continue;
            Box ob; ob.c = ImVec2((it->second.a.x + it->second.b.x) / 2, (it->second.a.y + it->second.b.y) / 2);
            ob.h = ImVec2(fabsf(it->second.b.x - it->second.a.x) / 2, fabsf(it->second.b.y - it->second.a.y) / 2); ob.piv = it->second.piv; ob.rot = it->second.rot;
            for (int k = 0; k < 4; ++k) { const ImVec2 p = BoxPoint(ob, k & 1 ? 1.f : -1.f, k & 2 ? 1.f : -1.f); lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); }
        }
        if (lo.x > hi.x) return false;
        ImVec2 a, b; ElemRect(e, a, b);
        const float k = Unit();
        out.c = ImVec2((lo.x + hi.x) / 2, (lo.y + hi.y) / 2); out.h = ImVec2((hi.x - lo.x) / 2, (hi.y - lo.y) / 2);
        out.piv = ImVec2((a.x + b.x) / 2 + e.pivX * k, (a.y + b.y) / 2 + e.pivY * k); out.rot = 0;
        return true;
    }
    auto it = g_drawn.find(&e);
    if (it == g_drawn.end() || it->second.frame < frame - 1) return false;
    const Drawn& d = it->second;
    out.c = ImVec2((d.a.x + d.b.x) / 2, (d.a.y + d.b.y) / 2); out.h = ImVec2(std::max(2.f, fabsf(d.b.x - d.a.x) / 2), std::max(2.f, fabsf(d.b.y - d.a.y) / 2));
    out.piv = d.piv; out.rot = d.rot;
    return true;
}
static bool BoxHit(const Box& b, ImVec2 m, float pad = 0) { const ImVec2 l = BoxLocal(b, m); return fabsf(l.x) <= b.h.x + pad && fabsf(l.y) <= b.h.y + pad; }
// the elements under a point, topmost first (shown, drawn this frame; group motions aren't picked on screen)
static std::vector<int> ElemsAt(ImVec2 m) {
    std::vector<int> out;
    const std::vector<int> order = DrawOrder(Elems());
    for (int oi = (int)order.size() - 1; oi >= 0; --oi) {
        const int i = order[oi]; const Elem& e = Elems()[i];
        if (!e.visible || e.type == E_MOTION) continue;
        Box b; if (BoxOf(i, b) && BoxHit(b, m)) out.push_back(i);
    }
    return out;
}
static void FocusElem(int i) {
    if (i < 0 || i >= (int)Elems().size()) return;
    const std::string k = KeyOf(g_editMode, Elems()[i]);
    if (k != g.elemKey) { g.graph.fit = g.big.fit = true; }
    g.elemKey = k; g_sel = i; if (g.focus != State::F_TRACK) g.focus = State::F_ELEMENT;
}
// a click on the screen: the element under it (again in the same place: the next one down; Alt: its group's motion)
static int CanvasPick(ImVec2 m, bool alt) {
    static ImVec2 lastAt(-1e9f, -1e9f);
    const std::vector<int> hits = ElemsAt(m);
    if (hits.empty()) return -1;
    int pick = hits[0];
    const bool same = fabsf(m.x - lastAt.x) < 4 && fabsf(m.y - lastAt.y) < 4;
    lastAt = m;
    if (same) { const int fi = FocusedIndex(); for (size_t j = 0; j < hits.size(); ++j) if (hits[j] == fi) { pick = hits[(j + 1) % hits.size()]; break; } }
    if (alt && !Elems()[pick].group.empty()) { FocusMotion(Elems()[pick].group); return FocusedIndex(); }
    FocusElem(pick);
    return pick;
}

// ---- the handle box: drag it to move / scale / turn the focused element (keyed at the playhead), or move its pivot
enum { H_NONE, H_MOVE, H_CORNER, H_EDGE, H_ROTATE, H_PIVOT };
struct CanvasDrag {
    int part = H_NONE; float ux = 0, uy = 0;   // which handle (ux, uy: -1 / 0 / 1 across the box)
    ImVec2 m0; Box box; float v0[P_NPROP] = {}; float pivX0 = 0, pivY0 = 0, ex0 = 0, ey0 = 0, unposedHW = 1, unposedHH = 1;
    float ang = 0, lastAng = 0;                // (turning: the angle so far)
    int elem = -1; Clip keep; bool active = false;
    int modal = 0;                             // G / S / R: the mouse moves it with no button held (click or Enter keeps it)
};
static CanvasDrag g_cd;
static bool Turns(const Elem& e) { return e.type == E_IMAGE || e.type == E_SHAPE || e.type == E_MOTION; }
static int HandleAt(const Box& b, ImVec2 m, const Elem& e, float& ux, float& uy) {
    const float r = 6;
    if (Turns(e) && fabsf(m.x - b.piv.x) <= r + 2 && fabsf(m.y - b.piv.y) <= r + 2 && e.type != E_TEXT) { ux = uy = 0; return H_PIVOT; }
    for (int k = 0; k < 4; ++k) {   // corners, then the turning ring just outside them
        const float cx = k & 1 ? 1.f : -1.f, cy = k & 2 ? 1.f : -1.f;
        const ImVec2 p = BoxPoint(b, cx, cy);
        if (fabsf(m.x - p.x) <= r && fabsf(m.y - p.y) <= r) { ux = cx; uy = cy; return H_CORNER; }
    }
    for (int k = 0; k < 4; ++k) {
        const float cx = k == 0 ? -1.f : k == 1 ? 1.f : 0.f, cy = k == 2 ? -1.f : k == 3 ? 1.f : 0.f;
        const ImVec2 p = BoxPoint(b, cx, cy);
        if (fabsf(m.x - p.x) <= r && fabsf(m.y - p.y) <= r) { ux = cx; uy = cy; return H_EDGE; }
    }
    if (Turns(e) && e.type != E_TEXT)
        for (int k = 0; k < 4; ++k) {
            const float cx = k & 1 ? 1.f : -1.f, cy = k & 2 ? 1.f : -1.f;
            const ImVec2 p = BoxPoint(b, cx, cy);
            const float d = sqrtf((m.x - p.x) * (m.x - p.x) + (m.y - p.y) * (m.y - p.y));
            if (d > r && d < 22 && !BoxHit(b, m)) { ux = cx; uy = cy; return H_ROTATE; }
        }
    if (BoxHit(b, m)) { ux = uy = 0; return H_MOVE; }
    return H_NONE;
}
static void KeyAt(Clip& c, int prop, float v) {
    const float t = Snap(g.time);
    SetKey(c.tracks[prop], t, v); c.on = true;
    if (t > c.length) c.length = t;
    g.focus = State::F_TRACK; g.track = prop;
}
static bool CanvasBegin(int part, float ux, float uy, ImVec2 m, int modal = 0) {   // (also the test script's)
    const int fi = FocusedIndex(); Clip* c = CurClip();
    Box b;
    if (fi < 0 || !c || !BoxOf(fi, b)) return false;
    Elem& e = Elems()[fi];
    g_cd = CanvasDrag(); g_cd.part = part; g_cd.ux = ux; g_cd.uy = uy; g_cd.m0 = m; g_cd.box = b; g_cd.elem = fi; g_cd.keep = *c; g_cd.active = true; g_cd.modal = modal;
    const float t = Snap(g.time);
    for (int p = 0; p < P_NPROP; ++p) g_cd.v0[p] = Value(c->tracks[p], t);
    g_cd.pivX0 = e.pivX; g_cd.pivY0 = e.pivY; g_cd.ex0 = e.x; g_cd.ey0 = e.y;
    ImVec2 a0, b0; ElemRect(e, a0, b0);
    g_cd.unposedHW = std::max(1.f, fabsf(b0.x - a0.x) / 2); g_cd.unposedHH = std::max(1.f, fabsf(b0.y - a0.y) / 2);
    g_cd.lastAng = atan2f(m.y - b.piv.y, m.x - b.piv.x);
    g.dragOn = true;
    return true;
}
static void CanvasDragTo(ImVec2 m, bool shift, bool ctrl) {
    if (!g_cd.active) return;
    Clip* c = CurClip(); const int fi = FocusedIndex();
    if (!c || fi != g_cd.elem) { g_cd.active = false; g.dragOn = false; return; }
    Elem& e = Elems()[fi];
    const float k = Unit();
    const Box& b = g_cd.box;
    const ImVec2 d(m.x - g_cd.m0.x, m.y - g_cd.m0.y);
    auto r1 = [](float v) { return roundf(v * 10) / 10; };
    switch (g_cd.part) {
    case H_MOVE: {
        float dx = d.x / k, dy = d.y / k;
        if (shift) { if (fabsf(dx) > fabsf(dy)) dy = 0; else dx = 0; }   // (Shift: along one axis)
        KeyAt(*c, P_X, r1(g_cd.v0[P_X] + dx)); KeyAt(*c, P_Y, r1(g_cd.v0[P_Y] + dy));
        break;
    }
    case H_CORNER: case H_EDGE: {   // about the pivot, in its own (turned) frame
        const ImVec2 q = Turn(ImVec2(m.x - b.piv.x, m.y - b.piv.y), -b.rot), q0 = Turn(ImVec2(g_cd.m0.x - b.piv.x, g_cd.m0.y - b.piv.y), -b.rot);
        float fx = fabsf(q0.x) > 2 ? q.x / q0.x : 1.f, fy = fabsf(q0.y) > 2 ? q.y / q0.y : 1.f;
        if (g_cd.modal || (g_cd.part == H_CORNER && shift)) {   // (G / S / R's S, Shift on a corner: both alike)
            const float l0 = sqrtf(q0.x * q0.x + q0.y * q0.y), l = sqrtf(q.x * q.x + q.y * q.y);
            fx = fy = l0 > 2 ? l / l0 : 1.f;
        }
        if (g_cd.part == H_EDGE) { if (g_cd.ux == 0) fx = 1; if (g_cd.uy == 0) fy = 1; }
        auto sv = [&](float v) { return ctrl ? roundf(v * 20) / 20 : roundf(v * 1000) / 1000; };   // (Ctrl: steps of 0.05)
        if (fx != 1 || g_cd.part == H_CORNER) KeyAt(*c, P_SX, sv(g_cd.v0[P_SX] * fx));
        if (fy != 1 || g_cd.part == H_CORNER) KeyAt(*c, P_SY, sv(g_cd.v0[P_SY] * fy));
        break;
    }
    case H_ROTATE: {
        const float a = atan2f(m.y - b.piv.y, m.x - b.piv.x);
        float da = a - g_cd.lastAng; while (da > 3.14159265f) da -= 6.2831853f; while (da < -3.14159265f) da += 6.2831853f;
        g_cd.ang += da; g_cd.lastAng = a;
        float v = g_cd.v0[P_ROT] + g_cd.ang * 180 / 3.14159265f;
        v = shift || ctrl ? roundf(v / 15) * 15 : roundf(v * 10) / 10;   // (Shift: 15 degree steps)
        KeyAt(*c, P_ROT, v);
        break;
    }
    case H_PIVOT: {   // (not keyed: the element's own pivot; a group motion: its point)
        if (e.type == E_MOTION) { e.x = r1(g_cd.ex0 + d.x / k); e.y = r1(g_cd.ey0 + d.y / k); }
        else {
            const float sx = std::max(0.01f, b.h.x / g_cd.unposedHW), sy = std::max(0.01f, b.h.y / g_cd.unposedHH);
            e.pivX = r1(g_cd.pivX0 + d.x / k / sx); e.pivY = r1(g_cd.pivY0 + d.y / k / sy);
        }
        g_dirty = true;
        break;
    }
    default: break;
    }
}
static void CanvasEnd(bool cancel) {
    if (!g_cd.active) return;
    if (cancel) {   // (Esc / right-click: as it was)
        Clip* c = CurClip();
        if (c && FocusedIndex() == g_cd.elem) *c = g_cd.keep;
        if (g_cd.part == H_PIVOT && g_cd.elem >= 0 && g_cd.elem < (int)Elems().size()) { Elem& e = Elems()[g_cd.elem]; e.pivX = g_cd.pivX0; e.pivY = g_cd.pivY0; e.x = g_cd.ex0; e.y = g_cd.ey0; }
    }
    g_cd.active = false; g.dragOn = false;
}
static void StartModal(int which) {   // G / S / R: grab, scale, rotate the focused element with the mouse
    if (g_cd.active) return;
    const ImVec2 m = ImGui::GetIO().MousePos;
    const int part = which == 'G' ? H_MOVE : which == 'S' ? H_CORNER : H_ROTATE;
    if (!CanvasBegin(part, 1, 1, m, which)) { Note("G / S / R: focus an element that's on screen first."); return; }
    Note(which == 'G' ? "Grab: move the mouse; click or Enter keeps it, Esc puts it back (Shift: one axis)." :
         which == 'S' ? "Scale: move the mouse from its pivot; click or Enter keeps it, Esc puts it back." :
                        "Rotate: move the mouse round its pivot; click or Enter keeps it, Esc puts it back (Shift: 15 degrees).");
}

// ---- drawing it: hover outline, the focused element's box and handles, the motion path
static void Quad(ImDrawList* dl, const Box& b, ImU32 col, float th) {
    ImVec2 q[4] = { BoxPoint(b, -1, -1), BoxPoint(b, 1, -1), BoxPoint(b, 1, 1), BoxPoint(b, -1, 1) };
    dl->AddPolyline(q, 4, IM_COL32(0, 0, 0, 170), ImDrawFlags_Closed, th + 2.f);
    dl->AddPolyline(q, 4, col, ImDrawFlags_Closed, th);
}
static void MotionPath(ImDrawList* dl) {   // the focused element's centre over the whole clip: a line, a dot per frame
    const int fi = FocusedIndex(); Clip* c = CurClip();
    if (fi < 0 || !c || !Keys(*c)) return;
    Elem& e = Elems()[fi];
    ImVec2 a, b; ElemRect(e, a, b);
    const float k = Unit();
    const ImVec2 c0((a.x + b.x) / 2, (a.y + b.y) / 2), pv(c0.x + (e.type == E_TEXT ? 0.f : e.pivX * k), c0.y + (e.type == E_TEXT ? 0.f : e.pivY * k));
    auto at = [&](float t) {
        Pose p; Sample(*c, t, p);
        ImVec2 q((c0.x - pv.x) * p.sx, (c0.y - pv.y) * p.sy);
        if (Turns(e)) q = Turn(q, p.rot);
        return ImVec2(pv.x + q.x + p.dx * k, pv.y + q.y + p.dy * k);
    };
    std::vector<ImVec2> pts;
    for (float t = 0; t <= c->length + 0.5f; t += FrameMs()) pts.push_back(at(t));
    if (pts.size() > 1) { dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(0, 0, 0, 140), 0, 3.5f); dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(255, 210, 90, 230), 0, 1.5f); }
    for (auto& p : pts) dl->AddCircleFilled(p, 1.8f, IM_COL32(255, 240, 200, 220));
    for (auto& tr : c->tracks) for (auto& kk : tr.keys) if (tr.prop == P_X || tr.prop == P_Y) { const ImVec2 p = at(kk.t); dl->AddCircle(p, 4.5f, IM_COL32(255, 255, 255, 230), 0, 1.5f); }
    const float now = PreviewTime() - (c->name == "In" ? InDelay(e) : 0.f);
    const ImVec2 p = at(ClipTime(*c, now)); dl->AddCircleFilled(p, 4.f, IM_COL32(80, 150, 255, 255));
}
static void Canvas(ImDrawList* dl) {
    if (!g.on || g.live) { if (g_cd.active) CanvasEnd(true); return; }
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 m = io.MousePos;
    const bool overUi = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || (ImGui::IsAnyItemActive() && !g_cd.active);
    const int fi = FocusedIndex();
    if (g.path && !g.playing) MotionPath(dl);
    // a drag / G S R going on
    if (g_cd.active) {
        if (g_cd.modal) {
            CanvasDragTo(m, io.KeyShift, io.KeyCtrl);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) CanvasEnd(false);
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) CanvasEnd(true);
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            CanvasDragTo(m, io.KeyShift, io.KeyCtrl);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) CanvasEnd(true);
        } else CanvasEnd(false);
    }
    // the focused element's box (auto key on: its handles)
    Box fb; const bool hasBox = fi >= 0 && BoxOf(fi, fb);
    int part = H_NONE; float ux = 0, uy = 0;
    if (hasBox) {
        const Elem& e = Elems()[fi];
        const bool handles = g.autoKey || g_cd.active;
        Quad(dl, fb, IM_COL32(255, 160, 50, 255), handles ? 1.5f : 1.f);
        if (handles) {
            for (int kk = 0; kk < 8; ++kk) {
                const float cx = kk < 4 ? (kk & 1 ? 1.f : -1.f) : kk == 4 ? -1.f : kk == 5 ? 1.f : 0.f;
                const float cy = kk < 4 ? (kk & 2 ? 1.f : -1.f) : kk == 6 ? -1.f : kk == 7 ? 1.f : 0.f;
                const ImVec2 p = BoxPoint(fb, cx, cy);
                dl->AddRectFilled(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), IM_COL32(255, 255, 255, 255));
                dl->AddRect(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), IM_COL32(0, 0, 0, 220), 0, 0, 1.f);
            }
            if (Turns(e) && e.type != E_TEXT) {
                const ImVec2 p = fb.piv; const ImU32 oc = IM_COL32(255, 165, 60, 255);
                dl->AddCircle(p, 5, IM_COL32(0, 0, 0, 200), 0, 3.f); dl->AddCircle(p, 5, oc, 0, 1.5f);
                dl->AddLine(ImVec2(p.x - 9, p.y), ImVec2(p.x + 9, p.y), oc, 1.5f); dl->AddLine(ImVec2(p.x, p.y - 9), ImVec2(p.x, p.y + 9), oc, 1.5f);
            }
            if (!overUi && !g_cd.active) part = HandleAt(fb, m, e, ux, uy);
        }
        const char* nm = e.name.c_str();
        const ImVec2 top = BoxPoint(fb, -1, -1);
        dl->AddText(ImVec2(top.x + 1, top.y - ImGui::GetTextLineHeight() - 3), IM_COL32(0, 0, 0, 200), nm);
        dl->AddText(ImVec2(top.x, top.y - ImGui::GetTextLineHeight() - 4), IM_COL32(255, 190, 110, 255), nm);
    }
    if (overUi || g_cd.active) return;
    // hover: what a click would pick
    const std::vector<int> hits = ElemsAt(m);
    const bool onHandle = part != H_NONE && part != H_MOVE;
    if (!hits.empty() && !onHandle && hits[0] != fi) {
        Box hb; if (BoxOf(hits[0], hb)) {
            Quad(dl, hb, IM_COL32(90, 170, 255, 255), 1.f);
            const ImVec2 top = BoxPoint(hb, -1, -1);
            dl->AddText(ImVec2(top.x, top.y - ImGui::GetTextLineHeight() - 4), IM_COL32(150, 200, 255, 255), Elems()[hits[0]].name.c_str());
        }
    }
    static const ImGuiMouseCursor curs[] = { ImGuiMouseCursor_Arrow, ImGuiMouseCursor_ResizeAll, ImGuiMouseCursor_ResizeNWSE, ImGuiMouseCursor_ResizeEW, ImGuiMouseCursor_Hand, ImGuiMouseCursor_Hand };
    if (part == H_CORNER && ux * uy < 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
    else if (part == H_EDGE && ux == 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    else if (part != H_NONE && !(part == H_MOVE && !hits.empty() && hits[0] != fi)) ImGui::SetMouseCursor(curs[part]);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // its handles first (a click inside it moves it only if it's the one under the mouse, or Ctrl is held)
        if (part != H_NONE && (part != H_MOVE || hits.empty() || hits[0] == fi || io.KeyCtrl)) CanvasBegin(part, ux, uy, m);
        else CanvasPick(m, io.KeyAlt);
    }
}
// onion skin: ghosts of the focused element (Shift on the toggle: its group's) at times around the playhead
static void Ghosts(ImDrawList* dl) {
    if (!g.on || g.live || !g.onion || g.playing) return;
    const int fi = FocusedIndex(); Clip* c = CurClip();
    if (fi < 0 || !c) return;
    std::vector<int> who;
    Elem& f = Elems()[fi];
    if (f.type == E_MOTION || g.onionGroup) {
        const std::string grp = f.type == E_MOTION ? f.group : f.group;
        for (int i = 0; i < (int)Elems().size(); ++i) if (Elems()[i].type != E_MOTION && (grp.empty() ? i == fi : InGroup(Elems()[i].group, grp))) who.push_back(i);
    } else who.push_back(fi);
    const float wait = c->name == "In" ? InDelay(f) : 0.f;
    std::vector<std::pair<float, int>> ts;   // (time, -n before / +n after)
    if (g.onionKeys) {
        std::vector<float> keys; for (auto& tr : c->tracks) for (auto& k : tr.keys) keys.push_back(k.t + wait);
        std::sort(keys.begin(), keys.end()); keys.erase(std::unique(keys.begin(), keys.end(), [](float x, float y) { return fabsf(x - y) < 0.5f; }), keys.end());
        int before = 0; for (int i = (int)keys.size() - 1; i >= 0 && before < 2; --i) if (keys[i] < g.time - 0.5f) ts.push_back({ keys[i], -(++before) });
        int after = 0; for (size_t i = 0; i < keys.size() && after < 2; ++i) if (keys[i] > g.time + 0.5f) ts.push_back({ keys[i], ++after });
    } else for (int n = 1; n <= 3; ++n) {
        const float d = n * g.onionStep * FrameMs();
        if (g.time - d >= 0) ts.push_back({ g.time - d, -n });
        ts.push_back({ g.time + d, n });
    }
    std::sort(ts.begin(), ts.end(), [](const std::pair<float, int>& x, const std::pair<float, int>& y) { return abs(x.second) > abs(y.second); });   // (far ones first: under)
    for (auto& [t, n] : ts) {
        g_ghost.on = true; g_ghost.t = t;
        const float blue[3] = { 0.35f, 0.6f, 1.f }, orange[3] = { 1.f, 0.55f, 0.2f };
        for (int i = 0; i < 3; ++i) g_ghost.tint[i] = n < 0 ? blue[i] : orange[i];
        g_ghost.alpha = 0.4f / (float)abs(n);
        for (int i : who) DrawElem(dl, Elems()[i], false);
    }
    g_ghost.on = false;
}

// ================================================================ key diamonds and keying (I / Alt + I)

static int KeyStateAt(const Track& tr, float t) {   // 0: no keys, 1: keys (none here), 2: a key here
    if (tr.keys.empty()) return 0;
    for (auto& k : tr.keys) if (fabsf(k.t - t) < 0.5f) return 2;
    return 1;
}
static void DrawDiamond(ImDrawList* dl, ImVec2 p, float r, int state, bool hot) {
    ImVec2 q[4] = { ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y) };
    const ImU32 on = hot ? IM_COL32(255, 225, 120, 255) : IM_COL32(255, 200, 60, 255), off = hot ? IM_COL32(230, 230, 240, 255) : IM_COL32(150, 150, 160, 255);
    if (state == 2) { dl->AddConvexPolyFilled(q, 4, on); dl->AddPolyline(q, 4, IM_COL32(0, 0, 0, 200), ImDrawFlags_Closed, 1.f); }
    else { dl->AddPolyline(q, 4, state == 1 ? on : off, ImDrawFlags_Closed, 1.5f); if (state == 1) dl->AddCircleFilled(p, r * 0.32f, on); }
}
// a track's diamond on the animation page: its state at the playhead; a click adds or removes the key there
static void KeyDiamond(Clip& c, int p) {
    Track& tr = c.tracks[p];
    const float t = Snap(g.time);
    const int st = KeyStateAt(tr, t);
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool click = ImGui::InvisibleButton("##kd", ImVec2(h, h));
    DrawDiamond(ImGui::GetWindowDrawList(), ImVec2(at.x + h / 2, at.y + h / 2), h * 0.36f, st, ImGui::IsItemHovered());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(st == 2 ? "A key here (filled): click to remove it." : st == 1 ? "Keys on this track, none here: click to key its value here." : "No keys on this track: click to key it here.");
    if (click) {
        if (st == 2) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [&](const Key& k) { return fabsf(k.t - t) < 0.5f; }), tr.keys.end());
        else { SetKey(tr, t, Value(tr, t)); c.on = true; if (t > c.length) c.length = t; }
        g.focus = State::F_TRACK; g.track = p;
    }
}
// I: key the focused element's keyed tracks at their values here (none keyed: move, scale, rotation, alpha); Alt + I: remove
// its keys here
static int KeyHere(bool remove) {
    Clip* c = CurClip();
    if (!c) { Note("Focus an element first (click it on screen or in the list)."); return 0; }
    const float t = Snap(g.time);
    int n = 0;
    if (remove) {
        for (auto& tr : c->tracks) { const size_t was = tr.keys.size(); tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [&](const Key& k) { return fabsf(k.t - t) < 0.5f; }), tr.keys.end()); n += (int)(was - tr.keys.size()); }
        Note(n ? "Removed " + std::to_string(n) + " key" + (n == 1 ? "" : "s") + " at " + TimeText(t) + "." : "No keys at " + TimeText(t) + ".");
        g_dirty = n > 0 || g_dirty;
        return n;
    }
    std::vector<int> props;
    for (int p = 0; p < P_NPROP; ++p) if (!c->tracks[p].keys.empty()) props.push_back(p);
    if (props.empty()) props = { P_X, P_Y, P_SX, P_SY, P_ROT, P_ALPHA };
    for (int p : props) { SetKey(c->tracks[p], t, Value(c->tracks[p], t)); ++n; }
    c->on = true; if (t > c->length) c->length = t;
    Note("Keyed " + std::to_string(n) + " track" + (n == 1 ? "" : "s") + " at " + TimeText(t) + ".");
    g_dirty = true;
    return n;
}
// auto / aligned / vector handles: worked out from the neighbouring keys (a key's in handle is the previous key's h2)
static void ApplyHandles(Track& tr) {
    auto& k = tr.keys; const int n = (int)k.size();
    for (int i = 0; i < n; ++i) {
        const int ht = k[i].ht;
        if (ht != HT_AUTO && ht != HT_VECTOR) continue;
        const bool hasOut = i + 1 < n && k[i].ease == EZ_BEZIER, hasIn = i > 0 && k[i - 1].ease == EZ_BEZIER;
        if (ht == HT_VECTOR) {
            if (hasOut) { k[i].h1x = 1 / 3.f; k[i].h1y = (k[i + 1].v - k[i].v) / 3; }
            if (hasIn) { k[i - 1].h2x = 2 / 3.f; k[i - 1].h2y = (k[i - 1].v - k[i].v) / 3; }
            continue;
        }
        float s = 0;   // (auto, clamped: through its neighbours; flat at the ends and where the curve turns)
        if (i > 0 && i + 1 < n) {
            const float a = k[i].v - k[i - 1].v, b = k[i + 1].v - k[i].v;
            if (a * b > 0) s = (k[i + 1].v - k[i - 1].v) / std::max(0.001f, k[i + 1].t - k[i - 1].t);
        }
        if (hasOut) {
            const float span = std::max(0.001f, k[i + 1].t - k[i].t), lim = fabsf(k[i + 1].v - k[i].v);
            k[i].h1x = 1 / 3.f; k[i].h1y = std::max(-lim, std::min(lim, s * span / 3));
        }
        if (hasIn) {
            const float span = std::max(0.001f, k[i].t - k[i - 1].t), lim = fabsf(k[i].v - k[i - 1].v);
            k[i - 1].h2x = 2 / 3.f; k[i - 1].h2y = std::max(-lim, std::min(lim, -s * span / 3));
        }
    }
}
static void ApplyAllHandles() {
    for (auto& e : Elems()) if (e.an.p) for (auto& c : e.an.p->clips) for (auto& tr : c.tracks) {
        bool any = false; for (auto& k : tr.keys) any |= k.ht == HT_AUTO || k.ht == HT_VECTOR;
        if (any) ApplyHandles(tr);
    }
}
// a handle type for the picked keys (none picked: every key of the tracks the graph shows): they become bezier
static void SetHandleType(Clip& c, int ht) {
    bool any = false; for (auto& tr : c.tracks) for (auto& k : tr.keys) any |= k.sel;
    const std::vector<int> shown = GraphTracks(&c);
    for (int p = 0; p < P_NPROP; ++p) {
        Track& tr = c.tracks[p];
        for (size_t i = 0; i < tr.keys.size(); ++i) {
            Key& k = tr.keys[i];
            if (!EaseTarget(c, any, shown, p, k)) continue;
            if (k.ease != EZ_BEZIER) ToBezier(k, i + 1 < tr.keys.size() ? &tr.keys[i + 1] : nullptr);
            k.ht = ht;
        }
        ApplyHandles(tr);
    }
    g_dirty = true;
}
static void HandleTypeItems(Clip* c) {
    if (!c) return;
    ImGui::TextDisabled("Handles");
    const char* tips[HT_N] = { "each handle on its own", "smooth through the keys beside it (never overshoots)", "in and out on one line", "pointing at the next / previous key: a corner" };
    for (int h = 0; h < HT_N; ++h) { if (ImGui::MenuItem(kHandleType[h])) SetHandleType(*c, h); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tips[h]); }
}

// ================================================================ custom clips
static void CustomClipButton(ElemAnim& an) {
    if (ImGui::Button("+ Custom clip")) ImGui::OpenPopup("##newclip");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A clip of your own (any name): it plays when an arrow of the state graph leads to it.");
    if (ImGui::BeginPopup("##newclip")) {
        static char nm[48] = "";
        ImGui::TextUnformatted("Name of the new clip");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##nm", nm, sizeof nm, ImGuiInputTextFlags_EnterReturnsTrue);
        bool taken = false; for (auto& c : an.clips) taken |= c.name == nm;
        ImGui::BeginDisabled(!nm[0] || taken);
        if (ImGui::Button("Add") || (enter && nm[0] && !taken)) {
            Clip k; k.name = nm; k.trigger = "(custom: by the state graph's arrows)"; k.on = true;
            for (int p = 0; p < P_NPROP; ++p) k.tracks[p].prop = p;
            an.clips.push_back(k); g.clip = (int)an.clips.size() - 1; g.focus = State::F_CLIP; nm[0] = 0; g_dirty = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (taken) ImGui::TextDisabled("It has a clip of that name.");
        ImGui::EndPopup();
    }
}

// ================================================================ the dope sheet (the timeline's Screen mode)
// Every element's clip of the name being edited, by group: In's rows wait their delay and stagger (hatched) first.

static Clip* ClipOfName(Elem& e, const std::string& name) {
    if (!e.an.p) return nullptr;
    for (auto& c : e.an.p->clips) if (c.name == name) return &c;
    return nullptr;
}
static float SheetWait(const Elem& e, const std::string& name) { return name == "In" ? InDelay(e) : 0.f; }
static bool KeySpan(const Clip& c, float& first, float& last) {
    first = 1e9f; last = -1e9f;
    for (auto& tr : c.tracks) for (auto& k : tr.keys) { first = std::min(first, k.t); last = std::max(last, k.t); }
    return first <= last;
}
static void ForSheetClips(const std::function<void(Elem&, Clip&)>& f) {   // every element's clip of the name being edited
    const std::string n = PreviewName();
    for (auto& e : Elems()) if (Clip* c = ClipOfName(e, n)) f(e, *c);
}
static void ClearSheetPicks() { ForSheetClips([](Elem&, Clip& c) { for (auto& tr : c.tracks) for (auto& k : tr.keys) k.sel = false; }); }
static int SheetPicked() { int n = 0; ForSheetClips([&](Elem&, Clip& c) { n += PickedKeys(c); }); return n; }
static int PickAtTime(int elem, int prop, float t, bool toggle) {   // an element row's diamond (prop -1: every track), or one key
    if (elem < 0 || elem >= (int)Elems().size()) return 0;
    Elem& e = Elems()[elem];
    Clip* c = ClipOfName(e, PreviewName());
    if (!c) return 0;
    int n = 0;
    for (auto& tr : c->tracks) {
        if (prop >= 0 && tr.prop != prop) continue;
        for (auto& k : tr.keys) if (fabsf(k.t - t) < 0.5f) { k.sel = toggle ? !k.sel : true; ++n; }
    }
    return n;
}
static void FinishSheetEdit() {   // (after moves: keys in order, clips long enough, handles worked out)
    ForSheetClips([](Elem&, Clip& c) {
        float last = 0;
        for (auto& tr : c.tracks) { SortKeys(tr); ApplyHandles(tr); for (auto& k : tr.keys) last = std::max(last, k.t); }
        if (last > c.length) c.length = last;
    });
    g_dirty = true;
}
static void DeleteSheetPicked() {
    ForSheetClips([](Elem&, Clip& c) { for (auto& tr : c.tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end()); });
    g_dirty = true;
}
// the picked keys, every element: moved by dt ms (never before 0)
struct MovingKey { Clip* c; int prop; int idx; float t0; };
static std::vector<MovingKey> PickedKeyList(bool copy) {
    std::vector<MovingKey> out;
    ForSheetClips([&](Elem&, Clip& c) {
        for (int p = 0; p < P_NPROP; ++p) {
            Track& tr = c.tracks[p];
            const int n = (int)tr.keys.size();
            for (int i = 0; i < n; ++i) if (tr.keys[i].sel) {
                if (copy) { Key k = tr.keys[i]; k.sel = false; tr.keys.push_back(k); }   // (Alt: a copy stays behind)
                out.push_back({ &c, p, i, tr.keys[i].t });
            }
        }
    });
    return out;
}
static void MoveKeysBy(const std::vector<MovingKey>& mk, float dt) {
    float lo = 1e9f; for (auto& k : mk) lo = std::min(lo, k.t0);
    if (!mk.empty()) dt = std::max(dt, -lo);
    for (auto& k : mk) { Track& tr = k.c->tracks[k.prop]; if (k.idx < (int)tr.keys.size()) tr.keys[k.idx].t = std::max(0.f, Snap(k.t0 + dt)); }
}
// a whole clip moved in time: In by its delay (its keys stay as they are), the others' keys shifted
static void MoveClipBy(int elem, float d0, const std::vector<float>& keys0, float dt) {
    Elem& e = Elems()[elem]; const std::string n = PreviewName();
    Clip* c = ClipOfName(e, n);
    if (!c || !e.an.p) return;
    if (n == "In") { e.an.p->p.delay = std::max(0.f, roundf(d0 + dt)); return; }
    float lo = 1e9f; for (float t : keys0) lo = std::min(lo, t);
    if (!keys0.empty()) dt = std::max(dt, -lo);
    size_t i = 0;
    for (auto& tr : c->tracks) for (auto& k : tr.keys) { if (i < keys0.size()) k.t = std::max(0.f, Snap(keys0[i] + dt)); ++i; }
}
static std::vector<float> ClipKeyTimes(const Clip& c) { std::vector<float> ts; for (auto& tr : c.tracks) for (auto& k : tr.keys) ts.push_back(k.t); return ts; }
// stretched or squeezed about one end (fixed, clip time): its keys' times scaled by f
static void StretchClip(Clip& c, const std::vector<float>& keys0, float fixed, float f) {
    f = std::max(0.02f, f);
    size_t i = 0;
    for (auto& tr : c.tracks) for (auto& k : tr.keys) { if (i < keys0.size()) k.t = std::max(0.f, Snap(fixed + (keys0[i] - fixed) * f)); ++i; }
}
static int MotionOfGroup(const std::string& grp) { auto& els = Elems(); for (int i = 0; i < (int)els.size(); ++i) if (els[i].type == E_MOTION && els[i].group == grp) return i; return -1; }
// rows with picked keys (the sheet's order): their In starts lined up with the earliest / staggered by `step`
static std::vector<int> SheetOrder();
static void AlignStarts(float step) {   // step < 0: lined up; else each row `step` ms after the one above
    const std::string n = PreviewName();
    std::vector<int> rows;
    for (int i : SheetOrder()) { Elem& e = Elems()[i]; Clip* c = ClipOfName(e, n); if (c && PickedKeys(*c)) rows.push_back(i); }
    if (rows.size() < 2 || n != "In") { Note(n != "In" ? "Align / stagger: In's rows (their delay)." : "Pick keys on two rows or more first."); return; }
    float base = 1e9f;
    for (int i : rows) { Elem& e = Elems()[i]; float f, l; if (KeySpan(*ClipOfName(e, n), f, l)) base = std::min(base, InDelay(e) + f); }
    for (size_t r = 0; r < rows.size(); ++r) {
        Elem& e = Elems()[rows[r]]; float f, l; KeySpan(*ClipOfName(e, n), f, l);
        const float want = base + (step > 0 ? step * r : 0.f), other = InDelay(e) - e.an.p->p.delay;   // (its groups' stagger)
        e.an.p->p.delay = std::max(0.f, roundf(want - f - other));
    }
    g_dirty = true;
    Note(step > 0 ? "Staggered " + std::to_string(rows.size()) + " rows by " + std::to_string((int)step) + " ms." : "Lined up the starts of " + std::to_string(rows.size()) + " rows.");
}
static std::vector<int> SheetOrder() {   // the layout's elements by group (as the element list)
    auto& els = Elems();
    std::vector<int> order; for (int i = 0; i < (int)els.size(); ++i) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return els[x].group < els[y].group; });
    return order;
}
struct SheetRow { int kind; std::string group; int elem; int prop; int depth; };   // kind: 0 group, 1 element, 2 a track of it
static std::vector<SheetRow> SheetRows() {
    std::vector<SheetRow> rows;
    auto& els = Elems(); const std::string n = PreviewName();
    std::set<std::string> emitted;
    for (int i : SheetOrder()) {
        Elem& e = els[i];
        if (g.sheetFilter[0] && (e.group + "/" + e.name).find(g.sheetFilter) == std::string::npos) continue;
        Clip* c = ClipOfName(e, n);
        const bool keyed = c && Keys(*c);
        if (g.keyedOnly && !keyed) continue;
        bool hidden = false; int depth = 0;
        if (!e.group.empty()) {
            size_t p = 0;
            while (true) {
                const size_t q = e.group.find('/', p);
                const std::string pre = e.group.substr(0, q);
                if (!hidden && !emitted.count(pre)) { emitted.insert(pre); rows.push_back({ 0, pre, -1, -1, depth }); }
                if (g.shutGroups.count(pre)) hidden = true;
                ++depth;
                if (q == std::string::npos) break;
                p = q + 1;
            }
        }
        if (hidden) continue;
        rows.push_back({ 1, e.group, i, -1, depth });
        if (keyed && g.openRows.count(KeyOf(g_editMode, e)))
            for (int p = 0; p < P_NPROP; ++p) if (!c->tracks[p].keys.empty()) rows.push_back({ 2, e.group, i, p, depth + 1 });
    }
    return rows;
}
static void ScreenSheet(ImDrawList* dl, ImVec2 o, ImVec2 ra, ImVec2 rb, float rulerH, float rowH, bool hov, bool& scrubbing, float& rowScroll) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 m = io.MousePos;
    auto X = [&](float t) { return ra.x + (t - g.tl0) / (g.tl1 - g.tl0) * (rb.x - ra.x); };
    auto T = [&](float x) { return g.tl0 + (x - ra.x) / (rb.x - ra.x) * (g.tl1 - g.tl0); };
    auto& els = Elems(); const std::string name = PreviewName();
    const std::vector<SheetRow> rows = SheetRows();
    {   const float rowsH = rowH * rows.size(), roomH = rb.y - ra.y - rulerH; rowScroll = std::max(0.f, std::min(rowScroll, std::max(0.f, rowsH - roomH))); }
    // what's under the mouse
    enum { SH_NONE, SH_DIAMOND, SH_BAR, SH_BAREND, SH_STAGGER, SH_ROW, SH_NAME, SH_FOLD, SH_OPEN };
    int hit = SH_NONE, hitRow = -1, hitProp = -1, hitEnd = 0; float hitT = 0;
    struct Dia { ImVec2 p; int elem, prop; float t; };
    std::vector<Dia> dias;
    static int mode = 0;   // 1 keys, 2 a clip moved, 3 stretched, 4 stagger, 5 box pick
    static ImVec2 from; static std::vector<MovingKey> mk; static int dElem = -1; static float d0 = 0, dFixed = 0, dEnd0 = 0;
    static std::vector<float> keys0; static std::string dGroup;
    const float top = ra.y + rulerH;
    dl->PushClipRect(ImVec2(o.x, top), rb, true);
    float y = top - rowScroll;
    const int fi = FocusedIndex();
    if (rows.empty()) dl->AddText(ImVec2(o.x + 8, y + 6), IM_COL32(150, 150, 150, 255), g.keyedOnly ? "No element has keys in this clip (keyed only is on)." : "No elements in this layout.");
    for (int ri = 0; ri < (int)rows.size(); ++ri, y += rowH) {
        const SheetRow& r = rows[ri];
        const bool over = hov && m.y >= y && m.y < y + rowH && m.y >= top;
        if (ri & 1) dl->AddRectFilled(ImVec2(o.x, y), ImVec2(rb.x, y + rowH), IM_COL32(255, 255, 255, 6));
        const float ind = 8 + r.depth * 12.f;
        if (r.kind == 0) {   // a group: its members' span, its motion's stagger handle
            const bool shut = g.shutGroups.count(r.group) > 0;
            dl->AddText(ImVec2(o.x + ind, y + 3), IM_COL32(255, 165, 75, 255), shut ? "+" : "-");
            dl->AddText(ImVec2(o.x + ind + 12, y + 3), IM_COL32(255, 165, 75, 255), GroupLeaf(r.group).c_str());
            float lo = 1e9f, hi = -1e9f;
            for (auto& e : els) { if (e.type == E_MOTION || !InGroup(e.group, r.group)) continue; Clip* c = ClipOfName(e, name); float f, l; if (c && KeySpan(*c, f, l)) { const float w = SheetWait(e, name); lo = std::min(lo, w + f); hi = std::max(hi, w + l); } }
            if (lo <= hi) dl->AddRectFilled(ImVec2(X(lo), y + rowH * 0.35f), ImVec2(std::max(X(hi), X(lo) + 2), y + rowH * 0.65f), IM_COL32(255, 165, 75, 70), 2.f);
            const int mi = MotionOfGroup(r.group);
            if (name == "In" && lo <= hi) {   // the stagger tab: drag it to fan the rows out
                const float st = mi >= 0 && els[mi].an.p ? els[mi].an.p->p.stagger : 0.f;
                float f0 = lo;   // (the first member's start: it doesn't move with the stagger)
                const float tx = X(f0 + st);
                const ImVec2 q[3] = { ImVec2(tx, y + 2), ImVec2(tx + 6, y + rowH * 0.5f), ImVec2(tx, y + rowH - 2) };
                const bool hot = over && fabsf(m.x - tx - 3) <= 6;
                dl->AddConvexPolyFilled(q, 3, hot ? IM_COL32(255, 220, 120, 255) : IM_COL32(255, 165, 75, 220));
                if (hot && m.x >= ra.x) { hit = SH_STAGGER; hitRow = ri; hitT = f0; }
            }
            if (over && m.x < ra.x) { hit = m.x < o.x + ind + 10 ? SH_FOLD : SH_NAME; hitRow = ri; }
            continue;
        }
        Elem& e = els[r.elem];
        Clip* c = ClipOfName(e, name);
        const bool keyed = c && Keys(*c);
        const float wait = SheetWait(e, name);
        if (r.kind == 1) {
            if (r.elem == fi) dl->AddRectFilled(ImVec2(o.x, y), ImVec2(rb.x, y + rowH), IM_COL32(80, 150, 255, 28));
            if (keyed) dl->AddText(ImVec2(o.x + ind, y + 3), IM_COL32(170, 170, 180, 255), g.openRows.count(KeyOf(g_editMode, e)) ? "v" : ">");
            dl->AddText(ImVec2(o.x + ind + 12, y + 3), keyed ? IM_COL32(225, 225, 225, 255) : IM_COL32(120, 120, 130, 255), e.name.c_str());
            if (over && m.x < ra.x) { hit = keyed && m.x < o.x + ind + 10 ? SH_OPEN : SH_NAME; hitRow = ri; }
            if (!keyed) { if (over && m.x >= ra.x && hit == SH_NONE) { hit = SH_ROW; hitRow = ri; } continue; }
            float f, l; KeySpan(*c, f, l);
            if (wait > 0.5f) {   // its wait (delay and stagger): hatched
                const float x0 = X(0), x1 = X(wait);
                dl->PushClipRect(ImVec2(std::max(x0, ra.x), y + 3), ImVec2(std::min(x1, rb.x), y + rowH - 3), true);
                for (float hx = x0 - rowH; hx < x1; hx += 6) dl->AddLine(ImVec2(hx, y + rowH - 3), ImVec2(hx + rowH - 6, y + 3), IM_COL32(255, 255, 255, 40), 1.f);
                dl->PopClipRect();
            }
            const float bx0 = X(wait + f), bx1 = std::max(X(wait + l), bx0 + 3);
            const bool barHot = over && m.x >= bx0 - 4 && m.x <= bx1 + 4;
            dl->AddRectFilled(ImVec2(bx0, y + rowH * 0.3f), ImVec2(bx1, y + rowH * 0.7f), barHot ? IM_COL32(110, 170, 255, 110) : IM_COL32(110, 170, 255, 60), 2.f);
            std::vector<float> ts; for (auto& tr : c->tracks) for (auto& k : tr.keys) ts.push_back(k.t);
            std::sort(ts.begin(), ts.end()); ts.erase(std::unique(ts.begin(), ts.end(), [](float a, float b) { return fabsf(a - b) < 0.5f; }), ts.end());
            for (float t : ts) {
                bool sel = false; for (auto& tr : c->tracks) for (auto& k : tr.keys) if (fabsf(k.t - t) < 0.5f && k.sel) sel = true;
                const ImVec2 p(X(wait + t), y + rowH * 0.5f);
                DrawDiamond(dl, p, 5, 2, false);
                if (sel) { ImVec2 q[4] = { ImVec2(p.x, p.y - 5), ImVec2(p.x + 5, p.y), ImVec2(p.x, p.y + 5), ImVec2(p.x - 5, p.y) }; dl->AddConvexPolyFilled(q, 4, IM_COL32(255, 200, 60, 255)); }
                else { ImVec2 q[4] = { ImVec2(p.x, p.y - 4), ImVec2(p.x + 4, p.y), ImVec2(p.x, p.y + 4), ImVec2(p.x - 4, p.y) }; dl->AddConvexPolyFilled(q, 4, IM_COL32(230, 230, 235, 255)); }
                dias.push_back({ p, r.elem, -1, t });
                if (over && fabsf(m.x - p.x) <= 6) { hit = SH_DIAMOND; hitRow = ri; hitT = t; hitProp = -1; }
            }
            for (auto& ev : c->events) {   // (its events / markers: small flags)
                const float ex = X(wait + ev.t);
                const ImVec2 q[3] = { ImVec2(ex, y + 1), ImVec2(ex + 6, y + 4), ImVec2(ex, y + 7) };
                dl->AddConvexPolyFilled(q, 3, ev.marker ? IM_COL32(230, 230, 230, 220) : IM_COL32(255, 120, 200, 230));
            }
            if (hit != SH_DIAMOND && barHot && m.x >= ra.x) {
                if (fabsf(m.x - bx0) <= 4 && bx1 - bx0 > 10) { hit = SH_BAREND; hitEnd = 0; } else if (fabsf(m.x - bx1) <= 4 && bx1 - bx0 > 10) { hit = SH_BAREND; hitEnd = 1; } else hit = SH_BAR;
                hitRow = ri;
            }
            if (over && m.x >= ra.x && hit == SH_NONE) { hit = SH_ROW; hitRow = ri; }
        } else {   // a track of it
            dl->AddRectFilled(ImVec2(o.x + ind, y + 5), ImVec2(o.x + ind + 4, y + rowH - 5), kProp[r.prop].col);
            dl->AddText(ImVec2(o.x + ind + 9, y + 3), IM_COL32(200, 200, 200, 255), kProp[r.prop].name);
            const Track& tr = c->tracks[r.prop];
            for (size_t i = 0; i < tr.keys.size(); ++i) {
                const Key& k = tr.keys[i];
                const ImVec2 p(X(wait + k.t), y + rowH * 0.5f);
                if (i + 1 < tr.keys.size()) dl->AddLine(p, ImVec2(X(wait + tr.keys[i + 1].t), p.y), (kProp[r.prop].col & 0x00FFFFFF) | 0x60000000, 2.f);
                ImVec2 q[4] = { ImVec2(p.x, p.y - 4), ImVec2(p.x + 4, p.y), ImVec2(p.x, p.y + 4), ImVec2(p.x - 4, p.y) };
                dl->AddConvexPolyFilled(q, 4, k.sel ? IM_COL32(255, 200, 60, 255) : IM_COL32(220, 220, 225, 255));
                dias.push_back({ p, r.elem, r.prop, k.t });
                if (over && fabsf(m.x - p.x) <= 5) { hit = SH_DIAMOND; hitRow = ri; hitT = k.t; hitProp = r.prop; }
            }
            if (over && m.x < ra.x) { hit = SH_NAME; hitRow = ri; }
            if (over && m.x >= ra.x && hit == SH_NONE) { hit = SH_ROW; hitRow = ri; }
        }
    }
    dl->PopClipRect();
    if (mode == 5) {   // box pick
        const ImVec2 a(std::min(from.x, m.x), std::min(from.y, m.y)), b(std::max(from.x, m.x), std::max(from.y, m.y));
        dl->AddRectFilled(a, b, IM_COL32(110, 170, 255, 40)); dl->AddRect(a, b, IM_COL32(110, 170, 255, 200));
    }
    // ---- input
    const bool inRuler = m.y < top;
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const SheetRow* r = hitRow >= 0 ? &rows[hitRow] : nullptr;
        if (inRuler && m.x >= ra.x) scrubbing = true;
        else if (hit == SH_FOLD && r) { if (g.shutGroups.count(r->group)) g.shutGroups.erase(r->group); else g.shutGroups.insert(r->group); }
        else if (hit == SH_OPEN && r) { const std::string k = KeyOf(g_editMode, els[r->elem]); if (g.openRows.count(k)) g.openRows.erase(k); else g.openRows.insert(k); }
        else if (hit == SH_NAME && r) {
            if (r->kind == 0) FocusMotion(r->group);
            else { FocusElem(r->elem); if (r->kind == 2) { g.focus = State::F_TRACK; g.track = r->prop; } else g.focus = State::F_ELEMENT; }
        } else if (hit == SH_DIAMOND && r) {
            Clip* c = ClipOfName(els[r->elem], name);
            bool was = false; if (c) for (auto& tr : c->tracks) for (auto& k : tr.keys) if ((hitProp < 0 || tr.prop == hitProp) && fabsf(k.t - hitT) < 0.5f && k.sel) was = true;
            if (!io.KeyShift && !was) ClearSheetPicks();
            PickAtTime(r->elem, hitProp, hitT, io.KeyShift);
            FocusElem(r->elem);
            mode = 1; from = m; mk = PickedKeyList(io.KeyAlt);
        } else if (hit == SH_STAGGER && r) {
            if (MotionOfGroup(r->group) < 0) FocusMotion(r->group);
            const int mi = MotionOfGroup(r->group);
            if (mi >= 0 && els[mi].an.p) { mode = 4; from = m; dGroup = r->group; d0 = els[mi].an.p->p.stagger; }
        } else if ((hit == SH_BAR || hit == SH_BAREND) && r) {
            Clip* c = ClipOfName(els[r->elem], name);
            FocusElem(r->elem);
            if (c) {
                dElem = r->elem; from = m; keys0 = ClipKeyTimes(*c); d0 = els[r->elem].an.p->p.delay;
                float f, l; KeySpan(*c, f, l);
                if (hit == SH_BAR) mode = 2;
                else { mode = 3; dFixed = hitEnd ? f : l; dEnd0 = hitEnd ? l : f; }
            }
        } else if (m.x >= ra.x) {   // an empty part: focus the row, clear the picks, drag: box pick
            if (r && r->kind != 0) FocusElem(r->elem);
            if (!io.KeyShift) ClearSheetPicks();
            mode = 5; from = m;
        }
    }
    if (mode && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float raw = T(m.x) - T(from.x);
        if (mode == 1) MoveKeysBy(mk, raw);
        else if (mode == 2 && dElem >= 0) MoveClipBy(dElem, d0, keys0, raw);
        else if (mode == 3 && dElem >= 0) { if (Clip* c = ClipOfName(els[dElem], name)) { const float span = dEnd0 - dFixed; if (fabsf(span) > 0.5f) StretchClip(*c, keys0, dFixed, (span + raw) / span); } }
        else if (mode == 4) { const int mi = MotionOfGroup(dGroup); if (mi >= 0 && els[mi].an.p) els[mi].an.p->p.stagger = std::max(0.f, roundf(d0 + raw)); }
        if (mode != 5) g.dragOn = true;
    } else if (mode) {
        if (mode == 5 && (fabsf(m.x - from.x) > 3 || fabsf(m.y - from.y) > 3)) {
            const ImVec2 a(std::min(from.x, m.x), std::min(from.y, m.y)), b(std::max(from.x, m.x), std::max(from.y, m.y));
            for (auto& d : dias) if (d.p.x >= a.x && d.p.x <= b.x && d.p.y >= a.y && d.p.y <= b.y) PickAtTime(d.elem, d.prop, d.t, false);
        } else if (mode == 5 && hov) { g.time = std::max(0.f, Snap(T(m.x))); g.playing = false; }   // (a click: the playhead too)
        if (mode != 5) FinishSheetEdit();
        mode = 0; mk.clear(); dElem = -1; g.dragOn = false;
    }
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m.x >= ra.x && !inRuler) {
        if (hit == SH_DIAMOND && hitRow >= 0) {
            Clip* c = ClipOfName(els[rows[hitRow].elem], name);
            bool was = false; if (c) for (auto& tr : c->tracks) for (auto& k : tr.keys) if (fabsf(k.t - hitT) < 0.5f && k.sel) was = true;
            if (!was) { ClearSheetPicks(); PickAtTime(rows[hitRow].elem, hitProp, hitT, false); }
            FocusElem(rows[hitRow].elem);
        }
        ImGui::OpenPopup("##sheetmenu");
    }
    if (ImGui::BeginPopup("##sheetmenu")) {
        const int n = SheetPicked();
        ImGui::TextDisabled("%d key%s picked", n, n == 1 ? "" : "s");
        if (ImGui::BeginMenu("Way to the next key", n > 0)) {
            for (int ez = 0; ez < EZ_NEASE; ++ez) if (ImGui::MenuItem(kEase[ez])) { ForSheetClips([&](Elem&, Clip& c) { if (PickedKeys(c)) ApplyEase(c, ez); }); g_dirty = true; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Handles", n > 0)) {
            for (int h = 0; h < HT_N; ++h) if (ImGui::MenuItem(kHandleType[h])) ForSheetClips([&](Elem&, Clip& c) { if (PickedKeys(c)) SetHandleType(c, h); });
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Delete picked keys", "Delete", false, n > 0)) DeleteSheetPicked();
        ImGui::Separator();
        KeyClipItems();
        ImGui::Separator();
        if (ImGui::MenuItem("Align starts", nullptr, false, name == "In")) AlignStarts(-1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The rows with picked keys: their In starts lined up with the earliest (by their delay).");
        static float step = 100;
        ImGui::SetNextItemWidth(90); ImGui::DragFloat("##stb", &step, 2, 0, 2000, "%.0f ms"); ImGui::SameLine();
        if (ImGui::MenuItem("Stagger by", nullptr, false, name == "In")) AlignStarts(std::max(1.f, step));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The rows with picked keys start this far apart, top to bottom (by their delay).");
        ImGui::EndPopup();
    }
    if (hov && hit == SH_STAGGER) ImGui::SetTooltip("Drag: the group's stagger (its members start this far apart, top to bottom).");
    else if (hov && hit == SH_BAR) ImGui::SetTooltip(name == "In" ? "Drag: move its In in time (its delay; the keys stay as they are)." : "Drag: move this clip's keys in time.");
    else if (hov && hit == SH_BAREND) ImGui::SetTooltip("Drag: stretch or squeeze its keys about the other end.");
    else if (hov && hit == SH_DIAMOND) ImGui::SetTooltip("Click / Shift-click: pick; drag: move the picked keys (Alt: copies); right-click: easing, handles, delete.");
    else if (hov && m.x < ra.x) ImGui::SetTooltip("Click a name: focus it (a group: its motion). + / -: fold a group, > : its tracks. Wheel: scroll.");
    else if (hov && !inRuler) ImGui::SetTooltip("Drag across diamonds: pick them. Right-click: align / stagger the picked rows. Tab: back to the element's tracks.");
}

// ================================================================ events and markers on the ruler (Element mode)
static int g_evEdit = -1;   // (the event being edited in its popup)
static bool RulerEvents(ImDrawList* dl, ImVec2 ra, ImVec2 rb, float rulerH, bool hov) {   // true: it took the click
    Clip* c = CurClip();
    if (!c) return false;
    ImGuiIO& io = ImGui::GetIO(); const ImVec2 m = io.MousePos;
    auto X = [&](float t) { return ra.x + (t - g.tl0) / (g.tl1 - g.tl0) * (rb.x - ra.x); };
    auto T = [&](float x) { return g.tl0 + (x - ra.x) / (rb.x - ra.x) * (g.tl1 - g.tl0); };
    int hitEv = -1;
    dl->PushClipRect(ra, rb, true);
    for (int i = 0; i < (int)c->events.size(); ++i) {
        const Event& ev = c->events[i];
        const float x = X(ev.t);
        const ImU32 col = ev.sel ? IM_COL32(255, 200, 60, 255) : ev.marker ? IM_COL32(230, 230, 230, 230) : IM_COL32(255, 120, 200, 240);
        dl->AddLine(ImVec2(x, ra.y), ImVec2(x, rb.y), (col & 0x00FFFFFF) | 0x38000000, 1.f);
        const ImVec2 q[3] = { ImVec2(x, ra.y + 1), ImVec2(x + 8, ra.y + 5), ImVec2(x, ra.y + 9) };
        dl->AddConvexPolyFilled(q, 3, col);
        dl->AddLine(ImVec2(x, ra.y), ImVec2(x, ra.y + rulerH), col, 1.5f);
        const std::string lb = !ev.name.empty() ? ev.name : ev.marker ? std::string("marker") : ev.cmd;
        dl->AddText(ImVec2(x + 10, ra.y + 1), col, lb.substr(0, 24).c_str());
        if (hov && m.y < ra.y + rulerH && fabsf(m.x - x) <= 5) hitEv = i;
    }
    dl->PopClipRect();
    static int drag = -1; static float dFrom = 0; static std::vector<float> t0;
    bool took = false;
    if (hitEv >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { g_evEdit = hitEv; ImGui::OpenPopup("##evedit"); took = true; }
    else if (hitEv >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Event& ev = c->events[hitEv];
        if (!io.KeyShift && !ev.sel) for (auto& o : c->events) o.sel = false;
        ev.sel = io.KeyShift ? !ev.sel : true;
        drag = hitEv; dFrom = m.x; t0.clear(); for (auto& o : c->events) t0.push_back(o.t);
        took = true;
    }
    if (drag >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && t0.size() == c->events.size()) {
            const float dt = T(m.x) - T(dFrom);
            for (size_t i = 0; i < c->events.size(); ++i) if (c->events[i].sel) c->events[i].t = std::max(0.f, Snap(t0[i] + dt));
            took = true;
        } else { std::stable_sort(c->events.begin(), c->events.end(), [](const Event& a, const Event& b) { return a.t < b.t; }); drag = -1; g_dirty = true; }
    }
    static float addAt = 0;
    if (hov && m.y < ra.y + rulerH && m.x >= ra.x && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (hitEv >= 0) { g_evEdit = hitEv; ImGui::OpenPopup("##evedit"); }
        else { addAt = std::max(0.f, Snap(T(m.x))); ImGui::OpenPopup("##rulermenu"); }
        took = true;
    }
    if (ImGui::BeginPopup("##rulermenu")) {
        ImGui::TextDisabled("At %s", TimeText(addAt).c_str());
        auto add = [&](bool marker) {
            Event ev; ev.t = addAt; ev.marker = marker; if (marker) ev.name = "marker";
            c->events.push_back(ev);
            std::stable_sort(c->events.begin(), c->events.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
            for (int i = 0; i < (int)c->events.size(); ++i) if (fabsf(c->events[i].t - addAt) < 0.5f && c->events[i].marker == marker) g_evEdit = i;
            g_dirty = true;
        };
        if (ImGui::MenuItem("Add event here")) { add(false); ImGui::CloseCurrentPopup(); ImGui::EndPopup(); ImGui::OpenPopup("##evedit"); goto edit; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A command that runs as the clip passes this time in the game (a sound, a game call).");
        if (ImGui::MenuItem("Add marker here", "M")) { add(true); ImGui::CloseCurrentPopup(); ImGui::EndPopup(); ImGui::OpenPopup("##evedit"); goto edit; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A named time for notes (\"bar lands\"): no command.");
        ImGui::EndPopup();
    }
edit:
    if (ImGui::BeginPopup("##evedit")) {
        if (g_evEdit < 0 || g_evEdit >= (int)c->events.size()) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return took; }
        Event& ev = c->events[g_evEdit];
        int kind = ev.marker ? 1 : 0;
        if (ImGui::RadioButton("event", &kind, 0)) ev.marker = false; ImGui::SameLine();
        if (ImGui::RadioButton("marker", &kind, 1)) ev.marker = true;
        char nb[64]; snprintf(nb, sizeof nb, "%s", ev.name.c_str());
        ImGui::SetNextItemWidth(260); if (ImGui::InputText("name##ev", nb, sizeof nb)) { ev.name = nb; g_dirty = true; }
        if (!ev.marker) {
            char cb[256]; snprintf(cb, sizeof cb, "%s", ev.cmd.c_str());
            ImGui::SetNextItemWidth(260); if (ImGui::InputTextWithHint("command##ev", "fe:Audio:playSound(\"UI_Whoosh\")", cb, sizeof cb)) { ev.cmd = cb; g_dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Anything a menu tile's action can do (Joy Ride: fe: commands).");
        }
        float t = ev.t; ImGui::SetNextItemWidth(120);
        if (ImGui::DragFloat("time##ev", &t, 2, 0, 100000, "%.0f ms")) { ev.t = std::max(0.f, t); g_dirty = true; }
        if (!ev.marker) { ImGui::SameLine(); if (ImGui::Button("Run it now")) RunEvent(ev); }
        if (ImGui::Button("Delete")) { c->events.erase(c->events.begin() + g_evEdit); g_evEdit = -1; g_dirty = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Done")) { std::stable_sort(c->events.begin(), c->events.end(), [](const Event& a, const Event& b) { return a.t < b.t; }); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (hov && hitEv >= 0) ImGui::SetTooltip("Drag: move it. Double-click / right-click: edit it. Delete: remove the picked ones.");
    return took;
}
static void AddMarkerHere() {
    Clip* c = CurClip(); if (!c) return;
    Event ev; ev.t = std::max(0.f, Snap(g.time)); ev.marker = true; ev.name = "marker";
    c->events.push_back(ev); std::stable_sort(c->events.begin(), c->events.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
    g_dirty = true; Note("Marker at " + TimeText(ev.t) + " (double-click it on the ruler to name it).");
}
// animation mode, playing with "play events" on: the events the playhead passes on every element (In: after its wait)
static void PreviewEvents(float t0, float t1) {
    if (!g.playEvents || t1 <= t0) return;
    const std::string n = PreviewName();
    for (auto& e : Elems()) {
        const Clip* c = ClipNamed(e.an.p, n);
        if (!c) continue;
        const float w = SheetWait(e, n);
        for (auto& ev : c->events) if (!ev.marker && ev.t + w > t0 && ev.t + w <= t1) RunEvent(ev);
    }
}

// ================================================================ the state graph (the panels window's second tab)
static void StateGraph() {
    const int fi = FocusedIndex(); ElemAnim* an = Focused();
    if (fi < 0 || !an) { ImGui::TextDisabled("Pick an element to see its clips and triggers."); return; }
    Elem& e = Elems()[fi];
    ImGui::Text("%s", e.name.c_str()); ImGui::SameLine();
    ImGui::TextDisabled("its clips and what starts them");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Boxes: its clips (lit: playing now in Live / the game, or the one being edited). Arrows: what starts them.\nClick a box: edit that clip; an arrow's label: its trigger on the page. Drag box to box: a transition of its own.");
    const float u = ImGui::GetFontSize() / 13.f;
    const float W = std::max(420 * u, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize);   // (narrow: it scrolls)
    ImGui::BeginChild("##sgscroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    std::vector<int> custom;
    for (int i = 0; i < (int)an->clips.size(); ++i) { bool std_ = false; for (auto& k : kClips) std_ |= an->clips[i].name == k[0]; if (!std_) custom.push_back(i); }
    const float laneH = 64 * u, H = laneH * (custom.empty() ? 3 : 4) + 8;
    ImGui::InvisibleButton("##sg", ImVec2(W, H));
    const bool hov = ImGui::IsItemHovered();
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float bw = std::min(130 * u, (W - 40) / 4.6f), bh = 30 * u, gap = (W - 24 - 4 * bw) / 3;
    auto colX = [&](float col) { return o.x + 12 + col * (bw + gap); };
    std::map<std::string, ImVec2> at;   // box centres by clip name
    const char* laneName[4] = { "Base: plays while shown", "State layer: focus and press", "On top of both", "Custom clips" };
    for (int l = 0; l < (custom.empty() ? 3 : 4); ++l) {
        const float y0 = o.y + l * laneH;
        dl->AddRectFilled(ImVec2(o.x, y0 + 2), ImVec2(o.x + W, y0 + laneH - 2), IM_COL32(255, 255, 255, 8), 6.f);
        dl->AddText(ImVec2(o.x + 8, y0 + 4), IM_COL32(150, 150, 160, 255), laneName[l]);
    }
    auto place = [&](const char* nm, int lane, float col) { at[nm] = ImVec2(colX(col) + bw / 2, o.y + lane * laneH + laneH * 0.58f); };
    place("In", 0, 0.8f); place("Idle", 0, 2.2f);
    place("Unfocus", 1, 0); place("Focus", 1, 1); place("Selected", 1, 2); place("Press", 1, 3);
    place("Locked", 2, 0.3f); place("Value change", 2, 1.5f); place("Out", 2, 2.7f);
    for (size_t i = 0; i < custom.size(); ++i) at[an->clips[custom[i]].name] = ImVec2(colX((float)i) + bw / 2, o.y + 3 * laneH + laneH * 0.58f);
    // what's playing now (Live / the game), or the clip being edited
    std::set<std::string> lit;
    const Run& r = an->run; const double now = ImGui::GetTime();
    if (g.live || !g.on) {
        if (r.state >= 0 && r.state < (int)an->clips.size()) lit.insert(an->clips[r.state].name);
        if (r.locked && r.lockAt >= 0) lit.insert("Locked");
        if (r.valueAt >= 0) lit.insert("Value change");
        if (r.outAt >= 0) lit.insert("Out");
        if (e.shownSince >= 0) { const Clip* in = ClipNamed(an, "In"); const float t = MsSince(e.shownSince, now) - InDelay(e); lit.insert(in && t < in->length ? "In" : "Idle"); }
    } else if (CurClip()) lit.insert(CurClip()->name);
    std::string flashFrom, flashTo;
    if (r.switchAt >= 0 && now - r.switchAt < 0.6) {
        flashFrom = r.prevState >= 0 && r.prevState < (int)an->clips.size() ? an->clips[r.prevState].name : "";
        flashTo = r.state >= 0 && r.state < (int)an->clips.size() ? an->clips[r.state].name : "";
    }
    // arrows
    struct Arr { std::string from, to, label, trig; bool below; int flow; };
    std::vector<Arr> arrows = {
        { "In", "Idle", "In ends", "", false, -1 },
        { "Unfocus", "Focus", "focus on" + (an->focusIf.empty() ? std::string() : ": " + an->focusIf), "focus", false, -1 },
        { "Focus", "Selected", "Focus ends", "", false, -1 },
        { "Selected", "Press", "press" + (an->pressIf.empty() ? std::string() : ": " + an->pressIf), "press", false, -1 },
        { "Press", "Selected", "back to rest", "", true, -1 },
        { "Selected", "Unfocus", "focus off", "focus", true, -1 },
    };
    for (int i = 0; i < (int)an->flows.size(); ++i) {
        const Flow& f = an->flows[i];
        arrows.push_back({ f.from, f.to, f.cond.empty() ? std::string("when it ends") : f.cond, "", false, i });
    }
    int hitArrow = -1;
    for (int ai = 0; ai < (int)arrows.size(); ++ai) {
        const Arr& a = arrows[ai];
        if (!at.count(a.to) || (a.from != "*" && !at.count(a.from))) continue;
        const ImVec2 q1 = at[a.to];
        const ImVec2 q0 = a.from == "*" ? ImVec2(o.x + 4, q1.y - bh) : at[a.from];
        const bool flash = a.from == flashFrom && a.to == flashTo;
        const bool own = a.flow >= 0;
        const ImU32 col = flash ? IM_COL32(255, 220, 90, 255) : own ? IM_COL32(120, 220, 160, 230) : IM_COL32(170, 170, 185, 220);
        ImVec2 p0, p1, c0, c1;
        if (fabsf(q0.y - q1.y) < 2 && !own) {   // (along a lane: edge to edge; back arrows bow under the boxes)
            const float dir = q1.x > q0.x ? 1.f : -1.f;
            if (!a.below) { p0 = ImVec2(q0.x + dir * bw / 2, q0.y); p1 = ImVec2(q1.x - dir * bw / 2, q1.y); c0 = p0; c1 = p1; }
            else { p0 = ImVec2(q0.x, q0.y + bh / 2); p1 = ImVec2(q1.x, q1.y + bh / 2); const float dy = (fabsf(q1.x - q0.x) > bw * 1.5f ? 22 : 14) * u; c0 = ImVec2(p0.x, p0.y + dy); c1 = ImVec2(p1.x, p1.y + dy); }
        } else {   // (a transition of its own: an arc over the boxes)
            p0 = ImVec2(q0.x, q0.y - bh / 2); p1 = ImVec2(q1.x + (q1.x == q0.x ? 8 : 0), q1.y - bh / 2);
            const float lift = 26 * u + 6.f * (a.flow % 3);
            c0 = ImVec2(p0.x, std::min(p0.y, p1.y) - lift); c1 = ImVec2(p1.x, std::min(p0.y, p1.y) - lift);
        }
        dl->AddBezierCubic(p0, c0, c1, p1, col, flash ? 3.f : 1.5f);
        {   // arrowhead (along the end tangent)
            ImVec2 d(p1.x - c1.x, p1.y - c1.y); if (fabsf(d.x) + fabsf(d.y) < 0.1f) d = ImVec2(p1.x - p0.x, p1.y - p0.y);
            const float l = std::max(0.01f, sqrtf(d.x * d.x + d.y * d.y)); d = ImVec2(d.x / l, d.y / l);
            const ImVec2 nrm(-d.y, d.x), b(p1.x - d.x * 8, p1.y - d.y * 8);
            dl->AddTriangleFilled(p1, ImVec2(b.x + nrm.x * 4, b.y + nrm.y * 4), ImVec2(b.x - nrm.x * 4, b.y - nrm.y * 4), col);
        }
        const ImVec2 mid(0.125f * p0.x + 0.375f * c0.x + 0.375f * c1.x + 0.125f * p1.x, 0.125f * p0.y + 0.375f * c0.y + 0.375f * c1.y + 0.125f * p1.y);
        const std::string lb = a.label.size() > 28 ? a.label.substr(0, 27) + "..." : a.label;
        const ImVec2 ts = ImGui::CalcTextSize(lb.c_str());
        const ImVec2 la(mid.x - ts.x / 2 - 3, mid.y - ts.y - 2), lb2(mid.x + ts.x / 2 + 3, mid.y - 1);
        const bool hot = hov && m.x >= la.x && m.x <= lb2.x && m.y >= la.y && m.y <= lb2.y;
        dl->AddRectFilled(la, lb2, hot ? IM_COL32(60, 64, 76, 240) : IM_COL32(28, 30, 36, 220), 3.f);
        dl->AddText(ImVec2(la.x + 3, la.y), hot ? IM_COL32(255, 255, 255, 255) : col, lb.c_str());
        if (hot) hitArrow = ai;
    }
    // boxes
    std::string hitBox;
    auto box = [&](const std::string& nm, const char* sub) {
        if (!at.count(nm)) return;
        const ImVec2 c = at[nm];
        const ImVec2 a(c.x - bw / 2, c.y - bh / 2), b(c.x + bw / 2, c.y + bh / 2);
        const Clip* cl = nullptr; for (auto& k : an->clips) if (k.name == nm) cl = &k;
        const bool has = cl && Keys(*cl) && cl->on, on = lit.count(nm) > 0, hot = hov && m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y;
        const ImU32 edge = on ? IM_COL32(110, 170, 255, 255) : has ? IM_COL32(200, 200, 210, 255) : IM_COL32(110, 110, 120, 200);
        dl->AddRectFilled(a, b, on ? IM_COL32(50, 90, 160, 255) : hot ? IM_COL32(56, 60, 72, 255) : IM_COL32(36, 38, 46, 255), 6.f);
        dl->AddRect(a, b, edge, 6.f, 0, on ? 2.f : 1.f);
        const ImVec2 ts = ImGui::CalcTextSize(nm.c_str());
        dl->AddText(ImVec2(c.x - ts.x / 2, sub ? a.y + 2 : c.y - ts.y / 2), has ? IM_COL32(235, 235, 240, 255) : IM_COL32(130, 130, 140, 255), nm.c_str());
        if (sub) { std::string s = sub; if (s.size() > 22) s = s.substr(0, 21) + "..."; const ImVec2 ss = ImGui::CalcTextSize(s.c_str()); dl->AddText(ImVec2(c.x - ss.x / 2, b.y - ss.y - 2), IM_COL32(150, 150, 160, 255), s.c_str()); }
        if (hot) hitBox = nm;
    };
    for (const char* nm : { "In", "Idle", "Unfocus", "Focus", "Selected", "Press", "Out" }) box(nm, nullptr);
    const std::string lk = an->lockedIf.empty() ? "(no locked value)" : "while " + an->lockedIf, vc = an->valueOf.empty() ? "(no value)" : "on change: " + an->valueOf;
    box("Locked", lk.c_str()); box("Value change", vc.c_str());
    for (int i : custom) box(an->clips[i].name, "(custom)");
    // input: click a box / an arrow; drag box to box: a transition
    static std::string dragFrom; static ImVec2 dragAt;
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!hitBox.empty()) { dragFrom = hitBox; dragAt = m; }
        else if (hitArrow >= 0) {
            const Arr& a = arrows[hitArrow];
            if (!a.trig.empty()) { g.focusTrigger = a.trig; g.pageTab = 0; }
            else if (a.flow >= 0) ImGui::OpenPopup("##flowedit"), g_evEdit = a.flow;
        }
    }
    if (!dragFrom.empty()) {
        const bool far_ = fabsf(m.x - dragAt.x) + fabsf(m.y - dragAt.y) > 6;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) { if (far_) dl->AddLine(at[dragFrom], m, IM_COL32(120, 220, 160, 255), 2.f); }
        else {
            if (!far_) { for (int i = 0; i < (int)an->clips.size(); ++i) if (an->clips[i].name == dragFrom) { g.clip = i; g.focus = State::F_CLIP; g.graph.fit = g.big.fit = true; } }
            else if (!hitBox.empty() && hitBox != dragFrom) { Flow f; f.from = dragFrom; f.to = hitBox; an->flows.push_back(f); g_dirty = true; Note("A transition " + dragFrom + " -> " + hitBox + ": set its condition below (empty: when " + dragFrom + " ends)."); }
            dragFrom.clear();
        }
    }
    if (ImGui::BeginPopup("##flowedit")) {
        if (g_evEdit >= 0 && g_evEdit < (int)an->flows.size()) ImGui::Text("%s -> %s: edit it in the list under the graph.", an->flows[g_evEdit].from.c_str(), an->flows[g_evEdit].to.c_str());
        ImGui::EndPopup();
    }
    if (hov && !hitBox.empty()) ImGui::SetTooltip("Click: edit the %s clip. Drag onto another box: a transition of its own to it.", hitBox.c_str());
    else if (hov && hitArrow >= 0) ImGui::SetTooltip(arrows[hitArrow].trig.empty() ? (arrows[hitArrow].flow >= 0 ? "A transition of its own (edit it below)." : "A fixed rule of the engine.") : "Click: its trigger on the page.");
    // its own transitions
    ImGui::Spacing();
    ImGui::TextUnformatted("Transitions of its own"); ImGui::SameLine();
    ImGui::TextDisabled("(after the fixed rules: while From plays on the state layer, To starts when its condition comes on; empty: when From ends)");
    std::vector<const char*> names; names.push_back("*"); for (auto& k : an->clips) names.push_back(k.name.c_str());
    for (int i = 0; i < (int)an->flows.size(); ++i) {
        Flow& f = an->flows[i];
        ImGui::PushID(i);
        int fromI = 0, toI = 1;
        for (int j = 0; j < (int)names.size(); ++j) { if (f.from == names[j]) fromI = j; if (f.to == names[j]) toI = j; }
        ImGui::SetNextItemWidth(110 * u); if (ImGui::Combo("##from", &fromI, names.data(), (int)names.size())) { f.from = names[fromI]; g_dirty = true; }
        ImGui::SameLine(); ImGui::TextUnformatted("->"); ImGui::SameLine();
        ImGui::SetNextItemWidth(110 * u); if (ImGui::Combo("##to", &toI, names.data() + 1, (int)names.size() - 1)) { f.to = names[toI + 1]; g_dirty = true; }
        char cb[128]; snprintf(cb, sizeof cb, "%s", f.cond.c_str());
        ImGui::SameLine(); ImGui::SetNextItemWidth(170 * u); if (ImGui::InputTextWithHint("##cond", "when (empty: From ends)", cb, sizeof cb)) { f.cond = cb; g_dirty = true; }
        ImGui::SameLine(); ImGui::SetNextItemWidth(80 * u); if (ImGui::DragFloat("##bl", &f.blend, 2, 0, 2000, "%.0f ms")) g_dirty = true;
        ImGui::SameLine(); if (ImGui::SmallButton("x")) { an->flows.erase(an->flows.begin() + i); g_dirty = true; ImGui::PopID(); break; }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ transition")) { Flow f; f.from = "Press"; f.to = custom.empty() ? "Selected" : an->clips[custom[0]].name; an->flows.push_back(f); g_dirty = true; }
    ImGui::SameLine(); CustomClipButton(*an);
    ImGui::EndChild();
}

// ================================================================ keyboard shortcuts (animation mode, Live off)
static bool g_openEase = false, g_openHandles = false, g_openHelp = false;
static void DuplicateKeys() {
    Clip* c = CurClip(); if (!c || !PickedKeys(*c)) { Note("Ctrl + D: pick keys first."); return; }
    KeyClip keep = g_kclip; CopyKeys(false); PasteKeys(); g_kclip = keep;
}
static void StepKey(int dir) {   // the previous / next key's time (Screen mode: any element's)
    std::vector<float> ts;
    if (g.screen) { const std::string n = PreviewName(); for (auto& e : Elems()) if (Clip* c = ClipOfName(e, n)) { const float w = SheetWait(e, n); for (auto& tr : c->tracks) for (auto& k : tr.keys) ts.push_back(k.t + w); } }
    else if (Clip* c = CurClip()) { for (auto& tr : c->tracks) for (auto& k : tr.keys) ts.push_back(k.t); }
    std::sort(ts.begin(), ts.end());
    if (dir < 0) { float best = 0; for (float t : ts) if (t < g.time - 0.5f) best = t; g.time = best; }
    else for (float t : ts) if (t > g.time + 0.5f) { g.time = t; break; }
    g.playing = false;
}
static void DeletePicked() {
    if (g.screen) DeleteSheetPicked();
    else if (Clip* c = CurClip()) {
        for (auto& tr : c->tracks) tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [](const Key& k) { return k.sel; }), tr.keys.end());
        c->events.erase(std::remove_if(c->events.begin(), c->events.end(), [](const Event& ev) { return ev.sel; }), c->events.end());
    }
    g_dirty = true;
}
static void Shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || g_cd.active) return;
    if (g.live) { if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { g.live = false; SimRestart(); } return; }   // (Live's own keys: Frame)
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
    auto pr = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    auto rp = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    if (!ctrl && !alt) {
        if (rp(ImGuiKey_LeftArrow)) { g.playing = false; g.time = shift ? 0.f : std::max(0.f, Snap(g.time - FrameMs())); }
        if (rp(ImGuiKey_RightArrow)) { g.playing = false; g.time = shift ? PreviewLen() : Snap(g.time + FrameMs()); }
        if (rp(ImGuiKey_UpArrow)) StepKey(-1);
        if (rp(ImGuiKey_DownArrow)) StepKey(1);
        if (pr(ImGuiKey_I)) KeyHere(false);
        if (pr(ImGuiKey_T)) g_openEase = true;
        if (pr(ImGuiKey_V)) g_openHandles = true;
        if (pr(ImGuiKey_Tab)) g.screen = !g.screen;
        if (pr(ImGuiKey_G)) StartModal('G');
        if (pr(ImGuiKey_S)) StartModal('S');
        if (pr(ImGuiKey_R)) StartModal('R');
        if (pr(ImGuiKey_L)) { g.live = true; g.playing = false; SimRestart(); }
        if (pr(ImGuiKey_M)) AddMarkerHere();
        if (pr(ImGuiKey_Delete)) DeletePicked();
    }
    if (alt && !ctrl && pr(ImGuiKey_I)) KeyHere(true);
    if (ctrl && !alt && pr(ImGuiKey_D)) DuplicateKeys();
}
// the T / V menus and the keys' help (opened from a window: the timeline)
static void ShortcutPopups() {
    if (g_openEase) { ImGui::OpenPopup("##tease"); g_openEase = false; }
    if (g_openHandles) { ImGui::OpenPopup("##vhandles"); g_openHandles = false; }
    if (g_openHelp) { ImGui::OpenPopup("##keyhelp"); g_openHelp = false; }
    if (ImGui::BeginPopup("##tease")) {
        ImGui::TextDisabled("Way to the next key (the picked keys)");
        for (int e = 0; e < EZ_NEASE; ++e) if (ImGui::MenuItem(kEase[e])) {
            if (g.screen) ForSheetClips([&](Elem&, Clip& c) { if (PickedKeys(c)) ApplyEase(c, e); });
            else if (Clip* c = CurClip()) ApplyEase(*c, e);
            g_dirty = true;
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##vhandles")) {
        if (g.screen) { ImGui::TextDisabled("Handles (the picked keys)"); for (int h = 0; h < HT_N; ++h) if (ImGui::MenuItem(kHandleType[h])) ForSheetClips([&](Elem&, Clip& c) { if (PickedKeys(c)) SetHandleType(c, h); }); }
        else HandleTypeItems(CurClip());
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##keyhelp")) {
        static const char* keys[][2] = {
            { "Space", "play / pause" }, { "Left / Right", "previous / next frame (Shift: clip start / end)" }, { "Up / Down", "previous / next key" },
            { "I / Alt + I", "key the keyed tracks here / remove the keys here" }, { "Delete", "delete the picked keys (and picked events)" },
            { "Ctrl + C / X / V", "copy / cut / paste keys at the playhead" }, { "Ctrl + A", "pick every key shown" }, { "Ctrl + D", "duplicate the picked keys at the playhead" },
            { "Ctrl + Z / Ctrl + Y", "undo / redo" }, { "T", "easing of the picked keys" }, { "V", "handle type of the picked keys" },
            { "F / Shift + F", "the graph: fit all / the picked keys" }, { "Tab", "the timeline: the element's tracks / the whole screen" },
            { "G / S / R", "grab / scale / rotate the focused element (click or Enter keeps it, Esc puts it back)" },
            { "M", "a marker at the playhead" }, { "H", "hide / show the panels" }, { "L", "Live on (Esc: off)" }, { "Esc", "cancel a drag or G / S / R" },
        };
        ImGui::TextUnformatted("Animation mode keys");
        ImGui::TextDisabled("(not while typing in a box; the game doesn't get them while animating)");
        if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            for (auto& k : keys) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(k[0]); ImGui::TableNextColumn(); ImGui::TextUnformatted(k[1]); }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("On screen: click an element to focus it (again: the one under it; Alt: its group).");
        ImGui::TextDisabled("Auto key on: drag its box to move, its corners / edges to scale, outside a corner to turn, the cross: its pivot.");
        ImGui::EndPopup();
    }
}

// ================================================================ saved window layouts
static void LayoutApply(const std::string& name) {
    auto set = [&](int i, float x, float y, float w, float h) { w_.r[i] = { x, y, w, h, true }; w_.force[i] = true; };
    g.layoutName = name; w_.dirty = true;
    if (name == "Standard") { WinReset(); g.bare = g.expanded = g.screen = false; return; }
    if (name == "Watch") { g.bare = true; g.expanded = false; return; }
    g.bare = g.expanded = false;
    if (name == "Big graph") { set(WIN_SIDE, 0.68f, 0, 0.32f, 0.45f); set(WIN_GRAPH, 0, 0.45f, 1, 0.435f); set(WIN_TL, 0, 0.885f, 1, 0.115f); w_.split = 0.62f; return; }
    if (name == "Screen timing") { set(WIN_SIDE, 0.72f, 0, 0.28f, 0.36f); set(WIN_GRAPH, 0.72f, 0.36f, 0.28f, 0.2f); set(WIN_TL, 0, 0.56f, 1, 0.44f); g.screen = true; return; }
    auto it = g_layouts.find(name);
    if (it == g_layouts.end()) return;
    for (int i = 0; i < WIN_N; ++i) if (it->second.r[i].set) { w_.r[i] = it->second.r[i]; w_.force[i] = true; }
    w_.split = it->second.split; g.screen = it->second.screen;
}
static void LayoutCombo() {
    ImGui::SetNextItemWidth(120);
    if (ImGui::BeginCombo("##layout", g.layoutName.c_str())) {
        for (const char* n : { "Standard", "Big graph", "Screen timing", "Watch" }) if (ImGui::Selectable(n, g.layoutName == n)) LayoutApply(n);
        for (auto& [n, l] : g_layouts) if (ImGui::Selectable(n.c_str(), g.layoutName == n)) LayoutApply(n);
        ImGui::Separator();
        static char nm[48] = "";
        ImGui::SetNextItemWidth(130); ImGui::InputTextWithHint("##lname", "name", nm, sizeof nm);
        ImGui::SameLine();
        if (ImGui::Button("Save current as") && nm[0]) {
            NamedLayout& l = g_layouts[nm];
            for (int i = 0; i < WIN_N; ++i) l.r[i] = w_.r[i];
            l.split = w_.split; l.screen = g.screen; g.layoutName = nm; nm[0] = 0; w_.dirty = true;
        }
        if (g_layouts.count(g.layoutName) && ImGui::Button("Delete this one")) { g_layouts.erase(g.layoutName); g.layoutName = "Standard"; w_.dirty = true; }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Window layouts: Standard (the windows back where they start), Big graph, Screen timing (a tall dope sheet),\nWatch (panels hidden), and your own. Drag a window by its title bar (the timeline: the grip at its left end or the\nempty part of its top row), size it from its edges; its edges snap to the other windows' and the screen's.");
}

// ================================================================ the test script (--jrt_reui_anim_test): a step every few
// frames, its results to <REUI>\anim_test.txt
static std::deque<std::string> g_test; static int g_testWait = 0; static double g_testUntil = 0;   // (steps: 2 frames and 50 ms apart at least)
static FILE* TestOut(bool fresh = false) { return fopen((dataDir + "anim_test.txt").c_str(), fresh ? "w" : "a"); }
static void TestLog(const char* fmt, ...) {
    char line[1024]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    Log("anim test: %s", line);
    if (FILE* f = TestOut()) { fprintf(f, "%7.2f %s\n", ImGui::GetTime(), line); fclose(f); }
}
static int ElemNamed(const std::string& n) {   // (in the layout being edited, or else the first layout that has it: edited now)
    for (int pass = 0; pass < 2; ++pass)
        for (int mm = 0; mm < NumModes(); ++mm) {
            if ((pass == 0) != (mm == g_editMode)) continue;
            auto& els = g_layout[mm];
            for (int i = 0; i < (int)els.size(); ++i) if (els[i].name == n || els[i].group + "/" + els[i].name == n) { if (g_editMode != mm) { g_editMode = mm; g_sel = -1; } return i; }
        }
    return -1;
}
static std::string TracksText(const Clip& c) {
    std::string s;
    for (auto& tr : c.tracks) {
        if (tr.keys.empty()) continue;
        s += std::string(kPropKey[tr.prop]) + ":";
        for (auto& k : tr.keys) { char b[96]; snprintf(b, sizeof b, " %g=%g%s", k.t, roundf(k.v * 1000) / 1000, k.ht ? (std::string("/") + kHandleKey[k.ht]).c_str() : ""); s += b; }
        s += "; ";
    }
    return s;
}
static void TestStep(const std::string& step) {
    std::vector<std::string> a; { size_t p = 0; while (p <= step.size()) { size_t q = step.find(':', p); if (q == std::string::npos) q = step.size(); a.push_back(step.substr(p, q - p)); p = q + 1; } }
    const std::string& cmd = a[0];
    auto num = [&](size_t i, float d = 0) { return i < a.size() ? (float)atof(a[i].c_str()) : d; };
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : std::string(); };
    Clip* c = CurClip(); const int fi = FocusedIndex();
    const std::string fname = fi >= 0 ? Elems()[fi].name : std::string("-");
    if (cmd == "focus") { const int i = ElemNamed(arg(1)); FocusElem(i); TestLog("focus %s: %s", arg(1).c_str(), i >= 0 ? "ok" : "not found"); }
    else if (cmd == "clip") { if (ElemAnim* an = Focused()) for (int i = 0; i < (int)an->clips.size(); ++i) if (an->clips[i].name == arg(1)) g.clip = i; TestLog("clip %s", PreviewName().c_str()); }
    else if (cmd == "time") { g.time = num(1); g.playing = false; TestLog("time %g", g.time); }
    else if (cmd == "autokey") { g.autoKey = num(1) != 0; TestLog("auto key %d", (int)g.autoKey); }
    else if (cmd == "pick") {
        const int i = ElemNamed(arg(1)); Box b;
        if (i < 0 || !BoxOf(i, b)) { TestLog("pick %s: not on screen", arg(1).c_str()); return; }
        const int got = CanvasPick(b.c, arg(2) == "alt");
        TestLog("pick %s at %.0f,%.0f -> focused %s", arg(1).c_str(), b.c.x, b.c.y, got >= 0 ? Elems()[FocusedIndex()].name.c_str() : "(nothing)");
    }
    else if (cmd == "drag") {   // drag:<move|corner|edge|rotate|pivot>:<ux>:<uy>:<dx>:<dy>[:shift]
        Box b; if (fi < 0 || !BoxOf(fi, b) || !c) { TestLog("drag: no focused element on screen"); return; }
        const std::string pn = arg(1);
        const int part = pn == "move" ? H_MOVE : pn == "corner" ? H_CORNER : pn == "edge" ? H_EDGE : pn == "rotate" ? H_ROTATE : H_PIVOT;
        const float ux = num(2), uy = num(3);
        ImVec2 p0 = part == H_PIVOT ? b.piv : part == H_MOVE ? b.c : BoxPoint(b, ux, uy);
        if (part == H_ROTATE) { const ImVec2 q = BoxPoint(b, ux, uy); p0 = ImVec2(q.x + ux * 10, q.y + uy * 10); }
        if (!CanvasBegin(part, ux, uy, p0)) { TestLog("drag: couldn't start"); return; }
        const ImVec2 p1(p0.x + num(4), p0.y + num(5));
        for (int s = 1; s <= 8; ++s) CanvasDragTo(ImVec2(p0.x + (p1.x - p0.x) * s / 8, p0.y + (p1.y - p0.y) * s / 8), arg(6) == "shift", false);
        CanvasEnd(false);
        TestLog("drag %s %s by %g,%g -> %s pivot %.1f,%.1f", pn.c_str(), fname.c_str(), num(4), num(5), TracksText(*c).c_str(), Elems()[fi].pivX, Elems()[fi].pivY);
    }
    else if (cmd == "modal") {   // modal:<G|S|R>:<dx>:<dy>[:cancel]
        Box b; if (fi < 0 || !BoxOf(fi, b) || !c) { TestLog("modal: no focused element on screen"); return; }
        const int w = arg(1).empty() ? 'G' : arg(1)[0];
        const ImVec2 p0 = w == 'G' ? b.c : ImVec2(b.piv.x + 60, b.piv.y);
        if (!CanvasBegin(w == 'G' ? H_MOVE : w == 'S' ? H_CORNER : H_ROTATE, 1, 1, p0, w)) { TestLog("modal: couldn't start"); return; }
        for (int s = 1; s <= 8; ++s) CanvasDragTo(ImVec2(p0.x + num(2) * s / 8, p0.y + num(3) * s / 8), false, false);
        CanvasEnd(arg(4) == "cancel");
        TestLog("modal %c %s%s -> %s", w, fname.c_str(), arg(4) == "cancel" ? " (cancelled)" : "", TracksText(*c).c_str());
    }
    else if (cmd == "key") { const int n = KeyHere(false); TestLog("I: %d tracks keyed -> %s", n, c ? TracksText(*c).c_str() : "-"); }
    else if (cmd == "unkey") { const int n = KeyHere(true); TestLog("Alt+I: %d removed -> %s", n, c ? TracksText(*c).c_str() : "-"); }
    else if (cmd == "diamond") {
        int p = -1; for (int i = 0; i < P_NPROP; ++i) if (arg(1) == kPropKey[i]) p = i;
        if (!c || p < 0) { TestLog("diamond: no clip / track"); return; }
        const float t = Snap(g.time); const int st = KeyStateAt(c->tracks[p], t);
        if (st == 2) c->tracks[p].keys.erase(std::remove_if(c->tracks[p].keys.begin(), c->tracks[p].keys.end(), [&](const Key& k) { return fabsf(k.t - t) < 0.5f; }), c->tracks[p].keys.end());
        else SetKey(c->tracks[p], t, Value(c->tracks[p], t));
        TestLog("diamond %s (was %s) -> now %s", arg(1).c_str(), st == 2 ? "filled" : st == 1 ? "half" : "empty", KeyStateAt(c->tracks[p], t) == 2 ? "filled" : KeyStateAt(c->tracks[p], t) == 1 ? "half" : "empty");
    }
    else if (cmd == "htype") {
        int h = 0; for (int i = 0; i < HT_N; ++i) if (arg(1) == kHandleKey[i]) h = i;
        if (!c) return;
        for (auto& tr : c->tracks) for (auto& k : tr.keys) k.sel = false;
        g.focus = State::F_CLIP;   // (every keyed track)
        SetHandleType(*c, h);
        std::string hs;
        for (auto& tr : c->tracks) for (size_t i = 0; i + 1 < tr.keys.size(); ++i) { char b[96]; snprintf(b, sizeof b, " %s[%zu] h1 %.2f,%.3f h2 %.2f,%.3f;", kPropKey[tr.prop], i, tr.keys[i].h1x, tr.keys[i].h1y, tr.keys[i].h2x, tr.keys[i].h2y); hs += b; }
        TestLog("handles %s ->%s", arg(1).c_str(), hs.c_str());
    }
    else if (cmd == "screen") { g.screen = num(1) != 0; TestLog("screen mode %d", (int)g.screen); }
    else if (cmd == "dopemove" || cmd == "dopestretch") {   // dopemove:<elem>:<ms> / dopestretch:<elem>:<factor>
        const int i = ElemNamed(arg(1)); Clip* cc = i >= 0 ? ClipOfName(Elems()[i], PreviewName()) : nullptr;
        if (!cc) { TestLog("%s: no %s clip on %s", cmd.c_str(), PreviewName().c_str(), arg(1).c_str()); return; }
        const std::vector<float> k0 = ClipKeyTimes(*cc);
        if (cmd == "dopemove") MoveClipBy(i, Elems()[i].an.p->p.delay, k0, num(2));
        else { float f, l; KeySpan(*cc, f, l); StretchClip(*cc, k0, f, num(2, 1)); }
        FinishSheetEdit();
        TestLog("%s %s %g -> delay %g, %s", cmd.c_str(), arg(1).c_str(), num(2), Elems()[i].an.p->p.delay, TracksText(*cc).c_str());
    }
    else if (cmd == "stagger") {
        if (MotionOfGroup(arg(1)) < 0) FocusMotion(arg(1));
        const int mi = MotionOfGroup(arg(1));
        if (mi < 0 || !Elems()[mi].an.p) { TestLog("stagger: no group %s", arg(1).c_str()); return; }
        Elems()[mi].an.p->p.stagger = std::max(0.f, num(2)); g_dirty = true;
        std::string w; for (auto& e : Elems()) if (e.type != E_MOTION && InGroup(e.group, arg(1))) { char b[96]; snprintf(b, sizeof b, " %s %.0f;", e.name.c_str(), InDelay(e)); w += b; }
        TestLog("stagger %s %g ms -> waits%s", arg(1).c_str(), num(2), w.c_str());
    }
    else if (cmd == "dopepick") { const int i = ElemNamed(arg(1)); if (arg(3) != "add") ClearSheetPicks(); const int n = PickAtTime(i, -1, num(2), false); TestLog("dope pick %s at %g: %d keys (%d picked in all)", arg(1).c_str(), num(2), n, SheetPicked()); }
    else if (cmd == "dopedrag") { auto mk = PickedKeyList(arg(2) == "copy"); MoveKeysBy(mk, num(1)); FinishSheetEdit(); std::string s; ForSheetClips([&](Elem& e, Clip& cc) { if (PickedKeys(cc)) s += e.name + ": " + TracksText(cc); }); TestLog("dope drag %g%s -> %s", num(1), arg(2) == "copy" ? " (copy)" : "", s.c_str()); }
    else if (cmd == "align" || cmd == "staggerby") {
        AlignStarts(cmd == "align" ? -1.f : num(1));
        std::string s; ForSheetClips([&](Elem& e, Clip& cc) { if (PickedKeys(cc)) { char b[96]; snprintf(b, sizeof b, " %s delay %g (waits %.0f);", e.name.c_str(), e.an.p->p.delay, InDelay(e)); s += b; } });
        TestLog("%s ->%s", cmd.c_str(), s.c_str());
    }
    else if (cmd == "event" || cmd == "marker") {
        if (!c) return;
        Event ev; ev.t = num(1); ev.marker = cmd == "marker";
        std::string rest; for (size_t i = 2; i < a.size(); ++i) { if (i > 2) rest += ":"; rest += a[i]; }   // (commands have colons)
        if (ev.marker) ev.name = rest; else ev.cmd = rest;
        c->events.push_back(ev); std::stable_sort(c->events.begin(), c->events.end(), [](const Event& x, const Event& y) { return x.t < y.t; });
        g_dirty = true;
        TestLog("%s at %g on %s %s: %s", cmd.c_str(), ev.t, fname.c_str(), c->name.c_str(), rest.c_str());
    }
    else if (cmd == "playevents") { g.playEvents = num(1) != 0; TestLog("play events %d", (int)g.playEvents); }
    else if (cmd == "fired") TestLog("events run: %d (last: %s)", g_evFired, g_evLast.c_str());
    else if (cmd == "live") { const bool on = num(1) != 0; if (g.live != on) { g.live = on; SimRestart(); } TestLog("live %d", (int)g.live); }
    else if (cmd == "play") { g.playing = num(1) != 0; g.loop = true; TestLog("playing %d", (int)g.playing); }
    else if (cmd == "onion") { g.onion = num(1) != 0; g.onionGroup = arg(2) == "group"; g.onionKeys = arg(2) != "steps"; TestLog("onion %d %s", (int)g.onion, arg(2).c_str()); }
    else if (cmd == "path") { g.path = num(1) != 0; TestLog("path %d", (int)g.path); }
    else if (cmd == "tab") { g.pageTab = arg(1) == "graph" ? 1 : 0; TestLog("panel tab %s", arg(1).c_str()); }
    else if (cmd == "graphtab") { g.graphTab = arg(1) == "speed" ? 1 : 0; g.normalise = arg(2) == "norm"; g.graph.fit = true; TestLog("graph %s %s", arg(1).c_str(), arg(2).c_str()); }
    else if (cmd == "layout") { LayoutApply(arg(1)); TestLog("layout %s", arg(1).c_str()); }
    else if (cmd == "custom") { if (fi >= 0) { Clip& k = ClipFor(Elems()[fi], arg(1)); k.on = true; } TestLog("custom clip %s on %s", arg(1).c_str(), fname.c_str()); }
    else if (cmd == "flow") { if (ElemAnim* an = Focused()) { Flow f; f.from = arg(1); f.to = arg(2); f.cond = arg(3); f.blend = num(4, 120); an->flows.push_back(f); g_dirty = true; } TestLog("flow %s -> %s when '%s'", arg(1).c_str(), arg(2).c_str(), arg(3).c_str()); }
    else if (cmd == "status") TestLog("status: time %g, %s mode, focused %s, clip %s, live %d, G/S/R %d, auto key %d", g.time, g.screen ? "Screen" : "Element", fname.c_str(), PreviewName().c_str(), (int)g.live, g_cd.active ? g_cd.modal : 0, (int)g.autoKey);
    else if (cmd == "press") { g.simPressAt = ImGui::GetTime(); TestLog("press (live)"); }
    else if (cmd == "row") { g.simRow = (int)num(1); TestLog("focus row %d (live)", g.simRow); }
    else if (cmd == "state") { if (ElemAnim* an = Focused()) TestLog("%s now: %s", fname.c_str(), StateNow(*an).c_str()); }
    else if (cmd == "dump") { if (fi >= 0) { std::string t = AnimText(Elems()[fi].an.p); for (auto& ch : t) if (ch == '\n') ch = '|'; TestLog("%s: %s", fname.c_str(), t.c_str()); } }
    else if (cmd == "undo") { Undo(); TestLog("undo -> %s", CurClip() ? TracksText(*CurClip()).c_str() : "-"); }
    else if (cmd == "wait") g_testUntil = ImGui::GetTime() + num(1, 500) / 1000.0;   // wait:<ms>
    else TestLog("unknown step %s", step.c_str());
}
static void TestTick() {
    if (g_test.empty()) return;
    if (g_testWait > 0) { --g_testWait; return; }
    if (ImGui::GetTime() < g_testUntil) return;
    const std::string s = g_test.front(); g_test.pop_front();
    g_testWait = 2; g_testUntil = std::max(g_testUntil, ImGui::GetTime() + 0.05);
    TestStep(s);
    if (g_test.empty()) TestLog("done");
}
