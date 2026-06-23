// =============================================================================
// rain_grow_misc_recon.cpp — VIBE_Rain_GrowDropList (gilde.exe 0x4292b8).
// Strict 1:1 translation of the disassembly (the Hex-Rays pseudocode lost the
// per-slot pointer arithmetic, so this follows the asm directly).
// =============================================================================
#include "render/rain_grow_misc_recon.h"

namespace guild::render {

namespace {
// get_bytes; bit-exact. flt_6115F4..flt_611608.
constexpr double kMul0001  = 0.0010000000474974513; // flt_6115F4  (~1e-3)
constexpr float  kRandNorm = 3.0518509447574615e-05f; // flt_6115F8 (~1/32767)
constexpr float  kTwo      = 2.0f;                    // flt_6115FC
constexpr float  kQuarter  = 0.25f;                   // flt_611600
constexpr float  kBias005  = 0.05000000074505806f;    // flt_611604
constexpr float  kBiasNeg5 = -0.5f;                   // flt_611608
constexpr const char* kTag = "d3t:RainDropList";      // aD3tRaindroplis
} // namespace

// gilde.exe 0x4292b8 — VIBE_Rain_GrowDropList.
void Rain_GrowDropList(RainGrowSys& sys, i32 opCount, const i32* args, int argCount,
                       RainAllocFn alloc, RainFreeFn freeFn, RainRandFn rnd) {
    int ai = 0; // variadic cursor (ebp walk over the pushed ints)
    auto nextArg = [&]() -> i32 {
        return (ai < argCount) ? args[ai++] : 0;
    };

    // for ( i = 0; i < opCount; ++i )            /*0x4292d5*/
    for (i32 i = 0; i < opCount; ++i) {
        const i32 op = nextArg();                 /*0x4292d7 var_2C*/

        if (static_cast<u32>(op) > 1u) {          /*0x4292e3 cmp op,1; jnb*/
            if (op == 2) {                        /*0x42932b cmp op,2*/
                // ---- burst-spawn (op == 2) -----------------------  /*0x429330*/
                const i32 a = nextArg();          /*var_1C (arg_8)*/
                sys.f24 = static_cast<float>(static_cast<double>(a) * kMul0001); /*0x429343*/
                const i32 b = nextArg();          /*arg_C*/
                const i32 f2c = sys.f2c;          /*0x429351 [ebx+2Ch]*/
                sys.f38 = f2c;                    /*0x429356 [ebx+38h]*/
                sys.f28 = static_cast<float>(static_cast<double>(b) * kMul0001); /*0x42935c*/
                sys.f14 = sys.f1c;                /*0x429362 [ebx+14h]=[ebx+1Ch]*/
                sys.f18 = sys.f20;                /*0x429368 [ebx+18h]=[ebx+20h]*/
                sys.f3c = f2c + b;                /*0x429376 [ebx+3Ch]=eax+esi*/
            }
            // op > 2: nothing.                                       /*0x42932e jnz next*/
            continue; // loc_42930E -> ++i                            /*0x42930e*/
        }

        // op == 0 or op == 1: grow / activate path                   /*loc_4292E9*/
        const i32 count = nextArg();              /*0x4292e9 var_24*/
        if (count >= sys.capacity) {              /*0x4292f6 cmp count,[ebx+4]*/
            // ---- grow the backing array by +100 records ---------- /*loc_42937B*/
            const i32 newCap = count + 100;       /*0x42937b add eax,64h*/
            RainGrowDrop* nb = static_cast<RainGrowDrop*>(
                alloc(static_cast<unsigned>(40 * newCap), kTag)); /*0x42938a imul 28h*/
            if (!nb)                              /*0x429393 jz*/
                return;
            // copy 40*capacity bytes from old drops into the new block.
            const i32 oldBytes = 40 * sys.capacity; /*0x429395 imul [ebx+4],28h*/
            {
                const u8* src = reinterpret_cast<const u8*>(sys.drops); /*[ebx+10h]*/
                u8* dst = reinterpret_cast<u8*>(nb);
                for (i32 k = 0; k < oldBytes; ++k) dst[k] = src[k]; /*movsd/movsb*/
            }
            freeFn(sys.drops);                    /*0x4293b1 FreeDebug([ebx+10h])*/
            const i32 oldCount = sys.head;        /*0x4293b6 mov edi,[ebx] (raw int)*/
            sys.drops = nb;                       /*0x4293b8 [ebx+10h]=edx*/
            sys.capacity = newCap;                /*0x4293c2 [ebx+4]=var_20*/
            // RNG-seed slots [oldCount, newCap).                     /*0x4293d0 cmp edi,newCap*/
            for (i32 k = oldCount; k < newCap; ++k) {                 /*loc_4293D8*/
                RainGrowDrop& d = nb[k];
                d.f00 = static_cast<float>(
                    (static_cast<double>(rnd()) * kRandNorm + kBiasNeg5) * kTwo); /*0x4293f7*/
                d.f04 = static_cast<float>(
                    (static_cast<double>(rnd()) * kRandNorm + kBiasNeg5) * kTwo); /*0x429418*/
                d.f08 = static_cast<float>(
                    (static_cast<double>(rnd()) * kRandNorm + kBiasNeg5) * kTwo); /*0x42943a*/
                d.f0c = static_cast<float>(
                    static_cast<double>(rnd()) * kRandNorm * kQuarter + kQuarter); /*0x42945c*/
                d.f10 = static_cast<float>(
                    static_cast<double>(rnd()) * kRandNorm * kBias005 + kQuarter); /*0x42947e*/
                d.f14 = static_cast<float>(
                    static_cast<double>(rnd()) * kRandNorm * kBias005 + kQuarter); /*0x4294a4*/
            }
        }

        // loc_4292FE: branch on the opcode.                          /*0x4292fe*/
        if (op != 0) {
            // ---- activate (op == 1) ----------------------------- /*loc_4294B4*/
            sys.f08 = sys.head;                   /*0x4294b6 mov eax,[ebx]; mov [ebx+8],eax (raw int)*/
            sys.f0c = count;                      /*0x4294bd [ebx+0Ch]=var_24*/
            const i32 f2c = sys.f2c;              /*0x4294c0 [ebx+2Ch]*/
            sys.f30 = f2c;                        /*0x4294c3 [ebx+30h]*/
            sys.f34 = f2c + nextArg();            /*0x4294c6 add eax,arg_8*/
        } else {
            // op == 0: set head = count (raw int).                   /*0x429308*/
            sys.head = count;                     /*0x42930c mov [ebx],eax (eax=count)*/
        }
    }
}

} // namespace guild::render
