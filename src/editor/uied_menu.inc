// UI editor menus (UniPad) - included by uied.cpp after Elem / the layout. See uied.h "menus".
//
// A menu page is a top-level group. Rows are groups directly inside it made widgets; their elements have roles:
//   label, value (text: the value / choice), fill (slider bar), tab (slider knob), arrowL / arrowR (picker),
//   and anywhere in the page: desc (shows the selected row's description), stab / strack (scroller tab and track).
// A Highlight widget's elements move with the selected row. Elements marked "selected row only" are hidden on the
// other rows (the game's picker arrows). Rows navigate top to bottom by their label's height.
// Widget kit: rows saved as templates (elements kept relative to the row's height) and stamped into pages.

// a row's group name from a label: "/" would make it a group inside a group (then it isn't a row of the page)
static std::string RowName(std::string label) {
    for (auto& c : label) if (c == '/' || c == '\\') c = '-';
    while (!label.empty() && label.back() == ' ') label.pop_back();
    while (!label.empty() && label[0] == ' ') label.erase(0, 1);
    return label;
}

// the menu tools show while the Menus workspace is the one being edited
static bool MenuWs() { return menuMode || g_editMode == MENUS; }

enum WType { W_NONE = 0, W_BUTTON, W_SLIDER, W_PICKER, W_HIGHLIGHT, W_NTYPES };
static const char* kWType[] = { "(not a widget)", "Button", "Slider", "Picker (left / right)", "Highlight" };
static const char* kWKey[] = { "none", "button", "slider", "picker", "highlight" };

struct Widget {
    std::string group;
    int type = W_BUTTON;
    std::string bind;                    // Setting key ("" = its own value / action)
    std::string desc;                    // shown by the page's description text while selected
    std::string action;                  // buttons: "page:<group>", "back", "close" ("" = nothing)
    std::vector<std::string> choices;    // pickers with no setting
    float x0 = 0, x1 = 0;                // sliders: the fill bar's left edge at 0% and 100% (units)
    float min = 0, max = 1, step = 0.05f, value = 0;   // own value (unbound): number, or choice index
    int col = 0;                         // the page column it's in (0 = the first); its pieces sit col * the page's column distance over
};
struct Page {
    std::string group;
    float spacing = 36;                  // row spacing (Restack, Add row)
    float firstY = -127;                 // the first row's height (Add row on an empty page, Restack)
    float colDist = 290;                 // distance between columns (units)
    float colX = 0;                      // how far the first column (and the others with it) is moved across
    int addCol = 0;                      // the column Add row puts rows in
    int visRows = 0;                     // > 0: rows shown at once (the rest scroll)
    bool start = false;                  // opened by the menu key
    bool useSel = true; float selCol[4] = { 202 / 255.f, 244 / 255.f, 138 / 255.f, 1.f };   // selected row's label / value
};
struct KitT { std::string name; Widget w; };        // a template: widget settings (group = "")
static std::vector<Widget> g_widgets;
static std::vector<Page> g_pages;
static std::vector<KitT> g_kitT;

std::vector<Setting> settings;
std::function<void(int)> menuSound;
std::function<void()> menuChanged;

static Widget* WidgetOf(const std::string& g) { for (auto& w : g_widgets) if (w.group == g) return &w; return nullptr; }
static Page* PageOf(const std::string& g) { for (auto& p : g_pages) if (p.group == g) return &p; return nullptr; }
static Setting* SettingOf(const std::string& k) { if (k.empty()) return nullptr; for (auto& s : settings) if (s.key == k) return &s; return nullptr; }
static std::string TopGroup(const std::string& g) { return g.substr(0, g.find('/')); }
static void Snd(int s) { if (menuSound) menuSound(s); }
// the widget an element belongs to: its group or the nearest group around it that's a widget
static Widget* WidgetFor(const std::string& eg) {
    for (std::string g = eg; !g.empty(); g = GroupParent(g)) if (Widget* w = WidgetOf(g)) return w;
    return nullptr;
}
// renames / copies / deletes follow the groups
static void MenuRebase(const std::string& from, const std::string& to) {
    for (auto& w : g_widgets) if (InGroup(w.group, from)) w.group = CleanGroup(to + w.group.substr(from.size()));
    for (auto& p : g_pages) if (InGroup(p.group, from)) p.group = CleanGroup(to + p.group.substr(from.size()));
    for (auto& w : g_widgets) if (w.action.compare(0, 5, "page:") == 0 && InGroup(w.action.substr(5), from)) w.action = "page:" + CleanGroup(to + w.action.substr(5 + from.size()));
}
static void MenuCopy(const std::string& from, const std::string& to) {
    std::vector<Widget> add; for (auto& w : g_widgets) if (InGroup(w.group, from)) { Widget c = w; c.group = to + w.group.substr(from.size()); add.push_back(c); }
    for (auto& w : add) g_widgets.push_back(w);
}
static void MenuForget(const std::string& g) {
    g_widgets.erase(std::remove_if(g_widgets.begin(), g_widgets.end(), [&](const Widget& w) { return InGroup(w.group, g); }), g_widgets.end());
    g_pages.erase(std::remove_if(g_pages.begin(), g_pages.end(), [&](const Page& p) { return InGroup(p.group, g); }), g_pages.end());
}

// ---------------------------------------------------------------- values
static bool g_menuValDirty = false;   // an unbound widget's own value changed (saved with the layout on close)
static float WGet(const Widget& w) { Setting* s = SettingOf(w.bind); return s && s->get ? s->get() : w.value; }
static void WSet(Widget& w, float v) {
    if (Setting* s = SettingOf(w.bind)) { if (s->set) s->set(v); if (menuChanged) menuChanged(); }
    else { w.value = v; g_dirty = true; g_menuValDirty = true; }
}
static void WRange(const Widget& w, float& lo, float& hi, float& st) {
    if (Setting* s = SettingOf(w.bind)) { lo = s->min; hi = s->max; st = s->step; }
    else { lo = w.min; hi = w.max; st = w.step; }
    if (hi <= lo) hi = lo + 1; if (st <= 0) st = (hi - lo) / 20;
}
static float WFrac(const Widget& w) { float lo, hi, st; WRange(w, lo, hi, st); return std::max(0.f, std::min(1.f, (WGet(w) - lo) / (hi - lo))); }
static std::vector<std::string> WChoices(const Widget& w) {
    if (Setting* s = SettingOf(w.bind)) if (s->choices) return s->choices();
    return w.choices;
}
static std::string WText(const Widget& w) {
    if (w.type == W_PICKER) {
        auto c = WChoices(w); if (c.empty()) return "-";
        int i = (int)lroundf(WGet(w)); i = ((i % (int)c.size()) + (int)c.size()) % (int)c.size();
        return c[i];
    }
    if (w.type == W_SLIDER) {
        Setting* s = SettingOf(w.bind);
        if (s && s->format) return s->format(WGet(w));
        char b[24]; snprintf(b, sizeof b, "%d%%", (int)lroundf(WFrac(w) * 100)); return b;
    }
    return "";
}

