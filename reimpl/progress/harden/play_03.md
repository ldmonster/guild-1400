# HARDEN play_03 — 1:1 verification report

Chunk: 22 files under `src/play/` (sdl_* native fronts, session_* drivers,
settings_io, slice_bank/church/combat). Every function/table carrying binary
provenance was diffed against the IDA decompile/disasm/raw bytes.

## Verdict summary

- **2 divergences FIXED** (both x87 80-bit-intermediate class, `session_atmos.cpp`).
- **1 comment mis-citation fixed** (`sdl_credits_screen.cpp` exit-affordance provenance).
- Everything else **VERIFIED-1:1** (or documented adaptation/model glue, unchanged).
- 26 test targets rebuilt + run: **all green** (see bottom).

## Fixes (binary evidence)

### FIX 1 — `SkyLayerStep` (session_atmos.cpp), gilde.exe 0x5ef7cc loop body
Binary: `fld [edx+10h]; fmul dt; fadd [edx+0Ch]; fst dword [edx+0Ch]` (0x5ef84b..0x5ef857)
then `fld1; fcompp` (0x5ef85a..0x5ef85c) — the `> 1.0` clamp compare runs on the
**unrounded 80-bit** sum, not the stored float. Likewise the fmod argument
`scrollSpeed*dt + scrollPos` stays on the FPU stack (0x5ef8b3..0x5ef8c3).
Reimpl rounded both to `float` before use. Fixed to `double` intermediates.

### FIX 2 — `SessionAtmos::brightnessStep` blend (session_atmos.cpp), gilde.exe 0x4b2504
Binary: v3 = brightness*0.01 is spilled to **float** by `fst [esp+var_34]`
(0x4b2677) and blend is `fsubr` of that float spill minus v11
(0x4b272b..0x4b2733): `flt_631DD4 = (float)((float)v3 - (double)v11)`.
Reimpl subtracted from the full-precision double. Fixed to model the float spill.

### Comment fix — sdl_credits_screen.cpp
The skip gate @0x56e6c3 is `dword_672230 (right-button release) || byte_67225C
(any-key latch)`; the code comments cited them backwards (ESC↔0x56e6c3,
click↔byte_67225C). Comments corrected; behavior (ESC/left-click) left as the
established rule-4 native-front input seam, pinned by sdl_credits_screen_test.

## Per-file verification

### session_atmos.cpp / .h
- 0x4b1e94 `VIBE_Sky_InitScene` regen arm (0x4b2146..0x4b2293) — **VERIFIED-1:1**
  by disasm: mode roll (`mov cx,ax` after `xor ecx,ecx` = u16; signed `sar` /2;
  `jge`→1 else 2); zero loop is **pre-incremented** (`add edx,4` first) so it
  zeroes exactly arc[0..23] (0x4b2180..0x4b218e); banding mask ecx=1
  (0x4b2197, `and eax,ecx`) with 700/+999, 500/+499, 250/+149; thunder gate
  mode==0 && arc>=999 → RandomModulo(3) (0x4b21ef..0x4b2216); wind walk
  angle=randf*dbl_61DD30, drift ±0.175 (0xBE333333 @0x4b2247), sin→11BC100,
  cos→11BC160, float-stored angle accumulation (0x4b2251..0x4b2282).
- 0x5efca8 `SetLayerFade` — VERIFIED-1:1 (±0 duration split, 1/duration step,
  offsets +12/+16/+36/+37).
- 0x5efc78 `SetLayerScrollSpeed` — VERIFIED-1:1 (−speed·flt_62C164).
- 0x5ef7cc loop body — **FIXED** (see FIX 1); fade current
  trunc-toward-zero via ConvertX, ≤255 else 0xFF — verified (0x5ef897..0x5ef8b0).
- 0x4c0040 `Weather_UpdateSky` — VERIFIED-1:1: grow counts (snow arc[h]; rain
  arc[h]&1?arc[h]/5:0 / arc[h]); peak-of-3 ((h+23)%24, h, (h+1)%24); category
  splits <50/<150; StrCmp-equal→++variant swap machine (mid @0x4c021f fade==0,
  front-name @0x4c02c9 fade==0xFF); scrolls ×0.75/×1/×1.5; dark fade
  (i<<6)/1000+96 @1000ms else 0 @2500ms.
- 0x4b2504 `DayCycle_UpdateBrightness` — **FIXED** blend float-spill (FIX 2);
  everything else VERIFIED-1:1: flash window unsigned compare, restore latch
  631DD8, skip window |t−1|<0.1 & Δ<10, soft window ≤0.2 & Δ<100 → force=
  relightDisabled else 1, band = trunc(b*0.01)%7 signed idiv, sun walk
  (v5<0||phase): phase==1 && v11%7≥3 → night else skip; else day walk; ++phase;
  lastBand latched in all fired branches (0x4b26f7).
