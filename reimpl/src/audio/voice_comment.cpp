#include "audio/voice_comment.h"

#include "util/math_random.h"

namespace guild::audio {

// gilde.exe 0x5823a4 — VIBE_Voice_PlayWorkerClickComment
//   int __usercall __spoils<ecx>@<eax>(unsigned __int16 *a1@<eax>)
// a1 points at the person record; *a1 is the spoken id.
int WorkerCommentPlayer::playWorkerClickComment(const CommentPerson& person) {
    // result = (unsigned __int16)VIBE_Math_RandomModulo(2u);
    int result = static_cast<u16>(guild::util::RandomModulo(2u));

    // if ( dword_6476EC && !result )
    //   return VIBE_Voice_PlayPositionalSample(*a1, dword_6476EC, -1, name);
    if (clickBank_ && result == 0) {
        sink_->playPositionalSample(person.id, clickBank_, kWorkerClickSampleName);
        return clickBank_; // original returns the PlayPositionalSample pointer
    }

    // if ( dword_6476F0 ) { if ( result == 1 )
    //   return VIBE_Voice_PlayPositionalSample(*a1, dword_6476F0, -1, name); }
    if (noiseBank_) {
        if (result == 1) {
            sink_->playPositionalSample(person.id, noiseBank_, kWorkerClickSampleName);
            return noiseBank_;
        }
    }

    // return result;
    return result;
}

// gilde.exe 0x582418 — VIBE_Voice_PlaySelectedWorkerComment
//   int __thiscall(void *this)
int WorkerCommentPlayer::playSelectedWorkerComment(
        const std::vector<CommentPerson>& people) {
    // result = 0;
    // if ( byte_12CEA98[0] ) return PlayWorkerClickComment(&word_12CE910[0]);
    // while (1) { result += 268; if (result >= 205824) break;
    //   if ( byte_12CEA98[result*2] ) return PlayWorkerClickComment(...); }
    // return result * 2;
    //
    // The byte-index arithmetic (result += 268 words, flag at result*2 bytes)
    // walks the person table one record at a time; here that is index i over the
    // injected table, bounded by the original's count (kPersonTableCount).
    const int count = static_cast<int>(people.size()) < kPersonTableCount
                          ? static_cast<int>(people.size())
                          : kPersonTableCount;
    for (int i = 0; i < count; ++i) {
        if (people[i].selected)
            return playWorkerClickComment(people[i]);
    }
    return 0; // result*2 with result == kPersonTableCount index past the end (==0 found)
}

// gilde.exe 0x58232c — VIBE_Voice_UnloadCommentBanks
//   int __usercall@<eax>(int a1@<esi>)
void WorkerCommentPlayer::unloadCommentBanks() {
    // if ( dword_6476E8 ) { VIBE_Audio_UnloadSampleBank(dword_6476E8, a1);
    //                       dword_6476E8 = 0; }
    if (commandBank_) {
        sink_->unloadSampleBank(commandBank_);
        commandBank_ = 0;
    }
    // if ( dword_6476EC ) { VIBE_Audio_UnloadSampleBank(dword_6476EC, 0);
    //                       dword_6476EC = 0; }
    if (clickBank_) {
        sink_->unloadSampleBank(clickBank_);
        clickBank_ = 0;
    }
    // if ( dword_6476F0 ) { VIBE_Audio_UnloadSampleBank(dword_6476F0, a1);
    //                       dword_6476F0 = 0; }
    if (noiseBank_) {
        sink_->unloadSampleBank(noiseBank_);
        noiseBank_ = 0;
    }
    // result = dword_6476F8; if ( dword_6476F8 ) {
    //   VIBE_Audio_UnloadSampleBank(dword_6476F8, a1); dword_6476F8 = 0; }
    if (greetingBank_) {
        sink_->unloadSampleBank(greetingBank_);
        greetingBank_ = 0;
    }
}

// gilde.exe 0x582448 — the nested favour bucketing (inlined into the craft
// comment). NB: avg == 75 is intentionally LEFT at 75 (the `if (v8 > 75)` guard
// skips it); the play branches test it as truthy-non-1 -> Good.
int BucketCraftFavorRaw(int avgFavor) {
    int v8 = avgFavor;
    if (v8 >= 50) {
        if (v8 >= 75) {
            if (v8 > 75)
                v8 = 2;
            // else v8 stays 75.
        } else {
            v8 = 1;
        }
    } else {
        v8 = 0;
    }
    return v8;
}

// gilde.exe 0x582448 — VIBE_Voice_PlayCraftFavorComment  (void())
//   if (RandomFloatScaled() > 0.5) return;
//   if (!commandBank) commandBank = LoadLanguageBank("ARBEITER_..._HANDWERK.sbf");
//   for i in 0..31: u = dword_11BB6A0[i];
//       if (u && u[392]) { if (u[9]) {v0=u[0]; ++v7;} else {v2=u[0]; ++v1;}
//                          v8 = (int)(ComputePersonFavorability(self,u[0],1)+v8); }
//   if (v1+v7) v8 /= v1+v7;
//   <bucket v8>;  <side tie-break + tag>;  PlayPositionalSample(speaker, bank, -1, tag);
int WorkerCommentPlayer::playCraftFavorComment(
        const CraftUnit* units, int unitCount,
        FavorFn favor, FloatFn randFloat, ModuloFn randMod, void* ctx) {
    (void)randMod; // the craft comment uses only the float roll + favourability.

    constexpr u16 kNoUnit = 0xFFFF;
    u16 v0 = kNoUnit; int v7 = 0;  // last side-A id, side-A count
    u16 v2 = kNoUnit; int v1 = 0;  // last side-B id, side-B count
    int v8 = 0;                    // accumulated then averaged favour

    // 50% skip gate.
    if (randFloat(ctx) > kCraftFavorSkipGate)
        return commandBank_;

    // Load the worker command bank on demand.
    if (!commandBank_)
        commandBank_ = sink_->loadCommentBank(kCraftCommentBankFile);

    const int n = unitCount < kCraftUnitCount ? unitCount : kCraftUnitCount;
    for (int i = 0; i != n; ++i) {
        const CraftUnit& u = units[i];
        if (u.present && u.active) {
            if (u.sideA) {
                v0 = u.id;
                ++v7;
            } else {
                v2 = u.id;
                ++v1;
            }
            // v8 += (int)ComputePersonFavorability(self, id, 1). The intervening
            // VIBE_Coord_ConvertX is a no-op FP->int truncation thunk.
            v8 = static_cast<int>(favor(u.id, ctx) + static_cast<double>(v8));
        }
    }

    if (v1 + v7)
        v8 /= (v1 + v7);

    v8 = BucketCraftFavorRaw(v8);

    // Side tie-break + tag selection (1:1 with the two play branches).
    if (v1 >= v7 || (v0 == kNoUnit && v2 == kNoUnit)) {
        const char* tag;
        if (v2 == kNoUnit) {
            v2 = static_cast<u16>(-5);  // sentinel speaker when no side-B id
            tag = kCraftFavorMedium;
        } else if (!v8) {
            tag = kCraftFavorBad;
        } else if (v8 != 1) {
            tag = kCraftFavorGood;
        } else {
            tag = kCraftFavorMedium;
        }
        sink_->playPositionalSample(v2, commandBank_, -1, tag);
        return commandBank_;
    }

    if (v0 != kNoUnit) {
        const char* tag = !v8 ? kCraftFavorBad
                              : (v8 == 1 ? kCraftFavorMedium : kCraftFavorGood);
        sink_->playPositionalSample(v0, commandBank_, -1, tag);
    }
    return commandBank_;
}

// gilde.exe 0x58269c — VIBE_Voice_PlayBuildingFavorComment
//   result = RandomModulo(3);  v6 = (u16)result;
//   v12 = (int)ComputePersonFavorability(self, owner, 1) / 33;
//   v4  = (slots collected && second-count == 0) ? 1 : 0;   // empty slot probe
//   if (v12 == 0)      Play(owner, greetingBank, v4 ? 3 : v6, "_SCHLECHTE_GUNST");
//   else if (v12 == 1) Play(owner, greetingBank, v4 ? 3 : v6, "_MITTLERE_GUNST");
//   else               Play(owner, greetingBank, v4 ? 3 : v6, "_GUTE_GUNST");
int WorkerCommentPlayer::playBuildingFavorComment(
        u16 ownerId, bool slotEmpty, FavorFn favor, ModuloFn randMod, void* ctx) {
    int channel = static_cast<u16>(randMod(3, ctx));
    int bucket  = static_cast<int>(favor(ownerId, ctx)) / 33;  // 0..3
    int used    = slotEmpty ? 3 : channel;

    const char* tag;
    if (bucket == 0)
        tag = kBldFavorBad;
    else if (bucket == 1)
        tag = kBldFavorMedium;
    else
        tag = kBldFavorGood;

    sink_->playPositionalSample(ownerId, greetingBank_, used, tag);
    return used;
}

} // namespace guild::audio
