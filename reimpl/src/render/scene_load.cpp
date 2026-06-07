// gilde.exe — guild::render  (MODULE: .ed3 / .sc scene-file loader)
// See scene_load.h for the recovered file format and addresses.
#include "render/scene_load.h"

#include <cstring>  // memcpy

namespace guild::render {

// ===========================================================================
// SceneReader — the byte cursor mirroring the VIBE_Bio_* VFS stream reads.
// Every read is raw little-endian; over-reads return zero and set eof.
// ===========================================================================
u8 SceneReader::ReadByte() {
    if (pos_ + 1 > size_) { eof_ = true; return 0; }
    return data_[pos_++];
}

u32 SceneReader::ReadDword() {
    if (pos_ + 4 > size_) { eof_ = true; pos_ = size_; return 0; }
    u32 v;
    std::memcpy(&v, data_ + pos_, 4);  // LE host; the original copies raw bytes
    pos_ += 4;
    return v;
}

float SceneReader::ReadFloat() {
    u32 bits = ReadDword();
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

SceneVec3 SceneReader::ReadVec3() {
    SceneVec3 v;
    v.x = ReadFloat();
    v.y = ReadFloat();
    v.z = ReadFloat();
    return v;
}

std::string SceneReader::ReadString() {
    // VIBE_Bio_ReadString: read one byte at a time until a NUL terminator.
    std::string s;
    while (pos_ < size_) {
        u8 c = data_[pos_++];
        if (c == 0) return s;
        s.push_back(static_cast<char>(c));
    }
    eof_ = true;   // ran off the end before the NUL
    return s;
}

// ===========================================================================
// gilde.exe 0x5e7e38 (header portion) — ParseSceneHeader.
//
// Mirrors VIBE_Scene_LoadFromStream's prologue verbatim:
//   read the tag; gate on (tag & 0xFFFF0000)==0x3A6C0000 && tag>=0x3A6C0001;
//   reject tag < 0x3A6C000B ("too old"); then the version-gated header reads.
// ===========================================================================

int SceneLightCount(u32 tag) {
    // The v14 ladder inside the `>= 0x3A6C00A2` block:
    //   >= 0x3A6C00BA -> 7 ; >= 0x3A6C00A5 -> 6 ; else 4.
    if (tag >= kVerLight7) return 7;
    if (tag >= kVerLight6) return 6;
    return 4;
}

bool ParseSceneHeader(SceneReader& r, SceneHeader& out) {
    out = SceneHeader{};
    out.tag = r.ReadDword();

    // (v9 == 0x3A6C0000 && v40 >= 0x3A6C0001) — the high-half + min gate.
    if (!SceneTagValid(out.tag))
        return false;

    // tag < 0x3A6C000B -> "Scene too old... Sorry!" (the load fails).
    if (out.tag < kSceneVerTooOld)
        return false;

    // >= 0x3A6C000B (guaranteed here): name, ambient, camera position.
    out.camName = r.ReadString();
    out.ambient = r.ReadFloat();          // flt_64A070
    out.camPos  = r.ReadVec3();           // flt_64A074/78/7C

    if (out.tag >= kVerCamTarget) {       // >= 0x3A6C00B5
        out.camTarget = r.ReadVec3();     // flt_64A084/88/8C
        out.hasCamTarget = true;
    }

    // The fog + per-light-keyframe block ( >= 0x3A6C00B3 ). In the original this
    // appears textually after the light-init loop but executes first (it is the
    // body of the `>= 980156595` branch at 0x5e836d): read the scene flag word
    // (only when >= 0x3A6C00B7, else 0), then fog color + near + far.
    if (out.tag >= kVerFogBlock) {        // >= 0x3A6C00B3
        out.camFlag = 0;
        if (out.tag >= kVerCamFlag)       // >= 0x3A6C00B7
            out.camFlag = r.ReadDword();
        out.hasFog   = true;
        out.fogColor = r.ReadDword();
        out.fogNear  = r.ReadFloat();
        out.fogFar   = r.ReadFloat();
    }

    // The light rig table ( >= 0x3A6C00A2 ). lightCount per the version ladder.
    if (out.tag >= kVerLightRig) {        // >= 0x3A6C00A2
        int count = SceneLightCount(out.tag);
        out.lights.resize(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            SceneLight& L = out.lights[static_cast<std::size_t>(i)];
            L.pos = r.ReadVec3();                          // flt_13FD1B8..
            if (out.tag >= kVerFogBlock) {                // >= 0x3A6C00B3
                if (out.tag >= kVerCamTarget) {           // >= 0x3A6C00B5
                    L.color = r.ReadVec3();               // flt_13FD1C4..
                    L.hasColor = true;
                }
                // inner v38 loop: 6 keyframes of (u32 id, f32 a, f32 b).
                L.keyframes.resize(6);
                for (int k = 0; k < 6; ++k) {
                    L.keyframes[static_cast<std::size_t>(k)].id = r.ReadDword();
                    L.keyframes[static_cast<std::size_t>(k)].a  = r.ReadFloat();
                    L.keyframes[static_cast<std::size_t>(k)].b  = r.ReadFloat();
                }
            }
        }
    }

    return true;
}

// ===========================================================================
// gilde.exe 0x5e67c8 — ReadObjectRecord (VIBE_WorldIo_ReadObject framing).
//
// The original reads a leading presence byte (v122). When zero the record is an
// empty leaf. When nonzero it reads the name + version-gated id fields + the
// kind selector dword, spawns the object and reads its body (render leaf), then
// reads two presence bytes that recursively pull a child then a sibling record.
// Here the body is delegated to hooks.consumeBody; the framing is verbatim.
// ===========================================================================
SceneObjectRecord ReadObjectRecord(SceneReader& r, u32 version,
                                   const SceneObjectHooks& hooks) {
    SceneObjectRecord rec;

    // The whole record is gated on version >= 0x3A6C000B in the original; older
    // scenes skip straight to the post-read fixup. All shipped scenes are newer,
    // and the scene loader itself rejects < 0x3A6C000B, so this always holds.
    if (version < kSceneVerTooOld)
        return rec;

    // Leading presence byte (v122). Zero => empty node (no further reads here;
    // the original still does the child/sibling reads below, but only when the
    // node spawned — an absent node returns immediately with v6 == 0).
    u8 present = r.ReadByte();
    rec.present = (present != 0);
    if (!rec.present)
        return rec;

    // Name (Bio_ReadString -> v111).
    rec.name = r.ReadString();

    // >= 0x3A6C00B2: a dword (v116 -> obj+512).
    if (version >= 0x3A6C00B2u)
        rec.field116 = r.ReadDword();
    // >= 0x3A6C00AB: a dword (v115 -> obj+535).
    if (version >= 0x3A6C00ABu)
        rec.field115 = r.ReadDword();

    // The kind/LOD selector dword (n).
    rec.kindRaw = static_cast<i32>(r.ReadDword());

    // >= 0x3A6C00A6: a trailing "explicit kind" byte (v120) is read; older
    // scenes derive the kind from the switch on (char)n instead. We don't need
    // the spawn result for the framing, but the BYTE read must be reproduced so
    // the stream cursor matches.
    if (version >= 0x3A6C00A6u)
        (void)r.ReadByte();

    // ---- object body (spawn + mesh attach): render leaf, delegated ----------
    if (hooks.consumeBody) {
        if (!hooks.consumeBody(r, version, rec))
            return rec;   // host aborted (e.g. unknown body) — stop the walk
    }

    // ---- recursive child then sibling (the two presence bytes) --------------
    // child presence (v122): pulls a nested record linked as a child.
    u8 hasChild = r.ReadByte();
    if (hasChild) {
        SceneObjectRecord child = ReadObjectRecord(r, version, hooks);
        rec.childCount = 1 + child.childCount + child.siblingCount;
    }
    // sibling presence (v123): pulls a nested record linked as a sibling.
    u8 hasSibling = r.ReadByte();
    if (hasSibling) {
        SceneObjectRecord sib = ReadObjectRecord(r, version, hooks);
        rec.siblingCount = 1 + sib.childCount + sib.siblingCount;
    }

    // >= 0x3A6C00A7: per-object event bindings follow (Event_LoadEventBindings).
    // A render/event leaf — the host body hook is responsible for consuming any
    // event-binding bytes if the fixture includes them; we don't read here.

    return rec;
}

// ===========================================================================
// gilde.exe 0x5e7e38 (object-list portion) — ReadObjectList.
// Read the object-count dword, then that many top-level records, then the floor
// flag byte. Floor regions + the post-load fixups are render leaves.
// ===========================================================================
std::vector<SceneObjectRecord> ReadObjectList(SceneReader& r, u32 version,
                                              const SceneObjectHooks& hooks,
                                              bool* floorPresent) {
    std::vector<SceneObjectRecord> out;
    u32 count = r.ReadDword();
    out.reserve(count);
    for (u32 i = 0; i < count && !r.atEnd(); ++i)
        out.push_back(ReadObjectRecord(r, version, hooks));

    // The floor flag byte (Bio_ReadByte -> v46) — nonzero => floor regions follow.
    u8 floorFlag = r.ReadByte();
    if (floorPresent)
        *floorPresent = (floorFlag != 0);
    return out;
}

// ===========================================================================
// ParseScene — header + object list over an in-memory buffer.
// With a null body hook, only the header and the object COUNT are read (the
// records need the engine to parse their bodies), which is exactly what a
// real-.ed3 structural inspection wants.
// ===========================================================================
ParsedScene ParseScene(const u8* data, std::size_t size,
                       const SceneObjectHooks& hooks) {
    ParsedScene ps;
    SceneReader r(data, size);
    ps.headerOk = ParseSceneHeader(r, ps.header);
    if (!ps.headerOk)
        return ps;

    if (hooks.consumeBody) {
        ps.objects = ReadObjectList(r, ps.header.tag, hooks, &ps.floorPresent);
        ps.objectCount = static_cast<u32>(ps.objects.size());
    } else {
        // Header-only: read just the object-count dword (the records' bodies
        // need the engine). The cursor is left at the first record.
        ps.objectCount = r.ReadDword();
    }
    return ps;
}

} // namespace guild::render
