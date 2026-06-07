// Script-VM long-tail: .esc binary token parser + context/command table
// scan helpers + trivial command bodies. See script_import.h for the map.
//
// Faithful 1:1 from gilde.exe pseudocode. Host-engine leaves are forward-
// declared here and resolved by their owning modules (tests provide stubs).
#include "sim/script_import.h"
#include "util/string_ops.h"
#include <cstring>
#include <cstdint>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Stream-reader hook plumbing (the only cross-module leaves of the .esc parser).
// The library owns an inert default (reads nothing -> immediate EOF) so every
// TU linking script_import resolves these symbols without a test stub; tests
// install their own readers backed by an in-memory stream (see header).
// ---------------------------------------------------------------------------
namespace {
const ScriptImportHooks* g_hooks = nullptr;
ScriptImportHooks        g_inert{};   // all-null readers -> EOF on every read
} // namespace

void SetScriptImportHooks(const ScriptImportHooks* hooks) { g_hooks = hooks; }
const ScriptImportHooks& GetScriptImportHooks() {
    return g_hooks ? *g_hooks : g_inert;
}

// ---------------------------------------------------------------------------
// Host leaves. The string utilities delegate to the real reconstructed
// guild::util primitives (already in the library); the byte/string stream
// readers route through the installed hooks (inert default => EOF).
//   VIBE_Vfs_ReadStream  @0x4514ac  -> hook (orig reads from the VFS stream)
//   VIBE_Bio_ReadByte    @0x5dc850  -> hook (orig: VfsReadStream(buf,1,h,1))
//   VIBE_Bio_ReadString  @0x5dc86c  -> hook (orig: read bytes until NUL)
//   VIBE_Util_StrChr     @0x5d3ef0  -> util::StrChrLast (last match)
//   VIBE_Util_StrCmp     @0x5d3f10  -> faithful byte strcmp (0 when equal)
//   VIBE_Util_StrCmpNoCase @0x5cb8f0 -> util::StrCmpNoCase
// ---------------------------------------------------------------------------
namespace host {
u32 VfsReadStream(u8* buf, u32 size, int stream, int count) {
    const auto& h = GetScriptImportHooks();
    return h.readStream ? h.readStream(buf, size, stream, count) : 0;
}
u32 BioReadByte(int stream, u8* out) {
    const auto& h = GetScriptImportHooks();
    if (h.readByte) return h.readByte(stream, out);
    // gilde.exe 0x5dc850: VfsReadStream(out, 1, stream, 1).
    return VfsReadStream(out, 1, stream, 1);
}
u32 BioReadString(int stream, u8* out) {
    const auto& h = GetScriptImportHooks();
    if (h.readString) return h.readString(stream, out);
    // gilde.exe 0x5dc86c: read bytes until a NUL terminator (inclusive).
    u32 result = 0;
    u8 b;
    do {
        if (!VfsReadStream(&b, 1, stream, 1)) { *out = 0; return result; }
        *out++ = b;
        result = b;
    } while (b);
    return result;
}
u8* UtilStrChr(u8* s, char c) {
    // gilde.exe 0x5d3ef0 — returns the *last* matching byte (strrchr semantics).
    return reinterpret_cast<u8*>(util::StrChrLast(reinterpret_cast<char*>(s), c));
}
int UtilStrCmp(const u8* a, const u8* b) {
    // gilde.exe 0x5d3f10 — case-sensitive byte compare; 0 when equal. (No
    // dedicated reconstructed target yet; this leaf is the std strcmp body.)
    return std::strcmp(reinterpret_cast<const char*>(a),
                       reinterpret_cast<const char*>(b));
}
int UtilStrCmpNoCase(const u8* a, const u8* b) {
    // gilde.exe 0x5cb8f0 — case-insensitive compare; 0 when equal.
    return util::StrCmpNoCase(reinterpret_cast<const char*>(a),
                              reinterpret_cast<const char*>(b));
}
} // namespace host

// ===========================================================================
// A) .esc binary token parser
// ===========================================================================

int g_escBlockDepth = 0;   // gilde.exe dword_64A35C

