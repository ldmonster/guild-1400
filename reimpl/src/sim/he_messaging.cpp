// 1:1 reconstruction of the "He" entity-handler messaging + icon cluster.
// See he_messaging.h for the function map and the packet/offset provenance.
#include "sim/he_messaging.h"
#include "world/world_history2.h"   // HeIconSlot / HeIconPool (the shared 64-slot pool)

#include <cmath>
#include <cstdint>
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered constants (get_bytes):
//   flt_61E73C = 0x40C90FDB = 6.2831855f   (2*PI)            — angle scale
//   dbl_61E744 = 0x3FE0000000000000 = 0.5  (double)          — height mid factor
//   42C80000   = 100.0f                                      — circle radius
// ---------------------------------------------------------------------------
namespace {
constexpr double kTwoPi        = 6.2831854820251465;  // (float)0x40C90FDB widened
constexpr double kHeightMid    = 0.5;                  // dbl_61E744
constexpr float  kCircleRadius = 100.0f;              // 42C80000

void   InertReportError(const char*) {}
const void* InertPersonFind(i32) { return nullptr; }
u8     InertPersonKind(const void*) { return 0; }
int    InertQueue(const u8*, unsigned, int, const char*) { return 0; }
const void* InertBuildingFind(i32) { return nullptr; }
int    InertObjFindByHandle(const char*, i32) { return 0; }
int    InertCreateGfx(i32, const char*, const char*) { return 0; }
i32    InertRecordDword(i32, int) { return 0; }
u8     InertRecordByte(i32, int) { return 0; }
u16    InertRecordWord(i32, int) { return 0; }
int    InertIconsEnabled() { return 1; }
u16    InertLocalCity() { return 0; }
i32    InertSuppress() { return 0; }
i32    InertPersonFlags(u16) { return 0; }
void   InertTransformAnchor(const float*, const float*, float* out) { out[0]=out[1]=out[2]=0.0f; }
void   InertMeshHeight(i32, float* lo, float* hi) { *lo=0.0f; *hi=0.0f; }
void   InertObjSetPos(i32, const float*) {}
void   InertObjSetValue(i32, const char*, int, int, int) {}
void   InertFormVisible(i32, int) {}
void   InertFormRaise(i32) {}
int    InertFeatureSync() { return 0; }

const HeMessagingHooks kInert = {
    InertPersonFind, InertPersonKind, InertQueue, InertReportError,
    InertBuildingFind, InertObjFindByHandle, InertCreateGfx,
    InertRecordDword, InertRecordByte, InertRecordWord,
    InertIconsEnabled, InertLocalCity, InertSuppress, InertPersonFlags,
    InertTransformAnchor, InertMeshHeight, InertObjSetPos,
    InertObjSetValue, InertFormVisible, InertFormRaise, InertFeatureSync,
};

HeMessagingHooks g_hooks = kInert;

// Event-panel module state — mirrors the engine globals.
//   dword_63226C : panel-enabled gate (-1 == disabled). Default -1 (disabled).
//   dword_632270 : currently active slot pointer (0 == none).
//   dword_62EB4C : input-suppress flag.
int   g_panelEnabledGate = -1;   // dword_63226C
void* g_activeSlot       = nullptr; // dword_632270
int   g_inputSuppress    = 0;    // dword_62EB4C

// Wide (2-byte-step) copy: copy byte pairs from src to dst until the LOW byte of a
// pair is zero (the original loops `*d=*s; if(!*s) break; d[1]=s[1]; s+=2; d+=2;`).
// Faithful to the original's exact termination (it always writes the terminating
// low byte; the high byte of the terminating pair is NOT copied).
void HeWideCopy(u8* dst, const char* src) {
    u8* d = dst;
    const char* s = src;
    for (;;) {
        char lo = *s;
        d[0] = static_cast<u8>(lo);
        if (!lo)
            break;
        char hi = s[1];
        s += 2;
        d[1] = static_cast<u8>(hi);
        d += 2;
        if (!hi)
            break;
    }
}

// strlen+1 (byte length including the NUL), matching the original repne-scasb count.
unsigned StrLenPlus1(const char* s) {
    return static_cast<unsigned>(std::strlen(s)) + 1u;
}

inline void PutByte(u8* buf, int off, u8 v)  { buf[off] = v; }
inline void PutDword(u8* buf, int off, i32 v) { std::memcpy(buf + off, &v, 4); }

// The original is 32-bit: record pointers ARE 32-bit ints. On a 64-bit host we
// pass record handles as the recordDword/Byte/Word hooks' `i32` argument; the
// caller (the test harness or the live binding) controls how a record handle maps
// back to memory. AsI32 narrows a host pointer to the engine's 32-bit handle width
// (faithful to the original's pointer-as-dword usage).
inline i32 AsI32(const void* p) {
    return static_cast<i32>(reinterpret_cast<std::uintptr_t>(p));
}

} // namespace

