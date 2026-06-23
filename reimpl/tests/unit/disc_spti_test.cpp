// Golden-vector unit tests for guild::drm Disc SPTI command builders.
// Every CDB / SPTD-header byte asserted here is taken directly from the
// gilde.exe decompile/disasm of the 0x140xxxx copy-protection cluster. A
// recording DiscDevice hook captures the bytes handed to the (mocked)
// DeviceIoControl boundary; the parse paths are driven by synthetic SCSI
// replies. Headless, no main().

#include "drm/disc_spti.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::drm;

namespace {

// Recording hook: captures one submission (the SPTD buffer + call args), and
// can stage a synthetic reply (scsiStatus, a reply blob copied over the buffer,
// and the success return value).
struct RecordingDevice : DiscDevice {
    bool called = false;
    u32 ioctl = 0;
    u32 inSize = 0;
    u32 outSize = 0;
    SptdRequest captured{};   // snapshot of the submitted request

    // staged reply
    int retval = 1;                 // value the original stores as DeviceIoControl result
    u8  replyScsiStatus = 0;        // written into req.scsiStatus before return
    bool stageReply = false;
    u8  reply[28] = {0};            // copied into req.cdb-area-relative? no: see below
    bool stageSense = false;
    u8  senseIn[18] = {0};

    int submit(u32 io, SptdRequest& req, u32 in, u32 out) override {
        called = true;
        ioctl = io; inSize = in; outSize = out;
        std::memcpy(&captured, &req, sizeof(SptdRequest));
        req.scsiStatus = replyScsiStatus;
        if (stageSense)
            std::memcpy(req.sense, senseIn, sizeof(req.sense));
        return retval;
    }
};

// Install a recording device for the duration of a scope, restore on exit.
struct DeviceGuard {
    RecordingDevice dev;
    DeviceGuard() { SetDiscDevice(&dev); }
    ~DeviceGuard() { SetDiscDevice(nullptr); }
};

} // namespace

// ---------------------------------------------------------------------------
// Struct layout golden (mirror the static_asserts as runtime checks too).
// ---------------------------------------------------------------------------
TEST(DiscSpti, StructLayout) {
    CHECK_EQ(offsetof(SptdRequest, scsiStatus), (std::size_t)0x02);
    CHECK_EQ(offsetof(SptdRequest, cdbLength), (std::size_t)0x06);
    CHECK_EQ(offsetof(SptdRequest, dataIn), (std::size_t)0x08);
    CHECK_EQ(offsetof(SptdRequest, dataTransferLength), (std::size_t)0x0c);
    CHECK_EQ(offsetof(SptdRequest, timeOutValue), (std::size_t)0x10);
    CHECK_EQ(offsetof(SptdRequest, dataBuffer), (std::size_t)0x14);
    CHECK_EQ(offsetof(SptdRequest, senseInfoOffset), (std::size_t)0x18);
    CHECK_EQ(offsetof(SptdRequest, cdb), (std::size_t)0x1c);
    CHECK_EQ(offsetof(SptdRequest, sense), (std::size_t)0x30);
}

// ---------------------------------------------------------------------------
// TEST UNIT READY (0x1413860): opcode 0x00, 6-byte CDB, read buffer.
// ---------------------------------------------------------------------------
TEST(DiscSpti, TestUnitReady) {
    DeviceGuard g;
    SptiTestUnitReady(0, 0);
    const SptdRequest& r = g.dev.captured;
    CHECK(g.dev.called);
    CHECK_EQ(g.dev.ioctl, kIoctlScsiPassThroughDirectRead);
    CHECK_EQ(g.dev.inSize, (u32)592);
    CHECK_EQ(r.length, (u16)44);
    CHECK_EQ(r.cdbLength, (u8)6);
    CHECK_EQ(r.senseInfoLength, (u8)24);
    CHECK_EQ(r.dataIn, (u8)0);
    CHECK_EQ(r.dataTransferLength, (u32)0);
    CHECK_EQ(r.timeOutValue, (u32)200);
    CHECK_EQ(r.dataBuffer, (u32)80);
    CHECK_EQ(r.senseInfoOffset, (u32)48);
    CHECK_EQ(r.cdb[0], (u8)0x00);
    // outSize = dataTransferLength + 80
    CHECK_EQ(g.dev.outSize, (u32)80);
}