// gilde.exe 0x5e3bd0 — VIBE_Script_ReadToken@<al>(stream@eax, ctx@ecx)
u8 EscReadToken(int stream) {
    u8 b[8];                                  // v3[8] (BYREF, only [0] used)
    if (!host::VfsReadStream(b, 1, stream, 1))
        return kEscTokEof;                    // 43 on EOF      /*0x5e3bff*/
    if (b[0] > kEscMaxToken)                  // > 0x3A
        return kEscTokError;                  // 39             /*0x5e3c03*/
    return b[0];                              /*0x5e3bfd*/
}

// gilde.exe 0x5e3c0c — VIBE_Script_FindTokenHandler@<eax>(token@al, table@edx)
int EscFindTokenHandler(u8 token, const EscHandler* table) {
    int v3 = 0;                               // index
    // while ( token != current.token ) { ... }
    // (orig advances by 12 bytes/entry; here we index by typed pointer so the
    //  reconstruction's wider 64-bit EscHandler stride stays correct.)
    while ((u8)token != table->token) {       /*0x5e3c23*/
        if (table->token != kEscTokOpen) {    // 40 sentinel -> stop /*0x5e3c28*/
            ++v3;                             /*0x5e3c2a*/
            ++table;                          /*0x5e3c2b (orig: a2 += 12)*/
            if (v3 < kEscHandlerMax)          /*0x5e3c31*/
                continue;
        }
        return kEscTokError;                  // 39             /*0x5e3c3c*/
    }
    return v3;                                /*0x5e3c3c*/
}

// gilde.exe 0x5e3d28 — VIBE_Script_IncrementCounter(a1, ctx@a2): ++ctx[+8].
void EscIncrementCounter(EscParseCtx* ctx) {
    ++ctx->recordIndex;                       /*0x5e3d28*/
}

namespace {
// Shared helper for the three string-field readers (ReadName/Mesh/Texture).
// gilde.exe 0x5e3d40 / 0x5e3da0 / 0x5e3e00 differ only by the +offset added to
// the record base. The body: read a NUL-terminated string, truncate at the
// first '.' found (StrChr returns the *last* '.', and the original NUL-cuts
// there), then copy the byte sequence into the record field. The copy loop is a
// 2-bytes-per-iteration unroll that stops at the terminating NUL.
void EscReadStringField(int stream, EscParseCtx* ctx, int fieldOffset) {
    u8 buf[268];                              // v6[268]
    host::BioReadString(stream, buf);         /*0x5e3d4d*/
    u8* dot = host::UtilStrChr(buf, '.');     // 46          /*0x5e3d59*/
    if (dot) *dot = 0;                        /*0x5e3d62*/
    u8* src = buf;                            // v3
    u8* dst = ctx->recordBase + kEscRecordStride * ctx->recordIndex + fieldOffset;
    u8 result;
    do {                                      /*0x5e3d90*/
        result = src[0];
        dst[0] = src[0];
        if (!result) break;                   // first byte NUL -> done
        result = src[1];
        src += 2;
        dst[1] = result;
        dst += 2;
    } while (result);                         // second byte NUL -> done
}
} // namespace

// gilde.exe 0x5e3d40 — VIBE_Script_ReadNameField (record +0)
void EscReadNameField(int stream, EscParseCtx* ctx)    { EscReadStringField(stream, ctx, 0);   }
// gilde.exe 0x5e3da0 — VIBE_Script_ReadMeshField (record +64)
void EscReadMeshField(int stream, EscParseCtx* ctx)    { EscReadStringField(stream, ctx, 64);  }
// gilde.exe 0x5e3e00 — VIBE_Script_ReadTextureField (record +128)
void EscReadTextureField(int stream, EscParseCtx* ctx) { EscReadStringField(stream, ctx, 128); }

// gilde.exe 0x5e3e64 — VIBE_Script_ReadByteFieldA: byte -> record +192
void EscReadByteFieldA(int stream, EscParseCtx* ctx) {
    host::BioReadByte(stream,
        ctx->recordBase + kEscRecordStride * ctx->recordIndex + 192); /*0x5e3e8a*/
}

// gilde.exe 0x5e3e8c — VIBE_Script_ReadByteFieldB: byte -> record +193
void EscReadByteFieldB(int stream, EscParseCtx* ctx) {
    host::BioReadByte(stream,
        ctx->recordBase + kEscRecordStride * ctx->recordIndex + 193); /*0x5e3eb2*/
}

