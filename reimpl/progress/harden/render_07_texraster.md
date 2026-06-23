# Harden Wave — render_07: texraster_recon2 (light / spanpatch / texcache / texsurf)

Scope: every `gilde.exe 0xADDR` function in the four target files, decompiled AND
disassembled, diffed line-for-line against the reconstruction. Module `gilde.exe`,
imagebase 0x400000. MCP live.

Targets built & green:
- `texraster_recon2_test` — PASS
- `render_raster_textured_test` — PASS
(`cd build && GUILD_GAME_DIR=.../europe_guild_1400_original ctest -R "texraster_recon2_test|render_raster_textured_test"` → 2/2 passed.)

## Counts
- Functions verified: 21
- VERIFIED-1:1 (no change needed): 20
- FIXED: 1
- Tests added: 1 (covers the fix)

---

## texraster_recon2_texcache.cpp

### FIXED — `TextureCache_DisposeAll` @0x5d9b78 — mip-drain loop off-by-one
- Evidence (disasm 0x5d9bef..0x5d9c18):
  `xor edx,edx` / loop: free `*(mipBase+ebx)`, `inc edx`, zero, `add ebx,308h` (776),
  `cmp edx,dword_1406A60` (mipCount), `jb` → continues while `edx < mipCount`, with
  edx pre-set 0. Net: processes **all N** mip blocks (0..N-1).
- Hex-Rays showed `while (v10 + 1 < dword_1406A60)` where `v10` is an uninitialized
  decompiler artifact — NOT the real loop variable. DISASM is the reference of record.
- BEFORE: `do { ...; v9 += 776; ++idx; } while (idx + 1 < g_state.mipCount);`
  → processed only N-1 blocks; the **last mip block was never freed/zeroed**.
- AFTER:  `do { ...; v9 += 776; ++idx; } while (idx < g_state.mipCount);`
- Texture-record drain (`v1+=32` dwords=128B, `v1[16]`=+64, count=dword_1406A80) and
  the trailing unrolled memsets verified equivalent.

### VERIFIED-1:1
- `TextureCache_Free` @0x5b9444 — count guard `if(dword_64A034)`, 68-byte stride,
  `*a1==42` special-slot drain with inner pointer `v4` held at `v3` (no advance),
  `slot[0]=0`/`slot+64=0`, then FreeDebug(base). (Recon's extra `&& tileBase` guard
  only differs in the impossible count!=0/base==0 state — benign; loop body also
  null-guards.)
- `TextureCache_Shutdown` @0x5ba428 — Free() then `dword_64A038 = 0`.
- `TextureCache_Setup` @0x5ba37c — verified `xor edx,edx` (0.0f) + `ebx=3F800000h`
  (1.0f); matrix `Z Z O O Z O Z Z O Z O O` (0x5ba3bf..0x5ba414) exact; blur=bl(var_10),
  mode=cl(var_C); BuildChannelLUT / Init / SetMipFilterLevel / ComputeFilterWeights order.
- `TextureCache_ShutdownFull` @0x5d9c98 — mip loop here is `inc ecx; cmp ecx,mipCount; jb`
  = `while(v4 < mipCount)` (full N) and recon already correct; record drain + array free
  + `if(dword_1406A64)` mip-array free verified.

## texraster_recon2_texsurf.cpp

### VERIFIED-1:1
- `Texture_RestoreSurface` @0x5d9660 — `(flags&8)?0:dword_64A1FC`; `(flags&4)` selects
  FMT_DYNAMIC(byte_14080C4, a2=0,a3=0) vs FMT_PLAIN(byte_14080A0, a2,a3); v8=mip byte.
- `Texture_LoadFromCache` @0x5d96c0 — disasm 0x5d96e4..0x5d9716 confirms compare order
  (deviceKey `[+74h]==[+8]` first; mipKey `(u8(32*flags))>>7` vs `[+10h]` gated by
  `!byte_14080E5`; dynKey `(u8(16*flags))>>7` vs `[+11h]`); `v5+=20` stride;
  `v6>=dword_1406A50` fallback; claim writes `[+8]=0`,`[+96]=*v5`,`[+100]=*(v5+4)`,
  `--dword_1406A5C`.
- `Texture_ReleaseSurfaces` @0x5d9970 — `byte_649D70 && *==42 && [24] && [25]` → evict
  & null both; else release palette([25]) then front([24]); common tail frees texel([17]).
