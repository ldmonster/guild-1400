# Harden sweep — src/sim/cheat_recon.cpp

Full-tree 1:1 verification against gilde.exe (MCP live). Every provenance-tagged
function decompiled and diffed line-for-line; tables/constants/strings confirmed
with get_bytes/get_string. `cheat_recon.cpp` compiles clean (g++ -std=c++17
-fsyntax-only -> EXIT 0). The golden vectors in tests/unit/cheat_recon_test.cpp
were diffed against the binary and are all consistent (no golden changes needed).

NOTE: the full `guild` library / test link currently fails in a DIFFERENT file,
`src/sim/animal_wander.cpp` (catSoundBurn / SoundBurnPolicy mismatch — an
in-progress edit by another agent, NOT my chunk), so the test binary could not be
linked/run this session. cheat_recon.cpp itself is unaffected and compiles.

This report was re-derived independently from the binary (the prior, rate-limited
agent's report was found to be accurate; corrections below where it overstated).

## Tables / constants confirmed via get_bytes

- **aFest @ 0x633938** (27 entries, 64-byte stride, INLINE strings — not pointers).
  All 27 tokens read byte-for-byte and matched `kCheatStrings[0..26]` EXACTLY
  (case + content): FEST, BELAGERUNG_START, AUFSTAND, BRAND, WIRBELSTURM,
  STADTKASSE, VERMOEGEN_STEUER, VERMOEGEN, ANSEHEN_BEI_AMTSTRAEGERN, KILL_PLAYER,
  STRAFE_HINRICHTUNG, STRAFE_KERKER, AMTSTRAEGER_INVENTAR_PLUS, GESETZ, GESETZ_REL,
  GLAUBENSWECHSEL, AUFRUHR, GILDENSITZE_BRACH, NACHFRAGE, SOELDNER_PLUENDERN,
  SOELDNER_MARODIEREN, FERNHANDEL_RAUBRITTER, SPENDE_ANSEHEN, BETEILIGUNG,
  KOMMENTARE, PEST, INVENTAR_PLUS.
- **funcs_4FDAA5 @ 0x634414** (27 fn ptrs). Cluster slots [16..26] read =
  0x4fc260,0x4fc2d0,0x4fc464,0x4fc7f0,0x4fc8b0,0x4fc970,0x4fca1c,0x4fcae4,
  0x4fcbac,0x4fcd80,0x4fce2c — exact match to the documented handler mapping.
- **dword_4F8D5C @ 0x4f8d5c** = {-1, +1} (ff ff ff ff / 01 00 00 00). ✓
- Sign strings @ 0x4f8d64: "MINUS\0" then "PLUS\0\0", stride 6. ✓
- Category strings @ 0x4f8d70 (aWaffen): "WAFFEN","BUECHER","ENDPRODUKTE", stride 12. ✓
- **dbl_6207C8 @ 0x6207c8** = 0x3f847ae147ae147b = 0.01 (percent factor; engine-side
  price math only).
- **byte_64A208 @ 0x64a208** (ctype): bit 0x20 = digits '0'..'9' only; bit 2 =
  {tab,LF,VT,FF,CR,space} — confirms ParseInt digit/whitespace classes.
- ToggleUpdateFlags banner strings (refs): all 8 on/off pairs confirmed exact
  (update_d3, update_script, main_update_sim, update_panel, main_update_ai,
  main_update_d2, "Update He", update_character).

## Per-function status

| Addr | Name | Status |
|------|------|--------|
| 0x4fd8ac | History_ParseCommandlineSecondPass (match loop / table provenance) | VERIFIED-1:1 (loop reproduced by Cheat_MatchToken; tokenizer is history-side, out of cluster) |
| 0x4fedb0 | Hotkey_ClearEntry | VERIFIED-1:1 (+4=-1,+8=-1) |
| 0x4fedc0 | Hotkey_InitTable | VERIFIED-1:1 (zero 132B; key=59+i; entry10 key=87; ids=-1) |
| 0x4ff590 | Hotkey_SaveTable | VERIFIED-1:1 (count=11, per-entry 1+4+4, short-write→0) |
| 0x4ff634 | Hotkey_LoadTable | VERIFIED-1:1 (count read, per-entry, short-read→0) |
| 0x4ff7a8 | Hotkey_HandleKeyPress | VERIFIED-1:1 (gate lastKey&&mode!=1; key88 assign-win; scan 11; assignArmed gate) |
| 0x4bfa54 | DebugKey_Dispatch | VERIFIED-1:1 (79→1/Sel, 80→2/Act, 82→0/Toggle; remembered mode) |
| 0x4bf054 | DebugKey_ToggleUpdateFlags | VERIFIED-1:1 (all 10 keys 4/18/21/23/25/30/32/35/37/46; flip newState = was-zero; banners exact) |
| 0x4bed44 | DebugKey_ClassifySelection | VERIFIED-1:1 (full branch ladder; keys 18/20/32/34/35/44 + None gaps) |
| 0x4bf2a8 | DebugKey_ClassifyAction | VERIFIED-1:1 (full nested ladder; keys 17/22/23/25/30/31/32/33/34/35/36/37/38/46/48) |
| 0x4fc260 | Cheat_QueueWinGame | VERIFIED decode (op125, always returns 1) / BOUNDARY effect |
| 0x4fc2d0 | Cheat_QueueAcquireOffices | VERIFIED decode ('-' gate, ParseInt, op0x80, ret1) / BOUNDARY (offices 30..33) |
| 0x4fc464 | Cheat_ParseSetWeapons | VERIFIED-1:1 decode (sign{-1,+1}×MINUS/PLUS, cat WAFFEN/BUECHER/ENDPRODUKTE, '_' seps, amount=sign*ParseInt via `imul eax,edx`) / BOUNDARY (building price sweep) |
| 0x4fc7f0 | Cheat_QueueHealAllChars | VERIFIED decode (op87 per animal, ret1) / BOUNDARY (entity sweep) |
| 0x4fc8b0 | Cheat_QueueRestoreAllChars | VERIFIED decode (op88, ret1) / BOUNDARY |
| 0x4fc970 | Cheat_ParseSpawnChar | VERIFIED-1:1 decode ('-'; tail len>=2; letters N/S/O/W/A = 78/83/79/87/65; op18 if amt>0; **ret1 always when gated**) / BOUNDARY (spawn) |
| 0x4fca1c | Cheat_QueueGiveBuildingsTeam | VERIFIED decode (op84 type12, <=8, ret1) / BOUNDARY |
| 0x4fcae4 | Cheat_QueueGiveBuildingsAlt | VERIFIED decode (op85 type10, <=8, ret1) / BOUNDARY |
| 0x4fcbac | Cheat_QueueTeleportCharByName | VERIFIED decode (op82) / BOUNDARY (history scan can return 0; reimpl returns action id) |
| 0x4fcd80 | Cheat_ParseSetGuildLevel | VERIFIED-1:1 decode ('-' gate, ParseInt, op89 level 11, ret1) / BOUNDARY |
| 0x4fce2c | Cheat_ParseRenameChar | VERIFIED decode ('-' gate, tail len>=4, op17) / BOUNDARY (person-record + rank gates + active-object count are engine-side) |
| (helper) | CheatParseInt vs VIBE_Util_ParseInt 0x5dc070 | VERIFIED-1:1 for reachable grammar (digit set exact; leading-whitespace skip unreachable — ParseInt always starts at sign/digit). Comment updated with ctype-table provenance. |

## Findings

- No divergences found in any control-flow, branch condition, constant, table,
  string, or struct offset. The reconstruction was already faithful.
- `Cheat_ParseSetWeapons`: confirmed the signed amount is `imul eax, edx`
  (ParseInt × sign-table value), and the table is {-1,+1} at 0x4f8d5c — the reimpl
  `signVal[s]*amount` matches the binary. The percent factor (×0.01, +1.0) is
  engine-side price math (BOUNDARY).
- The float→int caution (ConvertX/fistp/(int)) does not apply: this cluster has no
  float→int conversion. The only FP (ParseSetWeapons price factor) stays in the
  engine-side building sweep, which is correctly a boundary.
- RNG: only HandleActionCmds key 22 draws `RandomModulo(0x80)`; that is the engine
  effect (boundary). The classifier draws no RNG, keeping the stream untouched —
  correct.

## Changes made

- None. The reconstruction was already 1:1; no source edits were required this
  session. (The prior agent's report mentioned an expanded CheatParseInt comment,
  but that edit was never committed to the file before it was rate-limited; the
  current `CheatParseInt` comment is left as-is — it is behaviorally faithful and
  the ctype-class detail is captured in this report instead.)

## Counts

- VERIFIED-1:1 (pure decode, fully reproduced): 11
  (ParseCommandline match loop, ClearEntry, InitTable, SaveTable, LoadTable,
   HandleKeyPress, Dispatch, ToggleUpdateFlags, ClassifySelection, ClassifyAction,
   CheatParseInt) + ParseSetWeapons/ParseSpawnChar/ParseSetGuildLevel decode exact.
- BOUNDARY (faithful decode; queued effect / entity sweep / history scan is
  engine-side, out of cluster reach — rule 8 hooks): 11 cheat handlers
  (indices 16..26).
- FIXED: 0 (reconstruction was already correct).

Test: tests/unit/cheat_recon_test.cpp — golden vectors verified consistent with
the binary (cheat strings; MatchToken incl. GESETZ/GESETZ_REL table-order
collision -> 13; SetWeapons sign {-1,+1} + categories; SpawnChar letter gate;
hotkey init keys 59..68 + entry10=87; dispatch modes 79/80/82; toggle banners;
both classifiers). Not executed this session due to the unrelated animal_wander.cpp
link break noted above; cheat_recon.cpp compiles clean standalone.
