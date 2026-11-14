// The element panel (one element picked, or several): folding sections - Main, Transform, the type's own (Vectors /
// Text / Image), Visibility, Value, Animation, Menu, Copy to similar names. Several picked: one panel; values that
// differ show "--" (typing one sets them all); the type's sections show when they're all one type - the first one's
// values, any change goes to all of them. (Included in uied.cpp after CopyToSimilar.)

// ---- the type's own sections
static void PVectors(Elem& e) {
    const float full = PFull() - 11, fh = ImGui::GetFrameHeight();
    const float maxR = std::min(e.w, e.h) / 2;
    PIndent();
    if (ImGui::Checkbox("same", &e.radLink)) { if (e.radLink) e.rad[1] = e.rad[2] = e.rad[3] = e.rad[0]; g_dirty = true; }
    PTip("The four corners rounded alike (off: each its own - top-left, top-right, bottom-right, bottom-left).");
    const float sameW = ImGui::GetItemRectSize().x;
    ImGui::SameLine(0, 6);
    const float pillW = 40.f, radW = full - sameW - 6 - 4 - pillW;
    if (e.radLink) { ImGui::SetNextItemWidth(radW); if (ImGui::DragFloat("##rad", &e.rad[0], 0.25f, 0.f, maxR, "%.1f")) { e.rad[1] = e.rad[2] = e.rad[3] = e.rad[0]; g_dirty = true; } }
    else { ImGui::SetNextItemWidth(radW); if (ImGui::DragFloat4("##rad4", e.rad, 0.25f, 0.f, maxR, "%.1f")) g_dirty = true; }
    PTip("Rounding of the corners (units; half the short side makes a pill / circle). Ctrl + click to type.");
    ImGui::SameLine(0, 4); if (ImGui::Button("pill", ImVec2(pillW, 0))) { for (float& r : e.rad) r = maxR; e.radLink = true; g_dirty = true; }
    PLabel("Corners");
    {   // the border: its thickness, where it sits on the edge
        PIndent();
        if (ImGui::Button("-##bt")) { e.stroke = std::max(0.f, roundf(e.stroke * 2 - 1) / 2); g_dirty = true; }
        ImGui::SameLine(0, 3); ImGui::SetNextItemWidth(44);
        if (ImGui::DragFloat("##stroke", &e.stroke, 0.05f, 0.f, 200.f, "%.1f")) { e.stroke = std::max(0.f, e.stroke); g_dirty = true; }
        PTip("The border's thickness (units; 0 = no border). Its colour: the border colour below (or its gradient).");
        ImGui::SameLine(0, 3); if (ImGui::Button("+##bt")) { e.stroke = roundf(e.stroke * 2 + 1) / 2; g_dirty = true; }
        static const char* aligns[] = {"inside", "centred", "outside"};
        const float used = 44 + 2 * (ImGui::GetItemRectSize().x) + 6 + 6;
        ImGui::SameLine(0, 6); ImGui::SetNextItemWidth(std::max(60.f, full - used));
        if (ImGui::Combo("##edge", &e.strokeAlign, aligns, 3)) g_dirty = true;
        PTip("Where the border sits: inside the edge (the shape keeps its size), centred on it, or outside it.");
        PLabel("Border");
    }
    PColour("Border colour", e.col, e.colTok, e.colTokA);
    PColour("Fill", e.fillCol, e.fillTok, e.fillTokA);
    {
        static const char* sm[] = {"none", "across", "along"};
        PIndent();
        const bool ang = e.strokeGrad.mode == 2;
        ImGui::SetNextItemWidth(ang ? 90.f : full);
        if (ImGui::Combo("##sgm", &e.strokeGrad.mode, sm, 3)) g_dirty = true;
        PTip("Across: from the border's outer side to its inner side, following the curve of the corners.\nAlong: around the outline, clockwise from the top-left (the start turns where it begins).");
        if (ang) { ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(full - 94); if (ImGui::DragFloat("##sga", &e.strokeGrad.angle, 0.5f, -360.f, 360.f, "start %.0f deg")) g_dirty = true; }
        PLabel("Border gradient");
        if (e.strokeGrad.mode) { PIndent(); ImGui::BeginGroup(); GradientEdit("sgrad", e.strokeGrad, e.col, e.colTok.empty() ? nullptr : e.colTok.c_str()); ImGui::EndGroup(); }
    }
    {
        static const char* fm[] = {"none", "linear", "radial", "from the edge"};
        PIndent();
        const bool more = e.fillGrad.mode == 1 || e.fillGrad.mode == 3;
        ImGui::SetNextItemWidth(more ? 90.f : full);
        if (ImGui::Combo("##fgm", &e.fillGrad.mode, fm, 4)) { if (e.fillGrad.mode == 3 && e.fillGrad.depth <= 0) e.fillGrad.depth = 20; g_dirty = true; }
        PTip("Linear: across the shape at an angle. Radial: centre to edge.\nFrom the edge: inward from the edge over a depth, following the curve (an inner glow / bevel).\nThe fill's opacity still applies.");
        if (e.fillGrad.mode == 1) { ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(full - 94); if (ImGui::DragFloat("##fga", &e.fillGrad.angle, 0.5f, -360.f, 360.f, "%.0f deg")) g_dirty = true; }
        if (e.fillGrad.mode == 3) { ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(full - 94); if (ImGui::DragFloat("##fgd", &e.fillGrad.depth, 0.1f, 0.5f, 500.f, "depth %.1f")) g_dirty = true; }
        PLabel("Fill gradient");
        if (e.fillGrad.mode) { float fc[4] = {e.fillCol[0], e.fillCol[1], e.fillCol[2], 1}; PIndent(); ImGui::BeginGroup(); GradientEdit("fgrad", e.fillGrad, fc, e.fillTok.empty() ? nullptr : e.fillTok.c_str()); ImGui::EndGroup(); }
    }
    {   // cut / merge
        int m = e.cut ? 1 : e.merge ? 2 : 0;
        PIndent();
        bool ch = ImGui::RadioButton("none##cm", &m, 0); ImGui::SameLine();
        ch |= ImGui::RadioButton("cut", &m, 1);
        PTip("A boolean: this shape is cut out of another shape (a notch, a window, a panel or button sitting\nin a frame) and drawn itself over the hole, with its own fill and border - its border outlines the cut.\nFor an empty cut, set its fill and border opacity to 0. Cutters keep a pink outline while editing.");
        ImGui::SameLine(); ch |= ImGui::RadioButton("merge", &m, 2);
        PTip("A boolean: this shape is merged into another shape (a union) - it draws nothing itself; the shape it\nmerges into takes in its outline, so they're one: one fill, one border around them all, the gradients\nacross the whole. Several can merge into the same shape. Merged shapes keep a dashed cyan outline while editing.");
        if (ch) { e.cut = m == 1; e.merge = m == 2; g_dirty = true; }
        PLabel("Cut / merge");
        ImGui::BeginDisabled(!e.cut && !e.merge);
        PIndent(); ImGui::SetNextItemWidth(full - 64 - 4);
        const bool grpT = e.cutTarget.compare(0, 6, "group:") == 0;
        std::string shown = std::string(e.merge ? "into: " : "cuts: ") + (e.cutTarget.empty() ? "every shape in its group" : grpT ? "group " + RelGroup(e.cutTarget.substr(6)) : e.cutTarget);
        if (ImGui::BeginCombo("##cuttgt", shown.c_str())) {
            if (ImGui::Selectable("every shape in its group", e.cutTarget.empty())) { e.cutTarget.clear(); g_dirty = true; }
            ImGui::SeparatorText("a whole group");
            for (auto& n : GroupNames()) {
                if (n == e.group || MenuEdOff(n) || RelGroup(n).empty()) continue;
                std::string lb = "group: " + RelGroup(n) + "##cg" + n;
                if (ImGui::Selectable(lb.c_str(), e.cutTarget == "group:" + n)) { e.cutTarget = "group:" + n; g_dirty = true; }
            }
            ImGui::SeparatorText("one shape");
            for (auto& o : Elems()) if (o.type == E_SHAPE && !o.cut && !o.merge && &o != &e && !MenuEdOff(o.group) && ImGui::Selectable((o.name + "##one" + std::to_string((size_t)&o)).c_str(), e.cutTarget == o.name)) { e.cutTarget = o.name; g_dirty = true; }
            ImGui::EndCombo();
        }
        PTip(e.merge ? "What it merges into: every shape in its own group, every shape in another group (and the groups inside it),\nor one shape by name (usually: one shape)."
                     : "What it cuts: every shape in its own group, every shape in another group (and the groups inside it),\nor one shape by name. Only shapes on its layer or lower: a shape on a higher layer sits over the cutter and isn't cut.");
        ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(64);
        if (ImGui::DragFloat("##gap", &e.cutGap, 0.05f, -200.f, 200.f, "gap %.1f")) { e.cutGap = std::max(-200.f, std::min(200.f, e.cutGap)); g_dirty = true; }
        PTip(e.merge ? "Merged in this much bigger than the shape itself (units; negative: smaller). Ctrl + click to type."
                     : "Cuts this much wider than the cutter itself (units): room around what sits in the cut-out.\nNegative: the cut is that much smaller than the cutter. Ctrl + click to type.");
        ImGui::EndDisabled();
        int cutters = 0, mergers = 0;
        if (!e.cut && !e.merge) for (auto& o : Elems()) if (o.type == E_SHAPE && &o != &e && (o.cut || o.merge) && CutHits(o, e)) ++(o.cut ? cutters : mergers);
        if (cutters || mergers) {
            PIndent();
            if (cutters) ImGui::TextDisabled("%d cutter%s on it", cutters, cutters == 1 ? "" : "s");
            if (cutters && mergers) ImGui::SameLine();
            if (mergers) ImGui::TextDisabled("%d shape%s merged in", mergers, mergers == 1 ? "" : "s");
        }
    }
    PHex("##follow", e.follow, full, "Follows game object",
         "A game FEng object's name hash (from the dump): this shape takes where the game puts it and its size every\nframe (its x / y: an offset from there), its fill tinted by the game's colour; hidden while the game hides it.");
}
static void PTextSection(Elem& e) {
    const float full = PFull() - 11;
    static char tb[512];
    snprintf(tb, sizeof tb, "%s", e.text.c_str());
    PIndent();
    if (ImGui::InputTextMultiline("##text", tb, sizeof tb, ImVec2(full, ImGui::GetTextLineHeight() * 3 + 8))) { e.text = tb; g_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kTokenHelp);
    PLabel("Text");
    std::vector<const char*> fn; int cur = -1;
    for (size_t i = 0; i < g_fonts.size(); ++i) { fn.push_back(g_fonts[i].name.c_str()); if (g_fonts[i].name == e.font) cur = (int)i; }
    PIndent(); ImGui::SetNextItemWidth(full);
    if (ImGui::Combo("##font", &cur, fn.data(), (int)fn.size())) { e.font = g_fonts[cur].name; e.fontIdx = cur; g_dirty = true; }
    PLabel("Font");
    PIndent(); ImGui::SetNextItemWidth(64);
    if (ImGui::DragFloat("##scale", &e.scale, 0.01f, 0.1f, 8.f, "%.2f")) g_dirty = true;
    PTip("Its size (x1: the font's own).");
    static const char* al[] = { "left", "centre", "right" };
    ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(full - 68);
    if (ImGui::Combo("##align", &e.align, al, 3)) g_dirty = true;
    PTip("Where its text sits about its position (centre / right: in its box width - Size's W).");
    PLabel("Scale / align");
    PColour("Colour", e.col, e.colTok, e.colTokA);
    PIndent();
    if (ImGui::Checkbox("wrap", &e.wrap)) g_dirty = true;
    PTip("Break the text into lines to fit the box width (Size's W) - a long message from the game.");
    ImGui::SameLine(); if (ImGui::Checkbox("animate changes", &e.swapAnim)) g_dirty = true;
    PTip("When its text changes, the old text shrinks and fades out while the new one grows and fades in\n(0.32 s) - the game's help bar swapping the profile name and the cash ({bar.ticker}).");
}
static void PImage(Elem& e) {
    const float full = PFull() - 11;
    PIndent();
    {   // its picture
        const float th = ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.y;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(th, th));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cs = th / 4;
        for (int yy = 0; yy < 4; ++yy) for (int xx = 0; xx < 4; ++xx)
            dl->AddRectFilled(ImVec2(p.x + xx * cs, p.y + yy * cs), ImVec2(p.x + (xx + 1) * cs, p.y + (yy + 1) * cs), ((xx + yy) & 1) ? IM_COL32(90, 90, 90, 255) : IM_COL32(60, 60, 60, 255));
        float su = 1, sv = 1;
        gfx::Tex tx = e.tex.empty() ? nullptr : NamedTex(e.tex, (int)th, su, sv);
        if (tx) {
            float w = 1, h = 1;
            if (vecart::IsRef(e.tex)) { if (const vecart::Art* art = vecart::Find(vecart::RefName(e.tex))) { w = art->w > 0 ? art->w : 1; h = art->h > 0 ? art->h : 1; } }
            else { int i = FindTex(e.tex, ""); if (i >= 0) { w = (float)g_tex[i].w; h = (float)g_tex[i].h; } }
            const float sc = (th - 4) / std::max(w, h); const ImVec2 isz(w * sc, h * sc), off((th - isz.x) / 2, (th - isz.y) / 2);
            dl->AddImage(gfx::Id(tx), ImVec2(p.x + off.x, p.y + off.y), ImVec2(p.x + off.x + isz.x, p.y + off.y + isz.y), ImVec2(0, 0), ImVec2(su, sv));
        }
    }
    ImGui::SameLine(0, 6); ImGui::BeginGroup();
    {
        int ifam, iid;
        std::string what = e.tex.empty() ? "(none - pick one in Textures)" : e.tex;
        ImGui::SetNextItemWidth(full - ImGui::GetFrameHeight() * 2 - ImGui::GetStyle().ItemSpacing.y - 6);
        if (ImGui::BeginCombo("##tex", what.c_str())) {
            if (g_texSel >= 0 && ImGui::Selectable(("use the selected texture: " + g_tex[g_texSel].name).c_str())) { e.tex = g_tex[g_texSel].name; e.pack = g_tex[g_texSel].pack; e.texIdx = g_texSel; g_dirty = true; }
            if (g_texSel < 0) ImGui::TextDisabled("Pick a texture in Textures, then use it here.");
            ImGui::EndCombo();
        }
        if (IconRef(e.tex, ifam, iid)) PTip(("button icon: " + IconName(ifam, iid)).c_str());
        else if (vecart::IsRef(e.tex)) PTip(("vector art: " + vecart::RefName(e.tex) + (vecart::Find(vecart::RefName(e.tex)) ? "" : " (file missing)")).c_str());
        else if (!e.pack.empty()) PTip(("pack: " + e.pack).c_str());
        if (ImGui::Button("own size")) {
            const vecart::Art* art = vecart::IsRef(e.tex) ? vecart::Find(vecart::RefName(e.tex)) : nullptr;
            if (art && art->w > 0 && art->h > 0) { e.w = art->w; e.h = art->h; g_dirty = true; }
            else if (e.texIdx >= 0 && e.texIdx < (int)g_tex.size()) { e.w = g_tex[e.texIdx].w; e.h = g_tex[e.texIdx].h; g_dirty = true; }
        }
        PTip("The picture's own size (vector art: its own; button icons: 32 x 32 is the stock size).");
        ImGui::SameLine(0, 3); if (ImGui::Button("flip H")) { std::swap(e.u0, e.u1); g_dirty = true; }
        ImGui::SameLine(0, 3); if (ImGui::Button("flip V")) { std::swap(e.v0, e.v1); g_dirty = true; }
        ImGui::SameLine(0, 3); if (ImGui::Button("reset UV")) { e.u0 = e.v0 = 0; e.u1 = e.v1 = 1; g_dirty = true; }
    }
    ImGui::EndGroup();
    PLabel("Texture");
    {
        int ifam, iid;
        if (IconRef(e.tex, ifam, iid)) {
            PIndent(); bool follow = ifam < 0;
            if (ImGui::Checkbox("follows the controller", &follow)) { std::string fam = follow ? "auto" : iconFamilies.empty() ? "xbox" : iconFamilies[0]; e.tex = "icon:" + fam + ":" + iconKeys[iid]; g_dirty = true; }
            ImGui::SameLine(); if (ImGui::SmallButton("icon size (32)")) { e.w = e.h = 32; g_dirty = true; }
        } else if (vecart::IsRef(e.tex)) {
            const vecart::Art* art = vecart::Find(vecart::RefName(e.tex));
            if (art && art->w > 0 && art->h > 0) { PIndent(); if (ImGui::SmallButton("fix its proportions")) { e.h = e.w * art->h / art->w; g_dirty = true; } }
        }
    }
    PIndent(); ImGui::SetNextItemWidth(full); if (ImGui::DragFloat4("##uv", &e.u0, 0.002f, -1.f, 2.f, "%.3f")) g_dirty = true;
    PTip("The part of the texture shown: u0 v0 u1 v1 (0..1 across / down).");
    PLabel("UV");
    PColour("Colour", e.col, e.colTok, e.colTokA);
    {
        char buf[128]; snprintf(buf, sizeof buf, "%s", e.clipTo.c_str());
        PIndent(); ImGui::SetNextItemWidth(full);
        if (ImGui::InputTextWithHint("##clipto", "element name (nothing: not clipped)", buf, sizeof buf)) { e.clipTo = buf; g_dirty = true; }
        PTip("Drawn only inside that element (by name): its box, or the part of it below, turned with it.\nAs the game's masks: a sheen sweeping inside a bar.");
        PLabel("Clip to");
        if (!e.clipTo.empty()) { PIndent(); ImGui::SetNextItemWidth(full); if (ImGui::DragFloat4("##clipbox", e.clipBox, 0.002f, -1.f, 2.f, "%.3f")) g_dirty = true; PLabel("Clip box (of it)"); }
    }
    PIndent();
    if (ImGui::Checkbox("circle", &e.circle)) g_dirty = true;
    PTip("Clipped to the circle inside its box.");
    ImGui::SameLine(); if (ImGui::Checkbox("glow (additive)", &e.additive)) g_dirty = true;
    PTip("Added onto what's under it (glows; black = nothing).");
    PHex("##follow", e.follow, full, "Follows game object",
         "A stock HUD object's FEng name hash: this image copies where the game puts it, its size, turn,\nuv and whether it's shown, every frame (position / rotation here are added; texture: its own unless set).");
    PIndent();
    if (ImGui::Checkbox("minimap", &e.minimap)) { if (e.minimap) e.worldMap = false; g_dirty = true; }
    PTip("Drawn as the game's minimap: the track map around your car (round, in this box), turned\nheading-up when the game's radar option is on, with the game's icons and other mods' markers.\nIts texture and UV aren't used. Flag it \"replace: minimap\" to hide the stock one.");
    ImGui::SameLine(); if (ImGui::Checkbox("world map", &e.worldMap)) { if (e.worldMap) e.minimap = false; g_dirty = true; }
    PTip("Drawn as the world map: the whole city map in this box (your Minimap page art), the game's icons and\nother mods' markers; with UniPad's World Map page: its view, the selected icon's ring and the hidden types.");
    if (e.minimap || e.worldMap) {
        PIndent();
        if (e.minimap) { ImGui::SetNextItemWidth(80); if (ImGui::DragFloat("##mz", &e.mapZoom, 0.01f, 0.1f, 10.f, "zoom x%.2f")) g_dirty = true; PTip("Times the game's own zoom."); ImGui::SameLine(0, 4); }
        ImGui::SetNextItemWidth(80); if (ImGui::DragFloat("##mb", &e.mapBlip, 0.01f, 0.1f, 10.f, "icons x%.2f")) g_dirty = true;
        PTip("Icon size (x1: as big as the stock map's).");
        ImGui::SameLine(); if (ImGui::Checkbox("your car's icon", &e.mapPlayer)) g_dirty = true;
    }
}

