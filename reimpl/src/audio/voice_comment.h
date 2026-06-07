#pragma once
// guild::audio — WORKER-COMMENT click playback + comment-bank lifetime of
// gilde.exe.
//
// When the player clicks a selected worker the game plays a short spoken
// "click" comment, randomly choosing between the worker's spoken acknowledgement
// bank and a generic noise/grunt bank. The selected worker is found by scanning
// the person table for the one with its "selected" flag set. On a context change
// the four loaded comment sample-banks are released.
//
// Recovered from:
//   VIBE_Voice_PlayWorkerClickComment    @0x5823a4
//   VIBE_Voice_PlaySelectedWorkerComment @0x582418
//   VIBE_Voice_UnloadCommentBanks        @0x58232c
//   VIBE_Voice_PlayCraftFavorComment     @0x582448  (NEW)
//   VIBE_Voice_PlayBuildingFavorComment  @0x58269c  (NEW)
//
// The two FAVOR comments select WHICH spoken "Gunst" (favour/standing) line to
// play out of the worker-command bank (craft) / building-greeting bank, gated by
// the AVERAGE FAVORABILITY of the involved NPC(s) toward the local player. The
// favourability score is produced by the ai sibling
// (VIBE_Ai_ComputePersonFavorability @0x594330); to keep the audio module free of
// an ai link it is supplied through a caller callback, exactly as the click code
// receives its person ids through CommentPerson. The threshold bucketing and the
// "side" tie-break are reproduced 1:1.
//
// The originals read loose globals; they are modelled here as controller state
// (matching MusicPlayer / VoiceQueue in this module):
//   dword_6476E8 commandBank  — worker command/favour bank handle
//   dword_6476EC clickBank    — worker spoken-click bank handle
//   dword_6476F0 noiseBank    — generic click-noise bank handle
//   dword_6476F8 greetingBank — building greeting bank handle
// Person records live in the person table (word_12CE910, stride 268 words = 536
// bytes); the per-person "selected" flag is the byte at +392, the spoken person
// id is the first word.
//
// Playback routes through IVoiceCommentSink (a thin facade over
// VIBE_Voice_PlayPositionalSample, which itself reaches shim::IAudioDevice); the
// random choice uses util::RandomModulo so the call order matches the original.
#include "guild/common/types.h"
#include <string>
#include <vector>

namespace guild::audio {

// Byte offset of the per-person "selected" flag inside a person record
// (byte_12CEA98 - word_12CE910 = 0x188). Same flag PlayCraftFavorComment reads.
constexpr int kPersonSelectedFlagOffset = 392;

// Person-table stride in 16-bit words (the original steps the index by 268).
constexpr int kPersonStrideWords = 268;

// Number of person records scanned (205824 / 268; the original's loop bound).
constexpr int kPersonTableCount = 768;

// The fixed sample-name passed to PlayPositionalSample for both click banks
// ("_ARBEITER_KOMMENTARE_KLICK" @0x62636c).
constexpr char kWorkerClickSampleName[] = "_ARBEITER_KOMMENTARE_KLICK";

// "Gunst" (favour/standing) tier sample-name tags. The craft comment (0x582448)
// uses the bare names; the building comment (0x58269c) uses the leading-'_'
// variants. (Recovered .rdata @0x626388.. / @0x6263c0..)
constexpr char kCraftFavorBad[]    = "SCHLECHTE_GUNST";   // aSchlechteGunst
constexpr char kCraftFavorMedium[] = "MITTLERE_GUNST";    // aMittlereGunst
constexpr char kCraftFavorGood[]   = "GUTE_GUNST";        // aGuteGunst
constexpr char kBldFavorBad[]      = "_SCHLECHTE_GUNST";  // aSchlechteGunst_0
constexpr char kBldFavorMedium[]   = "_MITTLERE_GUNST";   // aMittlereGunst_0
constexpr char kBldFavorGood[]     = "_GUTE_GUNST";       // aGuteGunst_0

// The craft favour comment's on-demand command-bank file (loaded into
// commandBank_ the first time it is needed). (@0x626200)
constexpr char kCraftCommentBankFile[] = "ARBEITER_KOMMENTARE_BEFEHL_HANDWERK.sbf";

// RNG skip-gate for the craft comment: it speaks only when the float roll is
// <= 0.5 (dbl_6263B8). 50% of craft orders stay silent.
constexpr double kCraftFavorSkipGate = 0.5;

// One selected worker/unit slot (dword_11BB6A0[i], 32 entries) as the craft
// comment reads it: a spoken id (+0), an "active" gate (+392) and a "side" byte
// (+9). `present` models the slot pointer being non-null.
struct CraftUnit {
    bool present = false;  // dword_11BB6A0[i] != 0
    bool active  = false;  // record[+392] != 0  (same selected flag as elsewhere)
    bool sideA   = false;  // record[+9]   != 0
    u16  id      = 0;      // record[+0]
};

// Number of selected-unit slots the craft comment scans (dword_11BB6A0).
constexpr int kCraftUnitCount = 32;

// A person record as the comment code sees it: a spoken id and a "selected"
// flag. Modeled from word_12CE910 slots (id == first word, flag == byte +392).
struct CommentPerson {
    u16  id = 0;            // *a1 — the spoken sample/person id
    bool selected = false;  // byte at person+392
};

// Facade over VIBE_Voice_PlayPositionalSample @0x581fc0: play the positional
// sample identified by `personId` from sample-bank `bankHandle`, using the named
// sample (variation index -1 in the original). `bankHandle` is the opaque
// comment-bank handle; returns true if a voice was started.
class IVoiceCommentSink {
public:
    virtual ~IVoiceCommentSink() = default;
    virtual bool playPositionalSample(u16 personId, int bankHandle,
                                      const std::string& sampleName) = 0;
    // Facade over VIBE_Audio_UnloadSampleBank @0x446e4c.
    virtual void unloadSampleBank(int bankHandle) = 0;

