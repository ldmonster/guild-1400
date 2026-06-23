# New-Game flow AFTER the city is chosen

The funnel the original runs once a city is confirmed in `VIBE_Menu_RunChooseCity`
(`gui::Menu_RunChooseCity` already reconstructs the call site):

```
ChooseCity confirm (button 1210 / Enter) , new game (a1 == 0):
    if (VIBE_Menu_ChooseCharacterIntroVariant())   // 0x52e4e0  <-- THIS NODE
        if (VIBE_Menu_RunChooseHistory())           // 0x52d684
            -> proceed (dword_631614 = 1, v80 = 1)
```

`RunChooseHistory` is itself the parent of the deeper character-creation sub-tree:
`VIBE_Mission_RunChooseHistoryDialog 0x538b28`, `VIBE_Menu_RunChoosePlayer 0x52ccd8`,
`VIBE_Menu_ChooseCharacterIntro 0x52e3d8`, `VIBE_Menu_RunChooseCharacter 0x52bcd4`,
`VIBE_Menu_ChooseProfession 0x52c50c`, plus the `B_Persoenliches.esc` / `A_Stadtwahl.esc`
cutscenes.

---

## 2026-06-09 — VIBE_Menu_ChooseCharacterIntroVariant @0x52e4e0 (reconstructed 1:1)

The screen the funnel runs immediately after the city pick. Form
`menu\choosecharacter_intro` (window 128,72,441,490); its title + six radio options are
injected by `VIBE_Text_RenderRichString(0x16CC)` (the `.form` member itself is an empty
window shell — confirmed by parsing `Resources/forms.BIN`: `Menu/CHOOSECHARACTER_INTRO.form`
= 1 window, 0 objects).

**Faithful control flow** (decompile + the 0x52e60f tail, which the Hex-Rays `return v5`
obscured — recovered from the asm: `eax = 1` only on a committed radio pick):
- build: `GameTick_Finalize(form)` → `Form_CenterChildWindows` → seed
  `dword_63C744 = (u8)byte_12335BA` → `Text_RenderRichString(0x16CC)` → six
  `Form_GetChildObjectId(form,0,base+k)` → `RadioGroup_Create(6, id0)` →
  `Selection_Update(group, seed)`.
- loop `while (GameLogic_RunFrameLoop(198, group))`: window-close (`dword_672230` /
  `byte_67225C==1`) → `dword_631614=1`, ret 0; OK (`dword_75BF38==1210`) or Enter
  (`byte_67225C==28`) on radio member k (`dword_62D22C==id_k`) → `dword_63C744=k`, ret 1,
  exit; back button (`1155`) → exit, ret 0.
- persist `byte_12335BA = dword_63C744`; `RadioGroup_FreeSurface`; `Form_Destroy`; return eax.

Returns **1** on a committed pick (chains to `RunChooseHistory`), **0** on cancel; the
selection (0..4) round-trips through `byte_12335BA`. The 6-member radio group has only the
first 5 members mapped to variants 0..4 (the 6th is a non-selecting form decoration), faithful
to the asm.

**Reconstruction**: `gui/choosecharacter_intro_run.{h,cpp}` — `Menu_RunChooseCharacterIntroVariant(state, rec, maxFrames)`
+ `CharIntroState` (the `byte_12335BA`/`dword_631614`/`eax` words) + `CharIntroRunHooks`
(host leaves: form/radio build, `RunFrameLoop`, and the `dword_62D22C`/`dword_75BF38`/
`byte_67225C`/`dword_672230` input edges) with inert defaults — the same headless-testable
shape as `gui/options_run` and `gui/choosecity_run`.

**Tests**: `tests/unit/choosecharacter_intro_run_test.cpp` (5 golden control-flow vectors,
49 checks): OK commits the clicked variant (all 0..4), Enter commits identically, the seed
is pre-selected + round-trips on cancel, window-close cancels, OK with no radio active does
not commit. Portable suite 1165/1165.

