# 09 — New-game flow: choose city / history / player / character

> The wizard between [the main menu](08-main-menu.md) and a live session
> ([12 — Session init](12-session-init-worldload.md)). When the player clicks
> *New Game* in the main menu, control enters this multi-screen sequence which
> collects every parameter needed to start a dynasty: the city, the historical
> perspective / difficulty, the player identity (name, gender, faith, coat of
> arms), and — for "manual" starts — an ancestry / character. Each screen writes
> a small set of global variables; the world-load code (doc 12) reads those
> globals back out.

All addresses are RVA-style absolute (imagebase `0x400000`). Every function is
named `VIBE_*` from the IDA database; the binary is the source of truth.

---

## 1. Summary and call tree

The wizard is **not** a single state machine — it is a chain of nested run-loops.
The outer loop is the **ChooseCity** 3D scene; the deeper screens are launched
*from inside* it (and from inside each other) as blocking sub-runs that return a
success / abort code back up the stack.

```
VIBE_Menu_RunMainMenu @0x529d08   (doc 08)
  └─ "New Game" → VIBE_Menu_EnterChooseCity @0x52ee38
        └─ VIBE_Menu_RunChooseCity @0x52e6d8        ── 3D city-pick scene (outer loop)
              │   on confirm (Stadt picked):
              ├─ VIBE_Menu_ChooseCharacterIntroVariant @0x52e4e0   ── DIFFICULTY (radio-of-6 → byte_12335BA)
              └─ VIBE_Menu_RunChooseHistory @0x52d684              ── PERSPECTIVE (radio-of-4 → History flag)
                    │   inside its loop, in order:
                    ├─ VIBE_Mission_RunChooseHistoryDialog @0x538b28  ── picks the scenario mission
                    ├─ VIBE_Menu_RunChoosePlayer @0x52ccd8            ── identity wizard (name/sex/faith/wappen)
                    └─ VIBE_Menu_ChooseCharacterIntro @0x52e3d8       ── spine router (manual / automatic / abort)
                          ├─ manual  → VIBE_Menu_RunChooseCharacter @0x52bcd4   ── 3D dynasty / ancestry run loop
                          │                 └─ VIBE_Menu_ChooseCharacterTalent @0x52b088
                          │                 └─ VIBE_Menu_ChooseProfession @0x52c50c
                          │                 └─ VIBE_Menu_BuildCharacterPreviewScene @0x52b6b8
                          └─ automatic → VIBE_Menu_ChooseProfession @0x52c50c   ── profession-only start
```

When the chain finally succeeds, every collected choice lives in globals; the
main menu then proceeds to [VIBE_GameLogic_InitOrLoadSession @0x533a54](12-session-init-worldload.md),
which reads `byte_12335BA` (difficulty), the History flag, the `NewGameParams`
block at `dword_122F4EC`, and the player-identity globals (`dword_122F4A4`,
`byte_122F4A8`, `byte_122F4A9`, `String`, `byte_122F4CA`).

### Platform boundary
ChooseCity (`VIBE_Menu_RunChooseCity`) and ChooseCharacter
(`VIBE_Menu_RunChooseCharacter`) are **real 3D scenes**: they switch a universe
slot (`VIBE_Universe_SwitchActiveSlot`), bake lighting, set up a perspective view
transform (`VIBE_Render_SetupViewTransform`), run an `.esc` cutscene script that
flies a "MegaCam"-style camera along a spline, and do GPU object picking
(`VIBE_Pick_FindNearestObjectAt`). In this reconstruction the underlying GPU API
is swapped to **Vulkan** (project rule 3) and the input/window/timer layer to
**SDL** (rule 4); the projection/raster math itself is reproduced 1:1. See also
[27 — Animation](27-animation-skeleton.md) for the ancestor dummy actors.

---

## 2. Screens → markup → output global (quick table)

Each screen is a `.def` form loaded via `VIBE_GameTick_Finalize(0,0, "<form>")`,
populated with markup text via `VIBE_Text_RenderRichString(<idx>)` (the
`_M0_*` rich-string ids), and driven by a `VIBE_GameLogic_RunFrameLoop` loop.

