# Hardening sweep — chunk net_00

Files: src/net/{discovery,lobby,net_recon3_loadsync,net_savegame,net_session,session,transport}.cpp (+ headers/tests)

MCP-driven line-for-line diff of every `gilde.exe 0xADDR` provenance'd function against
the Hex-Rays decompilation + disasm. WinSock/recv/send/sendto/recvfrom are the rule-6
platform boundary (shim::INetSocket / INetDatagram) — protocol/packet MATH verified 1:1,
syscalls left as hooks.

Test targets built + green (no build/ touched):
net_discovery_test, net_lobby_test, net_recon3_loadsync_test, net_savegame_test,
net_session_test, net_test, net_lobby_itest, net_session_itest — 9/9 ctest PASS.

---

## transport.cpp

- **VIBE_Net_ConnectToServer @0x43b51c** — VERIFIED-1:1. Prologue resets (disconnected,
  rx/tx totals, buf ptrs, cursors) match 0x43b548..0x43b587; `!host -> false` (return -1);
  sockopt tuning is the shim's job (documented).
- **VIBE_Net_Disconnect @0x43b868** — VERIFIED-1:1. shutdown/close + disconnected=1.
- **VIBE_Net_SendPacket @0x43bc54** — VERIFIED-1:1. `dword_764CE0==-1 -> Closed(1)`;
  `!tx_buf -> Progress(0)`; total = `*(WORD*)(buf+1)`; send len `total-cursor`; n<0 (true
  error, WSAEWOULDBLOCK already mapped to 0 by shim) -> Teardown/Closed; cursor += n;
  `dword_62E5E0 += n` (tx bytes); `cursor != total -> Progress`; whole frame ->
  AccumulatePacketStats(tx) + clear. All exact.
- **VIBE_Net_ReceivePacket @0x43b8d0** — VERIFIED-1:1. Two-stage (header<3 then body to
  `*(WORD*)(buf+1)`); rx bytes `dword_62E5D8 += n`; sequence classification order EXACT:
  cmdId!=-1 → (LastReq+1==Count ? Advance, set LastReq) elif (Count==cm_LastSyncCount ?
  DupSync) elif (type==0x20 && [16]==0x0E ? Sync) else (Lost, LastReq=Count). Sync/DupSync
  do NOT touch dword_7652F8 — matches. Malformed-frame guards (total<3 / total>cap) are
  documented hardening that never fire on a valid [3,cap] frame (binary reads a fixed 153B
  global) — in-bounds path unchanged.
- **VIBE_Net_AccumulatePacketStats @0x43b260** — VERIFIED-1:1. Guard `pkt && len>pkt[0] &&
  pkt[0]<96`; dir a3: nonzero(rx)→dword offset 0, zero(tx)→offset +8 (4*2). Reimpl
  Rx→rx[type], Tx→tx[type]; per-type fields {recvCount@+0,recvBytes@+4,sendCount@+8,
  sendBytes@+12} confirmed against FormatStatistics global reads. ++count; bytes+=len.
- **EncodeHeader** — helper (no standalone binary fn); matches EnqueuePacket field stores
  (type@+0, u16 len@+1, flag@+3=0, u32 cmdId@+4, u32 Count@+8, u32 extra@+12=0), LE. OK.

## discovery.cpp

- **VIBE_Net_OpenBroadcastSocket @0x43aac0** — VERIFIED-1:1. `if (s==-1){ socket;
  setsockopt(SO_BROADCAST,1); bind(ANY:0); to=.255:htons(port) }` lazy/idempotent.
- **VIBE_Net_SendBroadcast @0x43aba8** — VERIFIED-1:1 (protocol). `if(s!=-1) return
  sendto(...)`. NOTE: when closed the binary returns the *data pointer* (cast); the
  portable API returns -1 instead (a pointer return is meaningless across the boundary and
  the value is never consumed — AdvertiseAndDiscover ignores it). Documented BOUNDARY detail.
