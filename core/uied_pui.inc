// The editor panels' building blocks (folding sections, two-box rows, colour rows...): the element panel
// (uied_props.inc), the group panel and the menus' widget / page sections (uied_menu.inc). Included before uied_menu.inc.

static std::map<std::string, bool> g_secFolded = { {"Copy to similar names", true} };   // (a section's fold, by its name: kept while the game runs)
static const float kPBox = 84.f, kPGap = 10.f;
static float PFull() { return 2 * kPBox + kPGap + 8 + ImGui::CalcTextSize("W").x; }   // (the X / Y row's width, from its first box)
static void PIndent() { ImGui::Dummy(ImVec2(11, 1)); ImGui::SameLine(0, 0); }
static void PLabel(const char* t) { ImGui::SameLine(0, kPGap); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(t); }
static void PTip(const char* t) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t); }

// a section's title line; its button (three lines) folds it. true: open
static bool PSection(const char* name, const char* shown = nullptr) {
    bool& folded = g_secFolded[name];
    ImGui::PushID(name);
    const ImVec2 p = ImGui::GetCursorScreenPos(); const float s = 14.f, lh = ImGui::GetTextLineHeight();
    if (ImGui::InvisibleButton("##fold", ImVec2(s, lh))) folded = !folded;
    const bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetTooltip(folded ? "Unfold" : "Fold");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = p.y + lh * 0.5f;
    dl->AddRectFilled(ImVec2(p.x, y - 5), ImVec2(p.x + s, y + 5), ImGui::GetColorU32(hov ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 1.f);
    const ImU32 c = ImGui::GetColorU32(folded ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    for (int i = -1; i <= 1; ++i) dl->AddLine(ImVec2(p.x + 3, y + i * 3.f), ImVec2(p.x + s - 3, y + i * 3.f), c);
    ImGui::SameLine(0, 4);
    if (folded) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::SeparatorText(shown ? shown : name);
    if (folded) ImGui::PopStyleColor();
    ImGui::PopID();
    return !folded;
}

// a number of one element or several ("--" while they differ: typing one sets them all; Enter)
static bool PNum(const char* id, const std::vector<float*>& v, float w, bool drag = false, float speed = 0.5f, const char* fmt = "%g",
                 float lo = 0.f, float hi = 0.f) {
    if (v.empty()) return false;
    bool same = true; for (float* f : v) same &= *f == *v[0];
    ImGui::SetNextItemWidth(w);
    bool ch = false;
    if (same) {
        float x = *v[0];
        ch = drag ? ImGui::DragFloat(id, &x, speed, lo, hi, fmt) : ImGui::InputFloat(id, &x, 0, 0, fmt);
        if (ch) for (float* f : v) *f = x;
    } else {
        char b[32] = "";
        if (ImGui::InputTextWithHint(id, "--", b, sizeof b, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific) && b[0]) {
            const float x = (float)atof(b); for (float* f : v) *f = x; ch = true;
        }
    }
    if (ch) g_dirty = true;
    return ch;
}
template <class F> static std::vector<float*> PFields(const std::vector<Elem*>& P, F f) { std::vector<float*> v; for (Elem* e : P) v.push_back(&f(*e)); return v; }
static void PPair(const char* a, const std::vector<float*>& av, const char* b, const std::vector<float*>& bv, const char* label, const char* tip = nullptr) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(a); ImGui::SameLine(0, 4); PNum("##a", av, kPBox); if (tip) PTip(tip);
    ImGui::SameLine(0, kPGap); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(b); ImGui::SameLine(0, 4); PNum("##b", bv, kPBox); if (tip) PTip(tip);
    PLabel(label);
    ImGui::PopID();
}

// a colour: its swatch, colour token, opacity, then its name
static bool PColour(const char* label, float* col, std::string& tok, float& tokA) {
    bool changed = false;
    ImGui::PushID(label);
    PIndent();
    ColorToken* t = tok.empty() ? nullptr : TokenOf(tok);
    if (ImGui::ColorEdit4("##c", t ? t->col : col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) changed = true;
    if (t && ImGui::IsItemHovered()) ImGui::SetTooltip("The token's colour: changes every element that uses %s.", t->name.c_str());
    const float h = ImGui::GetFrameHeight(), opW = 52.f;
    ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(PFull() - 11 - h - 4 - opW - 4);
    if (ImGui::BeginCombo("##tok", tok.empty() ? "own colour" : tok.c_str())) {
        if (ImGui::Selectable("own colour", tok.empty()) && !tok.empty()) { tok.clear(); changed = true; }
        for (auto& o : g_tokens) {
            TokenSwatch(o, ImGui::GetTextLineHeight()); ImGui::SameLine();
            if (ImGui::Selectable(o.name.c_str(), tok == o.name)) { tok = o.name; tokA = o.col[3] > 0.001f ? std::min(4.f, col[3] / o.col[3]) : 1.f; changed = true; }
            if (ImGui::IsItemHovered() && !o.label.empty()) ImGui::SetTooltip("%s", o.label.c_str());
        }
        ImGui::EndCombo();
    }
    PTip("A colour token: the colour by its role, shared with every element using it (Colour tokens below).");
    ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(opW);
    if (t) { float pc = tokA * 100; if (ImGui::DragFloat("##op", &pc, 0.5f, 0.f, 400.f, "%.0f%%")) { tokA = std::max(0.f, pc / 100); changed = true; } PTip("Its opacity, times the token's."); }
    else { float pc = col[3] * 100; if (ImGui::DragFloat("##op", &pc, 0.5f, 0.f, 100.f, "%.0f%%")) { col[3] = Clamp01f(pc / 100); changed = true; } PTip("Its opacity."); }
    PLabel(label);
    if (!tok.empty() && !t) { ImGui::SameLine(); ImGui::TextDisabled("(token %s isn't defined)", tok.c_str()); }
    ImGui::PopID();
    if (changed) { g_dirty = true; ResolveTokens(); }
    return changed;
}
static void PHex(const char* id, unsigned& v, float w, const char* label, const char* tip) {   // a game object's name hash
    char fb[16]; snprintf(fb, sizeof fb, v ? "%08X" : "", v);
    PIndent(); ImGui::SetNextItemWidth(w);
    if (ImGui::InputTextWithHint(id, "name hash", fb, sizeof fb, ImGuiInputTextFlags_CharsHexadecimal)) { v = (unsigned)strtoul(fb, nullptr, 16); g_dirty = true; }
    PTip(tip);
    PLabel(label);
    if (v) { GameObject g; ImGui::SameLine(); ImGui::TextDisabled(gameObject && gameObject(v, g) ? "(on screen)" : "(not shown now)"); }
}
static void PText(const char* id, std::string& s, const char* hint, float w, const char* label, const char* tip) {
    char b[256]; snprintf(b, sizeof b, "%s", s.c_str());
    PIndent(); ImGui::SetNextItemWidth(w);
    if (ImGui::InputTextWithHint(id, hint, b, sizeof b)) { s = b; g_dirty = true; }
    if (tip) PTip(tip);
    if (label) PLabel(label);
}