| Screen | Function @addr | Form (.def) asset | Markup / rich-string id | Radio-of-N | Output global(s) consumed by doc 12 |
|---|---|---|---|---|---|
| Choose City | `VIBE_Menu_RunChooseCity @0x52e6d8` | `Menu\CHOOSECITY` (+ `Menu\CHOOSECITY_HEADER`) | `0x16C8`, `0x7D` | object-pick (not a radio) | selected city record → `NewGameParams` city fields (`dword_122F4EC`) |
| Difficulty | `VIBE_Menu_ChooseCharacterIntroVariant @0x52e4e0` | `menu\choosecharacter_intro` | `_M0_DIFFICULTY` = `0x16CC` | **6** | `byte_12335BA` (difficulty 0..5) |
| Perspective / History | `VIBE_Menu_RunChooseHistory @0x52d684` | `Menu\CHOOSEHISTORY` | `_M0_HISTORIE` = `0x16CD` | **4** | History flag via `VIBE_History_SetActiveFlag @0x4fd218` (1 / 2 / 0); `dword_122F4EC` (mission) |
| Intro router | `VIBE_Menu_ChooseCharacterIntro @0x52e3d8` | `menu\choosecharacter_intro` | `0x16EC` / `0x16ED` / `0x16EE` | **2** | return code: `1`=manual, `0`=automatic, `-1`=abort |
| Choose Player | `VIBE_Menu_RunChoosePlayer @0x52ccd8` | `Menu\CHOOSEPLAYER` | `0x16DC`..`0x16E8`, `0x7E` | sex 2, faith 2, wappen 8 | `String` (Vorname), `byte_122F4CA` (Nachname), `byte_122F4A8` (sex), `byte_122F4A9` (faith), `dword_122F4A4` (Wappen, +1342) |
| Choose Character | `VIBE_Menu_RunChooseCharacter @0x52bcd4` | `Menu\CHOOSECITY_HEADER` | `0x16F3` + `$C` | object-pick (8 ancestor slots) | `dword_122F258[]` / `dword_122F254[]` ancestry slot fill |
| Choose Profession | `VIBE_Menu_ChooseProfession @0x52c50c` | `Menu\CHOOSEPROFESSION` | `0x1707` | **8** | `HIBYTE(dword_122F4A0)` profession/building variant |

