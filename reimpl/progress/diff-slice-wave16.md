# Wave-16 TRUE 1:1 binary diff — play slices + dialog + cutscene-UI (W16-SLICE)

MCP live. Decompiled every binary-grounded function in the cluster and compared
LINE-FOR-LINE against the reconstruction. Cluster: `src/play/` slice_*/dialog_*/
cutscene_recon2_*/interact_building/city_info/text_recon*/ui_recon* + tests.

Heads-up applied: **VIBE_Coord_ConvertX @0x5c6b08 truncates toward zero** — every
float->int that routes through ConvertX must be `std::trunc` / `(int)double`
(cvttsd2si), NOT round-to-nearest. Audited all ConvertX sites in the cluster.

## VERIFIED-1:1 (decompiled, matches the reconstruction exactly)

### text_recon3_itemlabel — VIBE_Text_FormatItemLabelWithIcon @0x59ccf4
Full switch (kinds 1..8 + default + a1>=0x300 fallback + default-record path)
re-diffed against the decompile. 536-byte (268-word) record, field offsets
+48/+64, the `record + (off-1) + strlen(off)` plural-end byte, `dword_8C36B0`
title-id base offsets (272/279, 525/560, 294/370, 471/498), and the WideCopy /
sprintf splices all match.
- `unk_627E00`/`unk_627E04` (the kind-1/3/4 plural suffix WideCopy targets):
  get_bytes shows BOTH start with a 0 code-unit -> empty wide strings. The recon
  modelling both as `"\0\0"` is correct (NOT the `%s %s` ASCII at 0x627e08, a
  different region). VERIFIED.
- `dword_8C4784` (a1>=0x300 fallback name) and `dword_59BF60/59C060/59C160`
  (stack scratch) are zero -> recon's empty / zeroed scratch matches.
- case 4: `(*(int*)(record+353) >> 24)` == byte[356]; `(*(int*)(v65+177) >> 24)`
  with v65 __int16* == byte[357]. Recon `record[356]`/`record[357]` correct.
- Wave-12 OOB hardening (bounded field reads, 552-byte scratch) confirmed to keep
  well-formed output byte-identical.

### ui_recon5_panels
- **VIBE_PlayerBar_Create @0x4b1ba8** — 32-slot reset loop (i+=10, i!=320), handle
  sentinel 0xFFFF, others -1, flag 0. Matches.
- **VIBE_InfoPanel_RunPlayerInfoWindow @0x55bff8** — trait-row mask table
  (4657..4665) with masks 0x0F/0xF0/0x0F/0x30/0x1C000/0x0E/0x70/0x180/0x1E and the
  per-branch row-advance shape. Matches. (The cash `(int)CashAmount` after
  ConvertX is a deferred Text_RenderRichString side effect, not in this pure unit.)
- **VIBE_MapTable_RunCityTowerScene @0x55a9f8** — pennant corner interpolation
  `pos = linksOben + (corner*scale)*(rechtsUnten-linksOben)` stays FLOAT (no int
  trunc; AttachToUniverseNode takes floats). Corner table `dword_13CD6E0/6E4` is
  zero-init runtime data, scales `flt_624928=0x3abb3ee7`, `flt_62492C=0x3aaec33e`
  — recon takes them as params. Matches.
- **VIBE_MapView_PanelDispatcher @0x5441d0** — disassembled the 8 radio-button
  `Object_AddToWindow` calls (usercall: eax=Y, edx=X=432, ebx=sprite). Confirmed
  `kMapViewRadioY = {88,166,218,270,354,406,458,536}` and
  `kMapViewRadioSprite = {1334,1338,1340,1337,1335,1339,1336,1341}` EXACTLY (the
  decompile folded the multi-reg usercall to `AddToWindow(window,432)`, hiding
  eax/ebx — the recon read the disasm correctly). Scroll-edge chain
  (671E28→dy-4 / 671E30→dy+4 / 671E2B→dx-4 / 671E2D→dx+4) and focus clamp
  (x∈[0,mapW-512], y∈[0,mapH-360], lower-then-upper) match.
- **VIBE_Hud_UpdateSelectionAndTargets @0x4ba614** — LABEL_9 shadow loop over 768
  entries (stride 536), selected count, prev-frame copy. Matches. Its corner-quad
  ConvertX sites (`v42=(int)v11`,`v41=(int)v15`) truncate — deferred command side
  effect, not in the pure shadow unit.

### ui_recon4_hud_surface
- **VIBE_Hud_BuildPersonCard @0x553f30** / **...Simple @0x55433c** — centering math
  `v24 = (cardSpriteDims>>16)/2 + baseX`; portrait X `= v24 - (portW>>16)/2 - 3`.
  Disassembled 0x553fa0: sprite index `[edx+0x18C]`, `*84` lea chain, `[eax+0x4E]`
  (+78), `sar 16`, signed `/2` (sar 0x1F / sub / sar 1), `lea [edx-3]`. Recon
  `v24 - portW/2 - 3` EXACT. Slider gate (kind!=6), kind==7 full, icon-row kinds
  {5,6,7} all match. (Favorability `(int)v17` after ConvertX = deferred slider
  value side effect.) Colour packing, paintbox gate, lender slots, asset-overview
  params all match.

