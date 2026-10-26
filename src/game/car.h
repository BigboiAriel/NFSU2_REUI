#pragma once
#include <string>

namespace reui {

// The local car's gauge values, read the way the game's HUD reads them (docs/RE_NOTES.md "Car gauges").
struct CarValues {
    bool valid = false;
    float rpm = 0, rpmMax = 0;   // rpmMax: the tachometer's full scale (handling +0x1F4)
    int gear = 1;                // game value: 0 reverse, 1 neutral, 2.. = 1st..
    char gearChar = 'N';
    bool shiftLight = false;
    float speed = 0;             // m/s (Car+0x430, signed: negative backwards)
    bool metric = true;          // the game's units option
    bool nosInstalled = false, nosOn = false;
    float nos = 0;               // 0..2: 1 = a full tank, above 1 = the extra (the gauge's second bar)
    bool turbo = false;
    float boost = 0;             // 0..1, the boost gauge's needle
    float throttle = 0;          // 0..1
};
CarValues ReadCar();
bool CarToken(const std::string& k, std::string& out);   // {rpm} {gear} {nos}... (car.cpp has the list)

} // namespace reui
