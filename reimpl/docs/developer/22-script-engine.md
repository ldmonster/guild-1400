# 22 — Script engine

The "VIBE" engine embeds a small interpreted scripting language used to drive
cut-scenes, character actions, object/particle/camera animation, sound playback,
and — crucially — to hand work off to the simulation's command/netcode layer
(the `cm_*` commands, see [19 — Commands & netcode](19-commands-netcode.md)).
Scripts are C-like text files (`.scr`) loaded from `x:\engine\gfx\scripts\`,
compiled into a per-script function/symbol table, and then run as **cooperative
coroutines**: every script "instance" lives in a fixed-size record, and the
engine steps all active instances once per frame, with `Sleep`, `PlayAnimation`,
`WalkToDummy`, etc. able to *yield* and resume on a later frame.

This document covers four things:

1. The **command table** and the registration primitive that builds it (the VM's
   instruction set / "imported" native functions).
2. The four registration entry points that populate that table, with a full
   command → handler reference.
3. The **tokenizer / compiler / interpreter** and the coroutine stepper.
4. The **coroutine record layout** and how `Sleep`/yield work.

Cross-links: [04 — App init](04-app-init-subsystems.md),
[14 — Per-frame loop](14-per-frame-loop.md),
[17 — Character actions](17-character-actions-ai.md),
[19 — Commands & netcode](19-commands-netcode.md),
[21 — Economy](21-economy-office-stats.md).

---

## 1. Where it is wired up

Both halves of the engine are bootstrapped from app init:

* `VIBE_App_InitEngineAndScriptCommands @0x528560` ([04 — App init](04-app-init-subsystems.md))
  calls, in order:
  * `VIBE_Script_ConsoleParseLine @0x4453a4` — allocates the engine's tables and
    seeds the tokenizer (despite the name, this is the **one-time initializer** of
    the script subsystem, not a per-line parser).
  * `VIBE_Script_RegisterCommands @0x43c850` — core/flow commands.
  * `VIBE_Script_RegisterObjectCommands @0x440618` — object / light / camera / particle.
  * `VIBE_Character_RegisterScriptCommands @0x43dfb0` — character actions.
  * `VIBE_Script_RegisterSoundCommands @0x440df0` — sound, plus the `SND_*` event tokens.

* `VIBE_GameLogic_RunFrameLoop @0x4c09a0` (the per-frame loop,
  [14 — Per-frame loop](14-per-frame-loop.md)) calls
  `VIBE_Script_StepAllActive @0x445250` each frame to advance every active
  coroutine. (This function also owns the `"MASTER(): Function already
  registrated!"` diagnostic string at `0x61e51c`, used by the simulation-side
  function-registration path it shares with the script engine.)

---

## 2. The command table & registration primitive

### 2.1 Table storage

The command table is a single flat array referenced by the global
`dword_62E8AC` (a pointer set up in `VIBE_Script_ConsoleParseLine`, allocated as a
`0x3400` = 13312-byte block via `VIBE_Memory_AllocDebug("evt:command_entry")`).

* **Stride: 52 bytes per entry**, **max 256 entries** (`52 * 256 = 13312`).
* Entry layout (offsets within a 52-byte record), recovered from
  `VIBE_Script_ImportCommand @0x445bc8` and the dispatchers:

```c
// gilde.exe — script command-table entry (52 bytes, base = dword_62E8AC)
struct ScriptCommand {
    char name[32];     // +0x00  command name (<= 31 chars, NUL-terminated)
    i32  argtypes[3];  // +0x20  argument type codes, one byte each, packed
                       //        (+0x24 holds arg[0], +0x25 arg[1], ... up to argc)
    i32  argc;         // +0x20  arg count  (== entry+32; see note below)
    i32  handler;      // +0x2C  native function pointer (entry+44)
    u8   callconv;     // +0x30  calling-convention class (entry+48): 1 or 5 (see §4.3)
};
```

Concretely from the writer (`VIBE_Script_ImportCommand`):

* `entry+44` (`+0x2C`) ← handler address (`a2`).
* `entry+32` (`+0x20`) ← `argc` (`a4`).
* `entry+48` (`+0x30`) ← `callconv` class (`a3`).
* `entry+36 .. +36+argc-1` ← the per-arg **type codes** (`a3`-typed bytes copied
  from the varargs), and
* `entry+0 ..` ← the command **name** string.

The empty-slot test in `ImportCommand` checks `*(entry+44) == 0` (handler slot)
to find the first free row.

### 2.2 Argument **type codes**

The variadic tail of each registration call is a list of one-byte type codes,
preceded by the **argument count**. Observed codes:

| Code | Meaning            |
|------|--------------------|
| `1`  | int                |
| `6`  | string             |
| `7`  | float              |

(e.g. `Print` is registered `argc=1, {6}` → one string; `SetPos` is
`argc=4, {1,7,7,7}` → handle + three floats.)

### 2.3 The registration primitive `VIBE_Script_ImportCommand @0x445bc8`

```c
int VIBE_Script_ImportCommand(const char *name, int handler, char callconv, int argc, ...);
```

Behaviour (1:1):