    // Channel-aware variant: VIBE_Voice_PlayPositionalSample's 3rd arg (the
    // variation/channel index) is -1 for click comments but varies for the favour
    // comments (0..3). The default forwards to the index-less overload so existing
    // sinks keep working; favour-aware sinks may override to observe `channel`.
    virtual bool playPositionalSample(u16 personId, int bankHandle, int channel,
                                      const std::string& sampleName) {
        (void)channel;
        return playPositionalSample(personId, bankHandle, sampleName);
    }

    // VIBE_Audio_LoadSampleBank facade used by the craft comment's on-demand load
    // of the worker command bank. Returns the bank handle (0 == load failed).
    // Default returns 0 (no banks available); production/test sinks override.
    virtual int loadCommentBank(const std::string& file) { (void)file; return 0; }
};

// gilde.exe 0x582448 (inlined) — bucket an average-favour value the way the craft
// comment does, returning the RAW intermediate (NOT a clean 0/1/2): >75 -> 2,
// [50,75) -> 1, <50 -> 0, and avg==75 is LEFT UNCHANGED at 75 (the binary only
// reassigns when avg>75). The play-branch tag test then treats this raw value as
// `!v` -> Bad, `v==1` -> Medium, else -> Good (so 75 speaks GOOD). Preserved
// byte-exactly; exposed for golden-vector testing.
int BucketCraftFavorRaw(int avgFavor);

class WorkerCommentPlayer {
public:
    explicit WorkerCommentPlayer(IVoiceCommentSink* sink) : sink_(sink) {}

    // Comment-bank handles (0 == not loaded). Set by the bank loaders.
    void setClickBank(int h) { clickBank_ = h; }      // dword_6476EC
    void setNoiseBank(int h) { noiseBank_ = h; }      // dword_6476F0
    void setCommandBank(int h) { commandBank_ = h; }  // dword_6476E8
    void setGreetingBank(int h) { greetingBank_ = h; }// dword_6476F8

    int clickBank() const { return clickBank_; }
    int noiseBank() const { return noiseBank_; }
    int commandBank() const { return commandBank_; }
    int greetingBank() const { return greetingBank_; }

    // VIBE_Voice_PlayWorkerClickComment @0x5823a4 — randomly play `person`'s
    // click line from either the spoken-click bank (roll 0) or the noise bank
    // (roll 1). Returns the original's int result (0 when neither bank fired).
    int playWorkerClickComment(const CommentPerson& person);

    // VIBE_Voice_PlaySelectedWorkerComment @0x582418 — find the first person in
    // `people` whose "selected" flag is set and play their click comment. Returns
    // 0 when none is selected (the original returns result*2, == 0).
    int playSelectedWorkerComment(const std::vector<CommentPerson>& people);

    // VIBE_Voice_UnloadCommentBanks @0x58232c — release every loaded comment bank
    // (command, click, noise, greeting) and clear its handle.
    void unloadCommentBanks();

    // Favourability provider: VIBE_Ai_ComputePersonFavorability(localPlayer, id, 1)
    // for an NPC id (the audio module does not link ai; the caller binds this to
    // the real scorer). RNG providers mirror the LCG helpers the original calls.
    using FavorFn  = double (*)(u16 npcId, void* ctx);
    using FloatFn  = double (*)(void* ctx);   // VIBE_Math_RandomFloatScaled 0x58b910
    using ModuloFn = int    (*)(u16 n, void* ctx); // VIBE_Math_RandomModulo  0x58b89c

    // VIBE_Voice_PlayCraftFavorComment @0x582448 — when the player issues a craft
    // order, speak one "Gunst" line from the worker that best represents the squad:
    //   * 50% skip gate (randFloat() > 0.5 -> silent);
    //   * load the worker command bank into commandBank_ on demand;
    //   * over the up-to-32 selected units, sum (int)favor(id) and track the last
    //     side-A id / side-B id and their counts;
    //   * average the sum over the active-unit count, bucket it (BucketCraftFavorRaw),
    //     and pick the speaker + tag by side tie-break (see .cpp, 1:1 with the binary).
    // `units` is the selected-unit list (dword_11BB6A0, up to 32). Returns the
    // commandBank_ handle in use (0 only when the gate skipped before any load).
    int playCraftFavorComment(const CraftUnit* units, int unitCount,
                              FavorFn favor, FloatFn randFloat, ModuloFn randMod,
                              void* ctx);

    // VIBE_Voice_PlayBuildingFavorComment @0x58269c — speak the building owner's
    // "Gunst" line out of greetingBank_:
    //   channel = randMod(3);
    //   bucket  = (int)favor(ownerId) / 33;   // 0 -> bad, 1 -> medium, >=2 -> good
    //   used    = slotEmpty ? 3 : channel;    // empty workstation forces channel 3
    //   play(ownerId, greetingBank_, used, tag);
    // `ownerId` is the resolved building-owner person id; `slotEmpty` is the
    // building's single-workstation-slot probe result. Returns the channel used.
    int playBuildingFavorComment(u16 ownerId, bool slotEmpty,
                                 FavorFn favor, ModuloFn randMod, void* ctx);

private:
    IVoiceCommentSink* sink_;
    int commandBank_ = 0;  // dword_6476E8
    int clickBank_ = 0;    // dword_6476EC
    int noiseBank_ = 0;    // dword_6476F0
    int greetingBank_ = 0; // dword_6476F8
};

} // namespace guild::audio
