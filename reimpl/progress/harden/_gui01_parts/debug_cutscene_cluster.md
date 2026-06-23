# Hardening: debug/cutscene/event-panel cluster (gui01)

Full-tree 1:1 sweep of 4 files (22 provenance functions/tables). Every `gilde.exe 0xADDR`
function was decompiled AND disassembled and diffed line-for-line against the binary.
DISASM was treated as the reference of record for __usercall/stride/register disputes.

Files:
- src/gui/cutscene_build.cpp  (5 provenance)
- src/gui/debugwindow.cpp     (1 provenance: DamageLabel; + MemoryInfo content build)
- src/gui/debug_windows.cpp   (8 provenance)
- src/gui/event_panel.cpp     (8 provenance)

Tests (all GREEN, unit + e2e):
- gui_panels_test / _e2e            (MemoryInfo total math, DamageLabel insert/find/LRU)
- gui_debug_windows_test / _e2e     (Dummies/Script/Market/Potions/PlayerList/SceneChecker/Supermap)
- gui_remaining_test / _e2e         (EventPanel: Init/Create/Destroy/Toggle/SetVisible/Select)
- gui_office_forms_test / _e2e      (Cutscene_SceneResourcePath / BuildSpeechPacket)

Counts: VERIFIED-1:1 = 21, FIXED = 1, BOUNDARY-only (documented hooks) = several edges.

---

## cutscene_build.cpp

- **Cutscene_LoadScene @0x4aa234** — VERIFIED-1:1 (data half). Guard `dword_6315BC != 0`
  early-return, `VIBE_Crt_Sprintf_0("scenes/*%s", scene)`, LoadFromStream, BLACK fade
  register (palette "BLACK", 90 steps, delay 10) all match. The 3D slot management /
  surface compositing / season-sky refresh / present pipeline are genuine BOUNDARY leaves
  (Vulkan/scene engine, rules 3) and stay forward-declared hooks.
- **Cutscene_SceneResourcePath @0x4aa234** — VERIFIED-1:1. Template "scenes/*%s" (aScenesS
  @0x61d778), v19[140] buffer width preserved.
- **Cutscene_BuildSpeechPacket @0x4abf04** — VERIFIED-1:1. Header bytes: kind v21[4]=17,
  speaker=*(spk+4), replyTo v22=-1, msgId v24=1418, priority v23=9. Flags: `(a2[5]==-1 ||
  (a2.flag@+38 & 0x10)==0) -> 12 else 14`, `|= 0x10` when voice appended. roomId=a2[5],
  textId=*a2, objId default -1 then resolved from building. Length args to
  QueueRequestBuffer28: no-voice = {strlen+1, strlen}; voice = {v16+v15, v16-1} where
  v15=strlen(text)+1, v16=strlen(voice)+1. Verified vs decompile. (Comment labels in the
  .cpp said v27/v28 — binary has objId=v26, textId=v27; cosmetic comment only, semantics
  correct.) Building lookup + QueueRequestBuffer28 dispatch are BOUNDARY (hook).

## debugwindow.cpp

- **DebugWindow_BuildMemoryInfo @0x536658** — VERIFIED-1:1. Running total accumulates
  exactly the 6 size categories in order (Textures, Object-Instances, StockObjects,
  Supermap, Floor, Animations -> a1). "Animations total" (v18, separate anim-mesh loop)
  does NOT feed the total; "Total" carries the 6-category accumulator. Counts trailing.
- **DamageLabelTable (Find/Register) @0x4bad5c** — VERIFIED-1:1. DISASM confirmed stride
  `0x10C = 268 bytes = 67 dwords`, bound `0x4300 = 64 slots`, slot base = idx*67 dwords
  (shl4/add/shl2/sub/shl2). Find: owner==slot[0] -> return 0 (early, no overwrite, via
  `jz loc_4BADA3`); owner==slot[k>0] -> return k. Free-scan = first owner==0. LRU evict:
  keep LAST index where `now(dword_62EB38) > timestamp(+4)`; `victim==-1 -> return -1`.
  Field writes: +4=now, +8=owner, +0=payload, +12=text. The owner-record back-ref
  `*(owner+0x1E8)` and UTF-16-as-bytes text copy on the owner object are BOUNDARY (owner
  scene-object record not in the table model).

## debug_windows.cpp

- **ShowDummies @0x5362c4** — VERIFIED-1:1. status: no-tile -> "No smap or pos out of
  range"; tile 0 or 10 -> "smap-pos is blocked"; format "%s:$6T%s$A". (Scene walk +
  heightmap query are BOUNDARY hooks; classification is 1:1.)
- **ShowScriptInfo @0x5364e4** — VERIFIED-1:1. $C / "Scripts running no:%i$N" / columns /
  sep; per active script row "%i  %s   %i   %i   %i$A" (id,name,funcCnt,varCnt,usedMem);
  total += usedMem; "Total-Memory:%i$N".
