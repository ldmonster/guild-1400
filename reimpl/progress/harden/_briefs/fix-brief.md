# Wave-H1b — fix the 15 baseline test failures 1:1 (MCP LIVE)
The tree builds clean; 15 tests fail because a prior `git stash` accident froze an inconsistent
source/golden state into these modules (the correct versions are unrecoverable from git). Each
failure is a 1:1 divergence: decompile the relevant gilde.exe function, determine the CORRECT
behavior, and fix the SOURCE and/or the GOLDEN test to match the binary exactly.

ABSOLUTE RULES (the stash accident is why we're here):
- NEVER run ANY git command. No git stash, no git checkout, no git clean, no git reset. None.
- NEVER delete or recreate the build/ directory. Build with: cmake --build build -j --target <your test target>.
- Edit ONLY the files you are assigned + their tests. Do not touch other modules.
- 1:1: ConvertX@0x5c6b08 truncates; bare fistp rounds-nearest; verify every float->int site.
  get_bytes every constant. Disasm is reference of record where Hex-Rays is wrong. If a golden
  encodes wrong behavior, fix it to the binary value (cite addr+evidence); if the source is
  wrong, fix the source. For SEGFAULTs, find the real null/OOB and fix it 1:1.
- Run your specific failing test target(s) until they pass: 
  cd build && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original ctest -R <name> --output-on-failure
- NEVER commit. Do not edit progress/INDEX.md. Write progress/harden/fix_<cluster>.md.