void SetHeMessagingHooks(const HeMessagingHooks* hooks) {
    if (!hooks) { g_hooks = kInert; return; }
    HeMessagingHooks h = *hooks;
    if (!h.personFindById)       h.personFindById       = kInert.personFindById;
    if (!h.personKindByte)       h.personKindByte       = kInert.personKindByte;
    if (!h.queueRequestBuffer28) h.queueRequestBuffer28 = kInert.queueRequestBuffer28;
    if (!h.reportError)          h.reportError          = kInert.reportError;
    if (!h.buildingFindById)     h.buildingFindById     = kInert.buildingFindById;
    if (!h.objectFindByHandle)   h.objectFindByHandle   = kInert.objectFindByHandle;
    if (!h.createGfxInfo)        h.createGfxInfo        = kInert.createGfxInfo;
    if (!h.recordDword)          h.recordDword          = kInert.recordDword;
    if (!h.recordByte)           h.recordByte           = kInert.recordByte;
    if (!h.recordWord)           h.recordWord           = kInert.recordWord;
    if (!h.iconsEnabled)         h.iconsEnabled         = kInert.iconsEnabled;
    if (!h.localCityMarker)      h.localCityMarker      = kInert.localCityMarker;
    if (!h.suppressFlag)         h.suppressFlag         = kInert.suppressFlag;
    if (!h.personFlagsDword)     h.personFlagsDword     = kInert.personFlagsDword;
    if (!h.transformAnchor)      h.transformAnchor      = kInert.transformAnchor;
    if (!h.meshHeightRange)      h.meshHeightRange      = kInert.meshHeightRange;
    if (!h.objectSetPosition)    h.objectSetPosition    = kInert.objectSetPosition;
    if (!h.objectSetValueOrText) h.objectSetValueOrText = kInert.objectSetValueOrText;
    if (!h.formSetObjectsVisible) h.formSetObjectsVisible = kInert.formSetObjectsVisible;
    if (!h.formRaiseWindows)     h.formRaiseWindows     = kInert.formRaiseWindows;
    if (!h.featureSyncBit)       h.featureSyncBit       = kInert.featureSyncBit;
    g_hooks = h;
}
const HeMessagingHooks& GetHeMessagingHooks() { return g_hooks; }

// ===========================================================================
// 0x4c5c54 — VIBE_He_SendEntityMessage
// ===========================================================================
int He_SendEntityMessage(i32 recipientId, i32 a2, const char* text,
                         i32 a4, const char* secondStr) {
    const void* rec = g_hooks.personFindById(recipientId);  // VIBE_Person_FindRecordById
    if (!rec)
        return -1;
    u8 kind = g_hooks.personKindByte(rec);                   // *(BYTE*)(rec+2)
    if (kind != 6 && kind != 7)
        return -1;

    // 8096-byte payload staging buffer (v18) + the 248-byte header (v19).
    static thread_local u8 payload[8096];
    u8 header[kHeMsgHeaderBytes];
    std::memset(header, 0, sizeof(header));                  // SetGrayColorThunk(0,0xF8,buf)

    PutByte(header, kHeMsgTypeOff, kHeMsgTypeValue);         // [4]=0x11
    PutDword(header, kHeMsgSenderOff, recipientId);          // [8]=a1
    PutDword(header, kHeMsgArg2Off, a2);                     // [12]=a2
    PutByte(header, kHeMsgFlagsOff, 0);                      // [0xD4]=cl (uninitialised -> 0)
    PutByte(header, kHeMsgSubKindOff, kHeMsgSubKindValue);   // [0x36]=9
    PutDword(header, kHeMsgPayLenOff, a4);                   // [0x98]=a4

    HeWideCopy(payload, text);                               // wide-copy text into v18
    unsigned v15 = StrLenPlus1(text);                        // strlen(text)+1

    if (!secondStr || !*secondStr)                           // no 2nd string
        return g_hooks.queueRequestBuffer28(header, v15, a4, text);

    std::strcpy(reinterpret_cast<char*>(&payload[v15]), secondStr); // append 2nd
    header[kHeMsgFlagsOff] = static_cast<u8>(header[kHeMsgFlagsOff] | kHeMsgFlagSecondStr);
    unsigned v16 = StrLenPlus1(secondStr);
    return g_hooks.queueRequestBuffer28(header, v16 + v15,
                                        static_cast<int>(v16 - 1),
                                        reinterpret_cast<const char*>(payload));
}

