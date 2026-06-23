# Wave-19 — AI needs/score master table builder (the "AI brain" data root)

**Agent:** W19-AINEEDS · **Date:** 2026-06-15 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the INI/DFN-driven AI **needs scorer table builder** — the data root of
the Meister/NPC decision brain — 1:1 from the binary, and wires its shipped read path
over the real install.

---

## What was reconstructed

| addr | name | status | where |
|------|------|--------|-------|
| 0x4764e8 | `VIBE_AiNeeds_BuildScoreTable` (7961 b, 2605 insns) | **DONE 1:1** | `src/sim/ai_needs.{h,cpp}` |
| 0x468a40 | `VIBE_AiMethod_LoadDataFile` (read path) | **DONE 1:1** | `AiNeeds_LoadDataFile` + `AiNeeds_OverlayFromDfn` in `ai_needs.cpp` |
| 0x468f6c | `VIBE_AiMethod_RegisterFromIni` | **REUSED** (already done) | `src/sim/aimethod_recon3_registry.{h,cpp}` |
| 0x4794e4 | `VIBE_AiNeeds_LookupAttributeIndex` | **REUSED** (already done) | `aimethod_recon3_registry.cpp` |

### 0x4764e8 — BuildScoreTable
The function is a giant unrolled registrar: **60 near-identical blocks**, one per AI
method. Each block:
1. `memset` a 148-byte stack record (rep stos);
2. writes the method's FIXED fields:
   - `+0` (byte) **id** — an explicit immediate (NOT sequential; ids 12/13/60 are
     reordered in build order);
   - `+1` (32) **name** (code-literal, NUL-copied);
   - `+36` (dword) **scorer** fn (var_88), `+40` **execA** fn (var_84),
     `+44` **execB** fn (var_80);
   - `+144` (word) **flag** (var_1C, category/eligibility mask);
3. calls `VIBE_AiMethod_RegisterFromIni(record /*eax*/, haveIni /*edx = a1*/)` which
   fills the desire slots `+48..+143` from the `[name]` INI section and commits the
   record to the catalog `byte_B57210[148*id]`;
4. on a 0 return (`test eax,eax; jz`), **aborts** the whole build → `return 0`.
After all 60, `return 1` (0x4783ef `mov eax,1`).

The full 60-entry table (id, name, scorer/execA/execB VA, flag) was extracted from the
binary via `py_eval` over the per-block stores and is golden-pinned in
`kAiNeedsMethodDefs`. The weight strings the brief flagged are all present and verified:
`DummyUnversehrtheit`, `DummyBeruf`, `Bildung`, `Bildung (Beruf)`,
`DummyRechtschaffenheit`, `DummySicherheit`, `DummyFortpflanzung`, etc.

`haveIni` is the eax argument, forwarded verbatim as RegisterFromIni's edx. The caller
`LoadDataFile` passes `dword_63C7D8` here.

### The INI / DFN data — found and reconstructed
- **`f3_ai_method.ini`** (ref `aF3AiMethodIni` @0x61a2bc) is the *authoring* INI read
  only when `dword_63C7D8 != 0`. It is **NOT shipped** with the real install (verified:
  not present anywhere under `europe_guild_1400_original`). That path BUILDS from INI and
  then WRITES the DFN.
- **`/gamedata/ai/ai_data.dfn`** (ref `aGamedataAiAiDa` @0x61a2d4) IS shipped as
  `Resources/gamedata/ai/AI_DATA.DFN` (1448 b). It is a **gzip stream** (magic `1f 8b 08`);
  decompressed it is exactly **4453 bytes = 61 records × 73 bytes**. On the shipped path
  (`dword_63C7D8 == 0`) the engine BuildScoreTables the fixed fields then READS the desire
  slots back from this DFN. The 73-byte on-disk record is `id(1) name(32)
  [shortAttr(1) shortChange(f32)]×4 [longAttr(1) longChange(f32)]×4` — exactly the
  `kAiMethodFieldSchedule` (18 fields) already in the registry module. After read,
  `MemMove(+80, +48, 32)` mirrors the 4 short slots into the prev vector (0x468f39).

The DFN read is the path reconstructed for "load over the real install". The gzip framing
uses the already-reconstructed `guild::compress::Gunzip`; the INI parse (when used) uses
the already-reconstructed `guild::io::IniProfile` (NOT a tech boundary).

---

## Fn-ptr identity note (not a cheap analogue)

