#include "render/texture_upload.h"
#include "render/texture_mip.h"  // MipWidth (baseWidth >> shift) — REUSED

// =============================================================================
// guild::render texture-record upload / bind layer — implementation. See
// texture_upload.h for the original-function map and the GPU-vtable note.
// `Texture` (the 128-byte record) and `TextureSet`/`TextureSetSize` are REUSED
// from texture.h/.cpp; this file adds only the GPU-surface + binding orchestration
// (no record-struct or slot-manager redefinition — ODR).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe binding helper — byte_1406A90[mipWidth] is a runtime width->log2
// lookup table (zero-initialised in the image, filled by InitTables). For a
// power-of-two width it yields log2(width); we derive it from WidthShift (the
// same value CreateRecord/TextureSetSize stored on the record). Kept local so
// the bank stays self-contained and no second copy of the table escapes.
// ---------------------------------------------------------------------------
static u8 WidthLog2Table(i32 mipWidth) {
    return WidthShift(mipWidth);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5db564 — VIBE_Texture_BindActive.
//   if (idx != dword_64A1F8 && idx valid):
//     rec = records[idx]; dword_64A1F8 = idx;
//     if (rec.flags & 2) rec = records[rec.slot];        ; clone -> master
//     if (rec.slot /*+76*/) {
//        dword_1406A78 = rec.palette (+72)
//        dword_1406A7C = rec.mipWidth (+116)
//        dword_1406A88 = rec.slot (+76)
//        unk_1406A8C   = rec.texels (+68)
//        byte_1407A91  = byte_1406A90[mipWidth]
//     } else { bind the 1x1 white default }
// ---------------------------------------------------------------------------
void TextureBank::BindActive(int idx) {
    // gilde.exe 0x5db566-0x5db570: `if (result != dword_64A1F8 && result)`.
    // The gate is idx != boundIndex AND idx != 0 (NOT a bounds check); idx==0 is
    // a no-op exactly like a re-bind of the already-bound record. (`valid()` is
    // kept only as a defensive guard on the vector access for out-of-tree callers
    // — the original trusts the caller and does no upper-bound check.)
    if (idx == binding.boundIndex || idx == 0 || !valid(idx))
        return;
    binding.boundIndex = idx;                  // dword_64A1F8 = result

    const Texture* rec = &records[(size_t)idx];
    if ((rec->flags & 2) != 0) {               // clone: follow +76 to the master
        int master = rec->slot;
        if (!valid(master))
            return;
        rec = &records[(size_t)master];
    }

    if (rec->slot != 0) {                       // *(+76) != 0 : a real texture
        binding.palBase    = rec->palette;      // dword_1406A78 = *(+72)
        binding.mipWidth   = rec->mipWidth;     // dword_1406A7C = *(+116)
        binding.slot       = rec->slot;         // dword_1406A88 = *(+76)
        binding.texBase    = rec->texels.empty() ? nullptr : rec->texels.data();
        binding.widthShift = WidthLog2Table(rec->mipWidth); // byte_1406A90[width]
    } else {                                    // slot 0 -> 1x1 white default
        binding.slot       = 0;                 // dword_1406A88 = 0
        binding.texBase    = nullptr;           // unk_1406A8C   = 0
        binding.palBase    = nullptr;           // dword_1406A78 = 0
        binding.mipWidth   = 1;                 // dword_1406A7C = 1
        binding.widthShift = 1;                 // byte_1407A91  = 1
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5db5f0 — VIBE_Texture_ResetBinding. Clear to the white default.
//   dword_1406A7C = 1 ; byte_1407A91 = 1 ; dword_1406A88 = 0 ; unk_1406A8C = 0
//   dword_64A1F8 = 0 ; dword_1406A78 = 0
// ---------------------------------------------------------------------------
void TextureBank::ResetBinding() {
    binding.mipWidth   = 1;        // dword_1406A7C = 1
    binding.widthShift = 1;        // byte_1407A91  = 1
    binding.slot       = 0;        // dword_1406A88 = 0
    binding.texBase    = nullptr;  // unk_1406A8C   = 0
    binding.boundIndex = 0;        // dword_64A1F8  = 0  (no record bound)
    binding.palBase    = nullptr;  // dword_1406A78 = 0
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5db1bc — VIBE_Texture_PropagateToInstances.
//   if (!(master.flags & 2) && master.name[0] != '*'):
//     selfIdx = (master - base) >> 7   ; == masterIdx
//     for each record r in [0, count):
//        if (r.refCount>0 && (r.flags & 2) && r.slot /*+76*/ == selfIdx):
//           r.+96 = master.+96 ; r.+100 = master.+100 ; r.texels(+68)=master.+68
//           r.mipWidth(+116)=master.+116 ; r.baseWidth(+120)=master.+120
// ---------------------------------------------------------------------------
void TextureBank::PropagateToInstances(int masterIdx) {
    if (!valid(masterIdx))
        return;
    const Texture& m = records[(size_t)masterIdx];
    bool nameSpecial = !m.name.empty() && (u8)m.name[0] == 42; // *(_BYTE*)v2 == 42
    if ((m.flags & 2) != 0 || nameSpecial)
        return;

    const TexGpu& mg = gpu[(size_t)masterIdx];
    for (size_t i = 0; i < records.size(); ++i) {
        Texture& r = records[i];
        if (r.refCount > 0 && (r.flags & 2) != 0 && r.slot == masterIdx) {
            gpu[i].srcSurface = mg.srcSurface;      // +96  = v2[24]
            gpu[i].dstSurface = mg.dstSurface;      // +100 = v2[25]
            r.texels    = m.texels;                 // +68  = v2[17]
            r.mipWidth  = m.mipWidth;               // +116 = v2[29]
            r.baseWidth = m.baseWidth;              // +120 = v2[30]
        }
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5db234 — VIBE_Texture_UploadToSurface.
// Faithful translation of the two-branch body (software path off vs. on), with
// the COM surface-release routed through dev.release (vtable +8) and the
// LoadAndStretchTexture upload routed through dev.loadAndUpload.
// ---------------------------------------------------------------------------
bool TextureBank::UploadToSurface(int idx, bool force, const char* path,
                                  ITextureSurface& dev) {
    if (!valid(idx))
        return false;
    Texture* rec = &records[(size_t)idx];
    int recIdx = idx;
    if (!(rec->refCount > 0))                   // *(a1+64) > 0 gate
        return false;

    if ((rec->flags & 2) != 0) {                // clone: operate on the master
        recIdx = rec->slot;                     // v4 = base + (*(+76) << 7)
        if (!valid(recIdx))
            return false;
        rec = &records[(size_t)recIdx];
    }
    TexGpu& g = gpu[(size_t)recIdx];

    u8 savedMipShift = mipShiftGlobal;          // v22 = byte_64A350
    bool nameSpecial = !rec->name.empty() && (u8)rec->name[0] == 42;

    if (swEnabled) {                            // byte_649D70 != 0 (GPU present)
        // 0x5db27d: `if (*(v4+96) != 0) goto tail` — reload only when there is no
        // live SOURCE surface (+96) OR a forced reload (a2).
        if (g.srcSurface == 0 || force) {       // !*(v4+96) || a2  (96==srcSurface)
            if (g.srcSurface != 0) {            // 0x5db287: release +96 (source) first
                dev.release(g.srcSurface);      // (*(vtable+8))(*(v4+96))
                g.srcSurface = 0;
            }
            if (g.dstSurface != 0) {            // 0x5db2a0: then release +100 (dest)
                dev.release(g.dstSurface);      // (*(vtable+8))(*(v4+100))
                g.dstSurface = 0;
            }
            if (force && !rec->texels.empty()) {// a2: drop the system texel copy
                rec->texels.clear();            // FreeDebug(*(v4+68)); +68 = 0
            }
            if (rec->slot != 0 && !nameSpecial) {   // *(+76) && name[0] != '*'
                if (path) {                          // BuildBmpPath result != 0
                    u8 noKey = (rec->flags & 8) ? 0 : noTransparency; // v9
                    u8 shift = (rec->flags & 0x40) ? 0 : savedMipShift; // v10
                    mipShiftGlobal = shift;          // byte_64A350 = v10
                    bool stretch = (rec->flags & 4) != 0;
                    u8 achieved = dev.loadAndUpload(path, rec->baseWidth, stretch,
                                                    noKey, rec->loadShift,
                                                    &g.srcSurface, &g.dstSurface);
                    rec->shift = achieved;           // *(v4+124) = v12
                    // 0x5db34d-0x5db352: *(v4+116) = (u32)*(v4+120) >> byte_64A350.
                    // A RAW logical shift (`shr eax,cl`) — NOT saturated to >=1
                    // (MipWidth() would clamp; the binary does not). Identical for
                    // every real input (po2 baseWidth, small shift); exact here.
                    rec->mipWidth =
                        (i32)((u32)rec->baseWidth >> (mipShiftGlobal & 31));
                }
            }
        }
    } else {                                    // software path: drop GPU surfaces
        // 0x5db492: enter when *(v4+96) || *(v4+100) || !*(v4+68).
        if (g.srcSurface != 0 || g.dstSurface != 0 || rec->texels.empty()) {
            if (g.srcSurface != 0) {            // 0x5db417: release +96 (source) first
                dev.release(g.srcSurface);
                g.srcSurface = 0;
            }
            if (g.dstSurface != 0) {            // 0x5db431: then release +100 (dest)
                dev.release(g.dstSurface);
                g.dstSurface = 0;
            }
            // 0x5db451: `if (!a2 && *(v4+68)) FreeDebug(...)` — the allocator free
            // runs only on the !force path; then 0x5db45f `*(v4+68) = 0` ALWAYS
            // drops the texel base. In our owning model "drop the base" == clear()
            // (force just leaks the original buffer there — not representable, and
            // immaterial: the pointer becomes 0 either way).
            rec->texels.clear();
            if (nameSpecial) {
                // '*' record: release the slot back to the pool. We mirror the
                // refCount decrement (ReleaseEntry) without a TextureSet handle by
                // dropping to a free slot when it hits zero.
                if (--rec->refCount <= 0)
                    *rec = Texture{};
            } else if (rec->slot != 0 && path) {
                // Re-decode the system-memory texels via the soft-palettize path.
                // (LoadAndStretchTexture's software sibling.) We leave the texel
                // (re)decode to the asset cache; here we mark the load shift.
                rec->shift = 8;                  // *(v4+124) = 8
            }
        }
    }

    // Common tail: propagate to clones, restore byte_64A350, recurse into mips.
    PropagateToInstances(recIdx);
    mipShiftGlobal = savedMipShift;             // byte_64A350 = v22

    if (rec->mipLevels > 1 && rec->isMip == 0) {  // *(+112) > 1 && !*(+113)
        int groupId = rec->slot;                  // *(v4+80)? -> see note below
        // The original keys the recursion on *(v4+80) (paletteId/group). Walk the
        // bank for active records whose +80 matches and which are themselves mips
        // (+113 set), uploading each.
        int paletteId = rec->paletteId;          // v16 = *(v4+80)
        (void)groupId;
        for (size_t i = 0; i < records.size(); ++i) {
            Texture& r = records[i];
            if (r.refCount > 0 && r.paletteId == paletteId && r.isMip != 0) {
                UploadToSurface((int)i, force, path, dev);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5db4b8 — VIBE_Texture_UploadAllRecords.
//   count active non-clone records (refCount>0 && !(flags&2));
//   for each such record: UploadToSurface(rec, force); then cb();
//   dword_64A1F8 = 0 (clear active binding) at the end.
// ---------------------------------------------------------------------------
int TextureBank::UploadAllRecords(bool force, ITextureSurface& dev,
                                  const char* (*pathFor)(const TextureBank&, int),
                                  void (*cb)(void*), void* cbCtx) {
    int total = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        const Texture& r = records[i];
        if (r.refCount > 0 && (r.flags & 2) == 0)
            ++total;                            // first pass: count (v2)
    }

    int uploaded = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        Texture& r = records[i];
        if (r.refCount > 0 && (r.flags & 2) == 0) {
            const char* p = pathFor ? pathFor(*this, (int)i) : nullptr;
            UploadToSurface((int)i, force, p, dev);
            if (cb)
                cb(cbCtx);                      // result = a2()
            ++uploaded;                         // ++j
        }
    }
    (void)total;
    binding.boundIndex = 0;                     // dword_64A1F8 = 0
    return uploaded;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5dba74 — VIBE_Texture_RestoreIfLost.
//   if (byte_649D70 && (rec.flags & 4)) LoadFromCache(rec, 0, 0);
// We model the restore as a forced UploadToSurface of the dynamic (stretched)
// record (the LoadFromCache path ultimately re-creates the surface via
// LoadAndStretchTexture for a flags-bit2 dynamic texture).
// ---------------------------------------------------------------------------
void TextureBank::RestoreIfLost(int idx, const char* path, ITextureSurface& dev) {
    if (!valid(idx))
        return;
    const Texture& r = records[(size_t)idx];
    if (swEnabled && (r.flags & 4) != 0)
        UploadToSurface(idx, true, path, dev);
}

} // namespace guild::render
