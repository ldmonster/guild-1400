# Harden sweep — gui text DB cluster (textdb / textfile_table / text_strtok)

MCP-live, DISASM-of-record verification of every provenance'd function in:
- `src/gui/text/textdb.cpp`
- `src/gui/text/textfile_table.cpp`
- `src/gui/text/text_strtok.cpp`

Tests built & green (from `build/`, `GUILD_GAME_DIR=.../europe_guild_1400_original`):
`gui_text_test`, `gui_textfile_table_test`, `text_cp1251_test`, `real_text_driver_test`
— 4/4 pass.

---

## textdb.cpp

### `RandomModulo` — VIBE_Math_RandomModulo @0x58b89c — VERIFIED-1:1
Disasm: `xor eax,eax; if(a1) return (int)RandNext() % a1`. `RandNext` @0x5cb8bc
returns `HIWORD(LCG)&0x7FFF` (0..32767, always non-negative), and `a1` is
`unsigned __int16` → promotes to `int` → signed modulo of two non-negative
operands. Reimpl `RandNext() % n` is identical. No float→int site.

### `StrCmpNoCase` (file-local) — VIBE_Util_StrCmpNoCase @0x5cb8f0 — VERIFIED-1:1
Reads `v3=*a1, v4=*a2`; lowercases A–Z (`>=0x41 && <=0x5A` then `+32`); breaks on
`v3!=v4 || !v4`; returns `v3 - v4` (unsigned chars widened). Byte-exact match.

### `FindIndex` — VIBE_Text_FindTextArrayIndex @0x44e0d8 — VERIFIED-1:1 (model)
Original: `if(!dword_8C36B0[0]) return -1`; loop `while(StrCmpNoCase(name, names[i]))`
with bound `i>=0x4000 || !dword_8C36B0[i] → -1`. The reimpl models the "text
pointer null / out of entries" stop as `i>=kMaxEntries(0x4000) || i>=Count()`, and
the empty-DB short-circuit as `Count()==0`. Argument order to StrCmpNoCase matches
(name, entry-name). Equality-only use → order-insensitive.

### `ParseRandomTextToken` — VIBE_Text_ParseRandomTextToken @0x44b8a0 — VERIFIED-1:1 (decode); UB-determinized
Disasm @0x44b8a0:
- `StrncmpN(a1,"{r",2)` gate. Reimpl's `token[0]=='{' && token[1]=='r'` is
  behaviorally identical to a 2-byte StrncmpN (verified incl. the `"{"`-then-NUL
  edge: both reject).
- `v2 = (u8)token[2] - '1'`; `jl/jg` ⇒ **signed** range test `v2<0 || v2>9` → error.
  Reimpl's `unsigned v2 = (u8)token[2]-'1'; if(v2<=9)` is equivalent (token[2]<'1'
  wraps to a value >9; token[2]>':' >9). Accepts exactly '1'..':' → tag (N-1)+9.
- The original writes `byte_767EB0[dword_62EB24] = var_10` where **`var_10` is an
  uninitialized stack slot** on the non-`{r` path AND on the syntax-error path
  (genuine UB). The reimpl determinizes this to `kTagNone(0)` — the only sound
  choice; reproducing uninitialized stack is impossible. Documented, not a bug.
- Cursor model: original stamps/returns `dword_62EB24` (the count = entry being
  built, pre-commit); the in-memory model stamps/returns the last committed entry
  (`Count()-1`). Equivalent placement under the model's commit ordering; the
  integration (BuildTextArray @0x44bb5c) is deferred, so this contract is internal
  and unit-tested. No churn.

### `PickRandomVariant` — RenderRichString random path — VERIFIED-1:1 (model)
Models `dword_8C36B0[base + (u16)RandomModulo(variants)]`: one RandNext draw,
`(u16)` truncation, `Text(base+offset)`. Draw count/order correct.

---

## textfile_table.cpp

### `FindSlot` — VIBE_Text_FindTextFileSlot @0x44d8f8 — VERIFIED-1:1 (model)
Original: fixed 128 slots, 112-byte stride, break when `StrCmpNoCase(slotName,
name)==0 && byte_77BEB0[112*i] != 0` (live = first name byte non-zero); else `-1`.
Reimpl scans `Count()(=128)`, tests `s.used && match`. `used` is the model's stand-in
for "name[0]!=0". Argument order (slotName, name) matches; equality-only.

### `FreeTextFile` — VIBE_Text_FreeTextFile @0x44d940 — VERIFIED-1:1
`slot=FindSlot; if(slot!=-1){ FreeDebug(blobPtr); blobPtr=0; } return slot`. Frees
ONLY the blob pointer, keeps the record (name/indices). Reimpl clears `blob` (+
shrink_to_fit), keeps name/used, returns slot/-1. Match.

### `FreeAllTextFiles` — VIBE_Text_FreeAllTextFiles @0x44d8a4 — **FIXED**
DISASM @0x44d8b4: inner `while(!*(int*)((char*)dword_77BF18+i))` skips slots whose
**blob pointer is null**, and frees + zeroes the full 112-byte record ONLY for
slots that own a blob. The live/name flag is **not** consulted.
- Before: `if (blob.empty() && !used) continue;` — wrongly also zeroed a
  used-but-blobless slot (clearing its name).