The original stores live 32-bit gilde.exe code addresses at +36/+40/+44 (the planner
dispatches via `tbl[9]`/`tbl[10]`). A portable build cannot store gilde.exe VAs as
callable pointers, so the catalog records the **original gilde.exe addresses as opaque
u32 tokens** — golden-pinnable and the exact dispatch identity. They are NOT serialized
to the DFN (the DFN holds only id+name+desire slots = 73 b/record — verified against the
shipped file), so this is lossless for the loader. Binding these tokens to the
reconstructed `VIBE_Interaction_*` / `VIBE_NpcAction_*` / `VIBE_AiAction_*` evaluators is
a planner-dispatch concern (a future wave when those evaluators' shapes are unified);
the table preserves the identity needed to do it.

---

## Wiring (rule 13)

Live call chain: `VIBE_App_InitEngineAndScriptCommands @0x528560` →
`VIBE_GameLogic_InitGuardState @0x4520d0` → `VIBE_AiMethod_LoadDataFile @0x468a40` →
`VIBE_AiNeeds_BuildScoreTable @0x4764e8`.

- `AiNeeds_LoadDataFile(decompressedDfn, len, out)` reconstructs LoadDataFile's shipped
  read path end-to-end (BuildScoreTable + 61-record overlay + the loop's `return 1` only
  after all 61) and returns the boolean `InitGuardState` consumes as `aiDataFileLoaded`.
- **Handoff (documented, files owned by other modules):** `InitGuardState` lives in
  `src/world/guildstate_recon.cpp` (its `aiDataFileLoaded` parameter is exactly
  LoadDataFile's return); the engine-init seam routes InitGuardState through the
  `gameLogicInitGuardState` std::function hook in `src/app/engine_init_app_recon.cpp`. To
  complete the live wiring, that hook should gunzip `Resources/gamedata/ai/AI_DATA.DFN`
  via the VFS, call `AiNeeds_LoadDataFile`, and feed the result to
  `GameLogicInitGuardState`. The entry point for that is `AiNeeds_LoadDataFile`.

---

## Tests

- `tests/unit/ai_needs_test.cpp` — **1312 checks, 0 failures**. Golden-pins the 60-entry
  definition table (ids are a permutation of 1..60; first/last/non-sequential rows;
  Bildung scorer; flags 0xff/0x19), the no-INI build (fixed fields land at `catalog[id]`,
  slot 0 unused, return 1), the INI-binder build (desire fill + short→prev mirror), the
  DFN overlay (field schedule + mirror + short-read stop), and `AiNeeds_LoadDataFile`
  (full → 1, short → 0).
- `tests/e2e/ai_needs_e2e_test.cpp` — **1159 checks, 0 failures** over the REAL shipped
  `Resources/gamedata/ai/AI_DATA.DFN`: gunzip → overlay → assert every method's on-disk
  name/id equals the builder's definition-table row, all desire attr indices are valid
  (-1 or 0..13), prev mirrors short, and the golden `DummyUnversehrtheit` / `Bildung`
  desire weights match the on-disk values
  (Bildung short = BERUF+20, APS−10, BILDUNG+40, SICHERHEIT+15). Guarded: clean skip when
  the install is absent (honors `GUILD_GAME_DIR`).

Built/ran standalone (the shared `libguild` is currently red from *other* waves'
in-flight untracked files — `src/script/script_vm.cpp`, `src/sim/character_ai.cpp` — not
touched here; `ai_needs.cpp` compiles cleanly and has no ODR clashes).

---

## Genuine leaves / deferred

- The 60 scorer/execA/execB **evaluator functions** themselves (`VIBE_Interaction_*`,
  `VIBE_NpcAction_*`, `VIBE_AiAction_*`, `VIBE_AiScore_ComputeRelationWeighted @0x4796b0`,
  `VIBE_AiMethod_EvalMoveToBuilding @0x46ac24`, …) are the planner's per-method behavior
  bodies — a separate, large cluster (the `Ai_CalcMeister*` / `Interaction_*` reconstructions
  in other modules). BuildScoreTable only *references* them by address; their bodies are
  out of scope for the table builder and are recorded as identity tokens (not stubbed,
  not faked). `VIBE_AiScore_ComputeRelationWeighted` and `EvalMoveToBuilding` already have
  partial reconstructions in `aimethod_score_ai_recon2`.
- The `f3_ai_method.ini` WRITE path (DFN authoring) is reachable only with the unshipped
  authoring INI; the shipped READ path is fully reconstructed. The INI-build branch logic
  is reconstructed (`AiNeeds_BuildScoreTable(haveIni=true, binder)`), wired to
  `IniProfile` via the `AiNeedsIniBinder`.

---

## Files

- `src/sim/ai_needs.h` / `src/sim/ai_needs.cpp` (new)
- `tests/unit/ai_needs_test.cpp` (new)
- `tests/e2e/ai_needs_e2e_test.cpp` (new)
- `progress/ai-needs-wave19.md` (this file)
