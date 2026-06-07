#include "render/cull.h"

// gilde.exe 0x5ad614 — VIBE_Render_ComputeVertexClipFlags.
// Faithful 1:1 translation of the two-loop classify pass. The original threaded
// the frustum planes through file-scope globals (flt_13DCDA0..); here they arrive
// in `f` (Frustum, geometry_types.h). Each per-plane in/out test is computed as a
// bool, shifted into its plane's bit, after first clearing that bit — exactly the
// `*v8 &= ~mask; *v8 |= bit;` idiom the decompiler emitted.
//
// Fidelity: the original did the plane dot-products on the x87 stack (float ops
// promoted to 80-bit) and compared against a float loaded as `(double)`. We model
// the products and the comparison in `double`, matching the rest of the geometry
// reconstruction (camera ProjectPoint / ClassifyBoundingBoxPlanes). The integer
// bit twiddling is reproduced verbatim.
namespace guild::render {

void ComputeVertexClipFlags(u8 planeMask, Vertex* verts, i32 vertCount,
                            Polygon* polys, i32 polyCount, const Frustum& f) {
    // ---- vertex pass: build each vertex's 6-bit frustum outcode ------------
    // The original walks `v6` (vertex base, stride 80) and `v8 = &flags[+76]`.
    for (i32 i = 0; i < vertCount; ++i) {
        Vertex& vtx = verts[i];
        // *(_BYTE *)(v6 + 76) = 0; *(_BYTE *)(v6 + 76) &= ~0x80u;  (clear all + bit7)
        vtx.clipFlags = 0;

        const double x = static_cast<double>(vtx.x);
        const double y = static_cast<double>(vtx.y);
        const double z = static_cast<double>(vtx.z);

        if (planeMask & kClipPlane0) {
            // x*plane0.a + z*plane0.c < plane0.d
            bool out = z * f.plane[0][2] + x * f.plane[0][0] < f.plane[0][3];
            vtx.clipFlags = static_cast<u8>((vtx.clipFlags & 0xFE) | (out ? 0x01 : 0));
        }
        if (planeMask & kClipPlane1) {
            bool out = z * f.plane[1][2] + x * f.plane[1][0] < f.plane[1][3];
            vtx.clipFlags = static_cast<u8>((vtx.clipFlags & 0xFD) | (out ? 0x02 : 0));
        }
        if (planeMask & kClipPlane2) {
            // y*plane2.b + z*plane2.c < plane2.d
            bool out = y * f.plane[2][1] + z * f.plane[2][2] < f.plane[2][3];
            vtx.clipFlags = static_cast<u8>((vtx.clipFlags & 0xFB) | (out ? 0x04 : 0));
        }
        if (planeMask & kClipPlane3) {
            bool out = y * f.plane[3][1] + z * f.plane[3][2] < f.plane[3][3];
            vtx.clipFlags = static_cast<u8>((vtx.clipFlags & 0xF7) | (out ? 0x08 : 0));
        }
        if (planeMask & kClipNear) {
            bool out = z < static_cast<double>(f.nearZ);
            vtx.clipFlags = static_cast<u8>((vtx.clipFlags & 0xEF) | (out ? 0x10 : 0));
        }
        if (planeMask & kClipFar) {
            bool out = z > static_cast<double>(f.farZ);
            vtx.clipFlags = static_cast<u8>((vtx.clipFlags & 0xDF) | (out ? 0x20 : 0));
        }
    }

    // ---- polygon pass: keep/cull flag + mark surviving polys' vertices -----
    // Original: v19 walks the poly array (stride 40 = 10 dwords). v19[0]=v0 ptr,
    // v19[1]=v1 ptr, v19[2]=v2 ptr; *(v19+36)=flags36, *(v19+38)=flags38.
    for (i32 i = 0; i < polyCount; ++i) {
        Polygon& poly = polys[i];
        poly.flags36 = 0;
        if (poly.v0 != nullptr && (poly.flags38 & 2) == 0) {
            Vertex* p0 = poly.v0;
            Vertex* p1 = poly.v1;
            Vertex* p2 = poly.v2;
            // KEEP when the three outcodes share no common outside plane.
            u8 sharedOut = static_cast<u8>(p2->clipFlags & p1->clipFlags &
                                           p0->clipFlags & kClipOutMask);
            if (sharedOut == 0) {
                u8 anyOut = static_cast<u8>((p2->clipFlags | p1->clipFlags |
                                             p0->clipFlags) & kClipOutMask);
                poly.flags36 = static_cast<u8>(poly.flags36 | anyOut | kClipKept);
                p0->clipFlags = static_cast<u8>(p0->clipFlags | kClipKept);
                p1->clipFlags = static_cast<u8>(p1->clipFlags | kClipKept);
                p2->clipFlags = static_cast<u8>(p2->clipFlags | kClipKept);
            }
        }
    }
}

} // namespace guild::render
