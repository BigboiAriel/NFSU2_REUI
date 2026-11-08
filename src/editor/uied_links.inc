// ---------------------------------------------------------------- linked groups (2026-11-08)
// Groups that are kept the same: the glyph bar's copies, a carousel's select pieces... on several menus. Each member
// keeps its own place (and layer) - an offset - and everything else is shared: an edit to one of them (a colour, a
// piece added, moved inside the group, renamed...) is made to the others as it happens. Moving the whole group only
// moves that one. Only the pieces (elements) are linked; a grid / carousel's own settings (its tiles, its rows) aren't.
// Saved as grouplink=<mode>|<dx> <dy> <layer> <group>|... (before the elements).
struct LinkMember { std::string group; float dx = 0, dy = 0; int dl = 0; };
struct LinkPiece { Elem e; std::string sig; };   // e: in the link's own terms (group inside the member, no offset)
struct LinkSet { int mode = 0; std::vector<LinkMember> m; std::vector<LinkPiece> ref; bool refOk = false; };
static std::vector<LinkSet> g_links;
static unsigned g_linkGen = ~0u;
// link / unlink picked in a menu: done at the end of the frame's panel (the lists being drawn keep their indices)
struct LinkAsk { int kind = 0; std::string a, b; };   // 1 join (a's look, b joins), 2 unlink a
static std::vector<LinkAsk> g_linkAsk;
static void LinkJoin(const std::string& look, const std::string& joining);
static void Unlink(const std::string& g);

static void LinksReset() { g_links.clear(); g_linkGen = ~0u; }
static LinkSet* LinkOf(const std::string& g, int mode, int* member = nullptr) {
    for (auto& s : g_links) if (s.mode == mode) for (size_t k = 0; k < s.m.size(); ++k) if (s.m[k].group == g) { if (member) *member = (int)k; return &s; }
    return nullptr;
}
static const char* kCutAt = "group:@";   // a cutter aimed at a group inside the member: kept relative
static Elem LinkRel(const Elem& src, const LinkMember& mb) {
    Elem e = src;
    e.group = e.group.size() > mb.group.size() ? e.group.substr(mb.group.size() + 1) : std::string();
    if (e.cutTarget.compare(0, 6, "group:") == 0 && InGroup(e.cutTarget.substr(6), mb.group)) e.cutTarget = kCutAt + e.cutTarget.substr(6 + mb.group.size());
    e.x -= mb.dx; e.y -= mb.dy; e.layer -= mb.dl;
    return e;
}
static Elem LinkAbs(const Elem& rel, const LinkMember& mb) {
    Elem e = rel;
    e.group = rel.group.empty() ? mb.group : mb.group + "/" + rel.group;
    if (e.cutTarget.compare(0, 7, kCutAt) == 0) e.cutTarget = "group:" + mb.group + e.cutTarget.substr(7);
    e.x += mb.dx; e.y += mb.dy; e.layer += mb.dl;
    return e;
}
static std::string LinkSig(const Elem& rel) { Elem c = rel; c.x = c.y = 0; c.layer = 0; std::string o; PutElemS(o, 0, c); return o; }

