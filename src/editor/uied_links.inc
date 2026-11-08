// ---------------------------------------------------------------- linked groups (2026-11-08)
// Groups that are kept the same: the glyph bar's copies, a carousel's select pieces... on several menus. Each member
// keeps its own place (and layer) - an offset - and everything else is shared: an edit to one of them (a colour, a
// piece added, moved inside the group, renamed...) is made to the others as it happens. Moving the whole group only
// moves that one. Only the pieces (elements) are linked; a grid / carousel's own settings (its tiles, its rows) aren't.
// Saved as grouplink=<mode>[ <name>]|<dx> <dy> <layer> <group>|... (before the elements).
// A named one ("Save as linked group", 2026-11-08) is listed on the Linked groups page - a preview, Add (another linked
// copy, on the menu being edited) and Unlink - and stays while it has one copy; an unnamed one goes below two.
struct LinkMember { std::string group; float dx = 0, dy = 0; int dl = 0; };
struct LinkPiece { Elem e; std::string sig; };   // e: in the link's own terms (group inside the member, no offset)
struct LinkSet { int mode = 0; std::string name; std::vector<LinkMember> m; std::vector<LinkPiece> ref; bool refOk = false;
                 bool Keep() const { return m.size() >= 2 || (!name.empty() && !m.empty()); } };
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
        if (!s.Keep()) { if (!s.name.empty()) Log("ui editor: linked group %s gone (no copies left)", s.name.c_str()); g_links.erase(g_links.begin() + si); continue; }
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
        if (!g_links[si].Keep()) g_links.erase(g_links.begin() + si); else ++si;
    }
}
static void SaveLinks(FILE* f) {
    for (auto& s : g_links) {
        if (!s.Keep()) continue;
        std::string l = std::to_string(s.mode);
        if (!s.name.empty()) l += " " + Esc(s.name);
        for (auto& m : s.m) { char b[64]; snprintf(b, sizeof b, "|%g %g %d ", m.dx, m.dy, m.dl); l += b; l += Esc(m.group); }
        fprintf(f, "grouplink=%s\n", l.c_str());
    }
}
static void LoadLinkLine(const std::string& v, int intoMode) {
    LinkSet s; s.mode = intoMode >= 0 ? intoMode : std::max(0, std::min(NMODES - 1, atoi(v.c_str())));
    size_t p = v.find('|');
    { std::string head = v.substr(0, p); size_t sp = head.find(' '); if (sp != std::string::npos) s.name = Unesc(head.substr(sp + 1)); }
    while (p != std::string::npos) {
        size_t q = v.find('|', p + 1);
        std::string part = v.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1);
        LinkMember m; int used = 0;
        if (sscanf(part.c_str(), "%f %f %d %n", &m.dx, &m.dy, &m.dl, &used) >= 3 && used > 0) { m.group = CleanGroup(Unesc(part.substr(used))); if (!m.group.empty()) s.m.push_back(m); }
        p = q;
    }
    if (s.Keep()) g_links.push_back(s);
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
    if (sa) ns.name = sa->name;
    if (ns.name.empty()) if (LinkSet* sb = LinkOf(joining, mode)) ns.name = sb->name;   // (a named one keeps its name)
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
    if (!s->Keep()) g_links.erase(g_links.begin() + (s - g_links.data()));
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

