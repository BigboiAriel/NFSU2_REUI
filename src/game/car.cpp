// The local car's gauge values, read like the game's HUD (SPEED2.EXE v1.2):
//   the tachometer / speedometer updater 0x4C68D0 and the nitrous gauge updater 0x4C6B80.
//   Car = [[0x8900AC] + 4] (player 1's car).
//   RPM: Car+0x440 (but when [Car+0x14]+4 == 1, not [Car+0x14]+0x900 and Car+0x968 is set: [Car+0x968]+0x58).
//   Tach full scale: handling (Car+0x20) +0x1F4 (7 segments, the 6th and 7th red).
//   Speed: |Car+0x430| m/s, x3.6 km/h or x2.237 mph; units option: byte 0x83AA1A == 1 metric (0x5EA8B0).
//   Gear: Car+0x434 as in the table 0x7F66A4 {gear, char}: 0 'R', 1 'N', 2.. '1'..'7'.
//   Shift light: on while rpm > ShiftRpm(car, gear) - 0.95 * k, ShiftRpm = 0x5ECD30 thiscall(car, gear);
//   k = [0x7FBF60 + (gear + 9 * type) * 4], type = 0x5B5330 thiscall([Car+0x10] + 0x10) (char), or for a player
//   car (Car+0x28 == 1) 0x594F00 thiscall([[Car+0x34]+0x4C], gear).
//   Nitrous gauge (updater 0x4C7B20): left = 0x5925B0 thiscall(Car+0x40) (0 hides the gauge; up to 1 fills the main
//   bar, past 1 the gauge's second bar (+0x24) shows the extra, value - 1); fitted when
//   (Car+0x40)+0x414 is neither 0 nor 0x7FFFFFFF (the bottle's start time, read by 0x5925B0).
//   Turbo boost needle (updater 0x4C6B80, 0x5ECEC0): player cars only, t = [[Car+0x34]+0x48]; fitted = t+0x98 > 0;
//   needle = t+0x9C (0..1, damped; resting about 0.55), updated by 0x5A0F30.
//   Nitrous firing: 0x59FA30 thiscall(Car+0x40) (from the multiplayer mod). Throttle: [Car+0x30]+0x20C.
#include "car.h"
#include "../log.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace reui {

static bool R(const void* p, size_t n) { return p && !IsBadReadPtr(p, n); }

CarValues ReadCar() {
    CarValues c;
    uint8_t* p1 = *(uint8_t**)0x8900AC;
    uint8_t* car = R(p1, 8) ? *(uint8_t**)(p1 + 4) : nullptr;
    if (!R(car, 0x970)) return c;
    c.valid = true;
    c.rpm = *(float*)(car + 0x440);
    uint8_t* st = *(uint8_t**)(car + 0x14);
    uint8_t* alt = *(uint8_t**)(car + 0x968);
    if (R(st, 0x904) && *(int*)(st + 4) == 1 && !st[0x900] && R(alt, 0x5C)) c.rpm = *(float*)(alt + 0x58);
    uint8_t* hd = *(uint8_t**)(car + 0x20);
    if (R(hd, 0x1F8)) c.rpmMax = *(float*)(hd + 0x1F4);
    if (!(c.rpmMax > 500.f && c.rpmMax < 30000.f)) c.rpmMax = 0;
    c.speed = *(float*)(car + 0x430);
    c.metric = *(uint8_t*)0x83AA1A == 1;
    c.gear = *(int*)(car + 0x434);
    c.gearChar = 'X';
    c.gearGrey = *(int*)(car + 0x448) != 0;
    for (int i = 0; i < 10; ++i) if (*(int8_t*)(0x7F66A4 + i * 2) == c.gear) { c.gearChar = *(char*)(0x7F66A5 + i * 2); break; }
    bool player = *(int*)(car + 0x28) == 1;
    uint8_t* eng = *(uint8_t**)(car + 0x34);
    if (c.gear >= 0 && c.gear < 10) {
        uint8_t* tr = *(uint8_t**)(car + 0x10);
        float k = 0;
        if (player && R(eng, 0x50) && R(*(uint8_t**)(eng + 0x4C), 0xF4))
            k = ((float(__thiscall*)(void*, int))0x594F00)(*(void**)(eng + 0x4C), c.gear);
        else if (R(tr, 0x20)) {
            int type = ((char(__thiscall*)(void*))0x5B5330)(tr + 0x10);
            if (type >= 0 && type < 8) k = *(float*)(0x7FBF60 + (c.gear + 9 * type) * 4);
        }
        if (player ? R(eng, 0x50) && R(*(uint8_t**)(eng + 0x4C), 4) : true) {
            float shift = ((float(__thiscall*)(void*, int))0x5ECD30)(car, c.gear);
            c.shiftLight = c.rpm > shift - 0.95f * k && shift > 0;
        }
    }
    if (player && R(eng, 0x4C)) {   // turbo
        uint8_t* t = *(uint8_t**)(eng + 0x48);
        if (R(t, 0xA0)) {
            c.turbo = *(float*)(t + 0x98) > 0.f;
            c.boost = *(float*)(t + 0x9C);
            if (!(c.boost >= 0.f)) c.boost = 0; if (c.boost > 1.f) c.boost = 1.f;
        }
    }
    {   // nitrous
        int startT = *(int*)(car + 0x40 + 0x414);
        c.nosInstalled = startT != 0 && startT != 0x7FFFFFFF;
        c.nos = ((float(__thiscall*)(void*))0x5925B0)(car + 0x40);
        if (!(c.nos >= 0.f)) c.nos = 0; if (c.nos > 2.f) c.nos = 2.f;
    }
    c.nosOn = ((int(__thiscall*)(void*))0x59FA30)(car + 0x40) != 0;
    uint8_t* ctl = *(uint8_t**)(car + 0x30);
    if (R(ctl, 0x210)) { c.throttle = *(float*)(ctl + 0x20C); if (!(c.throttle >= 0.f)) c.throttle = 0; if (c.throttle > 1.f) c.throttle = 1.f; }
    return c;
}