// ---------------------------------------------------------------- page structure
struct Row { Widget* w; float y, x; };
constexpr float kBand = 6.f;   // rows this close in height are side by side (columns)
// a row's height: its label's, else the middle of its texts, else of all its elements
static float RowY(const Widget& w) {
    float sum = 0; int n = 0, nt = 0; float st = 0;
    for (auto& e : MenuElems()) {
        if (!InGroup(e.group, w.group)) continue;
        if (e.role == "label") return e.y;
        if (e.type == E_TEXT) { st += e.y; ++nt; }
        sum += e.y; ++n;
    }
    return nt ? st / nt : n ? sum / n : 0.f;
}
// a row's place across: its label's, else the middle of its pieces
static float RowX(const Widget& w) {
    float lo = 1e9f, hi = -1e9f;
    for (auto& e : MenuElems()) {
        if (!InGroup(e.group, w.group)) continue;
        if (e.role == "label") return e.x;
        lo = std::min(lo, e.x); hi = std::max(hi, e.x);
    }
    return lo <= hi ? (lo + hi) / 2 : 0.f;
}
// rows top to bottom, side-by-side ones left to right
static std::vector<Row> PageRows(const Page& p) {
    std::vector<Row> r;
    for (auto& w : g_widgets)
        if (GroupParent(w.group) == p.group && (w.type == W_BUTTON || w.type == W_SLIDER || w.type == W_PICKER)) r.push_back({ &w, RowY(w), RowX(w) });
    std::stable_sort(r.begin(), r.end(), [](const Row& a, const Row& b) { return fabsf(a.y - b.y) > kBand ? a.y < b.y : a.x < b.x; });
    return r;
}
// Grid moves. dy: -1 up / +1 down -> the nearest line of rows that way (wrapping on a fresh press), the one closest
// across; dx: -1 / +1 -> the next row on the same line (-1 if none).
static int MoveSel(const std::vector<Row>& r, int cur, int dy, int dx, bool wrap) {
    int n = (int)r.size(); if (!n) return -1;
    const Row& c = r[cur];
    if (dx) {
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < n; ++i)
            if (i != cur && fabsf(r[i].y - c.y) <= kBand && (r[i].x - c.x) * dx > 0.5f && fabsf(r[i].x - c.x) < bd) { bd = fabsf(r[i].x - c.x); best = i; }
        return best;
    }
    // the nearest line that way, or (wrapping) the farthest the other way
    float lineY = 0; bool found = false;
    for (auto& o : r) {
        float d = (o.y - c.y) * dy;
        if (d > kBand && (!found || d < (lineY - c.y) * dy)) { lineY = o.y; found = true; }
    }
    if (!found) {
        if (!wrap) return cur;
        for (auto& o : r) if (!found || (o.y - lineY) * -dy > 0) { lineY = o.y; found = true; }
    }
    int best = cur; float bd = 1e9f;
    for (int i = 0; i < n; ++i)
        if (fabsf(r[i].y - lineY) <= kBand && fabsf(r[i].x - c.x) < bd) { bd = fabsf(r[i].x - c.x); best = i; }
    return best;
}
// the row brings its own highlight (pieces shown on the selected row only, other than picker arrows)
static bool OwnHighlight(const Widget& w) {
    for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.selOnly && e.role != "arrowL" && e.role != "arrowR") return true;
    return false;
}
static Widget* PageHighlight(const Page& p) {
    for (auto& w : g_widgets) if (GroupParent(w.group) == p.group && w.type == W_HIGHLIGHT) return &w;
    return nullptr;
}
static float GroupMidY(const std::string& g) {
    float lo = 1e9f, hi = -1e9f;
    for (auto& e : MenuElems()) if (InGroup(e.group, g)) { lo = std::min(lo, e.y); hi = std::max(hi, e.y); }
    return lo <= hi ? (lo + hi) / 2 : 0.f;
}

// ---------------------------------------------------------------- runtime
static struct MenuState {
    bool open = false;
    std::vector<std::string> stack;      // pages opened, the last is shown
    std::vector<int> selStack;           // selected row on each
    int sel = 0, top = 0;
    bool was[6] = {}; double held[6] = {}, next[6] = {};
    double flashL = 0, flashR = 0, t = 0;
    const Widget* capturing = nullptr;   // a binding row waiting for an input
} g_menu;