## 2026-06-09 — native playable difficulty screen + wired into the flow

Identified what the screen IS: `Text_RenderRichString(0x16CC)` resolves to text entry
`_M0_DIFFICULTY+0` in `Resources/textbin_deutsch.BIN` (`Text_M_Missionsziele.res`, base
5826) — the screen is the **difficulty picker**:
- heading «Уровень сложности», prompt «Пожалуйста, выберите уровень сложности.»
- `%ia[очень легкий] %ia[легкий] %ia[нормальный] %ia[тяжелый] %ia[очень тяжелый]` (the five
  selectable levels → `byte_12335BA` 0..4) + `%in[назад]` (the non-selecting back option,
  the radio group's 6th member, the `1155` back button). Exactly the radio-of-6 the headless
  reconstruction drives.

Built `play/sdl_charintro_screen.{h,cpp}` — the native (Vulkan/SDL) front, the analogue of
`sdl_options_screen` for `options_run`:
- `ParseDifficultyMarkup` (pure): extracts heading / prompt / the six option rows from the
  real `_M0_DIFFICULTY` markup (`$[heading]`, `$X` control tokens, `%i<sel>[label]`).
- `LoadCharIntroContent`: loads the real markup by NAME (`_M0_DIFFICULTY+0`, index-order
  independent) from `textbin_deutsch.BIN`; English fallback when assets are absent.
- `CharIntroLayout` + `RenderCharIntroFrame`: the form window (128,72,441,490 scaled) panel
  + the real menu backdrop (gilde.gfx) + CP1251 title/prompt/rows with hover + seed highlight.
- `RunCharIntroScreen`: the SDL frame loop (render → present → mouse-edge hit-test → click a
  level = commit / back row or ESC = cancel), mirroring the 1:1 commit logic.

**Wired** (rule 13): `native_main_menu.cpp EnterChooseCity` now runs the difficulty screen
between `RunCityScreen3D` (city confirm) and `RunCharCreateScreen`; a cancel/back aborts the
new-game chain back to the menu (the original `if (ChooseCharacterIntroVariant()) …`).

**Tests**: `charintro_markup_test` (parse the exact shipped CP1251 markup, 21 checks),
`charintro_screen_e2e_test` (real content loads, layout/hit-test, non-blank render — asset
guarded). Both presets 1167/1167.

**Deferred / next (named, not faked — rule 8):**
- The chosen difficulty (`byte_12335BA`) is returned by the screen but not yet threaded into
  a `NewGameParams`/sim field (no consumer wired downstream yet).
- Exact form-window backing sprite + full CP1251 5×7 glyph coverage (some authored glyphs are
  sparse) — cosmetic, the existing font deferral.
- **`VIBE_Menu_RunChooseHistory @0x52d684`** and its sub-tree (ChoosePlayer / ChooseProfession
  / ChooseCharacter / the history dialog) — the remainder of the new-game funnel.

## 2026-06-09 — VIBE_Menu_RunChooseHistory @0x52d684 (perspective screen, 1:1 + playable)

The screen the funnel runs right after the difficulty pick. Form `Menu\CHOOSEHISTORY`; its
title + options are `Text_RenderRichString(0x16CD, 5839,5840,5841, 5839,5840,5841, 1155)` ==
`_M0_HISTORIE` with the three mode names `_M0_HISTORIE_MODUS+0..2` substituted into the
`%ia[%s]` slots — a radio of four (three perspective modes + a `%in[Назад]` back member):
- «Фактическое историческое описание» → History flag **1**
- «Индивидуальная история игрока» → flag **2**
- «Без исторической справки» → flag **0**

Recovered mappings: seed `dword_12335AC` {0,1,2} → radio index {2,0,1}; commit id0→flag1,
id1→flag2, id2/Enter→flag0 (`VIBE_History_SetActiveFlag`); re-select from the active flag
(0→idx2,1→idx0,2→idx1); back = button 1155. On a mode pick the original runs
`Mission_RunChooseHistoryDialog` (0xFF = cancel → stay on screen) then the character spine
(`RunChoosePlayer → ChooseCharacterIntro → RunChooseCharacter | ChooseProfession`, with a
v9/v10 retry loop) and returns its v8 (1 = start).

- **Headless reconstruction**: `gui/choosehistory_run.{h,cpp}` — `Menu_RunChooseHistory` +
  `ChooseHistoryState` + `CharHistoryRunHooks`. The radio screen + seed/flag mapping is exact;
  the post-dialog character spine is modeled at the host boundary as `RunCharacterSpine()`
  returning the final v8 (to be expanded into RunChoosePlayer/ChooseProfession/ChooseCharacter
  reconstructions next). Tests: `choosehistory_run_test` (5 vectors, 44 checks): seed mapping,
  each mode→flag, dialog-cancel-stays, spine-abort, back.
- **Native playable screen**: `play/sdl_choosehistory_screen.{h,cpp}` — loads the real markup
  + mode names by NAME, substitutes the `%ia[%s]` slots, and renders/loops via the difficulty
  screen's shared primitives (`CharIntroContent`/`CharIntroLayout`/`RenderCharIntroFrame`);
  returns the picked History flag. Test: `choosehistory_screen_e2e_test` (real names
  substituted, row→flag mapping, non-blank render).
- **Wired** (rule 13): `native_main_menu` runs it after the difficulty screen; a cancel aborts
  the chain; the chosen flag is carried into `NewGameParams.historyFlag` (the existing field).

Both presets 1169/1169. The character spine (player/profession/character) remains the next
sub-tree; the difficulty value (`byte_12335BA`) is still not yet threaded into the sim.

## 2026-06-09 — VIBE_Menu_ChooseCharacterIntro @0x52e3d8 (the spine router, 1:1)

The router the character spine runs after `RunChoosePlayer`. Same form
`menu\choosecharacter_intro`, but the markup is `_M0_PERSOENLICH_CHARAKTER` («Выберите
предков!» — choose your ancestors): a two-button choice —
- `%ia[Задать генеалогическое древо]` (define the family tree → OK/1210/Enter → **return 1**
  → the dynasty scene `RunChooseCharacter @0x52bcd4`)
- `%in[Автоматически]` (automatic → back/1155 → **return 0** → `ChooseProfession @0x52c50c`)
- window-close / ESC(1) → **return -1** (abort; the caller steps back).

The return is purely button-driven (the radio-of-2 is visual); v2 inits 0. Reconstructed as
`Menu_RunChooseCharacterIntro` in `gui/choosecharacter_intro_run.{h,cpp}` (sibling of the
difficulty `…Variant`, sharing the `CharIntroRunHooks` leaves). Tests:
`choosecharacter_intro_router_test` (5 vectors, 11 checks): OK/Enter→1, back→0, window/ESC→-1,
default→0. Both presets 1170/1170.

The deterministic spine LOGIC is already reconstructed in `gui/charcreate.*` (dynasty actor
clicks `ChooseCharacter_ApplyActorClick`, talent caps, profession-id click resolution) and
`gui/newgame_setup.*` (profession/wappen button geometry + apply). What remains are the
remaining screen-level RUN functions / native screens:
- **`VIBE_Menu_RunChoosePlayer @0x52ccd8`** — the 6-page identity wizard (Vorname → Nachname →
  Geschlecht → Glauben → Wappen → confirm), with text entry + the page state machine + the
  `[Network]` gilde.INI read/write. The first spine screen; biggest remaining playable piece.
- **`VIBE_Menu_RunChooseCharacter @0x52bcd4`** — the 3D dynasty/ancestry scene (nine clickable
  ancestor actors) + `ChooseCharacterTalent @0x52b088` + the portrait/model picker.

## 2026-06-09 — VIBE_Menu_RunChoosePlayer @0x52ccd8 (identity wizard page machine, 1:1)

The first character-spine screen. Form `Menu\CHOOSEPLAYER` (7 windows) is a PAGE WIZARD
(`v2` = 0..5, one window per page): page 0 Vorname (text), 1 Nachname (text), 2 Geschlecht
(radio 2), 3 Glauben (radio 2), 4 Wappen (8 buttons), 5 = confirm. Recovered control flow:
the `[Network]` identity is read from gilde.INI on entry; the close edge (window-close /
the back-button `v61` / ESC) STEPS BACK one page when `v2>0`, else exits (result 0); each
page's own input advances it (Enter commits a text page, a radio/wappen click commits the
others); page 5 (`case 5`) runs unconditionally the frame `v2` reaches it — it commits the
wappen into `dword_122F4A4`, sets `v70=1` and arms `dword_631614` (so picking the wappen
ends the wizard); the identity is written back to gilde.INI on exit; returns `v70`.