1. **Name-length guard** — `strlen(name) > 0x1F` → log
   `"evt_ImportCommand: Commandnamelength is invalid..."` (`0x618578`) and return 0.
2. **Duplicate guard** — `VIBE_Script_FindCommandByName(name)` non-NULL → log
   `"evt_ImportCommand: Commandname already exists..."` (`0x6185ac`) and return 0.
   *(This is the registration-collision check; the sibling
   `"MASTER(): Function already registrated!"` at `0x61e51c` is the same idea on
   the simulation side, owned by `VIBE_GameLogic_RunFrameLoop`.)*
3. **Capacity guard** — scan for the first row whose `handler` slot (`+44`) is 0,
   stepping 52 bytes; if the index reaches 256 → log
   `"evt_ImportCommand: Too many commands!"` (`0x6185e0`) and return 0.
4. Write `handler`, `argc`, `callconv`, the per-arg type bytes, and the name into
   the row; return 1.

### 2.4 Lookup `VIBE_Script_FindCommandByName @0x445b70`

Linear scan of up to 256 rows (`v3 < 13312`, stride 52), comparing the query
against `name` at `entry+0` via `VIBE_Util_StrCmp`. Returns the row address
(`dword_62E8AC + 52*i`) or 0. The tokenizer uses exactly this to classify an
identifier as a command (token type 4, see §3).

### 2.5 Event tokens `VIBE_Script_AddEventToken @0x441140`

A *second*, separate table (base `dword_767944`, grown geometrically by
`VIBE_Memory_AllocDebug("evt:t")`, 48-byte stride, count in `dword_767948`)
holds named **event/enum tokens** rather than callable commands. Used only by the
sound block to register the speech-event enums:
`SND_W2, SND_W1, SND_M1, SND_M2, SND_M3, SND_HS, SND_NONE, SND_VAR`
(values at `0x62E784..0x62E7A0`). Each entry stores a name (`+1`), a value
pointer (`+44`), a flag nibble (`+0`), and a slot-init marker (`+36 = 1`).

---

## 3. Command reference (the VM instruction set)

The four registration functions are pure tables of `ImportCommand` calls. Each
row below is `name → handler @addr (argc, {types})`. Type codes: `1`=int,
`6`=string, `7`=float.

### 3.1 Core / flow — `VIBE_Script_RegisterCommands @0x43c850`

| Command                    | Handler                                         | argc / types     |
|----------------------------|-------------------------------------------------|------------------|
| `ecmd_Dummy`               | `VIBE_Script_CmdReturnTrue @0x43c650`           | 0                |
| `Print`                    | `VIBE_Script_CmdReturnFalse @0x43c658`          | 1 {6}            |
| `PrintInt`                 | `VIBE_Script_CmdReturnFalse @0x43c658`          | 1 {1}            |
| `PrintFloat`               | `VIBE_Script_CmdReturnFalse @0x43c658`          | 1 {7}            |
| `Rnd`†                     | `VIBE_Util_RandModulo @0x43c65c`                | 1 {1}            |
| `GetTime`                  | `VIBE_GameTick_GetScaledDelay @0x43c680`        | 0                |
| `RunScript`                | `VIBE_Script_LoadAndRunMain @0x43c690`          | 1 {6}            |
| `RunScriptInt`             | `VIBE_Script_LoadAndRunWithArg @0x43c6ac`       | 2 {6,1}          |
| `RunScriptString`          | `VIBE_Script_LoadAndRunWithArgAlt @0x43c6d4`    | 2 {6,6}          |
| `StopScript`               | `VIBE_Script_FinishThunk @0x43c6fc`             | 1 {1}            |
| `Sleep`                    | `VIBE_Script_CmdSleep @0x43c708`                | 1 {1}            |
| `FindScript`               | `VIBE_Script_FindByNameThunk @0x43c788`         | 1 {6}            |
| `KillLocalScripts`         | `VIBE_Script_CmdKillLocalScripts @0x43c790`     | 0                |
| `CallUserFunction`         | `VIBE_Script_CallUserFunction @0x43c7ec`        | 2 {1,1}          |
| `CallUserFunctionExtended` | `VIBE_Script_CallUserFunctionExtended @0x43c81c`| 3 {1,1,1}        |

† Registered via `off_61688C` ("Rnd").

`GetTime` returns `uDelay * dword_62EB38` (the **scaled** frame delay — frame tick
× speed multiplier), which is the same clock `Sleep` measures against (§4.4).

### 3.2 Object / light / camera / particle — `VIBE_Script_RegisterObjectCommands @0x440618`

