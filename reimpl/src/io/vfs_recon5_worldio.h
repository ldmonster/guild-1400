#pragma once
#include "guild/common/types.h"
#include <cstddef>
#include <cstdint>

// =============================================================================
// guild::io — recon5 worldio scene (de)serialization + VFS path-walk insert.
//
//   0x5e65b8  VIBE_WorldIo_SaveSceneObjects   (scene-objects file SERIALIZER)
//   0x5e84f4  VIBE_Scene_LoadObjectGroup      (object-group DESERIALIZER loop)
//   0x44f570  VIBE_Vfs_AddFileByPath          (path-walk -> sorted tree insert)
//
// The Bio_Write* primitives all funnel into VIBE_Vfs_WriteStream(buf, size, h, 1):
//   WriteDword(h, v)     -> 4 bytes      (the SLOBYTE/char arg is a decompiler
//                                          artifact; &a2 with size 4 writes the
//                                          whole 32-bit value)
//   WriteDwordPair(h, v) -> 4 bytes      (only the value arg is payload, count=1)
//   WriteByte(h, b)      -> 1 byte
//   WriteString(h, s)    -> strlen(s)+1 bytes (NUL-terminated)
//   WriteVec3(h, p)      -> 12 bytes     (3 little-endian dwords)
// The exact ORDER + WIDTH of these writes IS the scene-file format and is the
// real, in-scope logic reconstructed here as a typed write trace over an
// abstract byte sink (ByteSink) so it is golden-testable without a real VFS.
//
// COUPLED (hooks): the actual file open/close (VIBE_Vfs_OpenFile/CloseStream),
// the scene-graph child walk (VIBE_SceneGraph_GetFirstActiveChild), the per-object
// body writer (VIBE_WorldIo_WriteObject), building-data + event-name writers, and
// the per-object reader (VIBE_WorldIo_ReadObject) are injected. Those touch live
// scene-graph state / file syscalls; the framing math is reproduced 1:1.
// =============================================================================
namespace guild::io {

using guild::u8;
using guild::u16;
using guild::u32;
using guild::i16;
using guild::i32;

// Abstract little-endian byte sink mirroring VIBE_Vfs_WriteStream framing.
struct ByteSink {
    virtual ~ByteSink() = default;
    virtual void write(const void* data, std::size_t n) = 0;
    void dword(u32 v) {            // WriteDword / WriteDwordPair (4 bytes LE)
        u8 b[4] = {(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)};
        write(b, 4);
    }
    void byte(u8 v) { write(&v, 1); }             // WriteByte
    void str(const char* s) {                     // WriteString (incl. NUL)
        std::size_t n = 0; while (s[n]) ++n;
        write(s, n + 1);
    }
    void vec3(const u32 v[3]) {                    // WriteVec3 (3 dwords LE)
        dword(v[0]); dword(v[1]); dword(v[2]);
    }
};

// Scene-file header/global payload the serializer pulls from live globals. These
// are the *values* (not the framing); injected so the format walk is testable.
struct SceneSaveData {
    u32  magic = 980156603;        // VIBE_Bio_WriteDwordPair(h, 980156603)
    const char* tag = "";          // dword_649EFC string
    u32  formatByte = 0;           // flt_64A070 dword
    u32  vec3A[3] = {0,0,0};       // v6 vec3 (off_649D64-derived camera/origin)
    u32  vec3B[3] = {0,0,0};       // flt_64A084 vec3
    u32  fieldA = 0;               // *((dword*)off_649D64 + 45)
    u32  fieldB = 0;               // dword_649DD4
    u32  fogNear = 0;              // flt_13FC5FC dword
    u32  fogFar = 0;               // flt_13FC5F8 dword
    // 7 light/material slots: each writes 2 vec3 (pos+dir) then a 24-byte triple
    // group of {dwordpair, dword, dword} written twice (the inner v16!=v30 loop
    // runs over 24 bytes -> 2 iterations of the 12-byte stride).
    struct LightSlot {
        u32 posVec3[3] = {0,0,0};      // flt_13FD1B8[24*i]   (WriteVec3)
        u32 dirVec3[3] = {0,0,0};      // v13 (WriteVec3)
        // inner triples: 2 of (pair dword, dword, dword) per slot
        u32 triA0 = 0, triA1 = 0, triA2 = 0;   // first 12-byte triple
        u32 triB0 = 0, triB1 = 0, triB2 = 0;   // second 12-byte triple
    } light[7];
    u32  objectCount = 0;          // count from the GetFirstActiveChild walk
    u8   hasBuilding = 0;          // dword_64A028 != 0
};

// Injected scene-graph / body writers (coupled state).
struct SceneSaveHooks {
    // VIBE_WorldIo_WriteObject(h, node) — per-object body. Called objectCount
    // times in the live-child order.
    void (*writeObject)(ByteSink& sink, int objectIndex, void* ctx) = nullptr;
    // VIBE_WorldIo_WriteBuildingData(h, dword_64A028) — only if hasBuilding.
    void (*writeBuildingData)(ByteSink& sink, void* ctx) = nullptr;
    // VIBE_Event_WriteEventNames(h, ...).
    void (*writeEventNames)(ByteSink& sink, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// gilde.exe 0x5e65b8 — VIBE_WorldIo_SaveSceneObjects (framing only; file open/
// close handled by the caller). Emits, in exact original order:
//   magic(4), tag(str), formatByte(4), vec3A(12), vec3B(12), fieldA(4), fieldB(4),
//   fogNear(4), fogFar(4),
//   for slot 0..6: posVec3(12), dirVec3(12), [pair(4),dword(4),dword(4)]x2,
//   objectCount(4), objectCount * WriteObject,
//   hasBuilding(1), [WriteBuildingData], WriteEventNames.
void SaveSceneObjects(ByteSink& sink, const SceneSaveData& data,
                      const SceneSaveHooks& hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x5e84f4 — VIBE_Scene_LoadObjectGroup. Reads a 4-byte magic; the high
// 16 bits must equal 0x3A6E (980156416 >> 16 path: it masks LOWORD=0 and compares
// to 980156416). If version `v12 < 980156590`: a single ReadObject. Else: read a
// 4-byte count and loop ReadObject `count` times, linking each as a sibling of the
// first and tracking the first non-null as the group root. Returns the root object
// (and SetParent(parent, root) if both nonzero). File open/close + ReadObject are
// injected; the magic gate + version branch + link/parent control flow is 1:1.
// ---------------------------------------------------------------------------
struct ObjectGroupHooks {
    // VIBE_Bio_ReadDwordSwapArgs(h, &out) — read one 4-byte LE dword. Returns the
    // value; sets ok=false at EOF (the original ignores the read return).
    u32 (*readDword)(void* stream, bool* ok, void* ctx) = nullptr;
    // VIBE_WorldIo_ReadObject(stream, parent, 1, version, parent, prevRoot) ->
    // object token (0 if none). `prevRoot` is the running group root.
    i32 (*readObject)(void* stream, i32 parent, u32 version, i32 prevRoot,
                      void* ctx) = nullptr;
    // VIBE_Object_LinkAsSibling(root, obj).
    void (*linkSibling)(i32 root, i32 obj, void* ctx) = nullptr;
    // VIBE_Object_SetParent(parent, root).
    void (*setParent)(i32 parent, i32 root, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// `stream` is the already-open VFS handle (the caller does OpenFile/CloseStream).
// `parent` is a4 (the destination parent object token, 0 = none).
i32 LoadObjectGroup(void* stream, i32 parent, const ObjectGroupHooks& hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x44f570 — VIBE_Vfs_AddFileByPath. Walks `path` on '/' separators:
// for each leading directory segment it calls GetOrCreateSubDir(seg, parent) to
// descend the tree, then inserts the trailing file segment via AddFileSorted.
// Returns null if `path` ends in '/' (a directory, not a file). The path split is
// the in-scope logic; GetOrCreateSubDir / AddFileSorted are injected (tree state).
// ---------------------------------------------------------------------------
struct VfsAddHooks {
    // VIBE_Vfs_GetOrCreateSubDir(segment, parentDir) -> child dir token.
    void* (*getOrCreateSubDir)(const char* segment, void* parentDir, void* ctx) = nullptr;
    // VIBE_Vfs_AddFileSorted(leafName, dir, a3, a4, a5, a6, a7) -> file record.
    void* (*addFileSorted)(const char* leaf, void* dir, i32 a3, i32 a4,
                           i32 a5, u32 a6, u32 a7, void* ctx) = nullptr;
    void* ctx = nullptr;
};

void* VfsAddFileByPath(const char* path, void* rootDir, i32 a3, i32 a4,
                       i32 a5, u32 a6, u32 a7, const VfsAddHooks& hooks);

} // namespace guild::io
