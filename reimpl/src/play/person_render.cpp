// =============================================================================
// guild::play — LIVE PERSON -> CHARACTER MODEL resolution + posed-character
// support. See header for the recovered original chain.
//
// REUSED (extern, never redefined):
//   util::RandomModulo                 (0x58b89c, util/math_random.h)
//   render::LoadAnimation              (0x5e450c via agf_anim.h)
//   render::UpdateSkeletonPose         (0x5cd1d8, skeleton_pose_driver.h)
//   render::SamplePosedMeshSeg         (0x5c9394 + 0x5ca2fc, agf_anim.h)
//   render::CalculateClipNormals       (0x5d0020, anim_normals.h)
//   render::RelightPosedFrame          (0x5d0020 -> 0x5c9054 glue, anim_relight.h)
// =============================================================================
#include "play/person_render.h"

#include "render/anim_relight.h"
#include "render/skeleton_pose.h"   // MorphMeshBlock / VertexSource
#include "sim/types.h"
#include "util/math_random.h"

#include <cstring>

namespace guild::play {

namespace {

// ---------------------------------------------------------------------------
// The seven static model tables — bytes recovered verbatim with get_bytes.
// Record layout: +0 i32 code, +4 char name[32], +0x24 u8 texVariants[4].
// ---------------------------------------------------------------------------
#define SM(code, nm, t0, t1, t2, t3) { code, nm, { t0, t1, t2, t3 } }

// gilde.exe 0x63DA78 — the child-boy record (the !adult male path).
const StaffModelRecord kChildBoy =
    SM(0, "holzfaeller_SOLDAT", 0x77, 0x78, 0xff, 0xff);
// gilde.exe 0x63DAA0 — the child-girl record (the !adult female path).
const StaffModelRecord kChildGirl =
    SM(0, "Magd_FRAU", 0x79, 0x7a, 0xff, 0xff);

// gilde.exe 0x63DAC8 — male PROFESSION models (27 records; the scan stops at the
// code-0 record, so "bettler3_MANN2" is the unmatched-profession fallback).
const StaffModelRecord kMaleProfession[27] = {
    SM(0x0c, "bergmann3_MANN",          0x51, 0xff, 0xff, 0xff),
    SM(0x0a, "bergmann2_MANN",          0x50, 0xff, 0xff, 0xff),
    SM(0x0b, "bergmann3_MANN",          0x51, 0xff, 0xff, 0xff),
    SM(0x0a, "bergmann2_MANN",          0x50, 0xff, 0xff, 0xff),
    SM(0x09, "holzfaeller_SOLDAT",      0x47, 0x48, 0xff, 0xff),
    SM(0x0a, "stallbursche_MANN",       0x66, 0x67, 0xff, 0xff),
    SM(0x11, "handwerker2_MANN",        0x39, 0x3a, 0x3b, 0x3c),
    SM(0x12, "handwerker_MANN",         0x3b, 0x3c, 0x39, 0x3a),
    SM(0x17, "handwerker2_MANN",        0x39, 0x3a, 0x3b, 0x3c),
    SM(0x18, "handwerker_MANN",         0x3b, 0x3c, 0x39, 0x3a),
    SM(0x15, "handwerker2_MANN",        0x39, 0x3a, 0x3b, 0x3c),
    SM(0x16, "handwerker_MANN",         0x3b, 0x3c, 0x39, 0x3a),
    SM(0x19, "wirt_DICKER",             0x6b, 0x6c, 0xff, 0xff),
    SM(0x1a, "handwerker_MANN",         0x3b, 0x3c, 0x39, 0x3a),
    SM(0x04, "priester_KUTTE",          0x5d, 0x5e, 0x5f, 0xff),
    SM(0x03, "abt_KUTTE",               0x03, 0x01, 0x02, 0x00),
    SM(0x0e, "buerger2_MANN",           0x19, 0x1a, 0xff, 0xff),
    SM(0x0d, "buerger2_MANN",           0x19, 0x1a, 0xff, 0xff),
    SM(0x01, "zigeuner_MANN2",          0x71, 0x72, 0x73, 0xff),
    SM(0x02, "dieb_MANN2",              0x29, 0x2a, 0x15, 0x16),
    SM(0x0f, "raeubergeselle_SOLDAT",   0x7c, 0xff, 0xff, 0xff),
    SM(0x10, "raeuberlehrling_SOLDAT",  0x7b, 0xff, 0xff, 0xff),
    SM(0x13, "fechter_SOLDAT",          0x31, 0x32, 0xff, 0xff),
    SM(0x14, "musketenschuetze_SOLDAT", 0x54, 0xff, 0xff, 0xff),
    SM(0x07, "holzfaeller_SOLDAT",      0x47, 0x48, 0xff, 0xff),
    SM(0x08, "stallbursche_MANN",       0x66, 0x67, 0xff, 0xff),
    SM(0x00, "bettler3_MANN2",          0x15, 0x16, 0xff, 0xff),
};

// gilde.exe 0x63DF00 — female PROFESSION models (27 records; the code-0
// "minerin_FRAU" record is the unmatched fallback; records 7..26 are zero).
const StaffModelRecord kFemaleProfession[27] = {
    SM(0x0e, "handwerkerin_FRAU",       0x3f, 0x40, 0x74, 0x75),
    SM(0x0d, "arbeiterin_FRAU",         0x08, 0x3f, 0x53, 0xff),
    SM(0x05, "handwerkerin_FRAU",       0x3f, 0x40, 0x74, 0x75),
    SM(0x06, "arbeiterin_FRAU",         0x08, 0x3f, 0x53, 0xff),
    SM(0x19, "handwerkerin_FRAU",       0x3f, 0x40, 0x74, 0x75),
    SM(0x1a, "arbeiterin_FRAU",         0x08, 0x3f, 0x53, 0xff),
    SM(0x00, "minerin_FRAU",            0x52, 0xff, 0xff, 0xff),
    // remaining 20 records are all-zero in the binary
};

// gilde.exe 0x63E338 — male OFFICE models (76 records, terminator at index 75).
const StaffModelRecord kMaleOffice[76] = {
    SM(0x21, "feldarbeiter_SOLDAT",     0x33, 0x34, 0x35, 0xff),
    SM(0x20, "feldarbeiter_SOLDAT",     0x33, 0x34, 0x35, 0xff),
    SM(0x1f, "feldarbeiter_SOLDAT",     0x33, 0x34, 0x35, 0xff),
    SM(0x2e, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x2f, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x30, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x31, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x32, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x33, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x40, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x41, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x42, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x43, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x44, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x45, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x3a, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x3b, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x3c, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x3d, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x3e, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x3f, "arbeiter_MANN",           0x04, 0xff, 0xff, 0xff),
    SM(0x46, "wirt2_DICKER",            0x6d, 0x6e, 0xff, 0xff),
    SM(0x47, "wirt2_DICKER",            0x6d, 0x6e, 0xff, 0xff),
    SM(0x48, "wirt2_DICKER",            0x6d, 0x6e, 0xff, 0xff),
    SM(0x49, "wirt2_DICKER",            0x6d, 0x6e, 0xff, 0xff),
    SM(0x4a, "wirt2_DICKER",            0x6d, 0x6e, 0xff, 0xff),
    SM(0x4b, "wirt2_DICKER",            0x6d, 0x6e, 0xff, 0xff),
    SM(0x0d, "abt_KUTTE",               0x03, 0x01, 0x02, 0x00),
    SM(0x0e, "abt_KUTTE",               0x03, 0x01, 0x02, 0x00),
    SM(0x0f, "abt_KUTTE",               0x03, 0x01, 0x02, 0x00),
    SM(0x10, "abt_KUTTE",               0x03, 0x01, 0x02, 0x00),
    SM(0x11, "abt_KUTTE",               0x03, 0x01, 0x02, 0x00),
    SM(0x12, "abt_KUTTE",               0x03, 0x01, 0x02, 0x00),
    SM(0x22, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x23, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x24, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x25, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x26, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x27, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x13, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x14, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x15, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x16, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x17, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x18, "bauer_MANN2",             0x0c, 0x0d, 0x0e, 0xff),
    SM(0x01, "dieb4_KUTTE",             0x2b, 0xff, 0xff, 0xff),
    SM(0x02, "dieb4_KUTTE",             0x2b, 0xff, 0xff, 0xff),
    SM(0x03, "dieb4_KUTTE",             0x2b, 0xff, 0xff, 0xff),
    SM(0x04, "dieb4_KUTTE",             0x2b, 0xff, 0xff, 0xff),
    SM(0x05, "dieb4_KUTTE",             0x2b, 0xff, 0xff, 0xff),
    SM(0x06, "dieb4_KUTTE",             0x2b, 0xff, 0xff, 0xff),
    SM(0x28, "raeubermeister_SOLDAT",   0x55, 0x56, 0x45, 0x46),
    SM(0x29, "raeubermeister_SOLDAT",   0x55, 0x56, 0x45, 0x46),
    SM(0x2a, "raeubermeister_SOLDAT",   0x55, 0x56, 0x45, 0x46),
    SM(0x2b, "raeubermeister_SOLDAT",   0x55, 0x56, 0x45, 0x46),
    SM(0x2c, "raeubermeister_SOLDAT",   0x55, 0x56, 0x45, 0x46),
    SM(0x2d, "raeubermeister_SOLDAT",   0x55, 0x56, 0x45, 0x46),
    SM(0x34, "offizier_SOLDAT",         0x55, 0x56, 0x45, 0x46),
    SM(0x35, "offizier_SOLDAT",         0x55, 0x56, 0x45, 0x46),
    SM(0x36, "offizier_SOLDAT",         0x55, 0x56, 0x45, 0x46),
    SM(0x37, "offizier_SOLDAT",         0x55, 0x56, 0x45, 0x46),
    SM(0x38, "offizier_SOLDAT",         0x55, 0x56, 0x45, 0x46),
    SM(0x39, "offizier_SOLDAT",         0x55, 0x56, 0x45, 0x46),
    SM(0x19, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x1a, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x1b, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x1c, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x1d, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x1e, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x07, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x08, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x09, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x0a, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x0b, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x0c, "buerger3_DICKER",         0x1b, 0x1c, 0x1d, 0x1e),
    SM(0x00, "bettler_KUTTE",           0x13, 0xff, 0xff, 0xff),
};

// gilde.exe 0x63EF18 — female OFFICE models (76 records; terminator at index 69,
// records 70..75 are zero).
const StaffModelRecord kFemaleOffice[76] = {
    SM(0x21, "feldarbeiterin_FRAU",     0x36, 0x37, 0xff, 0xff),
    SM(0x20, "feldarbeiterin_FRAU",     0x36, 0x37, 0xff, 0xff),
    SM(0x1f, "holzfaellerin_FRAU",      0x49, 0xff, 0xff, 0xff),
    SM(0x2e, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x2f, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x30, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x31, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x32, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x33, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x40, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x41, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x42, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x43, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x44, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x45, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x3a, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x3b, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x3c, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x3d, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x3e, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x3f, "handwerkerin3_FRAU",      0x43, 0x44, 0xff, 0xff),
    SM(0x46, "magd_FRAU",               0x4d, 0x4e, 0xff, 0xff),
    SM(0x47, "magd_FRAU",               0x4d, 0x4e, 0xff, 0xff),
    SM(0x48, "magd_FRAU",               0x4d, 0x4e, 0xff, 0xff),
    SM(0x49, "magd_FRAU",               0x4d, 0x4e, 0xff, 0xff),
    SM(0x4a, "magd_FRAU",               0x4d, 0x4e, 0xff, 0xff),
    SM(0x4b, "magd_FRAU",               0x4d, 0x4e, 0xff, 0xff),
    SM(0x22, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x23, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x24, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x25, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x26, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x27, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x13, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x14, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x15, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x16, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x17, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x18, "bauerin_FRAU",            0x12, 0x08, 0xff, 0xff),
    SM(0x01, "zigeunerin_FRAU",         0x74, 0x75, 0x76, 0xff),
    SM(0x02, "zigeunerin_FRAU",         0x74, 0x75, 0x76, 0xff),
    SM(0x03, "zigeunerin_FRAU",         0x74, 0x75, 0x76, 0xff),
    SM(0x04, "zigeunerin_FRAU",         0x74, 0x75, 0x76, 0xff),
    SM(0x05, "zigeunerin_FRAU",         0x74, 0x75, 0x76, 0xff),
    SM(0x06, "zigeunerin_FRAU",         0x74, 0x75, 0x76, 0xff),
    SM(0x28, "minerin2_FRAU",           0x53, 0xff, 0xff, 0xff),
    SM(0x29, "minerin2_FRAU",           0x53, 0xff, 0xff, 0xff),
    SM(0x2a, "minerin2_FRAU",           0x53, 0xff, 0xff, 0xff),
    SM(0x2b, "minerin2_FRAU",           0x53, 0xff, 0xff, 0xff),
    SM(0x2c, "minerin2_FRAU",           0x53, 0xff, 0xff, 0xff),
    SM(0x2d, "minerin2_FRAU",           0x53, 0xff, 0xff, 0xff),
    SM(0x34, "buergerin_FRAU",          0x1f, 0x20, 0x21, 0xff),
    SM(0x35, "buergerin_FRAU",          0x1f, 0x20, 0x21, 0xff),
    SM(0x36, "buergerin_FRAU",          0x1f, 0x20, 0x21, 0xff),
    SM(0x37, "buergerin_FRAU",          0x1f, 0x20, 0x21, 0xff),
    SM(0x38, "buergerin_FRAU",          0x1f, 0x20, 0x21, 0xff),
    SM(0x39, "buergerin_FRAU",          0x1f, 0x20, 0x21, 0xff),
    SM(0x19, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x1a, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x1b, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x1c, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x1d, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x1e, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x07, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x08, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x09, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x0a, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x0b, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x0c, "buergerin2_FRAU",         0x22, 0x23, 0x24, 0xff),
    SM(0x00, "minerin_FRAU",            0x52, 0xff, 0xff, 0xff),
    // records 70..75 are all-zero in the binary
};

// gilde.exe 0x6405E8 — the kind-17 (reaper/spy) random trio.
const StaffModelRecord kReaper[3] = {
    SM(0, "spion_MEDIUM",  0x7b, 0xff, 0xff, 0xff),
    SM(0, "spion2_MEDIUM", 0x7c, 0xff, 0xff, 0xff),
    SM(0, "spion2_MEDIUM", 0x04, 0xff, 0xff, 0xff),
};

#undef SM

// The rotating 8-record scratch (dword_641FE8 cursor + unk_1234610 records) the
// stored-name path returns through.
int               g_scratchCursor = 0;     // dword_641FE8
StaffModelRecord  g_scratch[8] = {};       // unk_1234610 (8 * 40 bytes)

inline char ToLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

bool EqualsCaseInsensitive(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (ToLowerAscii(a[i]) != ToLowerAscii(b[i])) return false;
    return true;
}

} // namespace

const StaffModelRecord& StaffChildBoyModel()          { return kChildBoy; }
const StaffModelRecord& StaffChildGirlModel()         { return kChildGirl; }
const StaffModelRecord* StaffMaleProfessionTable()    { return kMaleProfession; }
const StaffModelRecord* StaffFemaleProfessionTable()  { return kFemaleProfession; }
const StaffModelRecord* StaffMaleOfficeTable()        { return kMaleOffice; }
const StaffModelRecord* StaffFemaleOfficeTable()      { return kFemaleOffice; }
const StaffModelRecord* StaffReaperTable()            { return kReaper; }

void ResetStaffModelScratch() {
    g_scratchCursor = 0;
    std::memset(g_scratch, 0, sizeof g_scratch);
}

PersonModelView MakePersonModelView(const sim::Person* rec) {
    PersonModelView v;
    if (!rec) return v;
    const u8* b = reinterpret_cast<const u8*>(rec);
    v.kind   = b[0x02];                       // byte_12CE912
    v.gender = b[0x09];                       // LOBYTE(dword_12CE919)
    std::memcpy(&v.adultWord, b + 0x0A, 2);   // (u16)((char*)&dword_12CE919 + 1)
    std::memcpy(&v.adultThreshold, b + 0x20, 4); // flt_12CE930
    v.office     = (i8)b[0x164];              // byte_12CEA74
    v.profession = (i8)b[0x165];              // byte_12CEA75
    std::memcpy(&v.texSet, b + 0x18C, 4);     // dword_12CEA9C
    v.storedName = reinterpret_cast<const char*>(b + 0x1F0); // byte_12CEB00
    return v;
}

// gilde.exe 0x57c1e8 — VIBE_Office_ResolveStaffModel (1:1; see header).
const StaffModelRecord* ResolveStaffModel(const PersonModelView& v,
                                          StaffRandModulo rnd) {
    const StaffModelRecord* v2 = nullptr;   // the gender-selected table base
    int v3 = 0;                             // the scan cursor

    // 0x57c20c..0x57c2f3 — the adult gate: office byte set OR the +0x0A word is
    // at/above the +0x20 float threshold. NOT adult -> the child records.
    if (!(v.office != 0 ||
          (double)v.adultWord >= (double)v.adultThreshold)) {
        // 0x57c2f9/0x57c302/0x57c311 — gender low byte picks the child record.
        return v.gender ? &kChildGirl : &kChildBoy;
    }

    // 0x57c23d — the stored-name path: +0x1F0 first byte set AND +0x18C != -1.
    if (v.storedName && v.storedName[0] != 0 && v.texSet != -1) {
        // 0x57c25e — advance the 8-slot rotation (dword_641FE8).
        g_scratchCursor = (g_scratchCursor + 1) % 8;
        StaffModelRecord& s = g_scratch[g_scratchCursor];
        // 0x57c27c..0x57c29c — copy the person's stored name into the scratch
        // record's name field (the engine's unbounded 2-byte copy; the source
        // field is bounded by the record so the copy stays inside the 36-byte
        // name+variant span exactly as in the original).
        char* dst = s.name;
        const char* src = v.storedName;
        std::size_t i = 0;
        for (; src[i] != 0 && i < sizeof(s.name) + sizeof(s.texVariants) - 1; ++i)
            dst[i] = src[i];
        dst[i] = 0;
        // 0x57c2a5..0x57c2c8 — texVariants[0..3] = LOBYTE(texSet) + 68.
        const u8 t = (u8)((u8)v.texSet + 68);
        s.texVariants[0] = t; s.texVariants[1] = t;
        s.texVariants[2] = t; s.texVariants[3] = t;
        return &s;                          // 0x57c2cf
    }

    // 0x57c336 — kind 17 (reaper/spy): random of the three spion records.
    if (v.kind == 17) {
        int r = rnd ? rnd(3) : util::RandomModulo(3);
        return &kReaper[(u16)r % 3u];       // 0x57c3bd (40 * RandomModulo(3))
    }

    // 0x57c338 — OFFICE table scan (<= 76 records, stop at code 0).
    if (v.office != 0) {
        const u8 g = v.gender;              // v15 @0x57c345
        if (g == 0)      v2 = kMaleOffice;     // unk_63E338
        else if (g == 1) v2 = kFemaleOffice;   // unk_63EF18
        if (!v2)
            return nullptr;  // gender outside {0,1}: the original dereferences
                             // a null table base here (unreachable live data)
        for (const StaffModelRecord* r = v2; v3 < 76 && r->code; ++r) {
            if ((i32)v.office == r->code)   // signed-byte compare @0x57c388
                return r;
            ++v3;
        }
        return &v2[v3];                     // 0x57c3e1 — the stop record
    }

    // 0x57c3e2 — PROFESSION table scan (<= 27 records, stop at code 0).
    if (v.profession != 0) {
        const u8 g = v.gender;              // v16 @0x57c3eb
        if (g == 0)      v2 = kMaleProfession;   // unk_63DAC8
        else if (g == 1) v2 = kFemaleProfession; // unk_63DF00
        if (!v2)
            return nullptr;  // same null-base note as the office branch
        for (const StaffModelRecord* r = v2; v3 < 27 && r->code; ++r) {
            if ((i32)v.profession == r->code)
                return r;                   // 0x57c42d
            ++v3;
        }
        if (v3 < 27)
            return &v2[v3];                 // 0x57c3e1 — the code-0 fallback
        // v3 >= 27 falls through to the gender default (LABEL_39).
    }

    // LABEL_39 (0x57c459) — no office/profession (or a 27-deep profession miss):
    // the gender's profession-table BASE record is the default model.
    if (v.gender == 0) return kMaleProfession;     // 0x57c463 (&unk_63DAC8)
    if (v.gender == 1) return kFemaleProfession;   // 0x57c47e (&unk_63DF00)
    return v2;                                     // 0x57c473 (null)
}

// ---------------------------------------------------------------------------
// Asset-name mapping.
// ---------------------------------------------------------------------------
std::string CharacterMeshMemberName(const char* modelName) {
    std::string s = "_DYNAMIC/Character/";
    s += (modelName ? modelName : "");
    s += ".bgf";
    return s;
}

std::string CharacterAnimMemberName(const char* baseName, const char* clipName) {
    // VIBE_Character_PreloadAniSet @0x403c34: "character/%s/%s_%s.baf".
    std::string s = "character/";
    s += (baseName ? baseName : "");
    s += "/";
    s += (clipName ? clipName : "");
    s += "_";
    s += (baseName ? baseName : "");
    s += ".baf";
    return s;
}

std::string FindMemberCaseInsensitive(const std::vector<io::ArchiveMember>& members,
                                      const std::string& wanted) {
    for (const auto& m : members)
        if (EqualsCaseInsensitive(m.name, wanted))
            return m.name;
    return std::string();
}

// ---------------------------------------------------------------------------
// PersonCharacterPose.
// ---------------------------------------------------------------------------
bool PersonCharacterPose::LoadClip(const u8* data, std::size_t size,
                                   const char* name) {
    poseable_ = false;
    if (!render::LoadAnimation(data, size, name ? name : "clip", clip_,
                               /*loadFlag=*/1))
        return false;
    if (!clip_.valid || clip_.FrameCount() <= 0)
        return false;

    // Per-frame segment durations from the loaded frames (AnimFrame +4).
    durations_.clear();
    durations_.reserve((std::size_t)clip_.FrameCount());
    for (int f = 0; f < clip_.FrameCount(); ++f) {
        i32 d = (f < (int)clip_.anim.frames.size())
                    ? clip_.anim.frames[(std::size_t)f].duration : 1;
        durations_.push_back(d > 0 ? d : 1);
    }

    // One-layer pose-driver state: forward LOOP playback (the anim-set play
    // mode the factory's preloaded gait/idle clips use), track 0 active.
    layer_ = render::SkeletonPoseState::Layer{};
    layer_.active = true;
    render::PoseTrack& t = layer_.tracks[0];
    t.animHeaderId = 1;                // header present (+104)
    t.flags = 0x02;                    // +110 bit1: track serviced/active
    t.mode  = 0x01;                    // +109 bit0: loop
    t.fromFrame = clip_.StartFrame();
    t.toFrame   = clip_.StartFrame();
    t.phase     = 0;
    t.expiry    = 0xFFFFFFFFu;         // no activity-expiry boundary
    render::PoseAnimHeader& hd = layer_.headers[0];
    hd.frameCount   = clip_.FrameCount();
    hd.startFrame   = clip_.StartFrame();
    hd.endFrame     = clip_.EndFrame();
    hd.deltaEncoded = (clip_.anim.header.flag361 != 0);
    hd.durations    = durations_.data();

    st_ = render::SkeletonPoseState{};
    st_.hasDrawData = true;
    st_.layers = &layer_;
    st_.layerCount = 1;
    timeCursor_ = 0;
    return true;
}

bool PersonCharacterPose::BindMesh(const render::MeshGeometry* rest) {
    poseable_ = false;
    rest_ = rest;
    if (!clip_.valid || !rest || !rest->vertices || !rest->polygons ||
        rest->vertexCount <= 0 || rest->polyCount <= 0)
        return false;
    // The .baf is a full-vertex morph stream; the pose substitutes every mesh
    // vertex, so the counts must agree (else the caller draws the rest pose).
    if (clip_.VertexCount() != rest->vertexCount)
        return false;

    // Topology as vertex-index triples (polygon pointers -> indices).
    triangles_.clear();
    triangles_.reserve((std::size_t)rest->polyCount);
    for (int p = 0; p < rest->polyCount; ++p) {
        const render::Polygon& pl = rest->polygons[p];
        if (!pl.v0 || !pl.v1 || !pl.v2) continue;
        std::array<int, 3> tri = {
            (int)(pl.v0 - rest->vertices),
            (int)(pl.v1 - rest->vertices),
            (int)(pl.v2 - rest->vertices),
        };
        if (tri[0] < 0 || tri[0] >= rest->vertexCount ||
            tri[1] < 0 || tri[1] >= rest->vertexCount ||
            tri[2] < 0 || tri[2] >= rest->vertexCount)
            continue;
        triangles_.push_back(tri);
    }

    // Load-time per-frame normals + smoothed bounds (the engine runs
    // VIBE_Anim_CalculateAnimNormals once per clip at load).
    frameNormals_.clear();
    frameBounds_.clear();
    render::CalculateClipNormals(clip_, triangles_, frameNormals_, frameBounds_);

    // The per-vertex +77 lit byte (the relight walk's gate), captured from the
    // rest mesh's raw vertex records.
    litFlags_.assign((std::size_t)rest->vertexCount, 0);
    for (int i = 0; i < rest->vertexCount; ++i)
        litFlags_[(std::size_t)i] =
            reinterpret_cast<const u8*>(&rest->vertices[i])[77];

    poseable_ = true;
    return true;
}

void PersonCharacterPose::Advance(float stepTicks) {
    if (!clip_.valid) return;
    // The driver folds the integer part of the fractional phase (the +96 rate
    // is host-supplied — the documented gap; we pre-load the increment).
    layer_.tracks[0].phaseFrac += stepTicks;
    timeCursor_ += 1;
    render::SkeletonPoseHooks hooks;   // inert defaults (headless side effects)
    st_.layers = &layer_;
    st_.layerCount = 1;
    render::UpdateSkeletonPose(st_, hooks, timeCursor_);
    // Looping playback keeps the track serviced across boundaries (the live
    // engine re-arms it from the action queue; the idle/gait set loops).
    layer_.tracks[0].flags |= 0x02;
}

render::MeshGeometry* PersonCharacterPose::SamplePosed() {
    if (!poseable_ || !rest_) return nullptr;

    const render::PoseTrack& t = layer_.tracks[0];
    int from = t.fromFrame;
    if (from < 0) from = 0;
    if (from >= clip_.FrameCount()) from = clip_.FrameCount() - 1;
    // The engine samples (fromFrame, phase) — the blend target is the next
    // frame of the active direction; weight = phase / segmentDuration
    // (VIBE_Anim_ComputeMorphWeights' w).
    int to = from + 1;
    if (to > clip_.EndFrame()) to = clip_.EndFrame();
    if (to >= clip_.FrameCount()) to = clip_.FrameCount() - 1;
    i32 segDur = durations_[(std::size_t)from];
    float w = segDur > 0 ? (float)t.phase / (float)segDur : 0.0f;
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;

    render::PosedMesh pm = render::SamplePosedMeshSeg(clip_, from, to, w);
    if (!pm.valid || pm.vertexCount != rest_->vertexCount)
        return nullptr;

    // Posed vertex array: rest vertices (UV/light/flags) with posed positions.
    posedVerts_.assign(rest_->vertices, rest_->vertices + rest_->vertexCount);
    for (int i = 0; i < rest_->vertexCount; ++i) {
        posedVerts_[(std::size_t)i].x = pm.points[(std::size_t)i * 3 + 0];
        posedVerts_[(std::size_t)i].y = pm.points[(std::size_t)i * 3 + 1];
        posedVerts_[(std::size_t)i].z = pm.points[(std::size_t)i * 3 + 2];
    }

    // Per-frame relight: push the frame's recomputed normals into the vertex
    // sources, then run the env-map lighting walk (gated per vertex by the +77
    // lit byte, exactly as the engine's walk is).
    if ((std::size_t)from < frameNormals_.size()) {
        posedSources_.assign((std::size_t)rest_->vertexCount,
                             render::VertexSource{});
        for (int i = 0; i < rest_->vertexCount; ++i) {
            render::VertexSource& s = posedSources_[(std::size_t)i];
            s.pos[0] = posedVerts_[(std::size_t)i].x;
            s.pos[1] = posedVerts_[(std::size_t)i].y;
            s.pos[2] = posedVerts_[(std::size_t)i].z;
        }
        render::MorphMeshBlock block;
        block.vertices    = posedVerts_.data();
        block.sources     = posedSources_.data();
        block.litFlags    = litFlags_.data();
        block.vertexCount = rest_->vertexCount;
        static const float kIdentity3x3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        // NOTE: RelightPosedFrame writes the frame normals into the (mutable)
        // sources, then relights; sources here are this frame's scratch.
        render::MorphMeshBlock mutableBlock = block;
        render::RelightPosedFrame(mutableBlock,
                                  frameNormals_[(std::size_t)from],
                                  kIdentity3x3);
    }

    // Rebind the rest polygons onto the posed vertices.
    posedPolys_.assign(rest_->polygons, rest_->polygons + rest_->polyCount);
    for (int p = 0; p < rest_->polyCount; ++p) {
        render::Polygon& pl = posedPolys_[(std::size_t)p];
        if (pl.v0) pl.v0 = posedVerts_.data() + (pl.v0 - rest_->vertices);
        if (pl.v1) pl.v1 = posedVerts_.data() + (pl.v1 - rest_->vertices);
        if (pl.v2) pl.v2 = posedVerts_.data() + (pl.v2 - rest_->vertices);
    }

    posedGeom_.vertices    = posedVerts_.data();
    posedGeom_.polygons    = posedPolys_.data();
    posedGeom_.polyCount   = (i32)posedPolys_.size();
    posedGeom_.polyCap     = (i32)posedPolys_.size();
    posedGeom_.vertexCount = (i32)posedVerts_.size();
    return &posedGeom_;
}

} // namespace guild::play
