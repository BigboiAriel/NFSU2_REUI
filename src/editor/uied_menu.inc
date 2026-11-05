// UI editor menus (UniPad) - included by uied.cpp after Elem / the layout. See uied.h "menus".
//
// A menu is a custom menu page (the editor's menu bar): a group of its own, sub menus being groups inside it. Its
// rows are grids and carousels (groups directly inside it made widgets): tiles drawn per cell from the pieces of
// the first one, with roles - label, value (a slider / picker tile's value), fill + tab (slider), arrowL / arrowR
// (picker; a carousel's ends), bindicon, icon (the tile's picture), gtab / gtrack (its scroller) - and anywhere in
// the page: desc (the selected tile's description). Pieces marked "selected tile only" show on the selected tile.
// (2026-11-03: the older menus - groups ticked "menu page", one-row widgets (button / slider / picker / highlight
// rows), the widget kit and its templates, page scrolling / columns / highlight - were taken out: grids and
// carousels replaced them. Layouts with them load: old pages become custom menus, the rest is left out.)

// a row's group name from a label: "/" would make it a group inside a group (then it isn't a row of the page)
static std::string RowName(std::string label) {
    for (auto& c : label) if (c == '/' || c == '\\') c = '-';
    while (!label.empty() && label.back() == ' ') label.pop_back();
    while (!label.empty() && label[0] == ' ') label.erase(0, 1);
    return label;
}

// the menu tools show while the Menus workspace is the one being edited
static bool MenuWs() { return menuMode || g_editMode == MENUS; }

// (button / slider / picker: the kinds of tile; highlight: an older one-row widget, only so its layouts still read)
enum WType { W_NONE = 0, W_BUTTON, W_SLIDER, W_PICKER, W_HIGHLIGHT, W_GRID, W_CAROUSEL, W_NTYPES };
static const char* kWKey[] = { "none", "button", "slider", "picker", "highlight", "grid", "carousel" };

struct Widget {
    std::string group;
    int type = W_BUTTON;
    std::string bind;                    // Setting key ("" = its own value / action)
    std::string desc;                    // shown by the page's description text while selected
    std::string action;                  // buttons: "page:<group>", "back", "close" ("" = nothing)
    std::vector<std::string> choices;    // pickers with no setting
    float min = 0, max = 1, step = 0.05f, value = 0;   // own value (unbound): number, or choice index
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
    bool rowSpace = true;                // its slider / picker pieces are laid out across the whole row (older layouts: across a tile, converted on load)
    // Its tiles (no live list): one per cell, top-left to bottom-right, cols x total rows. Each is a little widget of its
    // own - a button (bind: an action setting, or action), a slider (bind: a number setting, or its own min / max / step /
    // value) or a picker (bind: a choice setting, or its own choices / value) - with its label, icon and description.
    std::vector<Widget> tiles;
    std::string label, icon;             // (a tile's)
    // Carousels: a grid's tiles on one row, cols of them on screen at once (gw x gh), totalRows of them in all; left /
    // right scroll the row (as the game's icon menus: the pause menu). centred: the selected tile stays in the middle
    // slot (the row slides under it); otherwise the cursor moves and the row scrolls at the edges. wrap: past the last
    // comes the first. Its "left arrow" / "right arrow" pieces stay at their own places (light up when pressed); its
    // scroller tab moves across its track.
    bool centred = true, wrap = false;
};
static bool GridLike(const Widget& w) { return w.type == W_GRID || w.type == W_CAROUSEL; }   // (tiles, drawn per cell)
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
    bool start = false;                  // opened by the menu key
    bool useSel = true; float selCol[4] = { 202 / 255.f, 244 / 255.f, 138 / 255.f, 1.f };   // selected row's label / value
    bool dedicated = true;               // a custom menu (all are now; saved for older builds)
    bool pauseMenu = false;              // opened instead of the game's pause menu (by the menu driver, UniPad)
    bool pauseOptions = false;           // opened instead of the pause menu's Options screen (Audio / Display / Gameplay / Player)
    bool confirm = false;                // opened instead of the game's Yes / No confirm dialogs ({dialog.text}: its question)
    bool worldMap = false;               // opened instead of the game's World Map (UniPad drives the game's own with its buttons)
};
static std::vector<Widget> g_widgets;
static std::vector<Page> g_pages;

std::vector<Setting> settings;
std::function<void(int)> menuSound;
std::function<void()> menuChanged;

static Widget* WidgetOf(const std::string& g) { for (auto& w : g_widgets) if (w.group == g) return &w; return nullptr; }
static Page* PageOf(const std::string& g) { for (auto& p : g_pages) if (p.group == g) return &p; return nullptr; }
// The page a group belongs to: itself or the nearest group around it that's a page ("" = none). A custom menu can sit
// inside another (a sub menu: "Pause 1/Unipad Options"); its pieces are its own, not the menu around it's.
static std::string OwnerPage(const std::string& g) { for (std::string x = g; !x.empty(); x = GroupParent(x)) if (PageOf(x)) return x; return std::string(); }
static std::string ParentMenu(const std::string& page) { return OwnerPage(GroupParent(page)); }   // the menu a sub menu is in ("" = top level)
static Setting* SettingOf(const std::string& k) { if (k.empty()) return nullptr; for (auto& s : settings) if (s.key == k) return &s; return nullptr; }
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
        if (GroupParent(w.group) == p.group && GridLike(w)) r.push_back({ &w, RowY(w), RowX(w) });
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
// ---------------------------------------------------------------- runtime
static struct MenuState {
    bool open = false;
    std::vector<std::string> stack;      // pages opened, the last is shown
    std::vector<int> selStack;           // selected row on each
    int sel = 0;                         // the selected row (grid / carousel)
    bool was[6] = {}; double held[6] = {}, next[6] = {};
    double flashL = 0, flashR = 0, t = 0;
    std::string capturing;   // the binding row (its group) waiting for an input; looked up each time, as the widget
                             // list can be replaced (undo, reload, duplicate...) while it waits
    struct GridSt { int sel = -1, top = 0; float scroll = 0; bool locked = false; int wantCol = -1; };   // each grid's cursor (item), top row,
                             // shown scroll; locked: left / right change its slider / picker tile (B lets go)
    std::map<std::string, GridSt> grids;
} g_menu;
// Fades (menuFade seconds; 0 = none): a page fades in as the menu opens, out as it closes (the game showing through),
// and pages cross-fade when one opens another / goes back. customMenus off: no page stands in for the game's menus
// (pause, its Options, the Yes / No dialogs, the World Map) - the game's own show; the menu key's page still opens.
float menuFade = 0.15f;
bool customMenus = true;
static double g_menuOpenAt = -100;   // when the page now shown came up
static struct { std::string page; int sel = 0; double at = -100; bool on = false; } g_fadeOut;   // the page fading out
static void FadeOutNow() {
    if (g_menu.stack.empty()) return;
    g_fadeOut.page = g_menu.stack.back(); g_fadeOut.sel = g_menu.sel; g_fadeOut.at = ImGui::GetTime(); g_fadeOut.on = menuFade > 0;
}