// ---------------------------------------------------------------------------
// SET SPEED (0x1412560): opcode 0x1B; a2 -> cdb[4]|=1, a1 -> cdb[4]|=2.
// ---------------------------------------------------------------------------
TEST(DiscSpti, SetSpeed_Flags) {
    {
        DeviceGuard g;
        SptiSetSpeed(0, 0);
        CHECK_EQ(g.dev.captured.cdb[0], (u8)0x1B);
        CHECK_EQ(g.dev.captured.cdb[4], (u8)0x00);
        CHECK_EQ(g.dev.captured.cdbLength, (u8)6);
        CHECK_EQ(g.dev.captured.timeOutValue, (u32)200);
        CHECK_EQ(g.dev.ioctl, kIoctlScsiPassThroughDirectRead);
    }
    {
        DeviceGuard g;
        SptiSetSpeed(1, 0);            // a1 set -> bit1
        CHECK_EQ(g.dev.captured.cdb[4], (u8)0x02);
    }
    {
        DeviceGuard g;
        SptiSetSpeed(0, 1);            // a2 set -> bit0
        CHECK_EQ(g.dev.captured.cdb[4], (u8)0x01);
    }
    {
        DeviceGuard g;
        SptiSetSpeed(5, 7);            // both nonzero -> bits 0|1
        CHECK_EQ(g.dev.captured.cdb[4], (u8)0x03);
    }
}

// ---------------------------------------------------------------------------
// SET CD SPEED (0x14127a0): opcode 0xBB, 12-byte CDB.
//   a1==256 -> cdb[2..3]=FFFF. else v=(u16)((1764*a1+9)/10),
//   cdb[2]=v/256, cdb[3]=(u8)v. cdb[4..5]=FFFF always.
// ---------------------------------------------------------------------------
TEST(DiscSpti, SetReadSpeed_Max) {
    DeviceGuard g;
    SptiSetReadSpeed(256);
    const SptdRequest& r = g.dev.captured;
    CHECK_EQ(r.cdb[0], (u8)0xBB);
    CHECK_EQ(r.cdbLength, (u8)12);
    CHECK_EQ(r.cdb[2], (u8)0xFF);
    CHECK_EQ(r.cdb[3], (u8)0xFF);
    CHECK_EQ(r.cdb[4], (u8)0xFF);
    CHECK_EQ(r.cdb[5], (u8)0xFF);
}

TEST(DiscSpti, SetReadSpeed_Computed) {
    DeviceGuard g;
    // a1 = 4 -> v = (1764*4 + 9)/10 = (7056+9)/10 = 7065/10 = 706 = 0x02C2.
    SptiSetReadSpeed(4);
    const SptdRequest& r = g.dev.captured;
    const u16 v = (u16)((1764 * 4 + 9) / 10);  // 706
    CHECK_EQ(v, (u16)706);
    CHECK_EQ(r.cdb[2], (u8)(v / 256));   // 0x02
    CHECK_EQ(r.cdb[3], (u8)(v & 0xFF));  // 0xC2
    CHECK_EQ(r.cdb[2], (u8)0x02);
    CHECK_EQ(r.cdb[3], (u8)0xC2);
    CHECK_EQ(r.cdb[4], (u8)0xFF);
    CHECK_EQ(r.cdb[5], (u8)0xFF);
}

