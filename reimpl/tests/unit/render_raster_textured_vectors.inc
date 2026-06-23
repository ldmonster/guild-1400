// AUTO-GENERATED golden vectors for the RGBZ affine-textured rasterizer.
// Computed independently by /tmp/raster_ref_w4.py (a pure-Python replica of
// the captured gilde.exe 0x5F6C30/0x5F6B34/0x5F71AD decompiles: 16.16 edge
// walk, TEXEL-unit UV * 65536 (the v53 = mipWidth*65536 scale with the
// caller-side mipWidth factor), span texel fetch pal[lightRow8|idx]).
// Triangle: v0=(2,2,u0,v0) v1=(11,4,u4,v0) v2=(4,11,u0,v4) (UVs in texels),
// 4x4 tex idx=i, palette idx->0x100+idx, light=0, 16x16 16bpp surface.
struct GoldenTexel { int x, y; unsigned short v; };
static const GoldenTexel kRgbzTri[] = {
    {3, 3, 0x100},
    {4, 3, 0x100},
    {5, 3, 0x101},
    {6, 3, 0x101},
    {3, 4, 0x100},
    {4, 4, 0x100},
    {5, 4, 0x101},
    {6, 4, 0x101},
    {7, 4, 0x102},
    {8, 4, 0x102},
    {9, 4, 0x103},
    {10, 4, 0x103},
    {3, 5, 0x104},
    {4, 5, 0x104},
    {5, 5, 0x105},
    {6, 5, 0x101},
    {7, 5, 0x102},
    {8, 5, 0x102},
    {9, 5, 0x102},
    {3, 6, 0x104},
    {4, 6, 0x104},
    {5, 6, 0x104},
    {6, 6, 0x105},
    {7, 6, 0x105},
    {8, 6, 0x106},
    {4, 7, 0x108},
    {5, 7, 0x108},
    {6, 7, 0x105},
    {7, 7, 0x105},
    {4, 8, 0x108},
    {5, 8, 0x108},
    {6, 8, 0x109},
    {4, 9, 0x10C},
    {5, 9, 0x108},
    {4, 10, 0x10C},
};
static const int kRgbzTri_count = 35;