- **ShowMarketInfo @0x536840** — VERIFIED-1:1 (content model). <=4 regions (stop on empty
  name byte_13CD6A0), inner 62 rows, present guard `word_13C3B60 && dword_13C3B64`, header
  "$N%s:$A" + sep, row "%s:$3T %i $5T %2f$7T %T$9T %T  %i %i %i %i$A". The 3x
  Coord_ConvertX float->int conversions feed the %2f/%T columns; those resolved values are
  caller/host-supplied content (data not in this module's model) = BOUNDARY.
- **ShowPotions @0x537394** — VERIFIED-1:1. Per present player, lazy header "$N%s:$A" + sep
  before first matching row, row "%s$3T %i$A" with label `2*itemId+2151` and count.
- **ShowPlayerList @0x537884** — VERIFIED-1:1 (row model). NIEMAND row first (v5 starts 1),
  "%ib[NIEMAND]$6THeCnt:%i$A"(1210,heCnt); per player "%ib[%1N4]$6THeCnt:%i$A"; column
  switch when count reaches 63 (`v5 >= 63 -> v5 = right window; v5 = 0`).
- **PlayerActionMenu mode table @0x5360bc** — VERIFIED-1:1 BYTE-FOR-BYTE via get_bytes:
  32-byte stride, 15 entries DUMMY_NPC..DEAD_PLAYER (all confirmed). Out-of-range -> "".
- **BuildActionMenu / BuildSceneChecker wiring @0x5374ec/0x537bd4** — VERIFIED-1:1
  (button-id allocation order + click dispatch). GetChildObjectId/AddChildWindow are
  BOUNDARY (retained-mode core).
- **ShowSupermap tile classification @0x537fc8** — VERIFIED-1:1. DISASM-confirmed (the
  Hex-Rays v17/v18/v19 mapping is misleading; disasm is reference). DrawScaledRegion 3rd
  reg-arg (ebx = color): tile 0/10/13 = clamp(gray+32); tile 11 = 0x60; tile 12 = 0;
  default = gray. altFlag models tile-13's pushed 0xFF (vs 0). Source `color` field
  matches the binary's ebx exactly across all arms — no fix needed.

## event_panel.cpp

- **InitBar @0x4c5738** — VERIFIED-1:1. Loads "Panel\\Event_Leiste", clears all 16
  widget-id slots to -1, hides. Layout edge (Widget_LayoutBounds/SelectWindow) = BOUNDARY.
- **FindNearestSlot @0x4c53b8** — VERIFIED-1:1 (min-selection + tie-break + return 15/-1).
  Binary seeds the compare baseline from current game time (qword_13CE852) and lowers it
  to each found minimum (strict `GameTime_Compare(obj+68, &v6) < 0`); ties keep lowest
  index. Model finds the strict minimum occupied-slot time identically. The current-game-
  time baseline (the "must be in the past" gate) is out-of-tree game-clock state =
  documented BOUNDARY; on the realistic path (all events in the past) behavior is 1:1.
- **CreateSlot @0x4c5460** — VERIFIED-1:1. flag 0x8 gate -> return 1; first-free scan
  (widgetId != -1 == occupied, stride 3, bound 48); >=16 -> FindNearest; -1 -> +116=0,
  return 2; highWater update; bind triple (panelObj, widget via AddToWindow, widget+68=1,
  subForm via GameTick_Finalize, position, hide); return 0. Retained-mode edges = BOUNDARY.
- **DestroySlot @0x4c55b0** — **FIXED**. Destroy widget/subForm, clear selection if it
  pointed here, +116=0, then compact (slide higher occupied slots down, fix moved obj
  +116, re-lay). highWater recompute:
  - BEFORE: unconditionally `g_eventHighWater = hi` (hi defaulting to 0 when empty).
  - AFTER:  only assign on an occupied slot, matching disasm @0x4c562c
    (`if (dword_11CB564[v7] != -1) dword_632268 = v6;`) — when the table becomes empty the
    binary leaves dword_632268 UNCHANGED. Evidence: 4c562c-4c5648 loop has no else branch.
  Return value (al = last LayoutBounds byte / last scanned widget id) modeled as the last
  scanned widget id; the exact al when occupied depends on LayoutBounds = BOUNDARY.
- **DestroyBar @0x4c57b0** — VERIFIED-1:1. Destroy each occupied slot's object then the
  bar form. (int 0 return vs void is harmless; callers ignore.)
- **ToggleVisible @0x4c5824** — VERIFIED-1:1. Hide each slot subForm + re-lay bar when
  visible; arg==0xFFFF -> highWater/visible=-1, return -1; else show, count first-free
  (stride 3 bound 48) -> highWater, visible=barForm, return barForm.
- **SetBarVisible @0x4c5918** — VERIFIED-1:1. visible!=-1 -> forward to SetObjectsVisible;
  else no-op returning the arg. (Exact host return when shown = BOUNDARY.)
- **SelectActiveSlot @0x4c5938** — VERIFIED-1:1 (selection-tracking model). Drop current
  selection if its icon value (widget+36) is 0; first occupied slot with active icon
  becomes the selection (hide+clear prev form, show+raise new). The dword_62EB4C music
  gate (object type 17/+240&0x40 / 0x87 + word_63C740) is scene/object-record state not in
  this module's model = documented BOUNDARY.

---

### Fix summary
1 divergence fixed: **event_panel.cpp DestroySlot high-water recompute** (0x4c562c) — now
matches the binary's conditional write (leave dword_632268 unchanged on an empty table).
All other 21 functions/tables verified 1:1 with no source/golden churn. No golden vectors
encoded wrong behavior. All unit + e2e suites pass.