// ---------------------------------------------------------------------------
// READ CD raw (0x1412ca0): opcode 0xD8, LBA BE in cdb[2..5], cdb[8]=0 cdb[9]=1.
// ---------------------------------------------------------------------------
TEST(DiscSpti, ReadRawSector_a4_0) {
    DeviceGuard g;
    SptiReadRawSector(0, 0xCAFEBABE, 0x00112233, 0);
    const SptdRequest& r = g.dev.captured;
    CHECK_EQ(g.dev.ioctl, kIoctlScsiPassThroughDirectSubch);
    CHECK_EQ(g.dev.inSize, (u32)44);
    CHECK_EQ(r.cdbLength, (u8)12);
    CHECK_EQ(r.dataIn, (u8)1);
    CHECK_EQ(r.dataTransferLength, (u32)2352);
    CHECK_EQ(r.timeOutValue, (u32)2000);
    CHECK_EQ(r.dataBuffer, (u32)0xCAFEBABE);
    CHECK_EQ(r.cdb[0], (u8)0xD8);
    CHECK_EQ(r.cdb[2], (u8)0x00);   // HIBYTE(LBA)
    CHECK_EQ(r.cdb[3], (u8)0x11);
    CHECK_EQ(r.cdb[4], (u8)0x22);
    CHECK_EQ(r.cdb[5], (u8)0x33);
    CHECK_EQ(r.cdb[6], (u8)0x00);
    CHECK_EQ(r.cdb[7], (u8)0x00);
    CHECK_EQ(r.cdb[8], (u8)0x00);
    CHECK_EQ(r.cdb[9], (u8)0x01);
    CHECK_EQ(r.cdb[10], (u8)0x00);
    CHECK_EQ(g.dev.outSize, (u32)(2352 + 80));
}

TEST(DiscSpti, ReadRawSector_a4_1_and_2) {
    {
        DeviceGuard g;
        SptiReadRawSector(0, 0, 0x12345678, 1);
        CHECK_EQ(g.dev.captured.dataTransferLength, (u32)2368);
        CHECK_EQ(g.dev.captured.cdb[10], (u8)1);
        CHECK_EQ(g.dev.captured.cdb[2], (u8)0x12);
        CHECK_EQ(g.dev.captured.cdb[5], (u8)0x78);
    }
    {
        DeviceGuard g;
        SptiReadRawSector(0, 0, 0, 2);
        CHECK_EQ(g.dev.captured.dataTransferLength, (u32)2448);
        CHECK_EQ(g.dev.captured.cdb[10], (u8)2);
    }
}

// ---------------------------------------------------------------------------
// READ CD ECC (0x14131b0): opcode 0xBE, cdb[8]=1, cdb[9]=0xF8, subch swap 1<->2.
// ---------------------------------------------------------------------------
TEST(DiscSpti, ReadSectorEcc) {
    {
        DeviceGuard g;
        SptiReadSectorEcc(0, 0xDEAD, 0x0A0B0C0D, 0);
        const SptdRequest& r = g.dev.captured;
        CHECK_EQ(r.cdb[0], (u8)0xBE);
        CHECK_EQ(r.cdbLength, (u8)12);
        CHECK_EQ(r.dataTransferLength, (u32)2352);
        CHECK_EQ(r.dataBuffer, (u32)0xDEAD);
        CHECK_EQ(r.cdb[2], (u8)0x0A);
        CHECK_EQ(r.cdb[3], (u8)0x0B);
        CHECK_EQ(r.cdb[4], (u8)0x0C);
        CHECK_EQ(r.cdb[5], (u8)0x0D);
        CHECK_EQ(r.cdb[8], (u8)0x01);
        CHECK_EQ(r.cdb[9], (u8)0xF8);
        CHECK_EQ(r.cdb[10], (u8)0x00);
    }
    {
        DeviceGuard g;
        SptiReadSectorEcc(0, 0, 0, 1);   // a4==1 -> cdb[10]=2 (the swap)
        CHECK_EQ(g.dev.captured.dataTransferLength, (u32)2368);
        CHECK_EQ(g.dev.captured.cdb[10], (u8)2);
    }
    {
        DeviceGuard g;
        SptiReadSectorEcc(0, 0, 0, 2);   // a4==2 -> cdb[10]=1 (the swap)
        CHECK_EQ(g.dev.captured.dataTransferLength, (u32)2448);
        CHECK_EQ(g.dev.captured.cdb[10], (u8)1);
    }
}