// gilde.exe 0x5e3eb4 — VIBE_Script_ReadFlagBitsField: one byte, unpacked.
void EscReadFlagBitsField(int stream, EscParseCtx* ctx) {
    u8 v[4];                                  // v4[4] (BYREF)
    host::BioReadByte(stream, v);             /*0x5e3ebd*/
    u8* base = ctx->recordBase + kEscRecordStride * ctx->recordIndex;
    base[194] = v[0] & 1;                      /*0x5e3eda*/
    base[197] = ((int)v[0] >> 1) & 1;          /*0x5e3efd*/
    base[198] = ((int)v[0] >> 2) & 0xF;        /*0x5e3f21*/
    base[195] = ((int)v[0] >> 6) & 1;          /*0x5e3f45*/
}

// gilde.exe 0x5e3f54 — VIBE_Script_ReadFlagBitsField2: one byte, two fields.
void EscReadFlagBitsField2(int stream, EscParseCtx* ctx) {
    u8 v[4];                                  // v4[4] (BYREF)
    host::BioReadByte(stream, v);             /*0x5e3f5d*/
    u8* base = ctx->recordBase + kEscRecordStride * ctx->recordIndex;
    base[196] = v[0] & 1;                      /*0x5e3f7a*/
    base[199] = (u8)(2 * ((int)v[0] >> 1));    /*0x5e3f9c*/
}

// gilde.exe 0x5e3c44 — VIBE_Script_ParseBlock@<al>(stream@eax, table@edx, ctx@ecx)
// Walks tokens until a block-close (0x2F '/' or 0x2B '+') or stream error
// (0x27 "'"). Dispatches via the handler table:
//   * token 0x28 '(': resolve the open-handler, call its leaf fn (v6[1]/+4),
//     then break;
//   * otherwise: resolve handler; if it has a leaf fn AND no extra flag (+8==0)
//     call the leaf; else recurse into the child block (++depth) and continue;
//   * after a non-open token, read the next token; 0x27 ends the block.
u8 EscParseBlock(int stream, EscHandler* table, EscParseCtx* ctx) {
    u8 tok = EscReadToken(stream);            /*0x5e3c53*/
    if (tok != kEscTokError) {                // != 39        /*0x5e3c5a*/
        while (tok != kEscTokClose && tok != kEscTokEof) {  // 47 / 43 /*0x5e3c62*/
            if (tok == kEscTokOpen) {         // 40           /*0x5e3c66*/
                int idx = EscFindTokenHandler(kEscTokOpen, table); /*0x5e3c72*/
                if (idx != kEscTokError) {    /*0x5e3c7a*/
                    EscHandler* h = table + idx;            // &a2[12*idx]
                    if (h->leafFn)            // v6[1] (+4 in orig) /*0x5e3c88*/
                        h->leafFn();          /*0x5e3c92*/
                }
                break;                        /*0x5e3c92*/
            }
            int idx = EscFindTokenHandler(tok, table);      /*0x5e3caa*/
            if (idx != kEscTokError) {        /*0x5e3cb4*/
                EscHandler* h = table + idx;  // &a2[12*idx]
                // if has-leaf and no child -> call leaf; else recurse.
                if (h->leafFn && !h->childTable) {          /*0x5e3cc1*/
                    h->leafFn();              /*0x5e3ccd*/
                } else {
                    ++g_escBlockDepth;        /*0x5e3cfb*/
                    if (h->childTable)        // v12 (+8 in orig) /*0x5e3cf9*/
                        EscParseBlock(stream, h->childTable, ctx); /*0x5e3d20*/
                    else
                        EscParseBlock(stream, table, ctx);  /*0x5e3d0a*/
                }
            }
            tok = EscReadToken(stream);       /*0x5e3cd2*/
            if (tok == kEscTokError) {        // 39           /*0x5e3cd9*/
                --g_escBlockDepth;            /*0x5e3cdb*/
                return tok;                   /*0x5e3ce7*/
            }
        }
    }
    --g_escBlockDepth;                        /*0x5e3c95*/
    return tok;                               /*0x5e3c9b*/
}

// ===========================================================================
// B) Context / command table scan + lookup helpers, command stubs.
// ===========================================================================

