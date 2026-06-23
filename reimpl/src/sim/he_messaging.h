#pragma once
// "He" (history/event) entity-handler messaging + icon cluster — gilde.exe.
//
// This module reconstructs the five entry-reachable (frame loop 0x4c09a0) "He"
// functions that build the event-message command packets, lay out the floating
// event-icons in a circle over an entity, and drive the event-panel slot toggle:
//
//   0x4c5c54  VIBE_He_SendEntityMessage      — build & queue a "type-17" message
//             packet addressed to a person of kind 6/7 (with optional 2nd string).
//   0x4c5d98  VIBE_He_SendQuickjumpMessage   — the richer "quickjump" variant
//             (extra contact-name + 3 dword fields + the 0x02 quickjump flag).
//   0x4c6964  VIBE_He_AssignIconForHandler   — dispatch a He record's kind byte to
//             the right event-icon GFX (he_hammer_gold / he_muenze / …).
//   0x4c64bc  VIBE_He_ArrangeIconsInCircle   — re-layout every icon owned by an
//             entity evenly around a circle (the trig layout).
//   0x4c5b40  VIBE_EventPanel_HandleSlotClick— toggle the active event-panel slot
//             (hide the old slot's form, show & raise the new one).
//
// The messaging packet body, the Person/Building/Object record lookups, the form/
// object widget leaves, the icon-mesh build and the universe-node teardown are all
// cross-module engine boundaries; they are routed through an installable
// HeMessagingHooks table (inert defaults in the .cpp), exactly like the existing
// WorldHistory2Hooks / CourtCouncilHooks pattern, so the module runs deterministic
// and headless. The icon pool itself is the SAME 64-slot pool reconstructed in
// world/world_history2.cpp (HeIconSlot); ArrangeIconsInCircle is wired against it.
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// The event-message command packet body (gilde.exe local frame zero-filled to
// 248 bytes by VIBE_Light_SetGrayColorThunk(0, 0xF8, buf), then handed to
// VIBE_Command_QueueRequestBuffer28). Offsets are byte-for-byte from the two
// message builders' stack stores. The trailing payload (text strings) is appended
// AFTER the 248-byte header by the caller; the header is exactly 248 bytes.
//   +0x04  type marker byte (always 0x11 == 17)
//   +0x08  senderId / recipientId dword (a1)
//   +0x0C  a2 dword
//   +0x36  sub-kind byte (always 9)
//   +0x68  contact-name wide-copy (quickjump only; 0x30 char cap)
//   +0x58  payload-length / a-arg dword (var_AC entity / var_B4 quickjump a5)
//   +0x5C..+0x60 quickjump extra dwords (a6,a7)
//   +0x9C  flags byte (cl arg; |0x02 for quickjump, |0x10 when a 2nd string follows)
// NOTE: offsets verified against the IDA stack frames (var_104 base @0x1fa0 for the
//   entity builder, var_10C base @0x1fa0 for the quickjump builder). The previous
//   0x98/0x9C/0xA0/0xD4 literals were WRONG; the real frame slots are var_AC/var_B4
//   @0x1ff8 (=+0x58), var_B0 @0x1ffc (=+0x5C), var_AC @0x2000 (=+0x60), var_68/var_70
//   @0x203c (=+0x9C). See progress/harden/sim_06_he.md.
// ---------------------------------------------------------------------------
inline constexpr int kHeMsgHeaderBytes   = 248;  // 0xF8 — VIBE_Light_SetGrayColorThunk fill
inline constexpr int kHeMsgTypeOff       = 4;
inline constexpr int kHeMsgSenderOff     = 8;
inline constexpr int kHeMsgArg2Off       = 12;
inline constexpr int kHeMsgSubKindOff    = 0x36;
inline constexpr int kHeMsgContactOff    = 0x68;  // quickjump contact-name buffer
inline constexpr int kHeMsgPayLenOff     = 0x58;  // var_AC (entity) / var_B4 (quickjump a5)
inline constexpr int kHeMsgQjArg5Off     = 0x58;  // var_B4 a5
inline constexpr int kHeMsgQjArg6Off     = 0x5C;  // var_B0 a6
inline constexpr int kHeMsgQjArg7Off     = 0x60;  // var_AC a7
inline constexpr int kHeMsgFlagsOff      = 0x9C;  // var_68 / var_70
inline constexpr int kHeMsgTypeValue     = 0x11;  // 17
inline constexpr int kHeMsgSubKindValue  = 9;
inline constexpr u8  kHeMsgFlagQuickjump = 0x02;
inline constexpr u8  kHeMsgFlagSecondStr = 0x10;
inline constexpr int kHeQuickjumpNameCap = 0x30;  // contactname-too-long threshold