// one read per frame, however many texts ask
static const CarValues& Car() {
    static CarValues c; static DWORD at = 0; static bool once = false;
    DWORD now = GetTickCount();
    if (!once || now != at) { c = ReadCar(); at = now; once = true; }
    return c;
}

// The stock tachometer's art for the current car, as its constructor 0x4F5100 picks it: ride = 0x5ED960(0x89E7A0,
// player) + 0x10; handling = 0x5B5350(ride + 0x10, 1) + 0x1F0 (+4 redline, +8 limiter); the car's custom HUD part
// (slot 0xA3, 0x6102F0) gives its colour (attribute "HUDINDEX", 0x61B500); 0x5ED9B0 fills the texture hashes
// {lines, redline, fill, -, needle, turbo lines, turbo fill, turbo needle}. The dial's full scale by 0x60FE10's class
// (0..7): 10500 10500 9500 9500 8500 8500 7500 7500; the needle turns clamp(rpm, 0, limiter) / scale * 235 degrees.
static const CarValues& Car();
struct TachArt { bool ok = false; uint32_t tex[8] = {}; float scale = 0, limiter = 0; int hud = 0; int paint[3][3] = {{255, 255, 255}, {255, 255, 255}, {255, 255, 255}}; };
static TachArt ReadTach() {
    TachArt t;
    if (*(int*)0x8654A4 != 6) return t;
    uint8_t* r = ((uint8_t*(__thiscall*)(void*, int))0x5ED960)((void*)0x89E7A0, 0);
    if (!R(r, 0x30)) return t;
    r += 0x10;
    uint8_t* h = ((uint8_t*(__thiscall*)(void*, int))0x5B5350)(r + 0x10, 1);
    if (!R(h, 0x200)) return t;
    float* hp = (float*)(h + 0x1F0);
    void* part = ((void*(__thiscall*)(void*, int))0x6102F0)(r, 0xA3);
    uint32_t key = ((uint32_t(__cdecl*)(const char*))0x43DB50)("HUDINDEX");
    t.hud = part && R(part, 4) ? ((int(__thiscall*)(void*, uint32_t, int))0x61B500)(part, key, 0) : 0;
    // its three paints (parts in slots 0xA4..0xA6, attributes RED / GREEN / BLUE; white without one): 0x4F57CF..
    // paint 1 = the dial and the boost dial, 2 = the needles, 3 = the RPM and boost numbers (0x4F5977..)
    static const char* kRGB[3] = {"RED", "GREEN", "BLUE"};
    for (int p = 0; p < 3; ++p) {
        void* pp = ((void*(__thiscall*)(void*, int))0x6102F0)(r, 0xA4 + p);
        if (!pp || !R(pp, 4)) continue;
        for (int c = 0; c < 3; ++c) t.paint[p][c] = ((int(__thiscall*)(void*, uint32_t, int))0x61B500)(pp, ((uint32_t(__cdecl*)(const char*))0x43DB50)(kRGB[c]), 0) & 0xFF;
    }
    ((void(__cdecl*)(uint32_t, float*, int, uint32_t*, int))0x5ED9B0)(*(uint32_t*)r, hp, 0, t.tex, t.hud);
    int cls = ((int(__cdecl*)(float*))0x60FE10)(hp);
    static const float kScale[8] = {10500, 10500, 9500, 9500, 8500, 8500, 7500, 7500};
    t.scale = cls >= 0 && cls < 8 ? kScale[cls] : 10500;
    t.limiter = hp[2];
    t.ok = true;
    return t;
}
static const TachArt& Tach() {   // once a second (and when the car changes)
    static TachArt t; static DWORD at = 0; static void* car = nullptr;
    uint8_t* p1 = *(uint8_t**)0x8900AC;
    void* c = R(p1, 8) ? *(void**)(p1 + 4) : nullptr;
    DWORD now = GetTickCount();
    if (c != car || now - at > 1000) {
        TachArt n = c ? ReadTach() : TachArt();
        if (n.ok != t.ok || memcmp(n.tex, t.tex, sizeof n.tex) != 0 || n.scale != t.scale)
            Log("tach art: %s colour %d scale %.0f limiter %.0f textures %08X %08X %08X %08X %08X %08X %08X %08X", n.ok ? "found" : "none", n.hud, n.scale, n.limiter,
                n.tex[0], n.tex[1], n.tex[2], n.tex[3], n.tex[4], n.tex[5], n.tex[6], n.tex[7]);
        t = n; at = now; car = c;
    }
    return t;
}