// ===========================================================================
// 0x4c5d98 — VIBE_He_SendQuickjumpMessage
// ===========================================================================
int He_SendQuickjumpMessage(i32 a1, i32 a2, u8 flags, const char* text,
                            i32 a5, i32 a6, i32 a7,
                            const char* contactName, const char* secondStr) {
    static thread_local u8 payload[8096];
    u8 header[kHeMsgHeaderBytes];
    std::memset(header, 0, sizeof(header));                  // SetGrayColorThunk(0,0xF8,buf)

    PutByte(header, kHeMsgTypeOff, kHeMsgTypeValue);         // [4]=0x11
    PutDword(header, kHeMsgArg2Off, a2);                     // [12]=a2
    PutByte(header, kHeMsgSubKindOff, kHeMsgSubKindValue);   // [0x36]=9
    PutDword(header, kHeMsgSenderOff, a1);                   // [8]=a1

    if (contactName) {
        if (StrLenPlus1(contactName) <= static_cast<unsigned>(kHeQuickjumpNameCap)) {
            HeWideCopy(&header[kHeMsgContactOff], contactName);  // wide-copy contact name
        } else {
            g_hooks.reportError("he_MessageBoxQuickjump(): contactname is too long!");
        }
    }

    PutDword(header, kHeMsgQjArg5Off, a5);                   // [0x98]=a5 (var_B4)
    PutByte(header, kHeMsgFlagsOff,
            static_cast<u8>(flags | kHeMsgFlagQuickjump));  // [0xD4]= cl|2
    PutDword(header, kHeMsgQjArg6Off, a6);                   // [0x9C]=a6 (var_B0)
    PutDword(header, kHeMsgQjArg7Off, a7);                   // [0xA0]=a7 (var_AC)

    HeWideCopy(payload, text);                               // wide-copy text into v21
    unsigned v15 = StrLenPlus1(text);

    if (!secondStr || !*secondStr)
        return g_hooks.queueRequestBuffer28(header, v15, a7, text);

    std::strcpy(reinterpret_cast<char*>(&payload[v15]), secondStr);
    header[kHeMsgFlagsOff] = static_cast<u8>(header[kHeMsgFlagsOff] | kHeMsgFlagSecondStr);
    unsigned v16 = StrLenPlus1(secondStr);
    return g_hooks.queueRequestBuffer28(header, v16 + v15,
                                        static_cast<int>(v16 - 1),
                                        reinterpret_cast<const char*>(payload));
}

// ===========================================================================
// 0x4c6964 — VIBE_He_AssignIconForHandler
// Icon-name strings (gilde.exe @0x61E774..):
//   he_muenze / he_saege_hammer_gold / he_hammer_gold / he_plus_hammer /
//   he_fernglas / he_ausrufezeichen / he_schlaege ; objects ob_SCHWARZES_BRETT /
//   ob_TRIBUENE. The original passes the resolved record's +97 gfx ptr as the 3rd
//   CreateGfxInfo arg (a char*); we forward it as a void/char* through the hook.
// ===========================================================================
namespace {
const char* const kHeMuenze        = "he_muenze";
const char* const kHeSaegeHammer   = "he_saege_hammer_gold";
const char* const kHeHammerGold    = "he_hammer_gold";
const char* const kHePlusHammer    = "he_plus_hammer";
const char* const kHeFernglas      = "he_fernglas";
const char* const kHeAusrufezeichen = "he_ausrufezeichen";
const char* const kHeSchlaege      = "he_schlaege";
const char* const kObSchwarzesBrett = "ob_SCHWARZES_BRETT";
const char* const kObTribuene      = "ob_TRIBUENE";

// Resolve a building from record+offset and, if it has a +97 gfx ptr, create the
// named icon. Returns 1 on create. Mirrors the common
// "FindById(rec+off); if (b && *(b+97)) CreateGfxInfo(rec, name, *(b+97))" tail.
i32 BuildingIcon(i32 record, int refOff, const char* name) {
    const void* b = g_hooks.buildingFindById(g_hooks.recordDword(record, refOff));
    if (b) {
        i32 gfx = g_hooks.recordDword(AsI32(b), 97);
        if (gfx)
            return g_hooks.createGfxInfo(record, name,
                                         reinterpret_cast<const char*>(static_cast<std::uintptr_t>(gfx)));
    }
    return 0;
}
} // namespace

