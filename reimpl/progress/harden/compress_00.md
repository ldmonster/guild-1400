# Harden report — compress_00 (decompression/compression subsystem)

Scope: `src/compress/{crc,deflate,gzip,inflate,md5,trees,zlib}.cpp` (+ owned headers).
Method: every `// gilde.exe 0x…` function decompiled via IDA MCP and diffed
line-for-line; every `@0x…` const table fetched with `get_bytes` and diffed
programmatically. 3 proven divergences fixed; everything else VERIFIED-1:1.

## Fixes (with binary evidence)

| # | Function | Address | Divergence | Evidence |
|---|----------|---------|------------|----------|
| 1 | `TrAlign` (_tr_align) | 0x601b34 | Reimpl set `last_eob_len = 7` **before** the `1 + last_eob_len + 10 - bi_valid < 9` check; the binary reads the *previous block's* `last_eob_len` for the check and stores 7 only at the very end. | disasm: load `[edx+16ACh]` @0x601c24 (after first `bi_flush`, before the compare with 9); the only store `mov dword ptr [edx+16ACh], 7` is @0x601d2b, after the conditional second EOB. |
| 2 | `RunDeflate` (deflate header) | 0x5ed5f0 | Reimpl used the zlib-1.2 level_flags ladder (0/0/1/1/1/1/2/3/3/3). The binary uses the zlib-1.1.4 formula `level_flags = (level-1) >> 1` clamped to 3 → 3/0/0/1/1/2/2/3/3/3 (level 0 wraps and clamps to 3). Diverged for levels 0, 2, 5 (FLG byte of the zlib header). | decompile @0x5ed5f0: `v8 = (v4[31] - 1) >> 1; if (v8 > 3) v8 = 3; v9 = (v8 << 6) | (((v4[10]-8) << 12) + 2048);` (`v4[31]` = level @+124). |
| 3 | `InflateFast` (inflate_fast) | 0x60a1f0 | Reimpl omitted zlib's UNGRAB on all four exit paths (kZOk / kZStreamEnd / 2× kZDataError): the binary returns over-grabbed whole bytes from the bit buffer to the input stream (`c = min(avail_in - n, k>>3); n += c; p -= c; k -= c<<3`, bit buffer left untouched). Without it, if the final EOB is decoded in the fast path (possible whenever ≥6 bytes follow the deflate data, e.g. embedded/zlib-framed streams with trailing data), the adler-trailer bytes stay in the bit buffer, `next_in` is desynced, and CHECK4..1 reads the wrong bytes → false "incorrect data check". | decompile @0x60a1f0: every epilogue computes `v26 = a6[1] - v63; if (v11>>3 < v26) v26 = v11>>3; p -= v26; n += v26; bitk = k - 8*v26` before storing state (return-0 path, EOB return-1 path, both −3 paths). Verified end-to-end: 60000-byte zlib stream + 64 trailing bytes now inflates and adler-checks correctly. |

## Tables — all diffed against `get_bytes`, byte-identical

| Table | Address | Result |
|-------|---------|--------|
| extra_lbits / extra_dbits / extra_blbits | 0x5FFC30 / 0x5FFCA4 / 0x5FFD1C | VERIFIED-1:1 |
| bl_order | 0x5FFD68 | VERIFIED-1:1 |
| static_ltree (288×4B) / static_dtree (30×4B) | 0x5FFD7C / 0x6001FC | VERIFIED-1:1 (StaticInit generation reproduces the recovered bytes exactly) |
| _dist_code (512) / _length_code (256) | 0x600274+0x600374 / 0x600474 | VERIFIED-1:1 |
| base_length / base_dist | 0x600574 / 0x6005E8 | VERIFIED-1:1 |
| configuration_table (10×12B incl. func ptrs) | 0x5ECFD8 | VERIFIED-1:1 (func 0/1/2 ↔ 0x5ee020/0x5ee1b0/0x5ee530) |
| CRC-32 table (256×4B) | 0x5EE990 | VERIFIED-1:1 (poly 0xEDB88320 generation matches all 256 entries) |
| gzip magic | 0x64A7BC | VERIFIED-1:1 ({0x1f, 0x8b} as dwords) |
| border / inflate_mask | 0x5FEAC0 / 0x64AFD4 | VERIFIED-1:1 |
| cplens / cplext / cpdist / cpdext | 0x607F00 / 0x607F7C / 0x607FF8 / 0x608070 | VERIFIED-1:1 (incl. the two 112 invalid markers) |
| fixed_bl=9 / fixed_bd=5 | 0x64B018 / 0x64B01C | VERIFIED-1:1 |
| fixed ltree (512×8B) / fixed dtree (32×8B) | 0x64B020 / 0x64C020 | VERIFIED-1:1 — regenerated via a port of the reimpl's `HuftBuild` and byte-compared, **including** the stale-`r.base` quirk (invalid entries at dtree slots 15/31 carry base=24577 because the binary's huft_build leaves `r.base` unassigned on the out-of-values path; the reimpl reproduces this). |

