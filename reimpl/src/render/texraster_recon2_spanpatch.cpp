// =============================================================================
// guild::render — texraster_recon2: span-constant patchers (BLEND/MASKED/OR).
// 1:1 reconstruction of the five PatchSpanConstants* SMC functions; the SMC
// delivery is translated to a parameter struct exactly as raster_textured.cpp's
// BuildSpanTexParams does for the plain variant. See the header banner.
// =============================================================================
#include "render/texraster_recon2_spanpatch.h"

namespace guild::render {

// All five originals execute the identical six assignments (only the destination
// code address differs per variant). We reproduce the six source->field moves.
SpanPatchVariantParams BuildSpanPatchParams(SpanVariant variant,
                                            const SpanPatchSources& src)
{
    SpanPatchVariantParams p;
    p.variant    = variant;
    p.texBase    = src.texBase;     // imm <- unk_1406A8C
    p.palBase    = src.palBase;     // imm <- dword_1406A78
    p.uStepFrac  = src.uStepFrac;   // imm <- dword_13FC594
    p.lightStart = src.lightStart;  // imm <- dword_13FC5A8
    p.texelMask  = src.texelMask;   // imm <- dword_1406A88
    p.widthShift = src.widthShift;  // imm <- byte_1407A91
    return p;
}

SpanPatchVariantParams PatchSpanConstantsMasked(const SpanPatchSources& src)
{ return BuildSpanPatchParams(SpanVariant::Masked, src); }       // 0x5f753f

SpanPatchVariantParams PatchSpanConstantsBlend(const SpanPatchSources& src)
{ return BuildSpanPatchParams(SpanVariant::Blend, src); }        // 0x5f757e

SpanPatchVariantParams PatchSpanConstantsBlendMasked(const SpanPatchSources& src)
{ return BuildSpanPatchParams(SpanVariant::BlendMasked, src); }  // 0x5f75bd

SpanPatchVariantParams PatchSpanConstantsOr(const SpanPatchSources& src)
{ return BuildSpanPatchParams(SpanVariant::Or, src); }           // 0x5f75fc

SpanPatchVariantParams PatchSpanConstantsOrMasked(const SpanPatchSources& src)
{ return BuildSpanPatchParams(SpanVariant::OrMasked, src); }     // 0x5f763b

void Raster_NullStub17() { /* gilde.exe 0x5f71ac: retn */ }
void Raster_NullStub18() { /* gilde.exe 0x5f74ff: retn */ }

} // namespace guild::render