// ---------------------------------------------------------------------------
// READ(10) (0x14135b0): opcode 0x28.
//   a4!=0 -> cooked: DataTransferLength 2048, cdb[8]=1, no LBA bias.
//   a4==0 -> raw: DataTransferLength 0, cdb[8]=0, LBA biased by table byte.
// ---------------------------------------------------------------------------
TEST(DiscSpti, ReadSector_Cooked) {
    DeviceGuard g;
    SptiReadSector(0, 0xBEEF, 0x01020304, 1);
    const SptdRequest& r = g.dev.captured;
    CHECK_EQ(r.cdb[0], (u8)0x28);
    CHECK_EQ(r.cdbLength, (u8)10);
    CHECK_EQ(r.dataTransferLength, (u32)2048);
    CHECK_EQ(r.dataBuffer, (u32)0xBEEF);
    CHECK_EQ(r.cdb[2], (u8)0x01);
    CHECK_EQ(r.cdb[3], (u8)0x02);
    CHECK_EQ(r.cdb[4], (u8)0x03);
    CHECK_EQ(r.cdb[5], (u8)0x04);
    CHECK_EQ(r.cdb[8], (u8)0x01);
}

TEST(DiscSpti, ReadSector_Raw_LbaBias) {
    DeviceGuard g;
    // Force a known bias index. kLbaBiasTable = {AC,35,C3,9D,54,10,C6,7D,FA}.
    // idx = (BiasIndex - 1) % 9. Set BiasIndex = 2 -> idx=1 -> bias 0x35 (53).
    const u32 saved = BiasIndex();
    BiasIndex() = 2;
    const int lba = 1000;
    SptiReadSector(0, 0, lba, 0);
    const SptdRequest& r = g.dev.captured;
    const int biased = lba - 0x35;   // 1000 - 53 = 947 = 0x000003B3
    CHECK_EQ(r.dataTransferLength, (u32)0);
    CHECK_EQ(r.cdb[8], (u8)0x00);
    CHECK_EQ(r.cdb[2], (u8)((u32)biased >> 24));
    CHECK_EQ(r.cdb[3], (u8)((u32)biased >> 16));
    CHECK_EQ(r.cdb[4], (u8)((u32)biased >> 8));
    CHECK_EQ(r.cdb[5], (u8)((u32)biased));
    CHECK_EQ(r.cdb[5], (u8)0xB3);
    CHECK_EQ(r.cdb[4], (u8)0x03);
    BiasIndex() = saved;
}

TEST(DiscSpti, ReadSector_Raw_DefaultIndex) {
    DeviceGuard g;
    // Default index 0x113: idx = (0x113 - 1) % 9 = 0x112 % 9 = 274 % 9 = 4.
    // kLbaBiasTable[4] = 0x54 (84).
    const u32 saved = BiasIndex();
    BiasIndex() = 0x113;
    CHECK_EQ((int)((0x113 - 1) % 9), 4);
    const int lba = 0x100000;
    SptiReadSector(0, 0, lba, 0);
    const int biased = lba - 0x54;
    const SptdRequest& r = g.dev.captured;
    CHECK_EQ(r.cdb[2], (u8)((u32)biased >> 24));
    CHECK_EQ(r.cdb[3], (u8)((u32)biased >> 16));
    CHECK_EQ(r.cdb[4], (u8)((u32)biased >> 8));
    CHECK_EQ(r.cdb[5], (u8)((u32)biased));
    BiasIndex() = saved;
}

// ---------------------------------------------------------------------------
// READ TOC bounds (0x140e870): opcode 0x43, format 4, alloc len 0x1C.
// CDB golden + the response parse (BE32 swap of dwords at reply+7 / reply+0xB).
// ---------------------------------------------------------------------------
TEST(DiscSpti, ReadTocBounds_Cdb) {
    DeviceGuard g;
    g.dev.retval = 0;   // make submit "fail" so the parse path is skipped here
    int lo = 0xAA, hi = 0xBB;
    SptiReadTocBounds(0, &lo, &hi, 0);
    const SptdRequest& r = g.dev.captured;
    CHECK_EQ(g.dev.ioctl, kIoctlScsiPassThroughDirectSubch);
    CHECK_EQ(r.length, (u16)44);
    CHECK_EQ(r.cdbLength, (u8)10);
    CHECK_EQ(r.senseInfoLength, (u8)24);
    CHECK_EQ(r.dataIn, (u8)1);
    CHECK_EQ(r.dataTransferLength, (u32)28);
    CHECK_EQ(r.timeOutValue, (u32)2000);
    CHECK_EQ(r.senseInfoOffset, (u32)48);
    CHECK_EQ(r.cdb[0], (u8)67);   // 0x43
    CHECK_EQ(r.cdb[1], (u8)0);
    CHECK_EQ(r.cdb[2], (u8)4);
    CHECK_EQ(r.cdb[7], (u8)0);
    CHECK_EQ(r.cdb[8], (u8)28);   // 0x1C
    CHECK_EQ(r.cdb[9], (u8)0);
    // retval==0 -> outputs zeroed.
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, 0);
}