| Command | Handler | argc / types |
|---------|---------|--------------|
| `CreateObject` | `VIBE_Object_CmdShowObject @0x43e624` | 4 {7,7,7,6} |
| `CreateObjectAtDummy` | `VIBE_Object_CmdShowObjectAtDummy @0x43e66c` | 2 {6,1} |
| `CreateObjectGroupAtDummy` | `VIBE_Object_CmdAttachLightAtDummy @0x43e6bc` | 2 {6,1} |
| `KillObject` | `VIBE_Object_KillObject @0x43e724` | 1 {1} |
| `SetPos` | `VIBE_Object_SetPos @0x43e758` | 4 {1,7,7,7} |
| `SetAngle` | `VIBE_Object_SetAngle @0x43e79c` | 4 {1,7,7,7} |
| `MoveObject` | `VIBE_Object_MoveObject @0x43e804` | 5 {1,1,1,1,1} |
| `RotateObject` | `VIBE_Object_CmdRotateObject @0x43e868` | 5 {1,1,1,1,1} |
| `MoveObjectRelative` | `VIBE_Object_CmdMoveObjectRelative @0x43e8f0` | 5 {1,1,1,1,1} |
| `RotateObjectRelative` | `VIBE_Object_CmdRotateObjectRelative @0x43e968` | 5 {1,1,1,1,1} |
| `SetAmbiente` | `VIBE_Light_SetGlobalDirection @0x43ea0c` | 3 {1,1,1} |
| `LightCalc` | `VIBE_Light_RefreshAllToggle @0x43ea34` | 1 {1} |
| `ReplaceObject` | `VIBE_Object_ReplaceObject @0x43ea48` | 2 {1,6} |
| `LoadScene` | `VIBE_Object_CmdLoadScene @0x43ea80` | 1 {6} |
| `AttachAnim` | `VIBE_Object_CmdAttachAnimationLooped @0x43eab8` | 3 {1,6,1} |
| `PreloadAnim` | `VIBE_Object_CmdAttachAnimationOnce @0x43ec7c` | 2 {1,6} |
| `AttachAnimLooped` | `VIBE_Object_CmdAttachAnimLoopedVerified @0x43edcc` | 3 {1,6,1} |
| `AttachAnimLoopedToAll` | `VIBE_Sound_CmdPlaySampleAt @0x43f210` | 4 {6,6,1,1} |
| `DetachAnimFromAll` | `VIBE_Sound_CmdPlaySampleLooped @0x43f2bc` | 1 {6} |
| `GetObjectHandle` | `VIBE_Object_CmdGetObjectHandle @0x43e3e0` | 1 {6} |
| `GetRndObjectHandle` | `VIBE_Object_CmdFindRandomByName @0x43e48c` | 1 {6} |
| `GetRndSubObjectHandle` | `VIBE_Object_CmdFindRandomVisibleByName @0x43e520` | 2 {1,6} |
| `GetSubObjectHandle` | `VIBE_Object_CmdGetSubObjectHandle @0x43e5c4` | 2 {1,6} |
| `SetNoTextures` | `VIBE_Object_CmdSetActiveHandle @0x43f434` | 1 {1} |
| `SetLightSet` | `VIBE_SkyColor_ApplyDefaultLighting @0x43f444` | 1 {1} |
| `SetFogSet` | `VIBE_SkyColor_ApplyScaledBlend @0x43f460` | 3 {1,1,1} |
| `CameraFlight` | `VIBE_Camera_Flight @0x43f4d8` | 3 {1,6,6} |
| `ZoomOnObject` | `VIBE_Object_CmdObjectFlightSingle @0x43f7b8` | 2 {1,6} |
| `CameraFlightEnhanced` | `VIBE_Camera_CmdCameraFlight @0x43f528` | 7 {1,6,6,6,6,6,6} |
| `NewCameraFlight` | `VIBE_Camera_CmdCameraFlightTimed @0x43f5dc` | 7 {1,6,6,6,6,6,6} |
| `ObjectFlight` | `VIBE_Object_CmdObjectFlight @0x43f678` | 7 {1,1,6,6,6,6,6} |
| `CreateRain` | `VIBE_Object_CmdSetObjectStateThunk @0x43f844` | 1 {1} |
| `DeleteRain` | `VIBE_Object_CmdResetObjectThunk @0x43f84c` | 1 {1} |
| `CreateParticle` | `VIBE_Particle_SpawnBloodEffect @0x43fa1c` | 1 {1} |
| `KillParticle` | `VIBE_Particle_KillParticle @0x43fb88` | 1 {1} |
| `SetCameraToDummy` | `VIBE_Camera_SetToDummy @0x43fbbc` | 1 {6} |
| `StopCameraFlight` | `VIBE_Anim_FreeObjAnimDataAndReset @0x43fc38` | 0 |
| `RenameObject` | `VIBE_Util_StrCopyChecked @0x43fc58` | 2 {1,6} |
| `SetBlocked` | `VIBE_Object_SetBlocked @0x43fc8c` | 2 {1,1} |
| `SetTransient` | `VIBE_Object_SetTransient @0x43fcd8` | 2 {1,1} |
| `CreateEmitter` | `VIBE_Particle_CreateEmitter @0x43fd24` | 7 {1,1,1,1,6,1,1} |
| `KillEmitter` | `VIBE_Particle_KillEmitter @0x43fe68` | 1 {1} |
| `SetEmitterAmplitude` | `VIBE_Emitter_SetAmplitude @0x43fe9c` | 6 {1,1,1,1,1,1} |
| `SetEmitterPhasespeed` | `VIBE_Emitter_SetPhasespeed @0x43ff30` | 6 {1,1,1,1,1,1} |
| `SetEmitterDirection` | `VIBE_Emitter_SetDirection @0x43ffc4` | 4 {1,1,1,1} |
| `SetEmitterSize` | `VIBE_Emitter_SetSize @0x44002c` | 4 {1,1,1,1} |
| `SetEmitterVelocity` | `VIBE_Emitter_SetVelocity @0x440068` | 4 {1,1,1,1} |
| `SetEmitterAcceleration` | `VIBE_Emitter_SetAcceleration @0x4400d4` | 4 {1,1,1,1} |
| `SetEmitterRndVelocity` | `VIBE_Emitter_SetRndVelocity @0x440144` | 4 {1,1,1,1} |
| `SetEmitterPlane` | `VIBE_Emitter_SetPlane @0x4401b4` | 5 {1,1,1,1,1} |
| `SetEmitterTimeAndAlpha` | `VIBE_Emitter_SetTimeAndAlpha @0x440234` | 5 {1,1,1,1,1} |
| `SetEmitterColor` | `VIBE_Emitter_SetColor @0x44028c` | 5 {1,1,1,1,1} |
| `SetEmitterFlags` | `VIBE_Emitter_SetFlags @0x4402e4` | 7 {1,1,1,1,1,1,1} |
| `SetEmitterInitFill` | `VIBE_Emitter_SetInitFill @0x4403c8` | 2 {1,1} |
| `SetEmitterRebirthFill` | `VIBE_Emitter_SetRebirthFill @0x440418` | 2 {1,1} |
| `SetEmitterTextureMode` | `VIBE_Emitter_SetTextureMode @0x440468` | 2 {1,1} |
| `SetEmitterIsTrigger` | `VIBE_Emitter_SetIsTrigger @0x4404b8` | 2 {1,1} |
| `SetEmitterTriggerOnce` | `VIBE_Emitter_SetTriggerOnce @0x440508` | 2 {1,1} |
| `SetEmitterMaxTrigger` | `VIBE_Emitter_SetMaxTrigger @0x440558` | 2 {1,1} |
| `TriggerEmitter` | `VIBE_Emitter_Trigger @0x440590` | 1 {1} |
| `SetParticlePos` | `VIBE_Particle_SetPos @0x4405c4` | 4 {1,1,1,1} |
| `SelectAllTextureSets` | `VIBE_Sound_CmdPlaySampleWithFlag @0x43f394` | 2 {6,1} |