// gilde.exe 0x442174 — VIBE_Script_FindByHandle@<eax>(handle@eax)
u8* FindByHandle(u8* ctxTableBase, i32 handle) {
    if (handle == -1) return nullptr;                       /*0x442182*/
    int v2 = 0;
    // while ( handle != *(dword*)(base + v2 + 128) )
    while (handle != *reinterpret_cast<i32*>(ctxTableBase + v2 + kScHandle)) { /*0x44218f*/
        v2 += kScriptContextStride;                         /*0x442191*/
        if (v2 >= kScriptContextStride * kScriptContextCount) // 330752
            return nullptr;                                 /*0x44219b*/
    }
    return ctxTableBase + v2;                               /*0x4421a7*/
}

// gilde.exe 0x4421b8 — VIBE_Script_FindByName@<eax>(name@eax)
u8* FindByName(u8* ctxTableBase, const char* name) {
    int v4 = 0;
    const u8* key = reinterpret_cast<const u8*>(name);
    // while ( StrCmpNoCase(base + v4, name) )  -- nonzero == not equal
    while (host::UtilStrCmpNoCase(ctxTableBase + v4, key)) {  /*0x4421cf*/
        v4 += kScriptContextStride;                         /*0x4421d1*/
        if (v4 >= kScriptContextStride * kScriptContextCount)
            return nullptr;                                 /*0x4421e4*/
    }
    return ctxTableBase + v4;                               /*0x4421e3*/
}

// gilde.exe 0x445b70 — VIBE_Script_FindCommandByName@<eax>(name@eax)
u8* FindCommandByName(u8* cmdTableBase, const char* name) {
    int v2 = 0;   // slot index
    int v3 = 0;   // byte offset
    const u8* key = reinterpret_cast<const u8*>(name);
    do {                                                    /*0x445b96*/
        if (!host::UtilStrCmp(key, cmdTableBase + v3))      // 0 == equal /*0x445b83*/
            break;                                          /*0x445b8a*/
        v3 += kCommandStride;                               /*0x445b8c*/
        ++v2;                                               /*0x445b8f*/
    } while (v3 < kCommandStride * kCommandCapacity);       // 13312
    if (v2 >= kCommandCapacity)                             /*0x445b9e*/
        return nullptr;                                     /*0x445bbe*/
    return cmdTableBase + kCommandStride * v2;              /*0x445bb7*/
}

// VIBE_Script_CountActive (0x445d40), VIBE_Script_FreeFinished (0x445370) and
// VIBE_Script_SkipToSemicolon (0x441660) are already translated in
// script_vm.cpp / script_compiler.cpp and are reused, not duplicated here.

// gilde.exe 0x44191c — VIBE_Script_FindLogicalOperator@<eax>(p@eax, end@ebx)
const char* FindLogicalOperator(const char* p, const char* end) {
    if (*p == 38) return nullptr;                           // '&' as first byte -> bail /*0x441922*/
    char v2 = p[1];
    // bail if the leading 1-2 bytes already look like an operator boundary
    if (v2 == 38 || *p == 124 || v2 == 124)                 // '&' / '|' /*0x441934*/
        return nullptr;
    while (reinterpret_cast<std::uintptr_t>(p) <
           reinterpret_cast<std::uintptr_t>(end)) {          /*0x44193c*/
        char c = *p;
        if (*p == 41 || c == 59 || c == 40 || c == 123)      // ')' ';' '(' '{' /*0x441952*/
            break;
        if ((c == 38 && p[1] == 38) || (*p == 124 && p[1] == 124)) // "&&" / "||" /*0x44196d*/
            return p;
        ++p;                                                /*0x441963*/
    }
    return nullptr;                                         /*0x441969*/
}

// Constant-returning command bodies.
i32 CmdReturnTrue()  { return 1; }  // gilde.exe 0x43c650
i32 CmdReturnFalse() { return 0; }  // gilde.exe 0x43c658
i32 NullStub()       { return 0; }  // gilde.exe 0x442598

// gilde.exe 0x445d7c — VIBE_Script_ResetCurrentHandle: dword_62E8D4 = -1.
void ResetCurrentHandle(i32* currentHandle) { *currentHandle = -1; }

} // namespace guild::sim
