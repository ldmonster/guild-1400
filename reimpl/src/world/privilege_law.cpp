// Privilege / Gesetz / Office law-flow remainder — 1:1 ports. See privilege_law.h
// for the addr map and the rationale for the installable hooks (build model: a
// stub referenced from src/ must be DEFINED in src/, so cross-module leaves go
// through PrivilegeLawHooks whose inert defaults live here).
#include "world/privilege_law.h"

namespace guild::world {

namespace {

PrivilegeLawHooks g_hooks{};

// ---- inert defaults (match the deterministic/no-op behaviour) --------------
u16  DefRandMod(u32 /*n*/)                       { return 0; }
u8   DefComputeVariant(int a, int /*b*/)         { return static_cast<u8>(a); }
int  DefRankReq(u8, i16*, i16*, u8*, u8*)        { return 0; }
int  DefEnqueueObj(int, int, u8, int)            { return 0; }
int  DefApply(i32, u8)                           { return 1; }
int  DefPageWidth(int)                           { return 640; }
int  DefContentWidth()                           { return 0; }
int  DefBuildPersonCard(int, i16, int, CandidateCard*) { return 0; }

u16 RandMod(u32 n) {
    return g_hooks.randMod ? g_hooks.randMod(n) : DefRandMod(n);
}
u8 ComputeVariant(int a, int b) {
    return g_hooks.computeVariant ? g_hooks.computeVariant(a, b)
                                  : DefComputeVariant(a, b);
}
int PageWidth(int win) {
    return g_hooks.pageWidth ? g_hooks.pageWidth(win) : DefPageWidth(win);
}
int ContentWidth() {
    return g_hooks.contentWidth ? g_hooks.contentWidth() : DefContentWidth();
}

} // namespace

void PrivilegeLawSetHooks(const PrivilegeLawHooks& hooks) { g_hooks = hooks; }
const PrivilegeLawHooks& PrivilegeLawGetHooks() { return g_hooks; }
void PrivilegeLawResetHooks() { g_hooks = PrivilegeLawHooks{}; }

// NOTE: VIBE_Office_FindRoleTemplate (0x57c184) is NOT reconstructed here — it
// is already translated as guild::world::OfficeFindRoleTemplate in office_law3.cpp
// (with the genuine recovered role tables). Re-defining it would be an ODR clash.

// ===========================================================================
// 0x47fcf0  VIBE_Office_ConfirmCandidacyDialog
// ===========================================================================
// Original (recOffice2 == *(rec+2)):
//   v10[0]=type; v12[0]=3;
//   if (*(rec+2) != 19) return 0;
//   if (!GetRankRequirements(type,&v5)) return 0;
//   ageRoll  = RandomModulo(ageMax+1-ageMin);   // v7+1-v6
//   QueueRequest16(*(rec+4), -1, ageMin+ageRoll, byte_6477A1);
//   money    = RandomModulo(moneyMax+1-moneyMin)+moneyMin;  // v9+1-v8
//   BeginDeltaPacket; 3x AppendDeltaField (money, kind=3, type); QueueRequestState22;
//   ApplyForCandidacy(rec, type);
//   return 1;
// The GetRankRequirements block lays out: v6=ageMin, v7=ageMax (the +1-range pair),
// v8=moneyMin, v9=moneyMax. We surface those four scalars via the rankReq hook.
int OfficeConfirmCandidacyDialog(i32 personId, u8 officeType, u8 recOffice2,
                                 CandidacyCommit* out) {
    CandidacyCommit local{};
    local.officeType = officeType;

    if (recOffice2 != 19) {
        if (out) *out = local;
        return 0;
    }

    i16 ageMin = 0, ageMax = 0;
    u8  moneyMin = 0, moneyMax = 0;
    int ok = g_hooks.rankReq ? g_hooks.rankReq(officeType, &ageMin, &ageMax,
                                               &moneyMin, &moneyMax)
                             : DefRankReq(officeType, &ageMin, &ageMax,
                                          &moneyMin, &moneyMax);
    if (!ok) {
        if (out) *out = local;
        return 0;        // requirement failure -> return result(0)
    }

    local.eligible = true;
    u16 ageRoll = RandMod(static_cast<u32>(ageMax + 1 - ageMin));
    local.ageRoll = static_cast<i32>(ageMin) + ageRoll;

    u8 moneyRoll = static_cast<u8>(RandMod(static_cast<u32>(moneyMax + 1 - moneyMin)));
    local.moneyRoll = static_cast<u8>(moneyMin + moneyRoll);

    if (g_hooks.queueCandidacy)
        g_hooks.queueCandidacy(personId, local.ageRoll, local.moneyRoll, officeType);

    int apply = g_hooks.applyForCandidacy ? g_hooks.applyForCandidacy(personId, officeType)
                                          : DefApply(personId, officeType);
    (void)apply; // original ignores the ApplyForCandidacy return value

    if (out) *out = local;
    return 1;
}

// ===========================================================================
// 0x47fc24  VIBE_Office_RenderRequirementText
// ===========================================================================
// Original (a1 == rank):
//   if (!GetRankRequirements(a1,&v9)) return -1;
//   if (byte_62EE34 == 2) byte_62EE34 = RandomModulo(2);   // seed sticky parity
//   v1 = RandomModulo(v9[10]+1-v9[8]);     // ageMax+1-ageMin band -> we reuse the
//                                          //   rankReq scalars (ageMin/ageMax)
//   v2 = RandomModulo(12);
//   v4 = ComputeVariantIndex(v2+1, v3+v1); // v3 is uninit garbage in the original;
//                                          //   modeled as 0 (the only deterministic
//                                          //   reading) — documented quirk.
//   v8 = unk_62EE31 >> 24;  (an id high byte; modeled via the second range below)
//   v5 = RandomModulo(v9[1]+1-v9[0]);      // a second band -> moneyMin/moneyMax
//   result = EnqueueObjectInteraction(19, -1, range0Min+v5, -1, -1, v4, 0, v8);
//   byte_62EE34 = 1 - byte_62EE34;
//   return result;
// We expose the GetRankRequirements scalars as (ageMin/ageMax) and (moneyMin/
// moneyMax) — the two ranges the two RandomModulo calls consume.
int OfficeRenderRequirementText(u8 rank, u8* parity) {
    i16 ageMin = 0, ageMax = 0;
    u8  moneyMin = 0, moneyMax = 0;
    int ok = g_hooks.rankReq ? g_hooks.rankReq(rank, &ageMin, &ageMax,
                                               &moneyMin, &moneyMax)
                             : DefRankReq(rank, &ageMin, &ageMax, &moneyMin, &moneyMax);
    if (!ok)
        return -1;

    u8 localParity = parity ? *parity : 2;
    if (localParity == 2)
        localParity = static_cast<u8>(RandMod(2));

    u16 v1 = RandMod(static_cast<u32>(ageMax + 1 - ageMin));   // first band roll
    u8  v2 = static_cast<u8>(RandMod(12));
    u8  v4 = ComputeVariant(v2 + 1, /*v3==0 quirk*/ 0 + v1);

    u16 v5 = RandMod(static_cast<u32>(moneyMax + 1 - moneyMin)); // second band roll
    int target = static_cast<int>(moneyMin) + v5;               // v9[0] + v5

    int result = g_hooks.enqueueObjInteraction
                     ? g_hooks.enqueueObjInteraction(19, target, v4, /*v8 id-high*/ 0)
                     : DefEnqueueObj(19, target, v4, 0);

    localParity = static_cast<u8>(1 - localParity);
    if (parity) *parity = localParity;
    return result;
}

// ===========================================================================
// 0x555eb4  VIBE_Office_PrepareCandidatePage
// ===========================================================================
// Original (a1=form, a2=win, a3=alt):
//   SelectWindow(a1,a2); RenderRichString("$C"); RemoveChildren(curPage,1);
//   if (a3 == -1) return v5(uninit);     // -> we return 0
//   dword_67EDC8[...] = 0;
//   SelectWindow(a1,a3); RenderRichString("$C"); CreateScrollButtons(...);
//   SelectWindow(a1,a2);
//   return 0;
int OfficePrepareCandidatePage(int formId, int windowId, int altWindowId) {
    if (g_hooks.selectWindow)   g_hooks.selectWindow(formId, windowId);
    if (g_hooks.removeChildren) g_hooks.removeChildren(windowId);

    if (altWindowId == -1)
        return 0; // a3 == -1: original returns an uninitialized ecx; we return 0

    if (g_hooks.selectWindow)        g_hooks.selectWindow(formId, altWindowId);
    if (g_hooks.createScrollButtons) g_hooks.createScrollButtons(altWindowId);
    if (g_hooks.selectWindow)        g_hooks.selectWindow(formId, windowId);
    return 0;
}

// ===========================================================================
// 0x556108 / 0x55623c / 0x556370  ShowCandidateCardListVariant{A,B,C}
// ===========================================================================
// The three variants differ ONLY in the centered-label tag (8C8168/6C/70 ->
// variant 0/1/2). Faithful body:
//   if (!count) return 0;
//   page = PrepareCandidatePage(form, win, alt);
//   half = pageWidth/2; AddCenteredLabel(tag, half, 2*half, 8, 68);
//   v7=0; do {                              // exactly `count` iterations
//       slot = &cards[i];
//       slot.x = (pageWidth - contentWidth)/2;
//       slot.y = v7+40;
//       BuildPersonCard(slot.x, slot.y, 69, slot, 1);
//       v7 += 112;
//   } while (v7 < 112*count);
//   for (i=0;i<count;++i) SetEnabled(cards[i].objectId, 1);
//   return 1;
int OfficeShowCandidateCardList(int variant, int formId, int windowId,
                                int altWindowId, int count, CandidateCard* cards) {
    if (count == 0)
        return 0;

    OfficePrepareCandidatePage(formId, windowId, altWindowId);

    const int pageWidth = PageWidth(windowId);
    const int half = pageWidth / 2;
    if (g_hooks.addCenteredLabel)
        g_hooks.addCenteredLabel(variant, half, 2 * half);

    const int contentWidth = ContentWidth();
    const i16 x = static_cast<i16>((pageWidth - contentWidth) / 2);

    int v7 = 0;
    int i = 0;
    do {
        if (cards) {
            cards[i].x = x;
            cards[i].y = static_cast<i16>(v7 + 40);
            int id = g_hooks.buildPersonCard
                         ? g_hooks.buildPersonCard(x, cards[i].y, 69, &cards[i])
                         : DefBuildPersonCard(x, cards[i].y, 69, &cards[i]);
            if (!g_hooks.buildPersonCard)
                cards[i].objectId = static_cast<i16>(id);
        }
        v7 += kCandidateCardPitch;
        ++i;
    } while (v7 < kCandidateCardPitch * count);

    if (count > 0) {
        for (int j = 0; j < count; ++j) {
            if (g_hooks.setEnabled && cards)
                g_hooks.setEnabled(cards[j].objectId, 1);
        }
    }
    return 1;
}

// ===========================================================================
// 0x49db90  VIBE_Office_CollectActorsByOwner
// ===========================================================================
// Original:
//   SetGrayColorThunk(...);  // cosmetic, dropped
//   v1=0; result=actorTable; owner=off_649D64; v4=0;
//   do {
//       v5 = *((DWORD*)result + 97);        // actor's owner pointer
//       if (v5 && owner == *(char**)(v5+136))  // owner identity match
//           dword_11BB69C[++v4] = result;    // PRE-increment: out[1..count]
//       ++v1; result += 268;
//   } while (v1 < 768 && v4 < 31);
//   return result;   // the scan loop returns the *count* via v4
// `ownerOf(index)` yields the owner identity for actor `index` (0 == no owner /
// no match). We compare it against `ownerKey`. The 1-based out write order is a
// verbatim quirk of the pre-increment (out[0] is left untouched).
int OfficeCollectActorsByOwner(i32 ownerKey,
                               i32 (*ownerOf)(int actorIndex),
                               i32* out, int outCapacity) {
    int count = 0;
    for (int i = 0; i < kActorTableCount && count < kActorMaxMatches; ++i) {
        i32 owner = ownerOf ? ownerOf(i) : 0;
        if (owner != 0 && owner == ownerKey) {
            ++count;                         // pre-increment, as the original
            if (out && count < outCapacity)  // out[count]; out[0] left untouched
                out[count] = i;
        }
    }
    return count;
}

} // namespace guild::world
