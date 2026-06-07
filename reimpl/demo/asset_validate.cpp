#include "shim_impl/disk_filesystem.h"
#include "io/zip_archive.h"
#include "compress/gzip.h"
#include <cstdio>
#include <vector>
#include <cstring>
using namespace guild;
static const char* ROOT = "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
int main() {
    shim::DiskFileSystem fs(ROOT);
    int fails = 0;

    // 1) Mount the REAL PKZIP forms.BIN with the reconstructed ZipArchive
    io::ZipArchive z;
    if (!z.Open(&fs, "Resources/forms.BIN")) { printf("FAIL: ZipArchive.Open(forms.BIN)\n"); return 1; }
    printf("forms.BIN: %u members (python said 477)\n", z.numberEntry());
    int n=0; char nm[260]; io::ZipFileInfo fi;
    for (int r=z.GoToFirstFile(); r==io::kZipOk; r=z.GoToNextFile()) {
        z.GetCurrentFileInfo(&fi, nm, sizeof nm);
        if (n<4) printf("  member[%d]=%s (csize=%u usize=%u)\n", n, nm, fi.compressedSize, fi.uncompressedSize);
        n++;
    }
    printf("iterated %d members\n", n);
    if (z.numberEntry()!=477 || n!=477) { printf("FAIL: member count\n"); fails++; }

    // 2) Extract a real .form member and sanity-check
    std::vector<u8> form;
    if (z.ExtractByName("Bauen/Geb_Bauen.form", form, false)) {
        printf("extracted Bauen/Geb_Bauen.form: %zu bytes, first dword=%u\n", form.size(),
               form.size()>=4 ? *(u32*)form.data() : 0);
    } else { printf("FAIL: extract Geb_Bauen.form\n"); fails++; }

    // 3) gilde.gfx header (loose file): u32 objectCount + first record name
    shim::IFile* gf = fs.open("gfx/gilde.gfx", "rb");
    if (gf) { u8 hdr[100]; gf->read(hdr, sizeof hdr); fs.close(gf);
        u32 cnt = *(u32*)hdr; printf("gilde.gfx: objectCount=%u (xxd said 0x070e=1806), rec0name='%s'\n", cnt, (char*)hdr+4);
        if (cnt!=1806 || strncmp((char*)hdr+4,"_WIN_BORDER",11)!=0) { printf("FAIL: gfx header\n"); fails++; }
    } else { printf("FAIL: open gilde.gfx\n"); fails++; }

    // 4) Gunzip a real city .cty with the reconstructed gzip -> INI text
    shim::IFile* cf = fs.open("Resources/gamedata/Cities/AUGSBURG.cty", "rb");
    if (cf) { std::vector<u8> gz(cf->size()); cf->read(gz.data(), gz.size()); fs.close(cf);
        std::vector<u8> ini;
        if (compress::Gunzip(gz.data(), gz.size(), ini)) {
            printf("AUGSBURG.cty: gz %zu -> %zu bytes; starts: '%.40s'\n", gz.size(), ini.size(), (char*)ini.data());
            bool looksIni = memmem(ini.data(), ini.size(), "[", 1) != nullptr;
            if (!looksIni) { printf("FAIL: city text doesn't look like INI\n"); fails++; }
        } else { printf("FAIL: Gunzip(AUGSBURG.cty)\n"); fails++; }
    } else { printf("FAIL: open AUGSBURG.cty\n"); fails++; }

    printf("\n%s (%d failures)\n", fails? "VALIDATION FAILURES":"ALL REAL-ASSET CHECKS PASSED", fails);
    return fails?1:0;
}
