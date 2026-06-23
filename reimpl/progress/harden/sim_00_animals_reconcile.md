# Harden reconciliation — animal.cpp / animal_wander.cpp (orchestrator final pass)

Two sub-agents independently hardened the needs+animals cluster (reports
`sim_00_needs_queue_animals.md` and `sim_00_needs_animals.md`). They DISAGREED on two
animal sites and the later agent overwrote the earlier agent's correct fixes with WRONG
behavior. The orchestrator re-decompiled both sites and restored the binary-correct form.

## CONFLICT 1 — sheep sound-branch d100 burn (animal_wander.cpp) — RESTORED to binary
- gilde.exe VIBE_Animal_UpdateCat @0x483e34, else/sound branch @0x483e7b:
  `if (!*((_BYTE*)rec+4)) RandomModulo(0x64);`  -> cat burns a d100 ONLY when kind==0.
- gilde.exe VIBE_Animal_UpdateSheep @0x483fd4, else/sound branch @0x484015:
  `RandomModulo(0x64);`  -> sheep ALWAYS burns a d100 (unconditional).
- Agent A fixed this correctly (SoundBurnPolicy: cat conditional, sheep always).
  Agent B reverted it to `catSoundBurn` bool with sheep passing `false`, so sheep NEVER
  burned — a one-draw-per-call RNG desync of the entire RandNext stream.
- VERDICT: Agent B's revert was WRONG. Restored the policy enum:
  `if (soundBurn == kSheepSoundBurn || rec->kind == 0) RandomModulo(100);`
  Cat=kCatSoundBurn (wander gate <=0x1E=30), Sheep=kSheepSoundBurn (wander gate <=0x14=20).
  Evidence: decompile of 0x483e34 (0x483e7b conditional) and 0x483fd4 (0x484015 uncond).

## CONFLICT 2 — despawn lifetime float conversion (animal.cpp) — RESTORED to binary
- gilde.exe VIBE_Animal_Update @0x48364c, lifetime block @0x4837a4 (disasm):
  `add eax,834h` (threshold=spawnTick+2100); `cmp eax,gameTick; jnb` (unsigned skip);
  `sub ecx,eax` (denom); `xor edx,edx; div ecx` (UNSIGNED div); store qword {quotient,0};
  `fild qword` -> (float)(u32 quotient) — no signed (int) intermediate (high dword=0).
- Agent A fixed this to a direct u32->float. Agent B reverted it to
  `static_cast<float>(static_cast<int>(threshold / (unsigned)denom))` — the signed (int)
  step mis-signs any quotient >= 2^31.
- VERDICT: Agent B's revert was WRONG. Restored to all-unsigned:
  `unsigned denom = gameTick - threshold; unsigned quot = threshold / denom;
   float v14 = (float)quot;` matching the {u32,0} qword fild.
  Evidence: disasm 0x4837bd `xor edx,edx; div ecx`, 0x4837c1 `xor edx,edx` (hi=0),
  0x4837ca `fild [qword]`.

## Other animal/needs verdicts (agreed by both agents, confirmed)
- ai_needs.cpp 60-row kAiNeedsMethodDefs table: byte/VA-exact AND order-exact (the
  non-sequential ids 12-after-37 and 60-after-49 match the binary's RegisterFromIni call
  order). BuildScoreTable @0x4764e8 control flow + LoadDataFile @0x468a40 73-byte DFN
  schedule + MemMove(+80,+48,32) prev-mirror: VERIFIED-1:1 (rows 1/2 spot-confirmed by
  orchestrator: DummyUnversehrtheit 0x4796b0/0x469de0/0x469df0; DummyBeruf 0x4796b0/
  0x469df4/0x469e04).
- animal.cpp spawn type pick jump table @0x48360c, kind bytes (0/1/3/4/6/7): VERIFIED-1:1.
- animal.cpp herdCount=-1 spurious write removed (binary never touches +8 in spawners
  0x483b58..0x483d20): kept (behavior-neutral, byte-faithful).
- avatar.cpp Avatar_LookupById/FindOrAllocForPerson @0x4859b0/0x4859e0: VERIFIED-1:1.

## Compile
animal.cpp, animal_wander.cpp, ai_needs.cpp, avatar.cpp: g++ -std=c++17 -fsyntax-only rc=0.

## Counts (orchestrator-final for this cluster)
RESTORED-TO-BINARY (agent-B reverts corrected): 2  (sheep d100 burn; despawn u32->float)
Net fixes standing: sheep-burn, despawn-conversion, herdCount-removal.
VERIFIED-1:1: ai_needs table+control flow, animal spawn pick, avatar lookups.