- 0x4b24b0 `Light_SetSunHeight` — VERIFIED-1:1 (a2? −0.3−r·0.6 : r·0.6+0.3,
  RNG per node).
- 0x4c05ac `Weather_RenderAndThunder` decision core — VERIFIED-1:1: thunder roll
  chain (300 then ==3&&100 dead arm), flashDur = rand(8)+8, sun-ray tail hour
  11..17 unsigned, arc[h−1] via dword_11BC034[h], minute>30, roll(128)>0x60 &&
  !dword_62D564, latch/reset semantics (0x4c0657..0x4c06f8).
- Constants byte-checked (get_bytes): dbl_61DD30=6.2831853, dbl_61DD38=0.6,
  dbl_61DD40=−0.3, dbl_61DD48=0.3, flt_61DD50=1/255 (0x3B808081),
  dbl_61DD58=0.1, dbl_61DD60=0.2, dbl_61DD68=0.01, flt_62C164=1e-6f
  (0x358637BD), dbl_61E4B0/B8/C0=2.0/0.5/0.125, dbl_61E4C8/D0=0.75/1.5,
  flt_64A018=1.0 — **all match** the header/render constants.
- `Sky_CreateLayer` 0x5efb78 defaults (+12=1.0f, +16=0, +36/37=a3) — verified.

### session_input.cpp
- 0x40cdd0 `Input_ProcessMouseClicks` — VERIFIED-1:1: full three-button latch
  state machine (15-tick/±3px stale window, 40-tick double-click, LABEL_8
  right/middle mirrors), the 12-pair live-vs-latched compare list, the
  event-ring spill (v2=31/v3=589, −19 walk, v2<0→0 spill at row 1, v2≥31 skip,
  76-byte row copy + flag[19·(v2+1)]), the 0x4C latch memcpy.
- 0x40d920 keyboard latch/auto-repeat — VERIFIED-1:1 (press/release tables,
  repeat due = clock+9, unsigned `due+5 < clock` re-fire, dead-latch sentinel
  1316134911, return −1/0xFF path). g_inputClock/g_keyRepeatDueTick are u32 ✓.
- 0x40dab8 `Input_LatchMouseState` — VERIFIED-1:1: soft-cursor mirror pair,
  6 zero-inits, ring scan (76-byte steps, bound 2432), all 15 dword + 3 word
  field copies byte-offset-checked (+0x0C..+0x44, +0x06/08/0A), flag clear
  before the final word write.
- Poll glue vs 0x40d388 — verified: loop-head reset list (9 stores
  0x40d3d3..0x40d40f), wheel `(dword_62D0B8·delta)>>8` + 672208/672204 edges +
  6721CE/6721C8, button transition stores (incl. `!672238 → 6721DC=1` left
  click edge), the absolute-cursor double-clamp idiom 0x40d74c..0x40d786
  (clamp globals 62D0C4/C8/CC/D0 map to gui::g_cursorClamp* correctly), final
  ProcessMouseClicks. 0x40da88 `PollMouseAndKeyboard` = PollMouseDevice +
  0x4C-byte 672174→672210 copy + keyboard tail (rep movsd @0x40daa5) ✓.
  The synthesized 6721CA/CC deltas are the documented rule-4 stand-in for
  DirectInput mickeys (original absolute path leaves them 0; relative events
  supplied them).

### session_select.cpp
- 0x4b94d8 `Selection_ClearAll` — VERIFIED-1:1 (pre-incremented 536-stride sweep
  → records 1..768, 11BC270/631740/6317B0 stores, returns 411648).
