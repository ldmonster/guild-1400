#include "render/anim_normals.h"

#include "render/agf_anim.h"   // AnimClip
#include "util/math.h"         // TriangleNormal, VectorNormalize

namespace guild::render {

void CalculateClipNormals(const AnimClip& clip,
                          const std::vector<std::array<int, 3>>& triangles,
                          std::vector<std::vector<float>>& outNormals,
                          std::vector<AnimFrameBounds>& outBounds) {
    const int nf = clip.FrameCount();
    std::vector<std::vector<float>> frames;
    frames.reserve(nf > 0 ? (std::size_t)nf : 0);
    for (int f = 0; f < nf; ++f)
        frames.push_back(clip.FramePoints(f));     // frame f's deformed morph points
    CalculateAnimNormals(frames, triangles, clip.VertexCount(), outNormals, outBounds);
}

void CalculateAnimNormals(const std::vector<std::vector<float>>& frames,
                          const std::vector<std::array<int, 3>>& triangles,
                          int vertexCount,
                          std::vector<std::vector<float>>& outNormals,
                          std::vector<AnimFrameBounds>& outBounds) {
    const int nf = static_cast<int>(frames.size());
    outNormals.assign(nf, std::vector<float>((std::size_t)vertexCount * 3, 0.0f));
    std::vector<AnimFrameBounds> raw(nf);
    if (nf == 0 || vertexCount <= 0) { outBounds.assign(nf, AnimFrameBounds{}); return; }

    std::vector<float> faceN((std::size_t)triangles.size() * 3, 0.0f);

    for (int f = 0; f < nf; ++f) {
        const std::vector<float>& V = frames[f];
        auto vert = [&](int i, float out[3]) {
            const std::size_t b = (std::size_t)i * 3;
            out[0] = (b + 2 < V.size()) ? V[b + 0] : 0.0f;
            out[1] = (b + 2 < V.size()) ? V[b + 1] : 0.0f;
            out[2] = (b + 2 < V.size()) ? V[b + 2] : 0.0f;
        };

        // --- per-triangle face normals (TriangleNormal of the three deformed verts) ---
        for (std::size_t t = 0; t < triangles.size(); ++t) {
            float a[3], b[3], c[3];
            vert(triangles[t][0], a); vert(triangles[t][1], b); vert(triangles[t][2], c);
            util::TriangleNormal(a, b, c, &faceN[t * 3]);   // OUT is the 4th arg (util reorder)
        }

        // --- per-vertex normal = normalized sum of adjacent face normals; + bbox ---
        AnimFrameBounds bb;
        std::vector<float>& N = outNormals[f];
        for (int v = 0; v < vertexCount; ++v) {
            float p[3]; vert(v, p);
            for (int k = 0; k < 3; ++k) {
                if (p[k] < bb.bbMin[k]) bb.bbMin[k] = p[k];
                if (p[k] > bb.bbMax[k]) bb.bbMax[k] = p[k];
            }
            float acc[3] = {0.0f, 0.0f, 0.0f};
            for (std::size_t t = 0; t < triangles.size(); ++t) {
                if (triangles[t][0] == v || triangles[t][1] == v || triangles[t][2] == v) {
                    acc[0] += faceN[t * 3 + 0];
                    acc[1] += faceN[t * 3 + 1];
                    acc[2] += faceN[t * 3 + 2];
                }
            }
            util::VectorNormalize(acc);
            N[(std::size_t)v * 3 + 0] = acc[0];
            N[(std::size_t)v * 3 + 1] = acc[1];
            N[(std::size_t)v * 3 + 2] = acc[2];
        }
        raw[f] = bb;
    }

    // --- second pass: smooth each frame's bbox to the union of its (f-1, f, f+1) neighbours ---
    outBounds.assign(nf, AnimFrameBounds{});
    for (int f = 0; f < nf; ++f) {
        AnimFrameBounds b = raw[f];
        auto merge = [&](const AnimFrameBounds& o) {
            for (int k = 0; k < 3; ++k) {
                if (o.bbMin[k] < b.bbMin[k]) b.bbMin[k] = o.bbMin[k];
                if (o.bbMax[k] > b.bbMax[k]) b.bbMax[k] = o.bbMax[k];
            }
        };
        if (f > 0)      merge(raw[f - 1]);
        if (f < nf - 1) merge(raw[f + 1]);
        outBounds[f] = b;
    }
}

std::uint8_t QuantizeNormalByte(float n) {
    // gilde.exe 0x5d0020 quantize: (n + 1.0) * dbl_628F04 (0.5) * dbl_628F0C
    // (255.0) at x87/double precision, truncated toward zero (Coord_ConvertX
    // RC=11), LOW BYTE stored with NO clamp (the y/z axes store the float sum
    // before the double multiplies; for a unit normal the result is already in
    // [0,255], so the old 0/255 clamps were dead for valid inputs and are
    // removed to match the binary exactly). Bias vector = (1,1,1) @0x5CBA30.
    float t = n + 1.0f;                       // v113/v114 float store
    return static_cast<std::uint8_t>((int)((double)t * 0.5 * 255.0));
}

} // namespace guild::render