- **Headless reconstruction**: `gui/chooseplayer_run.{h,cpp}` — `Menu_RunChoosePlayer` +
  `ChoosePlayerState` (the `[Network]` block + `v2`/`v70`) + `ChoosePlayerRunHooks`. The page
  state machine + per-page value collection is exact; the per-page input (text-field
  contents / radio / wappen click) and the INI read/write are host leaves. The per-frame page
  interaction is modeled as `PlayerPageAction {kNone,kAdvance,kBack}`. Tests:
  `chooseplayer_run_test` (4 vectors, 20 checks): full walkthrough commits + collects all five
  values, back-from-page-0 cancels, back steps one page, INI seeded + written. Both presets
  1171/1171.

- **`VIBE_Menu_RunChooseCharacter @0x52bcd4`** — the 3D dynasty/ancestry scene (the spine's
  "manual family tree" branch from the router).

## 2026-06-09 — native player wizard + SDL text input

- **SDL text-input capability** (rule 4: Win32 WM_CHAR → SDL): added
  `shim::IPlatform::pollText()` (typed UTF-8 since the last call; default empty). The SDL
  backend (`SdlVulkanPlatform`) captures `SDL_TEXTINPUT` in `pumpMessages` (and
  `SDL_StartTextInput` on window create); `ScriptedPlatform` got a `queueText()` for tests.
