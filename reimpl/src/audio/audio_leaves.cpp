#include "audio/audio_leaves.h"

namespace guild::audio {

// gilde.exe 0x44ab2c — VIBE_Audio_FindFreeStreamSlot (eax = find(a1=out@eax)).
//   v6 = 0; if (cap <= 0) return -1;
//   for (i = streamSlots; *i; ++i) if (++v6 >= cap) return -1;
//   return v6;
// (The leading guards walk dword_62EA1C[] to confirm the output is one of the
// 16 and that allocatedSampleCount < capacity; modelled by the open/cap checks.)
int FindFreeStreamSlot(const DigitalAudio& d, int outIndex) {
    const DigitalOutput* o = d.outputAt(outIndex);
    if (!o || !o->open)
        return -1;                            // a1 not in dword_62EA1C[16]
    const int cap = static_cast<int>(o->maxSampleHandles); // *(a1+24)
    if (o->allocatedSampleCount >= o->maxSampleHandles)    // *(a1+20) >= cap
        return -1;
    if (cap <= 0)                              // if (v5 <= 0) return -1
        return -1;
    int v6 = 0;
    for (std::size_t i = 0; i < o->streamSlots.size() && o->streamSlots[i]; ++i) {
        if (++v6 >= cap)                       // ran past capacity, no free slot
            return -1;
    }
    return v6;                                 // first free (zero) slot index
}

// gilde.exe 0x44abd8 — VIBE_Audio_LookupStreamHandleIndex (eax = lookup(h@eax)).
// Scan each open output's stream slots over [0, capacity); first match wins.
int LookupStreamHandleIndex(const DigitalAudio& d, shim::VoiceHandle handle) {
    for (int i = 0; i < 16; ++i) {             // for (i=0; i<16; ++i)
        const DigitalOutput* o = d.outputAt(i);
        if (!o || !o->open)                    // if (dword_62EA1C[i])
            continue;
        for (int idx = 0; idx < static_cast<int>(o->maxSampleHandles); ++idx) {
            if (handle == o->streamSlots[static_cast<std::size_t>(idx)]) // a1 == streamSlots[i][idx]
                return idx;
        }
    }
    return -1;
}

// gilde.exe 0x44ac20 — VIBE_Audio_LookupSampleDriverIndex (eax = lookup(h@eax)).
// Returns the OUTPUT index (v6, bumped each iteration) owning sample `handle`.
int LookupSampleDriverIndex(const DigitalAudio& d, shim::VoiceHandle handle) {
    int v6 = 0;                                // v6 counts every slot, even closed
    for (int i = 0; i < 16; ++i, ++v6) {       // v2/v6 advance together
        const DigitalOutput* o = d.outputAt(i);
        if (o && o->open) {                    // if (dword_62EA1C[v2])
            for (int s = 0; s < static_cast<int>(o->maxSampleHandles); ++s) {
                if (handle == o->sampleSlots[static_cast<std::size_t>(s)])
                    return v6;
            }
        }
    }
    return -1;
}

// gilde.exe 0x44ac88 — VIBE_Audio_LookupStreamDriverIndex (eax = lookup(h@eax)).
int LookupStreamDriverIndex(const DigitalAudio& d, shim::VoiceHandle handle) {
    int v6 = 0;
    for (int i = 0; i < 16; ++i, ++v6) {
        const DigitalOutput* o = d.outputAt(i);
        if (o && o->open) {
            for (int s = 0; s < static_cast<int>(o->maxSampleHandles); ++s) {
                if (handle == o->streamSlots[static_cast<std::size_t>(s)])
                    return v6;
            }
        }
    }
    return -1;
}

// gilde.exe 0x44aa94 — VIBE_Audio_FindDriverIndex (eax = find(a1=out@eax)).
//   v2 = 0; if (a1 == dword_62EA1C[0]) return 0;
//   while (1) { ++v3; ++v2; if (v3 >= 16) break; if (a1 == dword_62EA1C[v3]) return v2; }
//   return -1;
// The original matches a1 by pointer identity against the 16-table. Here the
// caller passes the slot index it already holds; we confirm it names an open
// slot (the pointer-identity test always succeeds for the owning slot) and
// return that index, else -1.
int FindDriverIndex(const DigitalAudio& d, int outIndex) {
    const DigitalOutput* o = d.outputAt(outIndex);
    if (o && o->open && outIndex >= 0 && outIndex < 16)
        return outIndex;
    return -1;
}

// gilde.exe 0x449528 — VIBE_Audio_CountAllocatedVoices.
// The original sums the 0x10 ("allocated") flag over the global voice array;
// the per-output allocatedSampleCount is its faithful running total.
int CountAllocatedVoices(const DigitalAudio& d) {
    int total = 0;
    for (int i = 0; i < d.outputCount(); ++i) {
        const DigitalOutput* o = d.outputAt(i);
        if (o && o->open)
            total += static_cast<int>(o->allocatedSampleCount);
    }
    return total;
}

// gilde.exe 0x449dd4 — VIBE_Audio_GetGlobalPreference (eax = get(a1=&out@eax)).
//   if (!dword_62EADC) return -1;                      // driver present
//   count = #open outputs;
//   if (!count && !dword_62EAE0) return -1;            // no outputs & pref 0
//   *a1 = dword_62EAE0; return 0;
int GetGlobalPreference(const DigitalAudio& d, int globalPref, int* out) {
    if (!d.driverInstalled())
        return -1;
    int count = 0;
    for (int i = 0; i < 16; ++i) {
        const DigitalOutput* o = d.outputAt(i);
        if (o && o->open)
            ++count;
    }
    if (!count && !globalPref)
        return -1;
    if (out)
        *out = globalPref;                     // *a1 = dword_62EAE0
    return 0;
}

// gilde.exe 0x449e20 — VIBE_Audio_SetGlobalPreference (eax = set(a1=val@eax)).
//   if (!dword_62EADC) return -1;
//   count = #open outputs; if (count > 1) return -1;   // can't change once 2+ open
//   dword_62EAE0 = a1; AIL_set_preference(1, a1); return 0;
int SetGlobalPreference(const DigitalAudio& d, int* globalPref, int value) {
    if (!d.driverInstalled())
        return -1;
    int count = 0;
    for (int i = 0; i < 16; ++i) {
        const DigitalOutput* o = d.outputAt(i);
        if (o && o->open)
            ++count;
    }
    if (count > 1)
        return -1;
    if (globalPref)
        *globalPref = value;                   // dword_62EAE0 = a1
    return 0;
}

// gilde.exe 0x4498b4 — clamp portion of VIBE_Audio_SetDigitalMasterVolume.
//   ... if (v5 < 0 || a2 >= 0x80) return -1; AIL_set_digital_master_volume(...);
// a2 is unsigned, so a2 >= 0x80 rejects both >127 and (wrapped) negatives.
int ClampDigitalMasterVolume(int volume) {
    if (static_cast<unsigned>(volume) >= 0x80u) // a2 >= 0x80 (unsigned)
        return -1;
    return volume;                              // already in [0,127]
}

// gilde.exe 0x56c148 — VIBE_Audio_ApplyVolumeSettings.
//   v5 = (double)soundByte * scale0; SetMasterVolume((int)v5);
//   sfx   = (double)sfxByte  * v5;  ApplyMasterVolume((int)sfx);
//   music = (double)musicByte* v5;  SetMusicVolume((int)music);
//   flt_64200C = (double)ambByte  * scale0;
//   flt_6422A8 = (double)amb2Byte * scale1;
// The float-to-int conversions truncate toward zero (VIBE_Coord_ConvertX sets
// the x87 control word to chop mode). All operands are non-negative.
VolumeSettingsOut ApplyVolumeSettings(const VolumeSettingsIn& in) {
    VolumeSettingsOut out{};
    const double v5 = static_cast<double>(in.soundByte) * static_cast<double>(in.scale0);
    out.masterVolume = static_cast<int>(v5);            // SetMasterVolume((int)v5)
    out.sfxVolume    = static_cast<int>(static_cast<double>(in.sfxByte) * v5);  // ApplyMasterVolume
    out.musicVolume  = static_cast<int>(static_cast<double>(in.musicByte) * v5);// SetMusicVolume
    out.ambientScale = static_cast<float>(static_cast<double>(in.ambByte) * static_cast<double>(in.scale0));
    out.ambient2Scale= static_cast<float>(static_cast<double>(in.amb2Byte) * static_cast<double>(in.scale1));
    return out;
}

// gilde.exe 0x43a910 — VIBE_Audio_FadeOutTrack (eax = arm(a1=track@eax, dl=mode)).
//   if (result && *(result+260)) {            // track active
//     if (a2) {                               // mode != 0
//       GetStreamMsPosition(*(result+256), result+268);
//       *(result+276) = a2;                   // fadeMode
//       *(result+272) = *(result+268);        // fadeStartMs = ms position
//       *(result+288) = *(result+284);        // fadeBaseVol  = curVolume
//     }
//   }
void FadeOutTrack(TrackFadeState* t, u8 mode, int currentMs) {
    if (t && t->streamActive) {
        if (mode) {
            t->msPosition  = currentMs;        // GetStreamMsPosition writes +268
            t->fadeMode    = mode;             // +276
            t->fadeStartMs = t->msPosition;    // +272 = +268
            t->fadeBaseVol = t->curVolume;     // +288 = +284
        }
    }
}

// gilde.exe 0x43a95c — VIBE_Audio_SetTrackNamePrefix (al = copy(a1=src@eax)).
// Word-copy loop: copies two bytes per iteration into unk_7649B8 until a NUL is
// hit. The single-char overrun in the original's odd-length case is benign in
// the global byte buffer; we copy a normal NUL-terminated string. Returns 0
// (the terminating NUL it leaves in `al`).
char SetTrackNamePrefix(std::string& dst, const char* src) {
    dst.clear();
    if (!src)
        return 0;
    while (*src) {
        dst.push_back(*src);
        ++src;
    }
    return 0;
}

// gilde.exe 0x505ba8 — VIBE_Audio_PlayAmbientVoice (list-append portion).
//   v6 = dword_6344A0;
//   if (dword_6344A0 < 16) { dword_122DCA0[dword_6344A0] = h; dword_6344A0 = v6+1; return v6+1; }
//   return result;
int AppendAmbientVoice(AmbientVoiceList& list, shim::VoiceHandle handle) {
    const int prior = list.count;              // v6 = dword_6344A0
    if (list.count < kMaxAmbientVoices) {
        list.handles[static_cast<std::size_t>(list.count)] = handle;
        list.count = prior + 1;                // dword_6344A0 = v6 + 1
        return prior + 1;
    }
    return prior;
}

// gilde.exe 0x505da8 — VIBE_Audio_StopAmbientVoices.
//   v0 = 0; if (dword_6344A0 > 0) do { StopVoice(dword_122DCA0[v0], 1); ... } while (v0 < count);
//   dword_6344A0 = 0;
int StopAmbientVoices(AmbientVoiceList& list,
                      const std::function<void(shim::VoiceHandle)>& stop) {
    int stopped = 0;
    const int n = list.count;                  // v4 = dword_6344A0
    for (int i = 0; i < n; ++i) {
        if (stop)
            stop(list.handles[static_cast<std::size_t>(i)]); // VIBE_Audio_StopVoice(h, 1)
        list.handles[static_cast<std::size_t>(i)] = 0;       // clear slot
        ++stopped;
    }
    list.count = 0;                            // dword_6344A0 = 0
    return stopped;
}

} // namespace guild::audio