// what's being edited is in (or is) this group
static bool LinkHasSel(const std::string& g, int mode) {
    if (mode != g_editMode) return false;
    auto& els = Elems();
    if (g_sel >= 0 && g_sel < (int)els.size() && InGroup(els[g_sel].group, g)) return true;
    if (g_dragging >= 0 && g_dragging < (int)els.size() && InGroup(els[g_dragging].group, g)) return true;
    if (!g_selGroup.empty() && InGroup(g_selGroup, g)) return true;
    for (int i : g_multi) if (i >= 0 && i < (int)els.size() && InGroup(els[i].group, g)) return true;
    for (auto& x : g_multiG) if (InGroup(x, g)) return true;
    return false;
}
// the look is member src's; the others are made again from it, each where its first piece was in the list
static void LinkRebuild(LinkSet& s, int src) {
    auto& els = g_layout[s.mode];
    s.ref.clear();
    for (auto& e : els) if (InGroup(e.group, s.m[src].group)) { LinkPiece p; p.e = LinkRel(e, s.m[src]); p.sig = LinkSig(p.e); s.ref.push_back(p); }
    s.refOk = true;
    std::vector<int> owner(els.size(), -1);
    for (size_t i = 0; i < els.size(); ++i) for (size_t k = 0; k < s.m.size(); ++k) if ((int)k != src && InGroup(els[i].group, s.m[k].group)) owner[i] = (int)k;
    {   // the same number of pieces everywhere (a value edited, a drag): written over in place, so nothing in the list
        // moves - the element being edited stays the same object (the panel's fields are keyed on it: a drag goes on)
        std::vector<std::vector<int>> idx(s.m.size());
        for (size_t i = 0; i < els.size(); ++i) if (owner[i] >= 0) idx[owner[i]].push_back((int)i);
        bool fits = true;
        for (size_t k = 0; k < s.m.size(); ++k) if ((int)k != src && idx[k].size() != s.ref.size()) fits = false;
        if (fits) {
            for (size_t k = 0; k < s.m.size(); ++k) if ((int)k != src) for (size_t j = 0; j < s.ref.size(); ++j) els[idx[k][j]] = LinkAbs(s.ref[j].e, s.m[k]);
            return;
        }
    }
    std::vector<Elem> out; out.reserve(els.size() + s.ref.size() * s.m.size());
    std::vector<int> map(els.size(), -1);
    std::vector<char> placed(s.m.size(), 0);
    for (size_t i = 0; i < els.size(); ++i) {
        int k = owner[i];
        if (k >= 0) {
            if (!placed[k]) { placed[k] = 1; for (auto& p : s.ref) out.push_back(LinkAbs(p.e, s.m[k])); }
            continue;
        }
        map[i] = (int)out.size(); out.push_back(els[i]);
    }
    for (size_t k = 0; k < s.m.size(); ++k) if ((int)k != src && !placed[k]) for (auto& p : s.ref) out.push_back(LinkAbs(p.e, s.m[k]));
    els.swap(out);
    if (s.mode == g_editMode) {   // (the picked / dragged element indices follow)
        auto re = [&](int i) { return i >= 0 && i < (int)map.size() ? map[i] : -1; };
        g_sel = re(g_sel); g_dragging = re(g_dragging);
        std::vector<int> mm; for (int i : g_multi) { int j = re(i); if (j >= 0) mm.push_back(j); } g_multi.swap(mm);
    }
}
// after edits: a member that changed gives its look to the others (moving one whole member moves only it)
static void LinksSync() {
    std::vector<LinkAsk> asks; asks.swap(g_linkAsk);
    for (auto& a : asks) { if (a.kind == 1) LinkJoin(a.a, a.b); else if (a.kind == 2) Unlink(a.a); }
    if (g_dirty.gen == g_linkGen) return;
    g_linkGen = g_dirty.gen;
    for (size_t si = 0; si < g_links.size();) {
        LinkSet& s = g_links[si];
        auto& els = g_layout[s.mode];
        std::vector<std::vector<int>> idx(s.m.size());
        for (int i = 0; i < (int)els.size(); ++i) for (size_t k = 0; k < s.m.size(); ++k) if (InGroup(els[i].group, s.m[k].group)) idx[k].push_back(i);
        for (size_t k = s.m.size(); k-- > 0;) if (idx[k].empty()) { Log("ui editor: %s unlinked (it's gone)", s.m[k].group.c_str()); s.m.erase(s.m.begin() + k); idx.erase(idx.begin() + k); }
        if (s.m.size() < 2) { g_links.erase(g_links.begin() + si); continue; }
        bool fresh = !s.refOk;
        if (fresh) { s.ref.clear(); for (int i : idx[0]) { LinkPiece p; p.e = LinkRel(els[i], s.m[0]); p.sig = LinkSig(p.e); s.ref.push_back(p); } s.refOk = true; }
        std::vector<int> changed;
        for (size_t k = 0; k < s.m.size(); ++k) {
            if (idx[k].size() != s.ref.size()) { changed.push_back((int)k); continue; }
            bool same = true, first = true; float tx = 0, ty = 0; int tl = 0;
            for (size_t j = 0; j < idx[k].size() && same; ++j) {
                Elem r = LinkRel(els[idx[k][j]], s.m[k]);
                const Elem& q = s.ref[j].e;
                float ex = r.x - q.x, ey = r.y - q.y; int el = r.layer - q.layer;
                if (first) { tx = ex; ty = ey; tl = el; first = false; }
                else if (fabsf(ex - tx) > 0.01f || fabsf(ey - ty) > 0.01f || el != tl) { same = false; break; }
                if (LinkSig(r) != s.ref[j].sig) same = false;
            }
            if (!same) changed.push_back((int)k);
            else { s.m[k].dx += tx; s.m[k].dy += ty; s.m[k].dl += tl; }   // (moved as a whole, or nothing: its own offset)
        }
        if (!changed.empty()) {
            int src = fresh ? 0 : changed[0];   // (loaded / undone and they differ: the first one's look...)
            for (int k : changed) if (LinkHasSel(s.m[k].group, s.mode)) { src = k; break; }   // (... unless one's being edited)
            LinkRebuild(s, src);
        }
        ++si;
    }
}