i32 He_AssignIconForHandler(i32 record, i32 a2) {
    if (!g_hooks.iconsEnabled())                              // if ( !byte_123356B )
        return record;
    if (g_hooks.recordDword(record, 136))                    // record already has gfx ptr
        return record;

    u8 kind = g_hooks.recordByte(record, 0);                 // record[0]

    switch (kind) {
    case 2: {  // +16 building, icon he_plus_hammer (LABEL_14)
        const void* b = g_hooks.buildingFindById(g_hooks.recordDword(record, 16));
        if (b && g_hooks.recordDword(AsI32(b), 97))
            return BuildingIcon(record, 16, kHePlusHammer);
        return record;
    }
    case 4: {  // local-city person notice (+200 set, +188 building) -> he_ausrufezeichen
        if (g_hooks.recordWord(record, 8) == g_hooks.localCityMarker()
            && g_hooks.recordDword(record, 200)) {
            const void* b = g_hooks.buildingFindById(g_hooks.recordDword(record, 188));
            if (b && g_hooks.recordDword(AsI32(b), 97))
                return BuildingIcon(record, 188, kHeAusrufezeichen);
        }
        return record;
    }
    case 0x1A: {  // +208 building, icon he_schlaege
        const void* b = g_hooks.buildingFindById(g_hooks.recordDword(record, 208));
        if (b && g_hooks.recordDword(AsI32(b), 97))
            return BuildingIcon(record, 208, kHeSchlaege);
        return record;
    }
    case 0x1B:  // +172 building, icon he_muenze (note: muenze passes +97 unconditionally)
        if (const void* b = g_hooks.buildingFindById(g_hooks.recordDword(record, 172)))
            return g_hooks.createGfxInfo(record, kHeMuenze,
                        reinterpret_cast<const char*>(static_cast<std::uintptr_t>(
                            g_hooks.recordDword(AsI32(b), 97))));
        return record;
    case 0x1C:  // +172 building, icon he_hammer_gold
        return BuildingIcon(record, 172, kHeHammerGold);
    case 0x1E:  // +172 building, icon he_saege_hammer_gold
        return BuildingIcon(record, 172, kHeSaegeHammer);
    case 0x1F:  // +172 building, icon he_plus_hammer (LABEL_14)
        return BuildingIcon(record, 172, kHePlusHammer);
    case 0x35: {  // person at +188; ob_SCHWARZES_BRETT board -> he_ausrufezeichen
        const void* p = g_hooks.personFindById(g_hooks.recordDword(record, 188));
        if (!p)
            return record;
        u16 marker = g_hooks.recordWord(AsI32(p), 0);
        if (marker != g_hooks.localCityMarker())
            return record;
        if (g_hooks.suppressFlag())                          // dword_649D60
            return record;
        // The original passes the Object_FindByHandle result (the found object
        // handle in a1's low dword) as CreateGfxInfo's 3rd (gfxPtr) argument.
        int obj = g_hooks.objectFindByHandle(kObSchwarzesBrett, a2);
        if (!obj)
            return record;
        return g_hooks.createGfxInfo(record, kHeAusrufezeichen,
                    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(static_cast<u32>(obj))));
    }
    case 0x40: {  // +172 building, he_fernglas (only for the local city)
        const void* b = g_hooks.buildingFindById(g_hooks.recordDword(record, 172));
        if (b) {
            i32 gfx = g_hooks.recordDword(AsI32(b), 97);
            if (gfx) {
                if (g_hooks.recordWord(record, 8) == g_hooks.localCityMarker())
                    return g_hooks.createGfxInfo(record, kHeFernglas,
                                                 reinterpret_cast<const char*>(static_cast<std::uintptr_t>(gfx)));
            }
        }
        return record;
    }
    case 0x6B: {  // tribune (ob_TRIBUENE) -> he_ausrufezeichen, gated by phase 2 + office bit
        if (g_hooks.recordDword(record, 112) != 2)           // phase != 2
            return record;
        if (g_hooks.suppressFlag())
            return record;
        u16 marker = g_hooks.localCityMarker();
        if (g_hooks.personFlagsDword(marker) & 0x20000)      // office-held bit
            return record;
        // As the 0x35 path: forward the Object_FindByHandle result as gfxPtr.
        int obj = g_hooks.objectFindByHandle(kObTribuene, a2);
        if (!obj)
            return record;
        return g_hooks.createGfxInfo(record, kHeAusrufezeichen,
                    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(static_cast<u32>(obj))));
    }
    default:
        // All other kind bytes (3, 5..0x19, 0x1D, 0x20..0x34, 0x36..0x3F, 0x41..0x6A,
        // 0x6C+) fall through with no icon assignment, returning the record unchanged.
        return record;
    }
}

