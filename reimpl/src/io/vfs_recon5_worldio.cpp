#include "io/vfs_recon5_worldio.h"
#include <cstring>

namespace guild::io {

// ---------------------------------------------------------------------------
// gilde.exe 0x5e65b8 — VIBE_WorldIo_SaveSceneObjects (framing).
// ---------------------------------------------------------------------------
void SaveSceneObjects(ByteSink& sink, const SceneSaveData& data,
                      const SceneSaveHooks& hooks) {
    sink.dword(data.magic);            // WriteDwordPair(h, 980156603)
    sink.str(data.tag);                // WriteString(h, dword_649EFC)
    sink.dword(data.formatByte);       // WriteDword(h, flt_64A070)
    sink.vec3(data.vec3A);             // WriteVec3(h, v6)
    sink.vec3(data.vec3B);             // WriteVec3(h, &flt_64A084)
    sink.dword(data.fieldA);           // WriteDwordPair(h, *(off_649D64+45))
    sink.dword(data.fieldB);           // WriteDwordPair(h, dword_649DD4)
    sink.dword(data.fogNear);          // WriteDword(h, flt_13FC5FC)
    sink.dword(data.fogFar);           // WriteDword(h, flt_13FC5F8)

    // 7 light/material slots (outer do/while v10<7).
    for (int i = 0; i < 7; ++i) {
        const SceneSaveData::LightSlot& s = data.light[i];
        sink.vec3(s.posVec3);          // WriteVec3(h, &flt_13FD1B8[24*i])
        sink.vec3(s.dirVec3);          // WriteVec3(h, v13)
        // inner do/while over 24 bytes (v16 += 12, until v16 == v30): 2 triples,
        // each = {WriteDwordPair, WriteDword, WriteDword}.
        sink.dword(s.triA0); sink.dword(s.triA1); sink.dword(s.triA2);
        sink.dword(s.triB0); sink.dword(s.triB1); sink.dword(s.triB2);
    }

    sink.dword(data.objectCount);      // WriteDwordPair(h, i)
    for (u32 i = 0; i < data.objectCount; ++i) {   // per active child
        if (hooks.writeObject) hooks.writeObject(sink, (int)i, hooks.ctx);
    }

    sink.byte(data.hasBuilding);       // WriteByte(h, dword_64A028 != 0)
    if (data.hasBuilding && hooks.writeBuildingData)
        hooks.writeBuildingData(sink, hooks.ctx);   // WriteBuildingData

    if (hooks.writeEventNames)
        hooks.writeEventNames(sink, hooks.ctx);     // WriteEventNames
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5e84f4 — VIBE_Scene_LoadObjectGroup (open/close handled by caller).
// ---------------------------------------------------------------------------
i32 LoadObjectGroup(void* stream, i32 parent, const ObjectGroupHooks& hooks) {
    i32 object = 0;
    if (!stream || !hooks.readDword)
        return object;

    bool ok = true;
    u32 v12 = hooks.readDword(stream, &ok, hooks.ctx);   // ReadDwordSwapArgs
    // v8 = v12 with LOWORD cleared, compared to 980156416 (0x3A6E0000).
    u32 v8 = v12 & 0xFFFF0000u;
    if (v8 != 980156416u) {                              // bad magic
        // original: if (parent==0) CloseStream; (caller closes). Returns 0.
        return object;
    }

    if (v12 < 980156590u) {                              // old single-object form
        object = hooks.readObject ? hooks.readObject(stream, parent, v12, 0, hooks.ctx)
                                  : 0;
    } else {
        // byte_64A7A2 = 0; read count; loop.
        u32 count = hooks.readDword(stream, &ok, hooks.ctx);
        for (u32 i = 0; i < count; ++i) {
            i32 obj = hooks.readObject ? hooks.readObject(stream, parent, v12, object, hooks.ctx)
                                       : 0;
            if (object && obj && parent && hooks.linkSibling)
                hooks.linkSibling(object, obj, hooks.ctx);   // LinkAsSibling
            if (obj && !object)
                object = obj;                                 // first becomes root
        }
    }

    if (object && parent && hooks.setParent)
        hooks.setParent(parent, object, hooks.ctx);          // SetParent
    return object;
}

// ---------------------------------------------------------------------------
// Find the first '/' at or after `p`; return pointer to it, or null if none
// before NUL. Mirrors the unrolled 2-step scan loop in the original.
// ---------------------------------------------------------------------------
static const char* findSlash(const char* p) {
    while (*p != '/') {
        if (*p) {
            ++p;
            if (*p == '/') break;
            if (*p) { ++p; continue; }
        }
        return nullptr;          // hit NUL
    }
    return p;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x44f570 — VIBE_Vfs_AddFileByPath.
// ---------------------------------------------------------------------------
void* VfsAddFileByPath(const char* path, void* rootDir, i32 a3, i32 a4,
                       i32 a5, u32 a6, u32 a7, const VfsAddHooks& hooks) {
    std::size_t len = std::strlen(path);
    if (len == 0 || path[len - 1] == '/')
        return nullptr;          // path ends in '/' -> not a file

    char buf[256];
    std::strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    void* dir = rootDir;
    const char* segStart = buf;
    const char* slash = findSlash(buf);

    while (slash) {
        // copy [segStart .. slash) into a NUL-terminated segment buffer
        char seg[256];
        std::size_t n = (std::size_t)(slash - segStart);
        if (n >= sizeof(seg)) n = sizeof(seg) - 1;
        std::memcpy(seg, segStart, n);
        seg[n] = '\0';

        if (hooks.getOrCreateSubDir)
            dir = hooks.getOrCreateSubDir(seg, dir, hooks.ctx);

        segStart = slash + 1;                 // v15 = v17 + 1
        slash = findSlash(segStart);          // v24 over the remainder
    }

    // leaf = remainder after the last '/'  (v15)
    if (hooks.addFileSorted)
        return hooks.addFileSorted(segStart, dir, a3, a4, a5, a6, a7, hooks.ctx);
    return nullptr;
}

} // namespace guild::io
