# Wave-21 — Recruitment MODAL WINDOW DRIVERS (full 1:1 bodies, wired)

**Agent:** W21-RECRUIT · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Completes the three recruitment modal-window drivers that wave-20's coverage audit
flagged still-missing (their bodies were not cited in any `.cpp`; the simplified
shells in `gui/personnel_gui.cpp` dropped the load-bearing logic). They are now
reconstructed in FULL, 1:1 from the Hex-Rays decompile, inside the wave-19 module
`gui/recruit_office.{h,cpp}` with inline `0xADDR` provenance, and the internal
offer→pick→confirm call chain is wired.

## Owned files (edited / added)
- `src/gui/recruit_office.h`  (extended: `RecruitWindowHooks`, `HeHandler`, the 3 entry decls)
- `src/gui/recruit_office.cpp` (extended: the 3 full driver bodies + their hook table)
- `tests/unit/recruit_office_estate_test.cpp` (+9 window-driver tests; suite now 82 checks, all green)
- this doc

## Reconstructed functions (RECONSTRUCTED 1:1)

| addr | bytes | symbol | what was reconstructed |
|------|------:|--------|------------------------|
| 0x55d990 | 380 | VIBE_Recruit_RunHireConfirmDialog | cost = `ComputeRecruitmentCost(dword_12CE914[134*word_63CC5C])`; form `privillegien\werbung2` + center/select; rich-string 0x18E5; the do/while frame pump dispatching `dword_75BF38`: 1210 → queue `Command_QueueRequestSlotReset28` for the player id, **spin** on `Command_GetPacketStatusById` calling `Amt_RefreshGuildState` until done, result=1, quit; 1155 → quit; cancel edge (`dword_672230`) → quit; Form_Destroy + `Input_ClearMouseButtonsByMask(17)`; returns the 0/1 confirm result. |
| 0x55db0c | 754 | VIBE_Recruit_RunCandidatePickWindow | **candidate-list build** via the wave-19 `RecruitCollectNearbyCandidates` leaf; form `privillegien\werbung1` + the two selects + rich-string 0x18E3; the **3-column grid geometry** (`gap=panelW-3*cardW`, `quarter=gap/4`, `half=gap%4/2`; per card `x=half+10*(col-1)+col*cardW+quarter*(col+1)`, `y=130*(i/3)+5` with `col=i%3`); per-card `FindRecordById` + `Person_ResolveStatusFlags` + `ComputeRecruitmentCost` + formatted cost label (`cardH+y+43` / `cardW/2+x`) + `Hud_BuildPersonCard`; the **selection state machine** (`dword_672230` doubles as the row index: nonzero → quit, zero → scan from row 0; on a click matching a card object id → hide children, run `RunHireConfirmDialog`, success → result `-110` & quit, else re-show children & keep scanning); returns 2 default / -110 on hire. |
| 0x55de00 | 1544 | VIBE_Recruit_RunRecruitmentOfferWindow | **He-handler match** (FindFirst(kind 65) → iterate FindNext until `handler.entityId == entity+1`); **mode select** (1 no-handler / 2 pending / 3 `byte187==1` rejected / 4 `byte186==1` offer-pending, last wins); kind!=6 → no-handler returns 32 (the `+8`-flag slot-reset/16 sub-branch documented below); kind==6 & mode 1 → forwards to `RunCandidatePickWindow`; mode 2/3/4 → opens `privillegien\werbung2`, per-mode body (mode 2 clamped rank-cost delta + 0x18E7; mode 3 0x18E3+0x1931; mode 4 office-rank, the three offer descriptors `goodType[i]=byte188[i]` / `goodCount[i]=dword47[i]`, header 0x18F4 + the four buttons 0x18F5..0x18F8); the **offer decision logic + state mutation**: `OfferFindSlotIndex` over the 4 buttons; slot 3 → decline (msg 6394, clear `byte186`, `QueueRequestEntity29`→`dword33`, message box, quit); slot 0..2 → bribe gated by `Dialog_CheckResourceAmount(goodCount[slot])` → roll loyalty bonus (`OfferBonusRoll` from office rank), msg 6393, **accumulate `byte184 += b.count`**, if `>= byte185` → `GameTime_Advance`, clear `byte186`, `QueueRequestEntity29`→`dword33`, `EnqueueCmd15(-1, entityId, goodCount[slot], rate)`, message box, quit; Form_Destroy; returns 2/16/32/-110. |

### Deterministic kernels (golden-pinned, inline addrs)
- `OfferComputeMode` (0x55de22..0x55de7e), `OfferBonusRoll` (0x55e263..0x55e39d —
  `rank<=0`: count=rand(3)+1, idx=rand(5); `rank==1`: idx=rand(5)+5; `rank==2`:
  idx=rand(4)+9; else idx=rand(3)+12, count=clamp(rank,3)), `OfferFindSlotIndex`
  (0x55e029..0x55e04b).

## Leaves reused (extern / existing reconstructions — no ODR redefinition)
- `gui::RecruitCollectNearbyCandidates` (recruit_office.cpp, wave-19) — the candidate
  COLLECTION, now actually invoked by the pick window (was only a header reference).
- `sim::PersonFindRecordById` @0x58bc6c (entity.cpp), `Person::marker` / `Person::id`,
  `g_persons` / `g_personIds` (entity.h).