// ===========================================================================
// 0x4c64bc — VIBE_He_ArrangeIconsInCircle
//   Collect every icon slot whose +8 field == parentMesh (up to 32), then place
//   them on a circle. Uses the shared 64-slot icon pool (HeIconPool).
// ===========================================================================
void He_ArrangeIconsInCircle(const float* parentMesh) {
    world::HeIconSlot* pool = world::HeIconPool();

    // Collect matching slots (the original gathers the addresses of slot+0; we keep
    // the slot pointers, which carry the +12 node handle used below).
    world::HeIconSlot* matched[33];
    int count = 0;        // ecx
    const i32 parentKey = AsI32(parentMesh);   // engine compares 32-bit handles
    for (int i = 0; i < world::kHeIconSlotCount && count < 32; ++i) {
        // match on the +8 field (mesh, which actually holds the owning parent ptr).
        if (parentKey == pool[i].mesh) {
            matched[count + 1] = &pool[i];   // v15[++v4] = &slot (1-based as in original)
            ++count;
        }
    }

    float radius = (count == 1) ? 0.0f : kCircleRadius;     // 0 for single, else 100

    // anchor = Transform_PointThroughBoneChain(parentMesh, parentMesh+19, &anchor)
    float anchor[3] = {0, 0, 0};
    g_hooks.transformAnchor(parentMesh, parentMesh + 19, anchor);

    // baseLow = Mesh_ComputeHeightRange(parentMesh).low   (v21[0] -> v18)
    float parentLo = 0.0f, parentHi = 0.0f;
    g_hooks.meshHeightRange(AsI32(parentMesh),
                            &parentLo, &parentHi);
    float baseLow = parentLo;                               // v18 = v21[0]

    if (count <= 0)
        return;

    for (int i = 0; i < count; ++i) {                       // do { ... } while (v6 < count)
        i32 iconMesh = matched[i + 1]->node;               // *(v15[v8+1] + 12) — node handle
        float lo = 0.0f, hi = 0.0f;
        g_hooks.meshHeightRange(iconMesh, &lo, &hi);

        // angle = (double)i * (2*PI) * (1.0 / count)
        double angle = static_cast<double>(i) * kTwoPi * (1.0 / static_cast<double>(count));

        float pos[3];
        pos[0] = static_cast<float>(std::sin(angle) * radius + anchor[0]);
        pos[1] = static_cast<float>((static_cast<double>(hi) - static_cast<double>(lo))
                                    * kHeightMid + baseLow);
        pos[2] = static_cast<float>(radius * std::cos(angle) + anchor[2]);

        i32 iconObj = matched[i + 1]->node;                // v13 = v15[v8] ; obj = *(v13+12)
        g_hooks.objectSetPosition(iconObj, pos);
    }
}

