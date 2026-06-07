#include "sim/handler_entry.h"

#include "sim/gametime.h"

#include <cstdarg>
#include <cstring>

namespace guild::sim {

// ===========================================================================
// HandlerTable — the byte_11D6040 handler-entry pool + per-type tables.
// ===========================================================================

HandlerTable::HandlerTable() {
    pool_ = new HandlerRecord[kHandlerSlots];
    std::memset(pool_, 0, sizeof(HandlerRecord) * kHandlerSlots);
}

HandlerTable::~HandlerTable() { delete[] pool_; }

// gilde.exe 0x4c5248 — VIBE_He_InitHandlerTable (resettable portion). The
// original also calls the CharAction/Event/Building registrars and memsets three
// side tables (the icon-mesh scratch); those belong to other clusters, so we
// reset the pool/counters and install the type-0 fallbacks (matching the
// dword_1229040[0]/funcs_4C6EE9[0] = LogInvalid* assignment).
int HandlerTable::Init() {
    std::memset(pool_, 0, sizeof(HandlerRecord) * kHandlerSlots);
    std::memset(icons_, 0, sizeof(icons_));
    for (u32 i = 0; i < kNumHandlerTypes; ++i) { initFns_[i] = nullptr; runFns_[i] = nullptr; }
    highWater_ = 0;
    iconHigh_  = 0;
    liveCount_ = 0;
    nextOrdinal_ = 0;
    cursor_ = nullptr;
    fFirst_ = true;
    // slot 0 fallbacks: the original installs LogInvalidAllocType / LogInvalidRunType.
    // We model them as inert no-ops (they only log) so slot 0 is "present".
    initFns_[0] = [](HandlerRecord*) {};
    runFns_[0]  = [](HandlerRecord*) -> i32 { return 0; };
    return 0;
}

// gilde.exe 0x4c5398 — VIBE_He_RegisterHandlerByType.
int HandlerTable::RegisterHandlerByType(u8 type, HandlerInitFn initFn, HandlerRunFn runFn) {
    if (type >= kNumHandlerTypes)
        return 1;
    initFns_[type] = initFn;
    runFns_[type]  = runFn;
    return 0;
}

// gilde.exe 0x4c5f40 — VIBE_He_AllocHandlerEntry. `desc` is the stack template
// (a HeRecord laid out like the original's a1). The descriptor fields below use
// the original a1 offsets.
HandlerRecord* HandlerTable::AllocHandlerEntry(const HeRecord* desc) {
    const u8* a1 = reinterpret_cast<const u8*>(desc);
    u8 kind = a1[4];                                  // *(a1+4)
    if (kind >= kNumHandlerTypes || !initFns_[kind] || !runFns_[kind])
        return nullptr;

    i32 personId = *reinterpret_cast<const i32*>(a1 + 8);   // *(a1+8)
    const u8* person = nullptr;
    if (personId != -1) {
        person = personFind_ ? static_cast<const u8*>(personFind_(personId)) : nullptr;
        if (!person)
            return nullptr;                            // person required but absent
    }

    // find a free pool slot (kind byte == 0)
    u32 idx = 0;
    if (HrKind(&pool_[0])) {
        do { ++idx; } while (idx < kHandlerSlots && HrKind(&pool_[idx]));
    }
    if (idx == kHandlerSlots)
        return nullptr;                                // pool full

    HandlerRecord* v4 = &pool_[idx];
    if (static_cast<i32>(idx) > highWater_)
        highWater_ = static_cast<i32>(idx);

    // person marker/id mirror (+8 word, +12 dword)
    if (personId == -1) {
        HrIndex(v4) = 0xFFFF;
        HrId(v4)    = -1;
    } else {
        HrId(v4)    = *reinterpret_cast<const i32*>(person + 4); // *((_DWORD*)person+1)
        HrIndex(v4) = *reinterpret_cast<const u16*>(person);     // *(_WORD*)person
    }

    u8 flags = a1[54];                                 // *(a1+54)
    if (flags & kHfHasIcon) {
        // link into the icon array (dword_11D5A20)
        u32 j = 0;
        if (icons_[0]) {
            do { ++j; } while (j < kIconSlots && icons_[j]);
        }
        if (j == kIconSlots)
            return nullptr;                            // icon array full
        icons_[j] = v4;
        if (static_cast<i32>(j) > iconHigh_)
            iconHigh_ = static_cast<i32>(j);
    }

    // common stamping
    HrKind(v4)    = kind;                               // *v4 = kind
    HrOrdinal(v4) = nextOrdinal_++;                     // *((_DWORD*)v4+1) = dword_632244++
    HrField16(v4) = *reinterpret_cast<const i32*>(a1 + 12); // *((_DWORD*)v4+4) = *(a1+12)

    // copy 8 dwords a1[+16..+47] -> v4 dword 5..12 (record +20..+51)
    for (int k = 0; k < 8; ++k) {
        u32 v = *reinterpret_cast<const u32*>(a1 + 16 + 4 * k);
        *reinterpret_cast<u32*>(v4->bytes + 20 + 4 * k) = v;
    }
    // a1[+40/+44/+48] -> v4 dword 17/18/19 (record +68/+72/+76)
    *reinterpret_cast<u32*>(v4->bytes + 68) = *reinterpret_cast<const u32*>(a1 + 40);
    *reinterpret_cast<u32*>(v4->bytes + 72) = *reinterpret_cast<const u32*>(a1 + 44);
    *reinterpret_cast<u32*>(v4->bytes + 76) = *reinterpret_cast<const u32*>(a1 + 48);
    // a1[+52] word -> v4 word 40 (record +80)
    *reinterpret_cast<u16*>(v4->bytes + 80) = *reinterpret_cast<const u16*>(a1 + 52);

    // clear scratch dwords 28/29/31 (record +112/+116/+124), flags byte, dword 33 (+132)
    *reinterpret_cast<u32*>(v4->bytes + 112) = 0;
    *reinterpret_cast<u32*>(v4->bytes + 116) = 0;
    *reinterpret_cast<u32*>(v4->bytes + 124) = 0;
    HrFlags(v4) = flags;                               // v4[120] = flags
    *reinterpret_cast<i32*>(v4->bytes + 132) = -1;     // *((_DWORD*)v4+33) = -1

    // a1[+56..] -> v4 dword 34.. (record +136..); loop runs while v15 != a1+32:
    // i.e. 8 iterations reading a1[+56], a1[+60], ... wait the original loops
    // v15 from a1 to a1+32 (8 dwords) reading *(v15+56) -> v4 dword34+. Faithful:
    for (int k = 0; k < 8; ++k) {
        u32 v = *reinterpret_cast<const u32*>(a1 + 56 + 4 * k);
        *reinterpret_cast<u32*>(v4->bytes + 136 + 4 * k) = v;
    }
    // qmemcpy(v4+172, a1+88, 0xA0)
    std::memcpy(v4->bytes + 172, a1 + 88, 0xA0);

    // per-type init callback
    initFns_[kind](v4);
    ++liveCount_;
    return v4;
}

// gilde.exe 0x4c6144 — VIBE_He_FreeHandlerEntry.
i32 HandlerTable::FreeHandlerEntry(HandlerRecord* r) {
    if (!r)
        return 0;
    // free gfx ptr at +124
    if (HrGfx(r)) {
        // VIBE_Memory_FreeDebug — inert here (we never allocate it); just clear.
        HrGfx(r) = 0;
    }
    if (HrFlags(r) & kHfHasIcon) {
        // locate r in the icon array
        u32 j = 0;
        if (r != icons_[0]) {
            do { ++j; } while (j < kIconSlots && r != icons_[j]);
        }
        if (r == icons_[j]) {
            // (original tears down an event-panel slot here if +116 holds one and
            // its +8 != -1; that is a GUI leaf — skipped, no panel in isolation.)
            icons_[j] = nullptr;
            if (static_cast<i32>(j) == iconHigh_) {
                i32 h = iconHigh_;
                if (h >= 0) {
                    i32 k = h;
                    while (k >= 0) {
                        if (icons_[k]) break;
                        --k;
                    }
                    j = (k < 0) ? 0u : static_cast<u32>(k);
                }
                iconHigh_ = (static_cast<i32>(j) < 0) ? 0 : static_cast<i32>(j);
            }
        } else {
            return 0;   // not found in icon array (matches the original's bail)
        }
    }
    --liveCount_;
    std::memset(r, 0, kHandlerStride);                 // SetGrayColorThunk(0,332,r)

    // walk the pool high-water index back down if we freed the top record
    HandlerRecord* top = &pool_[highWater_];
    if (r == top) {
        i32 v = highWater_;
        if (highWater_ >= 0) {
            i32 k = highWater_;
            while (k >= 0) {
                if (HrKind(&pool_[k])) break;
                --k; --v;
            }
        }
        highWater_ = (v < 0) ? 0 : v;
    }
    // The original returns the address of the (now-zeroed) top record in eax; all
    // callers treat it as opaque/ignored. We return 0 (success).
    return 0;
}

// gilde.exe 0x4c63f8 — VIBE_He_FindFirstHandlerByFilter (varargs). Each filter is
// a (selector:int, value:int) pair.
HandlerRecord* HandlerTable::FindFirstHandlerByFilter(int count, ...) {
    fKind_  = 0xFF;
    i32 id  = -1;
    fIndex_ = 0xFFFF;
    i32 field = -1;
    fFirst_ = true;

    va_list ap;
    va_start(ap, count);
    for (int i = 0; i < count; ++i) {
        int sel = va_arg(ap, int);
        switch (sel) {
            case 0: fKind_  = static_cast<u8>(va_arg(ap, int)); break;
            case 1: id      = va_arg(ap, int); break;
            case 2: fIndex_ = static_cast<u16>(va_arg(ap, int)); break;
            case 3: field   = va_arg(ap, int); break;
            default: /* unknown selector: consume nothing extra (orig keeps prev) */
                break;
        }
    }
    va_end(ap);

    fId_    = id;
    fField_ = field;

    // seed the cursor at the first live record
    cursor_ = nullptr;
    for (u32 i = 0; i < kHandlerSlots; ++i) {
        if (HrKind(&pool_[i])) { cursor_ = &pool_[i]; break; }
    }
    return FindNextMatchingHandler();
}

// gilde.exe 0x4c6278 — VIBE_He_FindNextMatchingHandler.
HandlerRecord* HandlerTable::FindNextMatchingHandler() {
    HandlerRecord* top = &pool_[highWater_];
    HandlerRecord* v2 = cursor_;
    // On a continuation call the scan terminates once the cursor has reached or
    // passed the top live record (the record at the high-water index was already
    // returned). On the FIRST call after FindFirst the cursor is the first live
    // record and must be processed THROUGH the top inclusive, so the at-top bail
    // only applies to continuation calls.
    if (!cursor_ || (!fFirst_ && cursor_ >= top)) {
        cursor_ = nullptr;
        return nullptr;
    }
    if (fFirst_)
        fFirst_ = false;
    else
        v2 = cursor_ + 1;

    bool matched = false;
    bool atEnd   = false;
    do {
        if (HrKind(v2)) {
            matched = true;   // default true; each active filter can reset to false
            if (fKind_ != 0xFF) {
                if (HrKind(v2) != fKind_) matched = false;
            }
            if (matched && fId_ != -1) {
                if (HrOrdinal(v2) != fId_) matched = false;
            }
            if (matched && fIndex_ != 0xFFFF) {
                if (HrIndex(v2) != fIndex_) matched = false;
            }
            if (matched && fField_ != -1) {
                if (HrField16(v2) != fField_) matched = false;
            }
            // NB: the original treats "all active filters absent" (kind==-1 &&
            // id==-1 && index==-1 && field==-1) as a non-match (v0 stays 0), so a
            // fully-wild filter walks to the end and returns null. We mirror that:
            if (fKind_ == 0xFF && fId_ == -1 && fIndex_ == 0xFFFF && fField_ == -1)
                matched = false;
        } else {
            matched = false;
        }
        if (matched)
            break;
        if (v2 >= top)
            atEnd = true;
        else
            ++v2;
    } while (!matched && !atEnd);

    if (!matched)
        v2 = nullptr;
    cursor_ = v2;
    return v2;
}

// gilde.exe 0x537358 — VIBE_He_CountMatchingHandlers.
int HandlerTable::CountMatchingHandlers(const u16* markerWord) {
    HandlerRecord* first = markerWord
        ? FindFirstHandlerByFilter(1, 2, static_cast<int>(*markerWord))
        : FindFirstHandlerByFilter(1, 2, 0xFFFF);
    if (!first)
        return 0;
    int n = 1;
    while (FindNextMatchingHandler())
        ++n;
    return n;
}

// gilde.exe 0x4c6e0c — VIBE_He_RefreshEntityHandlers.
HandlerRecord* HandlerTable::RefreshEntityHandlers(const u16* markerWord) {
    HandlerRecord* r = FindFirstHandlerByFilter(1, 2, static_cast<int>(*markerWord));
    for (; r; r = FindNextMatchingHandler()) {
        if (worldPos_) worldPos_(r);
    }
    return r;
}

// gilde.exe 0x4c52d8 — VIBE_He_TickActiveHandlers (dominant path: free every
// live record up to the high-water index).
i32 HandlerTable::TickActiveHandlers() {
    i32 result = 0;
    if (highWater_ >= 0) {
        i32 i = 0;
        while (i <= highWater_ && i < static_cast<i32>(kHandlerSlots)) {
            HandlerRecord* r = &pool_[i];
            if (HrKind(r))
                result = FreeHandlerEntry(r);
            ++i;
        }
    }
    return result;
}

// gilde.exe 0x4c6e38 — VIBE_He_RunAllHandlers (the icon/world-pos refresh is a
// renderer leaf, delegated to the inert worldPos_ hook; the gating + run-callback
// dispatch is translated faithfully).
i32 HandlerTable::RunAllHandlers(const GameTime& clock) {
    i32 result = 0;
    if (highWater_ >= 0) {
        i32 i = 0;
        while (i <= highWater_) {
            if (i >= static_cast<i32>(kHandlerSlots)) break;
            HandlerRecord* r = &pool_[i];
            if (HrKind(r)) {
                if ((HrFlags(r) & 0x18) == 0) {
                    if (worldPos_) worldPos_(r);   // AssignIconForHandler leaf
                    const GameTime* appt = &He_ApptTime(reinterpret_cast<HeRecord*>(r));
                    if (GameTimeCompare(&clock, appt) > 0 && HrKind(r) < kNumHandlerTypes) {
                        if (worldPos_) worldPos_(r);
                        result = runFns_[HrKind(r)] ? runFns_[HrKind(r)](r) : 0;
                    }
                }
            }
            ++i;
        }
    }
    return result;
}

// gilde.exe 0x4c6eb4 — VIBE_He_RunMessageBoxHandlers.
i32 HandlerTable::RunMessageBoxHandlers() {
    i32 result = 0;
    if (iconHigh_ >= 0) {
        i32 n = 0, i = 0;
        while (n <= iconHigh_ && i < static_cast<i32>(kIconSlots)) {
            HandlerRecord* r = icons_[i];
            if (r && HrKind(r) < kNumHandlerTypes) {
                if (worldPos_) worldPos_(r);
                result = runFns_[HrKind(r)] ? runFns_[HrKind(r)](r) : 0;
            }
            ++n; ++i;
        }
    }
    return result;
}

} // namespace guild::sim