// ---------------------------------------------------------------- grids
// (a live list's item: <key>.N.color "r g b" tints its icon piece, <key>.N.on "1" / "0" shows its "item on" / "item
// off" pieces - neither when it has none)
static const float kOffAlpha = 60 / 255.f;   // an "off" live list item's icon and label (the game's World Map legend: 0x496920)
struct GItem { std::string label, icon, action; int tile = -1; float tint[3] = {1, 1, 1}; bool hasTint = false; int on = -1; };
// a piece only for slider / picker tiles: laid out across the grid's whole row (those tiles take a row of their own), so
// changing the columns doesn't squeeze it
static bool RowPiece(const Elem& e) { return e.tileKinds && !(e.tileKinds & ~6); }
static bool GridHasTiles(const Widget& w) { return w.bind.empty(); }   // (not a live list / a choice setting)
static int GridTileCount(const Widget& w) {
    if (w.type == W_CAROUSEL) return std::max(1, w.totalRows);   // (a carousel: totalRows = its tiles)
    return std::max(1, w.cols) * std::max(1, w.totalRows > 0 ? w.totalRows : w.rows);
}
static std::string TileName(int i) { return "Button " + std::to_string(i + 1); }
static void GridFitTiles(Widget& w) {   // a tile for every cell (more are kept: rows taken away and put back keep theirs)
    if (!GridLike(w)) return;
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
    if (w.totalRows > 0) { size_t cap = (size_t)w.totalRows * (w.type == W_CAROUSEL ? 1 : std::max(1, w.cols)); if (v.size() > cap) v.resize(cap); }
    return v;
}
static std::vector<GItem> GridItemsAll(const Widget& w) {
    std::vector<GItem> v;
    if (w.bind.compare(0, 5, "list:") == 0) {   // another mod's / REUI's values
        std::string key = w.bind.substr(5), s;
        int n = tokens && tokens(key + ".count", s) ? atoi(s.c_str()) : 0;
        for (int i = 1; i <= std::min(n, 1000); ++i) {
            GItem it; std::string k = key + "." + std::to_string(i);
            if (tokens) {
                tokens(k, it.label); tokens(k + ".icon", it.icon);
                std::string c, o; int r, g, b;
                if (tokens(k + ".color", c) && sscanf(c.c_str(), "%d %d %d", &r, &g, &b) == 3) { it.tint[0] = r / 255.f; it.tint[1] = g / 255.f; it.tint[2] = b / 255.f; it.hasTint = true; }
                if (tokens(k + ".on", o) && !o.empty()) it.on = o != "0";
            }
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
    if (!GridLike(w)) return;
    if (!w.rowSpace) {   // slider / picker pieces laid out across a tile (squeezed when columns were added): spread back across the row
        float f = w.refW > 0.5f ? w.gw / w.refW : 1.f;
        if (!w.group.empty() && fabsf(f - 1.f) > 0.001f)
            for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && RowPiece(e)) {
                e.x = w.gx + (e.x - w.gx) * f;
                if (e.type != E_TEXT || e.w > 0) e.w *= f;
            }
        w.rowSpace = true;
    }
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
// Where each item sits: tiles fill the rows left to right, but with more than one column a slider / picker tile takes a
// whole row of its own (the row before it ends early; the next tile starts a new row).
struct GSlot { int row = 0, col = 0, span = 1; };
static bool GridWide(const Widget& w, int i) {
    return w.type == W_GRID && w.cols > 1 && GridHasTiles(w) && i >= 0 && i < (int)w.tiles.size() && (w.tiles[i].type == W_SLIDER || w.tiles[i].type == W_PICKER);
}
static std::vector<GSlot> GridLayout(const Widget& w, int n) {
    std::vector<GSlot> v; v.reserve(n);
    int cols = std::max(1, w.cols), r = 0, c = 0;
    if (w.type == W_CAROUSEL) { for (int i = 0; i < n; ++i) v.push_back({0, i, 1}); return v; }   // (one row)
    for (int i = 0; i < n; ++i) {
        if (GridWide(w, i)) { if (c > 0) { ++r; c = 0; } v.push_back({r, 0, cols}); ++r; continue; }
        v.push_back({r, c, 1});
        if (++c >= cols) { c = 0; ++r; }
    }
    return v;
}
static int GridRowsOf(const std::vector<GSlot>& L) { return L.empty() ? 0 : L.back().row + 1; }
// how many cells a row's tiles are moved across (a short row, starting from the centre / right)
static float GridRowShift(const Widget& w, const std::vector<GSlot>& L, int row) {
    int cols = std::max(1, w.cols), in = 0;
    for (auto& s : L) if (s.row == row) in += s.span;
    return in <= 0 ? 0.f : (cols - std::min(cols, in)) * 0.5f * std::max(0, std::min(2, w.startAt));
}
static void GridKeepVisible(const Widget& w, MenuState::GridSt& st, int count);
static MenuState::GridSt& GridState(const Widget& w, int count) {
    auto& st = g_menu.grids[w.group];
    bool first = st.sel < 0;
    if (first) {   // first look: on the setting's value, scrolled to it
        Setting* s = SettingOf(w.bind);
        st.sel = s && s->get ? (int)lroundf(s->get()) : w.bind.empty() ? (int)lroundf(w.value) : 0;
    }
    std::string sv;   // a live list whose selection another mod keeps: <key>.sel
    if (w.bind.compare(0, 5, "list:") == 0 && tokens && tokens(w.bind.substr(5) + ".sel", sv) && !sv.empty()) {
        int s2 = atoi(sv.c_str());
        if (s2 != st.sel || first) { st.sel = s2; GridKeepVisible(w, st, count); if (first) st.scroll = (float)st.top; }
    }
    st.sel = std::max(0, std::min(std::max(0, count - 1), st.sel));
    if (first) { GridKeepVisible(w, st, count); st.scroll = (float)st.top; }
    return st;
}
static int Mod(int a, int n) { return n > 0 ? ((a % n) + n) % n : 0; }
static void GridKeepVisible(const Widget& w, MenuState::GridSt& st, int count) {
    if (w.type == W_CAROUSEL) {
        // centred: top is the tile in the middle slot (counting on past the ends while wrapping, so the row slides the
        // short way); otherwise the first tile on screen
        if (w.centred) { if (Mod(st.top, std::max(1, count)) != st.sel || (!w.wrap && st.top != st.sel)) st.top = st.sel; return; }
        int vis = std::max(1, w.cols);
        if (st.sel < st.top) st.top = st.sel;
        if (st.sel >= st.top + vis) st.top = st.sel - vis + 1;
        st.top = std::max(0, std::min(std::max(0, count - vis), st.top));
        return;
    }
    int rows = std::max(1, w.rows);
    auto L = GridLayout(w, count);
    int total = GridRowsOf(L), r = st.sel >= 0 && st.sel < count ? L[st.sel].row : 0;
    if (r < st.top) st.top = r;
    if (r >= st.top + rows) st.top = r - rows + 1;
    st.top = std::max(0, std::min(std::max(0, total - rows), st.top));
}
// an arrow inside the grid: true if it moved there (false: at the edge - the page takes it)
static bool GridMove(Widget& w, int dx, int dy) {
    auto items = GridItems(w); int n = (int)items.size();
    if (!n) return false;
    auto& st = GridState(w, n);
    if (w.type == W_CAROUSEL) {   // left / right only (up / down: the page's)
        if (dy || !dx) return false;
        int to = st.sel + dx;
        if ((to < 0 || to >= n) && !w.wrap) return false;
        if (w.centred) {
            if (st.top < 0 || st.top >= n) { int k = (int)floorf((float)st.top / n) * n; st.top -= k; st.scroll -= (float)k; }   // (back in range: same place)
            st.top = (st.sel == Mod(st.top, n) ? st.top : st.sel) + dx; st.sel = Mod(st.top, n);
        } else { st.sel = Mod(to, n); GridKeepVisible(w, st, n); }
        (dx < 0 ? g_menu.flashL : g_menu.flashR) = g_menu.t;
        Snd(dx < 0 ? SND_LEFT : SND_RIGHT);
        return true;
    }
    auto L = GridLayout(w, n);
    const GSlot& cs = L[st.sel];
    int ns = st.sel;
    if (dx) {   // the tile beside it on its row (a whole-row tile has none)
        ns = -1;
        for (int i = 0; i < n; ++i) if (L[i].row == cs.row && L[i].col == cs.col + dx) ns = i;
        if (ns < 0) return false;
        st.wantCol = -1;
    }
    if (dy) {
        int nr = cs.row + dy; if (nr < 0 || nr >= GridRowsOf(L)) return false;
        // the same column on screen (short rows can be moved across; a whole-row tile keeps the column you came from)
        float vis = cs.span > 1 ? (st.wantCol >= 0 ? (float)st.wantCol : 0.f) : cs.col + GridRowShift(w, L, cs.row);
        if (cs.span == 1) st.wantCol = (int)lroundf(vis);
        float best = 1e9f;
        for (int i = 0; i < n; ++i) {
            if (L[i].row != nr) continue;
            float lo = L[i].col + GridRowShift(w, L, nr), d = vis < lo ? lo - vis : vis > lo + L[i].span - 1 ? vis - (lo + L[i].span - 1) : 0.f;
            if (d < best) { best = d; ns = i; }
        }
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
// a piece copied per tile (not: a scroller's tab / track; a carousel's arrows, which stay at the row's ends)
static bool GridPiece(const Elem& e) {
    if (e.role == "gtab" || e.role == "gtrack") return false;
    if (e.role == "arrowL" || e.role == "arrowR") { Widget* w = WidgetFor(e.group); if (w && w->type == W_CAROUSEL) return false; }
    return true;
}
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
// The editor's menu bar: the custom menu being edited ("" = none: everything shows). Only its pieces are shown / listed.
static std::string g_menuEd;
static bool MenuEdOff(const std::string& group) {
    if (!MenuWs()) return false;
    if (g_menuEd.empty()) {   // none picked: the rest of the layout (custom menus show only when picked)
        for (auto& p : g_pages) if (p.dedicated && InGroup(group, p.group)) return true;
        return false;
    }
    Page* p = PageOf(g_menuEd);
    if (!p || !p->dedicated) { g_menuEd.clear(); return false; }   // (gone: undo, reload)
    return !InGroup(group, g_menuEd) || OwnerPage(group) != g_menuEd;   // (its sub menus' pieces are theirs)
}
// a group as the editor shows it while a custom menu is being edited: the menu is the page, not a group ("" = the menu)
static std::string RelGroup(const std::string& g) {
    if (!MenuWs() || g_menuEd.empty() || !InGroup(g, g_menuEd)) return g;
    return g == g_menuEd ? std::string() : g.substr(g_menuEd.size() + 1);
}
// a group for something new / moved while a custom menu is being edited: inside that menu ("" -> the menu itself)
static std::string IntoMenu(const std::string& g) {
    if (g_menuEd.empty() || !MenuEdOff(g)) return g;
    return g.empty() ? g_menuEd : CleanGroup(g_menuEd + "/" + g);
}
// the editor: a grid's pieces are its first tile as the panel's picked tile would show it (the others are hidden); with a
// custom menu picked in the menu bar, everything outside it is hidden
static bool GridPieceOff(const Elem& e) {
    if (MenuEdOff(e.group)) return true;
    if (!MenuWs() || e.group.empty()) return false;
    Widget* w = WidgetFor(e.group);
    if (!w || !GridLike(*w) || !GridHasTiles(*w) || !GridPiece(e)) return false;
    auto it = g_tileSel.find(w->group);
    int ts = it == g_tileSel.end() ? 0 : it->second;
    return !TileShows(e, TileBit(ts >= 0 && ts < (int)w->tiles.size() ? &w->tiles[ts] : nullptr));
}
static bool GridHasKind(const Widget& w, int bit) {   // the pieces that make that kind of tile (fill bar / arrows / binding icon)
    const char* a = bit == 2 ? "fill" : bit == 4 ? "arrowL" : bit == 8 ? "bindicon" : nullptr;
    const char* b = bit == 4 ? "arrowR" : a;
    if (!a) return true;
    for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && (e.role == a || e.role == b) && TileShows(e, bit)) return true;
    return false;
}
// the pieces a new kind of tile gets (as the older UniPad menu rows had them: Steering Deadzone, Active Gamepad, Steering Binding)
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
// A grid gets pieces for a new kind of tile: the built-in row's pieces, laid across the tile (their row's width spread
// over the tile less a margin, bars stretched with it, the rest their own size; up / down from the row's label), shown on
// that kind only. The grid's own label / icon pieces aren't shown on it then.
static void GridAddKind(Widget& w, int bit) {
    if (bit == 1 || w.group.empty()) return;
    float x0 = 0, x1 = 0;
    std::vector<Elem> src = KindDefault(bit, x0, x1);
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
    if (bit == 2 || bit == 4) cw = w.gw;   // (sliders / pickers: across the whole row)
    float m = cw * 0.04f, k = (cw - 2 * m) / std::max(1.f, br - bl);
    auto mapX = [&](float x) { return w.gx + m + (x - bl) * k; };
    float cy = w.gy + ch / 2;
    const char* kn = bit == 2 ? "Slider: " : bit == 4 ? "Picker: " : "Binding: ";
    std::string first = w.tiles.empty() ? std::string("{item}") : w.tiles[0].label;
    // bindings: as the Gamepad Bindings rows - the label (centred, as there) and the icon at their own distance apart,
    // the pair centred in the tile, everything at fixed places (so the icons line up down the column)
    float bindDx = w.gx + cw / 2 - (bl + br) / 2;
    for (auto& e : src) {
        Elem c = e;
        c.group = w.group; c.kit.clear(); c.tileKinds = bit; c.visible = true; c.name = kn + e.name;
        c.y = cy + (e.y - ly);
        float ax = (e.anchor % 3) * 0.5f;
        if (bit == 8) c.x = e.x + bindDx;
        else if (e.role == "fill") { c.w = (x1 - x0) * k; c.x = mapX(x0) + ax * c.w; }
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
    // slider tiles: the fill piece (as laid out in the first tile) is the bar's full length; the tab moves along it
    const Elem* fillPc = nullptr;
    for (int i : order) if (els[i].role == "fill" && els[i].w > 0.5f) { fillPc = &els[i]; break; }
    auto L = GridLayout(w, n);
    float cwid = GCellW(w);
    // rows partly scrolled in or out are cut at the grid's top / bottom
    ImVec2 ga, gb;
    { Elem box; box.type = E_IMAGE; box.anchor = els[order[0]].anchor; float ax = (box.anchor % 3) * 0.5f, ay = (box.anchor / 3) * 0.5f;
      box.w = w.gw; box.h = w.gh; box.x = w.gx + ax * w.gw; box.y = w.gy + ay * w.gh + scrollDy; ElemRect(box, ga, gb); }
    float scroll = preview ? 0.f : st.scroll;
    // cutters outside the grid (they still cut its tiles' shapes)
    std::vector<Elem> outerCut;
    if (g_drawList) for (auto& o : *g_drawList) if (o.type == E_SHAPE && o.cut && o.visible && !InGroup(o.group, w.group)) outerCut.push_back(o);
    // where each tile is drawn (cells across / down from the first): a grid's visible rows; a carousel's slots
    struct At { int idx; float ox, oy; };
    std::vector<At> at;
    bool car = w.type == W_CAROUSEL;
    if (car) {
        // centred: the tile scroll is on sits in the middle slot; otherwise scroll is the first slot's tile. Wrapping, the
        // ends meet (copies a row-length away fill the slots past them).
        float lead = !preview && w.centred ? (cols - 1) * 0.5f : 0.f;
        for (int idx = 0; idx < n; ++idx)
            for (int k = (w.wrap && !preview ? -1 : 0); k <= (w.wrap && !preview ? 1 : 0); ++k) {
                float pos = idx + k * n - scroll + lead;
                if (pos <= -1.f || pos >= (float)cols) continue;
                if (preview && idx == 0) continue;   // (the pieces themselves are the first slot)
                at.push_back({idx, pos * px, 0.f});
            }
        float pad = w.spX * 0.5f + 1.f;   // (tiles sliding in / out are cut half a gap past the end slots)
        dl->PushClipRect(ImVec2(ga.x - pad * (gb.x - ga.x) / std::max(1.f, w.gw), -10000), ImVec2(gb.x + pad * (gb.x - ga.x) / std::max(1.f, w.gw), 10000), true);
    } else {
        int r0 = (int)floorf(scroll), r1 = r0 + rows + (scroll != (float)r0 ? 1 : 0);
        std::map<int, float> shift; for (int r = r0; r < r1; ++r) shift[r] = GridRowShift(w, L, r);
        for (int idx = 0; idx < n; ++idx) {
            const GSlot& sl = L[idx]; int r = sl.row;
            if (r < r0 || r >= r1) continue;
            if (preview && idx == 0 && shift[r] == 0 && sl.span == 1) continue;   // (the pieces themselves are the first cell, unless it's moved / widened)
            at.push_back({idx, (sl.col + shift[r]) * px, (r - scroll) * py});
        }
        dl->PushClipRect(ImVec2(-10000, ga.y - 2), ImVec2(10000, gb.y + 2), true);
    }
    for (const At& a : at) {
            int idx = a.idx;
            const GSlot& sl = L[idx];
            // a whole-row tile: its pieces spread across the row (places, and bars / boxes stretched; the rest keep their size)
            float sxp = sl.span > 1 && w.refW > 0.5f ? (sl.span * cwid + (sl.span - 1) * w.spX) / w.refW : sx;
            bool cur = on && !preview && idx == st.sel;
            const GItem& it = items[idx];
            // a tile: its kind decides which pieces it shows (value text: sliders / pickers; fill + tab: sliders; arrows: pickers)
            const Widget* t = it.tile >= 0 && it.tile < (int)w.tiles.size() ? &w.tiles[it.tile] : nullptr;
            int kind = it.tile < 0 ? -1 : t ? t->type : W_BUTTON;
            std::string val = t && kind != W_BUTTON ? WText(*t) : std::string();
            Setting* bs = t && kind == W_BUTTON ? SettingOf(t->bind) : nullptr; if (bs && bs->kind != 3) bs = nullptr;   // a binding tile
            bool waiting = bs && !preview && g_menu.capturing == TileKey(w, it.tile);
            float ox = a.ox, oy = a.oy;
            // pieces after the label's text: moved by how much longer / shorter this tile's label is than the piece's own
            float afterDx = 0;
            for (int i : order) {
                const Elem& L = els[i];
                if (L.role != "label" || L.type != E_TEXT || (kind >= 0 && !TileShows(L, TileBit(t)))) continue;
                bool any = false; for (int j : order) if (els[j].afterLabel) any = true;
                if (!any || L.text.find('{') != std::string::npos) break;
                Elem raw = L, now = L; now.text = it.label;
                float grow = ElemSize(now).x - ElemSize(raw).x;   // (units; its right edge moves by the part not centred / right of it)
                afterDx = L.w > 0 ? 0.f : grow * (1.f - L.align * 0.5f);
                break;
            }
            // this tile's pieces, placed; drawn together after, so its shapes are cut by its own cutters (each tile its own
            // copies), as well as by cutters outside the grid
            std::vector<Elem> tileEls; std::vector<std::pair<int, bool>> tileSrc;
            for (int i : order) {
                Elem& src = els[i];
                if (src.selOnly && !cur) continue;
                if ((src.role == "on" && it.on != 1) || (src.role == "off" && it.on != 0)) continue;   // (a live list item's state)
                if (kind >= 0 && !TileShows(src, TileBit(t))) continue;
                Elem e = src;
                bool rowP = RowPiece(src);   // (slider / picker pieces: already laid out across the row)
                bool wideP = e.type != E_TEXT && e.w > 3 * e.h;   // (a bar / box: stretches across a whole-row tile)
                float kp = rowP ? 1.f : sxp;
                e.x = w.gx + (e.x - w.gx) * kp + ox + (e.afterLabel ? afterDx : 0.f);
                e.y = w.gy + (e.y - w.gy) * sy + oy + scrollDy;
                if (e.type != E_TEXT) { if (e.afterLabel) { e.w *= std::min(sx, sy); e.h *= std::min(sx, sy); } else if (!rowP) { e.w *= wideP ? sxp : sx; e.h *= sy; } }   // (icons after the text keep their shape)
                else { if (e.w > 0 && !rowP) e.w *= sxp; }   // (text keeps its size)
                if (kind == W_SLIDER && t && fillPc && (e.role == "fill" || e.role == "tab")) {
                    float kf = RowPiece(*fillPc) ? 1.f : sxp;
                    float fillW = fillPc->w * kf, fillL = w.gx + (fillPc->x - w.gx) * kf - (fillPc->anchor % 3) * 0.5f * fillW;
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
                    if (it.on == 0 && e.role == "label") e.col[3] *= kOffAlpha;   // (an "off" live list item: see-through, as the game's)
                }
                // its picture: the tile's icon; a binding tile without one (and binding icon pieces): what it's bound to
                bool ownTex = e.role == "icon" || e.role == "bindicon";
                if (bs && e.role == "icon" && it.icon.empty()) e.w = e.h = std::min(e.w, e.h);   // (a controller icon: square)
                if (bs && (e.role == "bindicon" || (e.role == "icon" && it.icon.empty()))) { if (!BindIcon(dl, e, bs, waiting, w.group)) continue; }
                else if (e.role == "icon") {
                    if (it.icon.empty()) continue;
                    e.tex = it.icon; e.texIdx = -2;
                    if (it.hasTint) for (int q = 0; q < 3; ++q) e.col[q] *= it.tint[q];
                    if (it.on == 0) e.col[3] *= kOffAlpha;   // (an "off" live list item: see-through, as the game's)
                }
                if (preview) e.col[3] *= 0.45f;
                if (!ownTex) e.texIdx = src.texIdx;
                e.fontIdx = src.fontIdx;
                tileEls.push_back(e); tileSrc.push_back({i, ownTex});
            }
            std::vector<Elem> cutList = tileEls;
            bool anyShape = false; for (auto& t : tileEls) anyShape |= t.type == E_SHAPE;
            if (anyShape) for (auto& o : outerCut) cutList.push_back(o);
            std::vector<Elem>* was = g_drawList;
            g_drawList = &cutList;
            for (size_t q = 0; q < tileEls.size(); ++q) {
                Elem& e = tileEls[q]; Elem& src = els[tileSrc[q].first];
                DrawElem(dl, e, false);
                if (!tileSrc[q].second) src.texIdx = e.texIdx;
                src.fontIdx = e.fontIdx;
            }
            g_drawList = was;
    }
    dl->PopClipRect();
}
// ---- the editor's view of a grid
// each cell the editor shows (cols x rows, short rows shifted): its box on screen, or a piece's copy in it
static void GridEachCell(Widget& w, const Elem* piece, const std::function<void(ImVec2, ImVec2)>& fn) {
    int cols = std::max(1, w.cols), rows = std::max(1, w.rows);
    int n = (int)GridItems(w).size(); if (!n || (w.type == W_CAROUSEL && n < cols)) n = std::max(n, cols * rows);
    float px = GPitchX(w), py = GPitchY(w), cw = GCellW(w), ch = GCellH(w);
    float sx = w.scalePieces && w.refW > 0.5f ? cw / w.refW : 1.f, sy = w.scalePieces && w.refH > 0.5f ? ch / w.refH : 1.f;
    int anchor = 4;
    for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && GridPiece(e)) { anchor = e.anchor; break; }
    float ax = (anchor % 3) * 0.5f, ay = (anchor / 3) * 0.5f;
    auto L = GridLayout(w, n);
    for (int idx = 0; idx < n; ++idx) {
            const GSlot& sl = L[idx];
            if (sl.row >= rows || (w.type == W_CAROUSEL && idx >= cols)) break;   // (a carousel: its slots)
            float ox = (sl.col + GridRowShift(w, L, sl.row)) * px, oy = sl.row * py;
            float spanW = sl.span * cw + (sl.span - 1) * w.spX, sxp = sl.span > 1 && w.refW > 0.5f ? spanW / w.refW : sx;
            ImVec2 a, b;
            if (piece) {
                Elem e = *piece;
                bool wideP = e.type != E_TEXT && e.w > 3 * e.h, rowP = RowPiece(e);
                if (rowP && sl.span == 1 && w.cols > 1) continue;   // (slider / picker pieces: on whole-row tiles only)
                float kp = rowP ? 1.f : sxp;
                e.x = w.gx + (e.x - w.gx) * kp + ox; e.y = w.gy + (e.y - w.gy) * sy + oy;
                if (!rowP) { if (e.type != E_TEXT) { e.w *= wideP ? sxp : sx; e.h *= sy; } else { if (e.w > 0) e.w *= sxp; } }   // (text keeps its size)
                ElemRect(e, a, b);
            } else {
                Elem box; box.type = E_IMAGE; box.anchor = anchor; box.w = spanW; box.h = ch;
                box.x = w.gx + ox + ax * spanW; box.y = w.gy + oy + ay * ch;
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
        if (!GridLike(w)) continue;
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
    if (w.type == W_CAROUSEL) {
        if (e.role == "arrowL" || e.role == "arrowR") {   // at their own places; light up when pressed
            double ft = e.role == "arrowL" ? g_menu.flashL : g_menu.flashR;
            if (g_menu.open && g_menu.t - ft < 0.15) e.col[3] = 1.f;
            return true;
        }
        if (e.role != "gtab" || n < 2) return true;   // (the track; a tab with one tile stays as placed)
        float l = 0, r = 0; bool track = false;
        for (auto& o : MenuElems()) if (InGroup(o.group, w.group) && o.role == "gtrack") { ImVec2 sz = ElemSize(o); float c = o.x - (o.anchor % 3) * 0.5f * sz.x + sz.x * 0.5f; l = c - sz.x / 2; r = c + sz.x / 2; track = true; }
        if (!track) return true;
        auto& st = GridState(w, n);
        float half = ElemSize(e).x / 2, f = (float)st.sel / (float)(n - 1);
        e.x = l + half + (r - l - 2 * half) * f + ((e.anchor % 3) * 0.5f - 0.5f) * 2 * half;
        return true;
    }
    int rows = std::max(1, w.rows), total = GridRowsOf(GridLayout(w, n));
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
std::string MenuPausePage() { if (customMenus) for (auto& p : g_pages) if (p.pauseMenu) return p.group; return std::string(); }
std::string MenuPauseOptionsPage() { if (customMenus) for (auto& p : g_pages) if (p.pauseOptions) return p.group; return std::string(); }
std::string MenuConfirmPage() { if (customMenus) for (auto& p : g_pages) if (p.confirm) return p.group; return std::string(); }
std::string MenuWorldMapPage() { if (customMenus) for (auto& p : g_pages) if (p.worldMap) return p.group; return std::string(); }
bool MenuIsOpen() { return g_menu.open; }
static Page* MenuPage() { return g_menu.stack.empty() ? nullptr : PageOf(g_menu.stack.back()); }
static void MenuGo(const std::string& page) {
    if (!g_menu.stack.empty()) { FadeOutNow(); g_menu.selStack.push_back(g_menu.sel); }   // (another page: cross-fade)
    g_menuOpenAt = ImGui::GetTime();
    g_menu.stack.push_back(page); g_menu.sel = 0;
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
    FadeOutNow();
    g_menu.open = false; g_menu.stack.clear(); g_menu.selStack.clear(); g_menu.capturing.clear();
    if (g_menuValDirty) {   // own values of unbound widgets: saved, unless that would also save other unsaved edits
        if (!g_menuOtherEdits) Save();
        else Log("menu: a value changed; not saved on its own because the editor has other unsaved changes (Save there)");
        g_menuValDirty = false;
    }
}
static void MenuBack() {
    Snd(SND_BACK);
    if (g_menu.stack.size() > 1) { FadeOutNow(); g_menuOpenAt = ImGui::GetTime(); }   // (back a page: cross-fade)
    else { MenuClose(); return; }
    g_menu.stack.pop_back();
    g_menu.sel = g_menu.selStack.empty() ? 0 : g_menu.selStack.back(); if (!g_menu.selStack.empty()) g_menu.selStack.pop_back();
}
static void MenuAccept(Widget& w) { if (GridLike(w)) GridAccept(w); }
// a slider / picker tile taken hold of: left / right change it
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
    Widget* lockG = n && GridLike(*rows[g_menu.sel].w) ? rows[g_menu.sel].w : nullptr;
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
            bool slider = lockT && lockT->type == W_SLIDER && i >= 2;
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
    if (GridLike(*rows[g_menu.sel].w)) {   // arrows move inside the grid first; at its edge the page takes them
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
    // left / right past a grid's edge: the grid beside it
    for (int k = 0; k < 2; ++k) {
        if (!fire[2 + k]) continue;
        int dir = k ? 1 : -1;
        int ns = MoveSel(rows, g_menu.sel, 0, dir, false);
        if (ns >= 0) { g_menu.sel = ns; Snd(dir < 0 ? SND_LEFT : SND_RIGHT); }
    }
    if (fire[4]) MenuAccept(*rows[g_menu.sel].w);
}

// The page as shown: its grids / carousels drawn per tile, the description text the selected tile's, the rest as they
// are. sel < 0: nothing selected (editor).
static void DrawMenuPage(ImDrawList* dl, const Page& p, int sel) {
    auto rows = PageRows(p);
    int n = (int)rows.size();
    if (sel >= n) sel = n - 1;
    std::map<const Widget*, int> rowIdx; for (int i = 0; i < n; ++i) rowIdx[rows[i].w] = i;
    std::string desc = sel >= 0 && n ? rows[sel].w->desc : "";
    if (sel >= 0 && n && GridHasTiles(*rows[sel].w)) {   // its selected tile's
        Widget& g = *rows[sel].w; auto items = GridItems(g);
        if (!items.empty()) { Widget* t = GridTile(g, items[GridState(g, (int)items.size()).sel].tile); if (t) desc = t->desc; }
    }
    std::set<const Widget*> gridsDrawn;
    auto& els = MenuElems();
    g_drawList = &els;   // (shapes find their cutters here)
    for (int i : DrawOrder(els)) {
        Elem& src = els[i];
        if (!InGroup(src.group, p.group) || !src.visible || OwnerPage(src.group) != p.group) continue;   // (not its sub menus' pieces)
        Elem e = src;
        Widget* w = WidgetFor(e.group);
        auto it = w ? rowIdx.find(w) : rowIdx.end();
        if (it != rowIdx.end()) {   // a grid / carousel: its tiles, all at once where its first piece is drawn; its scroller / arrows as themselves
            bool on = it->second == sel;
            if (GridPiece(e)) { if (gridsDrawn.insert(w).second) DrawGrid(dl, &p, *w, on, 0, false); continue; }
            if (!GridScroller(*w, e)) continue;
        } else if (e.role == "desc") {
            if (sel >= 0) e.text = desc;
        }
        e.texIdx = src.texIdx; e.fontIdx = src.fontIdx;
        DrawElem(dl, e, false);
        src.texIdx = e.texIdx; src.fontIdx = e.fontIdx;   // keep what it looked up
    }
}
// while the menu is open it's all that's drawn (a page fading out after it closed is drawn under the rest)
static bool DrawMenu(ImDrawList* dl) {
    double now = ImGui::GetTime(); float F = std::max(0.001f, menuFade);
    auto drawA = [&](const Page& p, int sel, float a) {   // the page at an opacity: its vertices' alpha scaled
        if (a <= 0.003f) return;
        int v0 = dl->VtxBuffer.Size;
        DrawMenuPage(dl, p, sel);
        if (a < 0.999f) for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
            ImU32& c = dl->VtxBuffer[i].col;
            c = (c & ~IM_COL32_A_MASK) | ((ImU32)(((c >> IM_COL32_A_SHIFT) & 0xFF) * a) << IM_COL32_A_SHIFT);
        }
    };
    if (g_fadeOut.on) {
        float k = (float)((now - g_fadeOut.at) / F);
        if (k >= 1 || menuFade <= 0) g_fadeOut.on = false;
        else if (Page* pp = PageOf(g_fadeOut.page)) drawA(*pp, g_fadeOut.sel, 1 - k);
    }
    if (!g_menu.open) return false;
    Page* p = MenuPage(); if (!p) return false;
    drawA(*p, g_menu.sel, menuFade > 0 ? std::min(1.f, (float)((now - g_menuOpenAt) / F)) : 1.f);
    return true;
}

// ---------------------------------------------------------------- authoring
static const char* kRoleKey[] = { "", "label", "value", "fill", "tab", "arrowL", "arrowR", "desc", "bindicon", "icon", "gtab", "gtrack", "on", "off" };
static const char* kRoleName[] = { "(none)", "label", "value text", "slider fill bar", "slider tab", "left arrow", "right arrow",
                                   "page: description text", "binding icon (??? while waiting)", "item icon", "scroller tab", "scroller track",
                                   "live list: item on (a tick)", "live list: item off (a cross)" };

// ---------------------------------------------------------------- authoring panels
static void ElemMenuProps(Elem& e) {
    if (!MenuWs()) return;
    const int nRoles = (int)(sizeof kRoleKey / sizeof kRoleKey[0]);
    int r = 0; for (int i = 0; i < nRoles; ++i) if (e.role == kRoleKey[i]) r = i;
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("role", &r, kRoleName, nRoles)) { e.role = kRoleKey[r]; g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What this piece does in its menu row (or page):\n"
        "label / value text: the tile's name / its setting; both take the page's selected colour.\n"
        "slider fill bar / tab: sized / moved by the value. Arrows: a picker's (light up when pressed); a carousel's ends.\n"
        "The label shows the item ({item} / {i} / {count} in any text), the item icon is its picture.\n"
        "page: description text - on the page itself: the selected tile's description.");
    Widget* inGrid = WidgetFor(e.group); bool tile = inGrid && GridLike(*inGrid);
    if (tile) { ImGui::SameLine(); if (ImGui::Checkbox("selected tile only", &e.selOnly)) g_dirty = true; }
    if (tile && ImGui::IsItemHovered()) ImGui::SetTooltip("Drawn on the selected tile only (a highlight that follows the cursor, the picker arrows).");
    Widget* gw = WidgetFor(e.group);
    if (gw && GridLike(*gw) && GridHasTiles(*gw) && GridPiece(e)) {   // a grid's piece: the kinds of tile it's on
        ImGui::TextUnformatted("shows on"); 
        static const char* kn[] = {"buttons", "sliders", "pickers", "bindings"};
        int mask = e.tileKinds ? e.tileKinds : 15;
        for (int i = 0; i < 4; ++i) {
            ImGui::SameLine(); bool on = (mask >> i) & 1;
            if (ImGui::Checkbox(kn[i], &on)) { mask = on ? mask | (1 << i) : mask & ~(1 << i); e.tileKinds = mask == 15 ? 0 : mask; g_dirty = true; }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The kinds of tile this piece is drawn on (by role too: value text on sliders / pickers,\nfill bar + tab on sliders, arrows on pickers, binding icon on bindings).");
        if (ImGui::Checkbox("after the label's text", &e.afterLabel)) g_dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Moves with the end of each tile's label: it keeps its distance from the text as laid out\nhere (a binding's icon just right of its name).");
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
        if (!GridPiece(e) && w.type == W_CAROUSEL) {   // its arrows: with the row's ends; its scroller stays
            if (e.role == "arrowL") e.x += ngx - w.gx;
            if (e.role == "arrowR") e.x += (ngx + ngw) - (w.gx + w.gw);
            e.y = w.gy + (e.y - w.gy) * kh;
            continue;
        }
        if (!GridPiece(e)) {   // its scroller: beside the grid's right side, along its height
            e.x += (ngx + ngw) - (w.gx + w.gw); e.y = w.gy + (e.y - w.gy) * kh;
            if (e.role == "gtrack") e.h *= kh;
            continue;
        }
        if (RowPiece(e)) {   // slider / picker pieces: across the row - places follow its width, bars stretch, the rest keep their size
            float kr = w.gw > 0.01f ? ngw / w.gw : 1.f;
            e.x = ngx + (e.x - w.gx) * kr; e.y = w.gy + (e.y - w.gy) * ky;
            if (e.type == E_TEXT) { if (e.w > 0) e.w *= kr; }
            else { if (e.w > 2 * e.h) e.w *= kr; e.h *= ky; }
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
static void CarouselTop(Widget& w) {
    int vis = w.cols, total = std::max(1, w.totalRows);
    ImGui::SetNextItemWidth(100); if (ImGui::InputInt("visible", &vis)) { GridResize(w, w.gw * std::max(1, std::min(32, vis)) / std::max(1, w.cols) + 0.f, w.gh, vis, 1, w.spX, w.spY); GridFitTiles(w); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tiles on screen at once (the carousel's width is these, with the spacing between).");
    ImGui::SameLine(); ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("tiles", &total)) { w.totalRows = std::max(1, std::min(1000, total)); GridFitTiles(w); g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tiles it has in all; left / right scroll through them.");
    ImGui::SameLine(); if (ImGui::Checkbox("selected stays in the middle", &w.centred)) g_dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: the row slides under the middle slot, the selected tile always there (the game's pause menu).\nOff: the cursor moves along the slots and the row scrolls at the ends.");
    ImGui::SameLine(); if (ImGui::Checkbox("wraps around", &w.wrap)) g_dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Past the last tile comes the first (and the other way).");
    float gwv = w.gw, ghv = w.gh;
    ImGui::SetNextItemWidth(150); if (ImGui::SliderFloat("width", &gwv, 20.f, 1280.f, "%.0f")) GridResize(w, gwv, w.gh, w.cols, 1, w.spX, w.spY);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The visible slots' width (units; Ctrl + click to type), less the spacing, shared out; the arrows keep to its ends.");
    ImGui::SameLine(); ImGui::SetNextItemWidth(150); if (ImGui::SliderFloat("height", &ghv, 20.f, 1280.f, "%.0f")) GridResize(w, w.gw, ghv, w.cols, 1, w.spX, w.spY);
    float sp = w.spX;
    ImGui::SetNextItemWidth(150);
    if (ImGui::SliderFloat("spacing", &sp, 0.f, 200.f, "%.1f")) { int c = std::max(1, w.cols); float cw = GCellW(w); GridResize(w, cw * c + (c - 1) * std::max(0.f, sp), w.gh, w.cols, 1, sp, w.spY); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gap between tiles (units; Ctrl + click to type). The tiles keep their size and move apart.");
    static const char* aligns[] = {"left", "centre", "right"};
    GridOrigin(w);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    int al = std::max(0, std::min(2, w.startAt));
    if (ImGui::Combo("align", &al, aligns, 3) && al != w.startAt) { w.startAt = al; GridPlace(w); }
}
static void GridPanel(Widget& w) {
    GridFitTiles(w);
    bool tiles = GridHasTiles(w);
    if (w.type == W_CAROUSEL) CarouselTop(w); else {
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
    }   // (grid)
    // the tiles' box backgrounds (the grid's "Cell" / "Cell selected" pieces): on / off
    {
        bool any = false, on = false;
        for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.name.compare(0, 4, "Cell") == 0) { any = true; on |= e.visible; }
        if (any) {
            if (ImGui::Checkbox("box backgrounds", &on)) { for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.name.compare(0, 4, "Cell") == 0) e.visible = on; g_dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The tiles' boxes (the grid's \"Cell\" and \"Cell selected\" pieces). Off: only your own pieces are drawn -\na highlight of your own with \"selected tile only\" on shows on the selected tile alone.");
        }
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
            if (ts == 0) {   // (the pieces themselves are the first tile in the editor; pieces after a label's text move with it)
                float grow = 0;
                for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.type == E_TEXT && e.role == "label" && e.text.find('{') == std::string::npos) {
                    float before = ElemSize(e).x; e.text = t.label;
                    if (e.w <= 0 && TileShows(e, 8)) grow = (ElemSize(e).x - before) * (1.f - e.align * 0.5f);
                }
                if (grow != 0) for (auto& e : MenuElems()) if (InGroup(e.group, w.group) && e.afterLabel) e.x += grow;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rename the tile.");
        ImGui::EndGroup();
        static const char* kinds[] = {"Button", "Slider", "Picker (Left / Right)"};
        int k = t.type == W_SLIDER ? 1 : t.type == W_PICKER ? 2 : 0;
        if (w.type == W_CAROUSEL) { t.type = W_BUTTON; k = -1; }   // (a carousel's tiles are buttons: left / right scroll it)
        if (k >= 0) ImGui::SameLine();
        if (k >= 0) ImGui::SetNextItemWidth(170);
        if (k >= 0 && ImGui::Combo("widget##tile", &k, kinds, 3)) {
            int nt = k == 1 ? W_SLIDER : k == 2 ? W_PICKER : W_BUTTON;
            if (nt != t.type) { t.type = nt; t.bind.clear(); t.action.clear(); t.value = 0; g_dirty = true; if (!GridHasKind(w, TileBit(&t))) GridAddKind(w, TileBit(&t)); }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("What the tile is. Sliders and pickers: A takes hold of it, left / right change it, B lets go.\nIts pieces show by role: value text on sliders / pickers, fill bar + tab on sliders, arrows on pickers.");
        { int was = TileBit(&t); ImGui::SameLine(); TileChanges(t); if (w.type == W_CAROUSEL) was = TileBit(&t); int now = TileBit(&t); if (now != was && !GridHasKind(w, now)) GridAddKind(w, now); }
        if (w.type != W_CAROUSEL && TileBit(&t) != 1 && !GridHasKind(w, TileBit(&t))) {
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
static const char* kWMake[] = { "(not a widget)", "Grid (rows x columns)", "Carousel (scrolls left / right)" };
static const int kWMakeType[] = { W_NONE, W_GRID, W_CAROUSEL };
// a group made a grid / carousel: its pieces are the first tile
static void MakeGridLike(Widget& w, int t) {
    w.type = t; w.bind.clear();
    if (t == W_CAROUSEL) { w.cols = std::max(1, w.cols); w.rows = 1; w.totalRows = std::max(w.cols, w.totalRows); }
    GridFromPieces(w); GridFitTiles(w); g_dirty = true;
}
static void WidgetPanel(Widget& w, Page* page) {
    int t = w.type == W_CAROUSEL ? 2 : 1;
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("widget", &t, kWMake, 3) && kWMakeType[t] != w.type) {
        if (t == 0) { std::string g = w.group; MenuForget(g); g_dirty = true; return; }
        MakeGridLike(w, kWMakeType[t]);
    }
    GridPanel(w); (void)page;
}
// a new grid on the page: one cell's pieces (backing, text, selected frame, icon) + a scroller beside it
static void AddCarousel(Page& p);
static void AddGrid(Page& p) {
    std::string g; for (int i = 1; ; ++i) { g = p.group + "/Grid" + (i > 1 ? " " + std::to_string(i) : ""); if (!WidgetOf(g)) { bool used = false; for (auto& e : MenuElems()) if (InGroup(e.group, g)) used = true; if (!used) break; } }
    // the default: 1 column, 8 rows of 550 x 30 tiles 7.7 apart, centred (as menu_big's grid)
    Widget w; w.group = g; w.type = W_GRID; w.cols = 1; w.rows = 8; w.totalRows = 8; w.spX = 0; w.spY = 7.7f;
    w.refW = 550; w.refH = 30; w.gw = 550; w.gh = 8 * 30 + 7 * 7.7f; w.gx = -16 - w.gw / 2; w.gy = -w.gh / 2;
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
// a new carousel: the game's pause menu row (UI_pause.fng) - 126 x 62 slots 159 apart (3 on screen) about the middle at
// y -14.3, the icon backing + green frame on the selected one, 118 x 59 icons, white arrows (26 x 26, 150 / 255) at
// -250.3 / 259.1, and the scroller (562 x 8 backing at y -52, a 16 x 16 tab)
static void AddCarousel(Page& p) {
    std::string g; for (int i = 1; ; ++i) { g = p.group + "/Carousel" + (i > 1 ? " " + std::to_string(i) : ""); if (!WidgetOf(g)) { bool used = false; for (auto& e : MenuElems()) if (InGroup(e.group, g)) used = true; if (!used) break; } }
    Widget w; w.group = g; w.type = W_CAROUSEL; w.cols = 3; w.rows = 1; w.totalRows = 5; w.spX = 33; w.spY = 0;
    w.refW = 126; w.refH = 62; w.gw = 3 * 126 + 2 * 33; w.gh = 62; w.gx = 0.3f - w.gw / 2; w.gy = -14.3f - w.gh / 2;
    w.centred = true; w.wrap = false;
    GridFitTiles(w);
    w.startAt = 1; GridOrigin(w);
    float cx = w.gx + w.refW / 2, cy = w.gy + w.refH / 2;
    auto img = [&](const char* name, float x, float y, float ww, float hh, const char* tex, float a, int layer) {
        Elem e; e.type = E_IMAGE; e.name = name; e.group = g; e.anchor = 4; e.x = x; e.y = y; e.w = ww; e.h = hh; e.tex = tex; e.texIdx = -2;
        e.col[3] = a; e.layer = layer; MenuElems().push_back(e); return &MenuElems().back();
    };
    Elem* bk = img("Icon backing", cx, cy, 126, 62, "U2_CARSELECT_ICON_BACKING", 80 / 255.f, 1); bk->selOnly = true;
    Elem* hl = img("Icon highlight", cx, cy, 126, 62, "U2_ICON_HIGHLIGHT", 1, 3); hl->selOnly = true; hl->col[0] = 202 / 255.f; hl->col[1] = 244 / 255.f; hl->col[2] = 138 / 255.f;
    Elem* ic = img("Icon", cx, cy, 118, 59, "", 1, 2); ic->role = "icon"; ic->tex.clear();
    float mid = w.gx + w.gw / 2;   // (the stock arrows sit 250.3 / 259.1 out from the middle)
    Elem* al = img("Arrow left", mid - 250.6f, -12.8f, 26, 26, "U2_MENU_ARROW_WHITE", 150 / 255.f, 4); al->role = "arrowL"; al->u0 = al->v0 = 1; al->u1 = al->v1 = 0;
    Elem* ar = img("Arrow right", mid + 258.8f, -12.8f, 26, 26, "U2_MENU_ARROW_WHITE", 150 / 255.f, 4); ar->role = "arrowR";
    Elem* tr = img("Scroller track", 3, -52, 562, 8, "U2_MENU_BOX_WHITE", 100 / 255.f, 1); tr->role = "gtrack";
    Elem* tb = img("Scroller tab", 3 - 281 + 8, -52.3f, 16, 16, "U2_SCROLLER_TAB", 150 / 255.f, 2); tb->role = "gtab";
    g_widgets.push_back(w);
    g_dirty = true;
    g_selGroup = g; g_sel = -1;
    Log("ui editor: carousel added as %s", g.c_str());
}
// A game menu the page stands in for (one page each; UniPad opens it when that screen comes up): the right-click menu's
// "replaces" dropdown
static void ReplacesCombo(Page& p) {
    static const char* kRep[] = { "nothing", "the pause menu", "the pause Options menu", "Yes / No confirm dialogs", "the World Map" };
    int cur = p.pauseMenu ? 1 : p.pauseOptions ? 2 : p.confirm ? 3 : p.worldMap ? 4 : 0;
    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("replaces", kRep[cur])) {
        for (int i = 0; i < 5; ++i) {
            std::string taken;   // (another page has it: picking it here moves it)
            for (auto& o : g_pages) if (&o != &p && ((i == 1 && o.pauseMenu) || (i == 2 && o.pauseOptions) || (i == 3 && o.confirm) || (i == 4 && o.worldMap))) taken = o.group;
            std::string lb = std::string(kRep[i]) + (taken.empty() ? "" : "  (now " + taken + ")");
            if (ImGui::Selectable(lb.c_str(), i == cur) && i != cur) {
                p.pauseMenu = i == 1; p.pauseOptions = i == 2; p.confirm = i == 3; p.worldMap = i == 4;
                for (auto& o : g_pages) if (&o != &p) { if (i == 1) o.pauseMenu = false; if (i == 2) o.pauseOptions = false; if (i == 3) o.confirm = false; if (i == 4) o.worldMap = false; }
                g_dirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(i == 1 ? "Opens instead of the game's pause menu (Start / Esc in the world; needs UniPad). Back closes it and\nresumes; give tiles the \"Pause: ...\" actions (Resume, Restart race, Jump to Garage, Options, Quit,\nFree Cam...) under changes."
                                                   : i == 2 ? "Opens instead of the pause menu's Options screen (its Options item; needs UniPad). Back goes back to the\npause menu; give tiles the \"Pause Options: ...\" actions (Audio, Display, Gameplay, Player) - they open the\ngame's settings panels - or open a page (UniPad Options)."
                                                   : i == 3 ? "Opens instead of the game's Yes / No dialogs (Jump to Garage?, Quit?, Restart race?...; needs UniPad).\n{dialog.text} in a text shows its question; give tiles the \"Dialog: ...\" actions (Yes, No). Back answers\nas the dialog's own Back would (No when it has none)."
                                                   : i == 4 ? "Opens instead of the game's World Map (needs UniPad, which drives the game's own map with your buttons:\nup / down its list, left / right that type's places, X show / hide the type, LT / RT the view, A the GPS).\nAn image element with \"world map\" ticked draws the map; a grid with the live list map.legend is the list\n({map.view} names the view)."
                                                   : "Opened only by tiles / the menu key.");
        }
        ImGui::EndCombo();
    }
}
static void PagePanel(Page& p) {
    if (ImGui::Checkbox("menu key opens this", &p.start)) { if (p.start) for (auto& o : g_pages) if (&o != &p) o.start = false; g_dirty = true; }
    // (what it replaces - the game's pause menu / its Options screen - is in the menu button's right-click menu)
    ImGui::SameLine(); if (ImGui::Checkbox("selected row colour", &p.useSel)) g_dirty = true;
    ImGui::SameLine(); if (ImGui::ColorEdit4("##selcol", p.selCol, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) g_dirty = true;
    auto rows = PageRows(p);
    int grids = 0, cars = 0; for (auto& r : rows) { grids += r.w->type == W_GRID; cars += r.w->type == W_CAROUSEL; }
    ImGui::TextDisabled("%d grid(s), %d carousel(s)", grids, cars);
    if (ImGui::Button("Add grid")) AddGrid(p);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A grid of tiles, 1 column x 8 rows (a backing, the tile's text, a selected-tile frame, its own scroller),\nnamed Button 1, 2, 3... Its pieces are the first tile: change them and every tile follows.");
    ImGui::SameLine(); if (ImGui::Button("Add carousel")) AddCarousel(p);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A row of icon tiles scrolling left / right, laid out as the game's pause menu: 3 on screen, the\nselected one in the middle with its frame, arrows at the ends, a scroller above. Its pieces are the first slot.");
    ImGui::SameLine();
    if (ImGui::Button(g_menu.open ? "Close the menu" : "Try it")) { if (g_menu.open) MenuClose(); else MenuOpen(p.group); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens this page as the real menu: pad / arrow keys, Enter, Esc.");
}
// The menu bar (Menus workspace, under Add image ... Undo): a button per custom menu and "+" to make another ("Add Menu +"
// when there are none). Picking one edits it alone (picking it again shows everything); under the buttons: its name,
// its page settings and Delete. Older pages - groups ticked "menu page" - aren't in it (deprecated, left as they are).
// Sub menus: custom menus inside another ("Pause 1/Unipad Options"), any depth, two rows shown at a time - the first
// row is the menus at one level (the top, or inside g_menuRoot), the second the picked one's sub menus (+ makes one
// there). Picking a sub menu moves down a level (its row becomes the first); "<" at the end of the first row goes back
// up. Right-click a menu: move it into another menu, or back to the top level.
static std::string g_menuRoot;   // the menu whose sub menus are the first row ("" = the top level)
static std::vector<std::string> SubMenus(const std::string& leader) {
    std::vector<std::string> v;
    for (auto& p : g_pages) if (p.dedicated && ParentMenu(p.group) == leader) v.push_back(p.group);
    return v;
}
static void MenuBar() {
    if (!MenuWs()) return;
    // all of them: on / off, and their fades
    if (ImGui::Checkbox("custom menus replace the game's", &customMenus)) g_dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: no menu stands in for the game's own (the pause menu, its Options, the Yes / No dialogs, the World\nMap) - the game's show again. The menu key's page still opens. Saved with the layout.");
    ImGui::SameLine(); ImGui::SetNextItemWidth(120);
    if (ImGui::SliderFloat("fade", &menuFade, 0.f, 1.f, menuFade > 0 ? "%.2f s" : "off")) { menuFade = std::max(0.f, std::min(1.f, menuFade)); g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Menus fade in as they open, out as they close, and cross-fade between pages (seconds; 0 = off).");
    auto pick = [](const std::string& g) { g_menuEd = g; g_sel = -1; g_selGroup.clear(); ClearMulti(); };
    auto newMenu = [&](const std::string& in) {   // in: the menu it goes in ("" = the top level)
        std::string g;
        for (int i = 1; ; ++i) {
            g = (in.empty() ? "" : in + "/") + "Menu " + std::to_string(i);
            bool used = PageOf(g) != nullptr; for (auto& e : MenuElems()) if (InGroup(e.group, g)) used = true;
            if (!used) break;
        }
        Page np; np.group = g; np.dedicated = true; np.start = g_pages.empty(); g_pages.push_back(np);
        pick(g); g_dirty = true;
        if (!in.empty()) g_menuRoot = in;   // (a sub menu: its leader's row on top, it in the second)
        Log("ui editor: custom menu %s made", g.c_str());
    };
    // move menu g into menu `into` ("" = the top level): its pieces, rows, links and sub menus go with it
    auto moveMenu = [&](const std::string& g, const std::string& into) {
        std::string to = (into.empty() ? "" : into + "/") + GroupLeaf(g);
        bool used = PageOf(to) != nullptr; for (auto& e : MenuElems()) if (InGroup(e.group, to)) used = true;
        if (used || InGroup(into, g)) { Log("ui editor: can't move %s there (%s)", g.c_str(), used ? "something there is already called that" : "it's inside it"); return; }
        RebaseGroup(g, to);
        g_menuEd = to; g_menuRoot = ParentMenu(ParentMenu(to)); g_sel = -1; g_selGroup.clear(); ClearMulti();
        Log("ui editor: menu %s moved to %s", g.c_str(), to.c_str());
    };
    // rename menu g (it stays where it is); false: the name's taken / empty
    auto renameMenu = [&](const std::string& g, const char* name) {
        std::string par = GroupParent(g), leaf = CleanGroup(RowName(name));
        std::string to = leaf.empty() ? std::string() : (par.empty() ? "" : par + "/") + leaf;
        if (to == g) return true;
        bool used = to.empty() || PageOf(to) != nullptr; for (auto& e : MenuElems()) if (InGroup(e.group, to)) used = true;
        if (used) { Log("ui editor: a menu or group is already called that"); return false; }
        RebaseGroup(g, to); g_dirty = true;
        if (InGroup(g_menuEd, g)) g_menuEd = CleanGroup(to + g_menuEd.substr(g.size()));
        if (InGroup(g_menuRoot, g)) g_menuRoot = CleanGroup(to + g_menuRoot.substr(g.size()));
        return true;
    };
    auto deleteMenu = [&](const std::string& g) {   // with everything in it (its sub menus too)
        MenuElems().erase(std::remove_if(MenuElems().begin(), MenuElems().end(), [&](const Elem& e) { return InGroup(e.group, g); }), MenuElems().end());
        std::string up = ParentMenu(g);
        MenuForget(g); g_dirty = true;
        if (InGroup(g_menuEd, g)) { g_menuEd.clear(); g_sel = -1; g_selGroup.clear(); ClearMulti(); }
        if (InGroup(g_menuRoot, g)) g_menuRoot = up;
        Log("ui editor: menu %s deleted", g.c_str());
    };
    MenuEdOff("");   // (forgets a menu that went away)
    if (!g_menuRoot.empty() && (!PageOf(g_menuRoot) || (!g_menuEd.empty() && !InGroup(g_menuEd, g_menuRoot)))) g_menuRoot.clear();
    bool any = false; for (auto& p : g_pages) any |= p.dedicated;
    if (!any) {
        if (ImGui::Button("Add Menu +")) newMenu("");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new custom menu: its own page, edited on its own. Add images / text / shapes / grids to it.");
        return;
    }
    // a menu's button: picked = lit; right-click = move it
    bool moved = false;
    auto button = [&](const std::string& g, bool on, int row, const std::function<void()>& click) {
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        std::string subs = SubMenus(g).empty() ? "" : " >";
        bool hit = ImGui::Button((GroupLeaf(g) + subs + "##menu" + std::to_string(row) + g).c_str());
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\nRight-click: rename, what it replaces (the pause menu / its Options), move into another menu / to the top level, delete.", g.c_str(),
            on ? "Editing this menu alone. Click again to stop." : "Edit this menu (alone on screen and in the list).");
        if (ImGui::BeginPopupContextItem(("##menuctx" + std::to_string(row) + g).c_str())) {
            // name (Enter or leaving the box renames it)
            static char nb[64]; static std::string nbFor;
            if (nbFor != g || ImGui::IsWindowAppearing()) { snprintf(nb, sizeof nb, "%s", GroupLeaf(g).c_str()); nbFor = g; }
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(180);
            bool enter = ImGui::InputText("name##menuname", nb, sizeof nb, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
                if (renameMenu(g, nb)) { moved = true; if (enter) ImGui::CloseCurrentPopup(); }
                else snprintf(nb, sizeof nb, "%s", GroupLeaf(g).c_str());
            }
            if (!ParentMenu(g).empty()) ImGui::TextDisabled("in %s", ParentMenu(g).c_str());
            if (!moved) if (Page* pg = PageOf(g)) ReplacesCombo(*pg);
            ImGui::Separator();
            if (!moved && ImGui::BeginMenu("Move into")) {
                std::string cur = ParentMenu(g);
                int n = 0;
                for (auto& p : g_pages) {
                    if (!p.dedicated || p.group == cur || InGroup(p.group, g)) continue;   // (not where it is, not itself / its own sub menus)
                    ++n; if (ImGui::MenuItem(p.group.c_str())) { moveMenu(g, p.group); moved = true; }
                }
                if (!n) ImGui::TextDisabled("(no other menu)");
                ImGui::EndMenu();
            }
            if (!moved && !ParentMenu(g).empty() && ImGui::MenuItem("Move to the top level")) { moveMenu(g, ""); moved = true; }
            ImGui::Separator();
            if (!moved && ImGui::BeginMenu("Delete menu")) {
                ImGui::TextUnformatted(("Delete \"" + GroupLeaf(g) + "\" and everything in it" + (SubMenus(g).empty() ? "?" : " (its sub menus too)?")).c_str());
                if (ImGui::MenuItem("Delete")) { deleteMenu(g); moved = true; }
                ImGui::MenuItem("Cancel");
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
        if (hit && !moved) click();
    };
    // row 1: the menus at this level; the one holding the menu being edited is lit
    std::string lead;   // (the row-1 menu picked, or holding the one picked)
    {
        auto row = SubMenus(g_menuRoot);
        for (auto& g : row) if (!g_menuEd.empty() && InGroup(g_menuEd, g)) lead = g;
        for (size_t i = 0; i < row.size() && !moved; ++i) {
            if (i) ImGui::SameLine();
            std::string g = row[i];
            button(g, g == lead, 1, [&] { pick(g == g_menuEd ? std::string() : g); });
        }
        if (moved) return;
        if (!row.empty()) ImGui::SameLine();
        if (ImGui::Button("+##menuadd1")) { newMenu(g_menuRoot); return; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(g_menuRoot.empty() ? "Add another custom menu." : "Add another sub menu to %s.", g_menuRoot.c_str());
        if (!g_menuRoot.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("<##menuup")) { std::string up = g_menuRoot; g_menuRoot = ParentMenu(up); pick(up); return; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back one menu (to %s's row).", ParentMenu(g_menuRoot).empty() ? "the top level" : ParentMenu(g_menuRoot).c_str());
        }
    }
    // row 2 (always shown): the lit menu's sub menus (picking one moves down: its row becomes the first)
    if (!lead.empty()) {
        auto row = SubMenus(lead);
        ImGui::TextDisabled("%s >", GroupLeaf(lead).c_str()); ImGui::SameLine();
        for (size_t i = 0; i < row.size() && !moved; ++i) {
            std::string g = row[i];
            bool on = !g_menuEd.empty() && InGroup(g_menuEd, g);
            button(g, on, 2, [&] { if (g == g_menuEd) pick(lead); else { pick(g); g_menuRoot = lead; } });
            ImGui::SameLine();
        }
        if (moved) return;
        if (ImGui::Button("+##menuadd2")) { newMenu(lead); return; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a sub menu to %s.", lead.c_str());
    } else {   // (always there: nothing picked yet)
        ImGui::TextDisabled("sub menus >"); ImGui::SameLine();
        ImGui::BeginDisabled(); ImGui::Button("+##menuadd2"); ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Pick a menu above: its sub menus show in this row (+ adds one).");
    }
    Page* p = g_menuEd.empty() ? nullptr : PageOf(g_menuEd);
    if (!p) return;
    // (its name and Delete: right-click its button)
    PagePanel(*p);
}
static void GroupMenuProps(const std::string& g) {
    if (!MenuWs()) return;
    ImGui::SeparatorText("Menu");
    if (Page* sp = PageOf(g)) {
        if (GroupParent(g).empty()) ImGui::TextDisabled("A custom menu (the menu bar under Add image / Undo).");
        else ImGui::TextDisabled("A sub menu of %s (the menu bar).", ParentMenu(g).c_str());
        PagePanel(*sp); return;
    }
    Page* page = PageOf(GroupParent(g));
    Widget* w = WidgetOf(g);
    if (!w) {
        if (!page) { ImGui::TextDisabled("Menus are made with the menu bar (Add Menu +); grids / carousels are groups directly in one."); return; }
        int t = 0;
        ImGui::SetNextItemWidth(220);
        if (ImGui::Combo("widget", &t, kWMake, 3) && t != 0) {
            Widget nw; nw.group = g; if (kWMakeType[t] == W_CAROUSEL) { nw.cols = 3; nw.totalRows = 3; }
            g_widgets.push_back(nw); MakeGridLike(g_widgets.back(), kWMakeType[t]);
        }
        return;
    }
    WidgetPanel(*w, page);
}
// ---------------------------------------------------------------- save / load
static void SaveWidgetProps(FILE* f, const Widget& w) {
    fprintf(f, "type=%s\n", kWKey[w.type]);
    if (!w.bind.empty()) fprintf(f, "bind=%s\n", w.bind.c_str());
    if (!w.desc.empty()) fprintf(f, "desc=%s\n", Esc(w.desc).c_str());
    if (!w.action.empty()) fprintf(f, "action=%s\n", Esc(w.action).c_str());
    if (!w.choices.empty()) { std::string j; for (size_t i = 0; i < w.choices.size(); ++i) j += (i ? "|" : "") + w.choices[i]; fprintf(f, "choices=%s\n", Esc(j).c_str()); }
    if (GridLike(w)) {
        Widget c = w; GridOrigin(c);
        if (w.type == W_CAROUSEL) fprintf(f, "carousel=%d %d\n", w.centred ? 1 : 0, w.wrap ? 1 : 0);
        fprintf(f, "grid=%d %d %g %g %g %g %g %g %d %d %g %g %g %g %d %d\n", w.cols, w.rows, w.gw, w.gh, w.gx, w.gy, w.refW, w.refH, w.scalePieces ? 1 : 0, w.startAt, c.ox, c.oy, w.spX, w.spY, w.totalRows, w.rowSpace ? 1 : 0);
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
    for (auto& p : g_pages) {
        fprintf(f, "\n[page]\ngroup=%s\nstart=%d\nselcolor=%d %g %g %g %g\ndedicated=1\n", Esc(p.group).c_str(), p.start ? 1 : 0,
                p.useSel ? 1 : 0, p.selCol[0], p.selCol[1], p.selCol[2], p.selCol[3]);
        if (p.pauseMenu) fprintf(f, "pausemenu=1\n");
        if (p.pauseOptions) fprintf(f, "pauseopts=1\n");
        if (p.confirm) fprintf(f, "confirmdlg=1\n");
        if (p.worldMap) fprintf(f, "worldmap=1\n");
    }
    for (auto& w : g_widgets) { fprintf(f, "\n[widget]\ngroup=%s\n", Esc(w.group).c_str()); SaveWidgetProps(f, w); }
}
// a widget / page line; returns false if k isn't one of theirs
static bool LoadWidgetKey(Widget& w, const std::string& k, const std::string& v) {
    if (k == "type") { for (int i = 0; i < W_NTYPES; ++i) if (v == kWKey[i]) w.type = i; }
    else if (k == "bind") w.bind = v;
    else if (k == "desc") w.desc = Unesc(v);
    else if (k == "action") w.action = Unesc(v);
    else if (k == "choices") { w.choices.clear(); std::string s = Unesc(v), cur; for (size_t i = 0; i <= s.size(); ++i) { if (i == s.size() || s[i] == '|') { if (!cur.empty()) w.choices.push_back(cur); cur.clear(); } else cur += s[i]; } }
    else if (k == "grid") { int sp = 1, rs = 0; int got = sscanf(v.c_str(), "%d %d %f %f %f %f %f %f %d %d %f %f %f %f %d %d", &w.cols, &w.rows, &w.gw, &w.gh, &w.gx, &w.gy, &w.refW, &w.refH, &sp, &w.startAt, &w.ox, &w.oy, &w.spX, &w.spY, &w.totalRows, &rs); w.scalePieces = sp != 0; w.rowSpace = got >= 16 && rs != 0; w.hasOrigin = got >= 12; w.spX = std::max(0.f, w.spX); w.spY = std::max(0.f, w.spY); w.totalRows = std::max(0, w.totalRows); w.startAt = std::max(0, std::min(2, w.startAt)); w.cols = std::max(1, w.cols); w.rows = std::max(1, w.rows); }
    else if (k == "num") sscanf(v.c_str(), "%f %f %f %f", &w.min, &w.max, &w.step, &w.value);
    else if (k == "carousel") { int c = 1, r = 0; sscanf(v.c_str(), "%d %d", &c, &r); w.centred = c != 0; w.wrap = r != 0; }
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

static int g_loadSect = 0;   // 2 page, 3 widget
static int g_loadOld = 0;    // older menu things left out while loading (widget-kit templates / pieces, one-row widgets)
static void LoadMenusReset() { g_pages.clear(); g_widgets.clear(); g_loadOld = 0; }
static int LoadMenuSection(const std::string& s) {
    if (s == "[page]") { g_pages.push_back(Page()); return 2; }
    if (s == "[widget]") { g_widgets.push_back(Widget()); return 3; }
    if (s == "[template]") ++g_loadOld;   // (the widget kit: gone)
    return 9;   // unknown: skipped
}
static bool LoadMenuKey(int sect, const std::string& k, const std::string& v) {
    if (sect == 2 && !g_pages.empty()) {   // (older page lines - spacing, firsty, coldist, colx, addcol, rows - are ignored)
        Page& p = g_pages.back();
        if (k == "group") p.group = Unesc(v);
        else if (k == "start") p.start = atoi(v.c_str()) != 0;
        else if (k == "pausemenu") p.pauseMenu = atoi(v.c_str()) != 0;
        else if (k == "pauseopts") p.pauseOptions = atoi(v.c_str()) != 0;
        else if (k == "confirmdlg") p.confirm = atoi(v.c_str()) != 0;
        else if (k == "worldmap") p.worldMap = atoi(v.c_str()) != 0;
        else if (k == "selcolor") { int u = 1; sscanf(v.c_str(), "%d %f %f %f %f", &u, &p.selCol[0], &p.selCol[1], &p.selCol[2], &p.selCol[3]); p.useSel = u != 0; }
        return true;
    }
    if (sect == 3 && !g_widgets.empty()) { if (k == "group") g_widgets.back().group = Unesc(v); else LoadWidgetKey(g_widgets.back(), k, v); return true; }
    return sect == 9;
}
static void LoadMenuEnd() {
    // older one-row widgets (button / slider / picker / highlight rows) are left out; every page is a custom menu
    size_t before = g_widgets.size();
    g_widgets.erase(std::remove_if(g_widgets.begin(), g_widgets.end(), [](const Widget& w) { return w.group.empty() || !GridLike(w); }), g_widgets.end());
    g_loadOld += (int)(before - g_widgets.size());
    for (auto& w : g_widgets) GridMigrate(w);
    g_pages.erase(std::remove_if(g_pages.begin(), g_pages.end(), [](const Page& p) { return p.group.empty(); }), g_pages.end());
    for (auto& p : g_pages) p.dedicated = true;
    if (g_loadOld) Log("ui editor: %d older menu thing(s) (widget-kit templates, one-row widgets) left out - grids / carousels replaced them", g_loadOld);
}
static bool MenuOpenNow() { return g_menu.open; }