- **VIBE_Net_CloseBroadcastSocket @0x43ab80** — VERIFIED-1:1. close + s=-1.
- **VIBE_Net_DiscoverServers @0x43abcc** — VERIFIED-1:1. `maxServers<=0 -> -2`; bind error
  -> -1; drain loop `while(now-start<timeoutMs && found<maxServers)`; accept predicate
  EXACT: `recv>=6 && buf[4..5]==106 && (buf[2..3]==1||==4) && buf[0..1]==1`; dedup by source
  IP across collected entries (StrCmp==0 → drop); kind1→42B blob+type 63, kind4→106B blob+
  type 127; return found. Blob copy clamps to received size (binary qmemcpy's fixed 40/104
  even past recv, reading stale stack) — documented defensive clamp, identical for the
  always-≥42/≥106-byte valid advert.
- **EncodeEntry** — VERIFIED-1:1. 128B record: IP@+0 (NUL), blob@+16, type dword@+124 LE,
  rest zero (leading memset). Matches v30 record geometry.
- **VIBE_Net_ResetStatistics @0x43b230** — VERIFIED-1:1. memset(dword_764CF8,0,1536) =
  96*16 table zero + four totals (62E5DC/D8/E4/E0) = 0.
- **VIBE_Net_FormatStatistics @0x43b2a8** — VERIFIED-1:1 (in the non-degenerate domain).
  Summary line args (recvPackets,recvBytes,sendPackets,sendBytes) and per-opcode loop over
  [0,0x60) with denominators dword_62E5DC/D8/E4/E0 and per-type 764CF8/CFC/D00/D04 all
  confirmed; `%02f%%` format matches golden ("25.000000%"). KNOWN DEVIATION: `Pct` guards
  total==0 → 0.0, where the binary divides unconditionally (0/0 → NaN, n/0 → inf). This
  only affects "format stats with zero traffic"; MSVC's "-1.#IND00"/"1.#INF" text is not
  portably reproducible on glibc, so the finite-output choice is retained and documented.
  The shipped golden (StatsFormatGolden) uses non-zero totals, so it agrees with the binary
  (0*100/4 == 0.0) — no wrong golden.
- **VIBE_Net_GetLocalHostAddress @0x43b440** — VERIFIED-1:1 (protocol). `if(cached) return`;
  else resolve (gethostname+gethostbyname+inet_ntoa) once and latch byte_62E5C8. DNS
  round-trip is the resolver callback (boundary).

## net_savegame.cpp

- **VIBE_Net_SendSaveGameToClients @0x5abfe8** — VERIFIED-1:1 (math/packetization). Pad len
  (0x5ac03d) + opcode-8 header (totalLen@+16) + opcode-9 128B chunks (zero-padded past
  fileLen). VFS read/alloc/Sleep/pump are leaves (boundary). NOTE: binary returns 0/1
  (fail/success); reimpl returns the chunk count (>0 == success) — caller truthiness
  (`!Send -> fail`) is preserved (chunks≥1 always, even empty save → 1). Documented.
- **PadSaveLength @0x5ac03d** — VERIFIED-1:1. `((len>>7)<<7)+128`; the binary's signed
  correction is a no-op for non-negative file lengths (always the case). Golden confirms
  exact-multiple-of-128 still adds a block.
- **VIBE_Command_HandleLoadBufAlloc @0x4964a4** — VERIFIED-1:1. total=pkt[+16]; alloc; cursor=0.
- **VIBE_Command_HandleLoadBufAppend @0x4964d8** — VERIFIED-1:1. memcpy(buf+cursor,
  pkt[+16],0x80); cursor+=128. `!allocated` early-out + per-byte bound are documented
  hardening (op9-before-op8 / over-append) that don't change the in-bounds path.
- **VIBE_Net_LoadReceivedSaveStream @0x5ac140** — VERIFIED-1:1 (transfer slice). The wait
  predicate `dword_11AA48C && dword_11AA464 >= dword_11AA490` is `complete()`; the reimpl
  delivers the byte-exact reassembled buffer. The full save-parse tail (OpenMemoryStream +
  LoadHeaderAndThumbnail + person/global/city/building tables + RelinkLoadedPointers + ~30
  VIBE_Save_* leaves) is DEFERRED (rule-8 out-of-cluster engine subsystems). Defensive
  total-vs-buf clamp documented; equals total when buf was sized to total (normal case).

## net_recon3_loadsync.cpp  (slices of VIBE_Net_LoadAndSyncSession @0x56da74)

