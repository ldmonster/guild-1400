#pragma once
// gilde.exe CPU feature detection.
//
// Provenance / scope note: the shipped image does NOT execute a raw `cpuid` in
// its feature path. It probes FP behavior with software floating point
// (VIBE_Fpu_DetectFeatures @0x606040) and otherwise defers to the Win32
// IsProcessorFeaturePresent (VIBE_Cpu_FeaturePresent @0x14226db). Per the module
// brief, this file reconstructs the standard x86 CPUID feature-bit *decode*
// (the logic a native build would use), backed by a configurable raw-cpuid shim
// so the decode can be unit-tested from sample EAX/EDX/ECX register values. The
// raw `cpuid` instruction itself is the Win32/CPU substitute (CpuidShim).
#include "guild/common/types.h"

namespace guild::config {

// Result of one CPUID leaf invocation.
struct CpuidRegs {
    u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
};

// Raw-cpuid shim: returns the four registers for a given leaf/subleaf. The real
// instruction would be issued by a tiny asm adapter; tests inject a stub.
class CpuidShim {
public:
    virtual ~CpuidShim() = default;
    virtual CpuidRegs query(u32 leaf, u32 subleaf) = 0;
};

// Standard CPUID.01h EDX feature bits (the classic set the original era cared
// about). Values are the bit positions defined by Intel/AMD.
enum CpuFeatureEdx : u32 {
    kFpu   = 1u << 0,   // x87 FPU on chip
    kVme   = 1u << 1,
    kTsc   = 1u << 4,   // time-stamp counter (RDTSC)
    kCx8   = 1u << 8,   // CMPXCHG8B
    kCmov  = 1u << 15,  // conditional move
    kMmx   = 1u << 23,  // MMX
    kFxsr  = 1u << 24,  // FXSAVE/FXRSTOR
    kSse   = 1u << 25,  // SSE
    kSse2  = 1u << 26,  // SSE2
    kHtt   = 1u << 28,  // hyper-threading
};

// Standard CPUID.01h ECX feature bits (subset).
enum CpuFeatureEcx : u32 {
    kSse3  = 1u << 0,
    kSsse3 = 1u << 9,
    kSse41 = 1u << 19,
    kSse42 = 1u << 20,
    kAvx   = 1u << 28,
};

// Decoded feature set, plus the basic identity fields from CPUID.01h EAX.
struct CpuFeatures {
    u32 edxFlags = 0; // raw EDX feature bits (CPUID.01h)
    u32 ecxFlags = 0; // raw ECX feature bits (CPUID.01h)
    u32 stepping = 0; // EAX[3:0]
    u32 model = 0;    // EAX[7:4]  (+ extended model when family==6 or 15)
    u32 family = 0;   // EAX[11:8] (+ extended family when family==15)

    bool has(CpuFeatureEdx f) const { return (edxFlags & f) != 0; }
    bool has(CpuFeatureEcx f) const { return (ecxFlags & f) != 0; }
};

// Decode the feature/identity fields from raw CPUID.01h register values. This is
// the load-bearing logic: extended family/model folding matches the Intel rule
// (extended fields apply when family==0x0F, extended model also when ==0x06).
CpuFeatures DecodeFeatures(u32 eax, u32 ecx, u32 edx);

// Query CPUID.01h through the shim and decode it.
CpuFeatures DetectFeatures(CpuidShim& shim);

} // namespace guild::config
