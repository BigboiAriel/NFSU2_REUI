# RE notes (SPEED2.EXE v1.2)

## Car gauges
The game's tachometer / speedometer updater is 0x4C68D0 (thiscall(hud part, player) - car = [player+4]); the
nitrous gauge updater is 0x4C6B80. REUI reads the same values (src/game/car.cpp):

| Value | Where |
|---|---|
| Car | [[0x8900AC] + 4] |
| RPM | Car+0x440 (when [Car+0x14]+4 == 1, not [Car+0x14]+0x900 and Car+0x968 set: [Car+0x968]+0x58) |
| Tach full scale | handling (Car+0x20) +0x1F4; 7 segments lit = 1 + rpm / scale * 7, the 6th / 7th red |
| Speed | Car+0x430 (m/s, signed), shown as abs * 3.6 or * 2.237 |
| Units | 0x5EA8B0: byte 0x83AA1A == 1 = metric |
| Gear | Car+0x434; text from the table 0x7F66A4 {s8 gear, char}: 0 R, 1 N, 2.. 1..7 |
| Shift light | rpm > 0x5ECD30(car, gear) - 0.95 * k; k = 0x594F00([[Car+0x34]+0x4C], gear) for a player car (Car+0x28 == 1), else [0x7FBF60 + (gear + 9 * 0x5B5330([Car+0x10]+0x10)) * 4] |
| Nitrous left | gauge updater 0x4C7B20: 0x5925B0 thiscall(Car+0x40), 0..2 (0 hides the gauge; past 1 the second bar shows value - 1); fitted: (Car+0x40)+0x414 not 0 / 0x7FFFFFFF |
| Turbo boost | needle updater 0x4C6B80 (0x5ECEC0): player cars, t = [[Car+0x34]+0x48]; fitted t+0x98 > 0; needle t+0x9C (0..1, damped by 0x5A0F30, rests ~0.55) |
| Nitrous firing | 0x59FA30 thiscall(Car+0x40) |
| Throttle | [Car+0x30]+0x20C (0..1) |

The gauge needle angle (0x4AA160) maps a 0..1 value between two angles (nitrous: 104..118 normally, 31..127 in drag).
NFSU2 has no speedbreaker: "NOS_GameBreaker" / "Tach_NOS_Gamebreaker" objects are leftovers.

## Tachometer needle and the stock gauges' moving parts
- Tach updater 0x4C68D0, analog style when [0x89E7C0] == 1: angle = 0x4AA0E0(rpm, sweep = [hud+0xC4], handling+0x1F0, ...)
  = clamp(rpm / handling+0x1F8 (limiter), 0, 1) * sweep (negative sweep + 360), wrapped to 0..360; the needle
  (hud+0x30) gets it through 0x51CED0(obj, degrees) (a turn about the screen axis, absolute). Other style: 7 lit
  segments (hud+0x34..), 1 + rpm / (handling+0x1F4) * 7 lit, the 6th / 7th red.
- Speed text hud+0x58 (0x537BE0 printf), units label hud+0x5C (language 0x8569A25F km/h / 0x8569AB44 mph),
  gear text hud+0x64 (grey 0xFFC3C3C3 when Car+0x448 is set, else 0xFFFF8040), shift light hud+0x78 (0x50CA50 shows,
  0x50CA00 hides).
- Nitrous gauge 0x4C7B20: [0x89E7C0] == 1 (or drag): a vertical fill, 0x4C66F0(hud, obj, value, 1, 45, -9, 1)
  cuts the picture's V range; else a mask (hud+0x28) turned to v * 152 - 180 degrees, the second bar (hud+0x24)
  for the extra (value - 1).
- Boost needle 0x4C6B80: angle 0x4AA160(v, lo, hi) = lo + (v + 1) / 2 * (hi - lo) (360-wrapped): -14..222 normally,
  158 / 96 in drag (0x78E058..0x78E064).