- The boundary leaves (form create/center/select/destroy, `RunFrameLoop` @0x4c09a0,
  text render, `Hud_BuildPersonCard`/`AddCenteredLabel`, the command-queue funcs
  `SlotReset28`/`GetPacketStatus`/`Entity29`/`EnqueueCmd15`, the He handler probe
  `FindFirst/Next`, `GameTime_Advance`, `Dialog_*`, `RandomModulo`, `ComputeOfficeRank`,
  `ComputeRecruitmentCost`) are routed through the new `RecruitWindowHooks` struct
  with inert defaults (the house pattern). They are the SDL/Vulkan + engine-subsystem
  BOUNDARY, not game logic.

## He-handler record (HeHandler)
The original handler is the 332-byte `byte_11D6040 + 332*i` "Handlung" record. The
drivers only touch: `+0x04` entityId, `+0x84` dword33 (written), `+0xAC` dword43,
`+0xB0` dword44 (candidate id), `+0xBC` dword47[3] (good counts) / byte188[3] (good
types), `+0xB5/+0xB6` byte181/182 (rank-cost lo/hi), `+0xB8/+0xB9` byte184/185
(loyalty / threshold), `+0xBA/+0xBB` byte186/187 (offer-pending / rejected). `HeHandler`
exposes exactly those — everything else is opaque (the full He table lives in the
He/event subsystem, reached via the `heFindFirst/heFindNext` hooks).

## DEFERRED / boundary (documented, not faked) — rule 8
- The **frame-loop / form / HUD / He-table / command-queue** plumbing is the SDL/Vulkan
  (rules 3–5) + engine-subsystem boundary, routed through `RecruitWindowHooks`. The
  drivers reconstruct the GAME LOGIC those leaves are wrapped in.
- The offer window's **kind!=6 + no-handler `+8`-flag** sub-branch (0x55dec5: resolve
  `FindRecordById(*(entity+532))`, check its `+8` byte, on set queue a slot-reset and
  return 16) reads the entity's bound-person link and the He registry state that are
  not modeled as plain arrays at this seam; the control flow + the dominant `return 32`
  path are reconstructed, and the slot-reset return-16 is documented. No fake value.
- The bribe-bonus rank source (`v33`, a leftover edx after `FindRecordById` in the
  decompile) is bound to the candidate's **office rank** (`personComputeOfficeRank`),
  which is the only candidate-rank value the offer window computes and the faithful
  reading of the rank-bracket switch (0x55e26e..0x55e39d). Flagged here for the audit.

## Wiring (rule 13)
- **Internal (done in my file):** `RunRecruitmentOfferWindow` (mode 1) →
  `RunCandidatePickWindow` (0x55e0ba); `RunCandidatePickWindow` (click) →
  `RunHireConfirmDialog` (0x55dd97); pick window → `RecruitCollectNearbyCandidates`
  (0x55db2e). All three are now in one live call chain.
- **External handoff (bind-site I do NOT own):** the real top caller is
  `VIBE_ContextAction_AssignTask` @0x56efb0 (`src/sim/interaction_handlers.cpp`,
  `ContextAssignTask`), which at `ev->mode == 3` does
  `return RunRecruitmentOfferWindow(actor) | 2`. That branch currently stands in with
  `Privilege(kPrivChangeProfession, actor, ev)`. **One-line handoff:** replace it with
  ```cpp
  // mode 3: VIBE_Recruit_RunRecruitmentOfferWindow(actor) | 2  (0x56eff0)
  int r = gui::RecruitRunRecruitmentOfferWindow(actorMarker, recruiterMarker, recruiterByte9);
  return static_cast<char>(r | 2);
  ```
  where `actorMarker` is the ContextActor record (the original's eax/a1), and the
  recruiter marker/+9 byte feed the candidate-collect leaf.
- **Hook install (bind-site I do NOT own):** install `RecruitWindowSetHooks(...)` in the
  real wiring layer next to the existing `SetPersonnelGuiHooks` call in
  `src/sim/wire_misc2.cpp`, forwarding each field to its reconstructed leaf
  (`recruitComputeCost → sim::RecruitComputeRecruitmentCost`, `randomModulo →
  util::RandomModulo @0x58b89c`, the form/frame/command/He fields → their `IPlatform`/
  command-subsystem adapters). The headless default keeps the drivers runnable.

## Verification
- `src/gui/recruit_office.cpp` compiles clean (`-std=c++17 -I src -I include`, no warnings).
- `libguild.a` links with the module; `recruit_office_estate_test` builds + runs:
  **82 checks, 0 failures** (14 wave-19 cases + 9 new window-driver cases):
  hire-confirm confirm/cancel/cancel-edge; candidate-pick grid geometry + collect +
  default result; offer no-handler-kind!=6 → 32; offer kind==6 mode-1 → pick → 2;
  offer mode-4 bribe-accept loyalty mutation; bribe meets-threshold → GameTime advance;
  decline clears offer + message box.
- A concurrent agent's in-progress `src/play/gamelogic_recon.cpp` was mid-edit and
  fails to compile (`IGameLogicHooks::stateFinalize` missing) — NOT my file; the lib
  links via its prior object. My module is independently clean.

## 1:1 fidelity callouts
- The candidate-pick scan uses `dword_672230` BOTH as the quit flag (nonzero) AND as
  the row-start index (when zero) — reproduced verbatim from 0x55dd55..0x55dd7f.
- The grid `quarter = (i64 gap) >> 2` collapses to `gap/4` for the in-range widths
  (matches `CandidateGridCellPos` in personnel_gui.cpp); `half = gap%4/2`.
- The loyalty accumulation is an 8-bit add (`byte184 += b.count`) with a signed
  `>= byte185` threshold compare, exactly as 0x55e2ba..0x55e2d2.
- The hire packet spin (`while(!GetPacketStatus) RefreshGuildState`) is preserved;
  the headless default returns "done" so the spin terminates.
