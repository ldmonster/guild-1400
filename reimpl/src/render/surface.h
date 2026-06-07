#pragma once
#include "guild/common/types.h"
#include "render/types.h"
#include "render/colorformat.h"

// Software (system-memory) surface: allocation, clone, per-pixel get/set, and
// the 2D primitives (hline, Bresenham line, rect outline) from gilde.exe gfx.c.
// The DDraw-backed branches of the originals (Blt/Lock/TextOutA) are the present
// layer and are intentionally NOT reproduced here — see render/02 recon.
//
// All drawing goes through the active ColorFormat (channel shifts). The original
// read those from a global; we attach a format to each Surface for testability.
namespace guild::render {

// gilde.exe 0x42311c — VIBE_Surface_Create (system-memory path only).
// Allocates a Surface and its pixel buffer for the given size/bpp. pitch =
// (bpp>>3)*width, widthPx = pitch/(bpp>>3). Clip rect defaults to [0,w)x[0,h).
// Returns nullptr on allocation failure. Caller owns it (free with SurfaceDestroy).
Surface* SurfaceCreate(int width, int height, u8 bpp,
                       const ColorFormat& fmt = Format565());

// gilde.exe 0x4234b0 — VIBE_Surface_Destroy. Frees pixel buffer + record.
int SurfaceDestroy(Surface* s);

// gilde.exe 0x423c14 — VIBE_Surface_Clone. New surface, same geometry; copies
// pixel bytes across (the original blits via VIBE_Result_Finalize).
Surface* SurfaceClone(const Surface* s);

// gilde.exe 0x423620 — VIBE_Surface_GetCaps. Software path: copy caps dword.
bool SurfaceGetCaps(const Surface* s, u32* outCaps);

// gilde.exe 0x423e5c — VIBE_Surface_SetPixelRgb (x@eax,y@edx,g@cl,r@bl,b,surf).
// Clipped write honouring bpp (8 = luma avg, 15/16 = packed, 24 = BGR, 32 = packed).
int SurfaceSetPixelRgb(Surface* s, int x, int y, u8 r, u8 g, u8 b);

// gilde.exe 0x423d74 — VIBE_Surface_GetPixelRgb. Reads one pixel into out[3] (RGB).
void SurfaceGetPixelRgb(const Surface* s, int x, int y, u8 out[3]);

// gilde.exe 0x423ffc — VIBE_Surface_DrawHLine. Horizontal run of `len` pixels.
int SurfaceDrawHLine(Surface* s, int x, int y, int len, u8 r, u8 g, u8 b);

// gilde.exe 0x424044 — VIBE_Surface_DrawLine. Bresenham line (x0,y0)->(x1,y1).
int SurfaceDrawLine(Surface* s, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b);

// gilde.exe 0x4242d4 — VIBE_Surface_DrawRectOutline. (x,y) origin, w x h border.
int SurfaceDrawRectOutline(Surface* s, int x, int y, int w, int h, u8 r, u8 g, u8 b);

// Fill the whole surface (software path of VIBE_Surface_ColorFill: zero the buffer).
void SurfaceColorFill(Surface* s, u8 r, u8 g, u8 b);

} // namespace guild::render
