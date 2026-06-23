// guild::render — SceneNode transform core. See header.
#include "render/scene_transform.h"

#include "util/matrix.h"   // guild::util::MatrixFromEuler @0x5cb1bc (reused, not redefined)

namespace guild::render {

// gilde.exe 0x5cb1bc — delegate to the existing util reconstruction, then repack the
// 3x3 rotation block (the 16-float matrix stores rows at [0..2],[4..6],[8..10]).
Mat3 MatrixFromEuler(const float e[3]) {
    float m[16];
    guild::util::MatrixFromEuler(e, m);
    Mat3 r;
    r.m[0] = m[0]; r.m[1] = m[1]; r.m[2] = m[2];
    r.m[3] = m[4]; r.m[4] = m[5]; r.m[5] = m[6];
    r.m[6] = m[8]; r.m[7] = m[9]; r.m[8] = m[10];
    return r;
}

void WorldToView(const Mat3& R, const float eye[3], const float world[3], float view[3]) {
    const float d0 = world[0] - eye[0], d1 = world[1] - eye[1], d2 = world[2] - eye[2];
    // view = R^T * d  (dot d with each column of R) — PointToBoneLocalSpace's mapping.
    view[0] = d0 * R.m[0] + d1 * R.m[3] + d2 * R.m[6];
    view[1] = d0 * R.m[1] + d1 * R.m[4] + d2 * R.m[7];
    view[2] = d0 * R.m[2] + d1 * R.m[5] + d2 * R.m[8];
}

void CameraForward(const Mat3& R, float fwd[3]) {
    // +view.z direction = column 2 of R (the world dir whose R^T projection is +z).
    fwd[0] = R.m[2]; fwd[1] = R.m[5]; fwd[2] = R.m[8];
}

Mat3 Transpose(const Mat3& R) {
    Mat3 t;
    t.m[0] = R.m[0]; t.m[1] = R.m[3]; t.m[2] = R.m[6];
    t.m[3] = R.m[1]; t.m[4] = R.m[4]; t.m[5] = R.m[7];
    t.m[6] = R.m[2]; t.m[7] = R.m[5]; t.m[8] = R.m[8];
    return t;
}

Mat3 Multiply(const Mat3& A, const Mat3& B) {
    Mat3 c;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            c.m[r * 3 + k] = A.m[r * 3 + 0] * B.m[0 * 3 + k] +
                             A.m[r * 3 + 1] * B.m[1 * 3 + k] +
                             A.m[r * 3 + 2] * B.m[2 * 3 + k];
    return c;
}

void Apply(const Mat3& R, const float v[3], float out[3]) {
    out[0] = R.m[0] * v[0] + R.m[1] * v[1] + R.m[2] * v[2];
    out[1] = R.m[3] * v[0] + R.m[4] * v[1] + R.m[5] * v[2];
    out[2] = R.m[6] * v[0] + R.m[7] * v[1] + R.m[8] * v[2];
}

} // namespace guild::render
