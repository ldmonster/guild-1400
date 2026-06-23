# Harden: src/io/ini_profile.cpp — Win32 private-profile (.ini) API

## Provenance / reference of record

`GetPrivateProfile{String,Int}A` and `WritePrivateProfileStringA` are **kernel32
import thunks** in gilde.exe — there is no in-binary parser to decompile:

| Symbol | IAT slot | Module |
|---|---|---|
| `GetPrivateProfileIntA`      | `0x60e6ac` | kernel32 (verified via `imports_query`) |
| `GetPrivateProfileStringA`   | `0x60e6b0` | kernel32 |
| `WritePrivateProfileStringA` | `0x60e754` | kernel32 |

`get_global_value` on each slot returns `0x0` (unbound import thunk). So the 1:1
reference is the **real kernel32 behavior**. The authoritative, reverse-engineered
reference for these functions is the Wine/ReactOS kernel32+kernelbase
implementation (`PROFILE_Load` / `PROFILE_CopyEntry` / `GetPrivateProfileIntW` ->
`RtlUnicodeStringToInteger`), which is the documented match for Windows for these
heavily-tested functions. This is the "clean-room impl that matches Win32/binary
behavior" case the harden brief anticipated. MSDN docs alone are NOT sufficient —
they diverge from real kernel32 (e.g. `#` comments, hex parsing).

## Live callers (confirmed via decompile)

- `GetPrivateProfileStringA`: `0x468f6c VIBE_AiMethod_RegisterFromIni` (keys
  `ShortDesire%li`/`ShortChange%li`/`LongDesire%li`/`LongChange%li`, default `""`
  at `0x61a304` — verified empty via `get_bytes`, nSize `0x80`, **nonzero return ==
  key present**); `0x56b834 VIBE_Config_ReadGfxAndSoundSettings` (`[Game] stadt`,
  default `"Augsburg"`, nSize `0x40`); plus net/menu/city callers per header.
- `GetPrivateProfileIntA`: `0x56b834` (all Gfx/Sound/Game numeric knobs, decompile
  shows defaults 0/1/50/100 etc.), `0x52ccd8`, `0x534bbc`.
- Real config files read this way: `Gilde.INI`, `server.ini` (inspected) — use only
  `;` comments, `key=value`, and quoted values in **commented-out** lines.

## Findings — per behavior

### FIXED (1:1 divergences from real kernel32)

1. **`#` was wrongly treated as a comment char.** kernel32 `IS_ENTRY_COMMENT`
   is `str[0]==';'` only — `#` is NOT special. A `#`-led line is a normal line
   (kept if it has `=`). Source fixed to comment on `;` only. Real game .ini uses
   only `;`, so no game-data regression; the fix restores exact kernel32 parsing.
   New test `HashIsNotACommentChar`.

2. **`getInt` integer parsing was decimal-only and returned the default on
   non-numeric values.** Real `GetPrivateProfileIntW`:
   - fetches the value via `GetPrivateProfileString` into a **30-WCHAR buffer**
     (default `""`); if that returns 0 (absent OR empty) -> `def_val`;
   - else `RtlUnicodeStringToInteger(base 0)`: skip chars `<= ' '`, optional single
     `+`/`-`, **auto-detect `0b`/`0o`/`0x` prefix** (binary/octal/hex), else base
     10 (a bare leading `0` is NOT octal); accumulate `0-9A-Za-z` digits in a
     **u32 (wraps mod 2^32, no overflow check)**, stop at first invalid/over-base
     digit; minus negates the u32 (two's-complement).
   - A present, non-empty, **non-numeric** value therefore yields **0**, not the
     default. Reconstructed exactly as `rtlStrToIntBase0()` + the 30-char buffer
     path. New tests `GetIntBaseAutoDetect`, `GetIntNegativeWrapAndJustZeroX`.

3. **Quote stripping handled only `"`.** kernel32 `PROFILE_CopyEntry` strips a
   single matching pair of EITHER `'` OR `"` (first char is the quote, ≥1 char
   after it, last char equals it). Added single-quote support. New test
   `SingleQuoteStripping`.

4. **Whitespace set too narrow.** kernel32 `PROFILE_isspaceW` trims `0x09..0x0d`,
   `0x1a`, `0x20` — not just space+tab. Widened `isWs()` to that exact set.
   (Within-line effect is VT/FF/0x1A; CR/LF are already consumed by line split.)

### Wrong golden FIXED (to binary behavior)

- `GetIntParsing`: `getInt("Gfx","stadt",88)` golden was `88` (assumed default on
  non-numeric). Real kernel32 returns **0** (key present + non-empty -> base-0
  parse of `"Augsburg"` stops at `'A'` -> 0). Source + golden now both 0; cited
  evidence: `GetPrivateProfileIntW` (kernelbase) + `RtlUnicodeStringToInteger`.

### VERIFIED-1:1 (already correct)

- Section/key matching: ASCII case-insensitive — matches kernel32.
- Key/value/section trimming of surrounding whitespace — matches `profile_trim_spaces`.
- Split on **first** `=`; line with no `=` ignored — matches.
- Section `[name]`: parse up to `]`, trailing content ignored, name trimmed — matches.
- Keys before first `[section]` go to an unnamed section, not reachable by named
  query, not enumerated as a section — matches kernel32.
- `getString` copy-out: ≤ nSize-1 chars + NUL, return = chars excl. NUL; nSize==0
  / out==NULL -> 0; default copied (and truncated) when key/section absent; default
  NOT quote-stripped — matches MSDN/kernel32.
- Empty value with non-empty default -> returns empty (len 0), default only on
  absent key — matches (GetPrivateProfileString returns the present empty value).
- Enumeration forms (NULL section / NULL key): per-name NUL + final double-NUL,
  truncation keeps double-NUL — MSDN-faithful (not reached by any live caller, per
  header note; retained for fidelity).

### BOUNDARY (intentional, not parse logic)

- File I/O (`readWholeFile` / `WritePrivateProfileStringA` write-back) routes
  through `shim::IFileSystem` per project platform-boundary rule. Parse logic is
  1:1; only the byte source/sink is the shim. Absent/unreadable file behaves as
  empty (-> defaults), matching kernel32 on a missing .ini.
- `serialize()` writes `"[section]\r\n" "key=value\r\n"` blocks for the write-back
  path. This is a re-emission format (not a decompiled kernel32 byte-for-byte
  rewrite); functionally faithful for the game's write callers. Flagged as
  re-serialization boundary; the *parse* round-trips 1:1.

## Test status

- Target: `ini_profile_test` — built clean (no warnings/errors).
- `ctest -R ini_profile` (with `GUILD_GAME_DIR`): **1/1 PASS**.
- Direct run: **92 checks, 0 failures** across 21 test cases (4 new:
  `GetIntBaseAutoDetect`, `GetIntNegativeWrapAndJustZeroX`, `HashIsNotACommentChar`,
  `SingleQuoteStripping`).

## Summary counts

- VERIFIED-1:1 behaviors: 11
- FIXED divergences: 4 (`#`-comment, getInt base-0/buffer/non-numeric, single-quote
  strip, whitespace set) + 1 wrong golden corrected to binary
- BOUNDARY: 2 (file I/O via shim, serialize re-emission)
