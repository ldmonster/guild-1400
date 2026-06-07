// End-to-end flow across the gui text-DB slot table + VFS IO:
//   SaveTextFile (@0x44de8c) writes a slot's TextDb range out as a compiled .res
//   through the IFileSystem-backed VFS, then ReloadTextFile (@0x44d970) reads it
//   back and rebuilds the TextDb entries — verifying the on-disk .res byte layout
//   round-trips and matches what BuildTextArray (@0x44bb5c) parses.
//
// The VFS is wired to a tiny in-memory IFileSystem mock (no OS access). A second,
// GUARDED case loads the REAL shipped Resources/textbin_deutsch.BIN and confirms a
// .res member's header agrees with the reconstructed format; it passes trivially
// when the assets are absent.
#include "test.h"

#include "gui/text/textdb.h"
#include "gui/text/textfile_table.h"

#include "io/vfs.h"
#include "io/zip_archive.h"
#include "shim/IFileSystem.h"
#include "shim_impl/disk_filesystem.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui::text;

namespace {

// Minimal in-memory IFileSystem: each path maps to a byte vector. Supports the
// read+write+seek surface the VFS exercises for .res IO.
class MemFile : public shim::IFile {
public:
    MemFile(std::vector<u8>* store, bool writing) : store_(store), writing_(writing) {
        if (writing_)
            store_->clear();
    }
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = store_->size() - pos_;
        std::size_t got = n < avail ? n : avail;
        std::memcpy(dst, store_->data() + pos_, got);
        pos_ += got;
        return got;
    }
    std::size_t write(const void* src, std::size_t n) override {
        const u8* p = static_cast<const u8*>(src);
        if (pos_ + n > store_->size())
            store_->resize(pos_ + n);
        std::memcpy(store_->data() + pos_, p, n);
        pos_ += n;
        return n;
    }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = 0;
        if (whence == SEEK_CUR) base = static_cast<std::int64_t>(pos_);
        else if (whence == SEEK_END) base = static_cast<std::int64_t>(store_->size());
        std::int64_t np = base + off;
        if (np < 0) np = 0;
        pos_ = static_cast<std::size_t>(np);
        return np;
    }
    std::int64_t tell() override { return static_cast<std::int64_t>(pos_); }
    std::int64_t size() override { return static_cast<std::int64_t>(store_->size()); }

private:
    std::vector<u8>* store_;
    std::size_t pos_ = 0;
    bool writing_;
};

class MemFs : public shim::IFileSystem {
public:
    shim::IFile* open(const char* path, const char* mode) override {
        bool writing = mode && (std::strchr(mode, 'w') || std::strchr(mode, 'W'));
        if (!writing && files_.find(path) == files_.end())
            return nullptr;
        return new MemFile(&files_[path], writing);
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }

private:
    std::map<std::string, std::vector<u8>> files_;
};

const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

bool assetsPresent() {
    guild::shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/textbin_deutsch.BIN");
}

} // namespace

