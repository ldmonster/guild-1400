// aimethod_recon3_registry.cpp — see aimethod_recon3_registry.h for scope/fidelity.
#include "sim/aimethod_recon3_registry.h"

#include <cstring> // std::memcpy, std::memmove, std::strlen

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x4794e4 — VIBE_AiNeeds_LookupAttributeIndex
// Case-insensitive name match, first hit wins; -1 when unmatched. Order and
// strings are byte-for-byte from the decompile (refs at 0x61aab8..0x61ab40).
// ---------------------------------------------------------------------------
namespace {
// VIBE_Util_StrCmpNoCase (0x5cb8f0): ASCII case-insensitive compare, returns 0 on
// equal. The original is a plain strcmp-style routine over the recognized tokens
// (all uppercase ASCII), so a per-char tolower compare is behavior-identical here.
bool eq_ci(const char* a, const char* b) {
    if (!a) return false;
    for (;; ++a, ++b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 32);
        if (ca != cb) return false;
        if (ca == 0) return true;
    }
}

// The 14 attribute tokens in their catalog order (index == array position).
const char* const kAttrNames[kAiAttributeCount] = {
    "APS",            // 0   aAps              0x61aab8
    "UNVERSEHRTHEIT", // 1   aUnversehrtheit   0x61aabc
    "WOHNUNG",        // 2   aWohnung          0x61aacc
    "GELD",           // 3   aGeld             0x61aad4
    "BERUF",          // 4   aBeruf_0          0x61aadc
    "VERGNUEGEN",     // 5   aVergnuegen       0x61aae4
    "ANSEHEN",        // 6   aAnsehen          0x61aaf0
    "AMT",            // 7   aAmt              0x61aaf8
    "BILDUNG",        // 8   aBildung_0        0x61aafc
    "RECHTSCHAFFENHEIT", // 9 aRechtschaffenh  0x61ab04
    "GEMEINHEIT",     // 10  aGemeinheit       0x61ab18
    "SICHERHEIT",     // 11  aSicherheit       0x61ab24
    "FORTPFLANZUNG",  // 12  aFortpflanzung    0x61ab30
    "TRAEGHEIT",      // 13  aTraegheit        0x61ab40
};
} // namespace

i8 AiNeeds_LookupAttributeIndex(const char* name) {
    if (!name)
        return -1;
    for (int i = 0; i < kAiAttributeCount; ++i) {
        if (eq_ci(name, kAttrNames[i]))
            return static_cast<i8>(i);
    }
    // 0x479671: logs via VIBE_Crt_Sprintf_0(byte_61AB4C, ...) then returns -1.
    return -1;
}

// ---------------------------------------------------------------------------
// Change-magnitude non-zero test: (bitcast<u32>(x) & 0x7FFFFFFF) != 0.
// (@0x4690ad / 0x4690bb: `v9[13] & 0x7FFFFFFF`). Masks the sign bit so +0.0 and
// -0.0 both read as "no change"; any other value (incl. subnormals) counts.
// ---------------------------------------------------------------------------
bool AiMethod_ChangeIsNonZero(f32 change) {
    u32 bits;
    std::memcpy(&bits, &change, sizeof(bits));
    return (bits & 0x7FFFFFFFu) != 0u;
}

