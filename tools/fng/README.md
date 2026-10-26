# FEng package tools

Read the game's HUD / menu layouts (.fng packages) straight from its bundles.

- `jdlz.py`: JDLZ decompression (chunk 0x30210 in GLOBAL\*.bun holds JDLZ-packed packages; 0x30203 is unpacked).
- `parse.py`: package parser - resources (RsNm / RsRq), objects (FObj > ObjD: Ot type, Oh name hash, OP guid / flags /
  resource index, PA parent guid, St text (UTF-16), SH language hash, Sj justification, SA start state: colour
  B G R A ints, pivot, position, rotation quaternion, size, uv) and their script names.
- `tree.py <unpacked package>`: the object tree with absolute positions (from the screen centre, 640x480 units,
  y down; images are centred on their position) and names from nfsu2-re's docs/dumps/hashes.txt.

Free roam / race HUD: HUD_SingleRace.fng, the first chunk of GLOBAL\InGameFreeRoam.bun / InGameRace.bun.
