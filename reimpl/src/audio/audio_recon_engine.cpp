// =====================================================================================
// audio_recon_engine.cpp — 1:1 reconstruction of the gilde.exe engine audio bookkeeping.
// See audio_recon_engine.h for record layouts, provenance and the MSS/SDL hook boundary.
//
// Translation notes on Hex-Rays artifacts: the decompile leaves several scratch regs
// (v2/v3/v6/v7/v14 ...) flagged "uninitialized" because IDA could not name the address
// already held in ecx/edx/esi from the preceding loop iteration. In every such case the
// register demonstrably aliases the record pointer just computed (the loop cursor or the
// just-allocated channel); the reconstruction names that alias explicitly. No behaviour
// is changed — the same offset is read/written on the same record.
// =====================================================================================
#include "audio/audio_recon_engine.h"

namespace guild::audio::recon {

// Sentinel returned by several functions when the lib is not live (the original returns
// pointers to fixed read-only "unk_767xx" blobs that callers treat as opaque non-null).
// We funnel them to one stable sentinel address; callers in this cluster only test for
// the live path, never dereference these.
static const char kUnkSentinel[64] = {0};
const char* AudioEngine::unkFallback() { return kUnkSentinel; }
static Addr kSentinelAddr() { return reinterpret_cast<Addr>(kUnkSentinel); }
Addr AudioEngine::unkFallbackAddr() { return reinterpret_cast<Addr>(kUnkSentinel); }

// Pointer-field side storage (64-bit-safe links over the original 4-byte offsets).
Addr AudioEngine::getPtr(Addr base, int off) const {
    auto it = ptrFields_.find({base, off});
    return it == ptrFields_.end() ? 0 : it->second;
}
void AudioEngine::setPtr(Addr base, int off, Addr val) {
    if (val) ptrFields_[{base, off}] = val;
    else     ptrFields_.erase({base, off});
}

// ------------------------------------------------------------------------------------
// VIBE_Util_StrCmpNoCaseN @0x5e0db0 (modelled): case-insensitive, n-bounded.
// ------------------------------------------------------------------------------------
int vibeStrCmpNoCaseN(const char* a, const char* b, int n) {
    for (int i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        unsigned char la = (ca >= 'A' && ca <= 'Z') ? (ca + 32) : ca;
        unsigned char lb = (cb >= 'A' && cb <= 'Z') ? (cb + 32) : cb;
        if (la != lb) return (int)la - (int)lb;
        if (ca == 0) return 0;
    }
    return 0;
}

int vibeStrCmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x445ef8 — VIBE_Sound_SetMasterEnable (__usercall, eax)
// ------------------------------------------------------------------------------------
int AudioEngine::setMasterEnable(int value) {
    st.masterEnable = value;   // dword_62EA10 = result
    return value;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x44644c — VIBE_Sound_FindBankByName (__usercall eax=name)
// Walk the bank linked list (+316 next), return first whose name[0..50) matches.
// ------------------------------------------------------------------------------------
Addr AudioEngine::findBankByName(const char* name) {
    Addr i = st.bankListHead;                      // dword_62E8F8
    for (; i; i = addrAt(i, 316)) {                // i = *(i+316)
        if (vibeStrCmpNoCaseN(reinterpret_cast<const char*>(i), name, 50) == 0)
            break;
    }
    return i;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x4464b0 — VIBE_Sound_FindVoiceBySample (__usercall eax=voice, edx=&idx)
// Scan the 48-byte channel table for the first entry (from *idx) whose +4 boundVoice
// equals the requested voice record; writes the matching index back to *idx.
// ------------------------------------------------------------------------------------
Addr AudioEngine::findVoiceBySample(Addr voice, int* idx) {
    Addr table = st.sampleTable;                   // v2 = dword_62EA08 (saved/restored)
    if (!st.soundEnabled) return kSentinelAddr();  // !dword_62E8FC -> &unk_7679F0
    if (!st.sampleTable)  return 0;
    int v5 = *idx;
    if (*idx >= st.sampleCount) return 0;
    int v6 = 48 * v5;
    while (voice != getPtr(st.sampleTable + v6, 4)) {   // channel[v6].boundVoice
        v6 += 48;
        ++v5;
        if (v6 >= 48 * st.sampleCount) return 0;
    }
    Addr hit = st.sampleTable + v6;                // v8 = dword_62EA08 + v6
    *idx = v5;
    st.sampleTable = table;                        // dword_62EA08 = v2 (restore)
    return hit;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x446640 — VIBE_Sound_FindBankContainingVoice (__usercall eax=voice)
// Walk banks; in each, scan its voice array (stride 64, count +308) for a record whose
// address equals the given voice; return owning bank.
// ------------------------------------------------------------------------------------
Addr AudioEngine::findBankContainingVoice(Addr voice) {
    Addr v2 = st.bankListHead;                     // dword_62E8F8
    if (!st.soundEnabled) return kSentinelAddr();  // &unk_767B60
    while (v2) {
        int v3 = 0;
        if (i32at(v2, 308) > 0) {                  // (int)v2[77] > 0
            Addr v4 = addrAt(v2, 312);             // v2[78] = voices base
            while (v4 != voice) {
                ++v3;
                v4 += 64;
                if (v3 >= i32at(v2, 308)) goto next;
            }
            return v2;
        }
    next:
        v2 = addrAt(v2, 316);                      // v2[79] = next
    }
    return v2;                                      // 0
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x446800 — VIBE_Sound_GetLastBank
// Walk to the tail of the bank linked list.
// ------------------------------------------------------------------------------------
Addr AudioEngine::getLastBank() {
    Addr result = st.bankListHead;                 // dword_62E8F8
    if (!st.soundEnabled) return kSentinelAddr();  // &unk_767B60
    if (st.bankListHead) {
        while (addrAt(result, 316))                // result[79]
            result = addrAt(result, 316);
    }
    return result;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x446540 — VIBE_Sound_FindOldestActiveSample
// For every bank, scan its voice records; track the loaded voice with the smallest +60
// priority/age key. Then (if found) walk the channels playing it: if any is actively
// playing (status 4) or sticky (+20 always-true here due to |0x10), it cannot be freed.
// Returns the freeable oldest voice record, or 0.
// ------------------------------------------------------------------------------------
Addr AudioEngine::findOldestActiveSample() {
    Addr v0 = st.bankListHead;                     // dword_62E8F8
    Addr v1 = 0;
    if (!st.soundEnabled) return kSentinelAddr();  // &unk_767B20
    while (v0) {
        int idx = 0;                               // v9[0]
        if (v1 == 0) {
            int v5 = 0;
            while (idx < i32at(v0, 308)) {         // v0[77]
                Addr vbase = addrAt(v0, 312);
                if (getPtr(v5 + vbase, 56)) {       // voices[idx].loadedData
                    v1 = v5 + vbase;
                    break;
                }
                v5 += 64;
                ++idx;
            }
        }
        int v6 = idx << 6;
        while (idx < i32at(v0, 308)) {
            Addr vbase = addrAt(v0, 312);
            Addr v7 = v6 + vbase;
            if (i32at(v1, 60) > i32at(v7, 60) && getPtr(v7, 56)) {
                v1 = v6 + vbase;
            }
            v6 += 64;
            ++idx;
        }
        v0 = addrAt(v0, 316);                       // v0[79]
    }
    if (v1) {
        int idx = 0;                                // v9[0]
        Addr ch;
        while ((ch = findVoiceBySample(v1, &idx)) != 0) {
            if (hooks.sampleStatus && hooks.sampleStatus(ch) == 4)
                return 0;
            // v8 = *(ch+20); LOBYTE(v8) |= 0x10; if (v8) return 0;  -> always non-zero
            int v8 = (int)i32at(ch, 20);
            v8 = (v8 & ~0xFF) | ((v8 | 0x10) & 0xFF);
            if (v8) return 0;
            setPtr(ch, 4, 0);                       // *(ch+4) = 0 (unreached: v8 always set)
            ++idx;
        }
    }
    return v1;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x446694 — VIBE_Sound_FreeMemoryForLoad (__usercall eax=need)
// Evict oldest loaded voices until (memBudget - memUsed) >= need; returns 0 on success,
// -1 if it cannot free enough.
// ------------------------------------------------------------------------------------
int AudioEngine::freeMemoryForLoad(int need) {
    do {
        while (true) {
            if (st.memBudget - st.memUsed >= need) return 0;
            Addr oldest = findOldestActiveSample();
            if (!oldest) break;
            Addr data = addrAt(oldest, 56);         // OldestActiveSample[14] = +56
            if (!data) return -1;
            if (u8at(oldest, 54) == 1)              // loadKind raw
                st.memUsed -= i32at(data, 4) + 12;
            else
                st.memUsed -= i32at(data, 8) + 12;
            if (hooks.memFree) hooks.memFree(data); // VIBE_Memory_FreeDebug
            setPtr(oldest, 56, 0);                   // *(v6+56) = 0
        }
    } while (st.memBudget - st.memUsed >= need);
    return -1;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x447058 — VIBE_Audio_UnloadAllSampleBanks
// Free every bank: stop all voices bound to each loaded sample, release sample memory
// (adjusting memUsed by header+payload), then free the bank record. Resets the head.
// ------------------------------------------------------------------------------------
void AudioEngine::unloadAllSampleBanks() {
    if (st.soundEnabled && st.memUsed) {
        Addr v2 = st.bankListHead;                  // dword_62E8F8
        if (st.bankListHead) {
            do {
                i32at(v2, 320) = 0;                 // v2[80] = 0
                int v3 = 0;
                if (i32at(v2, 308) > 0) {           // v2[77]
                    Addr a2 = 0;                    // byte offset into voices
                    do {
                        Addr voice = a2 + (Addr)addrAt(v2, 312);
                        if (getPtr(voice, 56)) {    // loadedData present
                            int idx = 0;
                            while (true) {
                                Addr ch = findVoiceBySample(voice, &idx);
                                if (!ch) break;
                                if (hooks.stopVoice) hooks.stopVoice(ch, 0);
                                setPtr(ch, 4, 0);   // *(v6+4) = 0
                                ++idx;
                            }
                            Addr data = addrAt(voice, 56);
                            if (u8at(voice, 54) == 1)
                                st.memUsed -= i32at(data, 4) + 12;
                            else
                                st.memUsed -= i32at(data, 8) + 12;
                            if (hooks.memFree) hooks.memFree(data);
                            setPtr(voice, 56, 0);
                        }
                        ++v3;
                        a2 += 64;
                    } while (v3 < i32at(v2, 308));
                }
                st.memUsed -= (i32at(v2, 308) << 6) + 324;
                Addr nextBank = addrAt(v2, 316);    // v8 (read before free in original)
                if (hooks.memFree) hooks.memFree(v2);
                v2 = nextBank;
            } while (v2);
        }
        st.bankListHead = 0;
    }
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x446d88 — VIBE_Sound_UpdatePlayback (__usercall eax=result)
// result != 0: arm playback, stamp 13*sampleClock onto every actively-playing channel's
//              +16 posScratch. result == 0: stop every channel currently at status 4.
// ------------------------------------------------------------------------------------
int AudioEngine::updatePlayback(int result) {
    if (st.soundEnabled) {
        if (result) {
            st.digitalOutput = result;              // dword_62EA18 = result
            int v4 = 13 * st.sampleClock;
            int v5 = 0;
            st.playbackActive = 1;                  // dword_62EA14 = 1
            if (st.sampleCount > 0) {
                int v6 = 0;
                do {
                    Addr ch = (Addr)v6 + st.sampleTable;
                    if (getPtr(ch, 4)
                        && hooks.sampleStatus && hooks.sampleStatus(ch) == 4)
                        i32at(ch, 16) = v4;          // *(v7+16) = v4
                    result = st.sampleCount;
                    ++v5;
                    v6 += 48;
                } while (v5 < st.sampleCount);
            }
        } else {
            int v1 = 0;
            if (st.sampleCount > 0) {
                int v2 = 0;
                do {
                    Addr ch = (Addr)v2 + st.sampleTable;
                    if (getPtr(ch, 4)) {
                        result = hooks.sampleStatus ? hooks.sampleStatus(ch) : -1;
                        if (result == 4) {
                            if (hooks.stopVoice) hooks.stopVoice(ch, 0);
                            result = 0;
                        }
                    }
                    ++v1;
                    v2 += 48;
                } while (v1 < st.sampleCount);
            }
        }
    }
    return result;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x4494a4 — VIBE_Audio_CountActiveVoices
// Count channels that should be considered "active": playing (status 4), or unmanaged /
// looping / busy, or reserved (flag bit 0x10).
// ------------------------------------------------------------------------------------
int AudioEngine::countActiveVoices() {
    int v3 = 0;
    if (st.sampleTable) {
        int v4 = 0;
        if (st.sampleCount > 0) {
            int v5 = 0;
            do {
                Addr ch = (Addr)v5 + st.sampleTable;
                if (getPtr(ch, 4)) {       // boundVoice
                    if (hooks.sampleStatus && hooks.sampleStatus(ch) == 4) {
                        ++v3;
                    } else {
                        if (!i32at(ch, 24) || i32at(ch, 12) == 1 || (u8at(ch, 20) & 0xE) != 0) {
                            if ((u8at(st.sampleTable, v5 + 20) & 0x10) != 0)
                                ++v3;
                        } else {
                            ++v3;
                        }
                    }
                }
                ++v4;
                v5 += 48;
            } while (v4 < st.sampleCount);
        }
    }
    return v3;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x4475b8 — VIBE_Audio_SetVoiceLoopCount (__usercall eax=voice, edx=count)
// ------------------------------------------------------------------------------------
Addr AudioEngine::setVoiceLoopCount(Addr voice, int count) {
    if (st.soundEnabled) {
        if (voice) {
            i32at(voice, 12) = count;               // *(voice+12) = count
            if (hooks.setSampleLoopCount) hooks.setSampleLoopCount(voice);
            // 0x4475cb: return VIBE_Audio_SetSampleLoopCount() (eax) — 0 on success, -1
            // when the digital driver is not live (its `!dword_62EADC` early-out). That
            // leaf is the Miles/MSS boundary (AIL_set_sample_loop_count). Its eax return
            // is conveyed via the void hook's side effect; the faithful "not-live" value
            // is -1, so we return -1 here. BOUNDARY: a live SDL backend that reports
            // success would return 0 — wire that through if/when the leaf is reconstructed.
            return (Addr)(i32)(-1);
        }
    }
    return voice;                                   // !live / null voice: returns voice (eax in)
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x44727c — VIBE_Audio_RestartVoice (__usercall eax=&voiceRef)
// result is a pointer to {channel, voiceRecord}: result[0]=channel, result[1]=voice.
// If the voice's sample is already loaded (+56), resume; else (re)start playback.
// ------------------------------------------------------------------------------------
Addr AudioEngine::restartVoice(Addr result) {
    if (st.soundEnabled && getPtr(result, 0)) {     // *result
        Addr voice = addrAt(result, 4);             // result[1]
        if (getPtr(voice, 56)) {                    // *(result[1]+56)
            if (hooks.resumeSample) hooks.resumeSample(result);
            return result;
        } else {
            if (hooks.playVoiceSample) hooks.playVoiceSample(result, 0);
            return result;
        }
    }
    return result;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x4472c4 — VIBE_Audio_StartVoiceVariation (__userpurge eax=bank, ...)
// Allocate a channel, bind the bank sample, ensure it is loaded, then seed the channel
// fields (loopCount a2, vol a4, flag|4, variation a5, sampleIndex a3) and play.
// ------------------------------------------------------------------------------------
Addr AudioEngine::startVoiceVariation(Addr bank, int a2, int a3, int a4, int a5, int a6) {
    Addr bankContaining = bank;                     // a1
    if (!st.soundEnabled) return kSentinelAddr();   // &unk_7679F0
    Addr ch = hooks.allocVoiceChannel
                ? hooks.allocVoiceChannel(a2, a3, bank) : 0;
    if (!ch) return ch;
    Addr sample = hooks.findSampleInBank
                ? hooks.findSampleInBank(bankContaining, /*name*/0) : 0;
    setPtr(ch, 4, sample);                          // *(channel+4) = sample
    if (!sample) return ch;                          // (v12 ^ v14): channel still returned
    if (!bankContaining)
        bankContaining = findBankContainingVoice(sample);
    if (!bankContaining) return 0;
    Addr v15 = addrAt(ch, 4);                        // *(channel+4) (== sample)
    if (u8at(v15, 54) != 2) return 0;
    if (!getPtr(v15, 56)) {
        if (hooks.loadEntry) hooks.loadEntry(bankContaining, v15);
    }
    Addr v16 = getPtr(addrAt(ch, 4), 56);            // *(*(channel+4)+56)
    if (!v16 || a3 >= i32at(v16, 0)) return 0;
    u8 f = u8at(ch, 20);
    i32at(ch, 12) = a2;
    i32at(ch, 40) = a4;
    u8at(ch, 20)  = f | 4;
    i32at(ch, 36) = a5;
    i32at(ch, 32) = a3;
    if (hooks.playVoiceSample) hooks.playVoiceSample(ch, a6);
    return ch;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x439d74 — VIBE_Sound_SetVoicePosition (__usercall eax=slot, edx=vol)
// Set a streaming track slot's target volume; if not fading and active, apply now.
// (Critical-section bracketing in the original is a Win32->SDL concern and elided here;
//  the bookkeeping is identical.)
// ------------------------------------------------------------------------------------
void AudioEngine::setVoicePosition(Addr slot, i32 vol) {
    if (st.trackSysEnabled && slot) {
        u8 fading = u8at(slot, 276);
        i32at(slot, 280) = vol;                      // targetVol
        if (!fading && u8at(slot, 260)) {            // not fading && active
            i32 stream = i32at(slot, 256);
            i32at(slot, 284) = vol;                  // current vol
            if (stream && hooks.setStreamVolume) hooks.setStreamVolume(stream, vol);
        }
    }
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x43a3e4 — VIBE_Audio_MixerUpdate
// Advance every streaming track slot: detect end-of-stream and hand off to the chained
// slot; run fade-in (mode 1) and fade-out (mode 2) volume ramps; then advance the global
// master-volume fade. All arithmetic reproduced exactly (integer/64-bit abs included).
// ------------------------------------------------------------------------------------
void AudioEngine::mixerUpdate() {
    if (!st.trackSysEnabled) return;                 // dword_62DA28
    for (int i = 0; i != 2960; i += 296) {
        Addr slot = st.trackSlots + (Addr)i;         // (char*)&unk_62DA30 + i
        if (!u8at(st.trackSlots, i + 260)) continue; // byte_62DB34[i] : slot active/used flag
        // NB: byte_62DB34 (0x62DB34) == unk_62DA30 (0x62DA30) + 0x104 (260) — the in-record
        // "active" byte (slot+260). FindFreeVoiceSlot confirms stride 296, flag at +260.

        if (!i32at(slot, 256)) {                     // no stream handle
            Addr chain = addrAt(slot, 292);          // [73] chainNext (+292)
            u8at(slot, 260) = 0;                     // not active
            if (!chain) continue;
            if (i32at(chain, 256)) {
                // LABEL_8: resume the chained stream
                if (hooks.pauseStream) hooks.pauseStream(i32at(chain, 256), 0);
                u8at(addrAt(slot, 292), 260) = 1;
            }
            // LABEL_9:
            u8at(addrAt(slot, 292), 261) = 0;
            addrAt(slot, 292) = 0;
            continue;
        }

        if (hooks.getStreamMsPosition)
            hooks.getStreamMsPosition(i32at(slot, 256), slot + 268);
        if (u8at(slot, 262)) continue;               // music slot: skip fade handling

        // end-of-stream detection (not playing & not pending)
        int s4 = hooks.streamStatus ? hooks.streamStatus(i32at(slot, 256)) : 0;
        if (s4 != 4 && s4 != 16) {
            u8at(slot, 260) = 0;
            i32 closing = i32at(slot, 256);
            u8at(slot, 276) = 0;
            if (hooks.closeStream) hooks.closeStream(closing);
            Addr chain = addrAt(slot, 292);
            i32at(slot, 256) = 0;
            if (chain) {
                if (i32at(chain, 256)) {
                    if (hooks.pauseStream) hooks.pauseStream(i32at(chain, 256), 0);
                    u8at(addrAt(slot, 292), 260) = 1;
                    u8at(addrAt(slot, 292), 261) = 0;
                    addrAt(slot, 292) = 0;
                }
            }
        }

        u8 mode = u8at(slot, 276);
        if (mode == 1) {
            // ---- fade-in ----
            int v11 = i32at(slot, 272);
            if (i32at(slot, 268) < v11) i32at(slot, 268) = v11;
            int v12 = i32at(slot, 288);
            int v13;
            if (v12)
                v13 = (int)((unsigned)(v12 * st.trackMasterClock) / (unsigned)i32at(slot, 280));
            else
                v13 = st.trackMasterClock;
            int v14 = i32at(slot, 268) - i32at(slot, 272);
            bool done =
                !v13
                || v13 <= v14
                || (i32at(slot, 284) = (int)((unsigned)(i32at(slot, 280) * v14) / (unsigned)v13),
                    abs64lo((i64)i32at(slot, 280) - (i64)i32at(slot, 284)) < 2)
                || v13 < i32at(slot, 268) - i32at(slot, 272);
            if (done) {
                u8at(slot, 276) = 0;
                i32at(slot, 284) = i32at(slot, 280);
            }
            if (hooks.setStreamVolume)
                hooks.setStreamVolume(i32at(slot, 256), i32at(slot, 284)); // LABEL_28
            continue;
        }
        if (mode == 2) {
            // ---- fade-out ----
            int v16 = i32at(slot, 272);
            if (i32at(slot, 268) < v16) i32at(slot, 268) = v16;
            unsigned v17 = (unsigned)i32at(slot, 280);
            int v18 = (int)((unsigned)(i32at(slot, 288) * st.trackMasterClock) / v17);
            int v19 = i32at(slot, 268) - i32at(slot, 272);
            if (v18 && v18 > v19) {
                unsigned v20 = v17 * (unsigned)(v18 - v19) / (unsigned)v18;
                i32at(slot, 284) = (int)v20;
                // 0x43a6a7 cmp eax,2 / jbe -> UNSIGNED compare (v20 > 2u); then
                // 0x43a6b8 cmp ebx,eax / jge -> SIGNED compare (v18 >= v19).
                if (v20 > 2u && v18 >= i32at(slot, 268) - i32at(slot, 272)) {
                    if (hooks.setStreamVolume)
                        hooks.setStreamVolume(i32at(slot, 256), i32at(slot, 284)); // LABEL_28
                    continue;
                }
                i32at(slot, 284) = 0;
                u8at(slot, 276) = 0;
                if (hooks.setStreamVolume)
                    hooks.setStreamVolume(i32at(slot, 256), i32at(slot, 284));
                if (hooks.closeStream) hooks.closeStream(i32at(slot, 256));
                i32at(slot, 256) = 0;
                Addr chain = addrAt(slot, 292);
                u8at(slot, 260) = 0;
                if (chain) {
                    if (hooks.pauseStream) hooks.pauseStream(i32at(chain, 256), 0);
                    u8at(addrAt(slot, 292), 260) = 1;
                    u8at(addrAt(slot, 292), 261) = 0;
                    addrAt(slot, 292) = 0;
                }
            } else {
                i32at(slot, 284) = 0;
                u8at(slot, 276) = 0;
                if (hooks.setStreamVolume)
                    hooks.setStreamVolume(i32at(slot, 256), i32at(slot, 284));
                if (hooks.closeStream) hooks.closeStream(i32at(slot, 256));
                i32at(slot, 256) = 0;
                Addr chain = addrAt(slot, 292);
                u8at(slot, 260) = 0;
                if (chain) {
                    if (i32at(chain, 256)) {
                        if (hooks.pauseStream) hooks.pauseStream(i32at(chain, 256), 0);
                        u8at(addrAt(slot, 292), 260) = 1;
                        u8at(addrAt(slot, 292), 261) = 0;
                        addrAt(slot, 292) = 0;
                    }
                }
            }
        }
    }

    // ---- master-volume fade tail ----
    if (st.masterVolTarget >= 0.0f) {                // flt_62DA00 >= 0
        unsigned v4 = (unsigned)(13 * st.sampleClock - st.masterFadeStart);
        if (v4 < (unsigned)st.masterFadeLen) {
            st.masterVolCur = st.masterVolBase
                + (st.masterVolTarget - st.masterVolBase)
                  / (double)(unsigned)st.masterFadeLen * (double)v4;
            if (hooks.applyMasterVolume)
                hooks.applyMasterVolume(st.masterVolHandle, st.sampleClock);
        } else {
            st.masterVolCur = st.masterVolTarget;
            st.masterVolTarget = -1.0f;
            if (hooks.applyMasterVolume)
                hooks.applyMasterVolume(st.masterVolHandle, st.sampleClock);
        }
    }
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x445ea4 — VIBE_Sound_LibShutdown
// Unload banks, free the channel table, close the digital output.
// ------------------------------------------------------------------------------------
void AudioEngine::libShutdown() {
    unloadAllSampleBanks();
    if (st.sampleTable) {
        if (hooks.memFree) hooks.memFree(st.sampleTable);
        st.sampleTable = 0;                          // dword_62EA08 = v4 (freed -> 0)
        if (!st.soundEnabled) return;
        if (hooks.closeDigitalOutput) hooks.closeDigitalOutput((Addr)st.soundEnabled);
        st.soundEnabled = 0;
        return;
    }
    if (st.soundEnabled) {
        if (hooks.closeDigitalOutput) hooks.closeDigitalOutput((Addr)st.soundEnabled);
        st.soundEnabled = 0;
    }
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x424c70 — VIBE_Sound3d_StopEntry (__usercall eax=entry, edx=a2)
// ------------------------------------------------------------------------------------
Addr AudioEngine::sound3dStopEntry(Addr entry, int a2) {
    if (entry) {
        if (getPtr(entry, 52)) {                     // *(entry+52) = bound channel
            if (hooks.stopVoice) hooks.stopVoice(addrAt(entry, 52), a2);
            return addrAt(entry, 52);
        }
    }
    return entry;
}

// ------------------------------------------------------------------------------------
// gilde.exe 0x424f00 — VIBE_Sound3d_SelectChannel (__usercall eax=channelIdx, edx=&mode)
// Two-channel positional crossfade selector. *mode in {-1,0,1} chooses A/B channel and
// looping; reuses the already-playing channel if it carries the same sample name, else
// (re)starts a new sample on the appropriate channel. Returns the chosen sample record.
//
// The 3d channel "name" record is at snd3dChannels + 25*channelIdx (v6 in the original).
// VIBE_Sound_PlaySample, SetLoopFlag, VoiceIsPlaying, StrCmp, StrNCopyPad are leaves
// reached via hooks; the branch structure and *mode writes are reproduced 1:1.
// ------------------------------------------------------------------------------------
Addr AudioEngine::sound3dSelectChannel(int channelIdx, int* mode) {
    int v4 = *mode;
    Addr v6 = st.snd3dChannels + (Addr)(25 * channelIdx);    // name record
    auto isPlaying = [&](Addr voice) -> int {
        return (voice && hooks.voiceIsPlaying) ? hooks.voiceIsPlaying(voice, v6) : 0;
    };
    auto sameName = [&](Addr sample) -> bool {
        return sample && vibeStrCmp(reinterpret_cast<const char*>(v6),
                                    reinterpret_cast<const char*>(sample)) == 0;
    };
    auto startOn = [&](Addr& sampleSlot, Addr& voiceSlot, int loop) -> Addr {
        if (voiceSlot && hooks.setLoopFlag) hooks.setLoopFlag(voiceSlot, 0);
        Addr s = hooks.playSample ? hooks.playSample((int)sampleSlot, v6) : 0;
        // *(channel+52) = s  -- channel-record bind (we expose via sampleSlot semantics)
        if (!s || !getPtr(s, 4)) return 0;
        if (hooks.setLoopFlag) hooks.setLoopFlag(s, 1);
        // VIBE_Util_StrNCopyPad(name <- *(s+4)+4, 50): copy bound sample's name into v6
        std::memset(reinterpret_cast<void*>(v6), 0, 50);
        std::strncpy(reinterpret_cast<char*>(v6),
                     reinterpret_cast<const char*>(addrAt(s, 4) + 4), 50);
        (void)loop;
        return s;
    };

    if (v4 < 0) {
        if (v4 != -1) return 0;
        if (!st.snd3dSampleA || !st.snd3dHandleA || !isPlaying(st.snd3dHandleA)) {
            Addr s = startOn(st.snd3dSampleA, st.snd3dHandleA, 0);
            if (!s) return 0;
            *mode = 0;
            return s;
        }
        if (sameName(st.snd3dSampleA)) { *mode = 0; return st.snd3dSampleA; }
        if (st.snd3dSampleB && st.snd3dHandleB && isPlaying(st.snd3dHandleB)) {
            if (!sameName(st.snd3dSampleB)) return 0;
            *mode = 1;
            return st.snd3dSampleB;
        }
        Addr s = startOn(st.snd3dSampleB, st.snd3dHandleB, 1);
        if (!s) return 0;
        *mode = 1;
        return s;
    }
    if (v4 == 0) {
        if (st.snd3dSampleB && st.snd3dHandleB && isPlaying(st.snd3dHandleB)) {
            if (!sameName(st.snd3dSampleB)) return 0;
            *mode = 1;
            return st.snd3dSampleB;
        }
        Addr s = startOn(st.snd3dSampleB, st.snd3dHandleB, 1);
        if (!s) return 0;
        *mode = 1;
        return s;
    }
    if (v4 != 1) return 0;
    if (!st.snd3dSampleA || !st.snd3dHandleA || !isPlaying(st.snd3dHandleA)) {
        Addr s = startOn(st.snd3dSampleA, st.snd3dHandleA, 1);
        if (!s) { *mode = 0; return 0; }
        *mode = 0;
        return s;
    }
    if (!sameName(st.snd3dSampleA)) return 0;
    return st.snd3dSampleA;
}

} // namespace guild::audio::recon
