#pragma once
// guild::gui — cutscene PRESENTATION builders (the DATA / widget-build leaves the
// council / trial / office cutscene shells defer):
//
//   VIBE_Cutscene_LoadScene        (gilde.exe 0x4aa234) — load + present a scene.
//   VIBE_Cutscene_BuildSpeechPacket(gilde.exe 0x4abf04) — pack a speech "bubble"
//                                                          command for an entity.
//
// These are the leaves invoked by VIBE_Office_BuildElectionForm /
// _BuildSuccessorDialog* / RunCourtTrial / RunCouncilSession. This module recovers
// the DATA half byte-for-byte: the scene path template, the speech-packet field
// layout (the 8-byte command header + the trailing string blob), and the optional
// voice-clip splice. The actual 3D scene streaming, surface fades, modal frame
// loop and voice playback are forward-declared/stubbed (deferred — see report).
//
// Original prototypes (recovered from the decompiles + call sites):
//   const char* VIBE_Cutscene_LoadScene(scene@eax, mode@ecx, arg@esi, ctx@edi)
//   int VIBE_Cutscene_BuildSpeechPacket(speaker@eax, person@edx, voice@ecx, text@ebx)

#include "guild/common/types.h"

#include <cstring>
#include <string>

namespace guild::gui {

using guild::i32;
using guild::u8;

// ---------------------------------------------------------------------------
// Scene loader (data half).
// ---------------------------------------------------------------------------
// gilde.exe 0x4aa234 builds the scene resource path "scenes/*%s" from the scene
// base name and hands it to VIBE_Scene_LoadFromStream, then runs the present /
// fade pipeline (deferred). The fade-out registered on exit uses palette "BLACK"
// over 90 steps with delay 10. We recover the path template + the BLACK fade
// constants; the streaming/present/fade engine is stubbed.
inline constexpr const char* kSceneNameTemplate = "scenes/*%s";  // aScenesS @0x61d778
inline constexpr const char* kSceneFadePalette  = "BLACK";       // aBlack   @0x61d734
inline constexpr int kSceneFadeSteps = 90;                       // VIBE_Fade_Register arg
inline constexpr int kSceneFadeDelay = 10;                       // VIBE_Fade_Register arg

// gilde.exe 0x4aa234 (data half) — format the scene resource path for `sceneName`.
// Mirrors VIBE_Crt_Sprintf_0(buf, "scenes/*%s", sceneName).
std::string Cutscene_SceneResourcePath(const char* sceneName);

// Deferred scene-engine leaves (forward-declared). The default is a no-op; a test
// installs a recording backend. `VIBE_Scene_LoadFromStream`, the surface present
// (`VIBE_Result_Handler_Interaction`), the season sky refresh and the BLACK fade
// (`VIBE_Fade_Register`) are not part of this module.
struct CutsceneSceneLeaves {
    void (*loadStream)(const char* path, void* ctx) = nullptr;
    void (*present)(void* ctx) = nullptr;
    void (*fadeBlack)(const char* palette, int steps, int delay, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// gilde.exe 0x4aa234 — load + present a scene. `reentryGuard` mirrors dword_6315BC:
// when nonzero the original returns immediately without doing anything. Returns
// true if the scene pipeline ran (guard clear).
bool Cutscene_LoadScene(const char* sceneName, const CutsceneSceneLeaves& leaves,
                        bool reentryGuard = false);

// ---------------------------------------------------------------------------
// Speech packet (VIBE_Cutscene_BuildSpeechPacket, gilde.exe 0x4abf04).
// ---------------------------------------------------------------------------
// The original builds an 8-byte command header `v21[8]` then a tail of ints and
// hands header+payload to VIBE_Command_QueueRequestBuffer28. Recovered field
// layout (byte offsets into the request struct it stacks, ebp-108h base v21..):
//
//   +0   (4)  color/style handle from VIBE_Light_SetGrayColorThunk(0, 248, &v21)
//   +4   (1)  packet kind tag        = 17   (v21[4] = 17)
//   +8   (4)  speaker handle         = *(speaker+4)  (v22 = entity handle word)
//   +12  (4)  reply-to / parent      = -1            (v23)
//   +0x36(1)  flags byte             = 12, or 14 if (person[5]!=-1 && person.flag&0x10),
//             then |= 0x10 when a voice clip string is appended  (v29)
//   +0x58(4)  building / room id     = person[5]     (v26)
//   +0x5C(4)  storable-object id     = -1, else resolved object id (v27)
//   +0x60(4)  message id             = 1418          (v25)  [HE message channel]
//   +0x64(4)  speaker text id        = *person       (v28, the entity's text id)
//   +0x6C(1)  priority               = 9             (v24)
//
// The text blob is the speaker's UTF-16-as-bytes string (copied 2 bytes at a time
// until a 0,0 terminator), and if a non-empty voice clip name `voice` is given it
// is strcpy'd right after the text (offset = strlen(text)+1) and the flags byte
// gets bit 0x10. The QueueRequestBuffer28 length args are recovered exactly.
//
// We model the packet as a struct with the recovered field offsets/values plus the
// produced payload bytes, so the layout is verifiable byte-for-byte. The HE-message
// dispatch (QueueRequestBuffer28) is routed through a settable command hook (mock).

inline constexpr int  kSpeechHeaderKind = 17;    // v21[4]
inline constexpr i32  kSpeechReplyTo    = -1;    // v23
inline constexpr i32  kSpeechMsgId      = 1418;  // v25  (HE message channel)
inline constexpr u8   kSpeechPriority   = 9;     // v24
inline constexpr u8   kSpeechFlagsNoVoice = 12;  // v29 base when no clip / no room voice
inline constexpr u8   kSpeechFlagsVoice   = 14;  // v29 base when person has room voice
inline constexpr u8   kSpeechFlagAttach   = 0x10;// v29 |= when a voice-clip name appended
inline constexpr int  kSpeechGrayLevel    = 248; // VIBE_Light_SetGrayColorThunk arg

// One speaker the packet describes. `roomId` mirrors person[5] (a building/room id,
// -1 = none); `roomVoiceBit` mirrors (person.flag@+38 & 0x10) (the speaker uses a
// room-localized voice). `textId` mirrors *person (the entity's localized text id).
// `speakerHandle` mirrors *(speaker+4) (the speaking entity's handle).
struct SpeechSpeaker {
    i32 speakerHandle = 0;  // *(speaker+4)
    i32 textId        = 0;  // *person  (v28)
    i32 roomId        = -1; // person[5] (v26)
    bool roomVoiceBit = false; // (person.flag & 0x10) != 0
    i32 resolvedObjId = -1; // VIBE_Building_FindStorableObject/QueryFind result (v27)
};

// The fully-built packet: the recovered header fields + the produced payload bytes
// and the length args passed to QueueRequestBuffer28.
struct SpeechPacket {
    u8  kind      = kSpeechHeaderKind; // +4
    i32 speaker   = 0;                 // +8
    i32 replyTo   = kSpeechReplyTo;    // +12
    u8  flags     = kSpeechFlagsNoVoice; // +0x36
    i32 roomId    = -1;                // +0x58
    i32 objId     = -1;                // +0x5C
    i32 msgId     = kSpeechMsgId;      // +0x60
    i32 textId    = 0;                 // +0x64
    u8  priority  = kSpeechPriority;   // +0x6C

    std::string payload;   // the text blob (+ optional voice-clip tail)
    int totalLen = 0;      // QueueRequestBuffer28 length arg
    int voiceLen = 0;      // QueueRequestBuffer28 "clip length - 1" arg
};

// Deferred command-queue leaf (mock). The original ends in
// VIBE_Command_QueueRequestBuffer28(header, totalLen, voiceLen, payload).
using SpeechQueueHook = void (*)(const SpeechPacket& pkt, void* ctx);
void Cutscene_SetSpeechQueueHook(SpeechQueueHook hook, void* ctx);

// gilde.exe 0x4abf04 — build the speech packet for `speaker` describing `who`,
// with display `text` and an optional `voice` clip name (nullptr/"" => no clip).
// Returns the built packet (and fires the queue hook with it). `text` is the
// already-rendered display string (the caller built it with RenderFormattedMessage).
SpeechPacket Cutscene_BuildSpeechPacket(const SpeechSpeaker& who,
                                        const char* text,
                                        const char* voice = nullptr);

} // namespace guild::gui