- After: `if (blob.empty()) continue;` — keys solely on blob presence, matching the
  binary; a named slot with no blob survives untouched.
Added golden `FreeAllKeepsBloblessNamedSlots` pinning this. Existing
`FreeAllClearsEveryRecord` (both slots have blobs) still passes.

### `LabelTable::Lookup` — VIBE_Text_LookupLabelEntry @0x44add4 — VERIFIED-1:1 (model)
Original: scan `unk_77F6B0` (80-stride) while `v6<0x3FFF`, return `dword_76BEB0[v6]`
on `StrCmpNoCase==0`, else `-1`. Reimpl caps `n` at `kLabelMaxEntries(0x3FFF)` and
scans the populated extent (`Count()`), returns parallel value / -1. Match.

### `ReloadTextFile` — VIBE_Text_ReloadTextFile @0x44d970 — VERIFIED-1:1 (layout) / BOUNDARY (VFS)
Read order verified against disasm: entryCount(4) → base(4) → last(4) →
offTable(4*count) → names(80*count each into byte_8D36B0[80*(base+i)]) →
tags(1*count into byte_767EB0[base+i]) → blobSize(4) → blob; then
`dword_8C36AC[base+1+i] = blob + offset[i]`. Confirmed `dword_8C36AC`(0x8C36AC) =
`dword_8C36B0`(0x8C36B0) − 4 (one dword), so `8C36AC[base+1+i] == 8C36B0[base+i]`
— the reimpl's `db.Add` at `base+i` is the same string slot (no off-by-one). The
ReadStream error sentinel `(u32)-1` confirmed at @0x4514bb (`v7=-1`), and `0` for
"nothing to read"; the reimpl `rd` lambda treats both as failure. File open/seek/
read + alloc/inflate machinery routed through the VFS shim (rules 3–5 boundary),
not reinvented.

### `SaveTextFile` — VIBE_Text_SaveTextFile @0x44de8c — VERIFIED-1:1 (layout) / BOUNDARY (VFS)
Write order verified: count(4) → base(4) → last(4) → offsets(4 each, original
`8C36B0[i]-blobPtr`) → names(80 each) → tags(1 each) → blobSize(4) → blob. The
reimpl re-packs the blob (NUL-terminated entries) and recomputes offsets so a
Save→Reload round-trip is self-consistent with the read path above. The bulk
single VfsWriteStream emits the identical byte sequence the original's many
WriteBuffered calls do. Path string `"<root>\german\textbin_<lang>\<name>.res"` and
the not-writable error branch modeled. File I/O is the VFS boundary.

### `0xFFFFFFFF` sentinels — VERIFIED-1:1
Vfs read/write return `(u32)-1` on error (confirmed @0x4514bb). Reimpl compares
against `0xFFFFFFFFu` for read (`rd`) and write (`SaveTextFile` return). Correct.

---

## text_strtok.cpp

### bit-mask table `kBitMask` — byte_62CEC0 @0x62cec0 — VERIFIED-1:1
`get_bytes` = `01 02 04 08 10 20 40 80`. Exact.

### `SetBitmapBits` — VIBE_Util_SetBitmapBits @0x5fe5a0 — VERIFIED-1:1
memset(out,0,32) (via SetGrayColorThunk(0,32,out)); `for(result=*p; *p; ){ ++p;
out[result>>3] |= byte_62CEC0[result&7]; result=*p; }` — reads the char before the
increment, indexes after; NUL's own bit never set; returns last byte (0 at term).
Reimpl matches instruction-for-instruction.

### `StrtokWhitespace` — VIBE_Text_StrtokWhitespace @0x5e9cd0 — VERIFIED-1:1
Disasm-checked:
- `v2=str; if(!str){ v2=ctx.saved; if(!v2) return 0; }`.
- Build bitmap from delims.
- Skip-leading-delims loop (loc_5E9CF8/D18): continue while
  `byte_62CEC0[c&7] & bitmap[c>>3]` set; the membership test is `test ecx,edx; jz
  break`. Reimpl `while(*v2 && in_set(*v2)) ++v2`.
- If `*v2==0` → original `return result` (=0); reimpl returns nullptr (0==null,
  equivalent).
- Scan loop: on a delimiter write NUL, `ctx.saved = i+1`, return v2; at string end
  `ctx.saved = 0` (NULL) and return v2. Reimpl matches exactly, incl. char
  signedness (unsigned char indexing). Static-pointer state modeled in
  `StrtokContext` (the original's `[off_64A90C()+16]` per-context slot).

---

## Counts
- VERIFIED-1:1: 13 functions/tables
  (RandomModulo, StrCmpNoCase, FindIndex, ParseRandomTextToken, PickRandomVariant,
   FindSlot, FreeTextFile, LabelTable::Lookup, sentinels, kBitMask, SetBitmapBits,
   StrtokWhitespace, + the Reload/Save on-disk layouts)
- FIXED: 1 (FreeAllTextFiles @0x44d8a4 — keys on blob pointer only; + new golden)
- BOUNDARY (VFS file I/O, rules 3–5): ReloadTextFile, SaveTextFile (logic 1:1; the
  open/seek/read/write/inflate machinery is the VFS shim)
- UB-determinized (documented): ParseRandomTextToken uninitialized-tag path → 0