std::vector<std::string> MenuPages() { std::vector<std::string> v; for (auto& p : g_pages) v.push_back(p.group); return v; }
bool MenuIsOpen() { return g_menu.open; }
static Page* MenuPage() { return g_menu.stack.empty() ? nullptr : PageOf(g_menu.stack.back()); }
static void MenuGo(const std::string& page) {
    if (!g_menu.stack.empty()) g_menu.selStack.push_back(g_menu.sel);
    g_menu.stack.push_back(page); g_menu.sel = 0; g_menu.top = 0;
}
bool MenuOpen(const std::string& page) {
    std::string p = page;
    if (p.empty()) { for (auto& pg : g_pages) if (pg.start) p = pg.group; if (p.empty() && !g_pages.empty()) p = g_pages[0].group; }
    if (p.empty() || !PageOf(p)) return false;
    if (g_state == 0) { g_state = 1; std::thread(Scan).detach(); }
    if (g_state != 2) return false;   // textures / fonts still being read: it couldn't be drawn
    g_menu = MenuState(); g_menu.open = true;
    for (auto& b : g_menu.was) b = true;   // the key / button that opened it doesn't act until let go
    for (auto& x : g_menu.next) x = 1e9;   // nor repeat
    MenuGo(p);
    return true;
}
void MenuClose() {
    if (!g_menu.open) return;
    g_menu.open = false; g_menu.stack.clear(); g_menu.selStack.clear(); g_menu.capturing = nullptr;
    if (g_menuValDirty) { Save(); g_menuValDirty = false; }   // own values of unbound widgets
}
static void MenuBack() {
    Snd(SND_BACK);
    g_menu.stack.pop_back();
    if (g_menu.stack.empty()) { MenuClose(); return; }
    g_menu.sel = g_menu.selStack.empty() ? 0 : g_menu.selStack.back(); if (!g_menu.selStack.empty()) g_menu.selStack.pop_back();
    g_menu.top = 0;
}
static void MenuAccept(Widget& w) {
    if (w.type == W_PICKER) { auto c = WChoices(w); if (c.empty()) { Snd(SND_ERROR); return; } WSet(w, (float)(((int)lroundf(WGet(w)) + 1) % (int)c.size())); g_menu.flashR = g_menu.t; Snd(SND_RIGHT); return; }
    if (w.type != W_BUTTON) return;
    if (Setting* s = SettingOf(w.bind)) {
        if (s->kind == 3 && s->beginCapture && s->capture) { Snd(SND_ACCEPT); s->beginCapture(); g_menu.capturing = &w; return; }
        if (s->run) { Snd(SND_ACCEPT); s->run(); return; }
    }
    if (w.action.compare(0, 5, "page:") == 0 && PageOf(w.action.substr(5))) { Snd(SND_ACCEPT); MenuGo(w.action.substr(5)); return; }
    if (w.action == "back") { MenuBack(); return; }
    if (w.action == "close") { Snd(SND_BACK); MenuClose(); return; }
    Snd(SND_ERROR);
}
static void MenuChange(Widget& w, int dir, double heldFor) {
    if (w.type == W_PICKER) {
        auto c = WChoices(w); if (c.size() < 2) { Snd(SND_ERROR); return; }
        int n = (int)c.size(), i = (int)lroundf(WGet(w));
        WSet(w, (float)(((i + dir) % n + n) % n));   // wraps
        (dir < 0 ? g_menu.flashL : g_menu.flashR) = g_menu.t;
        Snd(dir < 0 ? SND_LEFT : SND_RIGHT);
    } else if (w.type == W_SLIDER) {
        float lo, hi, st; WRange(w, lo, hi, st);
        if (heldFor > 1.2) st *= 4; else if (heldFor > 0.6) st *= 2;   // held: faster
        float v = WGet(w), nv = std::max(lo, std::min(hi, roundf((v + dir * st - lo) / st) * st + lo));
        if (fabsf(nv - v) < 1e-6f) return;   // at the end: quiet
        WSet(w, nv);
        Snd(dir < 0 ? SND_SLIDE_LEFT : SND_SLIDE_RIGHT);
    }
}
void MenuFrame(const MenuInput& in, double dt) {
    if (!g_menu.open) return;
    Page* p = MenuPage(); if (!p) { MenuClose(); return; }
    g_menu.t += dt;
    auto rows = PageRows(*p);
    int n = (int)rows.size();
    if (n) g_menu.sel = std::max(0, std::min(n - 1, g_menu.sel));
    if (g_menu.capturing) {   // a binding row is waiting for an input: nothing else moves
        Setting* s = SettingOf(g_menu.capturing->bind);
        int r = s && s->capture ? s->capture(dt) : -1;
        if (r != 0) {
            g_menu.capturing = nullptr;
            Snd(r > 0 ? SND_ACCEPT : SND_BACK);
            for (auto& b : g_menu.was) b = true;     // the input just used doesn't also act on the menu
            for (auto& x : g_menu.next) x = 1e9;
            if (r > 0 && menuChanged) menuChanged();
        }
        return;
    }
    // presses, and repeats while held: 0.35 s, then every 0.09 s (sliders 0.06 s)
    bool now[6] = { in.up, in.down, in.left, in.right, in.accept, in.back };
    bool fire[6] = {};
    for (int i = 0; i < 6; ++i) {
        if (now[i] && !g_menu.was[i]) { fire[i] = true; g_menu.held[i] = 0; g_menu.next[i] = 0.35; }
        else if (now[i] && i < 4) {
            g_menu.held[i] += dt;
            bool slider = n && rows[g_menu.sel].w->type == W_SLIDER && i >= 2;
            if (g_menu.held[i] >= g_menu.next[i]) { fire[i] = true; g_menu.next[i] += slider ? 0.06 : 0.09; }
        }
        g_menu.was[i] = now[i];
    }
    if (fire[5]) { MenuBack(); return; }
    if (!n) return;
    if (fire[0] || fire[1]) {
        int d = fire[1] ? 1 : -1;
        bool repeat = g_menu.held[fire[1] ? 1 : 0] > 0;   // a press wraps, holding stops at the end
        int ns = MoveSel(rows, g_menu.sel, d, 0, !repeat);
        if (ns >= 0 && ns != g_menu.sel) { g_menu.sel = ns; Snd(d > 0 ? SND_DOWN : SND_UP); }
    }
    Widget& w = *rows[g_menu.sel].w;
    // left / right: sliders and pickers change; buttons move to the one beside them (two columns)
    for (int k = 0; k < 2; ++k) {
        if (!fire[2 + k]) continue;
        int dir = k ? 1 : -1;
        if (w.type == W_SLIDER || w.type == W_PICKER) { MenuChange(w, dir, g_menu.held[2 + k]); continue; }
        int ns = MoveSel(rows, g_menu.sel, 0, dir, false);
        if (ns >= 0) { g_menu.sel = ns; Snd(dir < 0 ? SND_LEFT : SND_RIGHT); }
    }
    Widget& wsel = *rows[g_menu.sel].w;
    if (fire[4]) { MenuAccept(wsel); return; }
    if (p->visRows > 0) {
        if (g_menu.sel < g_menu.top) g_menu.top = g_menu.sel;
        if (g_menu.sel >= g_menu.top + p->visRows) g_menu.top = g_menu.sel - p->visRows + 1;
    } else g_menu.top = 0;
}

// The page as shown: each element copied and changed for the current state. sel < 0: nothing selected (editor).
static void DrawMenuPage(ImDrawList* dl, const Page& p, int sel, int top) {
    auto rows = PageRows(p);
    int n = (int)rows.size();
    if (sel >= n) sel = n - 1;
    std::map<const Widget*, int> rowIdx; for (int i = 0; i < n; ++i) rowIdx[rows[i].w] = i;
    float scrollDy = p.visRows > 0 ? -top * p.spacing : 0.f;
    // highlight: from the row it was laid out on to the selected one
    Widget* hl = PageHighlight(p);
    float hlDy = 0; bool hlShow = sel >= 0 && n > 0;
    if (hl && hlShow) {
        float mid = GroupMidY(hl->group), best = 1e9f, refY = 0;
        for (auto& r : rows) if (fabsf(r.y - mid) < best) { best = fabsf(r.y - mid); refY = r.y; }
        hlDy = rows[sel].y - refY + scrollDy;
        if (OwnHighlight(*rows[sel].w)) hlShow = false;   // a small button with its own highlight
    }
    // scroller: where the tab sits on the track
    float trTop = 0, trBot = 0; bool track = false;
    for (auto& e : MenuElems()) if (InGroup(e.group, p.group) && e.role == "strack") { trTop = e.y - e.h / 2; trBot = e.y + e.h / 2; track = true; }
    std::string desc = sel >= 0 && n ? rows[sel].w->desc : "";
    auto& els = MenuElems();
    for (int i : DrawOrder(els)) {
        Elem& src = els[i];
        if (!InGroup(src.group, p.group) || !src.visible) continue;
        Elem e = src;
        Widget* w = WidgetFor(e.group);
        auto it = w ? rowIdx.find(w) : rowIdx.end();
        if (it != rowIdx.end()) {                       // a row
            int r = it->second; bool on = r == sel;
            if (p.visRows > 0 && (r < top || r >= top + p.visRows)) continue;
            if (e.selOnly && !on) continue;
            e.y += scrollDy;
            if (e.role == "value") e.text = WText(*w);
            Setting* bs = SettingOf(w->bind);
            if (bs && bs->kind == 3) {   // a binding row
                if (e.type == E_TEXT) {   // {binding} = the action, {input} = what it's on
                    auto put = [&](const char* tok, const std::string& v) { for (size_t k; (k = e.text.find(tok)) != std::string::npos;) e.text.replace(k, strlen(tok), v); };
                    put("{binding}", bs->label);
                    put("{input}", g_menu.capturing == w ? std::string("???") : bs->current ? bs->current() : std::string());
                }
                if (e.role == "bindicon") {
                    if (g_menu.capturing == w) {   // waiting: "???" where the icon is, in the row's font
                        int fi = -1;
                        for (auto& o : MenuElems()) if (o.type == E_TEXT && InGroup(o.group, w->group)) { if (o.fontIdx == -2) o.fontIdx = FindFont(o.font); fi = o.fontIdx; if (o.role == "label") break; }
                        if (fi < 0) fi = FindFont("conduitmditc_tt21i");
                        if (fi >= 0) {
                            ImVec2 a, b; ElemRect(e, a, b);
                            const Font& f = g_fonts[fi];
                            float k = (b.y - a.y) / std::max(1, f.line);
                            float tw = TextWidth(f, "???") * k;
                            bool blink = fmod(g_menu.t, 0.8) < 0.55;   // pulses while it waits
                            float c[4] = { e.col[0], e.col[1], e.col[2], e.col[3] * (blink ? 1.f : 0.45f) };
                            DrawText(dl, f, ImVec2((a.x + b.x - tw) / 2, a.y), k, Col(c), "???", 0, 0);
                        }
                        continue;
                    }
                    int id = bs->icon ? bs->icon() : -1;
                    if (id < 0 || id >= (int)iconKeys.size()) continue;
                    e.tex = "icon:auto:" + iconKeys[id]; e.texIdx = -3;
                }
            }
            if (w->type == W_SLIDER && (e.role == "fill" || e.role == "tab")) {
                // x0 / x1 are in centre-anchored units (the fill's left edge, the tab's centre); a piece anchored
                // left / right has its x at that side of itself
                float f = WFrac(*w), x1 = w->x1 > w->x0 ? w->x1 : w->x0 + 1, ax = (e.anchor % 3) * 0.5f;
                if (e.role == "fill") { e.w = f * (x1 - w->x0); e.x = w->x0 + ax * e.w; if (e.w < 0.5f) continue; }
                else e.x = w->x0 + f * (x1 - w->x0) + (ax - 0.5f) * e.w;
            }
            if (w->type == W_PICKER && (e.role == "arrowL" || e.role == "arrowR")) {
                double ft = e.role == "arrowL" ? g_menu.flashL : g_menu.flashR;
                if (on && g_menu.open && g_menu.t - ft < 0.15) e.col[3] = 1.f;   // lights up when pressed
            }
            if (on && p.useSel && (e.role == "label" || e.role == "value")) memcpy(e.col, p.selCol, sizeof e.col);
        } else if (hl && InGroup(e.group, hl->group)) {
            if (!hlShow) continue;
            e.y += hlDy;
        } else if (e.role == "desc") {
            if (sel >= 0) e.text = desc;
        } else if (e.role == "stab" && track && sel >= 0 && n > 1) {
            float half = e.h / 2;
            e.y = trTop + half + (trBot - trTop - 2 * half) * (float)sel / (float)(n - 1);
        }
        e.texIdx = src.texIdx; e.fontIdx = src.fontIdx;
        DrawElem(dl, e, false);
        src.texIdx = e.texIdx; src.fontIdx = e.fontIdx;   // keep what it looked up
    }
}
// while the menu is open it's all that's drawn
static bool DrawMenu(ImDrawList* dl) {
    if (!g_menu.open) return false;
    Page* p = MenuPage(); if (!p) return false;
    DrawMenuPage(dl, *p, g_menu.sel, g_menu.top);
    return true;
}