- **SyncAckReset @0x56da94** — VERIFIED-1:1 (modeled). Binary: `for(r=0;r!=16;r+=2)
  dword_13CEC48[r]=-1` = 8 dwords at stride 2. The reimpl models the table as {slot[8],
  ack[8]} and resets both lanes. KNOWN MODELING NOTE: the binary's 13CEC48[even] and the
  count-lane 13CEC50 (=&13CEC48[2]) overlap (13CEC50[even] == 13CEC48[2,4,..16]); step-1
  writes 13CEC48[0..14], leaving the ack-lane slot at index 16 stale. The reconstruction
  abstracts the overlapping table into two clean lanes (documented in the header) — a
  faithful read of the *intent* (8 slot/ack pairs reset to -1), not the byte-exact aliasing.
- **CountRemotePlayers @0x56dad0** — VERIFIED-1:1. rows: liveMarker!=-1 && status∈{6,7} →
  ++byte_63CC1D (u8 wrap). 768 rows / stride 268 (=205824). Matches.
- **BuildSyncBlob @0x56dbbe** — VERIFIED-1:1. v29[0]=localPlayerId
  (dword_12CE914[134*word_63CC5C]), v29[1]=headerCrc32, rest of 31 dwords zero (124B).
- **SaveHeaderCrc32 @0x5dc6e0** — VERIFIED-1:1. Pass-through to compress::CrcCompute (reused,
  not redefined). Golden "123456789"→0xCBF43926 confirms standard reflected CRC-32.
- **CountAckedSlots @0x56dbe9 / AllClientsAcked** — VERIFIED-1:1 (modeled). Inner recount
  `for(i=0;i!=16;i+=2) if(dword_13CEC50[i]!=-1) ++v20`, looped while v20<byte_63CC1D. Reimpl
  counts ack[]!=-1; same modeling note as SyncAckReset re: the overlapping table.

## net_session.cpp

- **VIBE_Net_ReportWinsockError @0x43af00** — VERIFIED-1:1. All 17 dispatch arms diffed
  against the binary's balanced binary search; the (code→line,message) table is EXACT:
  10004→65, 10014→61, 10022→81, 10035→77, 10036→67, 10038→71, 10040→79, 10045→73,
  10050→59, 10052→69, 10053→83, 10054→87, 10057→63, 10058→75, 10060→85, 10093→57,
  default→89 ("Unknown winsock error %u!!!"). Strings byte-match .rdata (incl. the
  WSAECONNRESET "UPD-datagram"/">Port Unreachable<" typos). aUnitsNetNetC = "..\units\
  net\net.c". Severity literal 1 on every arm (the unknown arm's uninitialised v3 is
  observably 1; documented). Error-log sink is the installable boundary hook.

## session.cpp

- **VIBE_Net_RunWaitLoopWithStatus @0x4beb80** — VERIFIED-1:1. Copies status name into the
  staging buffer, QueueRequestFlagBlob32(14,...) (type 0x20 @+0, flag 14 @+16), loops
  `while !GetPacketStatusById(id)` calling RunFrameLoop(a1 | 0x300000). OR constant exact.
- **VIBE_Net_RunSyncWaitLoop @0x4beac8** — VERIFIED-1:1. dword_11BC2D0=a1; enqueue blob32(14);
  flags a1|0x300000; status text "sv_NetworkSync"; loop `while !GetPacketStatusById`. The
  timed message-box (timeGetTime+1500) is a GUI leaf (boundary).
- **VIBE_Net_RunWaitLoop @0x4bec44** — PARTIAL / documented divergence. Verified the
  opcode-14 blob32 enqueue, the `while status==0` loop, and the OR constant 0x210000.
  DIVERGENCE: in the binary the first arg (eax) is a *status-name string pointer* (call
  sites in VIBE_Office_RunCouncilSession / VIBE_Cutscene_RunParticipants load a .rdata
  string into eax) and the frame-loop flags come from the GLOBAL `dword_631598 | 0x210000`,
  not the argument. The reimpl signature `RunWaitLoop(u32 mode)` treats the arg as the flags
  and drops the name copy. dword_631598 is not modeled in this cluster and the function's
  real callers are outside net (Office/Cutscene). Left as-is (no test exercises its
  name/global semantics; correcting the signature would ripple the public session API with
  no in-tree caller to validate). Flagged for a future cross-cluster pass.
