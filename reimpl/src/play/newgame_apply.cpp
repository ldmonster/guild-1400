// guild::play — the new-game commit (VIBE_Command_EnqueueInheritanceTransfer
// @0x5336f0 + the InitOrLoadSession new-single-player block). See header.
#include "play/newgame_apply.h"

#include "app/session_init.h"      // MoneyMultiplyByRate / NewGameStartGoldBase (REUSED)
#include "sim/building2.h"         // Building_LookupTypeRecordA @0x589778 (REUSED)
#include "sim/command.h"           // CommandQueue / CommandPacket
#include "sim/command_apply.h"     // g_lastObjectId (dword_631288)
#include "sim/command_apply2.h"    // RegisterApplyHandlers2 (opcode 0x0F — cmd15 apply)
#include "sim/command_apply5.h"    // RegisterApplyHandlers5 (opcodes 0x0B/0x0C/0x20)
#include "sim/command_apply7.h"    // EnqueueTradeRequest @0x494548 / QueueRequestFlagBlob32
#include "sim/command_codec.h"     // EnqueueObjectInteraction @0x4944f0 / QueueRequestCoord27
#include "sim/command_inherit.h"   // EnqueueCmd15 @0x494604
#include "sim/entity.h"            // g_persons / PersonFindRecordById
#include "sim/name_tables.h"       // NameAt — dword_8C400C/8C4320 first-name slices
#include "sim/types.h"             // Person / kPersonStride / GameTime
#include "util/math_random.h"      // RandomModulo @0x58b89c (the shared LCG)
#include "world/mission.h"         // MissionSlotRegister @0x53872c / g_missionSlotMode

#include <cstring>

