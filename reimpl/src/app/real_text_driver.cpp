#include "app/real_text_driver.h"

#include "io/archive_mount.h"
#include "io/vfs.h"

#include <cctype>

namespace guild::app {

namespace {

// Case-insensitive ".res" suffix test (mount-normalized names are uppercased, but
// stay tolerant either way).
bool EndsWithRes(const std::string& s) {
    if (s.size() < 4)
        return false;
    const char* e = s.c_str() + (s.size() - 4);
    return e[0] == '.' &&
           (e[1] == 'r' || e[1] == 'R') &&
           (e[2] == 'e' || e[2] == 'E') &&
           (e[3] == 's' || e[3] == 'S');
}

// Find the mounted archive entry for `archiveMember` in the boot result.
RealGameAssets::MountedArchive* FindArchive(RealGameAssets& assets,
                                            const std::string& archiveMember) {
    for (auto& a : assets.archives) {
        if (a.name == archiveMember)
            return &a;
    }
    return nullptr;
}

} // namespace

RealTextDbResult LoadRealTextDb(RealGameAssets& assets,
                                const std::string& archiveMember,
                                gui::text::TextDb& db) {
    RealTextDbResult out;

    RealGameAssets::MountedArchive* ma = FindArchive(assets, archiveMember);
    if (!ma || !ma->mounted || !ma->mount)
        return out; // archiveMounted == false
    out.archiveMounted = true;

    io::ArchiveMount& mount = *ma->mount;

    // Walk the indexed members in central-directory order. For each ".res" member,
    // extract its bytes through the reconstructed reader and BuildTextArray() into
    // the shared TextDb. Each .res carries its own baseIndex, so the parser slots
    // its strings at the right global indices; the members tile the array, leaving
    // db.Count() == maxLastIndex + 1.
    for (const io::ArchiveMember& m : mount.members()) {
        if (!EndsWithRes(m.name))
            continue;

        TextResMemberResult mr;
        mr.member = m.name;
        ++out.resMembers;

        std::vector<guild::u8> bytes;
        if (!mount.OpenMember(m.name.c_str(), bytes) || bytes.empty()) {
            out.members.push_back(std::move(mr));
            continue;
        }
        mr.bytes = bytes.size();

        gui::text::TextResFile r =
            gui::text::BuildTextArray(bytes.data(), bytes.size(), db);
        mr.ok = r.ok;
        mr.baseIndex = r.baseIndex;
        mr.lastIndex = r.lastIndex;
        mr.entryCount = r.entryCount;
        if (r.ok)
            ++out.resLoaded;

        out.members.push_back(std::move(mr));
    }

    out.entryCount = db.Count();
    return out;
}

std::vector<ResolvedString> ResolveStringKeys(const gui::text::TextDb& db,
                                              const std::vector<std::string>& keys) {
    std::vector<ResolvedString> out;
    out.reserve(keys.size());
    for (const std::string& key : keys) {
        ResolvedString rs;
        rs.key = key;
        rs.index = db.FindIndex(key.c_str());
        if (rs.index >= 0) {
            rs.found = true;
            const char* t = db.Text(rs.index);
            if (t)
                rs.text = t;
            rs.tag = db.Tag(rs.index);
        }
        out.push_back(std::move(rs));
    }
    return out;
}

RealTextDbResult MountAndLoadRealTextDb(shim::IFileSystem* fs,
                                        const std::string& gameDir,
                                        gui::text::TextDb& db,
                                        const std::string& archiveMember) {
    RealGameAssets assets = MountRealGameAssets(fs, gameDir);
    return LoadRealTextDb(assets, archiveMember, db);
}

} // namespace guild::app