### 3.3 Character actions — `VIBE_Character_RegisterScriptCommands @0x43dfb0`

| Command | Handler | argc / types |
|---------|---------|--------------|
| `CreateCharacter` | `VIBE_Script_CmdCreateCharacter @0x43c9c0` | 4 {6,7,7,7} |
| `CreateCharacterAtDummy` | `VIBE_Script_CmdCreateCharacterAtDummy @0x43ca0c` | 3 {6,1,6} |
| `KillCharacter` | `VIBE_Character_DestroyThunk @0x43cb20` | 1 {1} |
| `WalkToDummy` | `VIBE_Script_CmdWalkToDummy @0x43cb28` | 2 {1,1} |
| `WalkToDummyRotate` | `VIBE_Script_CmdWalkToDummyRotate @0x43cbc8` | 2 {1,1} |
| `WalkToDummyVerified` | `VIBE_Script_CmdWalkToDummyVerified @0x43cd18` | 3 {1,1,1} |
| `WalkToDummyRotateVerified` | `VIBE_Script_CmdWalkToDummyRotateVerified @0x43cc68` | 3 {1,1,1} |
| `PlayAnimation` | `VIBE_Script_CmdPlayCharacterAni @0x43cdc8` | 3 {1,6,1} |
| `PlayAnimationSound` | `VIBE_Script_CmdPlayCharacterAniSound @0x43ce98` | 5 {1,6,1,1,6} |
| `PlayAnimationScript` | `VIBE_Script_CmdPlayCharacterAniScript @0x43cfb0` | 5 {1,6,1,1,6} |
| `PlayAnimationScriptInt` | `VIBE_Character_CmdPlayAnimationScript @0x43d0f8` | 6 {1,6,1,1,6,1} |
| `GetCharacterHandle` | `VIBE_Character_CmdGetCharacterHandle @0x43d208` | 1 {6} |
| `TakeObject` | `VIBE_Character_CmdTakeObject @0x43d260` | 4 {1,6,1,1} |
| `TakeObjectLeft` | `VIBE_Character_CmdTakeObjectLeft @0x43d30c` | 4 {1,6,1,1} |
| `TakeObjectScript` | `VIBE_Character_CmdTakeObjectScript @0x43d3b8` | 6 {1,6,1,1,1,6} |
| `GiveObject` | `VIBE_Character_AttachItemToBone2 @0x43d524` | 2 {1,6} |
| `DropObject` | `VIBE_Character_CmdDropObject @0x43d548` | 4 {1,6,1,1} |
| `DropObjectLeft` | `VIBE_Character_CmdDropObjectLeft @0x43d600` | 4 {1,6,1,1} |
| `SitDown` | `VIBE_Character_CmdSitDown @0x43d6b8` | 2 {1,6} |
| `SitDownAtOnce` | `VIBE_Character_CmdSitDownAtOnce @0x43d738` | 2 {1,6} |
| `GetUp` | `VIBE_Character_CmdGetUp @0x43d7c4` | 2 {1,6} |
| `SetCharacterCamera` | `VIBE_Character_CmdSetCharacterCamera @0x43d844` | 2 {1,6} |
| `MoveCharacterCamera` | `VIBE_Character_SetCameraViewMode @0x43d8f0` | 3 {1,6,1} |
| `PreloadAnimation` | `VIBE_Character_PreloadAnimation @0x43d9a4` | 5 {1,6,6,6,6} |
| `PreloadSitMesh` | `VIBE_Character_CmdPreloadSitMeshStub @0x43d9f0` | 1 {1} |
| `CharacterIdle` | `VIBE_Character_IsIdle @0x43d9f4` | 1 {1} |
| `StopCharacter` | `VIBE_Character_Stop @0x43da2c` | 1 {1} |
| `KillCharacterAnimations` | `VIBE_Character_CmdKillCharacterAnimations @0x43da5c` | 1 {1} |
| `DummyBlocked` | `VIBE_Character_CmdDummyBlocked @0x43dab4` | 2 {1,1} |
| `CharacterCount` | `VIBE_Character_CmdCharacterCount @0x43db90` | 0 |
| `LookAtCharacter` | `VIBE_Character_CmdLookAtCharacter @0x43dbc8` | 2 {1,1} |
| `LookAtObject` | `VIBE_Character_CmdLookAtObject @0x43dcb0` | 2 {1,1} |
| `SetCharacterToDummy` | `VIBE_Character_CmdSetCharacterToDummy @0x43dd38` | 2 {1,1} |
| `CharacterSitting` | `VIBE_Character_IsSitting @0x43ddd8` | 1 {1} |
| `GetCharacterSubObjectHandle` | `VIBE_Character_CmdGetCharacterSubObjectHandle @0x43de10` | 2 {1,6} |
| `SetCharacterStepSample` | `VIBE_Util_StrCopyToNormalBuf @0x43de48` | 1 {6} |
| `SetLowPoly` | `VIBE_Character_SetLowPoly @0x43de74` | 2 {1,1} |
| `AttachObject` | `VIBE_Character_CmdAttachObjectToBone @0x43debc` | 3 {1,6,6} |
| `SetCharacterTransperancy` | `VIBE_Character_CmdPlayCharacterAni @0x43df30` | 2 {1,1} |