// ---- shared by every type
static void PVisibility(Elem& e) {
    const float full = PFull() - 11;
    PText("##showif", e.showIf, "value", full, "Show only while", "A live value's key (no braces). Shown only while it isn't blank or 0.\n!key: only while it is. Empty: always.");
    PText("##tint", e.tint, "value giving R G B", full, "Tint from", "A live value giving \"R G B\" (0-255) that multiplies the colour - the car's custom HUD paints.");
    PIndent(); ImGui::SetNextItemWidth(full);
    if (ImGui::InputInt("##row", &e.row, 0, 0)) { e.row = std::max(0, std::min(22, e.row)); g_dirty = true; }
    PTip("0 = not a leaderboard row.\nN = shown only while there's an Nth racer on the board; in its text {name} {pos} {gap} ... are that racer's.");
    PLabel("Leaderboard row");
    if (e.row > 0) {
        PIndent(); if (ImGui::Checkbox("other colour on my row", &e.hiMine)) g_dirty = true;
        if (e.hiMine) PColour("My-row colour", e.hiCol, e.hiTok, e.hiTokA);
    }
}
static void PValue(Elem& e) {
    const float full = PFull() - 11;
    static const char* kinds[] = { "nothing", "fill left to right", "fill bottom to top", "rotation", "reveal (turning half)", "move across", "move up" };
    PIndent(); ImGui::SetNextItemWidth(full);
    if (ImGui::Combo("##vd", &e.bindKind, kinds, 7)) g_dirty = true;
    PTip("Fill: drawn as long as its value (images: cut, not squashed). Rotation / reveal: images. Move across / up:\nany element, from / to move in units - a meter's handle riding its fill.");
    PLabel("Value drives");
    if ((e.type == E_SHAPE && (e.bindKind == 3 || e.bindKind == 4)) || (e.type == E_TEXT && e.bindKind >= 1 && e.bindKind <= 4)) { ImGui::SameLine(); ImGui::TextDisabled("(images only)"); }
    if (!e.bindKind) return;
    PText("##bind", e.bind, "value key, e.g. rpm_pct", full, "Value", nullptr);
    const float half = (full - 4) / 2;
    auto two = [&](const char* id, float* v, float speed, float lo, float hi, const char* fmt, const char* label, const char* tip) {
        ImGui::PushID(id);
        PIndent(); ImGui::SetNextItemWidth(half); if (ImGui::DragFloat("##0", &v[0], speed, lo, hi, fmt)) g_dirty = true; if (tip) PTip(tip);
        ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(half); if (ImGui::DragFloat("##1", &v[1], speed, lo, hi, fmt)) g_dirty = true; if (tip) PTip(tip);
        PLabel(label);
        ImGui::PopID();
    };
    two("ft", &e.bindMin, 1.f, -100000.f, 100000.f, "%g", "From / to value", "The value at its start and at its end.");
    if (e.bindKind == 3 || e.bindKind == 4) two("ang", &e.rotMin, 0.5f, -720.f, 720.f, "%.0f deg", "From / to angle", nullptr);
    if (e.bindKind == 5 || e.bindKind == 6) two("mv", &e.rotMin, 0.5f, -2000.f, 2000.f, "%.1f", "From / to move", "Units it moves (up is positive for \"move up\").");
    if (e.bindKind == 1 || e.bindKind == 2) two("ef", &e.cutLo, 0.002f, 0.f, 1.f, "%.3f", "Empty / full at", "Where in the picture an empty and a full value land (0..1 along the fill).\nFor art whose bar doesn't reach its edges.");
    const float f = BindFrac(e);
    PIndent(); if (f >= 0) ImGui::TextDisabled("now: %.0f%%", f * 100); else ImGui::TextDisabled("now: no value");
}
static void PAnimation(Elem& e) {
    const float full = PFull() - 11;
    static const char* an[] = { "still", "pulse", "blink", "keyframes", "bounce" };
    PIndent(); ImGui::SetNextItemWidth(full);
    if (ImGui::Combo("##anim", &e.anim, an, 5)) g_dirty = true;
    PTip("Pulse: grows and fades (~1 s). Blink: on / off. Keyframes: your own keys. Bounce: grows and back once.\nAnimation mode (F8) has clips, curves and effects of its own.");
    PLabel("Animation");
    if (e.anim == 4) {   // bounce
        const float q = (full - 12) / 4;
        PIndent();
        ImGui::SetNextItemWidth(q); if (ImGui::DragFloat("##bsize", &e.bounceScale, 0.005f, 0.2f, 4.f, "x%.2f")) g_dirty = true;
        PTip("How big it gets at the top of the bounce (x1.20: a fifth bigger; under 1: it dips).");
        ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(q); if (ImGui::DragFloat("##bms", &e.bounceMs, 5.f, 20.f, 5000.f, "%.0f ms")) g_dirty = true;
        PTip("How long the bounce takes, up and back down.");
        ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(q); if (ImGui::DragFloat("##bglow", &e.bounceGlow, 0.005f, 0.f, 1.f, "glow %.2f")) g_dirty = true;
        PTip("How much brighter it is at the top than at its ends (0: no change). The stock menus' arrows: x1.20, 300 ms, glow 0.18.");
        ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(q); if (ImGui::DragFloat("##balpha", &e.bounceAlpha, 0.01f, 0.f, 4.f, "a x%.2f")) g_dirty = true;
        PTip("Its opacity at the top of the bounce, times this. The map's LT / RT: x1.30, 400 ms, glow 0, opacity x1.70.");
        PLabel("Bounce");
    }
    if (e.anim == 3) {
        PText("##keys", e.keys, "800;0 1 1 1;400 1.6 1.7 1;800 1 1 1", full, "Keys",
              "Loop length in ms (! in front: play once and hold), then keys \"time width-scale height-scale alpha [move-x move-y [red green blue]]\" separated by ;\n(scales and colours multiply, moves are units; straight lines between keys). Text scales from its alignment point.");
    }
    if (e.anim) PText("##playon", e.playOn, "value, e.g. bindheld.FENG_LTRIGGER", full, "Play on",
                      "A live value: the animation starts over each time it comes on (blank: from when the element shows).\nbindheld.<event>: while that UniPad binding is held - a button's press animation.");
}