### cutscene_recon2_movie — VIBE_Movie_PlayOutro @0x534924
Lazy moveahead.dll bind, music fade (1000ms), fade-to-BLACK pre-roll loop
(`(status&4)==0 || 0.0 > flt_62DA00`), save/disable byte_642008, "outro.mpg" path,
renderMode==1 present, input release, Sleep(500)/Prepare(…,138)/Sleep(200)/Play,
teardown, restore vol = `(u8)byte_1233552 * flt_623640`. `flt_623640 = 0x3C010204`
== recon `0.00787353515625f` (bit-exact). All constants VERIFIED.

### cutscene_recon2_theatre — VIBE_Theatre_RunEventMenu @0x536a30
All ten event codes verified: exec=5, wedding=6, birth=7, death=8, bankruptcy=9,
tenancy=10, duel=4, plague=89, fire=78, storm=80. subKind 32 (default) / 34
(tenancy,duel). Tenancy amount 16000, fire v58=2000, storm v58=1000, plague
v57=3*rand(3). Disaster offset `days=30*rand(2)`, `months=rand(4)(+7 if WORD2(clock)
>= 0x17)`. Execution extra=rand(5). All match.

### slice_market — price truncation
**VIBE_Trade_RequestSellObjekt @0x46bff0**: `v5 = LookupCachedMarketPrice(...);
ConvertX(); v12 = (int)v5` and `v11 = (__int64)v8` — ConvertX-truncate. The slice's
`TruncToInt(double)=(int)double` matches. `ComputeMarketPrice @0x58f3d0` lives in
the sim cluster (building_production.cpp), not edited here.

### city_info / dialog_* / other slices
Composed window-flow reconstructions (no single decompiled-function provenance to
diff line-for-line). Their coordinate math (`(int)(120*sx)`) already truncates
toward zero, consistent with ConvertX. No round-to-nearest idiom present anywhere
in the cluster. No divergence.

## FIXED (diverged from the binary -> corrected + golden-pinned)

### 1. Theatre_GatherDuelParticipants — wrong column stored (cutscene_recon2_theatre)
Binary @0x537275-0x5372f7 uses TWO distinct columns:
  COMPARE key  = `word_12CE910[268*rec]` (id word: id!=-1, role!=0, id!=localMaster)
  STORED value = `dword_12CE914[134*rec]` (master dword) -> pushed into v42[] tail.
The recon compared AND stored the same `idCol` -> pushed the id word, not the master
dword. **FIX**: added a 5-arg form taking an explicit `masterCol` for the stored
value (compare still uses `idCol`); kept the 4-arg form delegating with
`masterCol = idCol` for callers/tests where the columns coincide (goldens
byte-identical). Master column shorter than the id column reads 0 (bounded), no OOB.
- Goldens added: `GatherDuelStoresMasterColumnNotIdColumn`,
  `GatherDuelMasterColumnShortBoundedZero`.

### 2. Theatre wedding v44 — used player master instead of +402 slot
Binary wedding (code 6) sets `v44 = dword_12CE914[134*localMaster + 402]` — a
SEPARATE person record's master dword (+1608 bytes from the player record), distinct
from `v43 = v36 = the player's master`. The recon assigned `out.master2 =
masterDword`. Since +402 is not derivable from the builder's params, **FIX**:
surfaced it as a parameter — added a 6-arg `Theatre_BuildCutsceneRecord` taking
`weddingPartnerMaster`; the 5-arg form delegates with it defaulted to `masterDword`
(other events set v44=v36/Birth or leave it unused -> default preserves their
goldens).
- Goldens added: `WeddingMaster2IsPartnerMasterSlot` (+ existing `WeddingRecord`
  extended to assert the 4-arg default master2==masterDword).

## Build / tests
- All touched/reviewed play TUs compile clean against the lib headers
  (cutscene_recon2_theatre/movie/tutorial, text_recon3_itemlabel,
  ui_recon4_hud_surface, ui_recon5_panels).
- `cutscene_recon2_test` built in isolation and run: **119 checks, 0 failures**
  (includes the 4 new golden tests).
- NOTE (not my cluster): the full `build/` is currently RED due to a duplicate
  `enum class MissionCompletionOutcome` defined in BOTH `src/world/history_mission.h`
  and `src/world/mission_rules.h` (both files `M` in git from a concurrent wave-16
  world-cluster agent). That ODR clash is in `src/world/`, outside W16-SLICE
  ownership — flagged for the world-cluster owner. My cluster's objects and tests
  build/pass independently.

## Result
The cluster's binary-grounded reconstructions are 1:1 with the live decompile. Two
genuine divergences in the theatre event-record assembly (duel stored column;
wedding +402 master slot) fixed to match the binary and golden-pinned. All ConvertX
float->int sites in the cluster confirmed truncate-toward-zero. Goldens for valid
input remain byte-identical.
