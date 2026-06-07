#pragma once
#include "guild/common/types.h"
#include "sim/types.h"
#include <array>
#include <string>
#include <vector>

// =============================================================================
// guild::audio — world-coupled music selection (gilde.exe, msx/ track director).
//
//   0x581208  VIBE_Music_SelectOutdoorSeasonTrack
//   0x581594  VIBE_Music_UpdateOutdoorTrackPlayback
//   0x580de4  VIBE_Music_ResumeLocationTrack
//   0x581508  VIBE_Music_FindActiveTrackSlot
//   0x58153c  VIBE_Music_FindTrackById
//   0x58339c  VIBE_GameTime_GetSeasonFromDay   (day % 4)
//
// The track director keeps a table of "track entries", each entry mapping a set
// of LOCATION/TYPE ids (terminated by 0; the sentinel id 9876 = the generic
// OUTDOOR track) to a streamable file name and per-entry playback metadata. The
// season is derived purely from the calendar (day % 4): 0=spring, 1=summer,
// 2=autumn, 3=winter. The outdoor selector picks a season-appropriate file with
// a RandNext()-driven variant choice; the playback updater drives a small state
// machine (running -> ended/paused -> resume/reselect) keyed on stream cursor
// position, season change, and a random interrupt probability.
//
// RNG FIDELITY: variant selection uses crt::RandNext via util::RandomModulo
// (VIBE_Math_RandomModulo @0x58b89c). The call order matches the originals.
//
// COUPLING: the originals reference the active location pointer, the per-person
// query system, and the audio LoadTrack/StopTrack streaming layer. We abstract
// playback behind IMusicSink and pass the location/season inputs explicitly so
// the selection + state logic is testable in isolation. Field offsets in the
// 276-byte original record are noted on the modelled struct.
// =============================================================================
namespace guild::audio {

// The generic outdoor-track sentinel id (constant 9876 in the originals).
constexpr int kOutdoorTrackId = 9876;

// Season ids matching GM_SPRING/SUMMER/AUTUMN/WINTER.
enum Season { kSpring = 0, kSummer = 1, kAutumn = 2, kWinter = 3 };

// gilde.exe 0x58339c — VIBE_GameTime_GetSeasonFromDay: *day % 4.
inline int SeasonFromDay(const guild::sim::GameTime& t) { return t.day % 4; }

// ---------------------------------------------------------------------------
// Track entry — the 276-byte (0x114) original record, modelled by purpose.
//   ids[]   dword_645F18[69*i + 0..]  : id list, 0-terminated (9876 == outdoor)
//   name    dword_645F18[69*i + 4]    : file name at byte +16 (inline char[])
//   prob    unk_646026 (record >>16)  : interrupt probability (0..127)
//   volume  word_646028 (138*i)       : current volume bias word
//   active  byte_64602A (276*i)       : 1 = this entry's track is the active one
// ---------------------------------------------------------------------------
struct TrackEntry {
    std::vector<int> ids;       // 0-terminated id list (sentinel not stored)
    std::string name;           // streamable file name
    int prob = 0;               // interrupt probability (>>16 in the original)
    int volume = 0;             // volume bias word
    bool active = false;        // active-slot flag
};

// Playback sink — abstracts VIBE_Audio_LoadTrack/StopTrack. Returns a non-zero
// opaque stream handle from load (matching the original's dword_642018 != 0).
class IMusicSink {
public:
    virtual ~IMusicSink() = default;
    virtual int loadTrack(const std::string& name, int loop) = 0; // 0x439ed0
    virtual void stopTrack(int handle, int fade) = 0;             // 0x43a2fc
};

// The track director state (the loose globals dword_6476xx / dword_642018..).
struct MusicDirector {
    std::vector<TrackEntry> table;
    int currentTrackHandle = 0;   // dword_642018 (active stream, 0 = none)
    int currentLocationId = 0;    // dword_631744 / dword_631748 (active location)
    int lastSeason = -1;          // dword_6476E0 (cached season for change detect)
    int seasonCached = -1;        // byte_6422A0-equivalent for this director
    std::string lastTrackName;    // byte_645E16 (the just-finished track name)
    IMusicSink* sink = nullptr;
};

// gilde.exe 0x58153c — VIBE_Music_FindTrackById: index of the entry whose id
// list contains `id`, else -1.
int FindTrackById(const MusicDirector& d, int id);

// gilde.exe 0x581508 — VIBE_Music_FindActiveTrackSlot: index of the entry whose
// `active` flag is set, else -1.
int FindActiveTrackSlot(const MusicDirector& d);

// gilde.exe 0x581208 — VIBE_Music_SelectOutdoorSeasonTrack. Picks the OUTDOOR
// entry (id 9876), assigns a season-appropriate file name (RandNext variant),
// marks it active and loads it. If the entry already has a name it is resumed
// instead. Returns the entry index (or -1 if no outdoor entry exists).
int SelectOutdoorSeasonTrack(MusicDirector& d, int season);

// The four file-name pools the original chooses from per season; index 0 is the
// non-random fallback, the rest are the RandNext-selected variants.
const std::array<const char*, 4>& SpringTracks();
const std::array<const char*, 4>& SummerTracks();
const std::array<const char*, 4>& AutumnTracks();
const std::array<const char*, 4>& WinterTracks();

// State-machine inputs for one playback tick (the globals the updater reads).
struct PlaybackTick {
    int season;          // VIBE_GameTime_GetSeasonFromDay(now)
    bool atStreamEnd;    // *(handle+260)==0 && *(handle+261)==0 (cursor at end)
    int locationId;      // dword_631744 / 748 (current location, 0 = outdoor)
};

// What the updater decided this tick (so callers/tests can assert transitions).
enum class PlaybackAction {
    kNone,            // nothing changed
    kSeasonStop,      // running outdoor track stopped due to season change
    kTrackEnded,      // active track reached its end -> entered pause
    kResumeLocation,  // started/resumed a location-specific track
    kSelectOutdoor,   // (re)selected the outdoor season track
};

// gilde.exe 0x581594 — VIBE_Music_UpdateOutdoorTrackPlayback (state core).
// Applies one tick of the running/ended/season-change logic and returns the
// transition taken. `tick.atStreamEnd` models the stream-cursor end check.
PlaybackAction UpdateOutdoorTrackPlayback(MusicDirector& d, const PlaybackTick& tick);

// gilde.exe 0x580de4 — VIBE_Music_ResumeLocationTrack (simplified to the
// location->track id resolve + activate path). Resolves the current location's
// track id, finds its entry, marks it active and loads it. Returns the entry
// index or -1.
int ResumeLocationTrack(MusicDirector& d, int locationTrackId);

} // namespace guild::audio
