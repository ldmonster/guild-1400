#pragma once
// guild::play — host-side MP3 → PCM decode (for the menu music, gilde.exe's Miles
// .mp3 tracks). Implemented with libmpg123 when the real backend is built
// (GUILD_HAVE_MP3); a no-op stub otherwise so the portable/headless build needs no
// audio-codec dependency.
#include <cstdint>
#include <string>
#include <vector>

namespace guild::play {

// Decode the MP3 at absolute filesystem `path` to interleaved signed-16-bit STEREO
// PCM. On success fills `pcm` (L,R,L,R,...), sets `sampleRate`, and returns true.
// Returns false if the file is missing/malformed or MP3 support wasn't compiled in.
bool DecodeMp3File(const std::string& path, std::vector<std::int16_t>& pcm,
                   int& sampleRate);

} // namespace guild::play
