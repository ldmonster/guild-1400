#include "render/mesh.h"

#include "util/coord.h"  // ConvertX (x87 truncate-toward-zero)
#include "util/math.h"   // TriangleNormal

#include <cmath>  // fabs

namespace guild::render {

namespace {
constexpr float  kBiasDefault = 0.875f;  // flt_628B94
constexpr float  kLightCap    = 254.0f;  // flt_628B98
constexpr double kNormalEps   = 0.05;    // dbl_628B9C (|normal.y| floor)
} // namespace

// gilde.exe 0x5c5120 — VIBE_Mesh_ProjectVerticesToScreen
//
// The original read the camera origin / per-axis perspective reciprocals from the
// view block (a2 = v35) and screen biases from file-scope floats; we gather them
// in ProjectParams. The control flow, the +0.875 bias, the [1,254] light clamp,
// the |normal.y| >= 0.05 normal-validity gate, the signed-area backface test and
// the on-screen [0,screenW) clip are reproduced verbatim.
char ProjectVerticesToScreen(MeshGeometry* geom, const ProjectParams& p,
                             u8 objFlags530, i32 viewCull42, DrawList* out) {
    if (!geom)
        return 1;

    // Top gate (original): skip if the object is back-culled by the view's
    // backface gate, unless the 0x10 "force" flag is set.
    //   ((viewCull42 >> 24) & ((u8)(16*objFlags530) >> 6)) == 0  ||  (objFlags530 & 0x10)
    // NOTE (harden 0x5c514c): the original does `shl al,4 ; shr al,6` on the 8-bit
    // AL register, so the (objFlags530<<4) is truncated to 8 bits BEFORE the >>6.
    // (u8)(16*objFlags530) reproduces that mod-256 truncation; the prior code cast
    // to u8 only AFTER the >>6, which diverged for objFlags530 >= 0x10.
    int backCull = ((viewCull42 >> 24) & ((u8)(16 * objFlags530) >> 6)) == 0;
    if (!backCull && (objFlags530 & 0x10) == 0)
        return 1;

    Vertex* verts = geom->vertices;
    const int vcount = geom->vertexCount;

    // ----- Per-vertex projection + light index -----------------------------
    // screenX = (x - eye[0]) * invDepth[1] + biasX
    // screenY = biasX + (z - eye[2]) * scaleX
    // lightTerm = (y - eye[1]) * scaleY ; clamped to [1, 254] then truncated.
    for (int i = 0; i < vcount; ++i) {
        Vertex& vx = verts[i];
        vx.screenX = (vx.x - p.eye[0]) * p.invDepth[1] + p.biasX;
        vx.screenY = p.biasX + (vx.z - p.eye[2]) * p.scaleX;

        float lightTerm = (vx.y - p.eye[1]) * p.scaleY;
        float v39;
        if (lightTerm < 1.0f || p.lightCap >= (double)lightTerm) {
            float v40 = (vx.y - p.eye[1]) * p.scaleY;
            v39 = (v40 >= 1.0f) ? v40 : 1.0f;
        } else {
            v39 = p.lightCap;  // 254.0
        }
        vx.lightIdx = (u8)(int)util::ConvertX((double)v39);

        // Per-vertex flag byte at +65 (the original's *(j-15) write): encodes the
        // object render flags into the vertex for the rasterizer.
        //   bit0 = (objFlags530 & 0x10) != 0       (force)
        //   bit1 = backCull-was-true               (front-facing in this view)
        //   bit2 = (objFlags530 & 0x20) != 0
        vx._pad41 = (u8)((4 * ((objFlags530 & 0x20) != 0))
                       | ((objFlags530 & 0x10) != 0)
                       | (2 * (backCull != 0)));
    }

    // ----- Per-polygon backface cull + draw-list append --------------------
    // capacity remaining = min(out->capacity - out->count, geom->polyCap)
    int remaining = out->capacity - out->count;
    if (remaining > geom->polyCap)
        remaining = geom->polyCap;

    Polygon* polys = geom->polygons;
    float normal[5];

    for (int n = 0; n < remaining; ++n) {
        Polygon& poly = polys[n];
        Vertex* a = poly.v0;
        Vertex* b = poly.v1;
        Vertex* c = poly.v2;

        // Triangle normal of the model-space triangle (a,b,c).
        // NOTE: the original (gilde.exe 0x5cb824) takes (a, b, OUT, c) — the normal
        // buffer is the 3rd arg, the third vertex the 4th. The util reconstruction
        // uses the canonical (a, b, c, out) order, which yields the identical
        // e1=(b-a) x e2=(c-a) cross. We pass c then the out buffer accordingly.
        util::TriangleNormal(&a->x, &b->x, &c->x, &normal[0]);

        if (std::fabs((double)normal[1]) >= kNormalEps || (objFlags530 & 0x40) != 0) {
            // Signed screen-space area => backface bit (+36 bit7).
            //   area = cx*by - cy*bx + bx*ay - by*ax + ax*cy - ay*cx   (< 0 => backface)
            // (subscripts use projected screen x/y at +16/+20 of each vertex)
            bool backface =
                (poly.flags38 & 4) != 0 ||
                (c->screenX * b->screenY - c->screenY * b->screenX +
                 b->screenX * a->screenY - b->screenY * a->screenX +
                 a->screenX * c->screenY - a->screenY * c->screenX) < 0.0f;
            poly.flags36 = (u8)((poly.flags36 & 0x7F) | ((backface ? 1 : 0) << 7));
            poly.flags36 &= (u8)~0x40u;
        } else {
            poly.flags36 |= 0xC0u;  // fully culled (degenerate normal)
        }

        // Append front-facing, fully-on-screen polys to the draw list.
        if ((i8)poly.flags36 < 0 && (poly.flags38 & 2) == 0 && a->screenX >= 0.0f) {
            float w = p.screenW;
            if (a->screenX < (double)w && a->screenY >= 0.0f && a->screenY < (double)w &&
                b->screenX >= 0.0f && b->screenX < (double)w &&
                b->screenY >= 0.0f && b->screenY < (double)w &&
                c->screenX >= 0.0f && c->screenX < (double)w &&
                c->screenY >= 0.0f && c->screenY < (double)w) {
                if (out->count < out->capacity) {
                    int m = a->lightIdx;
                    if (m <= b->lightIdx) m = b->lightIdx;
                    if (m <= c->lightIdx) m = c->lightIdx;
                    DrawListEntry& e = out->entries[out->count];
                    e.sortKey = (u32)(768 * m);
                    e.poly = &poly;
                    ++out->count;
                }
            }
        }
    }

    return 1;
}

} // namespace guild::render
