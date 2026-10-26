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
| Nitrous left | gauge updater 0x4C7B20: 0x5925B0 thiscall(Car+0x40), 0..1 (0 hides the gauge); fitted: (Car+0x40)+0x414 not 0 / 0x7FFFFFFF |
| Turbo boost | needle updater 0x4C6B80 (0x5ECEC0): player cars, t = [[Car+0x34]+0x48]; fitted t+0x98 > 0; needle t+0x9C (0..1, damped by 0x5A0F30, rests ~0.55) |
| Nitrous firing | 0x59FA30 thiscall(Car+0x40) |
| Throttle | [Car+0x30]+0x20C (0..1) |

The gauge needle angle (0x4AA160) maps a 0..1 value between two angles (nitrous: 104..118 normally, 31..127 in drag).
NFSU2 has no speedbreaker: "NOS_GameBreaker" / "Tach_NOS_Gamebreaker" objects are leftovers.