- **Native wizard** `play/sdl_chooseplayer_screen.{h,cpp}` — drives the headless
  `gui::Menu_RunChoosePlayer` state machine through `ChoosePlayerRunHooks`, rendering the six
  pages with the real `_M0_PERSOENLICH_*` labels (Ваш герой / Имя / Фамилия / Пол:
  Мужской·Женский / Вера: Католическая·Катарическая / Выберите Ваши цвета): the name pages
  use `pollText()` + Backspace (`ApplyTextEdit`, with a live caret), the gender/faith pages
  are 2-row radios, the wappen page is an 8-cell grid (real decoded gfx 1342+i sprites when
  present). `ApplyTextEdit` is a pure, unit-tested helper.
- **Wired** (rule 13): `native_main_menu` runs it after the perspective screen; the collected
  identity (firstName / familyName / gender / faith / wappen) flows into `NewGameParams` and
  the downstream `RunCharCreateScreen` config.
- **Tests**: `chooseplayer_textedit_test` (append/backspace/filter/cap, 7 checks),
  `chooseplayer_screen_e2e_test` (a per-frame scripted platform types a name, advances the
  text pages with Enter, clicks the gender/faith rows + a wappen cell → commits with the
  collected identity, 9 checks). Both presets 1173/1173.

The current playable funnel: **main menu → city (3D) → difficulty → perspective → player
identity (name/gender/faith/wappen) → profession**. Wappen is currently asked by both the
player wizard and the legacy `RunCharCreateScreen` (a redundancy to reconcile when that
screen is folded in).

## 2026-06-09 — VIBE_Menu_RunChooseCharacter @0x52bcd4 (dynasty run loop, 1:1)