// ---- Save as linked group: names the group's link (a new one with just it, if it's not linked yet)
static LinkSet* LinkNamed(const std::string& name, int mode) { for (auto& s : g_links) if (s.mode == mode && s.name == name) return &s; return nullptr; }
static std::string LinkCleanName(const char* n) { std::string o; for (const char* c = n; *c; ++c) if (*c != '|' && *c != '\n') o += *c; while (!o.empty() && o.back() == ' ') o.pop_back(); size_t b = o.find_first_not_of(' '); return b == std::string::npos ? std::string() : o.substr(b); }
static bool LinkSaveNamed(const std::string& g, const std::string& name) {
    int mode = g_editMode;
    if (name.empty() || g.empty()) return false;
    LinkSet* s = LinkOf(g, mode);
    LinkSet* other = LinkNamed(name, mode);
    if (other && other != s) return false;   // (taken)
    if (s) { s->name = name; }
    else {
        if (LinkBlocked(g, mode)) return false;
        bool any = false; for (auto& e : g_layout[mode]) any |= InGroup(e.group, g);
        if (!any) return false;
        LinkSet ns; ns.mode = mode; ns.name = name; LinkMember m; m.group = g; ns.m.push_back(m);
        g_links.push_back(ns);
        LinkRebuild(g_links.back(), 0);   // (just reads its look: there are no other copies)
    }
    g_dirty = true; g_linkGen = g_dirty.gen;
    Log("ui editor: %s saved as linked group %s", g.c_str(), name.c_str());
    return true;
}
// the name box + Save (the group panel's popup, the list's right-click menu); true when saved
static bool LinkSaveUI(const std::string& g) {
    LinkSet* s = LinkOf(g, g_editMode);
    static char nb[64]; static std::string nbFor;
    if (nbFor != g) { nbFor = g; snprintf(nb, sizeof nb, "%s", s && !s->name.empty() ? s->name.c_str() : GroupLeaf(g).c_str()); }
    ImGui::TextDisabled(s && !s->name.empty() ? "Linked group (rename)" : "Save as linked group");
    ImGui::SetNextItemWidth(180);
    bool enter = ImGui::InputText("##lgname", nb, sizeof nb, ImGuiInputTextFlags_EnterReturnsTrue);
    std::string n = LinkCleanName(nb);
    LinkSet* taken = n.empty() ? nullptr : LinkNamed(n, g_editMode);
    bool ok = !n.empty() && (!taken || taken == s) && (s || !LinkBlocked(g, g_editMode));
    ImGui::SameLine();
    ImGui::BeginDisabled(!ok);
    bool go = ImGui::Button("Save##lg") || (enter && ok);
    ImGui::EndDisabled();
    if (taken && taken != s) ImGui::TextDisabled("(a linked group is already called that)");
    else if (!s && LinkBlocked(g, g_editMode)) ImGui::TextDisabled("(menus, and groups with a menu inside, can't be linked)");
    else ImGui::TextDisabled("Listed on the Linked groups page: add more linked copies from there.");
    if (go && LinkSaveNamed(g, n)) { nbFor.clear(); return true; }
    return false;
}

