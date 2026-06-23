// guild::render — REFLECTIVE-NODE SETUP (the data plumbing wave-6/7 was missing).
//
// PROBLEM (wave-6/7): the mirror/reflection pass only runs when a scene node is
// marked reflective — VIBE_Mirror_PrepareReflectionNode @0x5F676C scans a mesh's
// texture records for one whose flag byte +104 has bit5 (0x20) set
// (`(child[104] & 0x20) != 0`, 0x5f67c4), and only then binds it and publishes
// dword_649D6C so ShouldRenderMirrorPass @0x5b3af0 fires. But NOTHING in the
// wave-6/7 modules ever SETS that bit, so the pass never fired.
//
// THIS MODULE reconstructs, 1:1, the place the engine actually sets that bit: the
// texture loader VIBE_Texture_LoadByName @0x5DA714. The reflective bit5 of a
// texture record's +104 byte is the local `v85`, computed at 0x5da76a and written
// at 0x5dac92 (BMP fresh-load path) / 0x5da9ad (clone path):
//
//     v85 = ((flag2 >> 6) & 1) != 0           // 0x5da751: sar al,6 / test al,1
//           && (u8)flag0 < 0xFF               // 0x5da75c: cmp [flag0_lo],0FFh / jnb
//     texRec[104] = (32 * (v85 & 1)) | (texRec[104] & 0xDF)   // bit5 := v85
//
//   where (name, flag0, flag1, flag2) are the four args VIBE_Texture_LoadByName
//   receives — assembled per-material by the mesh loader (mesh_load.cpp step 6):
//     flag2 bit6 = mat.shiftHi   (`(shiftHi << 6)` in flag2)
//     flag0 low byte = mat.paletteByte when mat.presentFlag, else default 0xFF.
//   So a SURFACE IS REFLECTIVE iff its material has the high-shift bit set AND a
//   real palette index (< 0xFF). bit5 is then read back by PrepareReflectionNode.
//
// (NB: the same texRec+104 bit5 is what wave-3/4/5 doc'd as "8-bit-indexed source"
// — it is the engine's single `v85` selector; the mirror path reuses it as the
// reflective marker. There is no separate reflective bit. See progress doc.)
//
// THE REFLECTION PLANE (PrepareReflectionNode @0x5f689c..0x5f68cb): once a
// reflective texture is matched to its back-facing surface poly, the mirror plane
// is derived from that poly:
//     n = RotateVectorWithFrame(camNormalSrc) of the poly's first-vertex normal
//     d = n · (poly's first vertex POSITION)            (0x5f68c8)
// We expose the pure derivation: given the surface normal and a point on the
// surface, plane = { n, d = n·p } (the +36 store), the exact form wave-6/7's
// MirrorPlane consumes.
//
// HANDOFF: scene-load (mesh load) marks reflective textures (this module's
// IsMaterialReflective drives the same bit the loader sets). During the scene
// walk, CityView3D calls render::PrepareReflectionNode (wave-7) which finds the
// marked texture (child[104]&0x20), derives the plane (DeriveReflectionPlane
// here, matching 0x5f68c8), and publishes dword_649D6C → ShouldRenderMirrorPass
// → AppendMirroredPolys. See progress/reflective-nodes-wave8.md.

#ifndef GUILD_RENDER_REFLECTIVE_NODES_H
#define GUILD_RENDER_REFLECTIVE_NODES_H

#include "guild/common/types.h"
#include "render/mirror.h"   // guild::render::MirrorPlane

namespace guild::render {

// Texture-record +104 flag bit set/read for reflective surfaces (== kTexFlagIndexed8
// in texture.h; named here for the mirror path's intent). gilde.exe: 32 * v85.
constexpr u8 kReflectiveTexFlag = 0x20;   // bit5 of texRec+104

// gilde.exe 0x5da76a / 0x5da75c — the `v85` predicate, byte-for-byte.
// `flag0` and `flag2` are the loader args VIBE_Texture_LoadByName receives
// (a2=flag0 / a4=flag2 in the __usercall). Returns the value the loader stores
// into bit5 of the texture record's +104 byte.
//   v85 = ((flag2 >> 6) & 1) && ((u8)flag0 < 0xFF)
bool ComputeReflectiveBit(u32 flag0, u32 flag2);

// Material-level convenience: the two material fields the mesh loader feeds into
// flag0/flag2 (mesh_load.cpp step 6). A material is reflective when its high-shift
// bit is set AND it carries a real palette index. `paletteByte` is the value
// placed in flag0's low byte when `present` is true; when `present` is false the
// loader leaves flag0's low byte at the 0xFF default (=> never reflective).
//   gilde.exe: flag2 bit6 = (shiftHi<<6); flag0 lo = present ? paletteByte : 0xFF.
bool IsMaterialReflective(bool present, u8 paletteByte, u8 shiftHi);

// Query/stamp the reflective bit on a raw texture-record +104 flag byte. These are
// the exact read PrepareReflectionNode performs (`child[104] & 0x20`) and the
// write VIBE_Texture_LoadByName performs (`(32*v85) | (flags & 0xDF)`).
inline bool TextureFlagIsReflective(u8 flags104) {
    return (flags104 & kReflectiveTexFlag) != 0;          // 0x5f67c4
}
inline u8 ApplyReflectiveBit(u8 flags104, bool reflective) {
    // texRec[104] = (32 * (v85 & 1)) | (texRec[104] & 0xDF)   (0x5dac7b/0x5dac92)
    return static_cast<u8>((reflective ? kReflectiveTexFlag : 0)
                           | (flags104 & static_cast<u8>(~kReflectiveTexFlag)));
}

// gilde.exe 0x5f68c8 — derive the mirror plane from a reflective surface poly.
// `normal` is the (already camera-frame-rotated) surface normal n (the +24/28/32
// the engine fills via RotateVectorWithFrame); `pointOnSurface` is the poly's
// first-vertex POSITION p. The engine stores plane d = n·p at +36:
//     m.{nx,ny,nz} = n;   m.d = n.x*p.x + n.y*p.y + n.z*p.z
// This is exactly the MirrorPlane wave-6 ReflectPointAcrossPlane / wave-7
// CreateClippingPlanes consume (note: d = +n·p here — the PrepareReflectionNode
// convention; ReflectPointAcrossPlane reflects with t = -(P·n - d)*2).
MirrorPlane DeriveReflectionPlane(const float normal[3],
                                  const float pointOnSurface[3]);

// Scan a mesh's texture-record array for the first reflective one and return its
// index, or -1 if none. `texRecs` is an array of `count` pointers; each record's
// +104 flag byte is at byte offset 104. This mirrors the child-scan loop in
// PrepareReflectionNode (0x5f67b3..0x5f67c4): walk the array, skip nulls, break on
// the first record with bit5 set. Lets a scene-load caller cheaply answer "does
// this mesh carry a reflective surface?" so the mirror pass gate can be primed.
int FindReflectiveTexture(void* const* texRecs, int count);

} // namespace guild::render

#endif // GUILD_RENDER_REFLECTIVE_NODES_H