The "manual family tree" branch (from the ChooseCharacterIntro router). A 3D scene
(reusing the `Menu\CHOOSECITY_HEADER` form + Sky_Mittel_02 + band-4 lighting) with nine
clickable animated ancestor actors. Clicking actors fills the six dynasty slots
(dword_122F258[0..5]: paternal grandfather/grandmother, maternal grandfather/grandmother,
father, mother) — each click records the actor's profession code into the next free slot of
the actor's gender parity (even=male, odd=female) and plays its animation. When all six are
filled the scene chains: `if (!ChooseCharacterTalent())` → abort (return 0); else
`if (ChooseProfession() && BuildCharacterPreviewScene())` → start (return 1); else re-show
the form and retry the chain.

- **Headless reconstruction**: `gui/choosecharacter_run.{h,cpp}` — `Menu_RunChooseCharacter`
  + `ChooseCharacterState` + `CharSceneRunHooks`. The run loop drives the dynasty slot-fill
  LOGIC already in `gui/charcreate.*` (`ChooseCharacter_ApplyActorClick` / `DynastyTable` /
  `ChooseCharacter_IsComplete`); the scene render, the actor pick, and the Talent/Profession/
  Preview sub-screens are host leaves. Tests: `choosecharacter_run_test` (5 vectors, 25
  checks): full six-slot fill → start, wrong-parity click ignored, talent-cancel aborts,
  window-close-before-complete cancels, profession-fail-retries-then-starts. Both presets
  1174/1174.

**Deferred (named, not faked — rule 8):** the NATIVE 3D dynasty scene needs the
character-animation subsystem — the nine `VIBE_Character_CreateMenuDummyActor` actors are
skinned-mesh animated character models (`.baf`/skeletal animation), which is not yet
reconstructed. The scene chrome (form/sky/pick) can reuse the city-screen 3D infrastructure,
but the animated actors are the blocking piece; the run loop + dynasty logic above are ready
for it. The Talent (`0x52b088`) and Preview (`0x52b6b8`) sub-screens' run loops are likewise
next (their commit logic is in `gui/charcreate.*`).

### Status: the new-game funnel's screen LOGIC is now reconstructed end to end
city → difficulty → perspective → (player identity | dynasty) → profession, each with
golden-tested 1:1 control flow; the difficulty / perspective / player screens are natively
playable, and the dynasty scene awaits the character-animation subsystem.

**2026-06-11 update:** the "difficulty returned but not threaded" deferral is closed —
`gui::NewGameParams` gained `difficulty` (byte_12335BA), `play::NativeMenuResult` now
carries the full `NewGameParams` block through `EnterChooseCity`, and the session-start
commit that consumes it (`VIBE_Command_EnqueueInheritanceTransfer @0x5336f0` + the
InitOrLoadSession start-gold block) is reconstructed as `play::ApplyNewGameParams`
(src/play/newgame_apply.*). See progress/newgame-apply.md.

## 2026-06-09 — VIBE_Character_CreateMenuDummyActor @0x52af64 (dynasty actor factory, 1:1)

The single-actor factory the dynasty run loop (`RunChooseCharacter @0x52bcd4`) calls nine
times — one animated ancestor per clickable scene dummy. Faithful 1:1 port of the control
flow + the facing-yaw math:
1. `VIBE_Object_FindByHandle(0,256,name,0,model)` → the scene dummy node (null → return null).
2. `VIBE_Transform_PointThroughBoneChain(dummy, dummy+19, &v12)` → dummy world position.
3. `VIBE_Character_CreateFromModel(model)` → the char actor (null → return null).
4. Facing yaw = `VIBE_Math_VectorAngleBetween(flt_5CA2B0={0,0,1}, RotateVectorByHierarchy(dummy,{0,0,1}))`;
   pushed as `{0, yaw, 0}` via `VIBE_Object_SetWorldTranslation(*(v6+52), …)`.
