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

enum WType { W_NONE = 0, W_BUTTON, W_SLIDER, W_PICKER, W_HIGHLIGHT, W_GRID, W_NTYPES };
static const char* kWType[] = { "(not a widget)", "Button", "Slider", "Picker (left / right)", "Highlight", "Grid (rows x columns)" };
static const char* kWKey[] = { "none", "button", "slider", "picker", "highlight", "grid" };

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
    // grids: its pieces are one cell (the top-left one), repeated cols x rows across gw x gh units (cell = gw / cols by
    // gh / rows), from gx, gy (the top-left cell's corner); scalePieces: the cell's pieces stretch from their own size
    // (refW x refH) to the cell's. More items than cols x rows scroll up and down. Items: a choice setting (bind), or
    // choices ("label;icon;action" each), or bind "list:<key>" (values <key>.count, <key>.N, <key>.N.icon).
    int cols = 4, rows = 3;
    float gw = 400, gh = 240, gx = -200, gy = -120, refW = 100, refH = 80;
    bool scalePieces = true;
    int startAt = 0;                     // where it sits on its origin: left (0) = its left edge on it, centre (1) = its middle,
                                         // right (2) = its right edge; always centred on it up and down. Short rows line up the same way.
    float ox = 0, oy = 0; bool hasOrigin = false;   // the origin (units); old layouts: worked out from where the grid is
    float spX = 0, spY = 0;              // spacing between tiles across / down (units): tiles shrink to keep the grid's size
    int totalRows = 0;                   // rows of tiles there are (more than the visible rows: it scrolls); 0 = the visible rows
    // Its tiles (no live list): one per cell, top-left to bottom-right, cols x total rows. Each is a little widget of its
    // own - a button (bind: an action setting, or action), a slider (bind: a number setting, or its own min / max / step /
    // value) or a picker (bind: a choice setting, or its own choices / value) - with its label, icon and description.
    std::vector<Widget> tiles;
    std::string label, icon;             // (a tile's)
};
// how many cells a short row is moved across (a grid row starting from the centre / right)
static float GridShift(const Widget& w, int row, int count) {
    int cols = std::max(1, w.cols), inRow = std::min(cols, count - row * cols);
    return inRow <= 0 ? 0.f : (cols - inRow) * 0.5f * std::max(0, std::min(2, w.startAt));
}
// a tile's size (the grid's width / height less the spacing, shared out) and the step from one tile to the next
static float GCellW(const Widget& w) { int c = std::max(1, w.cols); return std::max(1.f, (w.gw - (c - 1) * w.spX) / c); }
static float GCellH(const Widget& w) { int r = std::max(1, w.rows); return std::max(1.f, (w.gh - (r - 1) * w.spY) / r); }
static float GPitchX(const Widget& w) { return GCellW(w) + w.spX; }
static float GPitchY(const Widget& w) { return GCellH(w) + w.spY; }
// The origin: left / centre / right puts the grid's left edge / middle / right edge on it, and its middle is always on it
// up and down. GridPlace moves the grid (and its pieces) there.
static void GridOrigin(Widget& w) {   // (a grid without one yet: where it is now)
    if (w.hasOrigin) return;
    w.ox = w.gx + std::max(0, std::min(2, w.startAt)) * 0.5f * w.gw; w.oy = w.gy + w.gh / 2; w.hasOrigin = true;
}
static void GridPlace(Widget& w) {
    GridOrigin(w);
    float tx = w.ox - std::max(0, std::min(2, w.startAt)) * 0.5f * w.gw, ty = w.oy - w.gh / 2;
    float dx = tx - w.gx, dy = ty - w.gy;
    if (dx == 0 && dy == 0) return;
    for (auto& e : MenuElems()) if (InGroup(e.group, w.group)) { e.x += dx; e.y += dy; }
    w.gx = tx; w.gy = ty; g_dirty = true;
}
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
    auto follow = [&](Widget& w) { if (w.action.compare(0, 5, "page:") == 0 && InGroup(w.action.substr(5), from)) w.action = "page:" + CleanGroup(to + w.action.substr(5 + from.size())); };
    for (auto& w : g_widgets) { follow(w); for (auto& t : w.tiles) follow(t); }
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
static bool g_menuOtherEdits = false; // ... while other editor changes were unsaved: then it isn't saved on its own
static float WGet(const Widget& w) { Setting* s = SettingOf(w.bind); return s && s->get ? s->get() : w.value; }
static void WSet(Widget& w, float v) {
    if (Setting* s = SettingOf(w.bind)) { if (s->set) s->set(v); if (menuChanged) menuChanged(); }
    else { if (!g_menuValDirty) g_menuOtherEdits = g_dirty; w.value = v; g_dirty = true; g_menuValDirty = true; }
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
        if (GroupParent(w.group) == p.group && (w.type == W_BUTTON || w.type == W_SLIDER || w.type == W_PICKER || w.type == W_GRID)) r.push_back({ &w, RowY(w), RowX(w) });
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
    std::string capturing;   // the binding row (its group) waiting for an input; looked up each time, as the widget
                             // list can be replaced (undo, reload, duplicate...) while it waits
    struct GridSt { int sel = -1, top = 0; float scroll = 0; bool locked = false; };   // each grid's cursor (item), top row,
                             // shown scroll; locked: left / right change its slider / picker tile (B lets go)
    std::map<std::string, GridSt> grids;
} g_menu;