- 0x4b950c `Selection_CommitContact` — VERIFIED-1:1 against the full decompile:
  5 zero-inits, +392 stale-worker drop, both quickjump arms (owner match /
  `!11BC284 || *(u16*)11BC284==253`), the 8-term input gate `|| v0`, tp_TUER /
  type==6 / 631738 door gate, +529&1 highlight chain, worker branch
  (flags>>8 &8, mark 12CEA98[536·id], mesh 12CEA94[134·id], voice, then the
  unconditional second 62D098 store), anchor stores in exact order, type-29
  reject with second sweep. (Error sprintf goes to a global instead of the
  original's dead stack buffer — observability only, no behavioral output.)
- 0x4bc280 tail — status latch VERIFIED-1:1 (v5 resolve 631744/631748,
  `v5!=631E54 || 631E58!=63174C || 631754` → ComputeSelectionFlags(word_63CC5C,
  v5, 0, 63174C) + ResetEntries + 3 latches). The v19&8 63175A/EdgeScroll block
  inside the cited range is owned by the camera adapter (see session_camera).
- 0x4b9444 `Selection_Reset` — reused reconstruction; the 4 zero stores verified.

### session_tick.cpp
- VERIFIED-1:1 orchestration against the binary: game-speed 40·level
  (0x4ff8b1/0x4ff8ee); sysmsg-3 sync `GameTime_Set(&v97,6,0,0)`
  (0x533ba4/0x53432e); frame-loop 0x4c0b8c broadcast gate
  (`764CE0==−1 && 62EB38>11AA488 unsigned && byte_63CC40`), 0x4c0be5 command
  window + `11AA488 = 62EB38+7`; 0x4c1324 day-end gate matches
  sim::ClockDayEndPending exactly (arms + `word_63C740 & 0x80`); fast-forward
  0x53449c.. (`test byte 63C740,8`, hour<0x17 unsigned, Advance(0,0,30));
  rollover 0x5345c6.. (Advance(+30min loop), Advance a2=+24 **hours** —
  GameTime_Advance 0x583150 adds a2 to the hour with day carry, so
  kDayRollAdvanceHours=24 is correct — then Set(6,0,0), commit, unpause).

### session_save.cpp
- All 6 string constants byte-checked: 0x624f30 "gamedata/saves",
  0x624f6c "Gamedata/saves/GILDE_SAVEGAME_%i.SAV", 0x624ef0 "QUICKSAVE",
  0x624efc "AUTOSAVE", 0x6252b8 "Gamedata\Saves\Quicksave.SAV",
  0x623551 "Saves\Autosave.SAV" — **all match**.
- Load driver vs 0x5a7604: version window 0x10026..0x10045 (@0x5a775a), scalar
  order + gates (≥0x10022 6477A8, ≥0x10030 649894, ≥0x1003D 632240 else
  1000000), section order (index/person/counters/city/slots), partial gate
  byte_13CEC94&2, Amt at ≥0x10045 in the partial prefix — VERIFIED-1:1.
- `EmitCityRecordGated` vs loader 0x5a8d3c: every field offset/size and gate
  (+520@0x1003E, +40/44@0x1003B, +64@0x10031, +124@0x10024, +372 word/dword
  @0x10020, +492@0x1002A, +453/+496@0x10021, +524..532@0x10036), counter
  biases ±1342/±1468 (loader adds, writer 0x5a4938 subtracts — decompiled
  both), the `<0x1003E → *(+400)=4` stamp — VERIFIED-1:1.

### session_audio.cpp
- flt_62522C=0x3C010204, flt_625230=0x3E800000 byte-checked ✓; 0x56c148
  ApplyVolumeSettings structure verified (master then msx·v6→ApplyMaster,
  sfx·v6→SetMusic, speech→64200C, msx_freq·0.25→6422A8 — the original's
  msx/sfx cross-wiring preserved). "Athmo_Marktplatz_Mono_4Bit" @0x6263f0 ✓,
  "msx\" @0x622b3e ✓. Outdoor-music entry gate @0x5815c4
  (`!byte_63CC40 || flt_6422A8<=0`) and the 9876 sentinel verified.
  MP3/IMA decode + device mapping are rule-5 shim (documented).

### session_camera.cpp
- Adapter over render/camera cluster. Verified: UpdatePan entry gate
  0x4b3685/0x4b3692 (62D4E8/62D4E4 → tail); the boot anchor call site —
  `push 3EA8F5C3h (0.33f); call VIBE_Camera_AnchorToTerrain` at **0x506fe9**
  (xref-confirmed in VIBE_Scene_ActivateAndRefreshCharacters). VERIFIED.

### session_npc_daily.cpp
- Work-distance gate @0x4e80b2 disasm-verified: dot+fsqrt then raw-bit
  `cmp dword, 45DAC000h (7000.0f); jge` skip — `< 7000.0f` for the always-
  non-negative distance ✓; missing-node pass-through modeled. Provider glue
  over sim leaves otherwise (state-1 social leaves documented as not derivable).

### session_hud.cpp / session_panels.cpp / session_persons3d.cpp / session_flow.cpp / settings_io.cpp
- Composition/glue over out-of-chunk reconstructions. Spot-verified:
  GetSeasonFromDay 0x58339c = `*a1 % 4` ✓; info-panel formats
  `"$Z%s$A%s"` / `"$Z%s$A%s$A>%s<"` with `14·code+1078` @0x4b64b0 and
  `"$Z%s..."` with `2·code+2151` @0x4b6930 ✓ (gui constants match).
  settings_io is Win32-profile-semantics shim over the real serializer
  0x56af54 / reader 0x56b834 (rule 4). session_flow is a disclosed
  deterministic harness over the real io serializers. No divergences.

### slice_bank.cpp / slice_church.cpp / slice_combat.cpp
- `BuildLoanPacket` vs `EnqueueCmd15` 0x494604 — VERIFIED-1:1: v5[0]=15,
  +0x10=a1, +0x14=a2, +0x1C=a4 byte, +0x1D=a3 dword (stack offsets recomputed
  from the decompile frame).
- slice_church: ExRemapObjectPair 0x496978 short-circuit semantics
  (RemoveObjektAmount==0 → return 1 without Add) verified; leaf money model
  is the disclosed hook seam.
- slice_combat: 0x498f44 record core verified (FindFreeSlot −1 early-out;
  qmemcpy 45 bytes from a1+16; id stamped at +0; taeter=+22 (a1+38),
  opfer=+18 (a1+34) per the sprintf). **Caveat (pre-existing, disclosed in the
  file):** the −10/−127 relation hit in `ApplyCrimeCommand` is NOT in 0x498f44
  (no relation-matrix write there; RelationLookup 0x5942fc xrefs are AI/info-
  panel readers) — it is the slice's modeled observable, already documented
  in-file; goldens pin it. Flagged for the world/relation owner.

### sdl_credits_screen.cpp
- 0x56e524 scroll model verified: step starts 4, ramp thresholds are raw-bit
  compares vs 0x42A00000 (80.0f) / 0x41F00000 (30.0f), initial offset
  −screenH (0x56e5d2), advance every `step` frames (0x56e635), complete when
  `textBottom < textHeight·dbl_625324 + offset` with dbl_625324=1.5
  (byte-checked). Comment mis-citation on the exit gate fixed (see above).

### sdl_loadgame_screen.cpp
- Verified at 0x569d00/0x569e88: +265 loose-name open (0x569d7f/0x569d9c),
  partial drop `hdr[4]&2` (0x569e1a), 16-slot cap, AddChildWindow y=130·slot
  h=16 (0x569e88), AddTextLabel(168,−2) (0x569ebd), 160-wide raw-bitmap
  thumbnail widget word_13CED76/78 at ≥0x10028 (0x569f61). VERIFIED.

### sdl_options_screen.cpp
- `SliderComputeStep` vs 0x41df08 — **all 13 thresholds/returns identical**.
- Thumb mapping `4·(value−min)/step` vs `Scrollbar_DragThumb` 0x4208ac ✓
  (dword_62D0C8/62D0D0/+32 formulas). Form geometry is asset groundtruth
  (forms.BIN, prior subscreen wave). VERIFIED.

### sdl_menu.cpp / sdl_city_screen.cpp / sdl_session.cpp
- sdl_menu: kind-9 button sizing vs 0x41b164 verified (`text + capL + capR + 4`).
- sdl_city_screen: disclosed 2D-equivalent front over the real ChooseCity model
  leaves (original is a 3D raycast scene) — adaptation glue.
- sdl_session: pure orchestration over the verified drivers above; season =
  day%4 and the catalog strides (589/536/65) consistent with the brief's
  ground truth. No owned divergent logic found.

## Test targets run (all green)

| target | result |
|---|---|
| session_atmos_test | 2216 checks, 0 failures |
| session_atmos_e2e_test | 13415 checks, 0 failures |
| light_atmos_test | 461 checks, 0 failures |
| atmos_lighting_itest | 37 checks, 0 failures |
| session_input_test | 119 checks, 0 failures |
| session_select_test | 106 checks, 0 failures |
| session_tick_test | 69 checks, 0 failures |
| session_save_e2e_test | 166 checks, 0 failures |
| save_roundtrip_test | 24 checks, 0 failures |
| session_audio_test | 99 checks, 0 failures |
| session_hud_test | 4660 checks, 0 failures |
| session_panels_test | 250 checks, 0 failures |
| session_camera_test | 72 checks, 0 failures |
| session_living_city_test | 75 checks, 0 failures |
| session_persons3d_test | 70 checks, 0 failures |
| session_flow_e2e_test | 36 checks, 0 failures |
| settings_io_test | 103 checks, 0 failures |
| slice_church_test | 34 checks, 0 failures |
| play_slice_bank_test | 47 checks, 0 failures |
| slice_combat_e2e_test | 29 checks, 0 failures |
| sdl_credits_screen_test | 47 checks, 0 failures |
| sdl_loadgame_screen_test | 259 checks, 0 failures |
| sdl_options_screen_test | 118 checks, 0 failures |
| sdl_menu_test | 28 checks, 0 failures |
| sdl_session_test | 25 checks, 0 failures |
| playable_flow_e2e_test | 154 checks, 0 failures |

No goldens needed re-pinning (both fixes are sub-ulp x87 modeling corrections;
all existing pins still hold).