Common framework globals every screen polls (set by the input/HUD layer each
frame, see [14 — per-frame loop](14-per-frame-loop.md)):
`dword_75BF38` (last command id; `1210` = OK / confirm, `1155` = back), `byte_67225C`
(key code; `28` = Enter, `1` = abort/ESC), `dword_672230` (window-close request),
`dword_62D22C` (object the click landed on), and `dword_631614` (the "this run-loop
should exit now" flag — setting it to `1` is how every screen breaks its loop).

---

## 3. Entry: `VIBE_Menu_EnterChooseCity @0x52ee38`

Called from `VIBE_Menu_RunMainMenu @0x529d08` (xref at `0x52a341`) when the player
chooses New Game. It is a thin wrapper:

```c
word_63C740 = 0;                       // reset the new-game "stage" bitmask
VIBE_DragCursor_SetSprite(this, 0);    // normal cursor
byte_63CC1D = 1;                       // "1 = single-player new game" mode flag
result = VIBE_Menu_RunChooseCity(0, a2);
if (!result) {                         // wizard aborted → repaint menu backdrop
    VIBE_Surface_ColorFill(dword_62D210, ...);
    VIBE_Window_RenderEntityList(1773);
}
return result;
```

`word_63C740` is the new-game progress bitmask; `RunChooseHistory` later ORs bit
`8` into it on success (`LOBYTE(word_63C740) |= 8`). A non-zero return means the
whole wizard completed and the caller should launch the session; zero means the
player backed all the way out and the main menu redraws.

---

## 4. Choose City — `VIBE_Menu_RunChooseCity @0x52e6d8`

This is the visual heart of the wizard: a 3D fly-over of the map where each
selectable city is a clickable tower in the world.

### 4.1 Scene setup
1. `VIBE_StatusText_ClearTable` / `VIBE_StatusText_ResetEntries` — clear the
   tooltip / status-string table.
2. `VIBE_Universe_SwitchActiveSlot(0,0,…)` — activate universe slot 0 (the menu
   world).
3. `VIBE_Object_SetPosition` / `VIBE_Object_SetWorldTranslation` on
   `dword_13FCD1C` (the MegaCam camera object) — seat the camera.
4. `VIBE_SkyColor_BlendBandLighting(0,0,1.0,0)` — bake the sky-band lighting.
5. `VIBE_SaveBrowser_EnumerateSaveFiles("gamedata/cities", …)` — enumerate the
   per-city data files under `gamedata/cities`; the count is `v82`. **The list of
   selectable cities is data-driven from that folder.**

### 4.2 Per-city load loop (`0x52e797`)
For each enumerated city file it opens the file (`VIBE_Vfs_OpenFile(…, "rb", …)`),
reads its header + thumbnail (`VIBE_Save_LoadHeaderAndThumbnail`), then:
- `VIBE_Map_SpawnCityPointMarker(name)` spawns the city's pickable world object;
  the handle is stored into the `v58[]` array (one slot per city).
- Builds two status / tooltip strings from the city name using
  `sprintf`-style format strings:
  - `"_STADTAUSWAHL_%s_INFO+0"` → uppercased → looked up in the text table
    (`VIBE_Text_FindTextArrayIndex`) → registered as the city's status text
    (`stadt_%s` key) via `VIBE_StatusText_Register`.

### 4.3 The pickable tower + flag
After the loop (`LABEL_15`, `0x52e8c5`) it attaches the rotating **`sp_STADTTURM`**
("city tower") marker object to the universe (`VIBE_Object_AttachToUniverseNode(…,
"sp_STADTTURM", …)`), tweaks its render flags, builds its light cache
(`VIBE_Light_BuildObjectCache`), and loads its waving-pennant animation
**`sonstiges\wimpel_STADTTURM.baf`** (`VIBE_Character_LoadObjectAnimation`). This
is the highlighted "selected city" tower with a flag on top.

### 4.4 Camera-flight cutscene
- `VIBE_Cutscene_LoadAndRunScript("Startmenu/A_Stadtwahl.esc")` +
  `VIBE_Cutscene_RunScriptLoop` — runs the `A_Stadtwahl.esc` cutscene that flies
  the MegaCam camera along its spline over the map.
- Two forms are created and laid out: the header (`Menu\CHOOSECITY_HEADER`, window
  3) and the main form (`Menu\CHOOSECITY`, windows 0/1/2) with their rich strings
  (`0x16C8`, `0x7D`). `VIBE_Render_SetupViewTransform(...)` installs the
  perspective projection from the engine globals (`flt_13FC76C`, `flt_13FCAFC`,
  `flt_13FCD0C`, screen size in `dword_69FFBC` / `dword_69FFB8`).

### 4.5 Main pick loop (`0x52e9c8`)
Each frame `VIBE_GameLogic_RunFrameLoop(4310, …)` pumps the scene. Then:
- `VIBE_Pick_FindNearestObjectAt(0, mouseX, mouseY, 96, 1)` does GPU object
  picking at the cursor (`dword_672210` holds packed mouse coords); the hit goes
  into `dword_631724` / `dword_631720`.
- The hit is accepted only if its name starts with **`"stadt_"`**
  (`VIBE_Util_StrncmpN(…, "stadt_", 6)`), i.e. it's a city marker — this filters
  out clicks on scenery.
- On a *new* hover (`v26 != v79`) it: plays a 3D sound at the city's position
  (`VIBE_Sound3d_SetListenerFromVectors`), rebuilds the info panel from
  `"_STADTAUSWAHL_%s_BESCHR+0"` (description) into window 1, and toggles the
  suspend state of every city marker so only the hovered city's tower is "awake"
  (`VIBE_Object_ToggleSuspendStateNamed`). `v79` caches the last hovered city.

### 4.6 Confirm / descend (`0x52ed4e`)
When the player confirms (`dword_75BF38 == 1210` OK, or `byte_67225C == 28`
Enter):
- Hide both forms.
- If `v76` (the main-menu "this is the very first city ever" path) is set →
  `dword_631614 = 1; v80 = 1` and return success directly.
- Otherwise **descend into the rest of the wizard**:
  ```c
  if (VIBE_Menu_ChooseCharacterIntroVariant(...)) {   // difficulty
      if (VIBE_Menu_RunChooseHistory(...)) {           // perspective → player → character
          dword_631614 = 1; v80 = 1;                   // full success: leave city loop
      }
  }
  ```
  If the nested screens abort, the forms are re-shown and the city loop resumes —
  so backing out of *History* drops you back onto the map.

### 4.7 Teardown (loop exit, `0x52e9d9` false branch)
Destroys both forms, releases the `sp_STADTTURM` tower (after first detaching its
`wimpel_STADTTURM` pennant via `VIBE_Character_UpdateSubMeshes` /
`VIBE_Object_DetachAndRelease`), and releases every spawned city marker in `v58[]`.
Returns `v80` (1 = proceed, 0 = abort).

---

## 5. Difficulty — `VIBE_Menu_ChooseCharacterIntroVariant @0x52e4e0`

The difficulty picker (markup **`_M0_DIFFICULTY` = `0x16CC`**), a **radio-of-6**.

1. Loads form `menu\choosecharacter_intro`, centers it.
2. Seeds `dword_63C744` from the persisted difficulty `byte_12335BA`.
3. Renders the `_M0_DIFFICULTY` rich string and pulls the **6** child object ids
   (the 6 radio buttons) via `VIBE_Form_GetChildObjectId(form, 0, base+n)` for
   `n = 0..5`.
4. `VIBE_RadioGroup_Create(6, firstButton)` builds the 6-way radio group;
   `VIBE_Selection_Update` pre-selects the current difficulty.
5. Loop (`VIBE_GameLogic_RunFrameLoop(198, …)`): on confirm
   (`dword_75BF38 == 1210` || Enter) it maps the clicked object `dword_62D22C`
   onto button index → sets `dword_63C744` to **0..4** (the 6th, index 5, leaves
   it unchanged), then exits. On abort (`dword_672230` / ESC) or back
   (`1155`) it just exits.
6. On exit: **`byte_12335BA = dword_63C744`** — this is the persistent difficulty
   global, written here and read by [VIBE_GameLogic_InitOrLoadSession](12-session-init-worldload.md)
   (xref `0x533ebb`) and by the config save/load (`VIBE_Config_WriteGfxSettings` /
   `…ReadGfxAndSoundSettings`). Frees the radio group, destroys the form. Returns
   non-zero ⇒ proceed to History.

---

## 6. Perspective / History — `VIBE_Menu_RunChooseHistory @0x52d684`

The "perspective" picker (markup **`_M0_HISTORIE` = `0x16CD`**), a **radio-of-4**,
and the *driver* that chains the rest of the wizard.

### 6.1 Form + radio
1. Loads `Menu\CHOOSEHISTORY`, renders `_M0_HISTORIE` (`0x16CD`) plus its
   sub-strings (`5839/5840/5841`, `1155`), and reads **4** child object ids
   (`ChildObjectId`=v32, `v33`, `v34`, plus a 4th button `v25`).
2. `VIBE_RadioGroup_Create(4, ChildObjectId)`.
3. Pre-selects from the persisted **`dword_12335AC`** (the saved history mode):
   `case 1 → sel 0`, `case 2 → sel 1`, `case 0 → sel 2`.

### 6.2 The three History modes → flag 1 / 2 / 0
On confirm the clicked object is matched and the **History flag** is set via
`VIBE_History_SetActiveFlag @0x4fd218`:
- click on button `v32` → `VIBE_History_SetActiveFlag(1)`
- click on button `v33` → `VIBE_History_SetActiveFlag(2)`
- click on button `v34` (or ESC `byte_67225C == 28`) → `VIBE_History_SetActiveFlag(0)`

It then re-selects a radio index from the resulting history state byte
(`dword_633920 >> 24`: `1→sel0`, `2→sel1`, `0→sel2`) and runs the scenario picker
**`VIBE_Mission_RunChooseHistoryDialog @0x538b28`**, whose result byte `v36` is
stored into the `NewGameParams` block at **`dword_122F4EC`** (`0x52d82d`). `0xFF`
means "no scenario chosen / cancel".

### 6.3 The chained sub-wizard
If a scenario was chosen (`v36 != 0xFF`), it hides its own form and runs, in order:

1. Cutscene `Startmenu/B_Persoenliches.esc` (the "personal details" camera move),
   `dword_11BC2D0 = 4822`.
2. **`VIBE_Menu_RunChoosePlayer(198, 0)`** — the identity wizard (§7). If it
   returns 0 (player backed out), it replays `Startmenu/A_Stadtwahl.esc` and
   loops back to the city scene.
3. **`VIBE_Menu_ChooseCharacterIntro @0x52e3d8`** — the spine router (§8). Its
   return value branches:
   - `== 1` (manual / family tree): sets `dword_62D314 = 1`, shows a wait box
     (`VIBE_Dialog_ShowMessageBoxSimple`, handle cached in `dword_63CD34`), then
     runs **`VIBE_Menu_RunChooseCharacter`** (§9, the 3D dynasty run). Success ⇒
     `v8 = 1; dword_631614 = 1` (finish); failure ⇒ re-enter the loop.
   - `== 0` (automatic / profession): runs **`VIBE_Menu_ChooseProfession(198)`**
     (§10). On success it looks up the chosen building/profession record
     (`VIBE_Building_LookupTypeRecordA(HIBYTE(dword_122F4A0), …)`), copies 5 bytes
     of that record into the `NewGameParams` block (`dword_122F4EC+3 …`), sets
     `dword_122F528 = 1555`, and finishes (`v8 = 1`).
   - `== -1` (abort): re-shows the History form and resumes its loop.

### 6.4 Exit
Frees the radio group and destroys the form. On success it ORs bit 8 into
`word_63C740` and returns `v8` (non-zero); on failure it clears `word_63C740` and
returns 0.

---

## 7. Choose Player — `VIBE_Menu_RunChoosePlayer @0x52ccd8`

The identity wizard. Form **`Menu\CHOOSEPLAYER`** has 6 windows that are revealed
one at a time (a "page" counter `v2`): Vorname → Nachname → gender → faith →
coat-of-arms → commit.

### 7.1 Defaults from the INI
It seeds every field from the `[Network]` section of the game INI
(`byte_122F638` = INI path) using Win32 profile APIs (these stay as INI reads /
writes per the Rule-6 decision to reconstruct the INI layer 1:1 — the *backing*
file I/O goes through the platform shim):
- `Name` (default `"Vorname"`) → `String` (`0x122F4AA`)
- `Familienname` (default `"Nachname"`) → `byte_122F4CA`
- `Wappen` (default 0) → `dword_122F4A4 = value + 1342`  (coat-of-arms object id base)
- `Geschlecht` (gender) → `byte_122F4A8`
- `Glauben` (faith) → `byte_122F4A9`

### 7.2 Widgets
- Window 1: Vorname text field, prompt `0x16DC` (with the city name spliced in via
  `VIBE_City_LookupSelectionInfoText`), `0x16DD`.
- Window 2: Nachname field, `0x16DE`.
- Windows 3/4: gender and faith — each a `VIBE_Hud_BuildButtonRow(…, 28, …)`
  two-button row → `VIBE_RadioGroup_Create(2, …)`, pre-selected from
  `byte_122F4A8` / `byte_122F4A9`.
- Window 5: coat-of-arms — **8** wappen icons laid out in a grid
  (`VIBE_Object_AddToWindow` ×8, ids `1342..1349`), `VIBE_RadioGroup_Create`,
  pre-selected to match `dword_122F4A4`.
- Window 6: the commit / OK button (`0x7E`).

### 7.3 Page state machine
The big loop advances `v2` through the pages on Enter (`byte_67225C == 28`),
copying the focused field's text out of the widget
(`VIBE_Object_GetDataPtr`) into `String` (Vorname) and `byte_122F4CA`
(Nachname), and on the gender/faith/wappen pages writing the picked radio index
into `byte_122F4A8` / `byte_122F4A9` / `dword_122F4A4`. `VIBE_Widget_SetFocus`
moves the keyboard focus to the active field; `VIBE_Object_SetVisibleRecursive`
reveals pages up to the current one.

The final page (`case 5`) commits: builds the `dword_122F4A0` header
(`LOBYTE=0`, bytes 1-2 = `4608`), sets `dword_122F4A4` to the selected wappen
object's value, then either finishes (`v70 = 1; dword_631614 = 1`) or, if a
family-tree exists (`dword_63C8F0 >> 24 != -1`) and not in the network sub-mode,
proceeds.

### 7.4 Persist back to INI
On exit it writes everything back to `[Network]` (`WritePrivateProfileStringA`):
`Name`, `Familienname`, and `Wappen` / `Geschlecht` / `Glauben` (each formatted
via `VIBE_AnimationState_Update(value, buf, 10)` = itoa base-10). Returns
`v70` (1 = committed, 0 = backed out).

---

## 8. Spine router — `VIBE_Menu_ChooseCharacterIntro @0x52e3d8`

A tiny **radio-of-2** decision screen (form `menu\choosecharacter_intro`) that
chooses *how* the founding character is created:

- Renders `0x16EC`, conditionally `0x16ED` (only if a family tree already exists:
  `dword_63C8F0 >> 24 != -1`), and `0x16EE`, then two child buttons →
  `VIBE_RadioGroup_Create(2, …)`.
- Returns:
  - **`1`** on confirm (`1210` / Enter) → *manual* family-tree / dynasty path
    (caller runs `VIBE_Menu_RunChooseCharacter`).
  - **`0`** on the `1155` (back) command, falling through with `v2 = 0` →
    *automatic* / profession path (caller runs `VIBE_Menu_ChooseProfession`).
  - **`-1`** on abort (`dword_672230` / ESC) → cancel back to History.

---

## 9. Choose Character — `VIBE_Menu_RunChooseCharacter @0x52bcd4`

The **3D dynasty / ancestry** run loop (manual path). It populates six ancestry
slots (the family tree's grandparents/parents) by letting the player click on
ancestor figures standing in a 3D scene.

### 9.1 Setup
- Copies the ancestry-slot name template `dummy_GROSSVATER_VAETERLICH` (+ siblings)
  into a local table `v26[]` (one per slot).
- `VIBE_Light_SetGrayColorThunk` sets the menu lighting; the slot-result array
  `dword_122F254[6]` is initialized to `-1` (empty).
- `VIBE_Universe_SwitchActiveSlot(1, 0, …, -1)` — activates the *character*
  universe slot (slot 1).
- **Spawns 10 ancestor dummy actors** via
  `VIBE_Character_CreateMenuDummyActor(dummyMesh, costume)` — the candidate
  ancestors, one male + one female per profession archetype (see
  [27 — Animation](27-animation-skeleton.md) for how the skinned dummies are
  posed). The mesh/costume pairs are:

  | Local | Dummy mesh | Costume | Archetype |
  |---|---|---|---|
  | `MenuDummyActor` | `dummy_NACHT_UND_NEBEL_MANN` | `dieb_MANN2` | thief / "night & fog" |
  | `v44` | `dummy_NACHT_UND_NEBEL_FRAU` | `zigeunerin_FRAU` | |
  | `v38` | `dummy_HANDWERKSKUNST_MANN` | `handwerker_MANN` | craftsman |
  | `v37` | `dummy_HANDWERKSKUNST_FRAU` | `handwerkerin2_FRAU` | |
  | `v43` | `dummy_KAMPF_MANN` | `offizier_SOLDAT` | combat |
  | `v40` | `dummy_VERHANDELN_MANN` | `buerger3_DICKER` | trade / "negotiate" |
  | `v39` | `dummy_VERHANDELN_FRAU` | `buergerin2_FRAU` | |
  | `v42` | `dummy_RHETORIK_MANN` | `priester_KUTTE` | rhetoric / clergy |
  | `v41` | `dummy_RHETORIK_FRAU` | `buergerin_FRAU` | |

- Loads the form `Menu\CHOOSECITY_HEADER`, sets up the sky `Sky_Mittel_02`
  (`VIBE_Cutscene_SetupSky`) and bakes lighting
  (`VIBE_SkyColor_BlendBandLighting(4, …)`).
- Destroys the leftover "please wait" box (`dword_63CD34`) from §6.3.

### 9.2 Pick loop
Each frame (`VIBE_GameLogic_RunFrameLoop(4822, …)`):
- It computes the next empty slot index `v7` (4..5 region) and renders the prompt
  `0x16F3` (slot label `v7 + 5879`) into window 0, flying the MegaCam to the
  appropriate camera waypoint (two alternating camera transforms read from
  `dword_13FCD1C + 228 / +252`), via `VIBE_Anim_FreeObjAnimData` +
  `VIBE_Sound3d_SetListenerFromVectors`.
- On a click hit (`dword_631720`) on an un-assigned actor
  (`*(actor+508) == -1`): it maps the clicked actor pointer to a **profession
  code** (`v5`: thief→2, craft→1, combat→3, rhetoric→4, trade→0) and to a
  **slot** (even slot 4 = male side, odd slot 5 = female side), then records
  `dword_122F258[slot] = code` and stamps the actor's `+508` field with the slot.
  `VIBE_Object_FindByHandle` + `VIBE_Command_Handler` triggers the actor's
  walk-into-place animation.

### 9.3 Finish
When all slots are filled (`v7 >= 6`) it hides the form and chains:
```c
if (!VIBE_Menu_ChooseCharacterTalent())       // talent allocation — abort if 0
    { v33 = 0; dword_631614 = 1; }             // → back out
else if (VIBE_Menu_ChooseProfession(198)       // pick starting profession
      && VIBE_Menu_BuildCharacterPreviewScene())  // final preview / confirm
    { v33 = 1; dword_631614 = 1; }             // → success
```
On success (`v33 == 1`) it runs a `BLACK` fade-out (`VIBE_Fade_Register`, 90
frames) while pumping frames, then tears everything down: destroys all 10 dummy
actors (`VIBE_Character_Destroy`), the form, the cutscene script, the sky
(`VIBE_Cutscene_DestroySky`), the heightmap, and unregisters the fade. Returns
`v33` (1 = character created, 0 = aborted).

---

## 10. Choose Profession — `VIBE_Menu_ChooseProfession @0x52c50c`

A **radio-of-8** profession picker (form `Menu\CHOOSEPROFESSION`, markup `0x1707`),
used by *both* the automatic path (§6.3) and the manual path (§9.3).

- Builds **8** buttons via `VIBE_Object_AddToWindow`, labelling each from the
  string table `dword_8C3DA8[...]`, added to a single radio group.
- On a click (`dword_672228`) it finds which of the 8 buttons matched
  (`dword_75BF38 == idx + 1349`), computes the building/profession variant
  `HIBYTE(dword_122F4A0) = VIBE_BuildingType_ComputeVariantIndex(rawIdx, 1)`, sets
  `dword_631614 = 1`, and returns 1. Returns 0 if dismissed without a pick.

`dword_122F4A0`'s high byte is the chosen profession/building type; the History
driver (§6.3) then expands it into the `NewGameParams` block via
`VIBE_Building_LookupTypeRecordA`.

---

## 11. What the wizard hands to session init (doc 12)

By the time the chain returns success to the main menu, the following globals hold
the new-game parameters that [VIBE_GameLogic_InitOrLoadSession @0x533a54](12-session-init-worldload.md)
reads back:

| Global | Set by | Meaning |
|---|---|---|
| `byte_12335BA` | Difficulty (§5) | difficulty level 0..5 |
| History flag (via `VIBE_History_SetActiveFlag @0x4fd218`, state at `dword_633920`) | History (§6.2) | perspective / historical mode (1 / 2 / 0); persisted mode mirror in `dword_12335AC` |
| `dword_122F4EC` (`NewGameParams` block) | History scenario (§6.2) + profession expansion (§6.3) + `VIBE_Menu_ShowMissionWarning @0x52c6c0` | chosen scenario mission id and 5-byte building/start record |
| `dword_122F528` | History auto path (§6.3) | `1555` start marker for the profession path |
| `String` (`0x122F4AA`) | Choose Player (§7) | player first name (Vorname) |
| `byte_122F4CA` | Choose Player (§7) | player family name (Nachname) |
| `byte_122F4A8` | Choose Player (§7) | gender |
| `byte_122F4A9` | Choose Player (§7) | faith / religion |
| `dword_122F4A4` | Choose Player (§7) | coat-of-arms object id (Wappen index + 1342) |
| `dword_122F4A0` | Choose Profession (§10) | header byte 0, profession variant in HIBYTE |
| `dword_122F254[6]` / `dword_122F258[6]` | Choose Character (§9) | ancestry slot profession codes (manual path) |
| `word_63C740` | Enter/History | new-game stage bitmask (bit 3 set on full success) |

See [16 — Characters / persons](16-characters-persons.md) for how the founding
person record is materialized from the player-identity globals and the ancestry
slots once the session world is loaded.

---

## Cross-references
- [08 — Main menu](08-main-menu.md) — `VIBE_Menu_RunMainMenu`, which invokes
  `VIBE_Menu_EnterChooseCity`.
- [12 — Session init / world load](12-session-init-worldload.md) — consumes every
  global written here.
- [14 — Per-frame loop](14-per-frame-loop.md) — `VIBE_GameLogic_RunFrameLoop` and
  the input/command globals (`dword_75BF38`, `byte_67225C`, `dword_62D22C`, …).
- [16 — Characters / persons](16-characters-persons.md) — founding-person /
  dynasty record creation.
- [27 — Animation / skeleton](27-animation-skeleton.md) — the `sp_STADTTURM`
  pennant and the ancestor dummy actors / poses.

## Provenance (addresses)
| Symbol | Address |
|---|---|
| `VIBE_Menu_EnterChooseCity` | `0x52ee38` |
| `VIBE_Menu_RunChooseCity` | `0x52e6d8` |
| `VIBE_Menu_ChooseCharacterIntroVariant` | `0x52e4e0` |
| `VIBE_Menu_RunChooseHistory` | `0x52d684` |
| `VIBE_Menu_ChooseCharacterIntro` | `0x52e3d8` |
| `VIBE_Menu_RunChoosePlayer` | `0x52ccd8` |
| `VIBE_Menu_RunChooseCharacter` | `0x52bcd4` |
| `VIBE_Menu_ChooseProfession` | `0x52c50c` |
| `VIBE_Menu_ChooseCharacterTalent` | `0x52b088` |
| `VIBE_Menu_BuildCharacterPreviewScene` | `0x52b6b8` |
| `VIBE_Menu_ShowMissionWarning` | `0x52c6c0` |
| `VIBE_Mission_RunChooseHistoryDialog` | `0x538b28` |
| `VIBE_History_SetActiveFlag` | `0x4fd218` |
| `VIBE_GameLogic_InitOrLoadSession` (doc 12) | `0x533a54` |
| `byte_12335BA` (difficulty) | `0x12335ba` |
| `dword_12335AC` (history mode mirror) | `0x12335ac` |
| `dword_122F4EC` (NewGameParams) | `0x122f4ec` |
