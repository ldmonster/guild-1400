#include "app/real_audio_driver.h"

#include "audio/samplebank_load.h"
#include "audio/samplebank.h"

#include <cctype>
#include <cstring>

namespace guild::app {

namespace {

// Read a whole file off `fs` into `out`. Returns false if it can't be opened.
// The only OS boundary; the .sbf parse below is pure reconstructed code.
bool SlurpFile(shim::IFileSystem* fs, const std::string& path, std::vector<u8>& out) {
    if (!fs)
        return false;
    shim::IFile* f = fs->open(path.c_str(), "rb");
    if (!f)
        return false;
    std::int64_t n = f->size();
    if (n < 0) {
        fs->close(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(n));
    std::size_t got = n > 0 ? f->read(out.data(), static_cast<std::size_t>(n)) : 0;
    out.resize(got);
    fs->close(f);
    return true;
}

bool EqualNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

// Resolve `vfsPath` over `fs`. If a plain open fails (host is case-sensitive and
// the include casing differs from the on-disk names — e.g. "SPRACHE\\..."),
// walk the path segment by segment matching each against the directory listing
// case-insensitively, rebuilding the real on-disk path. Returns "" if unresolved.
std::string ResolveCaseInsensitive(shim::IFileSystem* fs, const std::string& vfsPath) {
    if (!fs)
        return {};
    // Fast path: exact open works.
    if (fs->exists(vfsPath.c_str()))
        return vfsPath;

    // Segment walk. Split on '/'.
    std::vector<std::string> segs;
    std::string cur;
    for (char c : vfsPath) {
        if (c == '/') {
            if (!cur.empty()) segs.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) segs.push_back(cur);
    if (segs.empty())
        return {};

    std::string built; // accumulates the resolved real path
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const std::string& want = segs[i];
        std::string dir = built.empty() ? std::string(".") : built;
        shim::IDirListing* listing = fs->listDir(dir.c_str());
        if (!listing)
            return {};
        std::string match;
        for (std::size_t k = 0; k < listing->count(); ++k) {
            const shim::DirEntry& e = listing->at(k);
            if (e.name && EqualNoCase(want, e.name)) {
                match = e.name;
                break;
            }
        }
        delete listing;
        if (match.empty())
            return {};
        built = built.empty() ? match : (built + "/" + match);
    }
    return fs->exists(built.c_str()) ? built : std::string{};
}

} // namespace

std::vector<std::string> ParseSfxIncludeList(const std::string& text) {
    std::vector<std::string> out;
    const char* kw = "#include";
    std::size_t pos = 0;
    while ((pos = text.find(kw, pos)) != std::string::npos) {
        std::size_t i = pos + std::strlen(kw);
        // Skip whitespace between `#include` and the opening quote (both forms
        // appear in the real file: `#include"x"` and `#include "x"`).
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
            ++i;
        if (i >= text.size() || text[i] != '"') {
            pos = i;
            continue;
        }
        ++i; // past opening quote
        std::size_t start = i;
        while (i < text.size() && text[i] != '"' && text[i] != '\n' && text[i] != '\r')
            ++i;
        if (i < text.size() && text[i] == '"' && i > start)
            out.push_back(text.substr(start, i - start));
        pos = i;
    }
    return out;
}

std::string NormalizeSfxPath(const std::string& sfxRoot, const std::string& include) {
    std::string p;
    if (!sfxRoot.empty()) {
        p = sfxRoot;
        if (p.back() != '/' && p.back() != '\\')
            p.push_back('/');
    }
    p += include;
    // Backslash -> forward slash; collapse doubled separators.
    std::string out;
    char prev = 0;
    for (char c : p) {
        if (c == '\\')
            c = '/';
        if (c == '/' && prev == '/')
            continue; // collapse
        out.push_back(c);
        prev = c;
    }
    return out;
}

SfxBankLoad LoadOneSfxBank(shim::IFileSystem* fs, audio::SoundSystem& sound,
                           const std::string& sfxRoot, const std::string& include) {
    SfxBankLoad r;
    r.include = include;
    std::string norm = NormalizeSfxPath(sfxRoot, include);
    std::string resolved = ResolveCaseInsensitive(fs, norm);
    r.vfsPath = resolved.empty() ? norm : resolved;

    std::vector<u8> bytes;
    if (resolved.empty() || !SlurpFile(fs, resolved, bytes) || bytes.empty())
        return r; // opened=false
    r.opened = true;

    // VIBE_Sound_LoadSampleBank @0x446b2c — parse the .sbf index.
    audio::SbBank bank;
    if (!audio::LoadSampleBankFromBuffer(bytes.data(), bytes.size(), r.vfsPath, bank))
        return r; // parsed=false
    r.parsed = true;
    r.bankName = bank.name;
    r.entryCount = bank.entries.size();

    // Bridge the parsed entries into the live SampleBank so the name-lookup path
    // (VIBE_SampleBank_FindSampleByName) resolves against real loaded records.
    // format byte 2 == variation/mp3 group; 1 == single .wav. We add every entry
    // as a top-level sample (the real lookups walk the flat list first).
    for (const audio::SbEntry& e : bank.entries) {
        audio::SampleRecord& rec = sound.bank().addSample(e.name);
        rec.format = e.format;
        rec.sampleRate = audio::kDefaultSampleRate;
        // A tiny non-empty PCM payload so the 3D mix path treats it as playable;
        // the .sbf index carries no PCM (faulted on demand by VIBE_Sound_LoadEntry).
        rec.pcm.assign(64, 0);
    }
    return r;
}

RealAudioResult DriveRealSfxAudio(shim::IFileSystem* fs, audio::SoundSystem& sound,
                                  const audio::Vec3& listenerPos,
                                  const audio::Vec3& listenerForward,
                                  const std::string& includeName,
                                  const std::string& sfxRoot,
                                  std::size_t maxVoices) {
    RealAudioResult res;
    if (!fs)
        return res;

    // 1. read the include listing off the VFS-bound fs.
    std::vector<u8> iniBytes;
    std::string iniResolved = ResolveCaseInsensitive(fs, includeName);
    if (iniResolved.empty())
        iniResolved = includeName;
    if (!SlurpFile(fs, iniResolved, iniBytes))
        return res; // iniFound=false
    res.iniFound = true;

    std::string text(reinterpret_cast<const char*>(iniBytes.data()), iniBytes.size());
    std::vector<std::string> includes = ParseSfxIncludeList(text);
    res.includesListed = includes.size();

    // 2-3. resolve + load each .sbf, bridging entries into the SampleBank.
    for (const std::string& inc : includes) {
        SfxBankLoad b = LoadOneSfxBank(fs, sound, sfxRoot, inc);
        if (b.parsed) {
            ++res.banksLoaded;
            res.samplesLoaded += b.entryCount;
        }
        res.banks.push_back(std::move(b));
    }

    // 4. drive the 3D mix math on the loaded banks. Attach up to maxVoices of the
    // loaded samples as positioned emitters at spread-out positions, then run one
    // Sound3dPool::updateAll so Compute3dVolume / Compute3dPan run for real.
    std::vector<audio::SampleRecord>& recs = sound.bank().samples();
    std::size_t placed = 0;
    for (std::size_t i = 0; i < recs.size() && placed < maxVoices; ++i) {
        // Spread sources around the listener at varying distances so the mix
        // produces a range of volumes/pans (and at least some are audible).
        float ofs = static_cast<float>((i % 4) + 1) * 200.0f; // 200..800 units
        audio::Vec3 pos{ listenerPos.x + ofs, listenerPos.y, listenerPos.z + ofs * 0.5f };
        // radius (max distance) generous enough that near sources stay audible.
        audio::Sound3dEntry* e = sound.playPositioned(
            recs[i].name, pos, /*baseVol=*/127, /*radius=*/2000.0f,
            listenerPos, listenerForward, /*oneShot=*/true);
        if (e)
            ++placed;
    }
    res.voicesPlaced = placed;

    // Re-run the whole-pool update once more so every placed voice is mixed under
    // the same listener pose, then tally audible voices + peak volume.
    sound.pool3d().updateAll(listenerPos, listenerForward);
    audio::Sound3dPool& pool = sound.pool3d();
    for (int i = 0; i < pool.capacity(); ++i) {
        audio::Sound3dEntry* e = pool.entryAt(i);
        if (!e || !e->inUse)
            continue;
        int vol = audio::Compute3dVolume(listenerPos, e->sourcePos, e->radius, e->baseVol);
        if (vol > 0)
            ++res.voicesAudible;
        if (vol > res.peakVolume)
            res.peakVolume = vol;
    }
    return res;
}

} // namespace guild::app
