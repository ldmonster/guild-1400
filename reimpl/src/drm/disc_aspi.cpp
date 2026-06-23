#include "drm/disc_aspi.h"

#include <cstring>
#include <vector>

// =============================================================================
// guild::drm — Disc ASPI SCSI command builders + response parsers, 1:1.
//
// Each builder reproduces the original's fill of the static SRB_ExecSCSICmd at
// byte_1460540 field-by-field (same offset, same constant), submits it through
// the DiscAspi hooks boundary (SendASPI32Command + WaitForSingleObject), and
// returns the same success test (srb.status == kSS_COMP) / parses the reply
// identically. See disc_aspi.h for the SRB layout and the hardware-boundary
// rationale.
//
// LP64: pointer-bearing SRB fields (+0x10 SRB_BufPointer, +0x18 SRB_PostProc)
// are stored in native-width slots (g_srb.bufNative / g_srb.postNative); their
// 0x50-image dwords stay 0 (the original's runtime pointer is not a constant and
// is never byte-tested). All counts/flags/CDB bytes sit at their exact offsets.
// =============================================================================

namespace guild::drm {

// byte_145A790[9] — recovered with get_bytes @ 0x145A790.
const u8 kObfTable[9] = {0xac, 0x35, 0xc3, 0x9d, 0x54, 0x10, 0xc6, 0x7d, 0xfa};

namespace {
InertDiscAspi      g_inert;
DiscAspi*          g_aspi = nullptr;
InertScsiEnumHooks g_inertEnum;
ScsiEnumHooks*     g_enum = nullptr;
ScsiAddr           g_addr;     // dword_145A13C / _A140 / _A144 (all 0 in image)
u32                g_counter = 0x113;  // dword_145A014 initial image value
} // namespace

DiscAspi& ActiveDiscAspi() { return g_aspi ? *g_aspi : g_inert; }
void      SetDiscAspi(DiscAspi* backend) { g_aspi = backend; }
ScsiEnumHooks& ActiveScsiEnumHooks() { return g_enum ? *g_enum : g_inertEnum; }
void           SetScsiEnumHooks(ScsiEnumHooks* hooks) { g_enum = hooks; }
ScsiAddr&      DefaultScsiAddr() { return g_addr; }
u32&           ProtectionCounter() { return g_counter; }

namespace {

// The original keeps one static SRB (byte_1460540, 0x50 bytes). We mirror that
// with a single file-static instance so repeated calls reuse it exactly.
SrbExecScsiCmd g_srb;

// Zero the full SRB (image + native slots) — mirrors VIBE_Mem_Set(&srb, 0, 0x50)
// plus resets the native pointer slots that live outside the image.
void ZeroSrb() {
    std::memset(&g_srb.img, 0, sizeof(g_srb.img));
    g_srb.bufNative = nullptr;
    g_srb.postNative = nullptr;
}

// Submit + wait + success test, common tail of every builder:
//   dword_145BA0C = dword_145A134(&srb);   // SendASPI32Command
//   dword_145F360(dword_1459FB8, -1);      // WaitForSingleObject(hEvent, INFINITE)
//   return srb.status == 1;
bool SubmitAndWait(SrbExecScsiCmd& srb) {
    ActiveDiscAspi().sendAspiCommand(srb);
    return srb.img.status == kSS_COMP;
}

} // namespace

// gilde.exe 0x140e610 — VIBE_Disc_AspiReadTocBounds
int AspiReadTocBounds(int /*a1*/, u32* a2, u32* a3, int /*a4*/) {
    int result = 0;
    u8 v5[0x1C];
    std::memset(v5, 0, sizeof(v5));            // VIBE_Mem_Set(v5, 0, 0x1C)
    ZeroSrb();                                 // VIBE_Mem_Set(&srb, 0, 0x50)
    g_srb.img.cmd      = 2;                     // byte_1460540
    g_srb.img.haId     = g_addr.haId;           // byte_1460542 = dword_145A13C
    g_srb.img.target   = g_addr.target;         // byte_1460548 = dword_145A140
    g_srb.img.lun      = g_addr.lun;            // byte_1460549 = dword_145A144
    g_srb.img.bufLen   = 28;                    // dword_146054C
    g_srb.bufNative    = v5;                    // dword_1460550 = (int)v5 (native slot)
    g_srb.img.senseLen = 14;                    // byte_1460554
    g_srb.img.cdbLen   = 10;                    // byte_1460555
    g_srb.postNative   = nullptr;               // dword_1460558 = hEvent (boundary)
    g_srb.img.flags    = 72;                    // byte_1460543 = 0x48 (DIR_IN|EVENT)
    g_srb.img.cdb[0]   = 67;                    // byte_1460570 = 0x43 READ TOC
    g_srb.img.cdb[1]   = 0;                     // byte_1460571
    g_srb.img.cdb[2]   = 4;                     // byte_1460572 format 4 (raw TOC / ATIP)
    g_srb.img.cdb[7]   = 0;                     // byte_1460577
    g_srb.img.cdb[8]   = 28;                    // byte_1460578 alloc len = 28
    g_srb.img.cdb[9]   = 0;                     // byte_1460579

    bool ok = SubmitAndWait(g_srb);

    if (ok) {
        unsigned v12 = v5[1] + (v5[0] << 8);    // TOC data length (big-endian)
        if (v12 < 0xD) {
            *a2 = 0;
            *a3 = 0;
            result = 0;
            (void)result;
        } else {
            // v7 @ v5+7 (dword), v8 @ v5+0x0B (dword): the two LBAs, byteswapped.
            u32 v7, v8;
            std::memcpy(&v7, &v5[7], 4);
            std::memcpy(&v8, &v5[11], 4);
            *a2 = ((v7 & 0xFFu) << 24) | (((v7 >> 8) & 0xFFu) << 16) |
                  (((v7 >> 16) & 0xFFu) << 8) | ((v7 >> 24) & 0xFFu);
            *a3 = ((v8 & 0xFFu) << 24) | (((v8 >> 8) & 0xFFu) << 16) |
                  (((v8 >> 16) & 0xFFu) << 8) | ((v8 >> 24) & 0xFFu);
            // v6 @ v5+6: control-bit flags (v6&1, &2, &4) — computed but the
            // original discards them (locals v9/v10/v11).
        }
        return 1;
    }
    *a2 = 0;
    *a3 = 0;
    return 0;
}

// gilde.exe 0x1412460 — VIBE_Disc_AspiSetSpeed
int AspiSetSpeed(int a1, int a2) {
    ZeroSrb();
    g_srb.img.cmd      = 2;
    g_srb.img.haId     = g_addr.haId;
    g_srb.img.target   = g_addr.target;
    g_srb.img.lun      = g_addr.lun;
    g_srb.img.senseLen = 14;
    g_srb.img.cdbLen   = 6;
    g_srb.postNative   = nullptr;
    g_srb.img.flags    = 80;                       // 0x50 (DIR_OUT|EVENT)
    g_srb.img.cdb[0]   = 27;                       // byte_1460570 = 0x1B
    g_srb.img.cdb[1]   = static_cast<u8>(32 * g_addr.lun); // byte_1460571 = 32*lun
    if (a2) g_srb.img.cdb[4] |= 1u;                // byte_1460574 |= 1
    if (a1) g_srb.img.cdb[4] |= 2u;                // byte_1460574 |= 2
    return SubmitAndWait(g_srb) ? 1 : 0;
}

// gilde.exe 0x1412660 — VIBE_Disc_AspiSetReadSpeed
int AspiSetReadSpeed(int a1) {
    ZeroSrb();
    g_srb.img.cmd      = 2;
    g_srb.img.haId     = g_addr.haId;
    g_srb.img.target   = g_addr.target;
    g_srb.img.lun      = g_addr.lun;
    g_srb.img.senseLen = 14;
    g_srb.img.cdbLen   = 12;
    g_srb.postNative   = nullptr;
    g_srb.img.flags    = 80;                       // 0x50
    g_srb.img.cdb[0]   = 0xBB;                      // byte_1460570 = -69 = 0xBB SET CD SPEED
    if (a1 == 256) {
        g_srb.img.cdb[2] = 0xFF;                    // byte_1460572
        g_srb.img.cdb[3] = 0xFF;                    // byte_1460573 max read speed
    } else {
        // (unsigned __int16)((1764*a1 + 9)/10): high byte /256, low byte trunc.
        int kb = (1764 * a1 + 9) / 10;
        u16 v = static_cast<u16>(kb);
        g_srb.img.cdb[2] = static_cast<u8>(v / 256); // byte_1460572
        g_srb.img.cdb[3] = static_cast<u8>(kb);      // byte_1460573 low byte
    }
    g_srb.img.cdb[4]   = 0xFF;                       // byte_1460574
    g_srb.img.cdb[5]   = 0xFF;                       // byte_1460575 write speed = max
    return SubmitAndWait(g_srb) ? 1 : 0;
}

// gilde.exe 0x1412950 — VIBE_Disc_AspiReadTocAndDecode
int AspiReadTocAndDecode(u8 a1, u8 a2, u8 a3, u8* a4,
                         u32* a5, u32* a6, int a7) {
    int v16 = 0;
    int v13 = 0;
    u8  v8 = 0;
    int v12 = 0;
    char v15 = 0;
    std::memset(a4, 0, 4);                       // VIBE_Mem_Set(a4, 0, 4)
    ZeroSrb();
    g_srb.img.cmd      = 2;
    g_srb.img.haId     = a1;
    g_srb.img.target   = a2;
    g_srb.img.lun      = a3;
    g_srb.img.bufLen   = 16;                      // dword_146054C
    g_srb.bufNative    = a4;                      // dword_1460550 = a4 (native slot)
    g_srb.img.senseLen = 14;
    g_srb.img.cdbLen   = 10;
    g_srb.postNative   = nullptr;
    g_srb.img.flags    = 72;                      // 0x48 (DIR_IN|EVENT)
    g_srb.img.cdb[0]   = 66;                       // byte_1460570 = 0x42 READ SUB-CHANNEL/TOC MSF
    g_srb.img.cdb[1]   = static_cast<u8>((32 * a3) | 2); // byte_1460571 MSF bit
    g_srb.img.cdb[2]   = 64;                       // byte_1460572 = 0x40
    g_srb.img.cdb[3]   = 1;                        // byte_1460573
    g_srb.img.cdb[8]   = 16;                       // byte_1460578 alloc len = 16

    bool ok = SubmitAndWait(g_srb);
    if (ok) {
        // *a5 = 75*buf[10] + 4500*buf[9] + buf[11] - 150  (MSF->LBA)
        *a5 = 75u * a4[10] + 4500u * a4[9] + a4[11] - 150;
        std::memcpy(a6, &a4[12], 4);             // *a6 = *(DWORD*)(a4+12)
        v16 = 1;
    } else {
        *a5 = 0;
        *a6 = 0;
        v16 = 0;
    }
    *a5 -= kObfTable[g_counter % 9];             // byte_145A790[dword_145A014 % 9]

    if (a7) {
        for (int i = 0; i < 4; ++i) {
            int v17 = (static_cast<int>(*a5) >> (4 * i)) & 0xF;
            v13 += (static_cast<int>(*a5) >> (4 * i)) & 0xF;
            if (i < 3)
                v8 += static_cast<u8>(*a5 >> (4 * i));
            v12 |= v17 << (8 * i);
        }
        v12 |= (static_cast<u8>(v13) >> 4) << 28;
        v12 |= (v13 & 0xF) << 20;
        v12 |= (v8 >> 4) << 12;
        v12 |= 16 * (v8 & 0xF);
        u8* v14 = reinterpret_cast<u8*>(&v12);
        int v11 = static_cast<int>(*a5) & 0xF;
        int j;
        for (j = 3; j > 0; --j) {
            v14[j] ^= kObfTable[(j + v11) % 9];
            v15 += static_cast<char>(v14[j]);
        }
        v14[j] ^= static_cast<u8>(v15);          // j == 0 here
        *a5 = static_cast<u32>(v12);
    }
    return v16;
}

// gilde.exe 0x1412e10 — VIBE_Disc_AspiReadRawSector
int AspiReadRawSector(u8 a1, u8 a2, u8 a3, u8* a4, u32 a5, int a6) {
    std::memset(a4, 0, 4);
    ZeroSrb();
    g_srb.img.cmd    = 2;
    g_srb.img.haId   = a1;
    g_srb.img.target = a2;
    g_srb.img.lun    = a3;
    g_srb.img.flags  = 8;                          // byte_1460543 = 8 (DIR_IN)
    if (a6 == 0)       g_srb.img.bufLen = 2352;    // 0x930
    else if (a6 == 1)  g_srb.img.bufLen = 2368;    // 0x940
    else if (a6 == 2)  g_srb.img.bufLen = 2448;    // 0x990
    g_srb.bufNative  = a4;                         // dword_1460550 = a4 (native slot)
    g_srb.img.senseLen = 14;
    g_srb.img.cdbLen   = 12;
    g_srb.postNative   = nullptr;
    g_srb.img.flags   |= 0x40u;                    // |= EVENT_NOTIFY -> 0x48
    g_srb.img.cdb[0]   = 0xD8;                      // byte_1460570 = -40 = 0xD8 READ CD-DA
    g_srb.img.cdb[1]   = static_cast<u8>(32 * a3); // byte_1460571
    g_srb.img.cdb[2]   = static_cast<u8>(a5 >> 24);// byte_1460572 LBA big-endian
    g_srb.img.cdb[3]   = static_cast<u8>(a5 >> 16);// byte_1460573
    g_srb.img.cdb[4]   = static_cast<u8>(a5 >> 8); // byte_1460574
    g_srb.img.cdb[5]   = static_cast<u8>(a5);      // byte_1460575
    g_srb.img.cdb[6]   = 0;                         // byte_1460576
    g_srb.img.cdb[7]   = 0;                         // byte_1460577
    g_srb.img.cdb[8]   = 0;                         // byte_1460578
    g_srb.img.cdb[9]   = 1;                         // byte_1460579 transfer length = 1 sector
    if (a6 == 0)       g_srb.img.cdb[10] = 0;       // byte_146057A subchannel sel
    else if (a6 == 1)  g_srb.img.cdb[10] = 1;
    else if (a6 == 2)  g_srb.img.cdb[10] = 2;
    return SubmitAndWait(g_srb) ? 1 : 0;
}

// gilde.exe 0x1412fd0 — VIBE_Disc_AspiReadSectorEcc
int AspiReadSectorEcc(u8 a1, u8 a2, u8 a3, u8* a4, u32 a5, int a6) {
    int v7;
    std::memset(a4, 0, 4);
    ZeroSrb();
    g_srb.img.cmd    = 2;
    g_srb.img.haId   = a1;
    g_srb.img.target = a2;
    g_srb.img.lun    = a3;
    g_srb.img.flags  = 8;
    if (a6 == 0)       g_srb.img.bufLen = 2352;
    else if (a6 == 1)  g_srb.img.bufLen = 2368;
    else if (a6 == 2)  g_srb.img.bufLen = 2448;
    g_srb.bufNative  = a4;                         // dword_1460550 = a4 (native slot)
    g_srb.img.senseLen = 14;
    g_srb.img.cdbLen   = 12;
    g_srb.postNative   = nullptr;
    g_srb.img.flags   |= 0x40u;                    // -> 0x48
    g_srb.img.cdb[0]   = 0xBE;                      // byte_1460570 = -66 = 0xBE READ CD
    g_srb.img.cdb[2]   = static_cast<u8>(a5 >> 24);// byte_1460572 LBA big-endian
    g_srb.img.cdb[3]   = static_cast<u8>(a5 >> 16);// byte_1460573
    g_srb.img.cdb[4]   = static_cast<u8>(a5 >> 8); // byte_1460574
    g_srb.img.cdb[5]   = static_cast<u8>(a5);      // byte_1460575
    g_srb.img.cdb[6]   = 0;                         // byte_1460576
    g_srb.img.cdb[7]   = 0;                         // byte_1460577
    g_srb.img.cdb[8]   = 1;                         // byte_1460578 transfer length = 1 sector
    g_srb.img.cdb[9]   = 0xF8;                      // byte_1460579 = -8 = 0xF8 sync+hdr+user+EDC/ECC
    if (a6 == 0)       g_srb.img.cdb[10] = 0;       // byte_146057A subchannel selection
    else if (a6 == 1)  g_srb.img.cdb[10] = 2;
    else if (a6 == 2)  g_srb.img.cdb[10] = 1;

    if (SubmitAndWait(g_srb))
        return 1;
    v7 = 0;
    // On failure: VIBE_Mem_Set(&unk_145F380, 0, 0x12), then
    // VIBE_Mem_MoveOverlapping(&unk_145F380, byte_1460580 /*sense*/, 0xE).
    // unk_145F380 is the engine's sense-stash scratch; the sense lives in
    // g_srb.img.sense (SRB+0x40 = byte_1460580). We mirror the copy into a static
    // stash so callers that read it observe identical bytes.
    {
        static u8 senseStash[0x12];
        std::memset(senseStash, 0, sizeof(senseStash));       // 0x12 bytes
        std::memmove(senseStash, g_srb.img.sense, 0xE);       // 14 bytes
    }
    return v7;
}

// gilde.exe 0x1413430 — VIBE_Disc_AspiReadSector
int AspiReadSector(u8 a1, u8 a2, u8 a3, u8* a4, u32 a5, int a6) {
    std::memset(a4, 0, 4);
    ZeroSrb();
    g_srb.img.cmd    = 2;
    g_srb.img.haId   = a1;
    g_srb.img.target = a2;
    g_srb.img.lun    = a3;
    g_srb.img.flags  = 8;
    g_srb.img.bufLen = a6 ? 2048u : 0u;          // dword_146054C
    g_srb.bufNative  = a4;                        // dword_1460550 = a4 (native slot)
    g_srb.img.senseLen = 14;
    g_srb.img.cdbLen   = 10;
    g_srb.postNative   = nullptr;
    g_srb.img.flags   |= 0x40u;                   // -> 0x48
    g_srb.img.cdb[0]   = 40;                       // byte_1460570 = 0x28 READ(10)
    g_srb.img.cdb[1]   = static_cast<u8>(32 * a3);// byte_1460571
    if (!a6)
        a5 -= kObfTable[(g_counter - 1) % 9];     // byte_145A790[(dword_145A014-1)%9]
    g_srb.img.cdb[2]   = static_cast<u8>(a5 >> 24);// byte_1460572 LBA big-endian
    g_srb.img.cdb[3]   = static_cast<u8>(a5 >> 16);// byte_1460573
    g_srb.img.cdb[4]   = static_cast<u8>(a5 >> 8); // byte_1460574
    g_srb.img.cdb[5]   = static_cast<u8>(a5);      // byte_1460575
    g_srb.img.cdb[8]   = static_cast<u8>(a6 != 0); // byte_1460578 transfer length = (a6?1:0)
    return SubmitAndWait(g_srb) ? 1 : 0;
}

// gilde.exe 0x1413790 — VIBE_Disc_AspiTestUnitReady
int AspiTestUnitReady(u8 a1, u8 a2, u8 a3, int /*a4*/) {
    ZeroSrb();
    g_srb.img.cmd    = 2;
    g_srb.img.haId   = a1;
    g_srb.img.target = a2;
    g_srb.img.lun    = a3;
    g_srb.img.senseLen = 14;
    g_srb.img.cdbLen   = 6;
    g_srb.postNative   = nullptr;
    g_srb.img.flags    = 80;                      // 0x50 (DIR_OUT|EVENT)
    g_srb.img.cdb[0]   = 0;                        // byte_1460570 = 0x00 TEST UNIT READY
    g_srb.img.cdb[1]   = static_cast<u8>(32 * a3);// byte_1460571
    return SubmitAndWait(g_srb) ? 1 : 0;
}

// gilde.exe 0x1413900 — VIBE_Disc_AspiInquiry
int AspiInquiry(u8 a1, u8 a2, u8 a3, u8* a4) {
    std::memset(a4, 0, 4);                       // VIBE_Mem_Set(a4, 0, 4)
    ZeroSrb();
    g_srb.img.cmd    = 2;
    g_srb.img.haId   = a1;
    g_srb.img.target = a2;
    g_srb.img.lun    = a3;
    g_srb.img.bufLen   = 36;                      // dword_146054C = 36
    g_srb.bufNative    = a4;                      // dword_1460550 = a4 (native slot)
    g_srb.img.senseLen = 14;
    g_srb.img.cdbLen   = 6;                        // byte_1460555 = 6
    g_srb.postNative   = nullptr;
    g_srb.img.flags    = 72;                      // 0x48 (DIR_IN|EVENT)
    g_srb.img.cdb[0]   = 18;                       // byte_1460570 = 0x12 INQUIRY
    g_srb.img.cdb[4]   = 36;                       // byte_1460574 = alloc len = 36
    return SubmitAndWait(g_srb) ? 1 : 0;
}

// gilde.exe 0x1412120 — VIBE_Disc_EnumScsiDevices
//
// Walks the SCSI device-interface list (SetupDi*) and probes each device; the
// per-device work is the hardware boundary. The pure shape:
//   * the descriptor v9[8] primed by the original: v9[0..4]=0, v9[5]=5,
//   * first enumerate (enumFirst); on lastError()==1008 retry via fallback,
//   * getDetail size-probe (must fail with lastError()==122), then fill into a
//     (v4+3)&0xFC-byte buffer (the original alloca'd this), v8 aliases its head,
//   * openDevice; then for each entry i in [0, *v8): probe v8[2*i+1]; first hit
//     returns 1, else 0.
//
// Exposed as EnumScsiDevicesAspi() — the live tree already has the stubbed
// EnumScsiDevices() for this same 0x1412120 (see disc_aspi.h note / drm_stub).
int EnumScsiDevicesAspi() {
    ScsiEnumHooks& h = ActiveScsiEnumHooks();

    // v9[8]: descriptor primed by the original — v9[0..4]=0, v9[5]=5, v9[6..7]=0.
    u8 v9[8] = {0, 0, 0, 0, 0, 5, 0, 0};

    void* set = h.openClassDevs();              // v7 = dword_145F064()
    void* detail = nullptr;                     // v10
    if (!h.enumFirst(set, 8, &detail)) {        // dword_145E1E8(v7, 8, 0, &v10)
        if (h.lastError() != 1008)              // dword_145D680() != 1008
            return 0;
        set = h.openClassDevsFallback();        // v7 = dword_145B910()
        if (!h.enumFirst(set, 8, &detail))      // dword_145B908(v7, 8, &v10)
            return 0;
    }

    // dword_145AD1C(v10, 2, 0, 0, &v4): size probe. Original: nonzero -> fail.
    u32 v4 = 0;
    if (h.getDetail(detail, 2, nullptr, 0, &v4))
        return 0;
    if (h.lastError() != 122)                   // ERROR_INSUFFICIENT_BUFFER
        return 0;

    // Original alloca's (v4+3)&0xFC bytes; v8 aliases the buffer head. We model
    // the buffer with a vector (size>=4 so v8[0] is always readable).
    u32 allocSize = (v4 + 3) & 0xFCu;
    std::vector<u8> buf(allocSize < 4u ? 4u : allocSize, 0);
    u32* v8 = reinterpret_cast<u32*>(buf.data());

    // dword_145AD1C(v10, 2, v8, v4, &v4): fill. Original: !success -> fail.
    if (!(h.getDetail(detail, 2, v8, v4, &v4) == 0))
        return 0;

    // dword_145A5DC(v9, 2, 32, 544, 0,0,0,0,0,0, &v6): open the device file.
    void* v6 = nullptr;
    if (!h.openDevice(v9, 2, 32, 544, &v6))
        return 0;

    // Loop: count = *v8; entry id = v8[2*i + 1]. probe -> first hit returns 1.
    int v5 = 0;
    for (u32 i = 0; i < v8[0]; ++i) {
        if (h.probeDevice(v8[2 * i + 1], v6))
            return 1;
    }
    return v5;
}

} // namespace guild::drm
