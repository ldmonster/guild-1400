# HARDEN world_04 — 1:1 verification vs gilde.exe (IDA MCP)

Chunk: 22 files under `src/world/` (office/privilege/statistics/stammbaum/straftat/
road-network/production/finance). Every `gilde.exe 0x…` provenance decompiled via
the IDA MCP and diffed line-for-line; every `@0x…` const table byte-diffed with
`get_bytes`. Only proven divergences were changed; evidence addresses cited inline.

## Verdicts per function

### office_recon_privilege.cpp
- `0x49d9e0 VIBE_Office_DestroySessionActors` — **VERIFIED-1:1** (byte-offset loop
  i+=4 over 64, inner skip incl. `|| abortFlag` short-circuit).

### office_wages.cpp
- `0x57b480 VIBE_Amt_ComputeOfficeWages` — **VERIFIED-1:1**. Early-outs
  (`word==-1 || byte>=10 || !byte_12CE918 || (!A && !B)`), seat B then seat A,
  `(double)rank * flt_6258AC * flt_6258B0 * lawRate` with ConvertX truncation,
  bonus `6400*(RandomModulo(base+2)+base)` at state 3. Constants byte-verified:
  flt_6258AC=100.0f (0x42C80000), flt_6258B0=32.0f (0x42000000).

### player_finance.cpp
- `0x591600 SumStoredMoney` — **VERIFIED-1:1** (found → sum; -1 sentinel;
  uninit-accumulator quirk documented).
- `0x592b50 ComputeTopWealthList` — **VERIFIED-1:1** (5-slot board, strict `>`
  entry, bubble-up with the redundant class re-check, 768×536 walk).
- `0x592c18 CheckExamFeeAffordable` — **VERIFIED-1:1**; flt_626A8C=0.01f,
  flt_626A90=1/32768, dbl_626A94=0.15 byte-verified.
- `0x4c4b60 He_ShowFineAmount` — **VERIFIED-1:1**; flt_61E638=0.03f verified;
  variant dispatch (unsigned edx; 1→amount, 3→(4451,4,amount), else bare 3v+4442).

### privilege_cmd.cpp / privilege.cpp
- `0x561700 SendSimpleCmd` — **VERIFIED-1:1** (6/7 → overview, +457&4 → -127/2;
  v31=rand%9+6518 before v30=rand%9+6509; 16/96).
- `0x561a74 SendBuildCmd` — **VERIFIED-1:1** (overview 1/0; op90(-2); 16/96).

### office_recon2_rules.cpp — all **VERIFIED-1:1**
- `0x57c1e8` staff model (gate `(double)u16 >= float`, RR `(x+1)%8`, type-17
  `rand%3`, 76/27-cap table scans keyed on the sign-extended +0x74/+0x75 bytes,
  LABEL_39 fallbacks). `0x47e6c4` (al,0,3,0,255). `0x49da18` fallback strings
  byte-verified @0x61C388/98/A8. `0x51fc50` (count loop `<768 && bytes<16`, key
  byte +360, card layout `v34+v33*(p+1)+10*(p-1)+margin*p`, `y=130*(i/2)+40`,
  enable gate `count>=4 || holder+360`). `0x4a03f4` successor gate. `0x5210e4/
  0x521234` text id `(gender?560:525)+byte`. `0x56499c` miracle amounts
  (`rand%3+2`, `rand%5+3`, dbl_624D64=0.01 / flt_624D6C=0.01f byte-verified;
  strict `<` argmin).

### privilege_law.cpp — all **VERIFIED-1:1** (hook boundary)
- `0x47fcf0` (gate rec+2==19; age/money rolls `max+1-min`; apply ignored),
  `0x47fc24` (sticky parity ==2 seed, rolls, `1-parity` flip; v3-garbage
  documented), `0x555eb4` (a3==-1 uninit-return → 0 documented), `0x556108/23c/
  370` (pitch 112, y=v7+40, x=(pageW-contentW)/2, label(half,2*half,8,68),
  SetEnabled slot 2), `0x49db90` (stride 268 words, owner at +97 dwords → +136
  deref, pre-increment 1-based out, caps 768/31).