// renames / moves follow the groups; a member that's ungrouped (dissolved into the group around it) leaves its link
static void LinksRebase(const std::string& from, const std::string& to) {
    for (size_t si = 0; si < g_links.size();) {
        auto& m = g_links[si].m;
        for (size_t k = m.size(); k-- > 0;) {
            std::string& g = m[k].group;
            if (!InGroup(g, from)) continue;
            if (g == from && (to.empty() || (to != from && InGroup(from, to)))) { m.erase(m.begin() + k); continue; }
            g = CleanGroup(to + g.substr(from.size()));
        }
        if (m.size() < 2) g_links.erase(g_links.begin() + si); else ++si;
    }
}
static void SaveLinks(FILE* f) {
    for (auto& s : g_links) {
        if (s.m.size() < 2) continue;
        std::string l = std::to_string(s.mode);
        for (auto& m : s.m) { char b[64]; snprintf(b, sizeof b, "|%g %g %d ", m.dx, m.dy, m.dl); l += b; l += Esc(m.group); }
        fprintf(f, "grouplink=%s\n", l.c_str());
    }
}
static void LoadLinkLine(const std::string& v, int intoMode) {
    LinkSet s; s.mode = intoMode >= 0 ? intoMode : std::max(0, std::min(NMODES - 1, atoi(v.c_str())));
    size_t p = v.find('|');
    while (p != std::string::npos) {
        size_t q = v.find('|', p + 1);
        std::string part = v.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1);
        LinkMember m; int used = 0;
        if (sscanf(part.c_str(), "%f %f %d %n", &m.dx, &m.dy, &m.dl, &used) >= 3 && used > 0) { m.group = CleanGroup(Unesc(part.substr(used))); if (!m.group.empty()) s.m.push_back(m); }
        p = q;
    }
    if (s.m.size() >= 2) g_links.push_back(s);
}

