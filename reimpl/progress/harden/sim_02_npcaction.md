# Harden sweep — sim_02_npcaction (MCP-verified against gilde.exe)

Chunk files (only these + their unit tests were edited):
- `src/sim/charaction_npcaction_recon.cpp`  (+ `tests/unit/charaction_npcaction_recon_test.cpp`)
- `src/sim/charaction_npcaction_recon2.cpp` (+ `tests/unit/charaction_npcaction_recon2_test.cpp`)

Every provenanced function decompiled AND disassembled; every constant read with
get_bytes/get_global_value; every float->int site checked against the x87 disasm.

## Constants (recon2) — CONFIRMED byte-for-byte
- `dbl_61EB44` @0x61EB44: bytes `00 00 00 00 00 80 6f 40` = 0x406F800000000000 = **252.0**.
  Source `kDrinkSaturationCap = 252.0` VERIFIED.
- `flt_61EB4C` @0x61EB4C: bytes `31 0c c3 3c` = 0x3CC30C31 = **0.02380952425…** (~1/42).
  Source literal `0.0238095242f` round-trips to exactly 0x3CC30C31 (verified via struct
  pack). VERIFIED.

## Float->int (recon2 DrinkInit) — CONFIRMED truncation
`VIBE_Coord_ConvertX` @0x5c6b08 sets the x87 control word RC=chop (0x1F) and `frndint`s
ST(0) toward zero, then restores; the following `fistp` stores the already-integral
value. So `(int)(statByte*(1/42)+1.0)` = trunc toward zero. Operand is always >=1.0 so
trunc==floor. Source `static_cast<int>(scaled)` matches. Duration =
`(u8)(2*(int)scaled + 4)` matches `add al,al; add al,4`. Golden vectors (stat 251 ->
6.976 -> 6 -> 16) correctly encode the truncation. VERIFIED.

## Per-function results

### recon2 / 0x4d21ec DrinkInit — VERIFIED-1:1
Control flow (sat gate >= 252, self-skip pool scan with `goto proceed`, table re-read at
`proceed`), `sar` (signed) >>24 sub-method, row=u16@+8, ConvertX truncation, realtime
branch (v8=1/v9=0 else v9=5/v8=0), Advance(+82, v9, v8, 0), saved clock to +68. All
match disasm. No source or golden change.

### recon / 0x4ca938 BeginActionState7 — FIXED (3)
- **Early-return value.** 0x4ca94f: flag-4 short-circuit returns `eax==a1` (record
  pointer), not He_State. Source returned `He_State(h)`. FIXED -> `RecAsResult(h)`
  ((i32)(intptr_t)h). Golden `SpawnFlagShortCircuits` corrected (was `99`).
- **Actor id offset.** 0x4ca981 `mov ecx,[ebp+10h]` reads **record+16**; source used
  `He_CityId(h)` which is **+12**. FIXED -> `*(i32*)(HeBytes(h)+16)`. Golden
  `AbsentEntityArmsMinusOne` now writes +16 (was He_CityId/+12).
- **mixed45 id offset.** 0x4ca9d5 `mov eax,[eax+1]` reads **entity+1**; source used
  `HeBytes(ent)+4`. FIXED -> `+1`. Golden `PresentEntityNo437…` now writes ent+1.
Rest (stamp +82/+24h, type word 7@+86, scratch 0@+88, resolve, QueryFind(scene@+93,
1,0,437), ReqHandle=-1@+132, ent29 arg 0/-1) VERIFIED.

### recon / 0x4cbc20 BeginActionState20 — FIXED (2)
- **Early-return value.** 0x4cbc36: flag-4 short-circuit returns `eax==a1`, not
  He_State (which was just set to 0). FIXED -> `RecAsResult(h)`.
- **Loop slot count.** 0x4cbc7e..0x4cbcdd: loop is `ecx=record; … add ecx,4; cmp
  ecx,record+16; jnz` -> reads `*(int*)(ecx+172)` for ecx = record+0,+4,+8,+12 =
  **four** id slots at +172,+176,+180,+184. Source iterated only 3 (`off < 172+12`).
  FIXED -> `off < 172+16`. Golden `StrideOnePriorityByCategory` now sets +184 (SeqId=14)
  and asserts `findperson:14`.
Rest (state 0@+112, type word 20@+86, scratch 0@+88, stride==1 gate, RecId=rec+4,
category rec+2, prio 9 for 6/7 else 1, SlotReset28 packet modelled via coord27, ent29
arg 0) VERIFIED.

### recon / 0x4cf798 ArrestStep — VERIFIED-1:1
State ranges (`< -1` only -2 falls to release; `-2/-1` release+free; `>= 0` only 2 with
flag2 works; others pass through) match the `jge/jle/cmp 2` ladder exactly. args25(off
456=`[+1C8]-base`, val 0, sz 4, extra 256/512), coord27(RecId(a),RecId(b),-104),
messages: cat-of-b@`[esi+2]`->send to RecId(b) txt 6493; cat-of-a@`[edi+2]`->send to
RecId(b) txt 6492 (BOTH sends target RecId(b) — matches source). Stamp +82 (+2 min via
Advance(+82,0,0,2)), ent29(-1), ReqHandle@+132. No change.

### recon / 0x4d1d40 GuildJoinStep — FIXED (1)
- **flag-4 early-return value.** 0x4d1d5f -> 0x4d1d86 returns `eax==a1` (record
  pointer); source returned `state`. FIXED -> `RecAsResult(h)`. Added golden
  `SpawnFlagReturnsRecordPointer`.