namespace guild::play {
namespace {

NewGameApplyHooks g_defaultHooks;
NewGameApplyHooks* g_hooks = &g_defaultHooks;

// ---- raw 536-byte record access (the originals address fields by byte offset
// through per-field global symbols; we do the same through memcpy so the packed
// record stays alignment-safe). -----------------------------------------------
u8* Rec(sim::Person* p) { return reinterpret_cast<u8*>(p); }
void Put32(u8* rec, int off, i32 v) { std::memcpy(rec + off, &v, 4); }
void Put16(u8* rec, int off, u16 v) { std::memcpy(rec + off, &v, 2); }
i32  Get32(const u8* rec, int off) { i32 v; std::memcpy(&v, rec + off, 4); return v; }
u16  Get16(const u8* rec, int off) { u16 v; std::memcpy(&v, rec + off, 2); return v; }

// VIBE_Util_StrNCopyPad @0x5d9360 — copy up to n chars of src, zero-pad to n.
void StrNCopyPad(u8* dst, const char* src, int n) {
    int i = 0;
    if (src)
        for (; i < n && src[i]; ++i) dst[i] = static_cast<u8>(src[i]);
    for (; i < n; ++i) dst[i] = 0;
}

// The 2-byte-unrolled strcpy the commit uses for the first-name / model-name
// stores (0x53384e / 0x5338a3 / 0x533907) — a plain NUL-terminated copy. The
// original is unbounded; we cap at the record field width (the shipped names
// all fit), which is the only safe equivalent of "fits in the field".
void StrCopyCapped(u8* dst, const char* src, int cap) {
    if (!src) { dst[0] = 0; return; }
    int i = 0;
    for (; i < cap - 1 && src[i]; ++i) dst[i] = static_cast<u8>(src[i]);
    dst[i] = 0;
}

// The standalone lockstep barrier: MarkSyncRangeStart/End + the
// `while (!CheckSyncRangeAcked()) Amt_RefreshGuildState();` wait (0x533755 /
// 0x5337e4). In standalone mode the queue applies its own packets locally, so
// flushing + executing IS the acked state the original spins for.
void Barrier(sim::CommandQueue& q) {
    q.FlushSendQueue();
    q.ExecCommands();
}

// 0x533824-style family-field stamp: parent word+0x50 / dword+0x54 from the
// player, dword+0x68 = player id.
void CopyFamilyFields(u8* parent, const u8* player) {
    Put16(parent, 0x50, Get16(player, 0x50));   // family/household word
    Put32(parent, 0x54, Get32(player, 0x54));   // wappen id
    Put32(parent, 0x68, Get32(player, 0x04));   // family-head = player id
}

} // namespace

NewGameApplyHooks* NewGameApply_SetHooks(NewGameApplyHooks* hooks) {
    NewGameApplyHooks* prev = (g_hooks == &g_defaultHooks) ? nullptr : g_hooks;
    g_hooks = hooks ? hooks : &g_defaultHooks;
    return prev;
}

// gilde.exe 0x533834..0x5338bb — the parent first-name read. `female` picks the
// mother (dword_8C4320, RandomModulo(0x70)) vs the father (dword_8C400C,
// RandomModulo(0xBF)) slice of the loaded name tables. Falls back to "" when no
// textbin asset has been loaded (the inert behavior the headless tests rely on).
const char* NewGameApplyHooks::ParentFirstName(bool female, int index) {
    if (!sim::NameTables_Loaded())
        return "";
    return sim::NameAt(female ? sim::kNameFemale : sim::kNameMale, index);
}

// gilde.exe 0x52d9f9..0x52da0e — talents = record BYTES [1..5] of TypeRecordA
// (NOT [0..4]). LookupTypeRecordA @0x589778 writes a 6-byte record at the byref:
// dword0 (LE) at +0..3, word4 at +4..5. The copy loop runs eax=1..5 reading
// [esp+eax] and stores byte_122F4F0[0..4] = record[1..5] — i.e. it SKIPS byte 0.
// Evidence (disasm 0x52da00): `inc eax; mov dl,[esp+eax]; mov (122F4EC+3)[eax],dl;
// cmp eax,5; jl` -> eax 1..5 in, F0..F4 out.
void NewGameProfessionTalents(int professionVariant, u8 out[5]) {
    sim::TypeRecord rec{};
    sim::Building_LookupTypeRecordA(static_cast<std::uint8_t>(professionVariant), &rec);
    // record bytes laid out: b0=dword0&0xFF, b1=dword0>>8, b2=dword0>>16,
    // b3=dword0>>24, b4=word4&0xFF, b5=word4>>8. Take b1..b5.
    out[0] = static_cast<u8>(rec.dword0 >> 8);
    out[1] = static_cast<u8>(rec.dword0 >> 16);
    out[2] = static_cast<u8>(rec.dword0 >> 24);
    out[3] = static_cast<u8>(rec.word4);
    out[4] = static_cast<u8>(rec.word4 >> 8);
}

NewGameApplyResult ApplyNewGameParams(const gui::NewGameParams& p,
                                      int difficultyVariant,
                                      sim::CommandQueue& q,
                                      const NewGameApplyInputs& in) {
    NewGameApplyResult res;

    // The person-create handlers (opcodes 0x0B/0x0C, batch 5) and the stock-
    // transfer handler the purse cmd15 dispatches to (opcode 0x0F, batch 2)
    // must be live on this queue; registration is idempotent and the batches
    // are disjoint.
    sim::RegisterApplyHandlers2(q);
    sim::RegisterApplyHandlers5(q);

    // 0x5336fb — `cmp byte ptr dword_122F4A0+2, 0; jz return`: the commit is
    // armed by the RunChoosePlayer wappen commit (0x52d4af writes 0x1200 at
    // +1, i.e. BYTE2 = 0x12). In the collected block that arm is p.started
    // (gui::NewGame_Commit). (0x5336f6 VIBE_Object_ResetState clears the
    // caller's 4608-byte scratch — no record effect, see header.)
    if (!p.started) return res;
    res.applied = true;

    // ---- 1. the player (opcode 12) -----------------------------------------
    // 0x533713..0x533749 — EnqueueTradeRequest(-1, -1,
    //   SHIBYTE(dword_122F4A0)=professionVariant, 16, dword_122F4A4=wappen id,
    //   byte_122F4A8=gender, byte_122F4A9=faith, String, byte_122F4CA).
    const i32 wappenId = gui::Wappen_IdForIndex(p.wappen);   // 1342 + index
    sim::EnqueueTradeRequest(q, -1, -1,
                             static_cast<i8>(p.professionVariant),
                             /*a4=*/16, wappenId,
                             static_cast<i8>(p.gender), static_cast<i8>(p.faith),
                             p.firstName.c_str(), p.familyName.c_str());
    Barrier(q);                                   // 0x533750..0x533763
    res.playerId = sim::g_lastObjectId;           // the ack's record (0x533767)
    sim::Person* player = sim::PersonFindRecordById(res.playerId);
    if (!player) { res.createFailed = true; return res; }
    u8* pl = Rec(player);

    // ---- 2. the parents (two opcode-11 creates) ----------------------------
    // 0x533773..0x5337a2 — EnqueueObjectInteraction(9, -1, RandomModulo(4)+32,
    //   -1, -1, signed byte @0x122F4A1, 0, 1)   (gender flag 1 -> mother)
    const i16 profMother = static_cast<i16>(util::RandomModulo(4) + 32);
    sim::EnqueueObjectInteraction(q, 9, -1, profMother, -1, -1,
                                  in.parentProfCtxA, 0, 1);
    Barrier(q);
    res.motherId = sim::g_lastObjectId;           // GetPacketSeqById(v7) record
    // 0x5337a7..0x5337d8 — the same with signed byte @0x122F4A0, gender flag 0.
    const i16 profFather = static_cast<i16>(util::RandomModulo(4) + 32);
    sim::EnqueueObjectInteraction(q, 9, -1, profFather, -1, -1,
                                  in.parentProfCtxB, 0, 0);
    Barrier(q);                                   // 0x5337df..0x5337f2
    res.fatherId = sim::g_lastObjectId;           // GetPacketSeqById(edx) record
    sim::Person* mother = sim::PersonFindRecordById(res.motherId);
    sim::Person* father = sim::PersonFindRecordById(res.fatherId);
    if (!mother || !father) { res.createFailed = true; return res; }
    u8* mo = Rec(mother);
    u8* fa = Rec(father);

    // ---- 3. the family-field stamps (exact write order of 0x533810..0x5338f8)
    // 0x533817 — StrNCopyPad(father+0x40, byte_122F4CA, 16): the family name.
    StrNCopyPad(fa + 0x40, p.familyName.c_str(), 16);
    // 0x533824/0x53382b/0x533831 — mother family word / wappen / player link.
    CopyFamilyFields(mo, pl);
    // 0x533834..0x533866 — mother first name = female table[RandomModulo(0x70)].
    StrCopyCapped(mo + 0x30,
                  g_hooks->ParentFirstName(true, util::RandomModulo(0x70) & 0xFFFF),
                  16);
    // 0x533869/0x533870 — ResolveStaffModel(mother marker word).
    g_hooks->ResolveStaffModel(Get16(mo, 0));
    // 0x533875..0x533886 — father family word / wappen / player link.
    CopyFamilyFields(fa, pl);
    // 0x533889..0x5338bb — father first name = male table[RandomModulo(0xBF)].
    StrCopyCapped(fa + 0x30,
                  g_hooks->ParentFirstName(false, util::RandomModulo(0xBF) & 0xFFFF),
                  16);
    g_hooks->ResolveStaffModel(Get16(fa, 0));     // 0x5338c1
    // 0x5338c6..0x5338d7 — the spouse links: mother+0x5C = father id, then
    // father+0x5C = mother id.
    Put32(mo, 0x5C, Get32(fa, 0x04));
    Put32(fa, 0x5C, Get32(mo, 0x04));
    // 0x5338da..0x5338e7 — player +0x60 = father id, +0x64 = mother id.
    Put32(pl, 0x60, Get32(fa, 0x04));
    Put32(pl, 0x64, Get32(mo, 0x04));
    // 0x5338ea..0x5338f8 — both parents +0x68 = player id (re-stamped).
    Put32(mo, 0x68, Get32(pl, 0x04));
    Put32(fa, 0x68, Get32(pl, 0x04));
    // 0x5338fb..0x53391f — strcpy(player+0x1F0, unk_122F4F5): the avatar model
    // name (set only by the dynasty preview scene; "" on the automatic path).
    StrCopyCapped(pl + 0x1F0, in.avatarModelName.c_str(), 40);
    // 0x533920/0x53392a — player dword +0x18C = dword_122F528 (the portrait id).
    Put32(pl, 0x18C, in.portraitId);

    // ---- 4. relationships + parent purses ----------------------------------
    // 0x533930..0x5339ae — six QueueRequestCoord27(a, b, 127) pairs in this
    // exact order (the opcode-27 apply handler is a NAMED gap; the packets are
    // built + enqueued through the real queue, wire-exact).
    sim::QueueRequestCoord27(q, res.playerId, res.fatherId, 127, 0, 0);
    sim::QueueRequestCoord27(q, res.fatherId, res.playerId, 127, 0, 0);
    sim::QueueRequestCoord27(q, res.playerId, res.motherId, 127, 0, 0);
    sim::QueueRequestCoord27(q, res.motherId, res.playerId, 127, 0, 0);
    sim::QueueRequestCoord27(q, res.fatherId, res.motherId, 127, 0, 0);
    sim::QueueRequestCoord27(q, res.motherId, res.fatherId, 127, 0, 0);
    // 0x5339b3..0x533a10 — EnqueueCmd15(parent, -1, 32*RandomModulo(0x200)
    // + 16000, byte_6477A1): father (var_10) first, then mother (var_14).
    res.pursefather = ((util::RandomModulo(0x200) & 0xFFFF) << 5) + 16000;
    sim::EnqueueCmd15(q, res.fatherId, -1, res.pursefather, in.rateByte);
    res.purseMother = ((util::RandomModulo(0x200) & 0xFFFF) << 5) + 16000;
    sim::EnqueueCmd15(q, res.motherId, -1, res.purseMother, in.rateByte);

    // ---- 5. mission slot ----------------------------------------------------
    // 0x533a15..0x533a4d — `if ((signed byte)0x63C8F1 > -1)  // dword_63C8F0+1, sar 0x18
    //   Mission_SlotRegister(player id, LOBYTE(dword_122F4EC))`. The byte
    // arrives as in.missionModeByte (cold image 0xFE = -2 -> skipped); the
    // same original byte backs world::g_missionSlotMode inside SlotRegister.
    if (static_cast<i8>(in.missionModeByte) > -1)
        res.missionSlot = world::MissionSlotRegister(res.playerId, in.missionId);

    // ---- 6. talents ---------------------------------------------------------
    // 0x533a29..0x533a37 — player bytes +0x80..+0x84 = byte_122F4F0[0..4]
    // (filled by the RunChooseHistory profession tail; derived here 1:1).
    if (in.talentsOverride)
        std::memcpy(res.talents, in.talentsOverride, 5);
    else
        NewGameProfessionTalents(p.professionVariant < 0 ? 0 : p.professionVariant,
                                 res.talents);
    for (int i = 0; i < 5; ++i) pl[0x80 + i] = res.talents[i];

    // ---- 7. the caller's new-single-player block (InitOrLoadSession) --------
    // 0x533b85 gate: `(word_63C740 & 1) && !(word_63C740 & 4)` — new local game.
    if (in.seedStartGold && !p.network) {
        // 0x533b9d..0x533bba — GameTime_Set(&t, 6, 0, 0) ->
        // QueueRequestFlagBlob32(3, &t): seed the world clock to 06:00 (the
        // opcode-32 case-3 apply writes the calendar record).
        u8 blob[124] = {};
        sim::GameTime t{};
        t.day = 0; t.hour = 6; t.minute = 0; t.second = 0;
        std::memcpy(blob, &t, sizeof(t));
        sim::QueueRequestFlagBlob32(q, 3, blob);
        // 0x533f35/0x5340e5 — base = cheat ? 75000 : 1250 - 250*difficulty.
        res.startGoldBase = app::NewGameStartGoldBase(in.cheatStartGold,
                                                      difficultyVariant);
        // 0x533f40..0x533f8f — for j over the 536-stride array: alive byte +8
        // != 0 and kind byte +2 in {6, 7} -> EnqueueCmd15(person id, -1,
        // MoneyMultiplyByRate(base, rate), rate).
        for (int j = 0; j < sim::kPersonCapacity; ++j) {
            const u8* rec = reinterpret_cast<const u8*>(&sim::g_persons[j]);
            if (rec[8] == 0) continue;                     // byte_12CE918[j]
            const u8 kind = rec[2];                        // byte_12CE912[j]
            if (kind != 6 && kind != 7) continue;
            const i32 amount = app::MoneyMultiplyByRate(res.startGoldBase,
                                                        in.rateByte);
            const i32 id = Get32(rec, 0x04);               // dword_12CE914[j]
            sim::EnqueueCmd15(q, id, -1, amount, in.rateByte);
            res.goldSeededIds.push_back(id);
        }
        Barrier(q);   // the bootstrap's QueueRequestFlagBlob32 ack wait
    } else {
        Barrier(q);   // drain the relationship/purse packets
    }

    return res;
}

} // namespace guild::play