// ---------------------------------------------------------------------------
// Installable cross-module leaves. Inert defaults (in the .cpp) make the module
// run with no live sim/render/universe/command-queue.
// ---------------------------------------------------------------------------
struct HeMessagingHooks {
    // VIBE_Person_FindRecordById @0x58bc6c — id -> person record base (or null).
    // SendEntityMessage uses it to gate on the +2 kind byte (6 or 7).
    const void* (*personFindById)(i32 id) = nullptr;
    // Read the person record's +2 kind/class byte (byte_12CE912). Default 0.
    u8 (*personKindByte)(const void* rec) = nullptr;

    // VIBE_Command_QueueRequestBuffer28 @0x494910 — submit the assembled message.
    // `header` is the 248-byte+payload buffer, `totalLen` the header+payload size,
    // `secondArg` the original edx (payload length minus terminator), `payload`
    // the text source. Returns the enqueue result (or -1). Default returns 0.
    int (*queueRequestBuffer28)(const u8* header, unsigned totalLen,
                                int secondArg, const char* payload) = nullptr;

    // VIBE_ErrorLog_ReportMessage @0x438da8 — diagnostic sink (contactname too
    // long). Default: drop.
    void (*reportError)(const char* text) = nullptr;

    // --- AssignIconForHandler entity resolvers ----------------------------
    // VIBE_Building_FindById @0x587b20 — building id -> record base (or null).
    const void* (*buildingFindById)(i32 id) = nullptr;
    // VIBE_Object_FindByHandle @0x5b7be4 — locate a named object (e.g. the
    // ob_SCHWARZES_BRETT / ob_TRIBUENE notice board). Non-zero on success.
    int (*objectFindByHandle)(const char* name, i32 parentEntity) = nullptr;
    // VIBE_He_CreateGfxInfo @0x4c67e0 — bind an icon GFX to the parent record.
    // `iconName` is one of the he_* strings. Default returns 0.
    int (*createGfxInfo)(i32 record, const char* iconName, const char* gfxPtr) = nullptr;

    // Read a dword at record+offset (the He record's render/entity-ref fields).
    i32 (*recordDword)(i32 record, int offset) = nullptr;
    // Read a byte at record+offset (record[0] kind, record+97 gfx-ptr presence).
    u8 (*recordByte)(i32 record, int offset) = nullptr;
    // Read a word at record+offset (record+8 city index, resolved record +marker).
    u16 (*recordWord)(i32 record, int offset) = nullptr;

    // The "icons enabled" gate (byte_123356B). Non-zero == enabled. Default 1.
    int (*iconsEnabled)() = nullptr;
    // word_63CC5C — the local-player city/marker. Default 0.
    u16 (*localCityMarker)() = nullptr;
    // dword_649D60 — the "in cutscene / suppress icons" flag. Default 0.
    i32 (*suppressFlag)() = nullptr;
    // dword_12CEAD8[134*marker] & 0x20000 — the per-person "office held" bit used by
    // the kind-0x6B (tribune) path. Default returns 0.
    i32 (*personFlagsDword)(u16 marker) = nullptr;

    // --- ArrangeIconsInCircle geometry leaves -----------------------------
    // VIBE_Transform_PointThroughBoneChain @0x5c8b38 — compute the parent's anchor
    // point (writes a float[3] at `outXyz`). Default: zeros.
    void (*transformAnchor)(const float* parentMesh, const float* boneEnd,
                            float* outXyz) = nullptr;
    // VIBE_Mesh_ComputeHeightRange @0x42698c — writes [min,max] floats for a mesh
    // (out0=low, out1=high; the function uses out1-out0). Default: zeros.
    void (*meshHeightRange)(i32 mesh, float* outLow, float* outHigh) = nullptr;
    // VIBE_Object_SetPosition @0x5af38c — place an icon object at xyz (float[3]).
    void (*objectSetPosition)(i32 obj, const float* xyz) = nullptr;

    // --- EventPanel_HandleSlotClick form/object leaves --------------------
    // VIBE_Object_SetValueOrText @0x41dfec — set a widget's pressed value/text.
    void (*objectSetValueOrText)(i32 obj, const char* val, int a, int b, int c) = nullptr;
    // VIBE_Form_SetObjectsVisible @0x41d634 — show/hide a form's widgets.
    void (*formSetObjectsVisible)(i32 form, int visible) = nullptr;
    // VIBE_Form_RaiseWindows @0x41be6c — raise a form's windows to front.
    void (*formRaiseWindows)(i32 form) = nullptr;
    // word_63C740 & 4 — the "networked / sync" feature bit. Default 0.
    int (*featureSyncBit)() = nullptr;
};

