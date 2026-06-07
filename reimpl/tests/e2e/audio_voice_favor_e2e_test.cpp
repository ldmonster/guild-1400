// tests/e2e/audio_voice_favor_e2e_test.cpp — end-to-end favour-comment flow.
//
// Exercises the whole worker/building favour-feedback flow across the translated
// functions (click comment + craft favour comment + building favour comment),
// driven by the REAL ai favourability sibling and the REAL util LCG, through the
// same WorkerCommentPlayer / IVoiceCommentSink the game uses.
//
// GUARDED: the spoken "Gunst" sample banks are real .sbf assets not in the repo.
// The pure selection flow runs headlessly (always); the real-asset bank-load leg
// is only attempted when GUILD_E2E_ASSETS is set, otherwise it is a clean skip.
#include "test.h"

#include "audio/voice_comment.h"
#include "ai/favorability.h"
#include "util/math_random.h"
#include "util/math_rng_float.h"
#include "crt/rand.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;
using guild::audio::CraftUnit;
using guild::audio::CommentPerson;

namespace {

struct FavData : ai::FavorabilityEnv {
    ai::FavPersonFields Person(int) override { return {}; }
    ai::OfficeDefinition Office(u8) override { return {}; }
    int WorkstationWorkers(int) override { return 0; }
    int QueryByGoodType(int) override { return 0; }
    int QueryBeginWorkers(int, int, bool& g) override { g = false; return 0; }
    int GesetzState() override { return 0; }
    int InventorySlot(int, int) override { return 0; }
};
struct Ctx { FavData fav; int localPlayer = 5; };

double RealFavor(u16 id, void* c) {
    auto* x = static_cast<Ctx*>(c);
    return ai::ComputePersonFavorability(x->localPlayer, id, true, x->fav);
}
double RealFloat(void*) { return util::RandomFloatScaled(); }
int    RealMod(u16 n, void*) { return util::RandomModulo(n); }

struct E2ESink : audio::IVoiceCommentSink {
    int plays = 0; std::string last; int bankSeq = 200;
    bool playPositionalSample(u16, int, const std::string& n) override {
        ++plays; last = n; return true;
    }
    bool playPositionalSample(u16, int, int, const std::string& n) override {
        ++plays; last = n; return true;
    }
    void unloadSampleBank(int) override {}
    int  loadCommentBank(const std::string&) override { return bankSeq++; }
};

} // namespace

TEST(AudioVoiceFavorE2E, FullFavorDialogueFlow) {
    E2ESink sink;
    audio::WorkerCommentPlayer player(&sink);
    player.setClickBank(11);
    player.setNoiseBank(22);
    player.setGreetingBank(33);

    Ctx ctx;  // local player 5; all workers are the local player -> favour 100.

    // 1) Click comment on a selected worker (deterministic via reseed).
    crt::Srand(2024);
    CommentPerson worker; worker.id = 5; worker.selected = true;
    int roll = player.playWorkerClickComment(worker);
    CHECK(roll == 11 || roll == 22 || roll == 0 || roll == 1);

    // 2) Craft favour comment: seed 2 clears the 50% gate (~0.028), so it speaks.
    //    Squad of three side-A self-workers -> favour 100 -> GUTE on the last id.
    crt::Srand(2);
    CraftUnit squad[3] = {
        {true, true, true, 5}, {true, true, true, 5}, {true, true, true, 5},
    };
    int before = sink.plays;
    int cmdBank = player.playCraftFavorComment(squad, 3, RealFavor, RealFloat,
                                               RealMod, &ctx);
    CHECK(cmdBank != 0);          // command bank loaded on demand (gate passed)
    CHECK(sink.plays > before);
    CHECK(sink.last == "GUTE_GUNST");

    // 3) Building favour comment for the local-player owner -> _GUTE_GUNST.
    crt::Srand(11);
    int ch = player.playBuildingFavorComment(5, /*slotEmpty=*/false, RealFavor,
                                             RealMod, &ctx);
    CHECK(ch >= 0 && ch <= 3);
    CHECK(sink.last == "_GUTE_GUNST");

    // 4) Bank teardown releases the loaded command bank.
    player.unloadCommentBanks();
    CHECK_EQ(player.commandBank(), 0);
    CHECK_EQ(player.greetingBank(), 0);
}

TEST(AudioVoiceFavorE2E, RealAssetBankLoad_Guarded) {
    if (std::getenv("GUILD_E2E_ASSETS") == nullptr) {
        std::printf("  [skip] AudioVoiceFavorE2E.RealAssetBankLoad: "
                    "GUILD_E2E_ASSETS unset\n");
        return;  // clean skip — no checks recorded
    }
    E2ESink sink;
    audio::WorkerCommentPlayer player(&sink);
    Ctx ctx;
    // With real assets a production sink would mount the .sbf; here we only assert
    // the on-demand load edge is reached through the craft comment entry the game
    // uses (seed 2 passes the gate).
    crt::Srand(2);
    CraftUnit u{true, true, true, 5};
    int bank = player.playCraftFavorComment(&u, 1, RealFavor, RealFloat, RealMod, &ctx);
    CHECK(bank != 0);
}
