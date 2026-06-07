#include "sim/script_symbols.h"

namespace guild::sim {

// ===========================================================================
// gilde.exe 0x442c90 — VIBE_Script_DefineVariable
//
//   v5 = ctx[+136];  v6 = 48 * ctx[+148];           // base + 48*count
//   rec[+0] = (rec[+0] & 0xF0) | (type & 0x0F);     // write type nibble
//   rec[+44] = AllocDebug(elemSize * count);        // storage
//   rec[+36] = count;
//   return ctx[+136] + 48 * (ctx[+148])++;          // record ptr, post-inc cnt
//
// We append a ScriptVar and allocate a contiguous run of `count` storage cells
// in the flat arena (storage handle = first cell index). The original's
// per-element byte size is folded to dword cells for int/float/void; byte and
// string types reserve the same number of cells (the byte path writes a single
// cell, string writes a 96-byte/24-dword run — we reserve count*ceil(elem/4)).
// ===========================================================================
int ScriptSymbols::DefineVariable(const std::string& name, u8 type, i32 count) {
    if (count < 1) count = 1;
    ScriptVar v;
    v.typeNibble = type & 0x0F;          // high-nibble flags start clear
    v.name       = name;
    v.count      = count;
    v.isLocal    = 0;

    // Allocate storage in the arena. elemSize is in bytes; the runtime addresses
    // int/float as 4-byte cells, byte as 1, string as 96. We store one i32 cell
    // per dword of element to keep GetVariableAddress's *4 accounting exact.
    int elemBytes = VarElemSize(type);
    int cellsPerElem = (elemBytes + 3) / 4;        // 4->1, 1->1, 96->24
    if (cellsPerElem < 1) cellsPerElem = 1;
    v.storage = (int)storage_.size();
    storage_.resize(storage_.size() + (size_t)count * cellsPerElem, 0);

    vars_.push_back(v);
    return (int)vars_.size() - 1;
}

// ===========================================================================
// gilde.exe 0x4414d4 — VIBE_Script_LookupVariable
// Original order: (1) block-scope frame array at +2472 [+16, stride 4, <32],
// (2) the per-script var array (ctx[+148] entries, stride 48),
// (3) the global table dword_767944 (dword_767948 entries, stride 48).
// The block-scope frame lookup is the deferred local-scope path; we resolve over
// the per-script var array, which is what the e2e compiler/executor exercises.
// StrCmp matches the name region at record +1.
// ===========================================================================
int ScriptSymbols::LookupVariable(const std::string& name) const {
    for (int i = 0; i < (int)vars_.size(); ++i) {
        if (vars_[i].name == name) return i;
    }
    return -1;
}

// ===========================================================================
// gilde.exe 0x4415f8 — VIBE_Script_LookupFunction
// Linear StrCmp scan over ctx[+140] (count ctx[+144], stride 304); name at +0.
// ===========================================================================
int ScriptSymbols::LookupFunction(const std::string& name) const {
    for (int i = 0; i < (int)funcs_.size(); ++i) {
        if (funcs_[i].name == name) return i;
    }
    return -1;
}

int ScriptSymbols::DefineFunction(const std::string& name, int bodyCursor) {
    ScriptFunc f;
    f.name = name;
    f.bodyCursor = bodyCursor;
    funcs_.push_back(std::move(f));
    return (int)funcs_.size() - 1;
}

// ===========================================================================
// gilde.exe 0x4459bc — VIBE_Script_GetVariableAddress
//
//   v3 = 48*varCount + 304*funcCount + srcBase + 2584;     // region base
//   for (i=0; i<varCount; ++i) v3 += 4 * var[i].count;     // sum element bytes
//   return v3;                                             // end of var storage
//
// The region-base term (48*vars + 304*funcs + srcBase + 2584) is the original's
// in-process layout (symbol tables + source + ScriptContext laid contiguously);
// it is constant for a given table and not observable through the variable
// values, so we return the portable part: the summed storage size in bytes
// (sum of 4*count over all variables), which is what the original adds.
// ===========================================================================
int ScriptSymbols::VariableStorageEnd() const {
    int total = 0;
    for (const auto& v : vars_) {
        total += 4 * v.count;
    }
    return total;
}

} // namespace guild::sim