### privilege_panels_a.cpp — 7 fixes, rest VERIFIED
- `0x563000 PanelGenerateHatred` — **FIXED**: office arm made an extra 3rd
  `relationEntry(p1,p2)` call; the binary performs exactly TWO lookups
  (0x56347a p1→p2 into the first delta, 0x56348e p2→p1 into the second — disasm:
  ecx=L1+0x7F, eax=L2+0x7F, ebx-=ecx, var_38-=eax). Passive arm was already
  correct. Factor flt_624CBC=0.02f verified.
- `0x563614 PanelInterrogation` — **FIXED**: Args25 3rd arg 0 → **0x8000** in
  both arms (`mov ecx,8000h` @0x56367d / 0x56389a; ecx survives — EnqueuePacket
  pushes/pops ecx @0x49388d). Rest verified (-2/-3, 6368/-127, 16/96/32).
- `0x5639f4 PanelExpelWorker` — **FIXED**: Args25 3rd arg 0 → **0x80**
  (`mov ecx,80h` @0x563a92 / 0x563d24). Passive BuildOp90 2nd arg documented as
  an indeterminate leftover edx in the binary (post-QueryBegin), modeled 0.
- `0x560500 PanelMedicus` — **FIXED** (passive): BuildOp90 arg 0 → **-2**
  (`mov edx,-2` @0x560584) and Args25 3rd arg 0 → **0x40** (@0x560589). Office
  arm (already 64) verified @0x5607de. flt_624B48=0.02f verified.
- `0x5608b0 PanelDivorce` — **FIXED** (passive afford gate): was
  `currency < 0.12*wealth`; binary divides: `(double)lo/(double)hi < dbl_624B74`
  (@0x56091f). Now reproduced as the division. dbl_624B64=0.08 / dbl_624B6C=0.04 /
  dbl_624B74=0.12 byte-verified. DeltaField 0x5C ×2, msg 0x1990 verified.
- `0x5643e8 PanelConvert` — **VERIFIED-1:1** (kind<10 [+6/7 excl. passive only],
  religion byte cmp via dword>>24, `charm > byte994 + rand%0x7E`, delta field
  0xC, count msg 6626, office return = uninit cl → 0 documented; flt_624D34=0.06f).
- `0x563f14 PanelMakePeace` — **VERIFIED-1:1** (passive RNG drawn before the
  office404>=3 gate; rolls %0x28+15; -3; 6631/6632; 16/96/32).
- `0x560c1c BlackmailConfirm` — **VERIFIED-1:1** (roll%8 <= matchCount; +96 time
  + slot-reset; 456&0x100 → 6490; fail −26/−2/6489).
- `0x560f14 PanelBlackmail` — **VERIFIED-1:1** (457&2 → 6482; evidence>=3;
  -110 latch; passive %8>count → −26 else slot-reset; 16/96).
- `0x5647c8 PanelApology` — **VERIFIED-1:1** (passive RivalPairs→BuildOp90 order;
  office BuildOp90→RivalPairs; dragField 1..9 ≤ office404).
- `0x5627cc CharmConfirm`, `0x565304 PanelChangeProfession` — **VERIFIED-1:1**
  at the documented hook boundary (skill −4/−8 category rule noted).

### privilege_panels_b.cpp — 7 fixes, rest VERIFIED
- `0x561bb4 PanelEnactLaw` — **FIXED ×3**: (a) Args25 first arg is the SUBJECT
  handle `*(v48+4)` (v48 = a1 or link @0x561c12/0x561bdd), not the actor — both
  arms; (b) office-arm Args25 3rd arg = the class mask v47 (ecx reload
  @0x561f82), was 0; (c) the non-office "forbidden" comparand `v7 == v46` is the
  law-record int @ **+0x18** (v46 @ebp−0x40; record buffer @ebp−0x58), not the
  min @+4. Masks 0x80000/0x100000, rank<2→32, v51=−2 verified.