## Functions — per-address verdicts

### trees.cpp
| Address | Function | Verdict |
|---------|----------|---------|
| 0x600664 | TreeInit (_tr_init) | VERIFIED-1:1 |
| 0x6006cc | InitBlock (init_block) | VERIFIED-1:1 (note: binary also zeroes `matches` @state+5800; the field is written nowhere else and read nowhere — the binary's inlined tally does not touch +5800 — so omitting the dead field is behavior-identical) |
| 0x600748 | PqDownHeap | VERIFIED-1:1 (heap@+2900, heap_len@+5192, depth@+5200; tie-break `<=` on depth) |
| 0x600864 | GenBitlen | VERIFIED-1:1 (bl_count words @+2868; HEAP_SIZE 573; overflow redistribution identical) |
| 0x600ae0 | GenCodes | VERIFIED-1:1 (16-bit `code` truncation semantics equivalent for all trees build_tree can produce) |
| 0x600b5c | BuildTree | VERIFIED-1:1 |
| 0x600e10 | ScanTree | VERIFIED-1:1 (REP codes at bl_tree idx 16/17/18 = offsets 2740/2744/2748) |
| 0x600f08 | SendTree | VERIFIED-1:1 (inlined send_bits paths match, incl. `count--` when curlen!=prevlen) |
| 0x60168c | BuildBlTree | VERIFIED-1:1 (`opt_len += 3*(max_blindex+1) + 14`) |
| 0x6016fc | SendAllTrees | VERIFIED-1:1 (order lcodes-257/5, dcodes-1/5, blcodes-4/4; 3-bit bl lengths) |
| 0x601a5c | TrStoredBlock | VERIFIED-1:1 (copy_block called with header=1 via ecx @0x601ae9/0x601b22) |
| 0x601b34 | TrAlign | **FIXED** (see fixes table) |
| 0x601ea4 | TrFlushBlock | VERIFIED-1:1 (opt/static_lenb `(x+10)>>3`; stored if `stored_len+4 <= opt_lenb && buf != -1`; last→BiWindup) |
| 0x6021a0 | CompressBlock | VERIFIED-1:1 (ltree[code+257] @+1028; dist>>7 high table; EOB @+1024; last_eob_len from ltree[256].dl) |
| 0x602720 | SetDataType | VERIFIED-1:1 (bins 0-6, ascii 7-127, bins 128-255; `bin <= ascii>>2` → ASCII) |
| 0x602790 | BiReverse | VERIFIED-1:1 |
| 0x6027ac | BiFlush | VERIFIED-1:1 |
| 0x602848 | BiWindup | VERIFIED-1:1 |
| 0x6028b8 | CopyBlock (VIBE_Quant_EncodeRunLength) | VERIFIED-1:1 (len lo/hi, ~len lo/hi; `--len != -1` loop count) |

### deflate.cpp
| Address | Function | Verdict |
|---------|----------|---------|
| 0x5edb40 | ReadBuf | VERIFIED-1:1 (adler before copy when !noheader) |
| 0x5ed578 | FlushPending | VERIFIED-1:1 (documented adaptation: avail_out clamp can't bind on the unbounded vector sink; byte stream identical) |
| 0x5ed54c | PutShortMSB | VERIFIED-1:1 |
| 0x5edbc0 | LmInit | VERIFIED-1:1 (config fields good@+0/lazy@+2/nice@+4/chain@+6 of 0x5ECFD8 rows) |
| 0x5edc94 | LongestMatch | VERIFIED-1:1 (limit = strstart-(w_size-262) else 0; unroll-by-8; u16 chain compare; nice/lookahead clamps) |
| 0x5ede64 | FillWindow | VERIFIED-1:1 (more==-1 → -2 quirk; slide copies exactly wsize; head/prev rebase with `m<wsize→0`) |
| 0x5ee020 | DeflateStored | VERIFIED-1:1 (max_block_size min(0xFFFF, pending_buf_size-5); flush conditions identical) |
| 0x5ee1b0 | DeflateFast | VERIFIED-1:1 (note: binary keeps `hash_head` across iterations; reimpl resets it — provably output-identical since a stale head can only produce match_length ≤ lookahead < MIN_MATCH → literal path either way, and match_start is never read before being rewritten) |
| 0x5ee530 | DeflateSlow | VERIFIED-1:1 (filter `len<=5 && (strategy==1 || (len==3 && dist>4096))`; max_insert; prev_length-2 insert loop) |
| 0x5ed5f0 | RunDeflate (deflate) | **FIXED** level_flags (see fixes); rest of the single-shot Z_FINISH driver matches the binary's FSM for the reachable path (header → flush_pending → strategy → adler trailer, `noheader=-1`) |
| 0x5ed068 | DeflateInit2 | VERIFIED-1:1 (hash_shift=(hash_bits+2)/3; lit_bufsize=1<<(memLevel+6); pending_buf=4*lit_bufsize; d_buf/l_buf at +lit_bufsize/+3*lit_bufsize — reimpl uses separate vectors, safe because zlib's overlay guarantees no aliasing effect on output) |
| 0x5ed3b0 | Reset | VERIFIED-1:1 (folded into DeflateInit2 tail; status = noheader?113:42, adler=1, TreeInit+LmInit) |
| 0x5ed8b4 | End | VERIFIED-1:1 (resource release; RAII adaptation) |

### crc.cpp / zlib.cpp / md5.cpp
| Address | Function | Verdict |
|---------|----------|---------|
| 0x5eed90 | CrcGetTable | VERIFIED-1:1 |
| 0x5eed98 | CrcCompute | VERIFIED-1:1 (binary unrolls by 8 — byte-identical to the plain loop; `~crc` in, `~i` out, null→0) |
| 0x1418a40 | Crc16 table init | VERIFIED-1:1 (v=0xC0C1; v=(2v)^0x4003 generator, entries start at 0) |
| 0x1418ae0 | Crc16Update | VERIFIED-1:1 |
| 0x5ffaf0 | Adler32 | VERIFIED-1:1 (null→1; NMAX 5552 blocks; unroll-16 ≡ byte loop; mod 0xFFF1 per block) |
| 0x1418c20 | Md5Init | VERIFIED-1:1 |
| 0x1418c70 | Md5Update | VERIFIED-1:1 (count from old count[0]; buffer @ctx+24) |
| 0x1418da0 | Md5Final | VERIFIED-1:1 (incl. the trailing `memset(ctx,0,4)` = state[0]=0 quirk) |
| 0x1418e80 | Md5Transform | VERIFIED-1:1 (all 64 additive constants extracted from the decompile match; message order and all 64 rotations (incl. the `>>27|<<5`, `HIWORD|<<16`, `>>28|<<4` idioms) match; RFC 1321 vectors pinned in tests) |

### gzip.cpp
| Address | Function | Verdict |
|---------|----------|---------|
| 0x5ebdac | Gunzip (CheckHeader + raw inflate) | VERIFIED-1:1 for the framing logic (magic dwords, method==8, reserved 0xE0, skip order FEXTRA→FNAME→FCOMMENT→FHCRC, FEXTRA len = lo + (hi<<8)). Documented adaptations: in-memory (no transparent-copy fallback for non-gzip; trailer CRC/ISIZE left to caller per gzip.h). |

### inflate.cpp
| Address | Function | Verdict |
|---------|----------|---------|
| 0x6080e8 | HuftBuild | VERIFIED-1:1 (logic diffed line-for-line + byte-proof: a Python port of the reimpl code regenerates the binary's precomputed fixed tables 0x64B020/0x64C020 exactly, including the stale-base invalid entries; MANY=0x5A0 pool cap) |
| 0x608760 | InflateTreesBits | VERIFIED-1:1 (error mapping -3/-5/bb==0; -4 passes through without msg) |
| 0x6087fc | InflateTreesDynamic | VERIFIED-1:1 (success requires `*bd || nl <= 257`; all four msg/error mappings match; no PKZIP_BUG_WORKAROUND) |
| 0x60899c | GetFixedTrees | VERIFIED-1:1 (bl=9 bd=5 @0x64B018/1C; tables byte-identical) |
| 0x607d30 | RunBlocks (inflate_flush) | VERIFIED-1:1 (two-segment drain, `end==write → write=window` reset preserved; avail_out clamp + `-5→0` promotion are unreachable adaptations with the unbounded sink) |
| 0x60a1f0 | InflateFast | **FIXED** — UNGRAB added on all four exit paths (see fixes) |
| 0x60751c | InflateCodes | VERIFIED-1:1 (state offsets need@+12/tree@+8/len@+4/lit@+8/dist@+12/lbits@+16/dbits@+17/ltree@+20/dtree@+24; WASH single-byte pushback; BADCODE returns −3 without msg; every exit re-flushes) |
| 0x6074a0 | CreateBlocksState (codes_new) | VERIFIED-1:1 |
| 0x5fec3c | InflateBlocks | VERIFIED-1:1 (TYPE byte-align math; LENS ~len check; STORED min(left,n,m); TABLE 29-bounds; BTREE border fill + bb=7; DTREE 16/17/18 repeat logic incl. `c==16 && i<1` guard; DTREE→dynamic bl=9/bd=6; CODES/DRY/DONE/BAD flow; post-flush `read!=window` wrap guard differs only where unbounded output makes m==0 impossible after flush) |
| 0x5feb0c | BlocksReset | VERIFIED-1:1 (check out-param; frees blens on mode 4/5, codes on mode 6; check=adler(0,0,0)=1 when checkfn) |
| 0x5feb7c | BlocksNew | VERIFIED-1:1 (allocation adaptation) |
| 0x5ffa80 | BlocksFree | VERIFIED-1:1 (RAII adaptation) |
| 0x5eca44 | Process (inflate) | VERIFIED-1:1 (METHOD/FLAG checks incl. `(cinfo+8)>wbits` and `%31`; BLOCKS r-code dance collapses to the same bool result single-shot; CHECK4..1 big-endian accumulate + compare + "incorrect data check"; DONE→1, BAD→−3. Adaptation: FDICT path returns kZNeedDict immediately instead of consuming the 4 dict-id bytes first — both are hard failures for the bool API, and no game stream uses FDICT) |
| 0x5ec8c4 | Init2 | VERIFIED-1:1 (binary errors on wbits outside 8..15, reimpl clamps — unreachable: public ctors pass ±15 only) |
| 0x5ec824 | Reset | VERIFIED-1:1 (mode = noheader?BLOCKS:METHOD) |
| 0x5ec87c | End | VERIFIED-1:1 (RAII adaptation) |

## Out-of-chunk findings
None. (`_dist_code`/`_length_code` etc. confirmed as standard zlib-1.1.4 tables at
the addresses recorded in the sources; no game-specific patches found in any of
them.)

## Tests
Built and run (GUILD_GAME_DIR set): all green.

| Target | Checks |
|--------|--------|
| compress_deflate_test | 36 / 0 fail |
| compress_zlib_test | 59 / 0 fail |
| compress_hash_test | 13 / 0 fail |
| compress_malformed_test | 273 / 0 fail |
| compress_inflate_window_test | 18 / 0 fail |
| savestate_decompress_test | 26 / 0 fail |
| compress_deflate_e2e_test | 12 / 0 fail |
| compress_zlib_e2e_test | 9 / 0 fail |
| compress_hash_e2e_test | 17 / 0 fail |

Total: 463 checks, 0 failures. No golden pins needed changing (none pinned the
level-0/2/5 FLG byte, the TrAlign bit pattern, or the fast-path input pointer).
The InflateFast fix additionally verified with an ad-hoc harness: a 60000-byte
zlib stream followed by 64 trailing garbage bytes now inflates with a correct
adler check (previously the trailer bytes could be stranded in the bit buffer).
