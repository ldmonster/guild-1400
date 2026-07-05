#include "render/bone_palette.h"

#include "util/math.h"     // VectorLerp
#include "util/matrix.h"   // MatrixFromEuler, MatrixToEuler

#include <cstring>

namespace guild::render {

namespace {

// VIBE_Util_StrCmp (0x5d3f10) returns 0 on match. We compare NUL-terminated names.
inline bool NameEq(const char* a, const char* b) {
    return std::strcmp(a, b) == 0;
}

// Fold one keyframe pair of a track into a palette record. `rec` is the target
// record; `first` is true when it was just created (its refT/rows are virgin and
// the accumulation OVERWRITES rather than adds, exactly as the two code paths in
// the original differ). `refT` seeds a new record's +84 reference column.
//
// The original reads, per track:
//   from-frame translation @ frames[from] + 84 + 24*boneIndex  (3 floats)
//   to-frame   translation @ frames[to]   + 84 + 24*boneIndex
//   blend t = phaseNum / frames[from].duration                  (24*boneIndex+4)
//   Euler   @ +12 within the same 24-byte bone block (frames+84+24*idx+12)
// VectorLerp blends the from/to translation; (lerp - refT)*weight accumulates into
// accT; MatrixFromEuler builds from/to rotation rows, VectorLerp blends each of the
// 3 rows by the same t, and the rows accumulate.
void FoldTrack(BonePaletteRecord& rec, const BoneTrack& tr, bool first) {
    const AnimFrame& ff = tr.frames[tr.fromFrame];
    const AnimFrame& tf = tr.frames[tr.toFrame];

    // Per-frame bone translation triple lives at frame+84 + 24*boneIndex. The
    // AnimFrame named fields end at +56; the bone-attach blocks follow in the tail.
    // The first bone (index 0) block's translation is the frame tx/ty/tz @+32 in
    // the loader's layout, but the engine reads the +84-based attach array; we read
    // it from the AnimFrame tail via a byte view to stay layout-faithful.
    auto attachTrans = [](const AnimFrame& f, int boneIndex) -> const float* {
        const u8* base = reinterpret_cast<const u8*>(&f);
        return reinterpret_cast<const float*>(base + 84 + 24 * boneIndex);
    };
    auto attachEuler = [](const AnimFrame& f, int boneIndex) -> const float* {
        const u8* base = reinterpret_cast<const u8*>(&f);
        return reinterpret_cast<const float*>(base + 84 + 24 * boneIndex + 12);
    };

    double t = (double)tr.phaseNum / (double)ff.duration;

    float lerpT[3];
    {
        float a[3], b[3];
        const float* fa = attachTrans(ff, tr.boneIndex);
        const float* fb = attachTrans(tf, tr.boneIndex);
        a[0] = fa[0]; a[1] = fa[1]; a[2] = fa[2];
        b[0] = fb[0]; b[1] = fb[1]; b[2] = fb[2];
        guild::util::VectorLerp(a, b, (float)t, lerpT);
    }

    // gilde.exe 0x5cc0d0 accumulation (HARDEN fix, both paths were wrong):
    //   NEW record (0x5cc4xx): delta = lerp - refT(+84);
    //                          accT = weight*delta; then accT += refT
    //                          -> accT = refT + weight*(lerp - refT).
    //   EXISTING record (0x5cc25x): delta = lerp - CURRENT accT(+68);
    //                          accT += weight*delta   (relaxation toward lerp).
    if (first) {
        float d0 = lerpT[0] - rec.refT[0];
        float d1 = lerpT[1] - rec.refT[1];
        float d2 = lerpT[2] - rec.refT[2];
        rec.accT[0] = tr.weight * d0;
        rec.accT[1] = tr.weight * d1;
        rec.accT[2] = tr.weight * d2;
        rec.accT[0] = rec.accT[0] + rec.refT[0];   // v52 = accT + refT
        rec.accT[1] = rec.accT[1] + rec.refT[1];
        rec.accT[2] = rec.accT[2] + rec.refT[2];
    } else {
        // delta stored to float (v86 fstp), then weight*delta + accT is one
        // 80-bit chain with a single store — modeled with double.
        float d0 = lerpT[0] - rec.accT[0];
        float d1 = lerpT[1] - rec.accT[1];
        float d2 = lerpT[2] - rec.accT[2];
        rec.accT[0] = (float)((double)tr.weight * d0 + rec.accT[0]);
        rec.accT[1] = (float)((double)tr.weight * d1 + rec.accT[1]);
        rec.accT[2] = (float)((double)tr.weight * d2 + rec.accT[2]);
    }

    // Rotation rows: build from/to 4x4 from the per-frame Euler triple, blend each
    // of the 3 rotation rows by t, accumulate into rec.rows (3 rows of 4 floats).
    float mFrom[16], mTo[16];
    guild::util::MatrixFromEuler(attachEuler(ff, tr.boneIndex), mFrom);
    guild::util::MatrixFromEuler(attachEuler(tf, tr.boneIndex), mTo);
    for (int r = 0; r < 3; ++r) {
        float row[4];
        guild::util::VectorLerp(mFrom + 4 * r, mTo + 4 * r, (float)t, row);
        if (first) {
            rec.rows[4 * r + 0] = row[0];
            rec.rows[4 * r + 1] = row[1];
            rec.rows[4 * r + 2] = row[2];
            rec.rows[4 * r + 3] = row[3];
        } else {
            rec.rows[4 * r + 0] += row[0];
            rec.rows[4 * r + 1] += row[1];
            rec.rows[4 * r + 2] += row[2];
            rec.rows[4 * r + 3] += row[3];
        }
    }
}

} // namespace

// gilde.exe 0x5cc0d0 — VIBE_Anim_ComputeBoneMatrices (palette build core).
i32 ComputeBoneMatrices(const BoneGroup* groups, i32 groupCount,
                        const BoneNameEntry* names, i32 nameCount,
                        BonePaletteRecord* out, PaletteApplyFn apply, void* ctx) {
    // Clear the 4-record palette scratch (the memset(v82,0,656) prologue).
    std::memset(out, 0, sizeof(BonePaletteRecord) * 4);

    int recCount = 0;  // v2: number of distinct bone-name records (<= 4)

    // Outer: up to 4 animation groups; inner: 3 tracks each (the j != 348 / +=116
    // loop). Only groups whose gate byte is set contribute (modelled by groupCount).
    for (i32 g = 0; g < groupCount && g < 4 + 4; ++g) {
        const BoneGroup& grp = groups[g];
        for (int ti = 0; ti < 3; ++ti) {
            const BoneTrack& tr = grp.tracks[ti];
            if (!tr.active) continue;            // *(v5+104) == 0  -> skip
            if (tr.boneIndex == 0xFF) continue;  // *(v5+112) == 0xFF -> inactive
            if (!tr.name) continue;

            // Find an existing record with this bone name (v14 scan, stride 41 dwords
            // == 164 bytes). StrCmp returns 0 on match.
            int found = recCount;
            for (int k = 0; k < recCount; ++k) {
                if (NameEq(tr.name, out[k].name)) { found = k; break; }
            }

            if (found >= recCount) {
                // New record (only if v2 < 4 — the 4-record cap).
                if (recCount >= 4) continue;
                BonePaletteRecord& rec = out[recCount];
                // Copy the bone name (the 2-bytes-at-a-time copy until NUL).
                std::strncpy(rec.name, tr.name, sizeof(rec.name) - 1);
                rec.name[sizeof(rec.name) - 1] = '\0';
                rec.count = 0;

                // Seed the reference translation from the matching bone-name table
                // entry (the +180 lookup), else leave zero.
                for (i32 n = 0; n < nameCount; ++n) {
                    if (NameEq(tr.name, names[n].name)) {
                        rec.refT[0] = names[n].refT[0];
                        rec.refT[1] = names[n].refT[1];
                        rec.refT[2] = names[n].refT[2];
                        break;
                    }
                }

                FoldTrack(rec, tr, /*first=*/true);
                rec.count = 1;     // *(v82[v97+64]) = 1
                ++recCount;
            } else {
                // Existing record: accumulate this track's contribution.
                BonePaletteRecord& rec = out[found];
                FoldTrack(rec, tr, /*first=*/false);
                ++rec.count;       // ++*(v82[..+64])
            }
        }
    }

    // Average + decompose + push each record (the loc_5CC786 tail). For each record:
    //   rows *= 1/count (in place), MatrixToEuler(rows) -> euler in rows[0..2],
    //   then the scene-graph child push via apply().
    for (int k = 0; k < recCount; ++k) {
        BonePaletteRecord& rec = out[k];
        double inv = (rec.count != 0) ? 1.0 / (double)rec.count : 0.0;
        for (int r = 0; r < 3; ++r) {
            rec.rows[4 * r + 0] = (float)((double)rec.rows[4 * r + 0] * inv);
            rec.rows[4 * r + 1] = (float)((double)rec.rows[4 * r + 1] * inv);
            rec.rows[4 * r + 2] = (float)((double)rec.rows[4 * r + 2] * inv);
        }
        // MatrixToEuler overwrites the first three floats of the rows block with the
        // recovered Euler angles (it operates on the flat 16-float matrix at +100).
        // rec.rows is 12 floats (3 rows of 4); MatrixToEuler reads/writes within it.
        float m[16] = {0};
        for (int idx = 0; idx < 12; ++idx) m[idx] = rec.rows[idx];
        m[15] = 1.0f;
        guild::util::MatrixToEuler(m);
        rec.rows[0] = m[0];
        rec.rows[1] = m[1];
        rec.rows[2] = m[2];

        if (apply) {
            // SetPosition(translation @rec+68 == accT) + SetWorldTranslation(euler).
            apply(ctx, rec.name, rec.accT, rec.rows);
        }
    }

    return recCount;
}

} // namespace guild::render
