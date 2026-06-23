// gilde.exe 0x140xxxx — Disc SPTI (SCSI Pass-Through Interface) command builders.
// 1:1 reconstruction of the copy-protection cluster's raw-SCSI MMC command
// builders. See disc_spti.h for the hardware-boundary / hook policy.
//
// Field offsets below are taken directly from the decompile/disasm: the two
// global request buffers are word_145B680 (read buf) and word_145EBC0 (subch
// buf); a byte_145XXXX global at absolute address A maps to struct offset
// (A - base), e.g. byte_145EBDC == word_145EBC0 + 0x1c == cdb[0], and
// byte_145EBF0 == word_145EBC0 + 0x30 == sense[0].

#include "drm/disc_spti.h"

#include <cstring>

namespace guild::drm {

namespace {

// gilde.exe 0x1421a20 — VIBE_Mem_Set: plain memset.
void MemSet(void* dst, int val, std::size_t n) { std::memset(dst, val, n); }

// gilde.exe 0x1421340 — VIBE_Mem_MoveOverlapping: memmove.
void MemMove(void* dst, const void* src, std::size_t n) { std::memmove(dst, src, n); }

// The two shared request buffers (process-global, like the originals).
SptdRequest g_readBuf{};   // word_145B680
SptdRequest g_subchBuf{};  // word_145EBC0

// unk_145F380 — 18-byte sense scratch.
std::array<u8, 18> g_senseScratch{};

// dword_1464CDC — shared big-endian byte-swap scratch (used across the DRM
// cluster; the TOC decode below stores each field here then swaps it).
u32 g_tocSwapScratch = 0;

// gilde.exe 0x145A790 — byte_145A790: 9-byte LBA-bias key table used by READ(10).
// Recovered with get_bytes: AC 35 C3 9D 54 10 C6 7D FA.
const u8 kLbaBiasTable[9] = {
    0xAC, 0x35, 0xC3, 0x9D, 0x54, 0x10, 0xC6, 0x7D, 0xFA,
};

// gilde.exe 0x145A014 — dword_145A014: rolling index into the bias table.
// Recovered value at capture time was 0x113 (275); READ(10) indexes
// kLbaBiasTable[(dword_145A014 - 1) % 9]. Kept as a mutable global so the
// READ(10) path matches the original's data flow exactly.
u32 g_biasIndex = 0x113;

DiscDevice g_inertDevice{};
DiscDevice* g_activeDevice = nullptr;

} // namespace

DiscDevice& GetDiscDevice() { return g_activeDevice ? *g_activeDevice : g_inertDevice; }
void SetDiscDevice(DiscDevice* dev) { g_activeDevice = dev; }

SptdRequest& ReadRequestBuffer() { return g_readBuf; }
SptdRequest& SubchRequestBuffer() { return g_subchBuf; }
std::array<u8, 18>& SenseScratch() { return g_senseScratch; }
u32& TocSwapScratch() { return g_tocSwapScratch; }
u32& BiasIndex() { return g_biasIndex; }

// -----------------------------------------------------------------------------
// gilde.exe 0x140e4f0 — VIBE_Disc_SptiSubmitRead
//   v2 = DeviceIoControl(hDev, 315396, &word_145B680, 592, &word_145B680,
//                        dword_145B68C + 80, &bytesReturned, 0);
//   if (byte_145B682 == 2) { v2 = 0; memset(unk_145F380,0,0x12);
//                            memmove(unk_145F380, byte_145B6B0, 0xE); }
//   byte_145B682 == word_145B680+0x02 == scsiStatus.
//   byte_145B6B0 == word_145B680+0x30 == sense[0].
// -----------------------------------------------------------------------------
int SptiSubmitRead() {
    int v2 = 0;
    int bytesReturned = 0; (void)bytesReturned;

    const u32 outSize = g_readBuf.dataTransferLength + 80; // dword_145B68C + 80
    v2 = GetDiscDevice().submit(kIoctlScsiPassThroughDirectRead, g_readBuf,
                                592u, outSize);

    if (g_readBuf.scsiStatus == kScsiStatusCheckCondition) {
        v2 = 0;
        MemSet(g_senseScratch.data(), 0, 0x12);                  // 18 bytes
        MemMove(g_senseScratch.data(), g_readBuf.sense, 0x0E);   // 14 bytes from +0x30
    }
    return v2;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x140e580 — VIBE_Disc_SptiSubmitSubchannel
//   v2 = DeviceIoControl(hDev, 315412, &word_145EBC0, 44, &word_145EBC0,
//                        dword_145EBCC + 80, &bytesReturned, 0);
//   Input size here is 44 (the SPTD Length), per the decompile.
//   if (byte_145EBC2 == 2) { same sense capture as SubmitRead, from byte_145EBF0 }.
// -----------------------------------------------------------------------------
int SptiSubmitSubchannel() {
    int v2 = 0;
    int bytesReturned = 0; (void)bytesReturned;

    const u32 outSize = g_subchBuf.dataTransferLength + 80; // dword_145EBCC + 80
    v2 = GetDiscDevice().submit(kIoctlScsiPassThroughDirectSubch, g_subchBuf,
                                44u, outSize);

    if (g_subchBuf.scsiStatus == kScsiStatusCheckCondition) {
        v2 = 0;
        MemSet(g_senseScratch.data(), 0, 0x12);
        MemMove(g_senseScratch.data(), g_subchBuf.sense, 0x0E);
    }
    return v2;
}

// Big-endian 32-bit swap exactly as the original does it through dword_1464CDC:
//   ((u8)x << 24) + (BYTE1(x) << 16) + (BYTE2(x) << 8) + HIBYTE(x).
// (Stores x into the shared scratch first, matching the observable side effect.)
static int SwapBe32ViaScratch(int x) {
    g_tocSwapScratch = (u32)x;
    u32 v = g_tocSwapScratch;
    return (int)(((v & 0xFFu) << 24)
               + (((v >> 8) & 0xFFu) << 16)
               + (((v >> 16) & 0xFFu) << 8)
               + ((v >> 24) & 0xFFu));
}

// -----------------------------------------------------------------------------
// gilde.exe 0x140e870 — VIBE_Disc_SptiReadTocBounds  (__stdcall(a1, a2, a3, a4))
//   READ TOC/PMA/ATIP, opcode 0x43 (67), cdb[2]=4 (format 4 / full TOC),
//   alloc len 0x1C (28) at cdb[7..8] = 00 1C. DataIn=1, DataTransferLength=28,
//   TimeOut=2000, DataBuffer=&v5 (local 28-byte reply), SenseInfoOffset=48.
//   On success and a non-zero TOC data length (v5[0]<<8)+v5[1]:
//     *a2 = BE32(dword at v5+0x07);  *a3 = BE32(dword at v5+0x0B);
//   and three flag bools (unused outputs) are derived from v5[6] bits 4/2/1.
//   v5 stack layout (disasm): v5[6] @+0, v6 @+6, v7(dword) @+7, v8(dword) @+0xB.
// -----------------------------------------------------------------------------
int SptiReadTocBounds(int /*a1*/, int* a2, int* a3, int /*a4*/) {
    u8 v5[28];   // [ebp-30h] TOC reply buffer (0x1C), filled by the device
    int v13 = 0;

    MemSet(v5, 0, 0x1C);
    MemSet(&g_subchBuf, 0, 0x50);

    g_subchBuf.length = 44;             // word_145EBC0
    g_subchBuf.pathId = 0;              // byte_145EBC3
    g_subchBuf.targetId = 0;            // byte_145EBC4
    g_subchBuf.lun = 0;                 // byte_145EBC5
    g_subchBuf.cdbLength = 10;          // byte_145EBC6
    g_subchBuf.senseInfoLength = 24;    // byte_145EBC7
    g_subchBuf.dataIn = 1;              // byte_145EBC8
    g_subchBuf.dataTransferLength = 28; // dword_145EBCC
    g_subchBuf.timeOutValue = 2000;     // dword_145EBD0
    g_subchBuf.dataBuffer = 0;          // dword_145EBD4 = (int)&v5 (reply via hook)
    g_subchBuf.senseInfoOffset = 48;    // dword_145EBD8
    g_subchBuf.cdb[0] = 67;             // byte_145EBDC  opcode 0x43 READ TOC
    g_subchBuf.cdb[1] = 0;              // byte_145EBDD
    g_subchBuf.cdb[2] = 4;              // byte_145EBDE  format 4 (full TOC)
    g_subchBuf.cdb[7] = 0;              // byte_145EBE3  alloc len hi
    g_subchBuf.cdb[8] = 28;             // byte_145EBE4  alloc len lo (0x1C)
    g_subchBuf.cdb[9] = 0;              // byte_145EBE5

    v13 = SptiSubmitSubchannel();
    if (v13) {
        // toc data length = ((v5[0] & 0xFF) << 8) + (v5[1] & 0xFF)
        if (((int)(v5[0] & 0xFF) << 8) + (int)(v5[1] & 0xFF)) {
            // *a2 = v7 (signed dword at v5+0x07), then BE32-swapped via scratch.
            int v7;  std::memcpy(&v7, &v5[7], 4);
            *a2 = v7;
            *a2 = SwapBe32ViaScratch(*a2);
            // *a3 = v8 (signed dword at v5+0x0B), same swap.
            int v8;  std::memcpy(&v8, &v5[0x0B], 4);
            *a3 = v8;
            *a3 = SwapBe32ViaScratch(*a3);
            // v6 = v5[6]; flag bools (local, unused by callers) from bits 4/2/1.
            char v6 = (char)v5[6];
            (void)((v6 & 4) != 0);   // v11
            (void)((v6 & 2) != 0);   // v10
            (void)((v6 & 1) != 0);   // v9
        } else {
            *a2 = 0;
            *a3 = 0;
            return 0;
        }
    } else {
        *a2 = 0;
        *a3 = 0;
    }
    return v13;
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1412560 — VIBE_Disc_SptiSetSpeed  (__stdcall(a1, a2))
//   READ buffer; CDB opcode 0x1B (27). 6-byte CDB, no data, TimeOut=200.
//   a2 -> cdb[4] |= 1; a1 -> cdb[4] |= 2.
// -----------------------------------------------------------------------------
int SptiSetSpeed(int a1, int a2) {
    MemSet(&g_readBuf, 0, 0x250);
    g_readBuf.length = 44;
    g_readBuf.pathId = 0;            // byte_145B683
    g_readBuf.targetId = 0;          // byte_145B684
    g_readBuf.lun = 0;               // byte_145B685
    g_readBuf.cdbLength = 6;         // byte_145B686
    g_readBuf.senseInfoLength = 24;  // byte_145B687
    g_readBuf.dataIn = 0;            // byte_145B688
    g_readBuf.dataTransferLength = 0;   // dword_145B68C
    g_readBuf.timeOutValue = 200;       // dword_145B690
    g_readBuf.dataBuffer = 80;          // dword_145B694
    g_readBuf.senseInfoOffset = 48;     // dword_145B698
    g_readBuf.cdb[0] = 27;              // byte_145B69C  opcode 0x1B
    if (a2)
        g_readBuf.cdb[4] |= 1u;         // byte_145B6A0 |= 1
    if (a1)
        g_readBuf.cdb[4] |= 2u;         // byte_145B6A0 |= 2
    return SptiSubmitRead();
}

// -----------------------------------------------------------------------------
// gilde.exe 0x14127a0 — VIBE_Disc_SptiSetReadSpeed  (__stdcall(a1))
//   SET CD SPEED, opcode 0xBB (cdb[0] = -69 = 0xBB). 12-byte CDB, no data.
//   a1 == 256 -> cdb[2..3] = FF FF (max). else:
//     v = (unsigned __int16)((1764*a1 + 9) / 10);   [imul 6E4h, +9, cdq, idiv 0Ah]
//     cdb[2] = v / 256;   cdb[3] = (u8)v;            (v zero-extended -> >>8 == /256)
//   cdb[4..5] = FF FF (write speed = max).
// -----------------------------------------------------------------------------
int SptiSetReadSpeed(int a1) {
    MemSet(&g_readBuf, 0, 0x250);
    g_readBuf.length = 44;
    g_readBuf.pathId = 0;
    g_readBuf.targetId = 0;
    g_readBuf.lun = 0;
    g_readBuf.cdbLength = 12;        // byte_145B686 = 0x0C
    g_readBuf.senseInfoLength = 24;
    g_readBuf.dataIn = 0;
    g_readBuf.dataTransferLength = 0;
    g_readBuf.timeOutValue = 200;
    g_readBuf.dataBuffer = 80;
    g_readBuf.senseInfoOffset = 48;
    g_readBuf.cdb[0] = 0xBB;         // byte_145B69C  opcode 0xBB SET CD SPEED
    if (a1 == 256) {
        g_readBuf.cdb[2] = 0xFF;     // byte_145B69E
        g_readBuf.cdb[3] = 0xFF;     // byte_145B69F
    } else {
        // v is the 16-bit truncation of (1764*a1 + 9)/10 (idiv, then mov ax).
        u16 v = (u16)((1764 * a1 + 9) / 10);
        // byte_145B69E = (u16)v / 256 : eax = v zero-extended, sar 8 == >>8.
        g_readBuf.cdb[2] = (u8)((u32)v / 256u);
        // byte_145B69F = (u8)v : low byte of the 16-bit value.
        g_readBuf.cdb[3] = (u8)v;
    }
    g_readBuf.cdb[4] = 0xFF;         // byte_145B6A0
    g_readBuf.cdb[5] = 0xFF;         // byte_145B6A1
    return SptiSubmitRead();
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1412ca0 — VIBE_Disc_SptiReadRawSector  (__stdcall(a1, a2, a3, a4))
//   READ CD, opcode 0xD8 (cdb[0] = -40 = 0xD8). LBA a3 big-endian into cdb[2..5].
//   a4 selects data length: 0->2352, 1->2368, 2->2448. DataBuffer = a2.
//   cdb[8]=0, cdb[9]=1 (transfer 1 block), cdb[10] = subch sel (0/1/2).
//     subch byte sits at byte_145EBE6 == cdb[10].
// -----------------------------------------------------------------------------
int SptiReadRawSector(int /*a1*/, int a2, int a3, int a4) {
    MemSet(&g_subchBuf, 0, 0x50);
    g_subchBuf.length = 44;
    g_subchBuf.pathId = 0;
    g_subchBuf.targetId = 0;
    g_subchBuf.lun = 0;
    g_subchBuf.cdbLength = 12;       // byte_145EBC6 = 0x0C
    g_subchBuf.senseInfoLength = 24;
    g_subchBuf.dataIn = 1;           // byte_145EBC8
    if (a4) {
        if (a4 == 1)
            g_subchBuf.dataTransferLength = 2368;
        else if (a4 == 2)
            g_subchBuf.dataTransferLength = 2448;
    } else {
        g_subchBuf.dataTransferLength = 2352;
    }
    g_subchBuf.timeOutValue = 2000;
    g_subchBuf.dataBuffer = (u32)a2;          // dword_145EBD4 = a2
    g_subchBuf.senseInfoOffset = 48;
    g_subchBuf.cdb[0] = 0xD8;                 // byte_145EBDC  opcode 0xD8
    g_subchBuf.cdb[2] = (u8)((u32)a3 >> 24);  // byte_145EBDE  HIBYTE(a3)
    g_subchBuf.cdb[3] = (u8)((u32)a3 >> 16);  // byte_145EBDF  BYTE2(a3)
    g_subchBuf.cdb[4] = (u8)((u32)a3 >> 8);   // byte_145EBE0  BYTE1(a3)
    g_subchBuf.cdb[5] = (u8)((u32)a3);        // byte_145EBE1  a3
    g_subchBuf.cdb[6] = 0;   // byte_145EBE2
    g_subchBuf.cdb[7] = 0;   // byte_145EBE3
    g_subchBuf.cdb[8] = 0;   // byte_145EBE4
    g_subchBuf.cdb[9] = 1;   // byte_145EBE5  transfer length lo = 1 block
    if (a4) {
        if (a4 == 1)
            g_subchBuf.cdb[10] = 1;  // byte_145EBE6
        else if (a4 == 2)
            g_subchBuf.cdb[10] = 2;
    } else {
        g_subchBuf.cdb[10] = 0;
    }
    return SptiSubmitSubchannel();
}

// -----------------------------------------------------------------------------
// gilde.exe 0x14131b0 — VIBE_Disc_SptiReadSectorEcc  (__stdcall(a1, a2, a3, a4))
//   READ CD, opcode 0xBE (cdb[0] = -66 = 0xBE). LBA a3 BE into cdb[2..5].
//   data length by a4 (0->2352, 1->2368, 2->2448). cdb[8]=1 (1 block),
//   cdb[9]=0xF8 (-8: sync + all headers + user + EDC/ECC).
//   cdb[10] subch sel: a4==1 -> 2, a4==2 -> 1 (note the 1/2 swap vs RawSector).
// -----------------------------------------------------------------------------
int SptiReadSectorEcc(int /*a1*/, int a2, int a3, int a4) {
    MemSet(&g_subchBuf, 0, 0x50);
    g_subchBuf.length = 44;
    g_subchBuf.pathId = 0;
    g_subchBuf.targetId = 0;
    g_subchBuf.lun = 0;
    g_subchBuf.cdbLength = 12;
    g_subchBuf.senseInfoLength = 24;
    g_subchBuf.dataIn = 1;
    if (a4) {
        if (a4 == 1)
            g_subchBuf.dataTransferLength = 2368;
        else if (a4 == 2)
            g_subchBuf.dataTransferLength = 2448;
    } else {
        g_subchBuf.dataTransferLength = 2352;
    }
    g_subchBuf.timeOutValue = 2000;
    g_subchBuf.dataBuffer = (u32)a2;
    g_subchBuf.senseInfoOffset = 48;
    g_subchBuf.cdb[0] = 0xBE;                 // byte_145EBDC  opcode 0xBE READ CD
    g_subchBuf.cdb[2] = (u8)((u32)a3 >> 24);
    g_subchBuf.cdb[3] = (u8)((u32)a3 >> 16);
    g_subchBuf.cdb[4] = (u8)((u32)a3 >> 8);
    g_subchBuf.cdb[5] = (u8)((u32)a3);
    g_subchBuf.cdb[6] = 0;   // byte_145EBE2
    g_subchBuf.cdb[7] = 0;   // byte_145EBE3
    g_subchBuf.cdb[8] = 1;   // byte_145EBE4  transfer length lo = 1 block
    g_subchBuf.cdb[9] = 0xF8;// byte_145EBE5  = -8 (sync/headers/user/EDC/ECC)
    if (a4) {
        if (a4 == 1)
            g_subchBuf.cdb[10] = 2;  // byte_145EBE6  (1 -> 2)
        else if (a4 == 2)
            g_subchBuf.cdb[10] = 1;  //               (2 -> 1)
    } else {
        g_subchBuf.cdb[10] = 0;
    }
    return SptiSubmitSubchannel();
}

// -----------------------------------------------------------------------------
// gilde.exe 0x14135b0 — VIBE_Disc_SptiReadSector  (__stdcall(a1, a2, a3, a4))
//   READ(10), opcode 0x28 (cdb[0]=40=0x28). DataBuffer = a2.
//   a4 != 0 -> DataTransferLength = 2048, cdb[8] = 1.
//   a4 == 0 -> DataTransferLength = 0, and a3 -= (u8)kLbaBiasTable[
//              (dword_145A014 - 1) % 9] BEFORE the BE split, cdb[8] = 0.
//   LBA a3 BE into cdb[2..5] (byte_145EBDE..E1).
// -----------------------------------------------------------------------------
int SptiReadSector(int /*a1*/, int a2, int a3, int a4) {
    MemSet(&g_subchBuf, 0, 0x50);
    g_subchBuf.length = 44;
    g_subchBuf.pathId = 0;
    g_subchBuf.targetId = 0;
    g_subchBuf.lun = 0;
    g_subchBuf.cdbLength = 10;       // byte_145EBC6 = 0x0A
    g_subchBuf.senseInfoLength = 24;
    g_subchBuf.dataIn = 1;
    if (a4)
        g_subchBuf.dataTransferLength = 2048;  // 0x800
    else
        g_subchBuf.dataTransferLength = 0;
    g_subchBuf.timeOutValue = 2000;
    g_subchBuf.dataBuffer = (u32)a2;
    g_subchBuf.senseInfoOffset = 48;
    g_subchBuf.cdb[0] = 0x28;            // byte_145EBDC  opcode 0x28 READ(10)
    if (!a4) {
        // a3 -= (u8)byte_145A790[(dword_145A014 - 1) % 9]  (signed % matches idiv rem)
        int idx = ((int)g_biasIndex - 1) % 9;
        a3 -= (u8)kLbaBiasTable[idx];
    }
    g_subchBuf.cdb[2] = (u8)((u32)a3 >> 24);  // byte_145EBDE  HIBYTE(a3)
    g_subchBuf.cdb[3] = (u8)((u32)a3 >> 16);  // byte_145EBDF  BYTE2(a3)
    g_subchBuf.cdb[4] = (u8)((u32)a3 >> 8);   // byte_145EBE0  BYTE1(a3)
    g_subchBuf.cdb[5] = (u8)((u32)a3);        // byte_145EBE1  a3
    g_subchBuf.cdb[8] = (u8)(a4 != 0);        // byte_145EBE4  = (a4 != 0)
    return SptiSubmitSubchannel();
}

// -----------------------------------------------------------------------------
// gilde.exe 0x1413860 — VIBE_Disc_SptiTestUnitReady  (__stdcall(a1, a2))
//   TEST UNIT READY, opcode 0x00 (cdb all zero). READ buffer, 6-byte CDB,
//   no data, TimeOut=200.
// -----------------------------------------------------------------------------
int SptiTestUnitReady(int /*a1*/, int /*a2*/) {
    MemSet(&g_readBuf, 0, 0x250);
    g_readBuf.length = 44;
    g_readBuf.pathId = 0;
    g_readBuf.targetId = 0;
    g_readBuf.lun = 0;
    g_readBuf.cdbLength = 6;
    g_readBuf.senseInfoLength = 24;
    g_readBuf.dataIn = 0;
    g_readBuf.dataTransferLength = 0;
    g_readBuf.timeOutValue = 200;
    g_readBuf.dataBuffer = 80;
    g_readBuf.senseInfoOffset = 48;
    g_readBuf.cdb[0] = 0;        // byte_145B69C  opcode 0x00 TEST UNIT READY
    return SptiSubmitRead();
}

} // namespace guild::drm