// The Cingular / Engage panel (top left). Engage HUD = [[[0x8900AC] + 0x9A4] + 0x58] (built by 0x4F83D0): 8 slots at +0x60,
// 0x14 each: byte state (2 / 3 just arrived, 4 waiting, 6 idle), byte type (1..18 events / shops by 0x4ABE50's icon,
// 17 and 20 other, 19 a text message), +0x10 its count. 0x4C90B0 picks the slot the panel shows (0x4C9030 runs it):
// events 0x4C8B80 (phone icon pulsing, the type's icon blinking, D-pad up pulsing); text messages 0x4ABAF0 (new: phone
// pulsing, the envelope animating, D-pad right pulsing; waiting: Cingular logo, envelope + "x<count>"); else the
// Cingular logo.
struct Engage { bool ok = false; int state = 0, type = 0, count = 0; };
static Engage ReadEngage() {
    Engage g;
    if (*(int*)0x8654A4 != 6) return g;
    uint8_t* p1 = *(uint8_t**)0x8900AC;
    uint8_t* hud = R(p1, 0x9A8) ? *(uint8_t**)(p1 + 0x9A4) : nullptr;
    uint8_t* eng = R(hud, 0x5C) ? *(uint8_t**)(hud + 0x58) : nullptr;
    if (!R(eng, 0x60 + 8 * 0x14)) return g;
    g.ok = true;
    uint8_t* slot = ((uint8_t*(__thiscall*)(void*))0x4C90B0)(eng);
    if (!R(slot, 0x14)) return g;
    g.state = (int8_t)slot[0]; g.type = (int8_t)slot[1]; g.count = *(int*)(slot + 0x10);
    return g;
}
static bool EngageToken(const std::string& k, std::string& out) {
    if (k.compare(0, 7, "engage_") != 0 && k.compare(0, 4, "sms_") != 0) return false;
    Engage g = ReadEngage();
    bool fresh = g.state == 2 || g.state == 3;
    bool sms = g.type == 19, event = g.type >= 1 && g.type <= 18 && g.type != 17;
    char b[32];
    out.clear();
    if (k == "engage_state") { if (g.ok) { snprintf(b, sizeof b, "%d", g.state); out = b; } }
    else if (k == "engage_type") { if (g.ok) { snprintf(b, sizeof b, "%d", g.type); out = b; } }
    else if (k == "engage_phone") out = fresh && (sms || event) ? "1" : "";      // the phone icon (pulsing) instead of the Cingular logo
    else if (k == "engage_event") out = fresh && event ? "1" : "";             // an event / shop to drive into: its icon, D-pad up
    else if (k == "engage_icon") { if (fresh && event) { snprintf(b, sizeof b, "#%08X", ((uint32_t(__cdecl*)(int))0x4ABE50)(g.type)); out = b; } }
    else if (k == "sms_new") out = fresh && sms ? "1" : "";
    else if (k == "sms_waiting") out = g.state == 4 && sms ? "1" : "";
    else if (k == "sms_any") out = sms && (fresh || g.state == 4) ? "1" : "";
    else if (k == "sms_count") { if (sms) { snprintf(b, sizeof b, "%d", g.count); out = b; } }
    else return false;
    return true;
}

