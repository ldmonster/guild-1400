#pragma once
// ===========================================================================
// scene_load.{h,cpp} — the .ed3 / .sc SCENE-FILE loader (gilde.exe, d3 engine).
// Namespace: guild::render.
// ===========================================================================
//
// Faithful 1:1 reconstruction of the scene-file parser:
//
//   VIBE_Scene_LoadFromStream  (0x5e7e38) — open the scene by VFS path (or use a
//       caller-supplied stream), read+validate the 32-bit magic/version word,
//       then read the version-gated scene header (camera name, ambient light,
//       camera position + target, fog, the per-light rig table) followed by the
//       object list (a count dword + N recursive object records), the floor flag,
//       and the floor regions. Builds the scene graph and selects the MegaCam.
//
// THE SCENE-FILE FORMAT (recovered byte-for-byte from VIBE_Scene_LoadFromStream)
// ---------------------------------------------------------------------------
// All values are RAW little-endian over the VFS stream (the VIBE_Bio_* readers
// are thin VfsReadStream wrappers — the "SwapArgs" suffix is vestigial; nothing
// is byte-swapped). A scene begins with a 32-bit tag:
//
//   tag       : u32   — must satisfy (tag & 0xFFFF0000)==0x3A6C0000 AND
//                       tag >= 0x3A6C0001, else the load fails (returns 0).
//                       tag < 0x3A6C000B ("Scene too old... Sorry!") is rejected.
//   The low byte of `tag` is the format VERSION; later fields are gated on it.
//
//   if version >= 0x3A6C000B:                         (always, post the too-old gate)
//     camName : cstr  — NUL-terminated camera/scene name ("MegaCam")  -> v33
//     ambient : f32   — ambient light scalar            -> flt_64A070
//     camPos  : vec3  — camera eye position             -> flt_64A074/78/7C
//     if version >= 0x3A6C00B5:
//       camTarget : vec3 — camera look-at               -> flt_64A084/88/8C
//
//     if version >= 0x3A6C00B3:                          (the "fog + flags" block)
//       camFlag : u32 = 0;  if version >= 0x3A6C00B7: camFlag = read u32
//                          -> *(off_649D64 + 45*4) (scene flag word)
//       fogColor: u32       -> ConfigureFog arg
//       fogNear : f32
//       fogFar  : f32       -> VIBE_Render_ConfigureFog(near, far, color)
//
//     (the 7-light rig table is zero-initialised in memory; i=7)
//
//     if version >= 0x3A6C00A2:                          (the light rig table)
//       lightCount = (version >= 0x3A6C00BA) ? 7
//                  : (version >= 0x3A6C00A5) ? 6 : 4
//       for i in [0, lightCount):
//         lightPos[i] : vec3                              -> flt_13FD1B8..
//         if version >= 0x3A6C00B3:
//           if version >= 0x3A6C00B5:
//             lightColor[i] : vec3                        -> flt_13FD1C4..
//           for k in [0, 6):                              (6 per-light keyframes)
//             kf.id   : u32
//             kf.a    : f32                               -> flt_13FD1D4..
//             kf.b    : f32                               -> flt_13FD1D8..
//
//   objCount : u32                                        (the object list)
//   repeat objCount times: an OBJECT RECORD (VIBE_WorldIo_ReadObject), each a
//       presence-gated, recursively-linked node (child + sibling).  The per-node
//       body spawns a scene object + meshes (render leaves), so here the record
//       framing (presence byte, name, ids, kind, child/sibling recursion) is
//       recovered and the spawn/mesh attach is delegated to a host hook.
//
//   floorFlag : u8   — if nonzero, the floor-region block follows
//       (VIBE_WorldIo_LoadFloorRegions; delegated to the host hook).
//
//   if version >= 0x3A6C00A7 (per object): event bindings follow each record
//       (VIBE_Event_LoadEventBindings — delegated).
//
// WHAT THIS MODULE RECONSTRUCTS DIRECTLY (testable on real .ed3 bytes):
//   * the magic/version gate (ParseSceneHeader),
//   * the full version-gated header (camera, ambient, fog, light rig),
//   * the object-list framing (count + presence-driven child/sibling recursion),
//   reading from an injectable byte source so no VFS/engine state is required.
//
// DEFERRED (render leaves — forward-declared/hooked, listed in the report):
//   VIBE_Object_Spawn (0x5b054c), VIBE_Mesh_LoadOrFindByName (0x5d345c) +
//   AttachStockObjectLods, VIBE_Object_Link* / SetParent / SetWorldTranslation /
//   SetPosition / SetActiveCamera, VIBE_Render_ConfigureFog, VIBE_Heightmap_*,
//   VIBE_WorldIo_LoadFloorRegions (0x5e78a8), VIBE_Event_Load*Bindings,
//   VIBE_Texture_UploadAllRecords — all engine-state-coupled.
#include "guild/common/types.h"
#include <functional>
#include <string>
#include <vector>