### 3.4 Sound — `VIBE_Script_RegisterSoundCommands @0x440df0`

| Command | Handler | argc / types |
|---------|---------|--------------|
| `LoadSampleBank` | `VIBE_Sound_LoadSampleBankDefault @0x440cf0` | 1 {6} |
| `KillSampleBank` | `VIBE_Audio_UnloadSampleBankThunk @0x440d00` | 1 {1} |
| `PlaySampleHandle` | `VIBE_Sound_PlaySampleAsVoice @0x440d0c` | 2 {1,6} |
| `PlaySample` | `VIBE_Sound_CmdPlaySample @0x440d28` | 2 {6,1} |
| `PlaySample3D` | `VIBE_Sound_CmdPlaySample3D @0x440d5c` | 4 {1,1,6,1} |
| `StopSample` | `VIBE_Sound_CmdStopSample @0x440db0` | 1 {1} |
| `GetSampleBankHandle` | `VIBE_Sound_FindBankByNameThunk @0x440dc0` | 1 {6} |
| `GetSampleHandle` | `VIBE_Sound_PlaySampleThunk @0x440dc8` | 2 {1,6} |
| `SpeechQueued` | `VIBE_Voice_PlayQueuedSampleThunk @0x440dd4` | 5 {1,1,6,1,1} |

Plus the event tokens (§2.5): `SND_W2, SND_W1, SND_M1, SND_M2, SND_M3, SND_HS,
SND_NONE, SND_VAR` via `VIBE_Script_AddEventToken`.

> **Note on the audio backend (rules 5/6).** The handlers above ultimately call
> the audio subsystem; in the original these route through MSS32. The script
> *engine* is reconstructed 1:1; the audio *backend* is the SDL substitution.

---

## 4. Loading, compiling, and executing a script

### 4.1 Tokenizer seeding — `VIBE_Script_ConsoleParseLine @0x4453a4`

Despite the name, this is the **subsystem initializer**, run once from app init. It:

* Allocates the engine globals:
  * `dword_62E8AC` (`0x3400`) — the **command table** (§2.1).
  * `dword_62E8B0` (`0x20`) — current command-line buffer (`"evt:commandLine"`).
  * `dword_62E8B4` (`0x400`) — console buffer.
  * `dword_767950` (`0x1080`) — per-instance error/run-log lines (`132 *` 32 slots).
  * `dword_62E8A4` (`0x50C00` = 330752) — the **coroutine record pool**
    (`"evt:script"`).
* Zero/`-1`-initializes the record pool in **stride 2584** (`330752 / 2584 = 128`
  records; the loop writes `record+128 = -1` (handle) every 2584 bytes).