// can group g be linked with t? (not itself, not one inside the other, not a menu page or a group with one in it,
// not overlapping a group linked elsewhere)
static bool LinkBlocked(const std::string& x, int mode) {
    for (auto& p : g_pages) if (InGroup(p.group, x)) return true;
    for (auto& s : g_links) if (s.mode == mode) for (auto& m : s.m) if (m.group != x && (InGroup(m.group, x) || InGroup(x, m.group))) return true;
    return false;
}
static bool CanLink(const std::string& g, const std::string& t, int mode) {
    if (g.empty() || t.empty() || g == t || InGroup(g, t) || InGroup(t, g)) return false;
    if (LinkBlocked(g, mode) || LinkBlocked(t, mode)) return false;
    LinkSet* a = LinkOf(g, mode); LinkSet* b = LinkOf(t, mode);
    return !a || a != b;
}
// link `joining` with `look` (and the groups each is linked with already): all of them take look's pieces, each where it is
static void LinkJoin(const std::string& look, const std::string& joining) {
    int mode = g_editMode;
    auto& els = g_layout[mode];
    auto firstOf = [&](const std::string& g) { for (int i = 0; i < (int)els.size(); ++i) if (InGroup(els[i].group, g)) return i; return -1; };
    int la = firstOf(look); if (la < 0 || firstOf(joining) < 0) return;
    LinkSet ns; ns.mode = mode;
    int ka = -1; LinkSet* sa = LinkOf(look, mode, &ka);
    LinkMember lm; lm.group = look;
    if (sa) lm = sa->m[ka];
    ns.m.push_back(lm);
    // a newcomer's offset: its piece that matches look's first one (by name and kind; else its first piece)
    const Elem& ea = els[la];
    auto add = [&](const std::string& g) {
        for (auto& m : ns.m) if (m.group == g) return;
        int j = -1;
        for (int i = 0; i < (int)els.size(); ++i) if (InGroup(els[i].group, g) && els[i].name == ea.name && els[i].type == ea.type && LinkRel(els[i], LinkMember{ g }).group == LinkRel(ea, LinkMember{ look }).group) { j = i; break; }
        if (j < 0) j = firstOf(g);
        LinkMember m; m.group = g; m.dx = lm.dx + (els[j].x - ea.x); m.dy = lm.dy + (els[j].y - ea.y); m.dl = lm.dl + (els[j].layer - ea.layer);
        ns.m.push_back(m);
    };
    if (sa) for (auto& m : sa->m) if (m.group != look) ns.m.push_back(m);   // (already linked with look: their offsets stand)
    std::vector<std::string> others{ joining };
    if (LinkSet* sb = LinkOf(joining, mode)) for (auto& m : sb->m) if (m.group != joining) others.push_back(m.group);
    for (auto& g : others) add(g);
    // the sets they were in go; the new one takes their place
    g_links.erase(std::remove_if(g_links.begin(), g_links.end(), [&](const LinkSet& s) {
        if (s.mode != mode) return false;
        for (auto& m : s.m) for (auto& n : ns.m) if (m.group == n.group) return true;
        return false;
    }), g_links.end());
    g_links.push_back(ns);
    LinkRebuild(g_links.back(), 0);
    g_dirty = true; g_linkGen = g_dirty.gen;
    Log("ui editor: %s linked with %s (%d groups share %s's pieces)", joining.c_str(), look.c_str(), (int)ns.m.size(), look.c_str());
}
static void Unlink(const std::string& g) {
    int k = -1; LinkSet* s = LinkOf(g, g_editMode, &k); if (!s) return;
    s->m.erase(s->m.begin() + k);
    if (s->m.size() < 2) g_links.erase(g_links.begin() + (s - g_links.data()));
    g_dirty = true; g_linkGen = g_dirty.gen;
    Log("ui editor: %s unlinked", g.c_str());
}
static int LinkCount(const std::string& g) { LinkSet* s = LinkOf(g, g_editMode); return s ? (int)s->m.size() : 0; }
// go to a group (on its own menu)
static void LinkGoTo(const std::string& g) {
    if (MenuWs()) {
        std::string pg = OwnerPage(g);
        Page* p = pg.empty() ? nullptr : PageOf(pg);
        g_menuEd = p && p->dedicated ? pg : std::string();
        g_menuRoot = g_menuEd.empty() ? std::string() : ParentMenu(ParentMenu(g_menuEd));
    }
    g_sel = -1; ClearMulti(); g_selGroup = g;
}
// the link items: in the group's right-click menu, and behind the group panel's Link button
static void LinkMenuItems(const std::string& g) {
    int mode = g_editMode, k = -1;
    LinkSet* s = LinkOf(g, mode, &k);
    if (s) {
        ImGui::TextDisabled("Linked with:");
        std::vector<std::string> mates; for (auto& m : s->m) if (m.group != g) mates.push_back(m.group);
        for (auto& x : mates) if (ImGui::MenuItem(("  " + x + "##lgo").c_str())) { LinkGoTo(x); return; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click one to go to it.");
        if (ImGui::MenuItem("Unlink this group")) { g_linkAsk.push_back({ 2, g, "" }); return; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("It keeps its pieces as they are now; edits to it stop going to the others (and theirs to it).");
    }
    if (LinkBlocked(g, mode)) {
        ImGui::BeginDisabled(); ImGui::MenuItem("Link with..."); ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Menus (and groups with a menu inside) can't be linked, nor a group inside / around one that's linked.");
        return;
    }
    if (ImGui::BeginMenu(s ? "Link another..." : "Link with...")) {
        std::vector<std::string> same, other;
        for (auto& n : GroupNames()) {
            if (!CanLink(g, n, mode)) continue;
            (GroupLeaf(n) == GroupLeaf(g) ? same : other).push_back(n);
        }
        auto row = [&](const std::string& n) {
            int c = LinkCount(n);
            std::string lab = n + (c ? "  (linked x" + std::to_string(c) + ")" : "");
            if (ImGui::BeginMenu((lab + "##lw").c_str())) {
                if (ImGui::MenuItem("Use its look here")) g_linkAsk.push_back({ 1, n, g });
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("This group's pieces become a copy of %s's (each stays where it is).", n.c_str());
                if (ImGui::MenuItem("Use this group's look there")) g_linkAsk.push_back({ 1, g, n });
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s's pieces become a copy of this group's (each stays where it is).", n.c_str());
                ImGui::EndMenu();
            }
        };
        if (same.empty() && other.empty()) ImGui::TextDisabled("(no other group to link with)");
        for (auto& n : same) row(n);
        if (!other.empty()) {
            if (!same.empty()) ImGui::Separator();
            if (same.empty()) for (auto& n : other) row(n);
            else if (ImGui::BeginMenu("Other groups")) { for (auto& n : other) row(n); ImGui::EndMenu(); }
        }
        ImGui::EndMenu();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Linked groups keep the same pieces: an edit to one goes to the others.\nEach keeps its own place (moving a whole group moves only that one).");
}