- `0x562334 PanelEmbezzlement` — **FIXED** (both arms): Args25 args were
  (subject,0,0,4); binary: (subject handle, **456** [lea/sub @0x56239e],
  **0x400000** [ecx @0x5623e9/0x56250a], 4, 0). Amount math verified:
  `(rand%0xB0+25) * flt_624C2C(=0.005f byte-verified) * wageA`, ConvertX trunc;
  uninit v27 return documented.
- `0x561fd0 RemoveFromOffice` — **FIXED** (both arms): Args25 3rd arg 0 →
  **0x200000** (`mov ecx,200000h` @0x562180 / 0x562201). Coord27 −40/−20,
  removable `v31[16]==1`, "absetzen" bracket verified; the intervening
  `VIBE_Office_AddEntryForCharacter` noted as hook-boundary.
- `0x5628c8 PanelCounterEspionage` — **FIXED ×2**: office arm emits
  BuildOp90(−4) BEFORE the agent scan (cap 5); non-office arm gates rank<4→32
  FIRST, then BuildOp90(−4), then the uncapped scan (was scan-first).
- `0x56589c BuildEvidenceEntry` — **FIXED**: accuser id read used
  `personIdById[536*found]`; binary reads `word_12CE910[268 * v11]` (@0x565a3e)
  — the same 268-word record stride as the judge branch → `[268*found]`.
  Everything else verified line-for-line: judge scan (589-stride type byte ==15),
  record fields (+4=44, +8/+0x58 actor, +0x5C target, +0x36=2, +0x60..0x6C=−1),
  mode/bit12 judge sourcing (B when set), rand%2 fallback (nonzero→A), accuser
  17/17 suppression + matchCount>2 handle, witness filter routing
  (filter[2]==0xFFFF ? [2] : [3]), 16/64/16 return codes.
- `0x4c247c Gesetz_RequestApply` + `0x4c244c GetRecord` — **VERIFIED-1:1**
  (index>=26→−1 signed; null target enqueues with −1; marker 0xFFFF→−1; clamp
  min@+4/max@+8).
- `0x571218 ShowDialog` — **VERIFIED-1:1** (0→prv_small, 1→prv_big, else
  prv_very_big).
- `0x562cdc PanelSwapSeats` — **VERIFIED-1:1** (pairs 15→{13,14}, 21→{19,20},
  27→{25,26}; remaining=2 scan over byte_12CEA76, 768 cap; rank>=6 && both &&
  promote → 16 else 32; office: >0 left → 0x19CB ret 0; skill 6; ret 1).
- `0x5651bc/0x5651d9 PanelMiracle` — **VERIFIED-1:1** (kind7→0; office skill 4,
  −4, rand%6, member table, ret 1; non-office rank>=4 → 16 else 32).
- `0x565b88 EvidenceDetails` — **VERIFIED-1:1** (0xFFFF→0; count<1→96;
  `v28 |= field37==1`; self → v28=0; always returns 0).
- `0x565f9c / 0x5667a0 EvidenceReview(/Alt)` — **VERIFIED-1:1** (concrete path:
  actor+358==13→96, no target→96, target+358==13→96, count==0→96, then
  BuildEvidenceEntry mode 0/1; office HUD arm verdict init 32).

### production.cpp — **VERIFIED-1:1**
- `0x59064c ComputeOutputOverTime` / `0x590a3c ComputeDailyHourOutput`: full
  disasm walk. Work-hour tables byte-verified: flt_6476FC={8,7,8,9},
  flt_64770C={20,21,20,19}; flt_626A04=60.0, 360.0, flt_626A0C=60.0. Three-part
  integration (first-day clamp, whole-window middle days accumulated through
  double, final-day clamp of the END tod read at [ecx+4]/[ecx+6]) matches the
  reimpl's `integrateWindow` exactly; pause-mode DiffMinutes (0x5832bc) and
  Compare>0 gate (0x583230) verified. Note: reimpl guards negative days in the
  `%4` weekday (binary would read out-of-table floats — unreachable input,
  documented).

### relation.cpp
- `0x5942fc LookupMatrixEntry` — **VERIFIED-1:1** (self→127; signed byte at
  0x123D6D0 + 768a + b via the `dword>>24` alias).