// ===========================================================================
// 0x4c5b40 — VIBE_EventPanel_HandleSlotClick
//   Slot layout (addressed by byte offset off the slot pointer):
//     +0x00  widget ptr (0 == none); *(widget) is the widget kind byte
//     +0x04  pressed-value object id (-1 == none)
//     +0x08  form id (-1 == none)
//   widget +0xF0 (offset 240) is the anchored-flag byte (bit 0x40).
// ===========================================================================
namespace {
inline i32  SlotDword(void* slot, int off) {
    i32 v; std::memcpy(&v, static_cast<u8*>(slot) + off, 4); return v;
}
} // namespace

void* EventPanel_HandleSlotClick(void* newSlot, int a2, int a3, int a4) {
    void* result = newSlot;
    if (g_panelEnabledGate == -1)                            // dword_63226C != -1
        return result;

    // --- release the previously active slot (dword_632270) -----------------
    if (g_activeSlot) {
        i32 prevValueObj = SlotDword(g_activeSlot, 4);      // *(active+4)
        if (prevValueObj != -1)
            g_hooks.objectSetValueOrText(prevValueObj, nullptr, a3, a2, a4);
        g_hooks.formSetObjectsVisible(SlotDword(g_activeSlot, 8), 0); // hide form

        i32 widget = SlotDword(g_activeSlot, 0);            // *(active)
        result = g_activeSlot;
        if (widget) {
            // The original evaluates `(result = *active, *v6==17) && ... ||
            // (result = *active, *active==0x87)` — the `result = *active` (the
            // WIDGET pointer) side-effect lands whenever the widget is non-null,
            // regardless of which branch the inner sync test takes. Reproduce it.
            result = reinterpret_cast<void*>(static_cast<std::uintptr_t>(static_cast<u32>(widget)));
            u8 kind = g_hooks.recordByte(widget, 0);        // *widget
            u8 anchored = g_hooks.recordByte(widget, 240);  // widget[240]
            if ((kind == 17 && (anchored & 0x40) != 0) || kind == 0x87) {
                if ((g_hooks.featureSyncBit() & 4) == 0) {  // (word_63C740 & 4) == 0
                    result = nullptr;
                    g_inputSuppress = 0;                    // dword_62EB4C = 0
                }
            }
        }
        g_activeSlot = nullptr;                             // dword_632270 = 0
    }

    // --- activate the new slot --------------------------------------------
    if (newSlot && SlotDword(newSlot, 8) != -1) {           // newSlot && *(newSlot+8) != -1
        i32 valueObj = SlotDword(newSlot, 4);               // *(newSlot+4)
        if (valueObj != -1)
            g_hooks.objectSetValueOrText(valueObj, reinterpret_cast<const char*>(1), a3, a2, a4);
        g_hooks.formSetObjectsVisible(SlotDword(newSlot, 8), 1);  // show form
        g_hooks.formRaiseWindows(SlotDword(newSlot, 8));    // raise windows
        result = newSlot;

        i32 widget = SlotDword(newSlot, 0);                 // *(newSlot)
        if (widget) {
            u8 kind = g_hooks.recordByte(widget, 0);
            u8 anchored = g_hooks.recordByte(widget, 240);
            result = reinterpret_cast<void*>(static_cast<std::uintptr_t>(static_cast<u32>(widget)));
            if (kind != 17 || (anchored & 0x40) == 0) {
                if (kind != 0x87) {
                    g_activeSlot = newSlot;                 // dword_632270 = newSlot ; return
                    return result;
                }
            }
            if ((g_hooks.featureSyncBit() & 4) == 0)        // (word_63C740 & 4) == 0
                g_inputSuppress = 1;                        // dword_62EB4C = 1
        }
    }
    g_activeSlot = newSlot;                                 // dword_632270 = newSlot
    return result;
}

void  EventPanel_SetEnabled(bool enabled) { g_panelEnabledGate = enabled ? 0 : -1; }
void* EventPanel_ActiveSlot() { return g_activeSlot; }
void  EventPanel_SetActiveSlot(void* slot) { g_activeSlot = slot; }
int   EventPanel_InputSuppressed() { return g_inputSuppress; }
void  EventPanel_Reset() { g_panelEnabledGate = -1; g_activeSlot = nullptr; g_inputSuppress = 0; }

} // namespace guild::sim