// the fields a section changes (several picked: what changed on the stand-in goes to every one of them)
static bool SameGrad(const UGrad& a, const UGrad& b) { return a.mode == b.mode && a.angle == b.angle && a.depth == b.depth && a.tok == b.tok && SameStops(a.stops, b.stops); }
static void PApplyChanged(const Elem& was, const Elem& now, const std::vector<Elem*>& P) {
#define PF(f) if (!(was.f == now.f)) for (Elem* o : P) o->f = now.f;
#define PA(f) if (memcmp(was.f, now.f, sizeof was.f) != 0) for (Elem* o : P) memcpy(o->f, now.f, sizeof o->f);
#define PG(f) if (!SameGrad(was.f, now.f)) for (Elem* o : P) o->f = now.f;
    PA(rad) PF(radLink) PF(stroke) PF(strokeAlign) PA(col) PF(colTok) PF(colTokA) PA(fillCol) PF(fillTok) PF(fillTokA) PG(strokeGrad) PG(fillGrad)
    PF(cut) PF(merge) PF(cutTarget) PF(cutGap) PF(follow)
    PF(text) PF(font) PF(fontIdx) PF(scale) PF(align) PF(wrap) PF(swapAnim)
    PF(tex) PF(pack) PF(texIdx) PF(u0) PF(v0) PF(u1) PF(v1) PF(clipTo) PA(clipBox) PF(circle) PF(additive) PF(minimap) PF(worldMap) PF(mapZoom) PF(mapBlip) PF(mapPlayer)
    PF(showIf) PF(tint) PF(row) PF(hiMine) PA(hiCol) PF(hiTok) PF(hiTokA)
    PF(bindKind) PF(bind) PF(bindMin) PF(bindMax) PF(rotMin) PF(rotMax) PF(cutLo) PF(cutHi)
    PF(anim) PF(bounceScale) PF(bounceMs) PF(bounceGlow) PF(bounceAlpha) PF(keys) PF(playOn)
    PF(role) PF(selOnly) PF(tileKinds) PF(tileParity) PF(afterLabel)
#undef PF
#undef PA
#undef PG
}