namespace guild::render {

// ---------------------------------------------------------------------------
// Scene-file tag/version gate (recovered from VIBE_Scene_LoadFromStream).
// The high half must equal 0x3A6C0000; the low byte is the format version.
// ---------------------------------------------------------------------------
constexpr u32 kSceneTagBase   = 0x3A6C0000u;  // (tag & 0xFFFF0000) must equal this
constexpr u32 kSceneTagMin    = 0x3A6C0001u;  // tag >= this to be accepted at all
constexpr u32 kSceneVerTooOld = 0x3A6C000Bu;  // tag < this => "Scene too old"

// Version thresholds gating header fields (low-byte version compares, but the
// original compares the WHOLE dword, which is monotone since the high half is
// constant — so we compare the full tag too).
constexpr u32 kVerCamTarget   = 0x3A6C00B5u;  // camera look-at vec3 present
constexpr u32 kVerFogBlock    = 0x3A6C00B3u;  // fog + per-light keyframes present
constexpr u32 kVerCamFlag     = 0x3A6C00B7u;  // explicit camFlag dword present
constexpr u32 kVerLightRig    = 0x3A6C00A2u;  // light rig table present
constexpr u32 kVerLight6       = 0x3A6C00A5u; // >=6 lights
constexpr u32 kVerLight7       = 0x3A6C00BAu; // 7 lights
constexpr u32 kVerEventBindings = 0x3A6C00A7u;// per-object event bindings present

// HARDENING (wave-11): cap the child/sibling recursion in ReadObjectRecord. The
// original recursed without an explicit limit, but a malformed/cyclic .ed3 with a
// long run of "has child"/"has sibling" presence bytes would recurse one frame
// per byte and overflow the native stack (the file bytes are untrusted asset
// input). No valid shipped scene nests anywhere near this — the real node trees
// are shallow — so this guard never trips on valid data and changes no observable
// output on the in-bounds path; it only fails the walk safely on a degenerate
// stream instead of crashing. Depth is "records on the active recursion stack".
constexpr int kSceneMaxNodeDepth = 4096;

inline bool SceneTagValid(u32 tag) {
    return (tag & 0xFFFF0000u) == kSceneTagBase && tag >= kSceneTagMin;
}

// A 3-vector as stored in the file (12 raw bytes, little-endian floats).
struct SceneVec3 { float x = 0, y = 0, z = 0; };

// One per-light keyframe (the inner v38 loop: a u32 id + two f32).
struct SceneLightKeyframe { u32 id = 0; float a = 0, b = 0; };

// One light rig entry. `hasColor`/keyframes populate per the version gates.
struct SceneLight {
    SceneVec3 pos;
    SceneVec3 color;                 // only when version >= kVerCamTarget
    bool hasColor = false;
    std::vector<SceneLightKeyframe> keyframes;  // 6 when version >= kVerFogBlock
};

// The fully-parsed scene-file header (everything before the object list).
struct SceneHeader {
    u32 tag = 0;                     // raw magic/version dword
    u32 version() const { return tag; }
    std::string camName;             // NUL-terminated camera/scene name
    float ambient = 0;               // flt_64A070
    SceneVec3 camPos;                // flt_64A074..7C
    SceneVec3 camTarget;             // flt_64A084..8C (>= kVerCamTarget)
    bool  hasCamTarget = false;
    u32   camFlag = 0;               // *(off_649D64+45*4)
    bool  hasFog = false;            // (>= kVerFogBlock)
    u32   fogColor = 0;
    float fogNear = 0, fogFar = 0;
    std::vector<SceneLight> lights;  // 0/4/6/7 entries per version
};

// ---------------------------------------------------------------------------
// A byte-stream cursor over an in-memory scene buffer. Mirrors the VFS stream
// the original reads through (VIBE_Bio_Read{Byte,Dword,String,Vec3}). EOF-safe:
// over-reads return zero-filled values and set `eof`.
// ---------------------------------------------------------------------------
class SceneReader {
public:
    SceneReader(const u8* data, std::size_t size) : data_(data), size_(size) {}
    explicit SceneReader(const std::vector<u8>& v) : data_(v.data()), size_(v.size()) {}

    // gilde.exe 0x5dc850 — VIBE_Bio_ReadByte.
    u8 ReadByte();
    // gilde.exe 0x5dc894/0x5dc8b0 — VIBE_Bio_ReadDword (raw LE, no swap).
    u32 ReadDword();
    // raw LE float read (the Vec3 components / fog scalars).
    float ReadFloat();
    // gilde.exe 0x5dc938 — VIBE_Bio_ReadVec3 (3 raw LE floats).
    SceneVec3 ReadVec3();
    // gilde.exe 0x5dc86c — VIBE_Bio_ReadString (read bytes until a NUL).
    std::string ReadString();