### road_network.cpp — 6 fixes (0x592d98), 0x592c7c VERIFIED
All six proven from the raw disasm; the aliased 44-byte record store made the
prior transcription land several writes one record off:
1. **swapRec base**: was `buf+2`; the `rep movsd` swaps at `dword_12CDD8E+2` =
   record base **+0x28** (0x592fe9/0x592ffc/0x59300b, and again in the per-level
   sort 0x5932e1..). Without this the bubble sorts never moved depth/cost.
2. **Level-0 spread** writes `dword_12CDD6C[+44]` — `add edx,2Ch` BEFORE the
   store @0x5930e4 → node j's X lands in the **cost slot 0x30+44j** (record j+1).
   Was written to 0x04+44j, leaving level-0 costs 0 for the relaxation pass.
3. **Run-spread loop advance**: outer j advances by ONE (`add ecx,2Ch; inc ebx`
   @0x59331b) — overlapping re-runs on freshly spread costs are the original
   behavior; reimpl skipped to the run end.
4. **v91 rescale factor** is stored as a 4-byte float (`fstp [esp+var_40]`
   @0x593511), then reloaded for every multiply; was kept in double.
5. **Rescale X store** also lands one record up (`add eax,2Ch` @0x59356c before
   the store @0x593589) — overwriting the cost slot just read, in place.
6. **Y placement** stores at `dword_12CDD70[+44]` (`add ecx,2Ch` @0x5935f7,
   store @0x593602) = abs 0x34+44j; mirror-out now reads X/Y from the +1-record
   slots — proven by the consumer `VIBE_Building_BuildUpgradeTree` @0x59361c
   reading node X/Y as `LOWORD(dword_12CDD98[11j])` / `LOWORD(dword_12CDD9C[11j])`.
- Golden test values (266/22/510/22, Y 16/112/112/208) re-derived by hand under
  the fixed layout — identical; no pin changes needed.
- `0x592c7c ComputeBuildingChainDepth` — **VERIFIED-1:1** (memo 0xFFFF, lo/hi
  parent link scans over the nodeId word at +0x28, max of branches).

### stammbaum.cpp / stammbaum_query.cpp / stammbaum_tree.cpp
- `0x55ab84` gather kernel — **VERIFIED-1:1** (father +0x5C with 0x1CA&4 rule on
  both records, mother +0x64, spouse +0x60; spouse-children v17 sequence
  0→1→(2→4)→5 cap 4 — v17=0 at loop entry PROVEN: `xor ecx,ecx` @0x55acf2 and
  both callees preserve ecx; own-children category<10, v19+=8 cap 40).
- `0x58c408 GetFamilyRecord` — **VERIFIED-1:1** (cat 5/6/7, +81 sign, word+80
  HIBYTE=0 then &0xF).
- `0x5555c8` — cap `v7 < 4` (kMaxHeirs) verified; rest of stammbaum.cpp is
  documented window-derived support (no per-line binary provenance).
- stammbaum_query.cpp — carries **no** binary provenance (support queries);
  nothing to verify 1:1.

### statistic_recon_chart.cpp — 2 fixes (0x58beb8), rest VERIFIED
- **FIXED**: `v41` (maxWealth × flt_6267CC) is stored as a FLOAT
  (`fstp dword` @0x58c1ba); was double.
- **FIXED**: the bar-height multiply consumes the UNROUNDED composite still on
  the x87 stack (`fst dword ptr [edx+1Ch]` @0x58c21f — fst, not fstp); was
  recomputed from the rounded float field.
- All 8 scale/weight constants byte-verified (0x6267CC..0x6267E8, bits match
  header FloatFromBits values); pass-1/2 structure (needs +128..132, religion
  (i16)byte+13, office def BYTE2 sums, favor loop excl. self/free kinds
  {4,5,6,7}, favor div-by-zero preserved, wealth clamp ≥1, double-call to
  ComputeTotalWealth noted as pure) verified. `0x55fa88` slider clamp
  (>6→7, <=0→0) + palette @0x552704 {0x7E..0xA8 step 6} byte-verified.