// ---------------------------------------------------------------- authoring
static const char* kRoleKey[] = { "", "label", "value", "fill", "tab", "arrowL", "arrowR", "desc", "stab", "strack", "bindicon" };
static const char* kRoleName[] = { "(none)", "label", "value text", "slider fill bar", "slider tab", "left arrow", "right arrow",
                                   "page: description text", "page: scroller tab", "page: scroller track", "binding icon (??? while waiting)" };
static std::string Upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }

// roles from what the pieces are: textures, place, kind
static void DetectRoles(Widget& w) {
    std::vector<Elem*> m; for (auto& e : MenuElems()) if (InGroup(e.group, w.group)) m.push_back(&e);
    Elem* label = nullptr;
    for (Elem* e : m) if (e->type == E_TEXT && (!label || e->x < label->x)) label = e;
    for (Elem* e : m) {
        std::string t = Upper(e->tex);
        if (e == label) e->role = w.type == W_HIGHLIGHT ? "" : "label";
        else if (e->type == E_TEXT) e->role = w.type == W_BUTTON || w.type == W_HIGHLIGHT ? "" : "value";
        else if (t.find("SLIDERBAR_TAB") != std::string::npos) e->role = "tab";
        else if (t.find("WHITE16X16") != std::string::npos && w.type == W_SLIDER) e->role = "fill";
        else if (t.find("MENU_ARROW") != std::string::npos && w.type == W_PICKER) e->role = "arrow";
        else e->role = "";
    }
    // arrows: the left one and the right one; shown on the selected row only, like the game
    std::vector<Elem*> ar; for (Elem* e : m) if (e->role == "arrow") ar.push_back(e);
    std::sort(ar.begin(), ar.end(), [](Elem* a, Elem* b) { return a->x < b->x; });
    for (size_t i = 0; i < ar.size(); ++i) { ar[i]->role = i == 0 ? "arrowL" : "arrowR"; ar[i]->selOnly = true; }
    if (w.type == W_SLIDER) {   // the bar's range: the fill's edges, or its start and the track's other side
        Elem* fill = nullptr; Elem* trackE = nullptr; Elem* tab = nullptr;
        for (Elem* e : m) { if (e->role == "fill") fill = e; if (e->role == "tab") tab = e; if (e->type == E_IMAGE && Upper(e->tex).find("SLIDERBG") != std::string::npos) trackE = e; }
        auto left = [](Elem* e) { return e->x - (e->anchor % 3) * 0.5f * e->w; };
        auto mid = [](Elem* e) { return e->x + (0.5f - (e->anchor % 3) * 0.5f) * e->w; };
        if (fill && fill->w > 1) { w.x0 = left(fill); w.x1 = w.x0 + fill->w; }
        else if (fill || tab) { w.x0 = fill ? left(fill) : mid(tab); w.x1 = trackE ? 2 * mid(trackE) - w.x0 : w.x0 + 140; }
    }
    g_dirty = true;
}
// a binding by the label's words
static std::string GuessBind(const std::string& label, int type) {
    std::string l = Upper(label);
    auto has = [&](const char* s) { return l.find(s) != std::string::npos; };
    std::string k;
    if (has("MENU") && has("DEADZONE")) k = "menu.deadzone";
    else if (has("DEADZONE")) k = "steer.deadzone";
    else if (has("GATE") || has("SATURATION")) k = "steer.gate";
    else if (has("CURVE")) k = "steer.curve";
    else if (has("RUMBLE") && has("MODE")) k = "rumble.mode";
    else if ((has("RUMBLE") || has("VIBRATION")) && has("STRENGTH")) k = "rumble.strength";
    else if (has("GAMEPAD") || has("CONTROLLER")) k = "pad.active";
    Setting* s = SettingOf(k);
    if (!s) return "";
    int want = type == W_SLIDER ? 0 : type == W_PICKER ? 1 : 2;
    return s->kind == want ? k : "";
}

