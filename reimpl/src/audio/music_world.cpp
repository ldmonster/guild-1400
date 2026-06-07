#include "audio/music_world.h"
#include "util/math_random.h"

namespace guild::audio {

// ---------------------------------------------------------------------------
// Season file-name pools. Index 0 is the non-random fallback (variant==0); the
// random variant (RandomModulo(0x75) % 3) picks among the others. Strings are
// the exact original literals (cd1\\... .mp3). Winter has three distinct
// variants (0,1,2); the other seasons collapse 1 and 2 onto the same file.
// ---------------------------------------------------------------------------
namespace {
const std::array<const char*, 4> kSpringPool = {
    "cd1\\Burgfraeulein.mp3",  // variant 0
    "cd1\\ImFruehling.mp3",    // variant 1
    "cd1\\ImFruehling.mp3",    // variant 2
    "cd1\\ImFruehling.mp3"};
const std::array<const char*, 4> kSummerPool = {
    "cd1\\Burgfraeulein.mp3",  // variant 0
    "cd1\\ZurSommerzeit.mp3",  // variant 1
    "cd1\\ZurSommerzeit.mp3",  // variant 2
    "cd1\\ZurSommerzeit.mp3"};
const std::array<const char*, 4> kAutumnPool = {
    "cd1\\AufDenStrassen.mp3", // variant 0
    "cd1\\ImHerbst.mp3",       // variant 1
    "cd1\\ImHerbst.mp3",       // variant 2
    "cd1\\ImHerbst.mp3"};
const std::array<const char*, 4> kWinterPool = {
    "cd1\\AufDenStrassen.mp3",        // variant 0
    "cd1\\InkalterNovembernacht.mp3", // variant 1
    "cd1\\ImmerKalt.mp3",             // variant 2
    "cd1\\ImmerKalt.mp3"};

// Map (season, variant 0..2) -> file name, byte-faithful to the switch in
// VIBE_Music_SelectOutdoorSeasonTrack.
const char* PickName(int season, int variant) {
    switch (season) {
    case kSpring: return variant ? kSpringPool[1] : kSpringPool[0];
    case kSummer: return variant ? kSummerPool[1] : kSummerPool[0];
    case kAutumn: return variant ? kAutumnPool[1] : kAutumnPool[0];
    case kWinter:
        if (!variant) return kWinterPool[0];
        return variant == 1 ? kWinterPool[1] : kWinterPool[2];
    default: return "";
    }
}
} // namespace

const std::array<const char*, 4>& SpringTracks() { return kSpringPool; }
const std::array<const char*, 4>& SummerTracks() { return kSummerPool; }
const std::array<const char*, 4>& AutumnTracks() { return kAutumnPool; }
const std::array<const char*, 4>& WinterTracks() { return kWinterPool; }

int FindTrackById(const MusicDirector& d, int id) {
    for (std::size_t i = 0; i < d.table.size(); ++i) {
        for (int eid : d.table[i].ids)
            if (eid == id)
                return (int)i;
    }
    return -1;
}

int FindActiveTrackSlot(const MusicDirector& d) {
    for (std::size_t i = 0; i < d.table.size(); ++i)
        if (d.table[i].active)
            return (int)i;
    return -1;
}

int SelectOutdoorSeasonTrack(MusicDirector& d, int season) {
    // The original first clears any currently-active slot (decrements its
    // volume bias and flag); we clear the active flag to match the observable
    // "only one slot active" invariant.
    int activeSlot = FindActiveTrackSlot(d);
    if (activeSlot >= 0)
        d.table[activeSlot].active = false;

    int slot = FindTrackById(d, kOutdoorTrackId);
    if (slot < 0)
        return -1;
    TrackEntry& e = d.table[slot];

    if (!e.name.empty()) {
        // Resume path: the entry already has a name -> just (re)load it.
        if (d.sink)
            d.currentTrackHandle = d.sink->loadTrack(e.name, 0);
        e.active = true;
        return slot;
    }

    // Selection path: roll a variant until the chosen name differs from the
    // last-played one (avoid an immediate repeat). lastTrackName empty == no
    // constraint (matches `strlen(byte_645E16)` guard).
    std::string chosen;
    do {
        int variant = guild::util::RandomModulo(0x75) % 3;
        chosen = PickName(season, variant);
        e.name = chosen;
    } while (!d.lastTrackName.empty() && d.lastTrackName != chosen);

    d.lastTrackName.clear();
    e.active = true;
    if (d.sink)
        d.currentTrackHandle = d.sink->loadTrack(e.name, 0);
    return slot;
}

int ResumeLocationTrack(MusicDirector& d, int locationTrackId) {
    // Clear the active slot first (the original decrements its volume bias).
    int activeSlot = FindActiveTrackSlot(d);
    if (activeSlot >= 0)
        d.table[activeSlot].active = false;

    int slot = FindTrackById(d, locationTrackId);
    if (slot < 0)
        return -1;
    TrackEntry& e = d.table[slot];
    e.active = true;
    if (d.sink)
        d.currentTrackHandle = d.sink->loadTrack(e.name, 0);
    return slot;
}

PlaybackAction UpdateOutdoorTrackPlayback(MusicDirector& d, const PlaybackTick& tick) {
    // (A) Season change handling.
    if (tick.season != d.lastSeason) {
        if (d.lastSeason != 0 && d.lastSeason != -1) {
            // A non-initial season change: stop the running outdoor track.
            int outdoor = FindTrackById(d, kOutdoorTrackId);
            d.lastSeason = tick.season;
            if (outdoor >= 0 && d.table[outdoor].active) {
                d.table[outdoor].active = false;
                if (d.currentTrackHandle) {
                    d.lastTrackName = d.table[outdoor].name;
                    if (d.sink)
                        d.sink->stopTrack(d.currentTrackHandle, 1);
                    d.currentTrackHandle = 0;
                }
                return PlaybackAction::kSeasonStop;
            }
        } else {
            d.lastSeason = tick.season;
        }
        return PlaybackAction::kNone;
    }

    // (B) No track running -> (re)start one.
    if (!d.currentTrackHandle) {
        if (tick.locationId)
            return (ResumeLocationTrack(d, tick.locationId) >= 0)
                       ? PlaybackAction::kResumeLocation
                       : PlaybackAction::kNone;
        return (SelectOutdoorSeasonTrack(d, tick.season) >= 0)
                   ? PlaybackAction::kSelectOutdoor
                   : PlaybackAction::kNone;
    }

    // (C) Active track reached its end -> deactivate + enter pause.
    if (tick.atStreamEnd) {
        int slot = FindActiveTrackSlot(d);
        if (slot >= 0) {
            d.table[slot].active = false;
            d.lastTrackName = d.table[slot].name;
        }
        d.currentTrackHandle = 0;
        return PlaybackAction::kTrackEnded;
    }

    return PlaybackAction::kNone;
}

} // namespace guild::audio
