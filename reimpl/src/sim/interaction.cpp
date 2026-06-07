#include "sim/interaction.h"

#include <cstring>

// Faithful 1:1 port of the interaction resolution + dispatch from gilde.exe.
//
// VIBE_Interaction_Handler parses a '|'-delimited handler string. The original
// copies the source into a 0x400-byte stack scratch two bytes at a time (the
// `*v3 = *v4; v3[1]=v4[1]` unrolled copy), scans for token boundaries marked by
// the byte 124 ('|'), advances `field` boundaries, NUL-terminates the chosen
// token, and copies it out. The control flow below is a behaviour-identical
// transcription; the two-byte unrolling is folded into ordinary byte copies
// (provably identical: it copies a NUL-terminated string verbatim).

namespace guild::sim {

namespace {

// gilde.exe inner token scanner: advance `p` two bytes at a time to the next
// '|' (byte 124); returns nullptr on end-of-string (matches the original setting
// the cursor to 0). NOTE the original starts the scan one byte INTO the string
// (v8 = v14 == &scratch[1]), so callers seed `p` accordingly.
char* ScanToBar(char* p) {
    for (;;) {
        char c = *p;
        if (c == 124)
            return p;
        if (c == 0)
            return nullptr;
        char c2 = *++p;
        if (c2 == 124)
            return p;
        ++p;
        if (c2 == 0)
            return nullptr;
    }
}

} // namespace

// gilde.exe 0x411f8c — VIBE_Interaction_Handler.
// Faithful: copies the descriptor string into scratch (v13), then scans for the
// (field)-th '|' boundary starting one byte in (v8 = &scratch[1]). For field 0
// the head token "open" is returned; for field N>0 the returned token begins at
// the N-th '|' and INCLUDES that leading '|' (e.g. "|close"), exactly as the
// binary does. The selected token is NUL-terminated and copied to `out`.
char InteractionResolveHandler(const u8* descTable, int type, int field, char* out) {
    char scratch[0x400];

    const char* src = reinterpret_cast<const char*>(
        descTable + kInteractionDescStride * type + kInteractionDescStringOffset);
    {
        char* d = scratch;
        for (;;) {
            char a = src[0];
            d[0] = a;
            if (!a)
                break;
            char b = src[1];
            d[1] = b;
            src += 2;
            d += 2;
            if (!b)
                break;
        }
    }

    char* v8 = scratch + 1;        // v8 = v14 (one byte into the copy)
    char* v5 = scratch;            // v5 default token start = whole string
    v8 = ScanToBar(v8);

    char result = 0;
    int v9 = 0;
    if (field > 0) {
        while (v8) {
            v5 = v8++;             // v5 = boundary; step past the '|'
            ++v9;
            v8 = ScanToBar(v8);
            if (v9 >= field) {
                if (!v8)
                    return result;  // ran off the end before terminating
                break;
            }
        }
    }

    if (v8) {
        char* o = out;
        *v8 = 0;                   // terminate the selected token
        const char* s = v5;
        for (;;) {
            result = *s;
            o[0] = result;
            if (!result)
                break;
            result = s[1];
            s += 2;
            o[1] = result;
            o += 2;
            if (!result)
                break;
        }
    }
    return result;
}

bool InteractionLookupToken(const u8* descTable, int type, int field,
                            char* out, int outCap) {
    if (outCap <= 0)
        return false;
    out[0] = 0;
    InteractionResolveHandler(descTable, type, field, out);
    out[outCap - 1] = 0;
    return out[0] != 0;
}

// gilde.exe 0x40e6c0 — VIBE_GameObject_DispatchInteractions.
void InteractionDispatch(const InteractionEvent* queue, int count,
                         InteractionResultFn fn, i32 ctxA, i32 ctxB) {
    // The original loop bound is 16 * bucketCount over a byte cursor advancing
    // 16 at a time; equivalently iterate `count` events.
    for (int i = 0; i < count; ++i) {
        const InteractionEvent& ev = queue[i];
        // VIBE_Result_Handler_Interaction(v6, v7, v4, v5, ctxA, v6, v7, ctxB)
        //   v6 = +0, v7 = +4, v5 = +8, v4 = +12.
        if (fn)
            fn(ev.objectA, ev.fieldB, ev.paramD, ev.paramC, ctxA, ctxB);
    }
    // dword_62D2E0[bucket] = 0; (count reset is the caller's responsibility here)
}

} // namespace guild::sim