    std::size_t pos() const { return pos_; }
    // Bytes left in the stream — an absolute upper bound on how many further
    // records/values can be read. Used to cap untrusted pre-reservations.
    std::size_t remaining() const { return (pos_ < size_) ? size_ - pos_ : 0; }
    bool eof() const { return eof_; }
    bool atEnd() const { return pos_ >= size_; }

private:
    const u8* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool eof_ = false;
};

// gilde.exe 0x5e7e38 (header portion) — read + validate the scene tag and the
// full version-gated header into `out`. Returns false (and leaves the reader at
// the failure point) if the tag is invalid or "too old". The reader is left
// positioned at the object-count dword on success.
bool ParseSceneHeader(SceneReader& r, SceneHeader& out);

// The recovered light count for a given scene version (the v14 ladder).
//   tag >= 0x3A6C00BA -> 7 ; >= 0x3A6C00A5 -> 6 ; else 4.   (only when >= rig ver)
int SceneLightCount(u32 tag);

// ---------------------------------------------------------------------------
// Object-list framing (VIBE_WorldIo_ReadObject @0x5e67c8, structural skeleton).
// Each object record is recursively linked: after the node body is read, a
// "has child" byte and a "has sibling" byte each gate a nested ReadObject call.
// The body (object spawn + mesh attach) is a render leaf -> delegated to a hook.
// ---------------------------------------------------------------------------

// The fields recovered from the leading, structure-bearing part of a record.
struct SceneObjectRecord {
    bool present = false;     // leading presence byte (v122) — false => empty node
    std::string name;         // object name (Bio_ReadString)
    u32 field116 = 0;         // >= 0x3A6C00B2: a dword (v116 -> obj+512)
    u32 field115 = 0;         // >= 0x3A6C00AB: a dword (v115 -> obj+535)
    i32 kindRaw = 0;          // the kind/LOD selector dword (n)
    int childCount = 0;       // number of child records read recursively
    int siblingCount = 0;     // number of sibling records read recursively
};

// The host hook for the per-record body — the original spawns a scene object and
// attaches meshes here. `consumeBody(reader, version, rec)` must advance `reader`
// past the object body (everything between the kind dword and the child/sibling
// presence bytes). Returning false aborts the walk. Tests install a stub that
// knows the test fixture's body layout; the real engine installs the spawn path.
struct SceneObjectHooks {
    std::function<bool(SceneReader&, u32 version, SceneObjectRecord&)> consumeBody;
};

// gilde.exe 0x5e67c8 — VIBE_WorldIo_ReadObject (framing skeleton).
// Read one object record from `r`: the presence byte, then (if present) the
// version-gated header fields and kind dword, the delegated body, and the
// recursive child + sibling records. Returns the populated record. `version` is
// the scene tag. The presence byte governs the WHOLE record (an absent node is
// an empty leaf). Mirrors the original's child-then-sibling recursion order.
SceneObjectRecord ReadObjectRecord(SceneReader& r, u32 version,
                                   const SceneObjectHooks& hooks);

// gilde.exe 0x5e7e38 (object-list portion) — read the object-count dword then
// that many top-level object records (each may recurse), followed by the floor
// flag byte. Returns the list of top-level records. `floorPresent` (out) gets
// the floor flag. The floor-region block + event bindings are delegated/skipped
// (render leaves). `version` is the parsed tag from the header.
std::vector<SceneObjectRecord> ReadObjectList(SceneReader& r, u32 version,
                                              const SceneObjectHooks& hooks,
                                              bool* floorPresent);

// ---------------------------------------------------------------------------
// A convenience whole-scene parse: header + object list, over an in-memory
// buffer. The object-body hook is required (tests provide a fixture-aware stub;
// for header-only inspection of a real .ed3 the default hook stops at the first
// object since the real body needs the engine — see scene_load.cpp).
// ---------------------------------------------------------------------------
struct ParsedScene {
    SceneHeader header;
    std::vector<SceneObjectRecord> objects;  // top-level records (may be empty)
    u32 objectCount = 0;                      // the raw count dword from the file
    bool floorPresent = false;
    bool headerOk = false;
};

// Parse a scene buffer. If `hooks.consumeBody` is null, only the header and the
// object COUNT are read (the records are not walked, since the real body needs
// the engine) — `objects` is left empty but `objectCount`/`headerOk` are set.
ParsedScene ParseScene(const u8* data, std::size_t size,
                       const SceneObjectHooks& hooks);
inline ParsedScene ParseScene(const std::vector<u8>& v,
                              const SceneObjectHooks& hooks) {
    return ParseScene(v.data(), v.size(), hooks);
}

} // namespace guild::render