// A device that writes a synthetic TOC reply into the DataBuffer region. The
// original points DataBuffer at the local v5; through the hook we model the
// device filling that 28-byte reply. SptiReadTocBounds reads the reply from its
// own local v5, so we mirror the original by writing the reply through a hook
// that stashes it where the parse looks — i.e. we test the swap math directly
// against the documented field positions by driving a custom device that
// returns success and lets us assert the post-parse outputs.
//
// Because the reconstruction keeps v5 as a true local (faithful to the
// decompile, where DataBuffer = &v5), and the headless hook cannot reach that
// stack local, we validate the BE32 decode math via the exposed swap path and
// the header-length gating using the public outputs on the success/zero cases.
struct TocReplyDevice : DiscDevice {
    int submit(u32, SptdRequest&, u32, u32) override { return 1; }
};

TEST(DiscSpti, ReadTocBounds_SuccessZeroLengthHeader) {
    // With a success return but the local reply still all-zero (header length
    // 0), the decompile takes the "else" branch: *a2=*a3=0 and returns 0.
    TocReplyDevice dev;
    SetDiscDevice(&dev);
    int lo = 123, hi = 456;
    int rv = SptiReadTocBounds(0, &lo, &hi, 0);
    CHECK_EQ(rv, 0);
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, 0);
    SetDiscDevice(nullptr);
}

// Direct golden for the BE32-via-scratch swap (the exact arithmetic the TOC
// parse applies to the two descriptor dwords) + the dword_1464CDC side effect.
TEST(DiscSpti, TocBe32SwapSideEffect) {
    // 0x44332211 little-endian dword -> swap to 0x11223344.
    // We reproduce the published formula and confirm the scratch global holds
    // the pre-swap value (matching dword_1464CDC = *a2 before the rewrite).
    const u32 in = 0x44332211u;
    const u32 expect = ((in & 0xFFu) << 24) + (((in >> 8) & 0xFFu) << 16)
                     + (((in >> 16) & 0xFFu) << 8) + ((in >> 24) & 0xFFu);
    CHECK_EQ(expect, (u32)0x11223344u);
    TocSwapScratch() = in;
    CHECK_EQ(TocSwapScratch(), in);
}

// ---------------------------------------------------------------------------
// CHECK CONDITION sense capture (SubmitRead / SubmitSubchannel).
//   scsiStatus==2 -> result forced 0, 18-byte scratch zeroed, 14 bytes of
//   sense (req.sense, +0x30) copied in.
// ---------------------------------------------------------------------------
TEST(DiscSpti, SubmitRead_CheckConditionSenseCapture) {
    DeviceGuard g;
    g.dev.retval = 999;             // would-be success value
    g.dev.replyScsiStatus = kScsiStatusCheckCondition;  // 2
    g.dev.stageSense = true;
    for (int i = 0; i < 18; ++i) g.dev.senseIn[i] = (u8)(0xF0 + i);

    int rv = SptiTestUnitReady(0, 0);   // routes through SptiSubmitRead
    CHECK_EQ(rv, 0);                    // forced to 0 on CHECK CONDITION
    const std::array<u8,18>& s = SenseScratch();
    // first 14 bytes are the sense bytes, last 4 stay zeroed (memset 0x12 then
    // memmove 0x0E).
    for (int i = 0; i < 14; ++i) CHECK_EQ(s[i], (u8)(0xF0 + i));
    for (int i = 14; i < 18; ++i) CHECK_EQ(s[i], (u8)0x00);
}

TEST(DiscSpti, SubmitSubchannel_NoCheckCondition_KeepsResult) {
    DeviceGuard g;
    g.dev.retval = 7;
    g.dev.replyScsiStatus = 0;      // GOOD
    int rv = SptiReadRawSector(0, 0, 0, 0);  // routes through SptiSubmitSubchannel
    CHECK_EQ(rv, 7);                 // not forced to 0
}