// ---------------------------------------------------------------- grids
struct GItem { std::string label, icon, action; int tile = -1; };
static bool GridHasTiles(const Widget& w) { return w.bind.empty(); }   // (not a live list / a choice setting)
static int GridTileCount(const Widget& w) { return std::max(1, w.cols) * std::max(1, w.totalRows > 0 ? w.totalRows : w.rows); }
static std::string TileName(int i) { return "Button " + std::to_string(i + 1); }
static void GridFitTiles(Widget& w) {   // a tile for every cell (more are kept: rows taken away and put back keep theirs)
    if (w.type != W_GRID) return;
    for (int n = GridTileCount(w); (int)w.tiles.size() < n;) { Widget t; t.type = W_BUTTON; t.label = TileName((int)w.tiles.size()); w.tiles.push_back(t); }
}
static Widget* GridTile(Widget& w, int i) {
    if (!GridHasTiles(w) || i < 0 || i >= GridTileCount(w)) return nullptr;
    GridFitTiles(w); return &w.tiles[i];
}
static std::string TileKey(const Widget& g, int tile) { return g.group + "\x01" + std::to_string(tile); }
static std::vector<GItem> GridItemsAll(const Widget& w);
static std::vector<GItem> GridItems(const Widget& w) {   // the items the grid has room for (total rows)
    auto v = GridItemsAll(w);
    if (w.totalRows > 0) { size_t cap = (size_t)w.totalRows * std::max(1, w.cols); if (v.size() > cap) v.resize(cap); }
    return v;
}
static std::vector<GItem> GridItemsAll(const Widget& w) {
    std::vector<GItem> v;
    if (w.bind.compare(0, 5, "list:") == 0) {   // another mod's / REUI's values
        std::string key = w.bind.substr(5), s;
        int n = tokens && tokens(key + ".count", s) ? atoi(s.c_str()) : 0;
        for (int i = 1; i <= std::min(n, 1000); ++i) {
            GItem it; std::string k = key + "." + std::to_string(i);
            if (tokens) { tokens(k, it.label); tokens(k + ".icon", it.icon); }
            v.push_back(it);
        }
        return v;
    }
    if (Setting* st = SettingOf(w.bind)) { if (st->choices) for (auto& c : st->choices()) v.push_back({c, "", ""}); return v; }   // (older grids)
    for (int i = 0, n = GridTileCount(w); i < n; ++i) {   // its tiles
        GItem it; it.tile = i;
        if (i < (int)w.tiles.size()) { const Widget& t = w.tiles[i]; it.label = t.label; it.icon = t.icon; it.action = t.action; }
        else it.label = TileName(i);
        v.push_back(it);
    }
    return v;
}
// Older layouts: a grid's items ("label;icon;action" choices) become its tiles ("Item N" -> "Button N"); grids always
// stretch their pieces to the tile now.
static void GridMigrate(Widget& w) {
    if (w.type != W_GRID) return;
    if (!w.scalePieces) { w.refW = GCellW(w); w.refH = GCellH(w); w.scalePieces = true; }
    if (!w.bind.empty() || !w.tiles.empty() || w.choices.empty()) { w.choices.clear(); GridFitTiles(w); return; }
    for (auto& c : w.choices) {
        Widget t; t.type = W_BUTTON;
        size_t a = c.find(';'), b = a == std::string::npos ? a : c.find(';', a + 1);
        t.label = c.substr(0, a);
        if (a != std::string::npos) t.icon = c.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        if (b != std::string::npos) t.action = c.substr(b + 1);
        if (t.label.compare(0, 5, "Item ") == 0 && t.label.size() > 5 && t.label.find_first_not_of("0123456789", 5) == std::string::npos) t.label = "Button " + t.label.substr(5);
        w.tiles.push_back(t);
    }
    w.choices.clear();
    GridFitTiles(w);
    // the first tile's label piece (the editor shows the pieces themselves as the first tile)
    if (!w.group.empty())
        for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.type == E_TEXT && e.role == "label" && e.text.find('{') == std::string::npos) e.text = w.tiles[0].label;
}
static void GridKeepVisible(const Widget& w, MenuState::GridSt& st, int count);
static MenuState::GridSt& GridState(const Widget& w, int count) {
    auto& st = g_menu.grids[w.group];
    bool first = st.sel < 0;
    if (first) {   // first look: on the setting's value, scrolled to it
        Setting* s = SettingOf(w.bind);
        st.sel = s && s->get ? (int)lroundf(s->get()) : w.bind.empty() ? (int)lroundf(w.value) : 0;
    }
    st.sel = std::max(0, std::min(std::max(0, count - 1), st.sel));
    if (first) { GridKeepVisible(w, st, count); st.scroll = (float)st.top; }
    return st;
}
static void GridKeepVisible(const Widget& w, MenuState::GridSt& st, int count) {
    int cols = std::max(1, w.cols), rows = std::max(1, w.rows);
    int total = (count + cols - 1) / cols, r = st.sel / cols;
    if (r < st.top) st.top = r;
    if (r >= st.top + rows) st.top = r - rows + 1;
    st.top = std::max(0, std::min(std::max(0, total - rows), st.top));
}
// an arrow inside the grid: true if it moved there (false: at the edge - the page takes it)
static bool GridMove(Widget& w, int dx, int dy) {
    auto items = GridItems(w); int n = (int)items.size();
    if (!n) return false;
    auto& st = GridState(w, n);
    int cols = std::max(1, w.cols), c = st.sel % cols, r = st.sel / cols, last = (n - 1) / cols;
    int ns = st.sel;
    if (dx) { int nc = c + dx; if (nc < 0 || nc >= cols || r * cols + nc >= n) return false; ns = r * cols + nc; }
    if (dy) {
        int nr = r + dy; if (nr < 0 || nr > last) return false;
        // the same column on screen (rows can be shifted: short rows starting from the centre / right)
        float vis = c + GridShift(w, r, n);
        int inRow = std::min(cols, n - nr * cols);
        int nc = (int)lroundf(vis - GridShift(w, nr, n));
        nc = std::max(0, std::min(inRow - 1, nc));
        ns = nr * cols + nc;
    }
    st.sel = ns; GridKeepVisible(w, st, n);
    Snd(dy < 0 ? SND_UP : dy > 0 ? SND_DOWN : dx < 0 ? SND_LEFT : SND_RIGHT);
    return true;
}
static void MenuGo(const std::string& page);
static void MenuBack();
void MenuClose();
static void GridAccept(Widget& w) {
    auto items = GridItems(w); int n = (int)items.size();
    if (!n) { Snd(SND_ERROR); return; }
    auto& st = GridState(w, n);
    const GItem& it = items[st.sel];
    if (Widget* t = GridTile(w, it.tile)) {
        if (t->type == W_SLIDER || t->type == W_PICKER) {   // A takes hold of it: left / right change it until B
            if (t->type == W_PICKER && WChoices(*t).empty()) { Snd(SND_ERROR); return; }
            st.locked = true; Snd(SND_ACCEPT); return;
        }
        if (Setting* s = SettingOf(t->bind)) {
            if (s->kind == 3 && s->beginCapture && s->capture) { Snd(SND_ACCEPT); s->beginCapture(); g_menu.capturing = TileKey(w, it.tile); return; }
            if (s->run) { Snd(SND_ACCEPT); s->run(); return; }
        }
    }
    if (Setting* s = SettingOf(w.bind)) { if (s->set) { s->set((float)st.sel); if (menuChanged) menuChanged(); Snd(SND_ACCEPT); return; } }
    if (it.action.compare(0, 5, "page:") == 0 && PageOf(it.action.substr(5))) { Snd(SND_ACCEPT); MenuGo(it.action.substr(5)); return; }
    if (it.action == "back") { MenuBack(); return; }
    if (it.action == "close") { Snd(SND_BACK); MenuClose(); return; }
    if (w.bind.empty() && it.action.empty()) { WSet(w, (float)st.sel); Snd(SND_ACCEPT); return; }   // its own value: the item picked
    Snd(SND_ERROR);
}
static bool GridPiece(const Elem& e) { return e.role != "gtab" && e.role != "gtrack"; }
// ---- tile kinds: 1 button, 2 slider, 4 picker, 8 binding (a button changing a binding setting)
static std::map<std::string, int> g_tileSel;   // the tile picked in each grid's panel (outlined; the editor's first tile shows its kind)
static int TileBit(const Widget* t) {
    if (!t) return 1;
    if (t->type == W_SLIDER) return 2;
    if (t->type == W_PICKER) return 4;
    Setting* s = SettingOf(t->bind); return s && s->kind == 3 ? 8 : 1;
}
// a grid piece on a tile of that kind: its own "shows on" kinds, then by role (value text: sliders / pickers; fill bar +
// tab: sliders; arrows: pickers; binding icon: bindings)
static bool TileShows(const Elem& e, int bit) {
    if (e.tileKinds && !(e.tileKinds & bit)) return false;
    if (e.role == "value" && !(bit & 6)) return false;
    if ((e.role == "fill" || e.role == "tab") && bit != 2) return false;
    if ((e.role == "arrowL" || e.role == "arrowR") && bit != 4) return false;
    if (e.role == "bindicon" && bit != 8) return false;
    return true;
}
// the editor: a grid's pieces are its first tile as the panel's picked tile would show it (the others are hidden)
static bool GridPieceOff(const Elem& e) {
    if (!MenuWs() || e.group.empty()) return false;
    Widget* w = WidgetFor(e.group);
    if (!w || w->type != W_GRID || !GridHasTiles(*w) || !GridPiece(e)) return false;
    auto it = g_tileSel.find(w->group);
    int ts = it == g_tileSel.end() ? 0 : it->second;
    return !TileShows(e, TileBit(ts >= 0 && ts < (int)w->tiles.size() ? &w->tiles[ts] : nullptr));
}
static bool GridHasKind(const Widget& w, int bit) {   // pieces of its own for that kind of tile
    for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.tileKinds && (e.tileKinds & bit) && e.tileKinds != 15) return true;
    return false;
}
// The older menu rows a new kind of tile is made like: a slider (UniPad Options menu / Steering Deadzone), a picker
// (Active Gamepad), a binding (Gamepad Bindings menu / Steering Binding) - those first, else any row of that kind.
static Widget* KindRef(int bit) {
    const char* pref = bit == 2 ? "UniPad Options menu/Steering Deadzone" : bit == 4 ? "UniPad Options menu/Active Gamepad" : "Gamepad Bindings menu/Steering Binding";
    auto fits = [&](Widget& r) {
        if (!PageOf(GroupParent(r.group))) return false;
        Setting* st = SettingOf(r.bind);
        bool want = bit == 2 ? r.type == W_SLIDER : bit == 4 ? r.type == W_PICKER : (r.type == W_BUTTON && st && st->kind == 3);
        if (!want) return false;
        for (auto& e : MenuElems()) if (InGroup(e.group, r.group) && (bit == 2 ? e.role == "fill" : bit == 4 ? e.role == "arrowL" || e.role == "arrowR" : e.role == "bindicon")) return true;
        return false;
    };
    if (Widget* r = WidgetOf(pref)) if (fits(*r)) return r;
    for (auto& r : g_widgets) if (fits(r)) return &r;
    return nullptr;
}
// (no such row in the layout: the same pieces as those rows, built in)
static std::vector<Elem> KindDefault(int bit, float& x0, float& x1) {
    std::vector<Elem> v;
    auto text = [&](const char* name, float x, const char* t, const char* font, int align, const char* role, float c, int layer) {
        Elem e; e.type = E_TEXT; e.name = name; e.anchor = 4; e.x = x; e.y = 0; e.w = 0; e.h = 64; e.text = t; e.font = font; e.align = align;
        e.role = role; e.layer = layer; for (int i = 0; i < 3; ++i) e.col[i] = c; e.col[3] = 1; v.push_back(e);
    };
    auto img = [&](const char* name, float x, float y, float w, float h, const char* tex, const char* role, int layer) {
        Elem e; e.type = E_IMAGE; e.name = name; e.anchor = 4; e.x = x; e.y = y; e.w = w; e.h = h; e.tex = tex; e.role = role; e.layer = layer;
        v.push_back(e); return &v.back();
    };
    if (bit != 8) text("Label", -270, "Label", "conduitmditc_tt21i", 0, "label", 1, 2);
    if (bit == 2) {
        x0 = 53.2f; x1 = 198.8f;
        img("bar outline", 126, 4, 234.9f, 32, "U2_TEMP_SLIDERBG", "", 2)->col[3] = 0.88f;
        Elem* f = img("bar", 53.2f, 0, 0, 5.75f, "WHITE16X16_PURE_WHITE", "fill", 6); f->col[0] = 0.388f; f->col[1] = 0.698f; f->col[2] = 0.133f;
        img("slider", 53.2f, 3, 16, 16, "U2_SLIDERBAR_TAB", "tab", 9);
        text("value", 32, "0%", "conduitmditc_tt14i", 1, "value", 0.753f, 1);
    } else if (bit == 4) {
        Elem* l = img("Arrow", -2.93f, 0, 24, 24, "U2_MENU_ARROW", "arrowL", 2); l->u0 = l->v0 = 1; l->u1 = l->v1 = 0; l->col[3] = 0.588f; l->selOnly = true;
        Elem* r = img("Arrow", 252, 0, 24, 24, "U2_MENU_ARROW", "arrowR", 2); r->col[3] = 0.588f; r->selOnly = true;
        text("value", 124.66f, "Value", "conduitmditc_tt21i", 1, "value", 1, 2);
    } else {
        text("Label", -164, "Label", "conduitmditc_tt21i", 1, "label", 1, 2);
        img("icon (follows the controller)", -72, 0, 32, 32, "icon:auto:LeftStick", "bindicon", 9);
    }
    return v;
}
// A grid gets pieces for a new kind of tile: the reference row's pieces, laid across the tile (their row's width spread
// over the tile less a margin, bars stretched with it, the rest their own size; up / down from the row's label), shown on
// that kind only. The grid's own label / icon pieces aren't shown on it then.
static void GridAddKind(Widget& w, int bit) {
    if (bit == 1 || w.group.empty()) return;
    std::vector<Elem> src; float x0 = 0, x1 = 0;
    if (Widget* r = KindRef(bit)) {
        for (auto& e : MenuElems()) if (InGroup(e.group, r->group) && e.role != "desc" && e.role != "stab" && e.role != "strack" && e.role != "hl") src.push_back(e);
        x0 = r->x0; x1 = r->x1 > r->x0 ? r->x1 : r->x0 + 1;
        Log("ui editor: %s tiles of %s made like %s", bit == 2 ? "slider" : bit == 4 ? "picker" : "binding", w.group.c_str(), r->group.c_str());
    } else src = KindDefault(bit, x0, x1);
    if (src.empty()) return;
    // the row: across, its pieces' extent; up / down, its label
    float bl = 1e9f, br = -1e9f, ly = 0; bool hasLabel = false;
    for (auto& e : src) {
        if (e.role == "label" && !hasLabel) { ly = e.y; hasLabel = true; }
        if (e.role == "fill") { bl = std::min(bl, x0); br = std::max(br, x1); continue; }
        Elem c = e; ImVec2 sz = ElemSize(c);
        float ax = (e.type == E_TEXT && e.w <= 0) ? e.align * 0.5f : (e.anchor % 3) * 0.5f;
        bl = std::min(bl, e.x - ax * sz.x); br = std::max(br, e.x - ax * sz.x + sz.x);
    }
    if (!hasLabel) { float sum = 0; for (auto& e : src) sum += e.y; ly = sum / src.size(); }
    float cw = w.refW > 0.5f ? w.refW : GCellW(w), ch = w.refH > 0.5f ? w.refH : GCellH(w);
    float m = cw * 0.04f, k = (cw - 2 * m) / std::max(1.f, br - bl);
    auto mapX = [&](float x) { return w.gx + m + (x - bl) * k; };
    float cy = w.gy + ch / 2;
    const char* kn = bit == 2 ? "Slider: " : bit == 4 ? "Picker: " : "Binding: ";
    std::string first = w.tiles.empty() ? std::string("{item}") : w.tiles[0].label;
    for (auto& e : src) {
        Elem c = e;
        c.group = w.group; c.kit.clear(); c.tileKinds = bit; c.visible = true; c.name = kn + e.name;
        c.y = cy + (e.y - ly);
        float ax = (e.anchor % 3) * 0.5f;
        if (e.role == "fill") { c.w = (x1 - x0) * k; c.x = mapX(x0) + ax * c.w; }
        else if (e.role == "tab") c.x = mapX(x0);
        else {
            c.x = mapX(e.x);
            if (e.type != E_TEXT && e.w > 3 * e.h) c.w = e.w * k;   // (bars stretch across with the row)
        }
        if (c.role == "label") c.text = first;
        MenuElems().push_back(c);
    }
    for (auto& e : MenuElems())   // the grid's own label / icon: not on this kind
        if (InGroup(e.group, w.group) && e.tileKinds != bit && (e.role == "label" || e.role == "icon") && (e.tileKinds == 0 || (e.tileKinds & bit)))
            e.tileKinds = (e.tileKinds ? e.tileKinds : 15) & ~bit;
    g_dirty = true;
}
// the binding waiting for an input: a row (its group) or a grid's tile ("<grid>\x01<tile>")
static Widget* CaptureWidget() {
    size_t k = g_menu.capturing.find('\x01');
    if (k == std::string::npos) return WidgetOf(g_menu.capturing);
    Widget* g = WidgetOf(g_menu.capturing.substr(0, k));
    return g ? GridTile(*g, atoi(g_menu.capturing.c_str() + k + 1)) : nullptr;
}
// a binding's icon piece: the icon of what it's bound to, or "???" pulsing while it waits (in the font of the group's
// text). false: nothing more to draw
static bool BindIcon(ImDrawList* dl, Elem& e, Setting* bs, bool waiting, const std::string& group) {
    if (waiting) {
        int fi = -1;
        for (auto& o : MenuElems()) if (o.type == E_TEXT && InGroup(o.group, group)) { if (o.fontIdx == -2) o.fontIdx = FindFont(o.font); fi = o.fontIdx; if (o.role == "label") break; }
        if (fi < 0) fi = FindFont("conduitmditc_tt21i");
        if (fi >= 0) {
            ImVec2 a, b; ElemRect(e, a, b);
            const Font& f = g_fonts[fi];
            float k = (b.y - a.y) / std::max(1, f.line);
            float tw = TextWidth(f, "???") * k;
            bool blink = fmod(g_menu.t, 0.8) < 0.55;
            float c[4] = { e.col[0], e.col[1], e.col[2], e.col[3] * (blink ? 1.f : 0.45f) };
            DrawText(dl, f, ImVec2((a.x + b.x - tw) / 2, a.y), k, Col(c), "???", 0, 0);
        }
        return false;
    }
    int id = bs->icon ? bs->icon() : -1;
    if (id < 0 || id >= (int)iconKeys.size()) return false;
    e.tex = "icon:auto:" + iconKeys[id]; e.texIdx = -3;
    return true;
}
// The grid's cells: its pieces copied per item, moved by the cell pitch (and stretched to the cell). on: the grid is the
// page's selected row (its cursor shows). preview: the editor's ghost cells (sample items, the first cell left out, faded).
static void DrawGrid(ImDrawList* dl, const Page* p, Widget& w, bool on, float scrollDy, bool preview) {
    auto items = GridItems(w);
    int cols = std::max(1, w.cols), rows = std::max(1, w.rows);
    if (preview && items.empty())   // (an empty grid: sample items to see it by)
        for (int i = 0; i < cols * rows; ++i) items.push_back({"Button " + std::to_string(i + 1), "", ""});
    int n = (int)items.size();
    if (!n) return;
    MenuState::GridSt dummy; dummy.sel = 0;
    MenuState::GridSt& st = preview ? dummy : GridState(w, n);
    float px = GPitchX(w), py = GPitchY(w);   // (tile to tile: a tile plus the spacing)
    float sx = w.scalePieces && w.refW > 0.5f ? GCellW(w) / w.refW : 1.f, sy = w.scalePieces && w.refH > 0.5f ? GCellH(w) / w.refH : 1.f;
    auto& els = MenuElems();
    std::vector<int> order; for (int i : DrawOrder(els)) if (InGroup(els[i].group, w.group) && els[i].visible && GridPiece(els[i])) order.push_back(i);
    if (order.empty()) return;
    // slider tiles: the fill piece (as drawn in the first tile) is the bar's full length; the tab moves along it
    bool hasFill = false; float fillL = 0, fillW = 0;
    for (int i : order) if (els[i].role == "fill" && els[i].w > 0.5f) {
        const Elem& f = els[i]; fillW = f.w * sx; fillL = w.gx + (f.x - w.gx) * sx - (f.anchor % 3) * 0.5f * fillW; hasFill = true; break;
    }
    // rows partly scrolled in or out are cut at the grid's top / bottom
    ImVec2 ga, gb;
    { Elem box; box.type = E_IMAGE; box.anchor = els[order[0]].anchor; float ax = (box.anchor % 3) * 0.5f, ay = (box.anchor / 3) * 0.5f;
      box.w = w.gw; box.h = w.gh; box.x = w.gx + ax * w.gw; box.y = w.gy + ay * w.gh + scrollDy; ElemRect(box, ga, gb); }
    float scroll = preview ? 0.f : st.scroll;
    int r0 = (int)floorf(scroll), r1 = r0 + rows + (scroll != (float)r0 ? 1 : 0);
    dl->PushClipRect(ImVec2(-10000, ga.y - 2), ImVec2(10000, gb.y + 2), true);
    for (int r = r0; r < r1; ++r)
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            if (idx < 0 || idx >= n) continue;
            if (preview && idx == 0 && GridShift(w, 0, n) == 0) continue;   // (the pieces themselves are the first cell, unless its row is moved)
            bool cur = on && !preview && idx == st.sel;
            const GItem& it = items[idx];
            // a tile: its kind decides which pieces it shows (value text: sliders / pickers; fill + tab: sliders; arrows: pickers)
            const Widget* t = it.tile >= 0 && it.tile < (int)w.tiles.size() ? &w.tiles[it.tile] : nullptr;
            int kind = it.tile < 0 ? -1 : t ? t->type : W_BUTTON;
            std::string val = t && kind != W_BUTTON ? WText(*t) : std::string();
            Setting* bs = t && kind == W_BUTTON ? SettingOf(t->bind) : nullptr; if (bs && bs->kind != 3) bs = nullptr;   // a binding tile
            bool waiting = bs && !preview && g_menu.capturing == TileKey(w, it.tile);
            float ox = (c + GridShift(w, r, n)) * px, oy = (r - scroll) * py;
            for (int i : order) {
                Elem& src = els[i];
                if (src.selOnly && !cur) continue;
                if (kind >= 0 && !TileShows(src, TileBit(t))) continue;
                Elem e = src;
                e.x = w.gx + (e.x - w.gx) * sx + ox;
                e.y = w.gy + (e.y - w.gy) * sy + oy + scrollDy;
                if (e.type != E_TEXT) { e.w *= sx; e.h *= sy; }
                else { if (e.w > 0) e.w *= sx; }   // (text keeps its size)
                if (kind == W_SLIDER && t && hasFill && (e.role == "fill" || e.role == "tab")) {
                    float f = WFrac(*t), ax = (e.anchor % 3) * 0.5f;
                    if (e.role == "fill") { e.w = f * fillW; e.x = fillL + ox + ax * e.w; if (e.w < 0.5f) continue; }
                    else e.x = fillL + ox + f * fillW + (ax - 0.5f) * e.w;
                }
                if (kind == W_PICKER && cur && st.locked && (e.role == "arrowL" || e.role == "arrowR")) {
                    double ft = e.role == "arrowL" ? g_menu.flashL : g_menu.flashR;
                    if (g_menu.t - ft < 0.15) e.col[3] = 1.f;   // lights up when pressed
                }
                if (e.type == E_TEXT) {
                    auto put = [&](const char* tok, const std::string& v) { for (size_t k; (k = e.text.find(tok)) != std::string::npos;) e.text.replace(k, strlen(tok), v); };
                    if (e.role == "label" && e.text.find('{') == std::string::npos) e.text = it.label;
                    if (e.role == "value" && kind >= 0 && e.text.find('{') == std::string::npos) e.text = val;
                    put("{item}", it.label); put("{value}", val); put("{i}", std::to_string(idx + 1)); put("{count}", std::to_string(n));
                    if (bs) { put("{binding}", bs->label); put("{input}", waiting ? std::string("???") : bs->current ? bs->current() : std::string()); }
                    if (cur && p && p->useSel && (e.role == "label" || e.role == "value")) memcpy(e.col, p->selCol, sizeof e.col);
                }
                // its picture: the tile's icon; a binding tile without one (and binding icon pieces): what it's bound to
                bool ownTex = e.role == "icon" || e.role == "bindicon";
                if (bs && (e.role == "bindicon" || (e.role == "icon" && it.icon.empty()))) { if (!BindIcon(dl, e, bs, waiting, w.group)) continue; }
                else if (e.role == "icon") { if (it.icon.empty()) continue; e.tex = it.icon; e.texIdx = -2; }
                if (preview) e.col[3] *= 0.45f;
                if (!ownTex) e.texIdx = src.texIdx;
                e.fontIdx = src.fontIdx;
                DrawElem(dl, e, false);
                if (!ownTex) src.texIdx = e.texIdx;
                src.fontIdx = e.fontIdx;
            }
        }
    dl->PopClipRect();
}
// ---- the editor's view of a grid
// each cell the editor shows (cols x rows, short rows shifted): its box on screen, or a piece's copy in it
static void GridEachCell(Widget& w, const Elem* piece, const std::function<void(ImVec2, ImVec2)>& fn) {
    int cols = std::max(1, w.cols), rows = std::max(1, w.rows);
    int n = (int)GridItems(w).size(); if (!n) n = cols * rows;
    float px = GPitchX(w), py = GPitchY(w), cw = GCellW(w), ch = GCellH(w);
    float sx = w.scalePieces && w.refW > 0.5f ? cw / w.refW : 1.f, sy = w.scalePieces && w.refH > 0.5f ? ch / w.refH : 1.f;
    int anchor = 4;
    for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && GridPiece(e)) { anchor = e.anchor; break; }
    float ax = (anchor % 3) * 0.5f, ay = (anchor / 3) * 0.5f;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            if (idx >= n) continue;
            float ox = (c + GridShift(w, r, n)) * px, oy = r * py;
            ImVec2 a, b;
            if (piece) {
                Elem e = *piece;
                e.x = w.gx + (e.x - w.gx) * sx + ox; e.y = w.gy + (e.y - w.gy) * sy + oy;
                if (e.type != E_TEXT) { e.w *= sx; e.h *= sy; } else { if (e.w > 0) e.w *= sx; }   // (text keeps its size)
                ElemRect(e, a, b);
            } else {
                Elem box; box.type = E_IMAGE; box.anchor = anchor; box.w = cw; box.h = ch;
                box.x = w.gx + ox + ax * cw; box.y = w.gy + oy + ay * ch;
                ElemRect(box, a, b);
            }
            fn(a, b);
        }
}
static bool GridBox(Widget& w, ImVec2& a, ImVec2& b) {   // the whole grid on screen
    a = ImVec2(1e9f, 1e9f); b = ImVec2(-1e9f, -1e9f);
    GridEachCell(w, nullptr, [&](ImVec2 ca, ImVec2 cb) { a.x = std::min(a.x, ca.x); a.y = std::min(a.y, ca.y); b.x = std::max(b.x, cb.x); b.y = std::max(b.y, cb.y); });
    return b.x > a.x;
}
// The grid follows its pieces when they all move together (a group drag / nudge / move), not when they're rearranged
// inside the cell. A grid changed some other way (undo, reload) is just taken as it is.
static void GridFollow() {
    struct Seen { std::vector<ImVec2> at; float gx = 0, gy = 0; };
    static std::map<std::string, Seen> last;
    for (auto& w : g_widgets) {
        if (w.type != W_GRID) continue;
        Seen now; now.gx = w.gx; now.gy = w.gy;
        for (auto& e : MenuElems()) if (InGroup(e.group, w.group)) now.at.push_back(ImVec2(e.x, e.y));
        auto it = last.find(w.group);
        if (it != last.end() && !now.at.empty() && it->second.at.size() == now.at.size() && it->second.gx == w.gx && it->second.gy == w.gy) {
            float dx = now.at[0].x - it->second.at[0].x, dy = now.at[0].y - it->second.at[0].y;
            if (dx != 0 || dy != 0) {
                bool together = true;
                for (size_t i = 0; i < now.at.size() && together; ++i)
                    if (fabsf(now.at[i].x - it->second.at[i].x - dx) > 0.01f || fabsf(now.at[i].y - it->second.at[i].y - dy) > 0.01f) together = false;
                if (together) { GridOrigin(w); w.gx += dx; w.gy += dy; w.ox += dx; w.oy += dy; now.gx = w.gx; now.gy = w.gy; }
            }
        }
        last[w.group] = now;
    }
}
// a grid's own scroller: the tab along the track by the scroll
static bool GridScroller(Widget& w, Elem& e) {
    auto items = GridItems(w); int n = (int)items.size();
    int cols = std::max(1, w.cols), rows = std::max(1, w.rows), total = (n + cols - 1) / cols;
    if (e.role == "gtrack") return total > rows;
    if (total <= rows) return false;
    float trTop = 0, trBot = 0; bool track = false;
    for (auto& o : MenuElems()) if (InGroup(o.group, w.group) && o.role == "gtrack") { trTop = o.y - o.h / 2; trBot = o.y + o.h / 2; track = true; }
    if (!track) return true;
    auto& st = GridState(w, n);
    float half = e.h / 2, f = st.scroll / (float)(total - rows);
    e.y = trTop + half + (trBot - trTop - 2 * half) * std::max(0.f, std::min(1.f, f));
    return true;
}
// the pieces' box -> where the grid starts and the cell's own size (a group just made a grid: one cell as it is)
static void GridFromPieces(Widget& w) {
    float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
    for (auto& e : MenuElems()) {
        if (!InGroup(e.group, w.group) || !GridPiece(e)) continue;
        ImVec2 sz = ElemSize(e);
        float ax = (e.type == E_TEXT && e.w <= 0) ? e.align * 0.5f : (e.anchor % 3) * 0.5f, ay = (e.anchor / 3) * 0.5f;
        float l = e.x - ax * sz.x, t = e.y - ay * sz.y;
        lo[0] = std::min(lo[0], l); lo[1] = std::min(lo[1], t); hi[0] = std::max(hi[0], l + sz.x); hi[1] = std::max(hi[1], t + sz.y);
    }
    if (lo[0] > hi[0]) return;
    w.gx = lo[0]; w.gy = lo[1]; w.refW = std::max(1.f, hi[0] - lo[0]); w.refH = std::max(1.f, hi[1] - lo[1]);
    w.gw = w.refW * std::max(1, w.cols) + (std::max(1, w.cols) - 1) * w.spX; w.gh = w.refH * std::max(1, w.rows) + (std::max(1, w.rows) - 1) * w.spY;
    w.hasOrigin = false; GridOrigin(w);
    g_dirty = true;
}

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
    g_menu.open = false; g_menu.stack.clear(); g_menu.selStack.clear(); g_menu.capturing.clear();
    if (g_menuValDirty) {   // own values of unbound widgets: saved, unless that would also save other unsaved edits
        if (!g_menuOtherEdits) Save();
        else Log("menu: a value changed; not saved on its own because the editor has other unsaved changes (Save there)");
        g_menuValDirty = false;
    }
}
static void MenuBack() {
    Snd(SND_BACK);
    g_menu.stack.pop_back();
    if (g_menu.stack.empty()) { MenuClose(); return; }
    g_menu.sel = g_menu.selStack.empty() ? 0 : g_menu.selStack.back(); if (!g_menu.selStack.empty()) g_menu.selStack.pop_back();
    g_menu.top = 0;
}
static void MenuAccept(Widget& w) {
    if (w.type == W_GRID) { GridAccept(w); return; }
    if (w.type == W_PICKER) { auto c = WChoices(w); if (c.empty()) { Snd(SND_ERROR); return; } WSet(w, (float)(((int)lroundf(WGet(w)) + 1) % (int)c.size())); g_menu.flashR = g_menu.t; Snd(SND_RIGHT); return; }
    if (w.type != W_BUTTON) return;
    if (Setting* s = SettingOf(w.bind)) {
        if (s->kind == 3 && s->beginCapture && s->capture) { Snd(SND_ACCEPT); s->beginCapture(); g_menu.capturing = w.group; return; }
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
    if (!g_menu.capturing.empty()) {   // a binding row is waiting for an input: nothing else moves
        Widget* cw = CaptureWidget();
        Setting* s = cw ? SettingOf(cw->bind) : nullptr;
        int r = s && s->capture ? s->capture(dt) : -1;   // (the row went away meanwhile: cancelled)
        if (r != 0) {
            g_menu.capturing.clear();
            Snd(r > 0 ? SND_ACCEPT : SND_BACK);
            for (auto& b : g_menu.was) b = true;     // the input just used doesn't also act on the menu
            for (auto& x : g_menu.next) x = 1e9;
            if (r > 0 && menuChanged) menuChanged();
        }
        return;
    }
    // a grid's slider / picker tile taken hold of (A): left / right change it, B lets go; nothing else moves meanwhile
    Widget* lockG = n && rows[g_menu.sel].w->type == W_GRID ? rows[g_menu.sel].w : nullptr;
    MenuState::GridSt* lockSt = nullptr; Widget* lockT = nullptr;
    if (lockG) {
        auto items = GridItems(*lockG);
        auto& gs = GridState(*lockG, (int)items.size());
        if (gs.locked) {
            lockSt = &gs;
            lockT = items.empty() ? nullptr : GridTile(*lockG, items[gs.sel].tile);
            if (!lockT || (lockT->type != W_SLIDER && lockT->type != W_PICKER)) { gs.locked = false; lockSt = nullptr; lockT = nullptr; }
        }
    }
    // presses, and repeats while held: 0.35 s, then every 0.09 s (sliders 0.06 s)
    bool now[6] = { in.up, in.down, in.left, in.right, in.accept, in.back };
    bool fire[6] = {};
    for (int i = 0; i < 6; ++i) {
        if (now[i] && !g_menu.was[i]) { fire[i] = true; g_menu.held[i] = 0; g_menu.next[i] = 0.35; }
        else if (now[i] && i < 4) {
            g_menu.held[i] += dt;
            bool slider = n && (lockT ? lockT->type : rows[g_menu.sel].w->type) == W_SLIDER && i >= 2;
            if (g_menu.held[i] >= g_menu.next[i]) { fire[i] = true; g_menu.next[i] += slider ? 0.06 : 0.09; }
        }
        g_menu.was[i] = now[i];
    }
    for (auto& kv : g_menu.grids) {   // grids scroll smoothly to their top row
        auto& st = kv.second;
        st.scroll += ((float)st.top - st.scroll) * (float)std::min(1.0, dt * 16);
        if (fabsf(st.scroll - (float)st.top) < 0.01f) st.scroll = (float)st.top;
    }
    if (lockT) {
        if (fire[5]) { lockSt->locked = false; Snd(SND_BACK); return; }   // B: free to move again (not back a page)
        for (int k = 0; k < 2; ++k) if (fire[2 + k]) MenuChange(*lockT, k ? 1 : -1, g_menu.held[2 + k]);
        return;
    }
    if (fire[5]) { MenuBack(); return; }
    if (!n) return;
    if (rows[g_menu.sel].w->type == W_GRID) {   // arrows move inside the grid first; at its edge the page takes them
        Widget& gw = *rows[g_menu.sel].w;
        if (fire[0] && GridMove(gw, 0, -1)) fire[0] = false;
        if (fire[1] && GridMove(gw, 0, 1)) fire[1] = false;
        if (fire[2] && GridMove(gw, -1, 0)) fire[2] = false;
        if (fire[3] && GridMove(gw, 1, 0)) fire[3] = false;
    }
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
    if (sel >= 0 && n && rows[sel].w->type == W_GRID && GridHasTiles(*rows[sel].w)) {   // a grid: its selected tile's
        Widget& g = *rows[sel].w; auto items = GridItems(g);
        if (!items.empty()) { Widget* t = GridTile(g, items[GridState(g, (int)items.size()).sel].tile); if (t) desc = t->desc; }
    }
    std::set<const Widget*> gridsDrawn;
    auto& els = MenuElems();
    g_drawList = &els;   // (shapes find their cutters here)
    for (int i : DrawOrder(els)) {
        Elem& src = els[i];
        if (!InGroup(src.group, p.group) || !src.visible) continue;
        Elem e = src;
        Widget* w = WidgetFor(e.group);
        auto it = w ? rowIdx.find(w) : rowIdx.end();
        if (it != rowIdx.end()) {                       // a row
            int r = it->second; bool on = r == sel;
            if (p.visRows > 0 && (r < top || r >= top + p.visRows)) continue;
            if (w->type == W_GRID) {   // its cells, all at once where its first piece is drawn; its scroller as itself
                if (!GridPiece(e)) { if (!GridScroller(*w, e)) continue; e.y += scrollDy; }
                else { if (gridsDrawn.insert(w).second) DrawGrid(dl, &p, *w, on, scrollDy, false); continue; }
                e.texIdx = src.texIdx; e.fontIdx = src.fontIdx;
                DrawElem(dl, e, false);
                src.texIdx = e.texIdx; src.fontIdx = e.fontIdx;
                continue;
            }
            if (e.selOnly && !on) continue;
            e.y += scrollDy;
            if (e.role == "value") e.text = WText(*w);
            Setting* bs = SettingOf(w->bind);
            if (bs && bs->kind == 3) {   // a binding row
                if (e.type == E_TEXT) {   // {binding} = the action, {input} = what it's on
                    auto put = [&](const char* tok, const std::string& v) { for (size_t k; (k = e.text.find(tok)) != std::string::npos;) e.text.replace(k, strlen(tok), v); };
                    put("{binding}", bs->label);
                    put("{input}", g_menu.capturing == w->group ? std::string("???") : bs->current ? bs->current() : std::string());
                }
                if (e.role == "bindicon" && !BindIcon(dl, e, bs, g_menu.capturing == w->group, w->group)) continue;
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
        } else if (e.role == "hl") {   // a page piece that follows the selected row (from the row it's laid out on)
            if (sel < 0 || n == 0) continue;
            if (OwnHighlight(*rows[sel].w)) continue;   // (that row brings its own: a grid's selected cell)
            int ref = 0; float best = 1e9f;
            for (int ri = 0; ri < n; ++ri) {   // the row it sits on: nearest in height, then across
                float dd = fabsf(rows[ri].y - src.y) + 0.01f * fabsf(rows[ri].x - src.x);
                if (dd < best) { best = dd; ref = ri; }
            }
            e.y += rows[sel].y - rows[ref].y + scrollDy;
            e.x += (rows[sel].w->col - rows[ref].w->col) * p.colDist;
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
static const char* kRoleKey[] = { "", "label", "value", "fill", "tab", "arrowL", "arrowR", "desc", "stab", "strack", "bindicon", "icon", "gtab", "gtrack", "hl" };
static const char* kRoleName[] = { "(none)", "label", "value text", "slider fill bar", "slider tab", "left arrow", "right arrow",
                                   "page: description text", "page: scroller tab", "page: scroller track", "binding icon (??? while waiting)",
                                   "grid: item icon", "grid: scroller tab", "grid: scroller track", "page: highlight (follows the selected row)" };
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
    const int nRoles = (int)(sizeof kRoleKey / sizeof kRoleKey[0]);
    int r = 0; for (int i = 0; i < nRoles; ++i) if (e.role == kRoleKey[i]) r = i;
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("role", &r, kRoleName, nRoles)) { e.role = kRoleKey[r]; g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What this piece does in its menu row (or page):\n"
        "label / value text: the value text shows the setting; both take the page's selected colour.\n"
        "slider fill bar / tab: sized / moved by the value. Arrows: light up when pressed.\n"
        "Grids: the label shows the item ({item} / {i} / {count} in any text), the item icon is its picture;\n"
        "\"selected row only\" pieces show on the selected cell only.\n"
        "page: highlight - a piece on the page itself (not in a row) that moves to the selected row, from the row\n"
        "it's laid out on (and across to its column): a shape behind the selected row, say.");
    ImGui::SameLine(); if (ImGui::Checkbox("selected row only", &e.selOnly)) g_dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hidden on rows that aren't selected (the game's picker arrows).");
    Widget* gw = WidgetFor(e.group);
    if (gw && gw->type == W_GRID && GridHasTiles(*gw) && GridPiece(e)) {   // a grid's piece: the kinds of tile it's on
        ImGui::TextUnformatted("shows on"); 
        static const char* kn[] = {"buttons", "sliders", "pickers", "bindings"};
        int mask = e.tileKinds ? e.tileKinds : 15;
        for (int i = 0; i < 4; ++i) {
            ImGui::SameLine(); bool on = (mask >> i) & 1;
            if (ImGui::Checkbox(kn[i], &on)) { mask = on ? mask | (1 << i) : mask & ~(1 << i); e.tileKinds = mask == 15 ? 0 : mask; g_dirty = true; }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The kinds of tile this piece is drawn on (by role too: value text on sliders / pickers,\nfill bar + tab on sliders, arrows on pickers, binding icon on bindings).");
    }
}
static void SettingCombo(Widget& w) {
    int want = w.type == W_SLIDER ? 0 : w.type == W_PICKER || w.type == W_GRID ? 1 : 2;
    Setting* cur = SettingOf(w.bind);
    ImGui::SetNextItemWidth(220);
    std::string curName = cur ? (cur->kind == 3 ? "bind: " + cur->label : cur->label) : w.bind.compare(0, 5, "list:") == 0 ? w.bind : (w.type == W_BUTTON ? "(its own action)" : "(its own value)");
    if (ImGui::BeginCombo("changes", curName.c_str())) {
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
// The grid resized (width / height / columns / rows): with "stretch pieces" on, the first cell's pieces are resized to
// the new cell about the grid's top-left (so in the editor they stay the cell as it'll be drawn); its scroller keeps
// its place beside the grid and its height along it.
static void GridResize(Widget& w, float ngw, float ngh, int ncols, int nrows, float nspX, float nspY) {
    ncols = std::max(1, std::min(32, ncols)); nrows = std::max(1, std::min(32, nrows));
    ngw = std::max(4.f, ngw); ngh = std::max(4.f, ngh);
    nspX = std::max(0.f, nspX); nspY = std::max(0.f, nspY);
    float opx = GCellW(w), opy = GCellH(w);   // (tile sizes, before and after)
    float npx = std::max(1.f, (ngw - (ncols - 1) * nspX) / ncols), npy = std::max(1.f, (ngh - (nrows - 1) * nspY) / nrows);
    // (a grid whose pieces were made for another cell size, drawn stretched: they're made the cell's size first)
    float bx = w.scalePieces && w.refW > 0.5f ? opx / w.refW : 1.f, by = w.scalePieces && w.refH > 0.5f ? opy / w.refH : 1.f;
    float kx = w.scalePieces && opx > 0.01f ? npx / opx * bx : 1.f, ky = w.scalePieces && opy > 0.01f ? npy / opy * by : 1.f;
    float ngx = w.gx;   // (resized from its top-left here; then placed on its origin)
    GridOrigin(w);
    float kh = w.gh > 0.01f ? ngh / w.gh : 1.f;
    for (auto& e : MenuElems()) {
        if (!InGroup(e.group, w.group)) continue;
        if (!GridPiece(e)) {   // its scroller: beside the grid's right side, along its height
            e.x += (ngx + ngw) - (w.gx + w.gw); e.y = w.gy + (e.y - w.gy) * kh;
            if (e.role == "gtrack") e.h *= kh;
            continue;
        }
        e.x = ngx + (e.x - w.gx) * kx; e.y = w.gy + (e.y - w.gy) * ky;
        if (kx == 1.f && ky == 1.f) continue;
        if (e.type == E_TEXT) { if (e.w > 0) e.w *= kx; }   // (text keeps its size: only its place and box width follow the tile)
        else {
            e.w *= kx; e.h *= ky;
            if (e.type == E_SHAPE) { float kr = std::min(kx, ky); for (float& r : e.rad) r *= kr; }
        }
    }
    if (w.scalePieces) { w.refW = npx; w.refH = npy; }
    w.gx = ngx; w.gw = ngw; w.gh = ngh; w.cols = ncols; w.rows = nrows; w.spX = nspX; w.spY = nspY;
    GridPlace(w);
    g_dirty = true;
}
// a tile's "changes": buttons - an action (nothing / go back / close / open a page) or an action setting; sliders - a
// number setting or their own value; pickers - a choice setting or their own value
static void TileChanges(Widget& t) {
    Setting* cur = SettingOf(t.bind);
    std::string name = cur ? (cur->kind == 3 ? "bind: " + cur->label : cur->label) : t.type != W_BUTTON ? "(its own value)" : t.action.empty() ? "(nothing)" : t.action == "back" ? "go back"
                     : t.action == "close" ? "close the menu" : "open " + t.action.substr(5);
    int want = t.type == W_SLIDER ? 0 : t.type == W_PICKER ? 1 : 2;
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("changes", name.c_str())) {
        if (t.type == W_BUTTON) {
            if (ImGui::Selectable("(nothing)", !cur && t.action.empty())) { t.bind.clear(); t.action.clear(); g_dirty = true; }
            if (ImGui::Selectable("go back", !cur && t.action == "back")) { t.bind.clear(); t.action = "back"; g_dirty = true; }
            if (ImGui::Selectable("close the menu", !cur && t.action == "close")) { t.bind.clear(); t.action = "close"; g_dirty = true; }
            for (auto& pg : g_pages) if (ImGui::Selectable(("open " + pg.group).c_str(), !cur && t.action == "page:" + pg.group)) { t.bind.clear(); t.action = "page:" + pg.group; g_dirty = true; }
            ImGui::Separator();
        } else if (ImGui::Selectable("(its own value)", !cur)) { t.bind.clear(); g_dirty = true; }
        for (auto& st : settings) {
            if (st.kind != want && !(want == 2 && st.kind == 3)) continue;
            std::string lb = st.kind == 3 ? "bind: " + st.label : st.label;
            if (ImGui::Selectable(lb.c_str(), cur == &st)) { t.bind = st.key; t.action.clear(); g_dirty = true; }
        }
        ImGui::EndCombo();
    }
}
static void GridPanel(Widget& w) {
    GridFitTiles(w);
    bool tiles = GridHasTiles(w);
    // columns | visible rows | total rows
    int cols = w.cols, rows = w.rows, total = tiles ? (w.totalRows > 0 ? w.totalRows : w.rows) : w.totalRows;
    ImGui::SetNextItemWidth(100); if (ImGui::InputInt("columns", &cols)) { GridResize(w, w.gw, w.gh, cols, rows, w.spX, w.spY); GridFitTiles(w); }
    ImGui::SameLine(); ImGui::SetNextItemWidth(100); if (ImGui::InputInt("visible rows", &rows)) { GridResize(w, w.gw, w.gh, w.cols, rows, w.spX, w.spY); GridFitTiles(w); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rows on screen at once (the grid's height is these); the rest scroll up and down.");
    ImGui::SameLine(); ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("total rows", &total)) { w.totalRows = std::max(tiles ? 1 : 0, std::min(1000, total)); GridFitTiles(w); g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tiles ? "Rows of tiles the grid has. More than the visible rows: it scrolls up and down."
                                                        : "Rows the live list can fill (0 = as many as it has). More than the visible rows: it scrolls.");
    // width | height | align
    float gwv = w.gw, ghv = w.gh;
    ImGui::SetNextItemWidth(150); if (ImGui::SliderFloat("width", &gwv, 20.f, 1280.f, "%.0f")) GridResize(w, gwv, w.gh, w.cols, w.rows, w.spX, w.spY);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The grid on screen (units; Ctrl + click to type). Tiles share it out, less the spacing; their pieces stretch with them.");
    ImGui::SameLine(); ImGui::SetNextItemWidth(150); if (ImGui::SliderFloat("height", &ghv, 20.f, 1280.f, "%.0f")) GridResize(w, w.gw, ghv, w.cols, w.rows, w.spX, w.spY);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its visible rows' height (units; Ctrl + click to type).");
    static const char* aligns[] = {"left", "centre", "right"};
    GridOrigin(w);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    int al = std::max(0, std::min(2, w.startAt));
    if (ImGui::Combo("align", &al, aligns, 3) && al != w.startAt) { w.startAt = al; GridPlace(w); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The grid's origin is its centre as placed (move the grid to move it). Left: the grid's left edge on\nthe origin. Centre: its middle on it. Right: its right edge on it. Always centred on it up and down;\nresizing keeps it so, and rows with fewer items line up the same way.");
    // spacing across | down (tiles keep their size: the grid grows by the gaps)
    float sp[2] = {w.spX, w.spY};
    ImGui::SetNextItemWidth(150); bool spc = ImGui::SliderFloat("spacing across", &sp[0], 0.f, 100.f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gap between columns (units; Ctrl + click to type). The tiles keep their size and move apart:\nthe grid grows by the gaps (around its origin).");
    ImGui::SameLine(); ImGui::SetNextItemWidth(150); spc |= ImGui::SliderFloat("spacing down", &sp[1], 0.f, 100.f, "%.1f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gap between rows (units; Ctrl + click to type).");
    if (spc) {
        int c = std::max(1, w.cols), r = std::max(1, w.rows);
        float cw = GCellW(w), ch = GCellH(w);
        GridResize(w, cw * c + (c - 1) * std::max(0.f, sp[0]), ch * r + (r - 1) * std::max(0.f, sp[1]), w.cols, w.rows, sp[0], sp[1]);
    }
    ImGui::Separator();
    if (!tiles && !SettingOf(w.bind)) ImGui::TextDisabled("Its tiles come from the live list (%s): clear it below to make your own.", w.bind.c_str() + 5);
    else if (!tiles) {   // (an older grid: its items are a choice setting's)
        ImGui::TextDisabled("Its items are the choices of \"%s\".", SettingOf(w.bind)->label.c_str());
        ImGui::SameLine(); if (ImGui::SmallButton("use tiles instead")) { w.bind.clear(); GridFitTiles(w); g_dirty = true; }
    } else {
        int n = GridTileCount(w);
        int& ts = g_tileSel[w.group]; ts = std::max(0, std::min(n - 1, ts));
        Widget& t = w.tiles[ts];
        // tile (rename under it) | widget | changes
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(160);
        std::string shown = t.label.empty() ? "(no name) " + std::to_string(ts + 1) : t.label;
        if (ImGui::BeginCombo("##tile", shown.c_str())) {
            for (int i = 0; i < n; ++i) {
                std::string lb = (w.tiles[i].label.empty() ? "(no name) " + std::to_string(i + 1) : w.tiles[i].label) + "##t" + std::to_string(i);
                if (ImGui::Selectable(lb.c_str(), i == ts)) ts = i;
                if (i == ts) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The tile to set up, top-left to bottom-right (outlined on screen).");
        static char rb[128]; static std::string rbFor = "\x01";
        std::string key = w.group + "#" + std::to_string(ts);
        if (rbFor != key) { snprintf(rb, sizeof rb, "%s", t.label.c_str()); rbFor = key; }
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputTextWithHint("##rename", "rename", rb, sizeof rb)) {
            t.label = rb; g_dirty = true;
            if (ts == 0)   // (the pieces themselves are the first tile in the editor)
                for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.type == E_TEXT && e.role == "label" && e.text.find('{') == std::string::npos) e.text = t.label;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rename the tile.");
        ImGui::EndGroup();
        static const char* kinds[] = {"Button", "Slider", "Picker (Left / Right)"};
        int k = t.type == W_SLIDER ? 1 : t.type == W_PICKER ? 2 : 0;
        ImGui::SameLine(); ImGui::SetNextItemWidth(170);
        if (ImGui::Combo("widget##tile", &k, kinds, 3)) {
            int nt = k == 1 ? W_SLIDER : k == 2 ? W_PICKER : W_BUTTON;
            if (nt != t.type) { t.type = nt; t.bind.clear(); t.action.clear(); t.value = 0; g_dirty = true; if (!GridHasKind(w, TileBit(&t))) GridAddKind(w, TileBit(&t)); }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("What the tile is. Sliders and pickers: A takes hold of it, left / right change it, B lets go.\nIts pieces show by role: value text on sliders / pickers, fill bar + tab on sliders, arrows on pickers.");
        { int was = TileBit(&t); ImGui::SameLine(); TileChanges(t); int now = TileBit(&t); if (now != was && !GridHasKind(w, now)) GridAddKind(w, now); }
        if (TileBit(&t) != 1 && !GridHasKind(w, TileBit(&t))) {
            ImGui::SameLine(); if (ImGui::SmallButton("add its pieces")) GridAddKind(w, TileBit(&t));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The grid has no pieces of its own for this kind of tile: copy them from the older menu row of that kind.");
        }
        // (sliders / pickers of their own: their range / choices)
        if (t.type == W_SLIDER && !SettingOf(t.bind)) {
            ImGui::SetNextItemWidth(220); if (ImGui::DragFloat3("min / max / step", &t.min, 0.01f)) g_dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its own value's range and step (the fill bar's full length is the range).");
        }
        if (t.type == W_PICKER && !SettingOf(t.bind)) {
            static char cb[512]; static std::string cbFor = "\x01";
            if (cbFor != key) { std::string j; for (size_t i = 0; i < t.choices.size(); ++i) j += (i ? ", " : "") + t.choices[i]; snprintf(cb, sizeof cb, "%s", j.c_str()); cbFor = key; }
            ImGui::SetNextItemWidth(320);
            if (ImGui::InputTextWithHint("choices", "Off, On, Auto", cb, sizeof cb)) {
                t.choices.clear(); std::string s = cb, cur;
                for (size_t i = 0; i <= s.size(); ++i) {
                    if (i == s.size() || s[i] == ',') { while (!cur.empty() && cur[0] == ' ') cur.erase(0, 1); while (!cur.empty() && cur.back() == ' ') cur.pop_back(); if (!cur.empty()) t.choices.push_back(cur); cur.clear(); }
                    else cur += s[i];
                }
                g_dirty = true;
            }
        }
        // icon
        static char ib[160]; static std::string ibFor = "\x01";
        if (ibFor != key) { snprintf(ib, sizeof ib, "%s", t.icon.c_str()); ibFor = key; }
        ImGui::SetNextItemWidth(320);
        if (ImGui::InputTextWithHint("icon", "texture name, icon:auto:South, vec:...", ib, sizeof ib)) { t.icon = ib; g_dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The picture shown by the tile's \"grid: item icon\" piece: a texture name, icon:auto:<button>\n(the controller's icon for it) or vec:<file>. Empty: no picture (a binding tile: what it's bound to).");
        // description
        static char db[256]; static std::string dbFor = "\x01";
        if (dbFor != key) { snprintf(db, sizeof db, "%s", t.desc.c_str()); dbFor = key; }
        ImGui::SetNextItemWidth(530);
        if (ImGui::InputTextWithHint("description", "shown by the page's description text", db, sizeof db)) { t.desc = db; g_dirty = true; }
    }
    ImGui::Separator();
    // a live list instead of tiles
    if (!SettingOf(w.bind)) {
        static char lb[64]; static std::string lbFor = "\x01";
        if (lbFor != w.group) { snprintf(lb, sizeof lb, "%s", w.bind.compare(0, 5, "list:") == 0 ? w.bind.c_str() + 5 : ""); lbFor = w.group; }
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputTextWithHint("live list (values)", "key (empty: its own tiles)", lb, sizeof lb)) { w.bind = lb[0] ? std::string("list:") + lb : ""; GridFitTiles(w); g_dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tiles from another mod's live list instead: <key>.count items, each <key>.N (its text) and <key>.N.icon (its picture).");
    }
}
static void WidgetPanel(Widget& w, Page* page) {
    int t = w.type;
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("widget", &t, kWType, W_NTYPES)) {
        if (t == W_NONE) { std::string g = w.group; MenuForget(g); g_dirty = true; return; }
        w.type = t; DetectRoles(w);
        if (t == W_GRID) { GridFromPieces(w); w.bind.clear(); GridFitTiles(w); }
    }
    if (w.type == W_GRID) { GridPanel(w); (void)page; return; }   // (its tiles have their own settings)
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
    (void)page;
}
// a new grid on the page: one cell's pieces (backing, text, selected frame, icon) + a scroller beside it
static void AddGrid(Page& p) {
    std::string g; for (int i = 1; ; ++i) { g = p.group + "/Grid" + (i > 1 ? " " + std::to_string(i) : ""); if (!WidgetOf(g)) { bool used = false; for (auto& e : MenuElems()) if (InGroup(e.group, g)) used = true; if (!used) break; } }
    // the default: 1 column, 8 rows of 550 x 30 tiles 7.7 apart, centred (as menu_big's grid)
    Widget w; w.group = g; w.type = W_GRID; w.cols = 1; w.rows = 8; w.totalRows = 8; w.spX = 0; w.spY = 7.7f;
    w.refW = 550; w.refH = 30; w.gw = 550; w.gh = 8 * 30 + 7 * 7.7f; w.gx = -16 - w.gw / 2; w.gy = -w.gh / 2;
    w.col = p.addCol;
    GridFitTiles(w);   // (Button 1, 2, 3...)
    w.startAt = 1; GridOrigin(w);   // (its origin: its centre)
    float cx = w.gx + w.refW / 2, cy = w.gy + w.refH / 2;
    auto img = [&](const char* name, float x, float y, float ww, float hh, const char* tok, float a) {
        Elem e; e.type = E_IMAGE; e.name = name; e.group = g; e.anchor = 4; e.x = x; e.y = y; e.w = ww; e.h = hh; e.tex = "WHITE16X16"; e.texIdx = -2;
        e.colTok = tok; e.colTokA = a; MenuElems().push_back(e); return &MenuElems().back();
    };
    img("Cell", cx, cy, 517, 27.75f, "hud.shade", 0.55f);
    Elem* sel = img("Cell selected", cx, cy, 517, 27.75f, "hud.accent", 0.3f); sel->selOnly = true; sel->layer = 1;
    Elem* ic = img("Icon", cx, cy - 3.75f, 198, 13.5f, "", 1); ic->role = "icon"; ic->layer = 2; ic->tex.clear();
    Elem t; t.type = E_TEXT; t.name = "Item text"; t.group = g; t.anchor = 4; t.x = cx; t.y = cy + 5.25f; t.w = 0; t.align = 1; t.text = "{item}";
    t.role = "label"; t.layer = 3; t.colTok = "hud.text"; t.colTokA = 1;
    int fi = FindFont("conduitmditc_tt14i"); if (fi < 0 && !g_fonts.empty()) fi = 0;
    if (fi >= 0) { t.font = g_fonts[fi].name; t.fontIdx = fi; }
    MenuElems().push_back(t);
    Elem* tr = img("Scroller track", w.gx + w.gw + 10, w.gy + w.gh / 2, 4, w.gh, "hud.line", 0.3f); tr->role = "gtrack";
    Elem* tb = img("Scroller tab", w.gx + w.gw + 10, w.gy + 20, 8, 40, "hud.accent", 1); tb->role = "gtab"; tb->layer = 1;
    g_widgets.push_back(w);
    ResolveTokens(); g_dirty = true;
    g_selGroup = g; g_sel = -1;
    Log("ui editor: grid added as %s", g.c_str());
}
// (Deprecated 2026-10-27, no longer in the editor: Build rows, Restack rows, row spacing / first row y, columns -
// Start a new column / column distance / first column x / add to column -, Add row from the widget kit, row up /
// down, a row's column, Save as template and the Widget kit panel. Grids replace them. Pages made with them still
// load, save and work: BuildRows / Restack / StampRow / SaveTemplate / SwapRows / KitPanel are kept for that only.)
static void PagePanel(Page& p) {
    ImGui::SetNextItemWidth(80); if (ImGui::InputInt("rows shown", &p.visRows)) { p.visRows = std::max(0, p.visRows); g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = all. More rows than this scroll (the scroller tab follows).");
    ImGui::SameLine(); if (ImGui::Checkbox("menu key opens this", &p.start)) { if (p.start) for (auto& o : g_pages) if (&o != &p) o.start = false; g_dirty = true; }
    ImGui::SameLine(); if (ImGui::Checkbox("selected row colour", &p.useSel)) g_dirty = true;
    ImGui::SameLine(); if (ImGui::ColorEdit4("##selcol", p.selCol, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) g_dirty = true;
    auto rows = PageRows(p);
    int grids = 0; for (auto& r : rows) grids += r.w->type == W_GRID;
    ImGui::TextDisabled("%d row(s), %d grid(s)%s", (int)rows.size() - grids, grids, PageHighlight(p) ? ", highlight" : "");
    if (ImGui::Button("Add grid")) AddGrid(p);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A 4 x 3 grid of cells (a backing, the item's text, a selected-cell frame, its own scroller),\nwith sample items to start from. Its pieces are the first cell: change them and every cell follows.\nA grid of buttons, one column wide, is a list of menu buttons.");
    ImGui::SameLine();
    if (ImGui::Button(g_menu.open ? "Close the menu" : "Try it")) { if (g_menu.open) MenuClose(); else MenuOpen(p.group); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens this page as the real menu: pad / arrow keys, Enter, Esc.");
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
            if (t == W_GRID) GridFromPieces(*WidgetOf(g));
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
    if (w.type == W_GRID) {
        Widget c = w; GridOrigin(c);
        fprintf(f, "grid=%d %d %g %g %g %g %g %g %d %d %g %g %g %g %d\n", w.cols, w.rows, w.gw, w.gh, w.gx, w.gy, w.refW, w.refH, w.scalePieces ? 1 : 0, w.startAt, c.ox, c.oy, w.spX, w.spY, w.totalRows);
        // its tiles: "tile=<label>" starts one, the lines after it are its own
        for (int i = 0, n = std::min((int)w.tiles.size(), GridTileCount(w)); i < n; ++i) {
            const Widget& t = w.tiles[i];
            fprintf(f, "tile=%s\n", Esc(t.label).c_str());
            if (t.type != W_BUTTON) fprintf(f, "ttype=%s\n", kWKey[t.type]);
            if (!t.bind.empty()) fprintf(f, "tbind=%s\n", t.bind.c_str());
            if (!t.action.empty()) fprintf(f, "taction=%s\n", Esc(t.action).c_str());
            if (!t.icon.empty()) fprintf(f, "ticon=%s\n", Esc(t.icon).c_str());
            if (!t.desc.empty()) fprintf(f, "tdesc=%s\n", Esc(t.desc).c_str());
            if (!t.choices.empty()) { std::string j; for (size_t k = 0; k < t.choices.size(); ++k) j += (k ? "|" : "") + t.choices[k]; fprintf(f, "tchoices=%s\n", Esc(j).c_str()); }
            if (t.type != W_BUTTON) fprintf(f, "tnum=%g %g %g %g\n", t.min, t.max, t.step, t.value);
        }
    }
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
    else if (k == "grid") { int sp = 1; int got = sscanf(v.c_str(), "%d %d %f %f %f %f %f %f %d %d %f %f %f %f %d", &w.cols, &w.rows, &w.gw, &w.gh, &w.gx, &w.gy, &w.refW, &w.refH, &sp, &w.startAt, &w.ox, &w.oy, &w.spX, &w.spY, &w.totalRows); w.scalePieces = sp != 0; w.hasOrigin = got >= 12; w.spX = std::max(0.f, w.spX); w.spY = std::max(0.f, w.spY); w.totalRows = std::max(0, w.totalRows); w.startAt = std::max(0, std::min(2, w.startAt)); w.cols = std::max(1, w.cols); w.rows = std::max(1, w.rows); }
    else if (k == "num") sscanf(v.c_str(), "%f %f %f %f", &w.min, &w.max, &w.step, &w.value);
    else if (k == "tile") { Widget t; t.type = W_BUTTON; t.label = Unesc(v); w.tiles.push_back(t); }
    else if (k.size() > 1 && k[0] == 't' && !w.tiles.empty()) {   // the last tile's
        Widget& t = w.tiles.back(); std::string tk = k.substr(1);
        if (tk == "type") { for (int i = 0; i < W_NTYPES; ++i) if (v == kWKey[i]) t.type = i; if (t.type != W_SLIDER && t.type != W_PICKER) t.type = W_BUTTON; }
        else if (tk == "bind" || tk == "action" || tk == "desc" || tk == "choices" || tk == "num") LoadWidgetKey(t, tk, v);
        else if (tk == "icon") t.icon = Unesc(v);
        else return false;
    }
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
    for (auto& w : g_widgets) GridMigrate(w);
    for (auto& k : g_kitT) GridMigrate(k.w);
    g_pages.erase(std::remove_if(g_pages.begin(), g_pages.end(), [](const Page& p) { return p.group.empty(); }), g_pages.end());
    g_widgets.erase(std::remove_if(g_widgets.begin(), g_widgets.end(), [](const Widget& w) { return w.group.empty(); }), g_widgets.end());
}
static bool MenuOpenNow() { return g_menu.open; }
