// AUTO-GENERATED golden vectors for the RGBZ affine-textured rasterizer.
// Computed independently by /tmp/rgbz_gold.py (a pure-Python replica of the
// gilde.exe 16.16 fixed-point edge-walk + span texel fetch). BIT-EXACT.
// Triangle: v0=(2,2,uv0,0) v1=(11,4,uv4,0) v2=(4,11,uv0,4), 4x4 tex idx=i,
// palette idx->0x100+idx, mipWidth(texScale)=4, into a 16x16 16bpp surface.
struct GoldenTexel { int x, y; unsigned short v; };
static const GoldenTexel kRgbzTri[] = {
    {3, 3, 0x10D},
    {4, 3, 0x10F},
    {5, 3, 0x10D},
    {6, 3, 0x10F},
    {3, 4, 0x10D},
    {4, 4, 0x10E},
    {5, 4, 0x10C},
    {6, 4, 0x10E},
    {7, 4, 0x100},
    {8, 4, 0x10E},
    {9, 4, 0x100},
    {10, 4, 0x10E},
    {3, 5, 0x10C},
    {4, 5, 0x10E},
    {5, 5, 0x100},
    {6, 5, 0x10E},
    {7, 5, 0x100},
    {8, 5, 0x10D},
    {9, 5, 0x10F},
    {3, 6, 0x10C},
    {4, 6, 0x10E},
    {5, 6, 0x10F},
    {6, 6, 0x10D},
    {7, 6, 0x10F},
    {8, 6, 0x10D},
    {4, 7, 0x10D},
    {5, 7, 0x10F},
    {6, 7, 0x10D},
    {7, 7, 0x10F},
    {4, 8, 0x10D},
    {5, 8, 0x10F},
    {6, 8, 0x10C},
    {4, 9, 0x10C},
    {5, 9, 0x10E},
    {4, 10, 0x10C},
};
static const int kRgbzTri_count = 35;