5. `QueryTerrainType(v6,0)`; node+512 = char back-ref; `PropagateDirtyFlag(node,1)`;
   `PreloadAniSet(v6,1,"bewegung/gehen")`; node+72 = 0; actor +416 = 0.6666667f
   (0x3F2AAAB3), +508 = -1 (dynasty unused-slot marker), +44 = 1 (active), char+512(byte) = 4
   (type); `StatusText_Register(node,0)`. Returns the char actor.

- **Reconstruction**: `sim/menu_actor.{h,cpp}` — `CreateMenuDummyActor(name, model, rec)` +
  `MenuActorHooks` (inert defaults) + `MenuActorRecord`. The facing math REUSES the already-
  reconstructed `guild::util::RotateVectorByHierarchy` (0x5c8990), `VectorAngleBetween`
  (0x5ca334), and `PointThroughBoneChain` (0x5c8b38) — not redefined. The genuine scene-graph
  / character-subsystem leaves (object lookup, char creation, world-rotation, terrain query,
  dirty flag, gait preload, status-text) are routed through the hook table (mirrors the
  `CharRender3Hooks` pattern in `sim/character_render3.*`). Recovered constants: `flt_5CA2B0`
  = {0,0,1} (`get_bytes @0x5ca2b0`), `aBewegungGehen` = "bewegung/gehen", scale 0x3F2AAAB3.
- **Tests**: `menu_actor_test` (4 vectors, 42 checks): dummy-not-found → null (no further
  calls), char-create-fails → null (world-pos sampled, no actor config), success → facing yaw
  -π/2 for a dummy whose forward is {1,0,0}, scale 0.6666667, slot -1, type 4, active 1, gait
  preloaded, node backref linked, leaf calls each once; plus aligned-forward → yaw 0.

This unblocks the dynasty-scene actor creation: the run loop + dynasty slot-fill logic were
already reconstructed; `CreateMenuDummyActor` is the per-actor instantiation they consume.
The skinned-mesh animation/render of the resulting actors remains the deferred native piece.

## 2026-06-09 — VIBE_Anim_CalculateAnimNormals @0x5d0020 (per-frame anim normals + bounds, 1:1)

Closed the documented "normals stay static after deformation" gap in the (otherwise ~95%
reconstructed) character-animation subsystem. `gilde.exe 0x5d0020` recomputes, for every
frame of a morph/skeletal clip: the deformed-vertex face normals (`VIBE_Math_TriangleNormal`),
the per-vertex normal = normalized sum of adjacent face normals (`VIBE_Math_VectorNormalize`,
identical to the static `VIBE_Mesh_ComputeVertexNormals @0x5D1A6C` but per frame), and the
frame bbox — then smooths each frame's bbox to the union of its (f-1, f, f+1) neighbours.

- `render/anim_normals.{h,cpp}` — `CalculateAnimNormals(frames, triangles, vertexCount, …)`
  (the math core), `CalculateClipNormals(AnimClip, triangles, …)` (the load-time wrapper the
  engine runs once per clip), and `QuantizeNormalByte` (the engine's normal→byte storage:
  bias `dword_5CBA30`=(1,1,1), scale `dbl_628F04`=0.5; the reimpl keeps float normals for the
  lit posed-mesh path). Tests: `anim_normals_test` (4 golden vectors, 45 checks: flat-quad
  +Z normals, the 3-frame bbox union, the byte quantizer, empty-clip safety) +
  `anim_normals_e2e_test` (real `.baf` clips from animations.BIN — BAUM/faellen_BUCHE, 1525
  checks: per-frame unit normals + finite smoothed bounds). Both presets 1176/1176.

The animation subsystem (parse `.baf`, morph blend, skeleton/bone math, playback driver,
posed-mesh resolver) was already reconstructed + tested on real Augsburg assets; this adds
the remaining pure-math piece. Consuming it for per-frame relight (`VIBE_Mesh_ComputeVertexLighting
@0x5c9054`) on posed meshes, and `VIBE_Character_CreateMenuDummyActor @0x52af64` (the dynasty
scene's actor instantiation / object-universe coupling), remain the integration steps.