void SetHeMessagingHooks(const HeMessagingHooks* hooks);
const HeMessagingHooks& GetHeMessagingHooks();

// ===========================================================================
// gilde.exe 0x4c5c54 — VIBE_He_SendEntityMessage
//   (__userpurge, eax=recipientId, edx=a2, ebx=text, a4 (stack), a5 (stack)=2nd str)
// Look up the recipient person; only kind 6/7 entities accept the message. Build
// the 248-byte type-17 packet (wide-copying `text`, appending `secondStr` when
// present and setting the 0x10 flag), and submit it via QueueRequestBuffer28.
// Returns -1 if the recipient is missing or not kind 6/7, else the queue result.
// ===========================================================================
int He_SendEntityMessage(i32 recipientId, i32 a2, const char* text,
                         i32 a4, const char* secondStr);

// ===========================================================================
// gilde.exe 0x4c5d98 — VIBE_He_SendQuickjumpMessage
//   (__userpurge, eax=a1, edx=a2, cl=flags, ebx=text, stack: a5,a6,a7, contactName,
//    secondStr). Builds the richer "quickjump" packet: the type-17 header plus the
//   contact-name wide-copy (length-checked against 0x30), the a5/a6/a7 dwords, the
//   0x02 quickjump flag (always OR'd into the flags byte), the wide-copied text and
//   the optional second string (0x10 flag). Submits via QueueRequestBuffer28.
// ===========================================================================
int He_SendQuickjumpMessage(i32 a1, i32 a2, u8 flags, const char* text,
                            i32 a5, i32 a6, i32 a7,
                            const char* contactName, const char* secondStr);

// ===========================================================================
// gilde.exe 0x4c6964 — VIBE_He_AssignIconForHandler  (__usercall, eax=record, edi=a2)
// Dispatch the He record's kind byte (record[0]) to the appropriate floating
// event-icon GFX, after resolving the referenced Building / Person / Object. No-op
// when icons are disabled (byte_123356B == 0) or the record already has a gfx ptr
// (record+136 != 0). Returns the (low byte of the) result, faithful to the original
// char return; callers ignore it.
i32 He_AssignIconForHandler(i32 record, i32 a2);

// ===========================================================================
// gilde.exe 0x4c64bc — VIBE_He_ArrangeIconsInCircle  (__usercall, eax=parentMesh)
// Re-layout every icon slot whose +8 field (the owning parent ptr) equals the
// given parentMesh evenly around a circle of radius 100 (radius 0 when a single
// icon), at a height proportional to each icon's own height range. Uses the icon
// pool reconstructed in world/world_history2.cpp.
//   angle_i = (2*PI) * i / N ; x = sin(angle)*R + anchorX
//   y = (hi-lo)*0.5 + anchorLow ; z = cos(angle)*R + anchorZ
// `parentMesh` is the float* the original passes in eax. The geometry leaves route
// through HeMessagingHooks.
void He_ArrangeIconsInCircle(const float* parentMesh);

// ===========================================================================
// gilde.exe 0x4c5b40 — VIBE_EventPanel_HandleSlotClick
//   (__usercall, eax=newSlot, edx=a2, ebx=a3, esi=a4)
// Toggle the active event-panel slot. When the panel is enabled (dword_63226C !=
// -1): release the previously active slot (set its widget value to 0, hide its
// form, and — if it is a kind-17 anchored widget (+0xF0 & 0x40) or a kind-0x87 — and
// the sync bit is clear, clear the input-suppress flag), then activate `newSlot`
// (set its value to 1, show & raise its form), recording it as active. The active-
// slot pointer and the input-suppress flag are this module's state, byte-faithful
// to dword_632270 / dword_62EB4C.
// `newSlot` may be null. Slots are addressed by byte offset via HeMessagingHooks.
// ===========================================================================
struct EventPanelSlot;   // opaque; addressed by byte offset (see .cpp)
void* EventPanel_HandleSlotClick(void* newSlot, int a2, int a3, int a4);

// Event-panel module state (mirrors the engine globals) — exposed for tests/wiring.
void  EventPanel_SetEnabled(bool enabled);   // dword_63226C (-1 == disabled)
void* EventPanel_ActiveSlot();               // dword_632270
void  EventPanel_SetActiveSlot(void* slot);  // dword_632270
int   EventPanel_InputSuppressed();          // dword_62EB4C
void  EventPanel_Reset();                    // test helper

} // namespace guild::sim
