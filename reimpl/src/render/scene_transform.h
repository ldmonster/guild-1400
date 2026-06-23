#pragma once
// =============================================================================
// guild::render — SceneNode transform core (object/camera world matrices).
//
//   0x5cb1bc  VIBE_Math_MatrixFromEuler          (euler -> 3x3 rotation matrix)
//   0x5c8c40  VIBE_Transform_PointToBoneLocalSpace (world -> a node's local/view)
//   0x5c8b38  VIBE_Transform_PointThroughBoneChain (local -> world up the parents)
//
// The active camera is a SceneNode (the "MegaCam" object): its world-space view of
// a point is `view = R_cam^T * (world - eye)` where `eye = node+76 (+ node+120)` and
// `R_cam` is the node's rotation matrix at +396. For a type-3 (camera) node,
// VIBE_Object_SetWorldTranslation sets `R_cam = MatrixFromEuler(-(node+132))` (the
// negated translation/euler), and SetToDummy feeds node+76 <- dummy+92 (eye),
// node+132 <- dummy+144 (so the camera euler is -(dummy+144)).
// =============================================================================
#include "guild/common/types.h"

namespace guild::render {

// A 3x3 rotation matrix, row-major (the 3 rows the engine stores at +396/+412/+428).
struct Mat3 { float m[9]; };  // m[r*3+c]

// gilde.exe 0x5cb1bc — VIBE_Math_MatrixFromEuler(euler[3]) -> rotation matrix.
//   sx=sin(ex) cx=cos(ex) sy=sin(ey) cy=cos(ey) sz=sin(ez) cz=cos(ez)
//   [ cy*cz            sx*sy*cz - cx*sz   cx*sy*cz + sx*sz ]
//   [ cy*sz            cx*cz + sx*sy*sz   cx*sy*sz - sx*cz ]
//   [ -sy              sx*cy              cx*cy            ]
Mat3 MatrixFromEuler(const float euler[3]);

// World->view for a camera node: view = R^T * (world - eye)  (the inverse rotation,
// matching PointToBoneLocalSpace's dot-with-columns). `R` is the camera rotation.
void WorldToView(const Mat3& R, const float eye[3], const float world[3], float view[3]);

// The camera forward (world direction that maps to +view.z) = column 2 of R.
void CameraForward(const Mat3& R, float fwd[3]);

// Transpose of R (the local->world rotation PointThroughBoneChain applies: it dots
// the point with R's COLUMNS, i.e. multiplies by R^T).
Mat3 Transpose(const Mat3& R);

// Matrix product A*B (row-major 3x3).
Mat3 Multiply(const Mat3& A, const Mat3& B);

// out = R * v  (row-major: out[r] = sum_c R[r*3+c]*v[c]).
void Apply(const Mat3& R, const float v[3], float out[3]);

} // namespace guild::render