* Seeds the tokenizer's symbol tables (`byte_767958` = punctuation/operator table,
  5-byte stride; `byte_767450` = keyword table, 16-byte stride) by copying the
  literal strings into them. The recognized lexemes are:
  * Operators / punctuation:
    ` `, `.`, `-`, `+`, `--`, `++`, `=`, `==`, `!=`, `{`, `}`, `[`, `]`,
    `(`, `)`, `"`, `;`, `,`, `//`, `<`, `|`, `&`, `||`, `&&`, `<=`, `>`,
    `>=`, `/`, `*`.
  * Keywords: `while`, `do`, `if`, `return`, `else`, `#include`, `#singlestep`,
    `#normalstep`, `#multistep`.

So the language is C-flavoured with C++-style `//` comments, `if/else`,
`while`, `do`, `return`, an `#include` directive, and three per-script **stepping
mode** pragmas (single / normal / multi step — see §4.6).

### 4.2 Loading `.scr` files

* `RunScript "<name>"` → `VIBE_Script_LoadAndRunMain @0x43c690`:
  * `VIBE_Script_LoadFromScriptDir @0x4424e0` builds the path
    `"%s%s"` with prefix `aXEngineGfxScri = "x:\engine\gfx\scripts\"`
    (`0x62E7A4`) and calls `VIBE_Script_LoadScript @0x4421f0` to read the file
    into a coroutine record.
  * `VIBE_Script_RunMain @0x44396c` then:
    1. `VIBE_Script_CompileBlock @0x4435d0` — compiles the source into the
       record's function/symbol tables; on failure `VIBE_Script_DestroyContext`
       and return 0.
    2. `VIBE_Script_LookupFunction(rec, "main")` — finds the entry point; if
       absent, logs `"evt_RunScript:No entrypoint(main) found in script..."`.
    3. `VIBE_Script_EnterFunction` pushes the `main` frame, sets the **active
       bit** (`record+164 |= 1`), points the statement cursor
       `record+2472 = record+168`, clears the step mode (`record+2564 = 0`), logs
       `"Run script: %s"` into the 32-slot run-log ring (`dword_767950`,
       `dword_62E8C0`), and links the record into the owning universe slot
       (`record+2576 = dword_649D60`) and the current handle (`record+132 =
       dword_62E8D4`).

`RunScriptInt` / `RunScriptString` are the same with one extra argument copied
into `main`'s first parameter.

### 4.3 Per-frame stepping — `VIBE_Script_StepAllActive @0x445250`

Called once per frame from `VIBE_GameLogic_RunFrameLoop`:

```c
for (i = 0; i != 330752; i += 2584) {            // every record in the pool
    rec = dword_62E8A4 + i;
    if (rec[164] & 1) {                          // "active" bit set?
        dword_62E8A8 = rec;                      // "current instance" global
        VIBE_Script_Step(rec, ...);              // advance one slice
    }
}
dword_62E8A8 = 0;
```

`dword_62E8A8` is the **current-instance** global that every command handler and
the tokenizer read implicitly (a register-less "this" for the running coroutine).

### 4.4 Stepping one coroutine — `VIBE_Script_Step @0x4450e0`

For the current record it:

* Maintains a small **call/handle stack** (`dword_7653CC[]`, depth in
  `dword_62E8E4`, max 32) so nested `RunScript`/function calls restore the prior
  current-instance on return.
* Decides whether to run this frame by checking the record's handle
  (`+132`), a "blocked by another command" slot (`+2572`), the suspend bit
  (`+164 & 2`), and the source pointer (`+152`).
* If a native command is **pending** (`+2528 != 0`) it resumes it via
  `VIBE_Script_InvokeCommand`; otherwise it runs statements via
  `VIBE_Script_ExecuteStatement` in a loop. The loop condition
  `record[2564] == 2 && (record[164] & 1)` is the **multistep** mode: keep
  executing statements this frame until the script yields or deactivates.
* Wraps execution in `VIBE_Universe_SwitchActiveSlot` so the script runs against
  its owning world/universe slot (`+2576`).

### 4.5 Executing one statement — `VIBE_Script_ExecuteStatement @0x444bd0`

Pulls the next token from the source cursor (`record+152`) via
`VIBE_Script_NextToken`, then dispatches on the **token type** byte:

| Token type | Meaning | Action |
|------------|---------|--------|
| 1 | keyword | sub-dispatch on keyword id (see below) |
| 2 | variable | `VIBE_Script_AssignVariable @0x4445bc` |
| 3 | user function | `VIBE_Script_EnterFunction @0x4431dc` (push frame) |
| 4 | **command** | `VIBE_Script_CallFunction @0x444774` (call native, see §5) |
| 8 | type keyword | `VIBE_Script_ParseDeclaration @0x4413d0` |
| 11 | include symbol | jump statement cursor into the included block |
| 12 | end-of-block `}` | clear active bit (`+164 &= ~1`), return |

Keyword sub-dispatch (token type 1, id in `v24[0]`):
`1=while` → `ParseWhileLoop`, `2=do/for` → `ParseForLoop`,
`3=return` → evaluate expression, store result in `dword_62E8D0`, finish/exit,
`4=if` → `DispatchTokenBranch`, `5=#include` → `ParseInclude`,
`6/7/8` → set the per-record **step mode** byte `record+2564` to `1`
(`#singlestep`), `0` (`#normalstep`), or `2` (`#multistep`).
The block-nesting handlers manage the loop/if frame stack at `record+2472`
(12-byte frames, count at `+8`, with the loop's start cursor at `+48` and a
"repeat?" flag at `+56/+57`).