TEST(GuiTextFileTableE2E, SaveReloadRoundtrip) {
    MemFs fs;
    CHECK(io::VfsInit(&fs, /*caseInsensitive=*/false));

    // Author a TextDb whose slot covers [baseIndex, lastIndex]. The save path packs
    // entries [base..last]; reload puts them back at the same indices.
    const u32 base = 3;
    TextDb src;
    while (src.Count() < static_cast<int>(base))
        src.Add("", "", kTagNone);          // filler before baseIndex
    src.Add("Hallo Welt",       "_GREET+0", kTagPlain);   // index 3
    src.Add("Danke",            "_THANKS+0", kTagPlain);  // 4
    src.Add("",                 "_EMPTY+0", kTagNone);    // 5 (empty string)
    src.Add("Drei Zeichen: abc", "_LONG+0", 11);          // 6 (random-group tag)
    const u32 last = static_cast<u32>(src.Count() - 1);   // 6

    // A slot describing that range.
    TextFileTable table;
    int slot = table.Acquire("roundtrip");
    table.At(slot).baseIndex = base;
    table.At(slot).lastIndex = last;

    // Save -> ReloadTextFile reads "textbin_german\roundtrip.res", which Save wrote
    // under "<root>\german\textbin_german\roundtrip.res". Point root so the two
    // paths coincide: root + "\german\textbin_german\roundtrip.res" must equal
    // "textbin_german\roundtrip.res" — so save under the same name the reader uses.
    // We instead save with a root chosen so the reader path matches exactly.
    // Reader path: "textbin_german\\roundtrip.res".
    // Writer path: root + "\\german\\textbin_german\\roundtrip.res".
    // Make them equal by using the MemFs which keys on the literal path, and saving
    // directly to the reader's path via a thin root that produces it.
    // Simplest: write the file the reader expects by saving with a matching layout.
    // Here we just save then copy the produced bytes under the reader's key by
    // round-tripping through the same MemFs path the reader opens.

    // Save under an explicit root; capture path equals writer path.
    CHECK(SaveTextFile(table, src, slot, "ROOT", "german"));

    // The reader opens "textbin_german\roundtrip.res". Re-save to THAT path layout
    // by using a root that yields it is awkward; instead, read the saved bytes back
    // and re-open them through the reader by writing them to the reader's path.
    // Pull the saved file out of the MemFs and re-home it under the reader key.
    {
        shim::IFile* in = fs.open("ROOT\\german\\textbin_german\\roundtrip.res", "rb");
        CHECK(in != nullptr);
        std::vector<u8> bytes(static_cast<std::size_t>(in->size()));
        if (!bytes.empty()) in->read(bytes.data(), bytes.size());
        fs.close(in);
        CHECK(!bytes.empty());

        shim::IFile* out = fs.open("textbin_german\\roundtrip.res", "wb");
        CHECK(out != nullptr);
        out->write(bytes.data(), bytes.size());
        fs.close(out);
    }

    // Reload into a fresh TextDb/table.
    TextDb dst;
    TextFileTable table2;
    int slot2 = table2.Acquire("roundtrip");
    CHECK_EQ(slot2, 0);
    CHECK(ReloadTextFile(table2, dst, "roundtrip", "german"));

    // The reloaded slot record matches.
    CHECK_EQ(table2.At(0).baseIndex, base);
    CHECK_EQ(table2.At(0).lastIndex, last);

    // Entries land back at their original indices.
    CHECK_EQ(dst.Count(), static_cast<int>(last) + 1);
    CHECK(std::strcmp(dst.Text(3), "Hallo Welt") == 0);
    CHECK(std::strcmp(dst.Text(4), "Danke") == 0);
    CHECK(std::strcmp(dst.Text(5), "") == 0);
    CHECK(std::strcmp(dst.Text(6), "Drei Zeichen: abc") == 0);

    // Names and tags survive the roundtrip.
    CHECK(std::strcmp(dst.Name(3), "_GREET+0") == 0);
    CHECK(std::strcmp(dst.Name(6), "_LONG+0") == 0);
    CHECK_EQ(dst.Tag(3), kTagPlain);
    CHECK_EQ(static_cast<int>(dst.Tag(6)), 11);

    // Name lookup over the reloaded DB works (case-insensitive).
    CHECK_EQ(dst.FindIndex("_thanks+0"), 4);

    io::VfsShutdown();
}

TEST(GuiTextFileTableE2E, RealResMemberHeaderGuarded) {
    if (!assetsPresent()) { CHECK(true); return; }  // skipped: no assets

    guild::shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/textbin_deutsch.BIN"));

    std::vector<u8> res;
    CHECK(z.ExtractByName("Text_N_Nachrichten.res", res, false));
    CHECK(res.size() > 30000u);

    // Header agrees with the reconstructed .res layout the slot reader consumes:
    // u32 entryCount @+0, u32 baseIndex @+4, u32 lastIndex @+8.
    u32 entryCount = 0, baseIndex = 0, lastIndex = 0;
    std::memcpy(&entryCount, res.data() + 0, 4);
    std::memcpy(&baseIndex,  res.data() + 4, 4);
    std::memcpy(&lastIndex,  res.data() + 8, 4);
    CHECK_EQ(entryCount, 182u);
    CHECK_EQ(lastIndex, baseIndex + entryCount - 1);
}
