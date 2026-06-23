// =====================================================================================
// audio_recon_dispatch.cpp — 1:1 reconstruction of the gilde.exe engine audio glue:
// script-command dispatchers (VIBE_Sound_CmdPlaySample*), the music/ambient track
// control entry points (VIBE_Audio_StopActiveTrack / PlayAmbientTrack / UpdateActiveVoice),
// VIBE_Sound3d_BindHandle, VIBE_Audio_InitDefaultMixer and VIBE_SampleBank_Create.
//
// These are the thin control-flow layer that sits above the bookkeeping in
// audio_recon_engine.cpp. Out-of-cluster / MSS leaves are reached via AudioEngineHooks.
// =====================================================================================
#include "audio/audio_recon_engine.h"

namespace guild::audio::recon {

// ------------------------------------------------------------------------------------
// gilde.exe 0x440d28 — VIBE_Sound_CmdPlaySample (__usercall eax=&name, edx=&loop, ecx=kind)
// Resolve+prime a voice for the named sample, set its loop count, start it.
// (Original takes *a1 = sample name id, *a2 = loop count, a3 = kind.)
// ------------------------------------------------------------------------------------
Addr AudioEngine::cmdPlaySample(int kind, Addr name, int* loopOut) {
    Addr v = hooks.playSample ? hooks.playSample(kind, name) : 0; // VIBE_Sound_PlaySample(a3,*a1)
    if (!v) return 0;
    setVoiceLoopCount(v, loopOut ? *loopOut : 0);                 // VIBE_Audio_SetVoiceLoopCount
    if (hooks.playVoiceSample) hooks.playVoiceSample(v, 0);       // VIBE_Audio_PlayVoiceSample
    return v;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x440d5c — VIBE_Sound_CmdPlaySample3D
//   a1=&pos, a2=&vol, a3=&mode, a4=&name. If *mode==1 -> positional one-shot (baseVol 60).
//   Otherwise set loop count = *mode and start as a normal voice.
// ------------------------------------------------------------------------------------
Addr AudioEngine::cmdPlaySample3D(int* pos, int* vol, int* mode, Addr nameRef) {
    Addr v = hooks.playSample
               ? hooks.playSample(mode ? *mode : 0, nameRef ? getPtr(nameRef, 0) : 0) : 0;
    if (!v) return v;
    if (mode && *mode == 1) {
        float p = (float)(vol ? *vol : 0);
        if (hooks.sound3dPlayOneShot)
            hooks.sound3dPlayOneShot(v, pos ? (Addr)*pos : 0, 60, p);
        return v;
    }
    setVoiceLoopCount(v, mode ? *mode : 0);
    if (hooks.playVoiceSample) hooks.playVoiceSample(v, 0);
    return v;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x440db0 — VIBE_Sound_CmdStopSample (__usercall eax=&voice)
// ------------------------------------------------------------------------------------
int AudioEngine::cmdStopSample(Addr voiceRef) {
    if (hooks.stopVoice) hooks.stopVoice(voiceRef ? getPtr(voiceRef, 0) : 0, 0);
    return 0;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x424bd4 — VIBE_Sound3d_BindHandle (__usercall eax=entry, ebx=loop, edx=a3)
// Attach a loop count to an entry's bound voice (+52 == result[13]); resume or start it.
// Entry layout (dwords): [13]=+52 boundVoice, [14]=+56 name, [15]=+60 dirty, [17]=+68
// loopCount mirror, [19]=+76, [20]=+80 hasPlayedFlag.
// The two missing-handle error sprintf paths are diagnostics; reproduced as early-out.
// ------------------------------------------------------------------------------------
Addr AudioEngine::sound3dBindHandle(Addr entry, int loop, int a3) {
    Addr result = entry;
    if (!result) return result;
    Addr v6 = addrAt(result, 52);                 // result[13]
    if (!v6) return result;
    if (!getPtr(v6, 4)) {                         // *(v6+4) == 0 -> diagnostic, no play
        return result;                            // (original sprintf's an error string)
    }
    i32at(v6, 12) = loop;                         // *(v6+12) = a2
    i32at(result, 68) = loop;                     // v4[17] = a2
    setVoiceLoopCount(v6, loop);                  // VIBE_Audio_SetVoiceLoopCount
    int v9 = i32at(result, 80);                   // v4[20]
    i32at(result, 76) = 0;                        // v4[19] = v8 (== 0 here)
    if (v9) {
        result = restartVoice(addrAt(result, 52));      // VIBE_Audio_RestartVoice(v4[13])
        i32at(entry, 60) = 0;                            // v4[15] = 0
    } else {
        if (hooks.playVoiceSample) hooks.playVoiceSample(addrAt(entry, 52), a3);
        i32at(entry, 60) = 0;                            // v4[15] = 0
        result = entry;
    }
    return result;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x43a2b4 — VIBE_Audio_StopActiveTrack (__fastcall)
// Find the active streaming track slot under the lock and stop it.
// (Win32 CRITICAL_SECTION -> SDL/no-op per project boundary; bookkeeping preserved.)
// ------------------------------------------------------------------------------------
void AudioEngine::stopActiveTrack(int a2) {
    if (st.trackSysEnabled) {
        Addr slot = hooks.findActiveTrackSlot ? hooks.findActiveTrackSlot() : 0;
        if (slot && hooks.stopTrack) hooks.stopTrack(slot, a2);
    }
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x439d30 — VIBE_Sound_UpdateActiveVoice (__fastcall this, vol)
// Push a new volume to the active track slot.
// ------------------------------------------------------------------------------------
void AudioEngine::updateActiveVoice(int vol) {
    if (st.trackSysEnabled) {
        Addr slot = hooks.findActiveTrackSlot ? hooks.findActiveTrackSlot() : 0;
        if (slot) setVoicePosition(slot, vol);
    }
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x43a984 — VIBE_Audio_PlayAmbientTrack (__usercall eax=name, edx=vol)
// Reuse the active slot or grab a free one; copy the name into a free slot, close any
// prior stream, mark it as music (+262=1), start the track and set its volume.
// ------------------------------------------------------------------------------------
Addr AudioEngine::playAmbientTrack(const char* name, int vol) {
    if (!st.ambientEnabled) return AudioEngine::unkFallbackAddr();
    Addr slot = hooks.findActiveTrackSlot ? hooks.findActiveTrackSlot() : 0;
    if (!slot) {
        Addr fresh = hooks.findFreeVoiceSlot ? hooks.findFreeVoiceSlot() : 0;
        slot = fresh;
        if (!fresh) return 0;
        // VIBE_Light_SetGrayColorThunk(0,296,slot) zero-inits the 296-byte slot record:
        std::memset(reinterpret_cast<void*>(fresh), 0, 296);
        // copy the name (the original copies 2 bytes/iter until a NUL, i.e. a C string)
        std::strcpy(reinterpret_cast<char*>(fresh), name);
    }
    int stream = i32at(slot, 256);
    u8at(slot, 262) = 1;                           // mark as music
    if (stream) {
        if (hooks.closeStream) hooks.closeStream(stream);
        i32at(slot, 256) = 0;
    }
    if (hooks.startTrack && hooks.startTrack(slot, 1) == 0) {
        setVoicePosition(slot, vol);
        return slot;
    }
    return 0;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x449400 — VIBE_Audio_InitDefaultMixer  (-> VIBE_Sound_LibInit(0,3,2,44100))
// ------------------------------------------------------------------------------------
int AudioEngine::initDefaultMixer() {
    return hooks.soundLibInit ? hooks.soundLibInit(0, 3, 2, 44100) : 0;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x447920 — VIBE_SampleBank_Create (__usercall eax=name)
// Destroy any prior pending bank, allocate a fresh 64-byte bank header, zero it, copy
// the (<=50) name in, bump the bank counter. Returns 0 on success, -1 on error.
// (The original's memset is an inlined 64-byte clear; reproduced as a plain clear.)
// ------------------------------------------------------------------------------------
int AudioEngine::sampleBankCreate(const char* name) {
    if (hooks.sampleBankDestroy) hooks.sampleBankDestroy();   // VIBE_SampleBank_Destroy
    if (st.pendingBank) return -1;                            // dword_62E8E8 != 0
    if (!name) return -1;
    Addr blk = hooks.memAlloc ? hooks.memAlloc(0x40, "sbs:CreateSB") : 0;
    st.pendingBank = blk;
    if (!blk) return -1;
    std::memset(reinterpret_cast<void*>(blk), 0, 64);
    if (hooks.strNCopyPad) hooks.strNCopyPad(st.pendingBank, name, 50);
    ++st.bankSeq;                                             // ++dword_62E8EC
    return 0;
}

} // namespace guild::audio::recon