bool CarToken(const std::string& k, std::string& out) {
    if (EngageToken(k, out)) return true;
    if (k.compare(0, 5, "tach_") == 0 || k.compare(0, 6, "turbo_") == 0 || k == "hud_color") {
        static const char* kTex[8] = {"tach_lines", "tach_redline", "tach_fill", "", "tach_needle", "turbo_lines", "turbo_fill", "turbo_needle"};
        const TachArt& t = Tach();
        out.clear();
        char b[32];
        for (int i = 0; i < 8; ++i) if (kTex[i][0] && k == kTex[i]) { if (t.ok && t.tex[i]) { snprintf(b, sizeof b, "#%08X", t.tex[i]); out = b; } return true; }
        if (k == "tach_angle") {
            const CarValues& c = Car();
            if (!t.ok || !c.valid || t.scale <= 0) return true;
            float r = std::max(0.f, std::min(c.rpm, t.limiter > 0 ? t.limiter : c.rpm));
            snprintf(b, sizeof b, "%.2f", r / t.scale * 235.f); out = b; return true;
        }
        if (k == "tach_max") { if (t.ok) { snprintf(b, sizeof b, "%.0f", t.scale); out = b; } return true; }
        if (k == "hud_color") { if (t.ok) { snprintf(b, sizeof b, "%02d", t.hud); out = b; } return true; }
        if (k.size() == 12 && k.compare(0, 11, "tach_paint_") == 0 && k[11] >= '1' && k[11] <= '3') {   // "R G B" for tint
            const int* p = t.paint[k[11] - '1'];
            if (t.ok) { snprintf(b, sizeof b, "%d %d %d", p[0], p[1], p[2]); out = b; }
            return true;
        }
        return false;
    }
    static const char* kKeys[] = { "rpm", "rpm_max", "rpm_pct", "rpm_k", "gear", "shift_light", "speedo", "speed_unit", "speed_unit_hud",
                                   "nos", "nos_tank", "nos_extra", "nos_installed", "nos_on", "throttle", "reverse", "boost", "turbo", "gear_grey" };
    bool known = false; for (auto kk : kKeys) if (k == kk) known = true;
    if (!known) return false;
    const CarValues& c = Car();
    out.clear();
    if (!c.valid) return true;   // not in a car: blank
    char b[32];
    auto f0 = [&](float v) { snprintf(b, sizeof b, "%.0f", v); out = b; };
    if (k == "rpm") f0(c.rpm);
    else if (k == "rpm_max") { if (c.rpmMax > 0) f0(c.rpmMax); }
    else if (k == "rpm_pct") { if (c.rpmMax > 0) f0(std::min(100.f, std::max(0.f, c.rpm / c.rpmMax * 100.f))); }
    else if (k == "rpm_k") { snprintf(b, sizeof b, "%.1f", c.rpm / 1000.f); out = b; }
    else if (k == "gear") out = std::string(1, c.gearChar);
    else if (k == "shift_light") out = c.shiftLight ? "1" : "";
    else if (k == "speedo") f0(fabsf(c.speed) * (c.metric ? 3.6f : 2.237f));
    else if (k == "speed_unit") out = c.metric ? "KPH" : "MPH";
    else if (k == "speed_unit_hud") out = c.metric ? "KM/H" : "MPH";   // as the stock speedometer labels it
    else if (k == "nos") { if (c.nosInstalled) f0(c.nos * 100.f); }
    else if (k == "nos_tank") { if (c.nosInstalled) f0(std::min(c.nos, 1.f) * 100.f); }
    else if (k == "nos_extra") { if (c.nosInstalled) f0(std::max(c.nos - 1.f, 0.f) * 100.f); }
    else if (k == "nos_installed") out = c.nosInstalled ? "1" : "";
    else if (k == "nos_on") out = c.nosOn ? "1" : "";
    else if (k == "throttle") f0(c.throttle * 100.f);
    else if (k == "reverse") out = c.gear == 0 ? "1" : "";
    else if (k == "boost") { if (c.turbo) f0(c.boost * 100.f); }
    else if (k == "turbo") out = c.turbo ? "1" : "";
    else if (k == "gear_grey") out = c.gearGrey ? "1" : "";   // the tach shows the gear grey (else orange): Car+0x448
    return true;
}

} // namespace reui