### statistic_recon_dump.cpp — 1 fix, rest VERIFIED
- `0x594ff8 DumpNpcIdentity` — **FIXED**: status table index reads the dword at
  byte **+353** (>>24 → byte **+356**); reimpl read +356→byte 359. Test pin
  updated: `rec[359]=9` → `rec[356]=9` (old→new, evidence
  `dword_8C3B48[*(int*)((char*)a1+353) >> 24]`).
- `0x594fd0/0x5953bc` headers, `0x5950a4` attributes (all nine bitfields use
  `shr` — logical, matching the u32 shifts; assembled double lo@36/hi=sar16@38;
  uninit %i documented), `0x595168` needs, `0x5951ac` traits (+358..361),
  `0x595208` skills (wealth-then-currency order, dwords 0x190..0x1A4),
  `0x595260` inventory (ids +92..+124, NIEMAND, 2-byte strcat), `0x5952dc`
  record driver (0xFFFF guard, 8120 zero-fill, stray strlen) — **VERIFIED-1:1**.

### statistics.cpp / statistics_full.cpp / statistics_report.cpp
- `0x579ad0` — **FIXED** (statistics.cpp): `StatisticsCategoryTotal` now sums
  HIGH offset first entirely in double (x87 80-bit chain `fld a[k+15]; fadd
  a[k+10]; fadd a[k+5]; fadd a[k]; fmul flt_62570C; fstp` @0x579b04..0x579b89)
  with a single final float rounding; was ascending float-precision adds.
  flt_62570C=0.25 and thresholds dbl_625714/1C/24/2C/34/3C
  (=0.15/0.2/0.3/0.5/0.55/0.45) byte-verified. Cadence gate, trend ids
  6174..6181, argmin ladder, weak/severity/luxury branches, kind==6 recipient
  walk — **VERIFIED-1:1** (statistics_report.cpp / statistics_full.cpp).
- `0x57a900` tax row `round-16+row`, 17 rows — **VERIFIED-1:1**.

### straftat_resolve.cpp / straftat_sync.cpp — all **VERIFIED-1:1**
- `0x4c36ec` (768×536 walk, kinds 6/7, `&= ~mask`), `0x4c39a4` (2048 pairs,
  45-byte-stride id scan incl. the record-0 pre-check, provenState==1 && perp
  match → newState; mode 0 reset-to-1), `0x4c33f4` (state 1→flag 0; >1 &&
  !handler → flag 1; 32-emit network throttle at hook boundary), `0x4c3728`
  (per-record perp resolve, kinds 6/7 recipient walk, evidence-pair scan, the
  `dword_12CEAF4[134*v8] &= v12` index-v8 bug preserved).

## Tests (all green)
| target | checks |
|---|---|
| world_road_network_test / _itest | 33 / 34 |
| building_upgrade_window_test | 37 |
| privilege_panels_a_test | 117 |
| privilege_panels_b_test / wire_..._itest | 212 / 13 |
| privilege_law_test / _itest / _e2e | 63 / 17 / 24 |
| office_recon2_rules_test / office_recon_privilege_test | 64 / 79 |
| statistic_recon_test | 2642 |
| world_events_test / world_history_blob_harden_test | 111 / 316 |
| gui_statistics_window_test / _itest / _e2e | 32 / 179 / 18 |
| straftat_table_test / _itest / _e2e | 123 / 119 / 55 |
| world_stammbaum_e2e_test / world_stammbaum_tree_test | 19 / 33 |
| world_relation_harden_test / sim_person_relations_test | 40 / 33 |
| player_finance_test / _itest / _e2e | 59 / 16 / 17 |
| world_amt_test / world_amt_office_test / world_officelaw_test | 99 / 81 / 52 |
| slice_production_test / _itest / _e2e / wire_production_test | 49 / 13 / 21 / 20 |
| world_economy_e2e_test / office_cluster_harden_test | 17 / 66 |
| world_history_full_test / world_amt_event_test | 148 / 94 |

Golden pin change: statistic_recon_test Identity — status byte moved
rec[359]→rec[356] (binary reads dword@+353 >> 24 at 0x594ff8).