// ---- the Linked groups page: every link in this game mode (the named ones first), a preview, Add / Unlink
static void ElemsPreview(std::vector<Elem>& cache, ImVec2 box, const char* msg) {
    ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + box.x, p0.y + box.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(28, 30, 36, 255), 4.f);
    dl->AddRect(p0, p1, IM_COL32(80, 84, 96, 255), 4.f);
    ImGui::Dummy(box);
    if (cache.empty()) { dl->AddText(ImVec2(p0.x + 10, p0.y + 10), IM_COL32(140, 140, 150, 255), msg); return; }
    // drawn as on the game screen, then fitted into the box (every vertex scaled about the pieces' bounds)
    ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
    for (auto& e : cache) { if (!e.visible) continue; ImVec2 a, b; ElemRect(e, a, b); lo.x = std::min(lo.x, a.x); lo.y = std::min(lo.y, a.y); hi.x = std::max(hi.x, b.x); hi.y = std::max(hi.y, b.y); }
    if (!(hi.x > lo.x && hi.y > lo.y)) return;
    dl->PushClipRect(p0, p1, true);
    int v0 = dl->VtxBuffer.Size;
    g_drawList = &cache;
    for (int i : DrawOrder(cache)) DrawElem(dl, cache[i], true);
    g_drawList = nullptr;
    float sc = std::min((box.x - 16) / (hi.x - lo.x), (box.y - 16) / (hi.y - lo.y));
    ImVec2 off(p0.x + (box.x - (hi.x - lo.x) * sc) / 2, p0.y + (box.y - (hi.y - lo.y) * sc) / 2);
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) { ImVec2& v = dl->VtxBuffer[i].pos; v = ImVec2(off.x + (v.x - lo.x) * sc, off.y + (v.y - lo.y) * sc); }
    dl->PopClipRect();
}
static std::string LinkKey(const LinkSet& s) { return s.name.empty() ? "@" + (s.m.empty() ? std::string() : s.m[0].group) : s.name; }
static std::string LinkLabel(const LinkSet& s) { return s.name.empty() ? (s.m.empty() ? std::string("?") : s.m[0].group) : s.name; }
// another linked copy: a new group (named after the link) on the menu being edited, where the first copy is
static void LinkAddCopy(LinkSet& s) {
    if (s.mode != g_editMode || s.m.empty()) return;
    auto& els = g_layout[s.mode];
    if (!s.refOk) LinkRebuild(s, 0);
    std::string leaf = CleanGroup(s.name.empty() ? GroupLeaf(s.m[0].group) : s.name);
    if (leaf.empty()) leaf = "Linked";
    auto names = GroupNames();
    std::string ng = IntoMenu(leaf);
    for (int k = 2; std::find(names.begin(), names.end(), ng) != names.end() || PageOf(ng); ++k) ng = IntoMenu(leaf + " " + std::to_string(k));
    LinkMember m = s.m[0]; m.group = ng;
    for (auto& p : s.ref) els.push_back(LinkAbs(p.e, m));
    MenuCopy(s.m[0].group, ng);   // (a grid / carousel inside: its own settings, copied once)
    s.m.push_back(m);
    g_sel = -1; ClearMulti(); g_selGroup = ng;
    g_dirty = true; g_linkGen = g_dirty.gen;
    Log("ui editor: linked copy %s of %s added", ng.c_str(), LinkLabel(s).c_str());
}
static void LinkedGroupsPage() {
    if (!ImGui::CollapsingHeader("Linked groups")) return;
    static std::string pick; static std::vector<Elem> cache;
    ImGui::TextDisabled("Select a group, then \"Save as linked group...\" (next to Link...) to list it here.");
    std::vector<int> list;   // (this game mode's: named first, by name; then the unnamed links)
    for (int i = 0; i < (int)g_links.size(); ++i) if (g_links[i].mode == g_editMode) list.push_back(i);
    std::stable_sort(list.begin(), list.end(), [](int a, int b) {
        const LinkSet &x = g_links[a], &y = g_links[b];
        if (x.name.empty() != y.name.empty()) return !x.name.empty();
        return x.name < y.name;
    });
    float h = 230;
    LinkSet* cur = nullptr;
    ImGui::BeginChild("##lglist", ImVec2(230, h), ImGuiChildFlags_Border);
    if (list.empty()) ImGui::TextDisabled("(no linked groups yet)");
    int addOf = -1;
    for (int i : list) {
        LinkSet& s = g_links[i];
        std::string key = LinkKey(s);
        std::string label = LinkLabel(s) + "  (x" + std::to_string(s.m.size()) + ")";
        if (s.name.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Selectable((label + "##lg" + key).c_str(), pick == key, ImGuiSelectableFlags_AllowDoubleClick)) { pick = key; if (ImGui::IsMouseDoubleClicked(0)) addOf = i; }
        if (s.name.empty()) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%sDouble-click: add another linked copy (on the menu being edited).", s.name.empty() ? "Linked, not saved under a name.\n" : "");
    }
    ImGui::EndChild();
    if (addOf >= 0) LinkAddCopy(g_links[addOf]);
    for (int i : list) if (LinkKey(g_links[i]) == pick) cur = &g_links[i];
    ImGui::SameLine();
    ImGui::BeginGroup();
    cache.clear();
    if (cur && !cur->m.empty()) for (auto& e : g_layout[cur->mode]) if (InGroup(e.group, cur->m[0].group)) cache.push_back(e);
    ElemsPreview(cache, ImVec2(std::max(200.f, ImGui::GetContentRegionAvail().x), h - ImGui::GetFrameHeightWithSpacing()), cur ? "(empty)" : "Pick a linked group to see it here");
    ImGui::BeginDisabled(!cur);
    if (ImGui::Button("Add##lg") && cur) LinkAddCopy(*cur);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Another linked copy, as a new group on the menu being edited (where the first copy is on its menu).");
    ImGui::SameLine();
    if (ImGui::Button("Unlink##lg")) ImGui::OpenPopup("Unlink linked group?");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Every copy keeps its pieces as they are now, on its own from then on.");
    ImGui::EndDisabled();
    if (cur) { ImGui::SameLine(); ImGui::TextDisabled("%d cop%s - %d elements", (int)cur->m.size(), cur->m.size() == 1 ? "y" : "ies", (int)cache.size()); }
    if (ImGui::BeginPopupModal("Unlink linked group?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Unlink \"%s\"?", pick.c_str()[0] == '@' ? pick.c_str() + 1 : pick.c_str());
        ImGui::TextDisabled("Its copies stay, each with its pieces as they are; edits stop going between them.");
        LinkSet* s = nullptr; for (auto& x : g_links) if (x.mode == g_editMode && LinkKey(x) == pick) s = &x;
        if (ImGui::Button("Unlink them") && s) {
            Log("ui editor: linked group %s unlinked (%d copies)", LinkLabel(*s).c_str(), (int)s->m.size());
            g_links.erase(g_links.begin() + (s - g_links.data()));
            g_dirty = true; g_linkGen = g_dirty.gen;
            pick.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (cur && !cur->m.empty()) {   // its copies: click one to go to it
        ImGui::TextDisabled("copies:");
        std::string go;
        for (auto& m : cur->m) { ImGui::SameLine(); if (ImGui::SmallButton((m.group + "##lgc").c_str())) go = m.group; }
        if (!go.empty()) LinkGoTo(go);
    }
    ImGui::EndGroup();
}