Unknown identifiers raise `"Unknown symbol : '%s'"`; unsupported keywords raise
`"Keyword is not surported!"` and finish the script.

### 4.6 Tokenizer — `VIBE_Script_NextToken @0x441974`

Reads from `record+152`, skips spaces, then classifies the next lexeme into the
output token record (`*out = type`, payload at `out+1`/`out+4`):

| Type | Lexeme class |
|------|--------------|
| 1 | operator / keyword (id in `out+4`) |
| 2 | known **variable** (`VIBE_Script_LookupVariable`) |
| 3 | known **user function** (`VIBE_Script_LookupFunction`) |
| 4 | known **command** (`VIBE_Script_FindCommandByName` → §2.4) |
| 5 | integer literal (`VIBE_Util_ParseInt`) |
| 6 | float literal (`VIBE_Util_StrToDouble`) |
| 7 | string literal (text between `"` … `"`) |
| 8 | type keyword (`VIBE_Script_ParseTypeKeyword`) |
| 11 | include symbol (`VIBE_Script_LookupInclude`) |
| 12 | end of source |

Identifier classification order is exactly: variable → type keyword → user
function → **command** → include. `(`/`)` track paren depth in `dword_62E8E0`;
the operator id `14` (`(` after a function/command name) primes string-literal
capture. This is how a script line like `WalkToDummy(h, d);` tokenizes
`WalkToDummy` as a **command** (type 4) and routes it to `CallFunction`.

### 4.7 Finishing — `VIBE_Script_Finish @0x443f38`

Clears the pending/blocked command slots, then: if the script defines an `exit`
function (`VIBE_Script_LookupFunction(rec,"exit")`) and isn't force-killed
(`+164 & 2`), it enters `exit` in **multistep** mode and steps it once; otherwise
it clears the handle (`+128 = 0`), clears the active bits (`+164 = 0`), detaches
from any owning command (`+2580`), logs `"Script done..."`, and
`VIBE_Script_DestroyContext`'s the record (freeing the pool slot).

---

## 5. From a script line to a `cm_*` command

This is the bridge to the simulation ([19 — Commands & netcode](19-commands-netcode.md),
[21 — Economy](21-economy-office-stats.md)).

When `ExecuteStatement` sees a command token it calls
`VIBE_Script_CallFunction @0x444774`, which:

1. Evaluates each argument via `VIBE_Script_EvaluateExpression`, coercing per the
   registered **type code** at `entry+36+i`: `1`→int into a temp, `2`→variable
   ref, `6`→string (copied into a 96-byte-per-arg scratch buffer in `_OWORD
   v25[]`), `7`→float.
2. Validates the argument count by checking the trailing `)` token
   (`byte_62E8C4 != 13` → `"Expecting ')'...possible wrong parameter-count"`).
3. **Calls the native handler** through `entry+44`, choosing the thunk shape by
   `argc` (the `case 0..7` ladder): 0/1 args → bare call, ≥2 → `__fastcall`-style
   call passing pointers to the marshalled argument slots
   (`v28, v27, v29, v30, v31`).
4. **Coroutine bookkeeping (the yield).** If after the call the record is in
   `#singlestep` mode with a pending command (`record[2564]==1 &&
   record[2528]`) **or** the handler installed `Sleep`
   (`record+2528 == VIBE_Script_CmdSleep`), `CallFunction` snapshots the
   arguments into the record's **resume area** (`record+2532 .. +2548`, string
   args duplicated via `VIBE_Memory_AllocFromFreeList(0x60)`), and stores the
   pending command back-pointer at `record+2524`. On the next frame
   `VIBE_Script_InvokeCommand @0x444f4c` re-dispatches that same command with the
   saved arguments — i.e. the command can return "not done yet" and be re-polled
   until it completes. This is how `WalkToDummy`, `PlayAnimation`, etc. block the
   script for multiple frames without blocking the engine.

A command handler "enqueues a `cm_*`" by calling into the command/netcode layer
during step (3). For example, the economy/AI path builds the
`"cm_RequestSellObjekt(%i, %i, %i, %i, %i, %i)"` request (format string at
`0x619998`, referenced 25× from `VIBE_MeisterAi_TradeManageStorage @0x45f1e4`,
`VIBE_MeisterAi_TradeGeneral @0x4614d0`, and the two remote-purchase AIs) and
submits it through the command system documented in
[19 — Commands & netcode](19-commands-netcode.md). The script engine and the AI
share this `cm_*` request vocabulary: scripts request gameplay effects, the
command layer applies them deterministically across the network.

### 5.1 `Sleep` — the canonical yield (`VIBE_Script_CmdSleep @0x43c708`)

```c
// gilde.exe 0x43c708 — VIBE_Script_CmdSleep (eax = arg ptr)
if (dword_62E8CC && (*(handler*)(dword_62E8CC+44) == VIBE_Script_CmdSleep)) {
    // resumption poll: arg == -1 (sleep forever) OR start + ms/14 > now -> stay asleep
    if (*arg == -1 || rec[2568] + *arg/14 > dword_62EB38)
        rec[2528] = VIBE_Script_CmdSleep;          // remain pending
    return 0;
} else {
    rec[2528] = VIBE_Script_CmdSleep;              // first call: install pending
    rec[2568] = dword_62EB38;                      // record wake-clock baseline
    return 1;
}
```

