#include "sim/command_apply6.h"

#include <cstring>
#include <string>

#include "sim/command_apply.h"   // g_lastObjectId / g_lastSceneId / g_lastTradeId
#include "sim/gametime.h"        // GameTimeCompare

// command_apply6.cpp — the FINAL batch of Command_Ex apply handlers + the group
// framing opcodes, closing the 96-entry dispatch table. Each handler is a
// byte-exact decode of its packet payload that delegates the deterministic core
// to the reused modules (trade_sell for 0x11/0x12, the relation grids inline for
// 0x1B, handler_entry/He for 0x1C, the gametick cascade for 0x1E). Provenance
// addresses on every function; the deep render/HUD/scene/AI leaves are routed
// through mockable hooks (DEFERRED — see the module report).

namespace guild::sim {

// ===========================================================================
// Module-global engine state.
// ===========================================================================
i32 g_curSellCmd = 0;     // dword_649890
i32 g_msgBoxCmd  = 0;     // dword_632244
u8  g_localPlayer = 0;    // byte_6477A1
GameTime g_tickClock{};   // unk_13CE852
i32 g_tickSubCounter = 0; // dword_62EB98

namespace {
bool g_standalone = true; // dword_764CE0 == -1

// --- ack helpers (mirror the originals' stamping) ---------------------------
inline void AckStatus(AckEntry* a, u8 s) { if (a) a->status = s; }

// Remap the dword id at packet offset `off` through the -2/-3/-4 last-created
// tokens (dword_631288/63128C/631290 == g_lastObjectId/Scene/Trade) and write
// the resolved id back into the packet (the originals do this in place).
i32 RemapIdAt(CommandPacket& pkt, u32 off) {
    i32 id = static_cast<i32>(pkt.get32(off));
    switch (id) {
        case -2: id = g_lastObjectId; break;
        case -3: id = g_lastSceneId;  break;
        case -4: id = g_lastTradeId;  break;
        default: break;
    }
    pkt.put32(off, static_cast<u32>(id));
    return id;
}

Apply6Log g_log;
} // namespace

void Apply6_SetStandalone(bool v) { g_standalone = v; }
bool Apply6_Standalone()          { return g_standalone; }
const Apply6Log& Apply6_GetLog()  { return g_log; }

// ===========================================================================
// 0x11 / 0x12 sell/produce resolve hooks + default backends.
// ===========================================================================
namespace {
// Default sell-resolve: a tiny modeled container table keyed by owner id. The
// host installs a real backend; this lets the apply path round-trip in tests.
bool DefaultSellResolve(const SellDecoded& d, SellResolve& r) {
    r.proto = d.proto;
    r.qty   = d.qty;
    // No modeled stock by default => reject (the test seeds a hook instead).
    r.srcResolved = (d.srcOwnerId != -1) || (d.destOwnerId != -1);
    r.destResolved = true;
    r.srcHasStock = false;
    return r.srcResolved;
}
SellResolveFn g_sellResolve = &DefaultSellResolve;

bool DefaultSellableResolve(const SellableDecoded& d, SellableResolve& r) {
    r.outProto = d.outProto;
    r.startQty = d.startQty;
    r.player   = d.player;
    r.sourceResolved = (d.sourceId != -1);
    r.ownerResolved  = true;
    return r.sourceResolved;
}
SellableResolveFn g_sellableResolve = &DefaultSellableResolve;
} // namespace

void SetSellResolveHook(SellResolveFn fn)         { g_sellResolve = fn ? fn : &DefaultSellResolve; }
void SetSellableResolveHook(SellableResolveFn fn) { g_sellableResolve = fn ? fn : &DefaultSellableResolve; }

// ===========================================================================
// gilde.exe 0x496B90 — VIBE_Command_ExSellObjekt (opcode 0x11).
//
//   if (dword_764CE0 != -1) dword_649890 = *(a1+26);
//   srcId = remap(a1+20); if (srcId != -1) { resolve src container; gate source
//           stock (EffectiveStock for reserve goods, raw count always). }
//   dstId = remap(a1+16); resolve dst container; gate dst capacity
//           (free-capacity for storage, carry-capacity for carried — clamps qty).
//   move qty: src -= qty; dst += qty (AddObjekt/AddObjektToParent — hooked).
//   dword_631290 = newDstRecord.id;  (the last-trade-id register, LABEL_58)
//   ack[0]=1; if (a1+16 != -1) { ack[1]=3; ack[6]=dstRecord; }  return 0.
// The deep transfer leaves are routed through trade_sell's TradeCmd hook; the
// deterministic gate is TradeSellObjektResolve.
// ===========================================================================
int ExSellObjekt(CommandPacket& pkt, AckEntry* ack) {
    if (!g_standalone)
        g_curSellCmd = static_cast<i32>(pkt.get32(26));  // dword_649890

    SellDecoded d{};
    d.srcOwnerId  = static_cast<i32>(pkt.get32(20));
    if (d.srcOwnerId != -1)
        d.srcOwnerId = RemapIdAt(pkt, 20);
    d.destOwnerId = RemapIdAt(pkt, 16);
    d.proto       = static_cast<i16>(pkt.get32(22) >> 16);
    d.qty         = static_cast<i32>(pkt.get32(31));
    d.rawMaterial = static_cast<i32>(pkt.get32(35));
    d.goodByte    = pkt.bytes[30];
    d.cmdCount    = static_cast<i32>(pkt.get32(8));

    SellResolve r{};
    if (!g_sellResolve(d, r))
        return 1;

    i32 moved = TradeSellObjektResolve(r, /*commit=*/true);
    if (moved == 0)
        return 1;

    g_log.sellCommitCount++;
    g_log.lastSellMoved = moved;
    g_lastTradeId = moved;  // dword_631290 = *(newDst+1) modeled as the moved id

    if (ack) {
        ack->status = 1;
        if (static_cast<i32>(pkt.get32(16)) != -1) {
            ack->slot = 3;
            ack->seq  = moved;  // ack+6 = the dest record token (modeled)
        }
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x497538 — VIBE_Command_ExComputeSellableAmount (opcode 0x12).
//
//   if (dword_764CE0 != -1) dword_649890 = *(a1+26);
//   srcId = remap(a1[4] == a1+16); resolve source (type 42/278) + owner;
//   v4 = *(a1+22); recipe = 65*(HIWORD(a1+18)) + dword_13CE27C; gate over the
//   up-to-4 ingredient slots: v4 = min over slots of effStock/ratio (miss => 0);
//   cap v4 by ComputeFreeCapacity/outCount; if v15>0 consume + produce + credit.
//   ack[0]=1; if (a1+16 != -1) { ack[1]=3; ack[6]=outRecord; } else on reject
//   ack[1]=6 (no AddObjekt capacity) / 8 (nothing producible).  return 0/1.
// The deterministic core is TradeComputeSellableAmount.
// ===========================================================================
int ExComputeSellableAmount(CommandPacket& pkt, AckEntry* ack) {
    if (!g_standalone)
        g_curSellCmd = static_cast<i32>(pkt.get32(26));  // dword_649890

    SellableDecoded d{};
    d.sourceId   = RemapIdAt(pkt, 16);
    d.recipeType = static_cast<i16>(pkt.get32(18) >> 16);
    d.outProto   = d.recipeType;
    d.startQty   = static_cast<i32>(pkt.get32(22));
    d.player     = g_localPlayer;
    d.cmdCount   = static_cast<i32>(pkt.get32(8));

    SellableResolve r{};
    if (!g_sellableResolve(d, r)) {
        if (ack) ack->slot = 8;  // a2[1] = 8 (resolve / nothing producible)
        return 1;
    }

    SellableResult res = TradeComputeSellableAmount(r, /*commit=*/true);
    if (!res.ok) {
        if (ack) ack->slot = 8;  // nothing producible (return 1 in the original)
        return 1;
    }

    g_log.sellableCommitCount++;
    g_log.lastSellableProduced = res.produced;
    g_log.lastSellableProceeds = res.proceeds;

    if (ack) {
        ack->status = 1;
        if (static_cast<i32>(pkt.get32(16)) != -1) {
            ack->slot = 3;
            ack->seq  = res.produced;  // ack+6 = the output record token (modeled)
        }
    }
    return 0;
}

// ===========================================================================
// 0x1B relation matrix.
// ===========================================================================
RelationState::RelationState() { Reset(); }
void RelationState::Reset() {
    matrixA.assign(kRelCells, 0);
    matrixB.assign(kRelCells, 0);
    personId.assign(kRelPersons, 0);
    aliveMarker.assign(kRelPersons, -1);  // all free by default
    slotId.assign(kRelPersons, 0);
}
namespace { RelationState g_relations; }
RelationState& Apply6_Relations() { return g_relations; }

namespace {
// Find the Person index for `id` by scanning the id column (dword_12CE914 stride
// 134 dwords). Returns index in [0,768) or -1 (the original returns 1/reject).
int FindPersonIndex(const RelationState& rel, i32 id) {
    for (int i = 0; i < kRelPersons; ++i)
        if (rel.personId[i] == id)
            return i;
    return -1;
}

// The relation clamp used by modes 0/1/2 (and the band path): >126 => 127,
// < -127 => -127, else the signed byte itself. Returns an i8.
i8 ClampRel(int v) {
    if (v > 126) return 127;
    if (v < -127) return -127;
    return static_cast<i8>(v);
}

// trunc-to-zero double->int (cvttsd2si / Coord_ConvertX fixup).
i32 TruncToInt(double v) { return static_cast<i32>(v); }
} // namespace

// ===========================================================================
// gilde.exe 0x49818C — VIBE_Command_ExComputeObjectCoords (opcode 0x1B).
//
// Decoded payload:
//   +16 : person B id (dst row)         (-2/-3/-4 remap, written back)
//   +20 : person A id (src col)         (remap, written back)
//   +24 : signed delta (dword)
//   +28 : mode (dword) 0/1/2/3/4
//   +32 : float scale (mode 3)
//   +36 : band index (mode 4) 0..4
//
// mode 4: global band-decay pass over the whole 768x768 primary grid; per-band
//   (threshold, lowBound, highDelta, lowDelta) nudges every off-diagonal live
//   cell toward 0 and clamps. modes 0/1/2/3: targeted pair / column mutation.
// Returns 0 on apply, 1 if a referenced person index is missing / dead.
// ===========================================================================
int ExComputeObjectCoords(CommandPacket& pkt, AckEntry* ack) {
    RelationState& rel = g_relations;

    i32 idB = RemapIdAt(pkt, 16);            // dst (row)
    i32 idA = RemapIdAt(pkt, 20);            // src (col)
    i32 delta = static_cast<i32>(pkt.get32(24));
    i32 mode  = static_cast<i32>(pkt.get32(28));

    // --- mode 4: global band-decay pass -----------------------------------
    if (mode == 4) {
        i32 band = static_cast<i32>(pkt.get32(36));
        // Band parameter table (recovered from the switch at 0x4982DE..0x498390):
        //   threshold(var_24), lowBound(var_1C), highDelta(var_18), lowDelta(var_20).
        // band>=4 (unsigned, LABEL_9) and band==4 -> {40,-115,-20,3}; band 0 (the
        // post-switch fallthrough) -> {100,-75,-4,11}; bands 1/2/3 each distinct.
        int threshold, lowBound, highDelta, lowDelta;
        if (static_cast<u32>(band) >= 4) {     // includes band == 4 (LABEL_9)
            threshold = 40;  lowBound = -115; highDelta = -20; lowDelta = 3;
        } else switch (band) {
            case 3:  threshold = 55;  lowBound = -105; highDelta = -16; lowDelta = 5;  break;
            case 2:  threshold = 70;  lowBound = -95;  highDelta = -12; lowDelta = 7;  break;
            case 1:  threshold = 85;  lowBound = -85;  highDelta = -8;  lowDelta = 9;  break;
            default: threshold = 100; lowBound = -75;  highDelta = -4;  lowDelta = 11; break; // band 0
        }
        for (int i = 0; i < kRelPersons; ++i) {
            if (rel.aliveMarker[i] == -1) continue;
            for (int j = 0; j < kRelPersons; ++j) {
                if (j == i) continue;
                if (rel.aliveMarker[j] == -1) continue;
                int v = rel.A(i, j);
                if (v <= threshold) {
                    if (v < lowBound) v += lowDelta;
                } else {
                    v += highDelta;
                }
                rel.A(i, j) = ClampRel(v);
            }
        }
        g_log.relationMutateCount++;
        AckStatus(ack, 1);
        return 0;
    }

    // --- targeted modes: resolve the column person (id A == +20) ----------
    int idx = FindPersonIndex(rel, idA);
    if (idx < 0 || rel.aliveMarker[idx] == -1)
        return 1;

    // --- mode 3: scale matrix-B column `idx` by the float, where the row
    //     person's id differs from this person's slot id --------------------
    if (mode == 3) {
        float scale;
        std::memcpy(&scale, &pkt.bytes[32], sizeof(scale));
        i32 mySlot = rel.slotId[idx];
        for (int k = 0; k < kRelPersons; ++k) {
            if (rel.personId[k] != mySlot) {
                double scaled = static_cast<double>(rel.B(k, idx)) * static_cast<double>(scale);
                rel.B(k, idx) = static_cast<i8>(TruncToInt(scaled));
            }
        }
        g_log.relationMutateCount++;
        AckStatus(ack, 1);
        return 0;
    }

    // --- modes 0/1/2: resolve the row person (id B == +16) ----------------
    int idx2 = FindPersonIndex(rel, idB);
    if (idx2 < 0 || rel.aliveMarker[idx2] == -1)
        return 1;

    switch (mode) {
        case 2: {
            rel.B(idx2, idx) = 0;
            int v = delta + rel.A(idx2, idx);
            rel.A(idx2, idx) = ClampRel(v);
            break;
        }
        case 1: {
            int v = delta + rel.A(idx2, idx);
            i8 a = ClampRel(v);
            rel.A(idx2, idx) = a;
            int half = (delta < 0) ? a : (a / 2);
            int w = half + rel.B(idx2, idx);
            rel.B(idx2, idx) = ClampRel(w);
            break;
        }
        case 0:
        default: {
            int v = delta + rel.A(idx2, idx);
            rel.A(idx2, idx) = ClampRel(v);
            break;
        }
    }
    g_log.relationMutateCount++;
    AckStatus(ack, 1);
    return 0;
}

// ===========================================================================
// 0x1C He message box.
// ===========================================================================
namespace {
HeAllocResult DefaultHeAlloc(const u8* desc, u8 curFlags) {
    (void)desc; (void)curFlags;
    // No He pool installed => alloc fails (the test seeds a hook).
    return HeAllocResult{};
}
HeAllocFn g_heAlloc = &DefaultHeAlloc;

int DefaultMsgBoxPersonKind(i32 personId) { (void)personId; return -1; }
MsgBoxPersonKindFn g_msgBoxKind = &DefaultMsgBoxPersonKind;

std::string g_msgFirst;
std::string g_msgSecond;
} // namespace

void SetHeAllocHook(HeAllocFn fn)                 { g_heAlloc = fn ? fn : &DefaultHeAlloc; }
void SetMsgBoxPersonKindHook(MsgBoxPersonKindFn fn){ g_msgBoxKind = fn ? fn : &DefaultMsgBoxPersonKind; }
void Apply6_SeedMessageBoxStrings(const char* first, const char* second) {
    g_msgFirst  = first  ? first  : "";
    g_msgSecond = second ? second : "";
}

// ===========================================================================
// gilde.exe 0x498678 — VIBE_Command_ExShowMessageBox (opcode 0x1C).
//
//   qmemcpy(desc, &unk_1077B60, 0xF8);   // staged He template
//   if (dword_764CE0 != -1) {
//       if (desc[13] & 0x10000) { p = FindRecordById(desc[2]);
//           if (!p) return 1; k = p[2]; if (k==7 || (k!=6 && !ack)) return 1; }
//       if (!ack) { v4 = BYTE2(desc[13]); if (desc[13] & 0x20000) {
//           BYTE2(desc[13]) = (BYTE2(desc[13]) & 0xF9) | 4; v4 = BYTE2 & 0xF9; } }
//       dword_632244 = *(a1+16);
//   }
//   rec = He_AllocHandlerEntry(desc, v4); if (!rec) return 1;
//   if (*rec == 17) { copy chat string(s) into a fresh buffer; rec[31]=buf;
//                     rec[32]=len+1; }
//   if (ack) { ack[0]=1; ack[1]=4; ack[6]=rec; }  return 0.
// ===========================================================================
int ExShowMessageBox(CommandPacket& pkt, AckEntry* ack) {
    // The staged He template image (the original qmemcpy's 0xF8 bytes from
    // unk_1077B60). We model it as the packet payload region; the alloc hook
    // reads whatever fields the host wires. Recover desc[13] / desc[2] from it.
    const u8* desc = &pkt.bytes[kFPayload];
    auto descDword = [&](int dwordIdx) -> i32 {
        return static_cast<i32>(pkt.get32(kFPayload + 4 * dwordIdx));
    };

    u8 curFlags = 0;
    if (!g_standalone) {
        i32 d13 = descDword(13);
        if (d13 & 0x10000) {
            int kind = g_msgBoxKind(descDword(2));
            if (kind < 0)
                return 1;                          // person not found
            // k==7 reject; k!=6 && !ack reject.
            if (kind == 7 || (kind != 6 && ack == nullptr))
                return 1;
        }
        if (ack == nullptr) {
            curFlags = static_cast<u8>(d13 >> 16);          // BYTE2(desc[13])
            if (d13 & 0x20000)
                curFlags = static_cast<u8>(curFlags & 0xF9);
        }
        g_msgBoxCmd = static_cast<i32>(pkt.get32(16));      // dword_632244
    }

    HeAllocResult rec = g_heAlloc(desc, curFlags);
    if (rec.record == 0)
        return 1;
    g_log.heAllocCount++;

    if (rec.kind == 17) {
        // The chat buffer: byte_1077C58 (first) and, if flag 0x10, its trailing
        // string (second), 2-byte-stride copied into a fresh buffer; rec[32] =
        // total length + 1. We model the buffer image + length.
        g_log.msgBoxBuffer.clear();
        const std::string& s1 = g_msgFirst;
        for (char c : s1) g_log.msgBoxBuffer.push_back(c);
        g_log.msgBoxBuffer.push_back('\0');
        std::size_t total;
        if (rec.flag10) {
            const std::string& s2 = g_msgSecond;
            for (char c : s2) g_log.msgBoxBuffer.push_back(c);
            total = s1.size() + 1 + s2.size();   // strlen+1+strlen
        } else {
            total = s1.size();                   // strlen
        }
        g_log.msgBoxChatLen = static_cast<int>(total + 1);  // rec[32] = len + 1
    }

    if (ack) {
        ack->status = 1;
        ack->slot   = 4;
        ack->seq    = rec.record;
    }
    return 0;
}

// ===========================================================================
// 0x1E advance game tick.
// ===========================================================================
namespace {
void DefaultTickCascade(TickPass) {}
TickCascadeFn g_tickCascade = &DefaultTickCascade;
int g_livePersons = 0;
TickGates g_tickGates{};
} // namespace

TickGates& Apply6_TickGates() { return g_tickGates; }
void SetTickCascadeHook(TickCascadeFn fn) { g_tickCascade = fn ? fn : &DefaultTickCascade; }
void Apply6_SeedLivePersonCount(int n)    { g_livePersons = n; }

// ===========================================================================
// gilde.exe 0x498954 — VIBE_Command_ExAdvanceGameTick (opcode 0x1E).
//
//   if (GameTime_Compare(&clock, a1+16) < 0) {     // packet time is newer
//       clock = *(GameTime*)(a1+16);               // 14-byte commit
//       dword_62EB98 = 0;
//       if (dword_63C8E8) He_RunAllHandlers();
//       if (dword_63C8E4) { AdvanceCalendarClock(); ComputeGoodsDemand();
//                           if (!((clock.dayhi)%6)) AccumulateThreatStats(); }
//       if (dword_63C8E0) Character_UpdateAllNeeds();
//       Light_SetGrayColorThunk(...);                       // always
//       if (dword_63C8E0) { for each live person: Ai_EvaluateMeister();
//                           GameLogic_UpdatePlayerTurns(); }
//       Inventory_TickProductionTimers();                   // always
//       Hud_MarkOwnedObjects();                             // always
//   }
//   if (ack) ack[0] = 1;  return 0.
// The subsystem leaves are routed through the cascade hook + gates; the clock
// commit is exact.
// ===========================================================================
int ExAdvanceGameTick(CommandPacket& pkt, AckEntry* ack) {
    GameTime pktTime{};
    std::memcpy(&pktTime, &pkt.bytes[kFPayload], sizeof(GameTime));  // a1+16, 14 bytes

    if (GameTimeCompare(&g_tickClock, &pktTime) < 0) {
        g_tickClock = pktTime;
        g_tickSubCounter = 0;        // dword_62EB98 = 0
        g_log.tickAdvanceCount++;

        auto run = [&](TickPass p) { g_tickCascade(p); g_log.tickPasses.push_back(p); g_log.tickPassCount++; };

        if (g_tickGates.heHandlers)
            run(TickPass::kHeHandlers);
        if (g_tickGates.calendar) {
            run(TickPass::kCalendarClock);
            run(TickPass::kGoodsDemand);
            // (qword_13CE854 >> 32) % 6 == 0 : the day counter mod 6. The clock's
            // day field is g_tickClock.day; the original reads the high dword of
            // the qword image (the day-of advance counter). Model with .day.
            if ((static_cast<u32>(g_tickClock.day) % 6) == 0)
                run(TickPass::kThreatStats);
        }
        if (g_tickGates.needsAi)
            run(TickPass::kCharNeeds);
        run(TickPass::kLightGray);     // always
        if (g_tickGates.needsAi) {
            for (int i = 0; i < g_livePersons; ++i)
                run(TickPass::kMeisterAi);
            run(TickPass::kPlayerTurns);
        }
        run(TickPass::kProductionTimers);  // always
        run(TickPass::kMarkOwned);         // always
    }

    AckStatus(ack, 1);
    return 0;
}

// ===========================================================================
// 0x39 move object to room.
// ===========================================================================
namespace {
bool DefaultMoveRoomResolve(i32 roomId, i32 personId, MoveRoomView& v) {
    (void)roomId; (void)personId; (void)v;
    return false;  // no room subsystem installed (test seeds a hook)
}
MoveRoomResolveFn g_moveRoom = &DefaultMoveRoomResolve;
} // namespace
void SetMoveRoomResolveHook(MoveRoomResolveFn fn) { g_moveRoom = fn ? fn : &DefaultMoveRoomResolve; }

// gilde.exe 0x49AF00 — VIBE_Command_ExMoveObjectToRoom (opcode 0x39).
int ExMoveObjectToRoom6(CommandPacket& pkt, AckEntry* ack) {
    i32 roomId   = static_cast<i32>(pkt.get32(16));
    i32 personId = static_cast<i32>(pkt.get32(20));

    MoveRoomView v{};
    if (!g_moveRoom(roomId, personId, v))
        return 1;

    if (!v.roomResolved)                         // Person_QueryBegin == 0
        return 1;
    if (!v.personResolved && !v.personIsNull)     // FindRecordById null && id!=-1
        return 1;

    // Capacity room: bump the occupant count, rejecting if already full.
    if (v.roomIsCapacity) {
        int occ = v.roomOccupants ? *v.roomOccupants : 0;
        if (occ >= v.roomCapacity)
            return 1;                            // full
        if (v.roomOccupants)
            *v.roomOccupants = occ + 1;
    }

    // Detach from the old room (decrement its count if it is a capacity room).
    if (v.oldRoomIsCapacity && v.oldRoomOccupants && *v.oldRoomOccupants > 0)
        *v.oldRoomOccupants -= 1;

    // Re-parent the person into the new room.
    if (v.personRoomLink)
        *v.personRoomLink = v.newRoomToken;

    if (ack) {
        ack->status = 1;
        ack->slot   = 0;
        ack->seq    = 0;
    }
    return 0;
}

// ===========================================================================
// Group framing (opcodes 5/6/7).
// ===========================================================================

// gilde.exe 0x4942C0 — VIBE_Command_ExecCommandGroup. Walk the group members
// from the begin packet to the first group-end (opcode 6) inclusive; dispatch
// each through `apply`. If every member succeeds (returns 0) mark every member's
// status byte 1; else mark them all 2 (retry). Returns 1 (the original always
// returns 1).
int ExecCommandGroup(std::vector<CommandPacket*>& members,
                     std::vector<AckEntry*>& acks, GroupApplyFn apply) {
    bool allOk = true;
    // The original dispatches from the first member after begin up to (and
    // including) the group-end packet; the begin packet itself is not dispatched
    // (it is consumed when allOk). We treat `members` as exactly that run.
    for (std::size_t i = 0; i < members.size(); ++i) {
        CommandPacket* m = members[i];
        if (m->opcode() == kOp6GroupEnd)
            break;                     // *v2 != 6 loop guard (stop at end marker)
        AckEntry* a = (i < acks.size()) ? acks[i] : nullptr;
        if (apply(*m, a) != 0)         // handler returned nonzero => v3 = 0
            allOk = false;
    }
    // Stamp the status byte of every member (begin..end inclusive).
    u8 status = allOk ? 1 : 2;
    for (CommandPacket* m : members)
        m->bytes[0] = status;          // *(_BYTE*)i = 1 or 2
    return 1;
}

// Opcode 5 — group-begin marker. Positionally consumed by ExecCommands; as a
// direct-apply handler it just stamps the ACK so the opcode is no longer unset.
int ExGroupBegin(CommandPacket& pkt, AckEntry* ack) {
    (void)pkt;
    AckStatus(ack, 1);
    return 0;
}
// Opcode 6 — group-end marker.
int ExGroupEnd(CommandPacket& pkt, AckEntry* ack) {
    (void)pkt;
    AckStatus(ack, 1);
    return 0;
}
// Opcode 7 — skip marker (no dispatch in the original; pure ACK here).
int ExGroupSkip(CommandPacket& pkt, AckEntry* ack) {
    (void)pkt;
    AckStatus(ack, 1);
    return 0;
}

// ===========================================================================
// Reset.
// ===========================================================================
void ResetApply6State() {
    g_standalone = true;
    g_curSellCmd = 0;
    g_msgBoxCmd = 0;
    g_localPlayer = 0;
    g_tickClock = GameTime{};
    g_tickSubCounter = 0;
    g_sellResolve = &DefaultSellResolve;
    g_sellableResolve = &DefaultSellableResolve;
    g_heAlloc = &DefaultHeAlloc;
    g_msgBoxKind = &DefaultMsgBoxPersonKind;
    g_tickCascade = &DefaultTickCascade;
    g_livePersons = 0;
    g_tickGates = TickGates{};
    g_moveRoom = &DefaultMoveRoomResolve;
    g_relations.Reset();
    g_msgFirst.clear();
    g_msgSecond.clear();
    g_log = Apply6Log{};
}

// ===========================================================================
// Registry + direct apply.
// ===========================================================================
namespace {
#define APPLY6_ADAPTER(NAME) \
    void NAME##_adapter(CommandQueue&, CommandPacket& pkt, AckEntry* ack) { NAME(pkt, ack); }
APPLY6_ADAPTER(ExSellObjekt)
APPLY6_ADAPTER(ExComputeSellableAmount)
APPLY6_ADAPTER(ExComputeObjectCoords)
APPLY6_ADAPTER(ExShowMessageBox)
APPLY6_ADAPTER(ExAdvanceGameTick)
APPLY6_ADAPTER(ExMoveObjectToRoom6)
APPLY6_ADAPTER(ExGroupBegin)
APPLY6_ADAPTER(ExGroupEnd)
APPLY6_ADAPTER(ExGroupSkip)
#undef APPLY6_ADAPTER
} // namespace

void RegisterApplyHandlers6(CommandQueue& q) {
    q.set_handler(kOp6GroupBegin,            &ExGroupBegin_adapter);
    q.set_handler(kOp6GroupEnd,              &ExGroupEnd_adapter);
    q.set_handler(kOp6GroupSkip,             &ExGroupSkip_adapter);
    q.set_handler(kOp6SellObjekt,            &ExSellObjekt_adapter);
    q.set_handler(kOp6ComputeSellableAmount, &ExComputeSellableAmount_adapter);
    q.set_handler(kOp6ComputeObjectCoords,   &ExComputeObjectCoords_adapter);
    q.set_handler(kOp6ShowMessageBox,        &ExShowMessageBox_adapter);
    q.set_handler(kOp6AdvanceGameTick,       &ExAdvanceGameTick_adapter);
    q.set_handler(kOp6MoveObjectToRoom,      &ExMoveObjectToRoom6_adapter);
}

int ApplyPacket6(CommandPacket& pkt, AckEntry* ack) {
    switch (pkt.opcode()) {
        case kOp6GroupBegin:            return ExGroupBegin(pkt, ack);
        case kOp6GroupEnd:              return ExGroupEnd(pkt, ack);
        case kOp6GroupSkip:             return ExGroupSkip(pkt, ack);
        case kOp6SellObjekt:            return ExSellObjekt(pkt, ack);
        case kOp6ComputeSellableAmount: return ExComputeSellableAmount(pkt, ack);
        case kOp6ComputeObjectCoords:   return ExComputeObjectCoords(pkt, ack);
        case kOp6ShowMessageBox:        return ExShowMessageBox(pkt, ack);
        case kOp6AdvanceGameTick:       return ExAdvanceGameTick(pkt, ack);
        case kOp6MoveObjectToRoom:      return ExMoveObjectToRoom6(pkt, ack);
        default:                        return -1;
    }
}

} // namespace guild::sim