Verified: -2/-1 -> free; packet gate (`reqhandle==-1 || GetStatus!=0`), status==0 path
returns 0; state dispatch (`jbe`==0 -> state-0 block, `cmp 1` -> state-1); state-0:
render 5800, `inc +184`, `+86 += 24`, ent29(retry>=3), RegisterApEvent((u16)*p,
-(+180), 0); state-1: GroupFromCode(`[+173] sar 24`), rank pair, BeginDelta, two
`RandomModulo(16)+16` fields, State22, op72, render 5801 to RecId(p), stamp+82, ent29(-1).
- BOUNDARY: the `word_12CE910` city/building table (pointer `v10/v6`) is data not in the
  tree; the source models its `*v10` (name word), `*(v10+4)` (rank id) and `*(v10+9)`
  (welcome/decline selector) plus the multi-arg `RenderFormattedMessage` via the
  collapsed `sendEntityMessage`/`beginDeltaPacket`/`requestBuildOp72` hooks. The state-1
  text-id selector reads `[esi+9]` (table) in the binary, not `[p+9]`; the source's
  `base` is computed-then-discarded (`(void)base`) so the modelled side effect is
  unaffected. Sub-method byte passed to GroupFromCode/op72 is the signed `sar` value;
  source narrows to u8 — same low 8 bits, only differs if bit7 set into the (inert) hook.

### recon / 0x4dc074 CancelEntityActions — VERIFIED-1:1
Null/0xFFFF record returns record. Self id = record+4. Seven pool scans with filters
**65,111,69,44,71,94,95** (0x41,0x6F,0x45,0x2C,0x47,0x5E,0x5F). Active gate `(fl&2)||
(fl&1)` on +120. 65/111/69: actor@+172==self -> peer@+176; else self==peer -> actor;
resolve + StampTimeAndRequest. 44: actor@+172 OR self==`+180`(4*45) OR self==`+192`
(4*48) -> quad60(actor,peer,0,He_Id) then stamp; else self==peer@+176 -> stamp only. 71:
actor||peer -> stamp. 94/95: actor-only -> stamp. Returns the (null) filter-95 cursor.
All offsets/branches match disasm. No change.

### recon / 0x4e55bc BeginCarryGoods — FIXED (1) + BOUNDARY
- **Clock2 minute/second zeroing.** 0x4e561b `mov dword[ebp+0CAh],0` (+202) and
  0x4e5626 `mov dword[ebp+0CEh],0` (+206) zero the second clock's **minute** (i32@+6 of
  the 14B image = +202) and **second** (i32@+10 = +206) AFTER the advance, while keeping
  day@+196 and stamping hour=8@+200. Source set only +200; it omitted the two zero
  writes. FIXED -> `HeR_Clock2(h).minute = 0; HeR_Clock2(h).second = 0;`. Existing golden
  `AdvancesSecondClockAndMirrors` only asserts `.day`/Clock2Word/CarryArg, still passes.
Verified: stamp +82; copy clock2 to +196; Advance(+196, 24*[+184], 0, 0); +200=8;
+188=[+184]; QueryBegin(&word_13CE860,1,1,[+172]); flag2 gate; FindRecordById(+176)
called once in-block; cat 6/7 gate; return carrier on skip / violation handle on report.
- BOUNDARY: the smuggling-report path is entangled with the `word_12CE910` table
  (`esi=&word_12CE910[536*carrier[+39]]`) and the multi-arg `RenderFormattedMessage` /
  `SendQuickjumpMessage` / `EvaluateViolation` (which also reads `dword_13CD6F2[189*
  byte_6477A1]>>16` and `*(carrier+1)`). The source models the category via the carrier
  record's +2 and the recipient/obj via RecId(carrier); the `viol:13:1:88:444:444`
  golden reflects that model. These global-table / formatted-render reads are data/leaves
  not in the tree and stay collapsed in the hook bridge (documented in the module note).

## Counts
- VERIFIED-1:1: 3 functions (ArrestStep, CancelEntityActions, DrinkInit) + 2 constants.
- FIXED: 4 functions, 7 distinct divergences total
  (State7 ×3, State20 ×2, GuildJoinStep ×1, BeginCarryGoods ×1).
- BOUNDARY: 2 (GuildJoinStep + BeginCarryGoods report paths — `word_12CE910` city table
  / multi-arg formatted-render leaves, already modelled via the inert hook bridge).
- Goldens updated: 4 (State7 SpawnFlag, State7 AbsentEntity, State7 PresentEntity, State20
  StrideOne) + 1 new (GuildJoin SpawnFlagReturnsRecordPointer). recon2 goldens unchanged.

## Build / handoff
- Both chunk .cpp compile clean (object targets built OK); both test TUs pass
  `-fsyntax-only` with the project include set.
- BLOCKER (not mine): the full `guild` library link fails on an **untracked** WIP file
  `src/gui/widget_layout.cpp` (`Widget` has no member `ld` — `w.ld<i32>(…)`), owned by
  another agent. Because all of `src/**` links into one library, the two test binaries
  cannot be run until that file compiles. No edits were made to it (out of my ownership).
  HANDOFF: the gui agent must fix/remove `src/gui/widget_layout.cpp` before
  `charaction_npcaction_recon_test` / `_recon2_test` can run.