- **AllPlayersReady @0x56d930** — VERIFIED-1:1. `!byte_63CC40 -> 0`; walk 768 rows (102912/
  134); `byte_12CE918[i*4]` (alive) && status∈{6,7} && `dword_12CEB18[i]!=-1` → 0; else 1.
- **ConnectLocalStub / Connect / RunSyncCore / WaitAllReady / StartLocalSinglePlayer** —
  modeled glue over the reused CommandQueue + ISessionHook; control-flow shapes
  (standalone latch, sync-enqueue-then-pump-until-acked, bit-2 barrier) match the binary's
  single-player path. OK.

## lobby.cpp

- **BuildHostAdvert @0x528dac** — VERIFIED-1:1 (advert-build slice). magic 1 / kind 1 / tag
  106 / city name / playerCount=byte_63CC1D / extra=dword_122F490; session flags |= 0x10.
- **BuildSaveJoinAdvert @0x528f24** — VERIFIED-1:1 (advert-build slice). kind 4; flags |= 0x50.
- **EncodeLobbyAdvert / DecodeLobbyAdvert** — VERIFIED-1:1. Field offsets +0/+2/+4/+6/+0x26/
  +0x28; name NUL-clamped into the record; decode == the DiscoverServers accept predicate.
- **RunNetworkLobby (spine of @0x503f78 / @0x50442c)** — FIXED + documented partial.
  * FIXED — cmd6 framing. Binary @0x50422e..0x50423a: `v45[0]=byte_63CC1D;
    QueueRequestFlagBlob32(6, v45)` → packet bytes [0]=0x20 (type), [16]=6 (flag), blob
    copied at [17] so the count dword lands at byte offset 17. The reimpl previously stamped
    `bytes[kFOpcode]=6` (byte[0]=6) and `bytes[kFPayload]=count` (byte[16]=count) — wrong
    type byte AND wrong flag/count positions vs QueueRequestFlagBlob32 @0x494ab4 (verified:
    v3[0]=32, v3[16]=flag, v4=v3+17=blob). Now stamps bytes[kFOpcode]=kSyncType(0x20),
    bytes[kFSync]=6, put32(kFSync+1, playerCount). Evidence: 0x494ab4 disasm + 0x50423a.
    Tests stay green (no test inspected the byte image; readyCount/ack still verified).
  * PARTIAL — the spine reproduces wait-ready (`do pump while !byte_63CC28`), host save-ship
    (`byte_63CC28&1` → SendSaveGameToClients, return-1-on-fail), cmd6 ready-count, and the
    all-ready barrier (`while !(byte_63CC28&4)`). The interleaved LoadReceivedSaveStream
    (return 2), the trade/mission/scene-sync world leaves, and the exact ordering of cmd6
    after world setup are DEFERRED engine leaves (documented in lobby.h).
- **AdvertiseAndDiscover** — test/integration round-trip over the REAL BroadcastAdvertiser +
  DiscoverServers siblings. OK.

---

## Summary
- VERIFIED-1:1: 27 functions (transport 6, discovery 8, savegame 5, recon3 5 [modeled],
  net_session 1, session AllPlayersReady + 2 wait loops).
- FIXED: 1 — lobby.cpp cmd6 ready packet framing (now matches QueueRequestFlagBlob32 layout;
  addr 0x494ab4 / 0x50423a evidence).
- Documented divergences (no churn, justified): SendBroadcast closed-return (boundary),
  SendSaveGameToClients return-count vs 0/1 (truthiness preserved), FormatStatistics
  total==0 guard (non-portable NaN/inf text), recon3 overlapping-table abstraction,
  RunWaitLoop arg/global flag-source (out-of-cluster callers, unmodeled dword_631598).
- BOUNDARY/DEFERRED (rule 8 / rules 3-5): all WinSock syscalls (shim), VFS/save-parse tail
  of LoadReceivedSaveStream, GUI message-box/progress-form leaves, error-log sink.
- No wrong goldens found; cmd6 fix did not require a golden change.
- Tests: 9/9 net suites PASS (unit + integration). build/ untouched; no git run.
