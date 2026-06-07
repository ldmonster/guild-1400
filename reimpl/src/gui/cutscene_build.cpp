#include "gui/cutscene_build.h"

#include <cstdio>

// Faithful 1:1 port of the cutscene PRESENTATION builders' data half:
//   VIBE_Cutscene_LoadScene         (gilde.exe 0x4aa234)
//   VIBE_Cutscene_BuildSpeechPacket (gilde.exe 0x4abf04)
// The scene streaming/present/fade engine and the command-queue dispatch are
// deferred to forward-declared/stubbed leaves (see the module report).

namespace guild::gui {

namespace {
SpeechQueueHook g_speechHook = nullptr;
void*           g_speechCtx  = nullptr;
} // namespace

void Cutscene_SetSpeechQueueHook(SpeechQueueHook hook, void* ctx) {
    g_speechHook = hook;
    g_speechCtx  = ctx;
}

// gilde.exe 0x4aa234 (data half) — VIBE_Crt_Sprintf_0(v19, "scenes/*%s", scene).
std::string Cutscene_SceneResourcePath(const char* sceneName) {
    char buf[140]; // matches the original v19[140] stack buffer
    std::snprintf(buf, sizeof(buf), kSceneNameTemplate, sceneName ? sceneName : "");
    return std::string(buf);
}

// gilde.exe 0x4aa234 — VIBE_Cutscene_LoadScene.
//   if ( !dword_6315BC ) { ...switch active slot..., LoadFromStream(path),
//        ...present/fade pipeline..., register BLACK fade }
// We reproduce the guard + the scene-stream load + the present/fade leaf calls;
// the 3D slot management, surface compositing and season sky refresh are deferred.
bool Cutscene_LoadScene(const char* sceneName, const CutsceneSceneLeaves& leaves,
                        bool reentryGuard) {
    if (reentryGuard)            // dword_6315BC != 0 -> early return
        return false;

    std::string path = Cutscene_SceneResourcePath(sceneName);
    if (leaves.loadStream)
        leaves.loadStream(path.c_str(), leaves.ctx);   // VIBE_Scene_LoadFromStream
    if (leaves.present)
        leaves.present(leaves.ctx);                    // present pipeline (deferred)
    if (leaves.fadeBlack)
        leaves.fadeBlack(kSceneFadePalette, kSceneFadeSteps,
                         kSceneFadeDelay, leaves.ctx);  // VIBE_Fade_Register BLACK
    return true;
}

// gilde.exe 0x4abf04 — VIBE_Cutscene_BuildSpeechPacket.
SpeechPacket Cutscene_BuildSpeechPacket(const SpeechSpeaker& who,
                                        const char* text,
                                        const char* voice) {
    SpeechPacket pkt{};

    // --- Header (the v21..v28 stack fields, recovered offsets) --------------
    // VIBE_Light_SetGrayColorThunk(0, 248, &v21) seeds the +0 color handle.
    pkt.kind    = kSpeechHeaderKind;        // v21[4] = 17
    pkt.speaker = who.speakerHandle;        // v22 = *(speaker+4)
    pkt.replyTo = kSpeechReplyTo;           // v23 = -1
    pkt.msgId   = kSpeechMsgId;             // v25 = 1418
    pkt.priority= kSpeechPriority;          // v24 = 9

    // v29: 12 normally; 14 when the speaker is room-bound and uses a room voice.
    //   if ( a2[5] == -1 || (person.flag & 0x10) == 0 )  v29 = 12;  else v29 = 14;
    if (who.roomId == -1 || !who.roomVoiceBit)
        pkt.flags = kSpeechFlagsNoVoice;    // 12
    else
        pkt.flags = kSpeechFlagsVoice;      // 14

    pkt.roomId = who.roomId;                // v26 = a2[5]
    pkt.textId = who.textId;                // v28 = *a2
    pkt.objId  = -1;                        // v27 = -1 (default)

    // When the speaker is bound to a room/building, resolve the storable object id
    // (VIBE_Building_FindStorableObject / QueryFind). The lookup itself belongs to
    // the building cluster; the caller supplies the resolved id (v27).
    if (who.roomId != -1 && who.resolvedObjId != -1)
        pkt.objId = who.resolvedObjId;      // v27 = *(v11+1)

    // --- Payload (the v20[2048] blob) ---------------------------------------
    // The original copies the display string two bytes at a time until a 0,0 pair
    // (UTF-16-as-bytes). For our byte-string model the produced blob is the text
    // up to its NUL; strlen(v20)+1 is the inclusive byte length (v16).
    const char* t = text ? text : "";
    pkt.payload.assign(t);
    int v16 = static_cast<int>(pkt.payload.size()) + 1;   // strlen(v20) + 1

    if (voice && *voice) {
        // strcpy(&v20[v16], voice);  v29 |= 0x10;  v17 = strlen(voice)+1;
        // QueueRequestBuffer28(v21, v17 + v16, v17 - 1, v20).
        pkt.payload.push_back('\0');           // the v16 NUL separating text/voice
        pkt.payload.append(voice);
        pkt.flags = static_cast<u8>(pkt.flags | kSpeechFlagAttach); // |= 0x10
        int v17     = static_cast<int>(std::strlen(voice)) + 1;
        pkt.totalLen = v17 + v16;
        pkt.voiceLen = v17 - 1;
    } else {
        // QueueRequestBuffer28(v21, v19, v19 - 1, v30) with v19 = strlen(text)+1.
        int v19      = static_cast<int>(std::strlen(t)) + 1;
        pkt.totalLen = v19;
        pkt.voiceLen = v19 - 1;
    }

    if (g_speechHook)
        g_speechHook(pkt, g_speechCtx);  // VIBE_Command_QueueRequestBuffer28
    return pkt;
}

} // namespace guild::gui
