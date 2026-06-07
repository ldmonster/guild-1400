// End-to-end validation of the reconstructed asset loaders against the REAL
// shipped game files (europe_guild_1400_original/). Guarded: if the asset folder
// isn't present, the test passes trivially so the suite stays green everywhere.
#include "test.h"
#include "shim_impl/disk_filesystem.h"
#include "io/zip_archive.h"
#include "compress/gzip.h"
#include <cstring>
#include <vector>

using namespace guild;

static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool assetsPresent() {
    shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/forms.BIN");
}

TEST(RealAssets, PkzipArchiveParsesRealFormsBin) {
    if (!assetsPresent()) { CHECK(true); return; } // skipped: no assets
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/forms.BIN"));
    CHECK_EQ(z.numberEntry(), 477u);            // matches PKZIP central dir
    int n = 0;
    char nm[260]; io::ZipFileInfo fi;
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        z.GetCurrentFileInfo(&fi, nm, sizeof nm);
        ++n;
    }
    CHECK_EQ(n, 477);
}

TEST(RealAssets, InflateRealFormMember) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/forms.BIN"));
    std::vector<u8> form;
    CHECK(z.ExtractByName("Bauen/Geb_Bauen.form", form, false)); // deflated member
    CHECK(form.size() > 1000);                                   // real .form inflated
}

TEST(RealAssets, GfxCatalogHeaderMatches) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    shim::IFile* gf = fs.open("gfx/gilde.gfx", "rb");
    CHECK(gf != nullptr);
    if (!gf) return;
    u8 hdr[100] = {0};
    gf->read(hdr, sizeof hdr);
    fs.close(gf);
    u32 count = 0; std::memcpy(&count, hdr, 4);
    CHECK_EQ(count, 1806u);                                  // recovered objectCount
    CHECK(std::strncmp((char*)hdr + 4, "_WIN_BORDER", 11) == 0); // first record name
}

TEST(RealAssets, GunzipRealCityFile) {
    if (!assetsPresent()) { CHECK(true); return; }
    shim::DiskFileSystem fs(kRoot);
    shim::IFile* cf = fs.open("Resources/gamedata/Cities/AUGSBURG.cty", "rb");
    CHECK(cf != nullptr);
    if (!cf) return;
    std::vector<u8> gz((std::size_t)cf->size());
    cf->read(gz.data(), gz.size());
    fs.close(cf);
    std::vector<u8> ini;
    CHECK(compress::Gunzip(gz.data(), gz.size(), ini)); // reconstructed gzip on real file
    CHECK(ini.size() > gz.size());                      // it decompressed
    // It's an INI (starts with a ';' comment; contains '[' sections somewhere).
    bool hasSection = std::memchr(ini.data(), '[', ini.size()) != nullptr;
    CHECK(hasSection);
}