// Build rows: every label in the page's label column becomes a row with the pieces level with it
static int BuildRows(Page& p) {
    std::vector<int> mine;   // the page's elements not in a widget yet
    for (int i = 0; i < (int)MenuElems().size(); ++i) {
        Elem& e = MenuElems()[i];
        if (!InGroup(e.group, p.group) || WidgetFor(e.group)) continue;
        mine.push_back(i);
    }
    // a "highlight" group inside the page becomes the Highlight
    for (auto& g : GroupNames()) {
        if (GroupParent(g) != p.group || WidgetOf(g)) continue;
        if (Upper(GroupLeaf(g)).find("HIGHLIGHT") != std::string::npos && !PageHighlight(p)) {
            Widget w; w.group = g; w.type = W_HIGHLIGHT; g_widgets.push_back(w);
        }
    }
    // label column: the most common left edge of left-aligned texts
    std::map<int, int> col;
    for (int i : mine) { Elem& e = MenuElems()[i]; if (e.type == E_TEXT && e.align == 0 && !WidgetFor(e.group)) col[(int)lroundf(e.x / 4)]++; }
    if (col.empty()) return 0;
    int bestK = 0, bestN = 0; for (auto& kv : col) if (kv.second > bestN) { bestN = kv.second; bestK = kv.first; }
    float colX = bestK * 4.f;
    struct L { int i; float y; std::string text, rg; };
    std::vector<L> labels;
    for (int i : mine) {
        Elem& e = MenuElems()[i];
        if (e.type != E_TEXT || e.align != 0 || fabsf(e.x - colX) > 6 || WidgetFor(e.group)) continue;
        std::string t = e.text; while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
        if (Upper(t) == "DESCRIPTION") { e.role = "desc"; continue; }   // the description line
        labels.push_back({ i, e.y, t, "" });
    }
    std::sort(labels.begin(), labels.end(), [](const L& a, const L& b) { return a.y < b.y; });
    if (labels.size() >= 2) { float sp = labels[1].y - labels[0].y; if (sp > 8) p.spacing = sp; }
    float half = p.spacing * 0.45f;
    {   // row group names, unique (two labels with the same text get "Name 2")
        auto names = GroupNames(); std::vector<std::string> used;
        for (auto& lb : labels) {
            std::string base = p.group + "/" + RowName(lb.text.empty() ? "row" : lb.text), rg = base;
            for (int n = 2; WidgetOf(rg) || std::find(used.begin(), used.end(), rg) != used.end() ||
                            std::find(names.begin(), names.end(), rg) != names.end(); ++n) rg = base + " " + std::to_string(n);
            used.push_back(rg); lb.rg = rg;
        }
    }
    for (auto& lb : labels) {
        const std::string& rg = lb.rg;
        int members = 0;
        for (int i : mine) {
            Elem& e = MenuElems()[i];
            if (WidgetFor(e.group)) continue;
            if (i != lb.i) {
                if (fabsf(e.y - lb.y) > half) continue;
                if (e.type == E_IMAGE && e.h > p.spacing * 1.4f) continue;              // boxes, tracks
                if (Upper(e.tex).find("SCROLL") != std::string::npos) continue;        // the scroller
                if (e.type == E_TEXT && e.align == 0 && fabsf(e.x - colX) <= 6) continue;
            }
            e.group = rg; ++members;
        }
        Widget w; w.group = rg;
        bool tab = false, fill = false, arrows = false; int texts = 0;
        for (auto& e : MenuElems()) if (e.group == rg) {
            std::string t = Upper(e.tex);
            tab |= t.find("SLIDERBAR_TAB") != std::string::npos; fill |= t.find("WHITE16X16") != std::string::npos;
            arrows |= t.find("MENU_ARROW") != std::string::npos; texts += e.type == E_TEXT;
        }
        w.type = tab || fill ? W_SLIDER : arrows || texts >= 2 ? W_PICKER : W_BUTTON;
        g_widgets.push_back(w);
    }
    // (pointers only after all push_backs)
    int count = 0;
    for (auto& lb : labels) {
        const std::string& rg = lb.rg;
        Widget* w = WidgetOf(rg); if (!w) continue;
        DetectRoles(*w);
        w->bind = GuessBind(lb.text, w->type);
        if (w->type == W_PICKER && w->bind.empty() && w->choices.empty()) {   // its value text as the first choice
            for (auto& e : MenuElems()) if (e.group == rg && e.role == "value") { w->choices.push_back(e.text); break; }
        }
        ++count;
    }
    // pickers without arrows get a copy of another picker's (moved to their row)
    std::vector<Elem> arrowsOf; float arrowY = 0;
    for (auto& w : g_widgets) {
        if (GroupParent(w.group) != p.group || w.type != W_PICKER) continue;
        std::vector<Elem> a; for (auto& e : MenuElems()) if (e.group == w.group && (e.role == "arrowL" || e.role == "arrowR")) a.push_back(e);
        if (a.size() == 2) { arrowsOf = a; arrowY = RowY(w); break; }
    }
    if (!arrowsOf.empty())
        for (auto& w : g_widgets) {
            if (GroupParent(w.group) != p.group || w.type != W_PICKER) continue;
            bool has = false; for (auto& e : MenuElems()) if (e.group == w.group && (e.role == "arrowL" || e.role == "arrowR")) has = true;
            if (has) continue;
            float dy = RowY(w) - arrowY;
            for (auto e : arrowsOf) { e.group = w.group; e.y += dy; e.texIdx = -2; MenuElems().push_back(e); }
        }
    // the scroller: a scroller tab, and the tall thin piece it runs on
    Elem* stab = nullptr;
    for (auto& e : MenuElems()) if (InGroup(e.group, p.group) && !WidgetFor(e.group) && Upper(e.tex).find("SCROLLER_TAB") != std::string::npos) { e.role = "stab"; stab = &e; }
    if (stab) for (auto& e : MenuElems())
        if (InGroup(e.group, p.group) && !WidgetFor(e.group) && &e != stab && e.type == E_IMAGE && fabsf(e.x - stab->x) < 6 && e.h > 100 && e.w < 20) e.role = "strack";
    g_dirty = true;
    return count;
}
// rows evenly spaced from the page's first row y, in their order
static void Restack(Page& p) {
    auto all = PageRows(p); if (all.empty()) return;
    int maxCol = 0; for (auto& r : all) maxCol = std::max(maxCol, r.w->col);
    for (int col = 0; col <= maxCol; ++col) {   // each column stacked on its own
    std::vector<Row> rows; for (auto& r : all) if (r.w->col == col) rows.push_back(r);
    if (rows.empty()) continue;
    // rows side by side (the same height, apart across) are one line and stay together; rows on top of each other
    // (the same height and place) get lines of their own
    int line = -1; float lineY = 0; std::vector<float> lineXs;
    for (size_t i = 0; i < rows.size(); ++i) {
        bool overlaps = false; for (float x : lineXs) overlaps |= fabsf(rows[i].x - x) < 40.f;
        if (i == 0 || fabsf(rows[i].y - lineY) > kBand || overlaps) { ++line; lineY = rows[i].y; lineXs.clear(); }
        lineXs.push_back(rows[i].x);
        float dy = p.firstY + line * p.spacing - rows[i].y;
        for (auto& e : MenuElems()) if (InGroup(e.group, rows[i].w->group)) e.y += dy;
    }
    }
    g_dirty = true;
}
static void SwapRows(Page& p, Widget* w, int dir) {
    auto rows = PageRows(p);
    for (int i = 0; i < (int)rows.size(); ++i) if (rows[i].w == w) {
        int j = i + dir; if (j < 0 || j >= (int)rows.size()) return;
        float a = rows[i].y, b = rows[j].y;
        for (auto& e : MenuElems()) { if (InGroup(e.group, rows[i].w->group)) e.y += b - a; else if (InGroup(e.group, rows[j].w->group)) e.y += a - b; }
        g_dirty = true; return;
    }
}
static void SaveTemplate(const Widget& w, const std::string& name) {
    g_kit.erase(std::remove_if(g_kit.begin(), g_kit.end(), [&](const Elem& e) { return e.kit == name; }), g_kit.end());
    g_kitT.erase(std::remove_if(g_kitT.begin(), g_kitT.end(), [&](const KitT& k) { return k.name == name; }), g_kitT.end());
    float y0 = RowY(w);
    Page* pg = PageOf(GroupParent(w.group));
    float x0 = pg ? pg->colX + w.col * pg->colDist : 0.f;   // kept as if in the first column, unmoved
    for (auto& e : MenuElems()) if (InGroup(e.group, w.group)) {
        Elem c = e; c.kit = name; c.y -= y0; c.x -= x0; c.group = e.group.size() > w.group.size() ? e.group.substr(w.group.size() + 1) : "";
        g_kit.push_back(c);
    }
    KitT k; k.name = name; k.w = w; k.w.group.clear(); k.w.bind.clear(); k.w.desc.clear(); k.w.x0 -= x0; k.w.x1 -= x0; k.w.col = 0;
    g_kitT.push_back(k); g_dirty = true;
}
// a new row from a template, under the page's last row
static void StampRow(Page& p, const KitT& k, const std::string& label, const std::string& bind) {
    // under the last row of the column rows are being added to (or at the first row's height)
    auto rows = PageRows(p);
    float y = p.firstY; bool any = false;
    for (auto& r : rows) if (r.w->col == p.addCol && (!any || r.y + p.spacing > y)) { y = r.y + p.spacing; any = true; }
    float dx = p.colX + p.addCol * p.colDist;
    std::string leaf = RowName(label.empty() ? k.name : label);
    std::string rg = p.group + "/" + leaf;
    auto names = GroupNames();
    for (int n = 2; WidgetOf(rg) || std::find(names.begin(), names.end(), rg) != names.end(); ++n) rg = p.group + "/" + leaf + " " + std::to_string(n);
    int layerBase = 0;
    for (auto& e : g_kit) if (e.kit == k.name) {
        Elem c = e; c.kit.clear(); c.y += y; c.x += dx; c.group = c.group.empty() ? rg : rg + "/" + c.group; c.texIdx = -2; c.fontIdx = -2;
        if (c.role == "label" && !label.empty()) { c.text = label; c.name = label; }
        MenuElems().push_back(c); (void)layerBase;
    }
    Widget w = k.w; w.group = rg; w.bind = bind; w.col = p.addCol; w.x0 += dx; w.x1 += dx; g_widgets.push_back(w);
    g_dirty = true;
}