// the panel. P: the element(s); several: groups picked go along as groups (move to / duplicate)
static void ElemPanel(const std::vector<Elem*>& P) {
    if (P.empty()) return;
    const bool multi = P.size() > 1;
    Elem& e0 = *P[0];
    const float full = PFull() - 11;
    enum { A_NONE, A_DUP, A_DEL } act = A_NONE;
    ImGui::PushID(multi ? (const void*)&g_multi : (const void*)&e0);
    if (multi) {
        ImGui::TextDisabled("%d picked", (int)PickCount());
        if (!g_multiG.empty()) { ImGui::SameLine(); ImGui::TextDisabled("(%d group%s, %d elements in all)", (int)g_multiG.size(), g_multiG.size() == 1 ? "" : "s", (int)P.size()); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl + click: add / remove one (element or group), Shift + click: everything between.\nDrag any of them on screen to move them all.");
        ImGui::SameLine(); if (ImGui::SmallButton("clear")) { ClearMulti(); ImGui::PopID(); return; }
    }
    // ---- Main
    if (PSection("Main")) {
        PIndent(); ImGui::SetNextItemWidth(170);
        if (multi) { char nb[8] = ""; ImGui::BeginDisabled(); ImGui::InputTextWithHint("##name", ("Name (" + std::to_string(P.size()) + " picked)").c_str(), nb, sizeof nb); ImGui::EndDisabled(); }
        else { char nb[64]; snprintf(nb, sizeof nb, "%s", e0.name.c_str()); if (ImGui::InputTextWithHint("##name", "Name", nb, sizeof nb)) { e0.name = nb; g_dirty = true; } }
        ImGui::SameLine();
        { char pb[8] = ""; ImGui::SetNextItemWidth(170); ImGui::BeginDisabled(); ImGui::InputTextWithHint("##ph", "(to come)", pb, sizeof pb); ImGui::EndDisabled(); }
        // its group: typed (Enter), or moved into one; Select group picks the whole group
        std::string grp = e0.group; bool oneGroup = true; for (Elem* o : P) oneGroup &= o->group == grp;
        if (!oneGroup) grp.clear();
        {
            static char gb[64]; static std::string gbWas; static const void* gbFor = nullptr;
            const void* key = multi ? (const void*)&g_multi : (const void*)&e0;
            if (gbFor != key || gbWas != grp) { snprintf(gb, sizeof gb, "%s", RelGroup(grp).c_str()); gbFor = key; gbWas = grp; }
            PIndent(); ImGui::SetNextItemWidth(170);
            if (ImGui::InputTextWithHint("##group", oneGroup ? "Group Name" : "Group Name (mixed)", gb, sizeof gb, ImGuiInputTextFlags_EnterReturnsTrue)) {
                const std::string to = IntoMenu(CleanGroup(gb));
                if (multi) PutPickedIn(to); else { e0.group = to; g_dirty = true; }
                gbWas = to;
            }
            PTip("Its group: type a name and press Enter (a group inside a larger one: Larger/Smaller, e.g. Race HUD/Leaderboard).");
        }
        ImGui::SameLine(); ImGui::SetNextItemWidth(170);
        if (ImGui::BeginCombo("##moveto", "move to...")) {
            if (ImGui::Selectable(MenuEdOff("") ? "(out of its groups)" : "(no group)")) { if (multi) PutPickedIn(IntoMenu("")); else { e0.group = IntoMenu(""); g_dirty = true; } }
            for (auto& n : GroupNames()) {
                if (MenuEdOff(n) || RelGroup(n).empty()) continue;
                bool own = false; for (auto& g : g_multiG) own |= InGroup(n, g);   // (picked groups: not into one of themselves)
                if (own) continue;
                if (ImGui::Selectable((RelGroup(n) + "##mv" + n).c_str(), oneGroup && n == grp)) { if (multi) PutPickedIn(n); else { e0.group = n; g_dirty = true; } }
            }
            ImGui::EndCombo();
        }
        PTip("Move it into a group (the same as typing the group's name).");
        ImGui::SameLine();
        ImGui::BeginDisabled(RelGroup(grp).empty());
        if (ImGui::Button("Select group")) { g_selGroup = grp; g_sel = -1; ClearMulti(); }
        ImGui::EndDisabled();
        // visible, layer, duplicate, delete
        PIndent();
        { bool all = true; for (Elem* o : P) all &= o->visible; if (ImGui::Checkbox("Visible", &all)) { for (Elem* o : P) o->visible = all; g_dirty = true; } }
        {
            int lo = P[0]->layer; for (Elem* o : P) lo = std::min(lo, o->layer);
            ImGui::SameLine(0, 14);
            if (ImGui::Button("-##layer")) { for (Elem* o : P) --o->layer; g_dirty = true; }
            ImGui::SameLine(0, 3); ImGui::SetNextItemWidth(40);
            int L = lo; if (ImGui::InputInt("##layer", &L, 0, 0) && L != lo) { for (Elem* o : P) o->layer += L - lo; g_dirty = true; }
            PTip(multi ? "The lowest of their layers: changing it moves them all, keeping their order. Higher layers are drawn on top."
                       : "Its layer: higher layers are drawn on top (the same layer: lower in the list on top).");
            ImGui::SameLine(0, 3); if (ImGui::Button("+##layer")) { for (Elem* o : P) ++o->layer; g_dirty = true; }
            ImGui::SameLine(0, 4); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("layer");
        }
        {
            const float h = ImGui::GetFrameHeight();
            ImGui::SameLine(0, 12);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##dup", ImVec2(h, h))) act = A_DUP;
            PTip("duplicate");
            ImDrawList* dl = ImGui::GetWindowDrawList(); const ImU32 c = ImGui::GetColorU32(ImGuiCol_Text);
            dl->AddRect(ImVec2(p.x + h * 0.25f, p.y + h * 0.2f), ImVec2(p.x + h * 0.65f, p.y + h * 0.6f), c);
            dl->AddRectFilled(ImVec2(p.x + h * 0.4f, p.y + h * 0.36f), ImVec2(p.x + h * 0.8f, p.y + h * 0.76f), c);
            ImGui::SameLine(0, 3);
            if (ImGui::Button("X##del", ImVec2(h, h))) act = A_DEL;
            PTip("delete");
        }
        PIndent(); FlagBoxes(P, multi ? "##multi" : "", false);
        ImGui::Spacing();
    }
    // ---- Transform
    if (PSection("Transform")) {
        {
            const int an = e0.anchor; bool mixed = false; for (Elem* o : P) mixed |= o->anchor != an;
            PIndent(); ImGui::Dummy(ImVec2(11, 1)); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(full - 11);
            if (ImGui::BeginCombo("##anchor", (std::string("Anchor Set: ") + (mixed ? "--" : kAnchors[an])).c_str())) {
                for (int a = 0; a < 9; ++a) if (ImGui::Selectable(kAnchors[a], !mixed && a == an)) { for (Elem* o : P) SetAnchorKeep(*o, a); g_dirty = true; }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); ImGui::TextDisabled("(?)");
            PTip("The screen corner / edge it keeps its distance to when the screen's shape changes. Changing it doesn't move it.");
        }
        PPair("X", PFields(P, [](Elem& o) -> float& { return o.x; }), "Y", PFields(P, [](Elem& o) -> float& { return o.y; }), "Position");
        bool anyText = false, allMotion = true; for (Elem* o : P) { anyText |= o->type == E_TEXT; allMotion &= o->type == E_MOTION; }
        if (!allMotion) {
            ImGui::PushID("size");
            ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("W"); ImGui::SameLine(0, 4); PNum("##w", PFields(P, [](Elem& o) -> float& { return o.w; }), kPBox);
            if (anyText) PTip("Texts: their box width (centre / right alignment, wrapping).");
            ImGui::SameLine(0, kPGap); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("H"); ImGui::SameLine(0, 4);
            std::vector<Elem*> nt; for (Elem* o : P) if (o->type != E_TEXT) nt.push_back(o);
            if (nt.empty()) { char hb[8] = ""; ImGui::SetNextItemWidth(kPBox); ImGui::BeginDisabled(); ImGui::InputTextWithHint("##h", "auto", hb, sizeof hb); ImGui::EndDisabled(); PTip("A text's height comes from its font and scale."); }
            else PNum("##h", PFields(nt, [](Elem& o) -> float& { return o.h; }), kPBox);
            PLabel("Size");
            ImGui::PopID();
            PPair("X", PFields(P, [](Elem& o) -> float& { return o.pivX; }), "Y", PFields(P, [](Elem& o) -> float& { return o.pivY; }), "Origin Point",
                  "The point it turns around, from its middle (units). A needle's hub.");
            ImGui::Dummy(ImVec2(11, 1)); ImGui::SameLine(0, 0);
            PNum("##rot", PFields(P, [](Elem& o) -> float& { return o.rot; }), full - 11, true, 0.5f, "%.1f", -360.f, 360.f);
            PTip("Degrees clockwise, about its origin point. Drag, or double-click to type.");
            PLabel("Rotation");
        }
        ImGui::Spacing();
    }
    if (!multi && e0.type == E_MOTION) {   // a group's motion: nothing else of its own
        PIndent(); ImGui::TextDisabled("Group motion: not drawn. Its clips (animation mode, F8) move, scale, turn, fade and tint");
        PIndent(); ImGui::TextDisabled("every element of its group \"%s\" (and the groups inside it) about this point.", e0.group.c_str());
        PText("##showif", e0.showIf, "value key (its In plays when it comes on)", full, "Show only while", nullptr);
    } else {
        // ---- the rest: on the element itself, or (several) on a stand-in whose changes go to them all
        bool oneType = true; for (Elem* o : P) oneType &= o->type == e0.type;
        static Elem stand;
        Elem was;
        Elem* ed = &e0;
        if (multi) { stand = e0; was = e0; ed = &stand; }
        Elem& e = *ed;
        if (oneType && e.type != E_MOTION) {
            const char* sec = e.type == E_SHAPE ? "Vectors" : e.type == E_TEXT ? "Text" : "Image";
            std::string shown = multi ? std::string(sec) + " (all " + std::to_string(P.size()) + " " + (e.type == E_SHAPE ? "shapes" : e.type == E_TEXT ? "texts" : "images") + ")" : sec;
            if (PSection(sec, shown.c_str())) {
                if (multi) { PIndent(); ImGui::TextDisabled("Shown: %s's. A change goes to all of them.", e0.name.c_str()); }
                if (e.type == E_SHAPE) PVectors(e); else if (e.type == E_TEXT) PTextSection(e); else PImage(e);
                ImGui::Spacing();
            }
        }
        if (PSection("Visibility")) { PVisibility(e); ImGui::Spacing(); }
        if (PSection("Value")) { PValue(e); ImGui::Spacing(); }
        if (PSection("Animation")) { PAnimation(e); ImGui::Spacing(); }
        if (MenuWs() && PSection("Menu")) { PIndent(); ImGui::BeginGroup(); ElemMenuProps(e); ImGui::EndGroup(); ImGui::Spacing(); }
        if (multi) PApplyChanged(was, stand, P);
        else if (PSection("Copy to similar names")) CopyToSimilar(e0);
    }
    ImGui::PopID();
    // (last: these change the list)
    if (act == A_DEL) DeleteSelected();
    if (act == A_DUP) {
        auto& els = Elems();
        if (!multi) {
            if (g_sel >= 0 && g_sel < (int)els.size()) { Elem c = els[g_sel]; c.name += " copy"; c.x += 10; c.y += 10; els.insert(els.begin() + g_sel + 1, c); ++g_sel; g_dirty = true; }
        } else {   // picked groups as groups ("<name> copy", widgets too), loose elements one by one
            std::vector<int> copies; std::vector<std::string> newGroups;
            auto names = GroupNames();
            for (auto& g : g_multiG) {
                std::string ng = g + " copy"; for (int k = 2; std::find(names.begin(), names.end(), ng) != names.end(); ++k) ng = g + " copy " + std::to_string(k);
                names.push_back(ng);
                std::vector<Elem> add; for (auto& o : els) if (InGroup(o.group, g)) { Elem c = o; c.group = ng + o.group.substr(g.size()); c.x += 10; c.y += 10; add.push_back(c); }
                for (auto& c : add) els.push_back(c);
                MenuCopy(g, ng); newGroups.push_back(ng);
            }
            for (int i : std::vector<int>(g_multi)) { Elem c = els[i]; c.x += 10; c.y += 10; c.name += " copy"; els.push_back(c); copies.push_back((int)els.size() - 1); }
            g_multi = copies; g_multiG = newGroups; g_sel = copies.empty() ? -1 : copies.back(); g_dirty = true;
        }
    }
}
static void Selected(Elem& e) { ElemPanel({ &e }); }
static void MultiPanel() {
    auto& els = Elems();
    g_multi.erase(std::remove_if(g_multi.begin(), g_multi.end(), [&](int i) { return i < 0 || i >= (int)els.size(); }), g_multi.end());
    { auto names = GroupNames(); g_multiG.erase(std::remove_if(g_multiG.begin(), g_multiG.end(), [&](const std::string& g) { return std::find(names.begin(), names.end(), g) == names.end(); }), g_multiG.end()); }
    std::vector<Elem*> P; for (int i : PickedElems()) P.push_back(&els[i]);
    if (P.empty()) return;
    ElemPanel(P);
}

// ---------------------------------------------------------------- the group panel (a group picked in the list)
// Main: its name (Enter renames), the group it's in (Enter / move to... / Select parent), visible, layer, duplicate /
// ungroup / delete / link icons, flags. Transform: anchor, position (its middle), rotation. Then its Widget / Menu
// page section (uied_menu.inc's GroupMenuProps).
static void GroupPanel() {
    const std::string g = g_selGroup;
    std::vector<int> mem; for (int i = 0; i < (int)Elems().size(); ++i) if (InGroup(Elems()[i].group, g)) mem.push_back(i);
    if (mem.empty()) { g_selGroup.clear(); return; }
    if (MenuEdOff("") && g == g_menuEd) { g_selGroup.clear(); return; }   // (a custom menu is the page, not a group)
    const std::string parent = GroupParent(g), leaf = GroupLeaf(g);
    const bool menuTop = MenuEdOff("") && parent == g_menuEd;   // (at the top of the custom menu being edited)
    const float full = PFull() - 11;
    std::vector<Elem*> P; for (int i : mem) P.push_back(&Elems()[i]);
    enum { A_NONE, A_DUP, A_UNGROUP, A_DEL } act = A_NONE;
    ImGui::PushID("grppanel");
    if (PSection("Main")) {
        {   // its name: Enter renames it
            static char nb[64]; static std::string nbFor;
            if (nbFor != g) { snprintf(nb, sizeof nb, "%s", leaf.c_str()); nbFor = g; }
            PIndent(); ImGui::SetNextItemWidth(170);
            if (ImGui::InputTextWithHint("##gname", "Group Name", nb, sizeof nb, ImGuiInputTextFlags_EnterReturnsTrue) && CleanGroup(nb)[0]) {
                RebaseGroup(g, (parent.empty() ? "" : parent + "/") + CleanGroup(nb)); nbFor = g_selGroup; ImGui::PopID(); return;
            }
            PTip("The group's name: Enter renames it.\nDrag any of its elements on screen to move the group (Alt + drag: a grouped element's outermost group).");
            int subs = 0; for (auto& n : GroupNames()) if (n != g && GroupParent(n) == g) ++subs;
            ImGui::SameLine(); ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%d element%s%s", (int)mem.size(), mem.size() == 1 ? "" : "s", subs ? (", " + std::to_string(subs) + (subs == 1 ? " group inside" : " groups inside")).c_str() : "");
        }
        bool newParent = false;
        {   // the group it's in: typed (Enter), or move to...
            const std::string shownParent = menuTop ? "" : RelGroup(parent);
            static char pb[96]; static std::string pbFor, pbWas;
            if (pbFor != g || pbWas != shownParent) { snprintf(pb, sizeof pb, "%s", shownParent.c_str()); pbFor = g; pbWas = shownParent; }
            PIndent(); ImGui::SetNextItemWidth(170);
            if (ImGui::InputTextWithHint("##gparent", "Inside (top level)", pb, sizeof pb, ImGuiInputTextFlags_EnterReturnsTrue)) {
                const std::string into = CleanGroup(pb);
                const std::string to = into.empty() ? IntoMenu(leaf) : IntoMenu(into) + "/" + leaf;
                if (!InGroup(to, g) && to != g) { RebaseGroup(g, to); ImGui::PopID(); return; }
            }
            PTip("The group it's inside: type another and press Enter to move it there (empty: the top level).");
            ImGui::SameLine(); ImGui::SetNextItemWidth(150);
            if (ImGui::BeginCombo("##gmoveto", "move to...")) {
                if (!parent.empty() && !menuTop && ImGui::Selectable("(top level)")) { RebaseGroup(g, IntoMenu(leaf)); ImGui::EndCombo(); ImGui::PopID(); return; }
                if (ImGui::Selectable("(new larger group...)")) newParent = true;
                for (auto& n : GroupNames()) {
                    if (InGroup(n, g) || n == parent || MenuEdOff(n) || RelGroup(n).empty()) continue;
                    if (ImGui::Selectable((RelGroup(n) + "##mv" + n).c_str())) { RebaseGroup(g, n + "/" + leaf); ImGui::EndCombo(); ImGui::PopID(); return; }
                }
                ImGui::EndCombo();
            }
            PTip("Move it into another group, or into a new larger group made around it.");
            ImGui::SameLine();
            ImGui::BeginDisabled(shownParent.empty());
            if (ImGui::Button("Select parent")) { g_selGroup = parent; ImGui::EndDisabled(); ImGui::PopID(); return; }
            ImGui::EndDisabled();
        }
        if (newParent) ImGui::OpenPopup("##newparent");
        if (ImGui::BeginPopup("##newparent")) {
            static char nb2[64] = "";
            if (ImGui::IsWindowAppearing()) { nb2[0] = 0; ImGui::SetKeyboardFocusHere(); }
            ImGui::SetNextItemWidth(200);
            if (ImGui::InputTextWithHint("##np", "the larger group's name (Enter)", nb2, sizeof nb2, ImGuiInputTextFlags_EnterReturnsTrue) && CleanGroup(nb2)[0]) {
                RebaseGroup(g, (parent.empty() ? "" : parent + "/") + CleanGroup(nb2) + "/" + leaf);
                ImGui::CloseCurrentPopup(); ImGui::EndPopup(); ImGui::PopID(); return;
            }
            ImGui::EndPopup();
        }
        // visible, layer, duplicate / ungroup / delete / link
        PIndent();
        { bool all = true; for (Elem* o : P) all &= o->visible; if (ImGui::Checkbox("Visible", &all)) { for (Elem* o : P) o->visible = all; g_dirty = true; } }
        {
            int lo = P[0]->layer, hi = lo; for (Elem* o : P) { lo = std::min(lo, o->layer); hi = std::max(hi, o->layer); }
            ImGui::SameLine(0, 14);
            if (ImGui::Button("-##glayer")) { for (Elem* o : P) --o->layer; g_dirty = true; }
            ImGui::SameLine(0, 3); ImGui::SetNextItemWidth(40);
            if (lo == hi) { int L = lo; if (ImGui::InputInt("##glayer", &L, 0, 0) && L != lo) { for (Elem* o : P) o->layer += L - lo; g_dirty = true; } }
            else {
                char lb[16] = ""; char hint[24]; snprintf(hint, sizeof hint, "%d..%d", lo, hi);
                if (ImGui::InputTextWithHint("##glayer", hint, lb, sizeof lb, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal) && lb[0]) {
                    const int L = atoi(lb); for (Elem* o : P) o->layer += L - lo; g_dirty = true;
                }
            }
            PTip("Its members' layers (lowest..highest): - / + and typing move them all, keeping their order among themselves.\nHigher layers are drawn on top.");
            ImGui::SameLine(0, 3); if (ImGui::Button("+##glayer")) { for (Elem* o : P) ++o->layer; g_dirty = true; }
            ImGui::SameLine(0, 4); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("layer");
        }
        {
            const float h = ImGui::GetFrameHeight();
            ImDrawList* dl = ImGui::GetWindowDrawList(); const ImU32 c = ImGui::GetColorU32(ImGuiCol_Text);
            ImGui::SameLine(0, 12);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##gdup", ImVec2(h, h))) act = A_DUP;
            PTip("duplicate");
            dl->AddRect(ImVec2(p.x + h * 0.25f, p.y + h * 0.2f), ImVec2(p.x + h * 0.65f, p.y + h * 0.6f), c);
            dl->AddRectFilled(ImVec2(p.x + h * 0.4f, p.y + h * 0.36f), ImVec2(p.x + h * 0.8f, p.y + h * 0.76f), c);
            ImGui::SameLine(0, 3); p = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##gung", ImVec2(h, h))) act = A_UNGROUP;
            PTip("ungroup - its elements and the groups inside it move up one level");
            for (int i = 0; i < 4; ++i) {   // (a dashed box with two pieces in it)
                const float t0 = 0.18f + i * 0.17f;
                dl->AddLine(ImVec2(p.x + h * t0, p.y + h * 0.18f), ImVec2(p.x + h * (t0 + 0.09f), p.y + h * 0.18f), c);
                dl->AddLine(ImVec2(p.x + h * t0, p.y + h * 0.82f), ImVec2(p.x + h * (t0 + 0.09f), p.y + h * 0.82f), c);
                dl->AddLine(ImVec2(p.x + h * 0.18f, p.y + h * t0), ImVec2(p.x + h * 0.18f, p.y + h * (t0 + 0.09f)), c);
                dl->AddLine(ImVec2(p.x + h * 0.82f, p.y + h * t0), ImVec2(p.x + h * 0.82f, p.y + h * (t0 + 0.09f)), c);
            }
            dl->AddRectFilled(ImVec2(p.x + h * 0.32f, p.y + h * 0.32f), ImVec2(p.x + h * 0.48f, p.y + h * 0.48f), c);
            dl->AddRectFilled(ImVec2(p.x + h * 0.52f, p.y + h * 0.52f), ImVec2(p.x + h * 0.68f, p.y + h * 0.68f), c);
            ImGui::SameLine(0, 3);
            if (ImGui::Button("X##gdel", ImVec2(h, h))) act = A_DEL;
            PTip("delete - the group and its elements");
            // link: blue while linked; its hint says with what, a click opens the link menu
            int k = -1; LinkSet* ls = LinkOf(g, g_editMode, &k);
            ImGui::SameLine(0, 3); p = ImGui::GetCursorScreenPos();
            if (ls) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.62f, 1.f));
            if (ImGui::Button("##glink", ImVec2(h, h))) ImGui::OpenPopup("##grplink");
            if (ls) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                if (ls) {
                    ImGui::TextUnformatted(ls->name.empty() ? "Linked group (not saved under a name)" : ("Linked group: " + ls->name + " (saved)").c_str());
                    ImGui::TextDisabled("Linked with %d group%s - an edit to one goes to the others:", (int)ls->m.size() - 1, ls->m.size() == 2 ? "" : "s");
                    for (auto& m : ls->m) if (m.group != g) ImGui::BulletText("%s", m.group.c_str());
                    ImGui::TextDisabled("Each keeps its own place: moving a whole group moves only that one.");
                    ImGui::TextDisabled("Click: link another / unlink / save as linked group / go to one");
                } else {
                    ImGui::TextUnformatted("link");
                    ImGui::TextDisabled("Linked groups keep the same pieces (on several menus): an edit to one goes to the others.\nClick: link with... / save as linked group");
                }
                ImGui::EndTooltip();
            }
            dl->AddRect(ImVec2(p.x + h * 0.14f, p.y + h * 0.33f), ImVec2(p.x + h * 0.58f, p.y + h * 0.67f), c, h * 0.16f, 0, 1.5f);
            dl->AddRect(ImVec2(p.x + h * 0.42f, p.y + h * 0.38f), ImVec2(p.x + h * 0.86f, p.y + h * 0.72f), c, h * 0.16f, 0, 1.5f);
            if (ImGui::BeginPopup("##grplink")) {
                LinkMenuItems(g);
                ImGui::Separator();
                const bool named = ls && !ls->name.empty();
                if (ImGui::BeginMenu(named ? ("Linked group: " + ls->name + "##lgsave").c_str() : "Save as linked group...")) { if (LinkSaveUI(g)) ImGui::CloseCurrentPopup(); ImGui::EndMenu(); }
                ImGui::EndPopup();
            }
            if (g_selGroup != g) { ImGui::PopID(); return; }   // (went to another, or the group changed)
        }
        PIndent(); FlagBoxes(P, "##grp", false);
        ImGui::Spacing();
    }
    if (PSection("Transform")) {
        {   // anchor for all of them (each keeps its place on screen)
            const int an = P[0]->anchor; bool mixed = false; for (Elem* o : P) mixed |= o->anchor != an;
            PIndent(); ImGui::Dummy(ImVec2(11, 1)); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(full - 11);
            if (ImGui::BeginCombo("##ganchor", (std::string("Anchor Set: ") + (mixed ? "--" : kAnchors[an])).c_str())) {
                for (int a = 0; a < 9; ++a) if (ImGui::Selectable(kAnchors[a], !mixed && a == an)) { for (Elem* o : P) SetAnchorKeep(*o, a); g_dirty = true; }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); ImGui::TextDisabled("(?)");
            PTip("Sets every element in the group (and the groups inside it) to this anchor, without moving them.");
        }
        {   // position: the middle of its box (from the screen's centre, units); a change moves every member
            const float k = Unit(); const ImVec2 ds = ImGui::GetIO().DisplaySize, mid = GroupMiddle(mem);
            float px = roundf((mid.x - ds.x / 2) / k * 10) / 10, py = roundf((mid.y - ds.y / 2) / k * 10) / 10;
            const float ox = px, oy = py;
            if (PFloats("gpos", "X", px, "Y", py, "Position", "The middle of the group's box (units from the screen's centre): a change moves every element in it.")) {
                const float dx = px - ox, dy = py - oy;
                for (Elem* o : P) { o->x += dx; o->y += dy; }
                g_dirty = true;
            }
        }
        {   // rotation: the members turned together about the group's middle (its first member's rotation stands for the group's)
            static ImVec2 about; static std::string aboutFor;
            const float cur = P[0]->rot;
            float v = cur;
            ImGui::Dummy(ImVec2(11, 1)); ImGui::SameLine(0, 0); ImGui::SetNextItemWidth(full - 11 + 11);
            const bool changed = ImGui::DragFloat("##grot", &v, 0.25f, -180.f, 180.f, "%.1f");
            if (ImGui::IsItemActivated() || aboutFor != g) { about = GroupMiddle(mem); aboutFor = g; }   // (fixed while dragging)
            PTip("Turns the whole group about its middle: each element's place swings round and its own rotation\nturns the same. Shown: its first element's rotation. Drag, or double-click to type.");
            if (changed && v != cur) RotateGroup(mem, v - cur, about);
            PLabel("Rotation");
        }
        ImGui::Spacing();
    }
    GroupMenuProps(g);
    ImGui::PopID();
    // (last: these change the list)
    if (act == A_DUP) {
        std::string ng = g + " copy"; int n = 2; auto names = GroupNames();
        while (std::find(names.begin(), names.end(), ng) != names.end()) ng = g + " copy " + std::to_string(n++);
        for (int i : mem) { Elem c = Elems()[i]; c.group = ng + c.group.substr(g.size()); c.x += 10; c.y += 10; Elems().push_back(c); }
        MenuCopy(g, ng);
        g_selGroup = ng; g_dirty = true;
    } else if (act == A_UNGROUP) {   // drops this level only: what's directly in it goes to the group around it
        for (int i : mem) Elems()[i].group = CleanGroup(parent + Elems()[i].group.substr(g.size()));
        g_widgets.erase(std::remove_if(g_widgets.begin(), g_widgets.end(), [&](const Widget& w) { return w.group == g; }), g_widgets.end());
        g_pages.erase(std::remove_if(g_pages.begin(), g_pages.end(), [&](const Page& pg) { return pg.group == g; }), g_pages.end());
        MenuRebase(g, parent);
        g_selGroup = parent; g_dirty = true;
    } else if (act == A_DEL) {
        for (int k = (int)mem.size() - 1; k >= 0; --k) Elems().erase(Elems().begin() + mem[k]);
        MenuForget(g);
        g_selGroup.clear(); g_sel = -1; g_dirty = true;
    }
}