## Stock tach art per car (0x4F5100 / 0x5ED9B0)
ride = 0x5ED960(0x89E7A0, player) + 0x10; handling = 0x5B5350(ride + 0x10, 1) + 0x1F0; HUD colour = attribute
"HUDINDEX" (0x61B500) of the car's part in slot 0xA3 (0x6102F0). 0x5ED9B0([ride], handling, player, out[8], colour)
gives texture hashes {lines, redline, fill, -, needle, turbo lines, turbo fill, turbo needle}: "%s_CUSTOM_%02d" names
(TACH_FILL, 3RDPERSON_RPMDIAL, 3RDPERSON_7500/8500/9500/10500LINES by class, 3RDPERSON_TURBO*), redline
3RDPERSON_REDLINE_<rpm>. Class 0x60FE10(handling) 0..7 -> full scale 10500 10500 9500 9500 8500 8500 7500 7500;
[hud+0xC4] = limiter / scale * 235, so the needle turns clamp(rpm, 0, limiter) / scale * 235 degrees from pointing
down (checked: 850 rpm on an 8500 dial = 23.5). Texture infos keep 24 characters of a name; +0x24 is the full
name's hash.
Nitrous arc (analog style): TACH_NOS drawn only where NOS_MASK's alpha half-disc lets it (the mask turned to
v * 152 - 180 degrees about its centre); REUI does it with "reveal (turning half)" + additive. Boost needle:
104 + v * 118 degrees (0x4AA160 with -14 / 222).
Tach colours: the car's custom HUD paint parts in slots 0xA4 / 0xA5 / 0xA6 (attributes RED / GREEN / BLUE, white
without a part) tint, in that order, the dial + boost dial, the needles, and the RPM + boost numbers (0x4F57CF..0x4F59C6).
Live textures: a TextureInfo's +0 points at its platform part; the IDirect3DTexture9* is at platform +0x18.

## Rear-view mirror
- Views are `0x832DE0 + id * 0x70` (0x48B1E0): +8 active, +0x40 camera, +0x58 render target struct (+8 surface, +0xC depth, +0x14 / +0x18 size).
- The mirror is view 3: camera 0x8718C0, render target 0x86E7BC. The render loop (0x5CAAD0, 0x5CAE3D .. 0x5CAFAF) draws the world into it. Then it draws the "RVM" art over it (TextureInfo at 0x8707E8) and RVM_MASK into its alpha (0x8707EC). Both are looked up in 0x5B98B0.
- The mirror texture itself is `[0x8708A4]`. 0x5CBF40 draws it on screen at x 200..440, y 15..95 (640x480) when view 3 is active. The `je` at 0x5CC0E6 (`0F 84 0A 01 00 00`) skips that draw; REUI turns it into `90 E9 ...` while an element shows `{rvm}`.

## HUD components and feature bits
- The HUD (`[Player+0x9A4]`, built by 0x4F8BF0) keeps its components at fixed slots. Each component has the feature bits it shows for at +0x10 / +0x14 (set by its constructor) and the last feature mask at +0x18 / +0x1C.
- The feature mask comes from 0x600090 (bit names: table at 0x7F6728, `HUD_FEATURE_*`). 0x600590 applies it only when it changes, through 0x4CA090. That function calls 0x4C6690 (thiscall comp, lo, hi; ret 8) for each affected component, which shows or hides the component's FE objects by `(bits & features) != 0`.
- Slots, with constructor and bits:

  | Slot | Constructor | Bits | Piece |
  |---|---|---|---|
  | 0x18 | 0x4F5100 | 0x10000060 (5, 6, 28) | tach |
  | 0x1C | 0x4F5A70 | 0x10000060 (5, 6, 28) | tach |
  | 0x34 | 0x4F6500 | 0x10000060 (5, 6, 28) | tach |
  | 0x20 | 0x4F4AC0 | 0x200 (bit 9) | race position |
  | 0x24 | 0x4F69E0 | bit 29 | |
  | 0x28 | 0x4F6A90 | bit 50 | NOS |
  | 0x2C | 0x4F6C00 | bit 31 | messages |
  | 0x30 | 0x4F7170 | 4 (bit 2) | race over |
  | 0x3C | 0x4F5C40 | 0x508 (bits 3, 8, 10) | laps |
  | 0x44 | 0x4F6420 | bit 14 | wrong way |
  | 0x48 | 0x4F6030 | bit 7 | split time |
  | 0x4C | 0x4F7AD0 | bit 17 | style points |
  | 0x50 | 0x4F80E0 | bit 14 | online |
  | 0x54 | 0x4F7340 | bit 49 | ? |
  | 0x58 | 0x4F83D0 | bit 33 | engage |
  | 0x5C | 0x4F73E0 | bit 0 | minimap |
  | 0x64 | 0x4F86D0 | bit 34 | showcase |
  | 0x6C | 0x4F87E0 | bit 38 | outrun meter |
  | 0x70 | 0x4F89D0 | bit 11 | dyno |
  | 0x74 | 0x4F4E70 | bit 0 | region popup |
  | 0x78 | 0x4F5060 | bit 39 | cash reward |
  | 0x80 | 0x4F4F10 | bit 51 | bank |
- "Replace stock HUD [x]": REUI zeroes a component's bits, calls 0x4C6690 again, and repeats that every 0.5 s while the replacing element shows. It restores the constructor's bits afterwards.