`dword_62EB38` is the scaled game clock (same one `GetTime` reads, §3.1); the
record field `+2568` stores the sleep start, `+2528` is the **pending-command**
slot the stepper polls. `Sleep(ms)` therefore re-installs itself each frame until
`start + ms/14` has passed, then clears and lets the script continue.

---

## 6. Coroutine record layout

Each script instance is a **2584-byte record** in the pool at `dword_62E8A4`
(128 records). Offsets recovered from the stepper, finisher, loader, and command
machinery:

```c
// gilde.exe — script coroutine record (stride 2584, pool = dword_62E8A4)
struct ScriptInstance {
    /* +0x000  */ char    name[...];        // script/source name (logged)
    /* +0x080  */ i32     handle;           // +128: instance handle (-1 = free slot)
    /* +0x084  */ i32     parentHandle;     // +132: owning/parent handle (==dword_62E8D4 at start)
    /* +0x098  */ char*   srcCursor;        // +152: current source/statement pointer (tokenizer reads here)
    /* +0x0A4  */ u8      flags;            // +164: bit0 = active/running, bit1 = suspended/force-kill
    /* +0x9A8  */ struct  blockFrame[..];   // +2472: loop/if frame stack (12B frames; count at +8, start cursor +48, repeat flags +56/+57)
    /* +0x9DC  */ i32     pendingCmdSaved;  // +2524: command to re-invoke on resume (InvokeCommand)
    /* +0x9E0  */ void*   pendingCmd;       // +2528: pending native command (e.g. Sleep) — non-0 means "yielded"
    /* +0x9E4  */ ...     resumeArgs[..];   // +2532..: saved marshalled args for the pending command
    /* +0xA00  */ u8      stepMode;         // +2564: 0=normal, 1=singlestep, 2=multistep
    /* +0xA08  */ u32     sleepBaseline;    // +2568: game-clock baseline captured by Sleep
    /* +0xA0C  */ i32     blockedByCmd;     // +2572: set while blocked by an external command
    /* +0xA10  */ i32     universeSlot;     // +2576: owning universe/world slot (==dword_649D60)
    /* +0xA14  */ i32     ownerCmdRec;      // +2580: back-pointer to owning command record (cleared/-1 on finish)
    /*  ...    total 2584 bytes (0xA18)  */
};
```

Key engine globals:

| Global | Meaning |
|--------|---------|
| `dword_62E8A4` | base of the 128-record coroutine pool |
| `dword_62E8A8` | **current instance** being stepped (implicit "this") |
| `dword_62E8AC` | base of the 256-entry command table |
| `dword_62E8C0` | run-log ring index (0..30) |
| `dword_62E8CC` | command record currently dispatching (used by `Sleep` resume test) |
| `dword_62E8D0` | last `return` value |
| `dword_62E8D4` | current parent handle; reset to `-1` by `VIBE_Script_ResetCurrentHandle @0x445d7c` |
| `dword_62E8E4` / `dword_7653CC[]` | nested-call handle stack + depth |
| `dword_62EB38` | scaled game clock (`Sleep`/`GetTime` reference) |
| `dword_767944` / `dword_767948` | event-token table base / count |
| `dword_767950` | per-instance run/error-log lines |

---

## 7. Helper / lookup entry points

| Function | Addr | Role |
|----------|------|------|
| `VIBE_Script_FindByHandle` | `0x442174` | linear scan of pool by `handle` (+128); stride 2584 |
| `VIBE_Script_FindActiveByHandle` | `0x4ba284` | spin-runs the frame loop while the handle is still active (blocking wait for a script to finish) |
| `VIBE_Script_RunWaitLoop` | `0x487098` | modal wait: pump `RunFrameLoop` until the handle's script ends or an abort flag fires, then `Finish` it and reset mouse state |
| `VIBE_Script_ResetCurrentHandle` | `0x445d7c` | `dword_62E8D4 = -1` (clears the "current parent handle") |
| `VIBE_Script_LookupFunction` | `0x4415f8` | resolve a name to a compiled user function in the record |
| `VIBE_Script_LookupVariable` | `0x4414d4` | resolve a name to a script variable |
| `VIBE_Script_LookupInclude` | `0x4415bc` | resolve an included-block symbol |
| `VIBE_Script_ParseTypeKeyword` | `0x441788` | recognize a declaration type keyword |
| `VIBE_Script_CompileBlock` | `0x4435d0` | compile source → function/symbol tables |
| `VIBE_Script_DestroyContext` | `0x445a28` | free a record's compiled context / pool slot |

`VIBE_Script_FindActiveByHandle` and `VIBE_Script_RunWaitLoop` are the two
**synchronous** ways the rest of the game waits on a script (e.g. an intro
cut-scene): instead of returning to the main loop, they drive
`VIBE_GameLogic_RunFrameLoop` themselves until the target coroutine deactivates.
```