// ---------------------------------------------------------------- authoring panels
static void ElemMenuProps(Elem& e) {
    if (!MenuWs()) return;
    int r = 0; for (int i = 0; i < 11; ++i) if (e.role == kRoleKey[i]) r = i;
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("role", &r, kRoleName, 11)) { e.role = kRoleKey[r]; g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What this piece does in its menu row (or page):\n"
        "label / value text: the value text shows the setting; both take the page's selected colour.\n"
        "slider fill bar / tab: sized / moved by the value. Arrows: light up when pressed.");
    ImGui::SameLine(); if (ImGui::Checkbox("selected row only", &e.selOnly)) g_dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hidden on rows that aren't selected (the game's picker arrows).");
}
static void SettingCombo(Widget& w) {
    int want = w.type == W_SLIDER ? 0 : w.type == W_PICKER ? 1 : 2;
    Setting* cur = SettingOf(w.bind);
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("changes", cur ? (cur->kind == 3 ? ("bind: " + cur->label).c_str() : cur->label.c_str()) : (w.type == W_BUTTON ? "(its own action)" : "(its own value)"))) {
        if (ImGui::Selectable(w.type == W_BUTTON ? "(its own action)" : "(its own value)", !cur)) { w.bind.clear(); g_dirty = true; }
        for (auto& s : settings) {
            bool fits = s.kind == want || (want == 2 && s.kind == 3);
            std::string lb = s.kind == 3 ? "bind: " + s.label : s.label;
            if (fits && ImGui::Selectable(lb.c_str(), cur == &s)) {
                w.bind = s.key; g_dirty = true;
                if (s.kind == 3 && !w.group.empty())   // its icon pieces show what it's bound to
                    for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.type == E_IMAGE && e.tex.compare(0, 5, "icon:") == 0) e.role = "bindicon";
            }
        }
        ImGui::EndCombo();
    }
}
static void WidgetPanel(Widget& w, Page* page) {
    int t = w.type;
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("widget", &t, kWType, W_NTYPES)) {
        if (t == W_NONE) { std::string g = w.group; MenuForget(g); g_dirty = true; return; }
        w.type = t; DetectRoles(w);
    }
    ImGui::SameLine(); if (ImGui::SmallButton("detect roles")) DetectRoles(w);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets each piece's role from its texture and place (label = the leftmost text).");
    if (w.type == W_HIGHLIGHT) { ImGui::TextDisabled("Moves with the selected row (from the row it's laid out on)."); return; }
    SettingCombo(w);
    if (w.type == W_BUTTON && !SettingOf(w.bind)) {
        std::string cur = w.action.empty() ? "(nothing)" : w.action == "back" ? "go back" : w.action == "close" ? "close the menu" : "open " + w.action.substr(5);
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("on accept", cur.c_str())) {
            if (ImGui::Selectable("(nothing)")) { w.action.clear(); g_dirty = true; }
            if (ImGui::Selectable("go back")) { w.action = "back"; g_dirty = true; }
            if (ImGui::Selectable("close the menu")) { w.action = "close"; g_dirty = true; }
            for (auto& p : g_pages) if (ImGui::Selectable(("open " + p.group).c_str())) { w.action = "page:" + p.group; g_dirty = true; }
            ImGui::EndCombo();
        }
    }
    if (w.type == W_PICKER && !SettingOf(w.bind)) {
        static char cb[512]; static std::string cbFor = "\x01";
        if (cbFor != w.group) { std::string j; for (size_t i = 0; i < w.choices.size(); ++i) j += (i ? ", " : "") + w.choices[i]; snprintf(cb, sizeof cb, "%s", j.c_str()); cbFor = w.group; }
        ImGui::SetNextItemWidth(320);
        if (ImGui::InputTextWithHint("choices", "Off, On, Auto", cb, sizeof cb)) {
            w.choices.clear(); std::string s = cb, cur;
            for (size_t i = 0; i <= s.size(); ++i) {
                if (i == s.size() || s[i] == ',') { while (!cur.empty() && cur[0] == ' ') cur.erase(0, 1); while (!cur.empty() && cur.back() == ' ') cur.pop_back(); if (!cur.empty()) w.choices.push_back(cur); cur.clear(); }
                else cur += s[i];
            }
            g_dirty = true;
        }
    }
    if (w.type == W_SLIDER) {
        if (!SettingOf(w.bind)) {
            ImGui::SetNextItemWidth(220); if (ImGui::DragFloat3("min / max / step", &w.min, 0.01f)) g_dirty = true;
        }
        ImGui::SetNextItemWidth(220); if (ImGui::DragFloat2("bar from / to (x)", &w.x0, 0.5f, -2000.f, 2000.f, "%.1f")) g_dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where the fill bar starts and where it ends at 100%% (the tab moves along it).");
        ImGui::SameLine(); if (ImGui::SmallButton("from the fill bar")) {
            for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.role == "fill" && e.w > 1) { w.x0 = e.x - (e.anchor % 3) * 0.5f * e.w; w.x1 = w.x0 + e.w; g_dirty = true; }
        }
    }
    static char db[256]; static std::string dbFor = "\x01";
    if (dbFor != w.group) { snprintf(db, sizeof db, "%s", w.desc.c_str()); dbFor = w.group; }
    ImGui::SetNextItemWidth(320);
    if (ImGui::InputTextWithHint("description", "shown by the page's description text", db, sizeof db)) { w.desc = db; g_dirty = true; }
    if (page) {
        if (ImGui::SmallButton("row up")) SwapRows(*page, &w, -1);
        ImGui::SameLine(); if (ImGui::SmallButton("row down")) SwapRows(*page, &w, +1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        int c = w.col;
        if (ImGui::InputInt("column", &c) && c >= 0 && c != w.col) {   // moves it across by the column distance
            float dx = (c - w.col) * page->colDist;
            for (auto& e : MenuElems()) if (InGroup(e.group, w.group)) e.x += dx;
            w.x0 += dx; w.x1 += dx; w.col = c; g_dirty = true;
        }
        ImGui::SameLine();
    }
    static char tn[64] = "";
    ImGui::SetNextItemWidth(140); ImGui::InputTextWithHint("##tname", "template name", tn, sizeof tn);
    ImGui::SameLine();
    if (ImGui::SmallButton("save as template")) { std::string nm = tn[0] ? tn : GroupLeaf(w.group); SaveTemplate(w, nm); tn[0] = 0; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keeps this row (pieces, roles, type) in the widget kit, to add more rows like it to any page.");
}
static void PagePanel(Page& p) {
    ImGui::SetNextItemWidth(100); if (ImGui::DragFloat("row spacing", &p.spacing, 0.5f, 4.f, 200.f, "%.1f")) g_dirty = true;
    ImGui::SameLine(); ImGui::SetNextItemWidth(100); if (ImGui::DragFloat("first row y", &p.firstY, 0.5f, -240.f, 240.f, "%.1f")) g_dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where the first row goes: Add row on an empty page, and Restack rows starts here.");
    ImGui::SameLine(); ImGui::SetNextItemWidth(80); if (ImGui::InputInt("rows shown", &p.visRows)) { p.visRows = std::max(0, p.visRows); g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = all. More rows than this scroll (the scroller tab follows).");
    ImGui::SameLine(); if (ImGui::Checkbox("menu key opens this", &p.start)) { if (p.start) for (auto& o : g_pages) if (&o != &p) o.start = false; g_dirty = true; }
    {   // columns
        int maxCol = 0; for (auto& w : g_widgets) if (GroupParent(w.group) == p.group) maxCol = std::max(maxCol, w.col);
        float cd = p.colDist;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("column distance", &cd, 40.f, 600.f, "%.0f")) {   // live: every column past the first moves
            float d = cd - p.colDist;
            for (auto& w : g_widgets) {
                if (GroupParent(w.group) != p.group || w.col == 0) continue;
                for (auto& e : MenuElems()) if (InGroup(e.group, w.group)) e.x += d * w.col;
                w.x0 += d * w.col; w.x1 += d * w.col;
            }
            p.colDist = cd; g_dirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far apart the columns are (Ctrl + click to type a value).");
        ImGui::SameLine();
        float cx = p.colX;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("first column x", &cx, -320.f, 320.f, "%.0f")) {   // live: every column moves with the first
            float d = cx - p.colX;
            for (auto& w : g_widgets) {
                if (GroupParent(w.group) != p.group) continue;
                for (auto& e : MenuElems()) if (InGroup(e.group, w.group)) e.x += d;
                w.x0 += d; w.x1 += d;
            }
            p.colX = cx; g_dirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Moves the first column across; the other columns follow, keeping the column distance.");
        ImGui::SameLine();
        if (ImGui::Button("Start a new column")) { p.addCol = maxCol + 1; g_dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add row puts rows in a new column from now on, starting at \"first row y\".");
        ImGui::SameLine(); ImGui::SetNextItemWidth(80);
        if (ImGui::InputInt("add to column", &p.addCol)) { p.addCol = std::max(0, std::min(maxCol + 1, p.addCol)); g_dirty = true; }
    }
    if (ImGui::Checkbox("selected row colour", &p.useSel)) g_dirty = true;
    ImGui::SameLine(); if (ImGui::ColorEdit4("##selcol", p.selCol, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) g_dirty = true;
    auto rows = PageRows(p);
    ImGui::TextDisabled("%d row(s)%s", (int)rows.size(), PageHighlight(p) ? ", highlight" : ", no highlight yet");
    if (ImGui::Button("Build rows")) { int n = BuildRows(p); Log("ui editor: %d row(s) built in %s", n, p.group.c_str()); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns each label in the page's label column into a row with the pieces level with it,\n"
        "and guesses its kind (slider / picker / button), roles and setting. A group named ...highlight...\n"
        "becomes the highlight; a text \"Description\" shows the selected row's description.");
    ImGui::SameLine(); if (ImGui::Button("Restack rows")) Restack(p);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spaces the rows evenly (row spacing) from \"first row y\" down, in their order.");
    ImGui::SameLine();
    if (ImGui::Button(g_menu.open ? "Close the menu" : "Try it")) { if (g_menu.open) MenuClose(); else MenuOpen(p.group); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens this page as the real menu: pad / arrow keys, Enter, Esc.");
    // add a row from the kit
    if (!g_kitT.empty()) {
        static int ki = 0; static char lb[64] = ""; static std::string bind;
        ki = std::max(0, std::min((int)g_kitT.size() - 1, ki));
        ImGui::SetNextItemWidth(130);
        if (ImGui::BeginCombo("##kit", g_kitT[ki].name.c_str())) {
            for (int i = 0; i < (int)g_kitT.size(); ++i) if (ImGui::Selectable(g_kitT[i].name.c_str(), i == ki)) { ki = i; bind.clear(); }
            ImGui::EndCombo();
        }
        ImGui::SameLine(); ImGui::SetNextItemWidth(150); ImGui::InputTextWithHint("##rowlabel", "label", lb, sizeof lb);
        ImGui::SameLine();
        Widget tmp = g_kitT[ki].w; tmp.bind = bind;
        ImGui::PushID("addrow"); SettingCombo(tmp); ImGui::PopID(); bind = tmp.bind;
        ImGui::SameLine(); if (ImGui::Button("Add row")) { StampRow(p, g_kitT[ki], lb, bind); lb[0] = 0; }
    } else ImGui::TextDisabled("Widget kit is empty: pick a row, then \"save as template\".");
}
static void GroupMenuProps(const std::string& g) {
    if (!MenuWs()) return;
    ImGui::SeparatorText("Menu");
    if (GroupParent(g).empty()) {
        Page* p = PageOf(g);
        bool is = p != nullptr;
        if (ImGui::Checkbox("menu page", &is)) {
            if (is) { Page np; np.group = g; np.start = g_pages.empty(); g_pages.push_back(np); }
            else g_pages.erase(std::remove_if(g_pages.begin(), g_pages.end(), [&](const Page& x) { return x.group == g; }), g_pages.end());
            g_dirty = true; p = PageOf(g);
        }
        if (p) PagePanel(*p);
        return;
    }
    Page* page = PageOf(GroupParent(g));
    Widget* w = WidgetOf(g);
    if (!w) {
        if (!page) { ImGui::TextDisabled("Rows are groups directly inside a menu page."); return; }
        int t = 0;
        ImGui::SetNextItemWidth(170);
        if (ImGui::Combo("widget", &t, kWType, W_NTYPES) && t != W_NONE) {
            Widget nw; nw.group = g; nw.type = t; g_widgets.push_back(nw);
            DetectRoles(*WidgetOf(g)); WidgetOf(g)->bind = GuessBind(GroupLeaf(g), t); g_dirty = true;
        }
        return;
    }
    WidgetPanel(*w, page);
}
static void KitPanel() {
    if (!MenuWs() || !ImGui::CollapsingHeader("Widget kit")) return;
    ImGui::TextWrapped("1) Make a top-level group a menu page (select it, tick \"menu page\").  2) \"Build rows\" turns its labels "
                       "into rows, or make a group inside it a widget yourself.  3) Pick a finished row and \"save as template\".  "
                       "4) On any page: choose a template, type a label, pick what it changes, \"Add row\".  "
                       "Menu key: %s opens the start page in the game.", "F7 (MenuKey in the ini)");
    for (size_t i = 0; i < g_kitT.size(); ++i) {
        ImGui::PushID((int)i);
        int n = 0; for (auto& e : g_kit) if (e.kit == g_kitT[i].name) ++n;
        ImGui::BulletText("%s  - %s, %d piece(s)", g_kitT[i].name.c_str(), kWType[g_kitT[i].w.type], n);
        ImGui::SameLine();
        if (ImGui::SmallButton("delete")) {
            std::string nm = g_kitT[i].name;
            g_kit.erase(std::remove_if(g_kit.begin(), g_kit.end(), [&](const Elem& e) { return e.kit == nm; }), g_kit.end());
            g_kitT.erase(g_kitT.begin() + i); g_dirty = true; ImGui::PopID(); break;
        }
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------- save / load
static void SaveWidgetProps(FILE* f, const Widget& w) {
    fprintf(f, "type=%s\n", kWKey[w.type]);
    if (!w.bind.empty()) fprintf(f, "bind=%s\n", w.bind.c_str());
    if (!w.desc.empty()) fprintf(f, "desc=%s\n", Esc(w.desc).c_str());
    if (!w.action.empty()) fprintf(f, "action=%s\n", Esc(w.action).c_str());
    if (!w.choices.empty()) { std::string j; for (size_t i = 0; i < w.choices.size(); ++i) j += (i ? "|" : "") + w.choices[i]; fprintf(f, "choices=%s\n", Esc(j).c_str()); }
    if (w.type == W_SLIDER) fprintf(f, "range=%g %g\n", w.x0, w.x1);
    if (w.col) fprintf(f, "col=%d\n", w.col);
    fprintf(f, "num=%g %g %g %g\n", w.min, w.max, w.step, w.value);
}
static void SaveMenus(FILE* f) {
    for (auto& p : g_pages)
        fprintf(f, "\n[page]\ngroup=%s\nspacing=%g\nfirsty=%g\ncoldist=%g\ncolx=%g\naddcol=%d\nrows=%d\nstart=%d\nselcolor=%d %g %g %g %g\n", Esc(p.group).c_str(), p.spacing, p.firstY, p.colDist, p.colX, p.addCol, p.visRows, p.start ? 1 : 0,
                p.useSel ? 1 : 0, p.selCol[0], p.selCol[1], p.selCol[2], p.selCol[3]);
    for (auto& w : g_widgets) { fprintf(f, "\n[widget]\ngroup=%s\n", Esc(w.group).c_str()); SaveWidgetProps(f, w); }
    for (auto& k : g_kitT) { fprintf(f, "\n[template]\nname=%s\n", Esc(k.name).c_str()); SaveWidgetProps(f, k.w); }
}
// a widget / page / template line; returns false if k isn't one of theirs
static bool LoadWidgetKey(Widget& w, const std::string& k, const std::string& v) {
    if (k == "type") { for (int i = 0; i < W_NTYPES; ++i) if (v == kWKey[i]) w.type = i; }
    else if (k == "bind") w.bind = v;
    else if (k == "desc") w.desc = Unesc(v);
    else if (k == "action") w.action = Unesc(v);
    else if (k == "choices") { w.choices.clear(); std::string s = Unesc(v), cur; for (size_t i = 0; i <= s.size(); ++i) { if (i == s.size() || s[i] == '|') { if (!cur.empty()) w.choices.push_back(cur); cur.clear(); } else cur += s[i]; } }
    else if (k == "range") sscanf(v.c_str(), "%f %f", &w.x0, &w.x1);
    else if (k == "col") w.col = std::max(0, atoi(v.c_str()));
    else if (k == "num") sscanf(v.c_str(), "%f %f %f %f", &w.min, &w.max, &w.step, &w.value);
    else return false;
    return true;
}

static int g_loadSect = 0;   // 2 page, 3 widget, 4 template
static void LoadMenusReset() { g_pages.clear(); g_widgets.clear(); g_kitT.clear(); }
static int LoadMenuSection(const std::string& s) {
    if (s == "[page]") { g_pages.push_back(Page()); return 2; }
    if (s == "[widget]") { g_widgets.push_back(Widget()); return 3; }
    if (s == "[template]") { g_kitT.push_back(KitT()); return 4; }
    return 9;   // unknown: skipped
}
static bool LoadMenuKey(int sect, const std::string& k, const std::string& v) {
    if (sect == 2 && !g_pages.empty()) {
        Page& p = g_pages.back();
        if (k == "group") p.group = Unesc(v);
        else if (k == "spacing") p.spacing = (float)atof(v.c_str());
        else if (k == "firsty") p.firstY = (float)atof(v.c_str());
        else if (k == "coldist") p.colDist = (float)atof(v.c_str());
        else if (k == "colx") p.colX = (float)atof(v.c_str());
        else if (k == "addcol") p.addCol = std::max(0, atoi(v.c_str()));
        else if (k == "rows") p.visRows = atoi(v.c_str());
        else if (k == "start") p.start = atoi(v.c_str()) != 0;
        else if (k == "selcolor") { int u = 1; sscanf(v.c_str(), "%d %f %f %f %f", &u, &p.selCol[0], &p.selCol[1], &p.selCol[2], &p.selCol[3]); p.useSel = u != 0; }
        return true;
    }
    if (sect == 3 && !g_widgets.empty()) { if (k == "group") g_widgets.back().group = Unesc(v); else LoadWidgetKey(g_widgets.back(), k, v); return true; }
    if (sect == 4 && !g_kitT.empty()) { if (k == "name") g_kitT.back().name = Unesc(v); else LoadWidgetKey(g_kitT.back().w, k, v); return true; }
    return false;
}
static void LoadMenuEnd() {
    g_pages.erase(std::remove_if(g_pages.begin(), g_pages.end(), [](const Page& p) { return p.group.empty(); }), g_pages.end());
    g_widgets.erase(std::remove_if(g_widgets.begin(), g_widgets.end(), [](const Widget& w) { return w.group.empty(); }), g_widgets.end());
}
static bool MenuOpenNow() { return g_menu.open; }
