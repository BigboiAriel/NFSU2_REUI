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
//   Nitrous gauge (updater 0x4C7B20): left = 0x5925B0 thiscall(Car+0x40) (0..1; 0 hides the gauge); fitted when
//   (Car+0x40)+0x414 is neither 0 nor 0x7FFFFFFF (the bottle's start time, read by 0x5925B0).
//   Turbo boost needle (updater 0x4C6B80, 0x5ECEC0): player cars only, t = [[Car+0x34]+0x48]; fitted = t+0x98 > 0;
//   needle = t+0x9C (0..1, damped; resting about 0.55), updated by 0x5A0F30.
//   Nitrous firing: 0x59FA30 thiscall(Car+0x40) (from the multiplayer mod). Throttle: [Car+0x30]+0x20C.
#include "car.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

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
        if (!(c.nos >= 0.f)) c.nos = 0; if (c.nos > 1.f) c.nos = 1.f;
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

bool CarToken(const std::string& k, std::string& out) {
    static const char* kKeys[] = { "rpm", "rpm_max", "rpm_pct", "rpm_k", "gear", "shift_light", "speedo", "speed_unit",
                                   "nos", "nos_installed", "nos_on", "throttle", "reverse", "boost", "turbo" };
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
    else if (k == "nos") { if (c.nosInstalled) f0(c.nos * 100.f); }
    else if (k == "nos_installed") out = c.nosInstalled ? "1" : "";
    else if (k == "nos_on") out = c.nosOn ? "1" : "";
    else if (k == "throttle") f0(c.throttle * 100.f);
    else if (k == "reverse") out = c.gear == 0 ? "1" : "";
    else if (k == "boost") { if (c.turbo) f0(c.boost * 100.f); }
    else if (k == "turbo") out = c.turbo ? "1" : "";
    return true;
}

} // namespace reui