- `Texture_ReleaseSurface` @0x5db624 — `!byte_649D70 || (flags&2)` early-out; release
  front/palette; `if(!*(+76) || *==42) return 0`; else re-upload. (DDraw vtable→hook.)
- `UnpackDdrawPalette` @0x5dbd1a..0x5dbd5f — verified against disasm: R=[edx+0], G=[edx+1],
  B written at `var_30D + (edx+3)` = `var_30C + edx + 2` (the Hex-Rays `&v22[255]+v12+3`
  obfuscation) → consecutive R,G,B per entry, `v12+=3`. Recon's `out[v12+0/+1/+2]` correct.
- `Texture_CapturePaletteSurface` @0x5dbc38 — `flags&=~2`, front/pal=0,
  `wrapMask=(w-1)|(w*w-1)` (0x5dbc84) with `w=*(+116)`; `tag!=42` branch; no-readback
  when `(flags&4)||((byte_14080A4&0x20)==0)` else palette readback→unpack→LoadFromCache.
  DDraw lock/Blt are GPU boundary (Rule 3).
- `Texture_SetBasePath` @0x5d995c — `NormalizeDirPath(a1, dword_62EB78, a2)`;
  `dword_1406A54 = result`.

## texraster_recon2_spanpatch.cpp

### VERIFIED-1:1
- `PatchSpanConstants{Masked,Blend,BlendMasked,Or,OrMasked}` @0x5f753f/57e/5bd/5fc/63b —
  all five execute the identical six source→dest moves
  (unk_1406A8C, dword_1406A78, dword_13FC594, dword_13FC5A8, dword_1406A88, byte_1407A91);
  only the SMC patch target address differs per variant. Recon delivers the six sources
  via a param struct (SMC→param-struct boundary, same pattern as BuildSpanTexParams).
- `Raster_NullStub17` @0x5f71ac, `Raster_NullStub18` @0x5f74ff — bare `retn`.

## texraster_recon2_light.cpp

### VERIFIED-1:1
- `Light_CreateSunRays` @0x42dc7c — desc fills (v8[5]=a1,[0]=0,[2]=0,[1]=plicht+136),
  xyz from dword_13FCD1C+76/80/84, AttachToUniverseNode; exact flag edits o[535]=5,
  o[536]=1, o[531]&=0xFB, o[530]|=0xC, o[529]&=0xFD; globals dword_62D564/568/56C/570;
  UpdateDayCycle; returns dword_62D564. (FindByHandle/ByteBase = scene boundary.)
- `Light_ApplyTorchEffects` @0x5047c0 — disasm 0x504800..0x504831 confirms the
  ReparentWithTransform call passes only `eax=child, edx=a1` (Hex-Rays' `v4*4` 3rd arg is
  a stale-stack artifact); gatePredicate(child,"rLicht"); child type `[+215h]` read as
  signed byte `cmp ah,5/jl` (`>=5`); child head `[+1FCh]`(+508), next `[+1F0h]`(+496);
  DetachAndRelease(collected[v4]); loop `v3<v9`.
- `Light_RefreshTorchLighting` @0x504860 — TraverseTree(...192); octree free/rebuild
  guard `if(dword_634488)`; unconditional `return RefreshAllObjects(1)`.
- `Light_EnableDaylight` @0x504a00 — octree free guard; TraverseTree(...64);
  BuildOctreeForRegion(0,64,7,8); store+return.
- `Light_RegisterUpdateCallbacks` @0x5c7d70 — two TraverseTree calls (RefreshChildBrightness
  then UpdateFlickerIntensity, depth 4); returns the 2nd's char result (hook boundary).

## BOUNDARY (Rule 3 — Vulkan/DDraw GPU calls hooked, math 1:1)
- DDraw `CreateDynamicTexture` / `CreateSurfacePalette` (texsurf RestoreSurface/LoadFromCache).
- DDraw surface vtable Release/Lock/Blt (ReleaseSurfaces, ReleaseSurface, CapturePaletteSurface).
- Scene-graph / object helpers in light cluster (FindByHandle, AttachToUniverseNode,
  WalkAndInvoke, TraverseTree, BuildOctreeForRegion, Reparent/Detach) — flow & ordering 1:1.
- SMC self-patch in spanpatch cluster — modelled as a param struct (no runtime code edit).