// ---------------------------------------------------------------------------
// "Method has any effect" validation loop (@0x4690a8..0x4690cd).
// Per slot (4 slots): effective iff
//   (shortAttr != -1 && shortChange bits set) || (longAttr != -1 && longChange bits)
// Short-circuits on the first effective slot. Returns false if none qualify.
// ---------------------------------------------------------------------------
bool AiMethod_HasAnyEffect(const AiMethodRecord& rec) {
    for (int i = 0; i < kAiMethodDesireSlots; ++i) {
        const AiDesireSlot& s = rec.shortSlots[i];
        const AiDesireSlot& l = rec.longSlots[i];
        bool shortEff = (s.attrIndex != -1) && AiMethod_ChangeIsNonZero(s.change);
        bool longEff  = (l.attrIndex != -1) && AiMethod_ChangeIsNonZero(l.change);
        if (shortEff || longEff)
            return true;          // 0x4690bd: v10 = 1 (and the loop's !v10 exits)
    }
    return false;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x468f6c — VIBE_AiMethod_RegisterFromIni
// ---------------------------------------------------------------------------
int AiMethod_RegisterFromIni(AiMethodRecord& rec, bool haveIni,
                             const IniSource& ini, bool* outHasEffect) {
    // 1. id gate (@0x468f88). Signed byte compare: id <= 0 || id >= 61 -> reject.
    i8 idSigned = static_cast<i8>(rec.id);
    if (idSigned <= 0 || idSigned >= 61) {
        if (outHasEffect) *outHasEffect = false;
        return 0;                          // 0x46914a
    }

    if (haveIni) {
        // 2. per-slot fill, slots 0..3 (loop `while (v4 < 4)`).
        for (int slot = 0; slot < kAiMethodDesireSlots; ++slot) {
            // ----- short side -----
            const char* sName = ini.getShortDesire
                              ? ini.getShortDesire(slot, ini.ctx) : nullptr;
            if (sName) {                                   // key present (@0x468fd6)
                i8 attr = AiNeeds_LookupAttributeIndex(sName);
                rec.shortSlots[slot].attrIndex = attr;     // 0x46915e: v5[48]=v13
                if (attr >= 0) {                           // 0x469163
                    rec.shortSlots[slot].change = ini.getShortChange
                        ? ini.getShortChange(slot, ini.ctx) : 0.0f; // 0x4691ae
                }
                // (attr < 0: change left as-is; original does not write it.)
            } else {                                       // key absent
                rec.shortSlots[slot].attrIndex = -1;       // 0x468fe5: v5[48]=-1
                rec.shortSlots[slot].change    = 0.0f;     // 0x468fe9: *(dword)=0
            }
            // ----- long side -----
            const char* lName = ini.getLongDesire
                              ? ini.getLongDesire(slot, ini.ctx) : nullptr;
            if (lName) {                                   // 0x469022
                i8 attr = AiNeeds_LookupAttributeIndex(lName);
                rec.longSlots[slot].attrIndex = attr;      // 0x46903a: v5[112]=v8
                if (attr >= 0) {                           // 0x46903f
                    rec.longSlots[slot].change = ini.getLongChange
                        ? ini.getLongChange(slot, ini.ctx) : 0.0f; // 0x469086
                }
            } else {
                rec.longSlots[slot].attrIndex = -1;        // 0x4691b6
                rec.longSlots[slot].change    = 0.0f;      // 0x4691ba
            }
        }

        // validation: "method has no effect" check (@0x4690a8). Logging is a side
        // effect in the binary; we surface the boolean instead.
        bool hasEffect = AiMethod_HasAnyEffect(rec);
        if (outHasEffect) *outHasEffect = hasEffect;
        // (the engine's am_RegisterMethod "keinerlei Auswirkungen" log fires when
        //  !hasEffect; it does NOT change the return value.)

        // prev-mirror: copy the 4 short slots into the prev vector (@0x469107).
        std::memcpy(rec.prevSlots, rec.shortSlots, sizeof(rec.prevSlots));
    } else if (outHasEffect) {
        *outHasEffect = AiMethod_HasAnyEffect(rec);
    }

    // 3. commit (qmemcpy into catalog[148*id]) is done by the caller; we report ok.
    return 1;                              // 0x469141
}

// ---------------------------------------------------------------------------
// gilde.exe 0x468a40 — VIBE_AiMethod_LoadDataFile field schedule.
// Exact (offset, size) order from the Read/WriteStream call chains.
// ---------------------------------------------------------------------------
const AiMethodFieldSpec kAiMethodFieldSchedule[18] = {
    {  0, 1},  {  1, 32}, // id, name
    { 48, 1},  { 52, 4},  // short slot 0: attr, change
    { 56, 1},  { 60, 4},  // short slot 1
    { 64, 1},  { 68, 4},  // short slot 2
    { 72, 1},  { 76, 4},  // short slot 3
    {112, 1},  {116, 4},  // long slot 0
    {120, 1},  {124, 4},  // long slot 1
    {128, 1},  {132, 4},  // long slot 2
    {136, 1},  {140, 4},  // long slot 3
};

bool AiMethod_StreamRecord(u8* recordBytes, const ByteStream& stream,
                           bool mirrorPrevOnSuccess) {
    if (!stream.xfer || !recordBytes)
        return false;
    for (int i = 0; i < kAiMethodFieldCount; ++i) {
        const AiMethodFieldSpec& f = kAiMethodFieldSchedule[i];
        if (!stream.xfer(recordBytes, f.offset, f.size, stream.ctx))
            return false;                  // first failed field stops the record
    }
    if (mirrorPrevOnSuccess) {
        // READ path: MemMove(record+80, record+48, 32) (@0x468f39).
        std::memmove(recordBytes + 80, recordBytes + 48, 32);
    }
    return true;
}

} // namespace guild::sim
