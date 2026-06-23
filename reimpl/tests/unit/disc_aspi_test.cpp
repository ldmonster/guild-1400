// Golden tests for guild::drm Disc ASPI SCSI builders / parsers (wave9-drm).
//
// We never touch hardware: a recording DiscAspi backend captures the exact 0x50
// SRB image the builder produced (so we can assert byte-for-byte CDB/field bytes)
// and can write a synthetic reply into the data buffer + set the completion
// status, which drives the response-parse paths (TOC bounds, MSF->LBA, the
// nibble-shuffle key transform). Suite prefix: DiscAspi. No main().
#include "drm/disc_aspi.h"

#include <cstring>

#include "test.h"

using namespace guild;
using namespace guild::drm;

namespace {

// Recording backend: snapshots the SRB image on submit, optionally injects a
// reply (status + buffer bytes) so the parse half of each builder runs.
struct RecordingAspi : DiscAspi {
    u8   img[0x50] = {};      // last submitted 0x50 SRB image
    void* lastBuf = nullptr;  // native data-buffer pointer the builder set
    u32  lastBufLen = 0;
    int  calls = 0;

    // Reply injection:
    u8   replyStatus = kSS_PENDING;       // what to write back to img[1] (status)
    const u8* replyData = nullptr;        // bytes copied into the data buffer
    u32  replyLen = 0;
    const u8* senseData = nullptr;        // bytes copied into srb sense (+0x40)
    u32  senseLen = 0;

    u32 sendAspiCommand(SrbExecScsiCmd& srb) override {
        ++calls;
        std::memcpy(img, &srb.img, 0x50);
        lastBuf = srb.bufNative;
        lastBufLen = srb.img.bufLen;
        if (replyData && srb.bufNative)
            std::memcpy(srb.bufNative, replyData, replyLen);
        if (senseData)
            std::memcpy(srb.img.sense, senseData, senseLen);
        srb.img.status = replyStatus;     // emulate completion handshake result
        return 0;
    }
    u32 supportInfo() override { return 0; }

    u8 cmd() const     { return img[0x00]; }
    u8 status() const  { return img[0x01]; }
    u8 haId() const    { return img[0x02]; }
    u8 flags() const   { return img[0x03]; }
    u8 target() const  { return img[0x08]; }
    u8 lun() const     { return img[0x09]; }
    u32 bufLen() const { u32 v; std::memcpy(&v, img + 0x0C, 4); return v; }
    u8 senseLenF() const { return img[0x14]; }
    u8 cdbLen() const  { return img[0x15]; }
    const u8* cdb() const { return img + 0x30; }
};

struct ScopedAspi {
    RecordingAspi rec;
    ScopedAspi() { SetDiscAspi(&rec); }
    ~ScopedAspi() { SetDiscAspi(nullptr); }
};

} // namespace

// ----------------------------------------------------------------------------
// Struct / layout invariants.
// ----------------------------------------------------------------------------
TEST(DiscAspi, ImageLayout0x50) {
    CHECK_EQ(sizeof(SrbImage), (size_t)0x50);
    SrbImage s{};
    auto base = reinterpret_cast<u8*>(&s);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.cmd) - base),      (size_t)0x00);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.status) - base),   (size_t)0x01);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.haId) - base),     (size_t)0x02);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.flags) - base),    (size_t)0x03);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.target) - base),   (size_t)0x08);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.lun) - base),      (size_t)0x09);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.bufLen) - base),   (size_t)0x0C);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.bufPointer) - base),(size_t)0x10);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.senseLen) - base), (size_t)0x14);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.cdbLen) - base),   (size_t)0x15);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(&s.postProc) - base), (size_t)0x18);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(s.cdb) - base),       (size_t)0x30);
    CHECK_EQ((size_t)(reinterpret_cast<u8*>(s.sense) - base),     (size_t)0x40);
}

TEST(DiscAspi, ObfTableBytes) {
    static const u8 expect[9] = {0xac,0x35,0xc3,0x9d,0x54,0x10,0xc6,0x7d,0xfa};
    for (int i = 0; i < 9; ++i)
        CHECK_EQ(kObfTable[i], expect[i]);
}

TEST(DiscAspi, DefaultsAndCounter) {
    CHECK_EQ(DefaultScsiAddr().haId, (u8)0);
    CHECK_EQ(DefaultScsiAddr().target, (u8)0);
    CHECK_EQ(DefaultScsiAddr().lun, (u8)0);
    CHECK_EQ(ProtectionCounter(), (u32)0x113);
}

TEST(DiscAspi, InertNeverCompletes) {
    SetDiscAspi(nullptr);
    u8 buf[64] = {};
    // Inert backend leaves status pending -> every builder returns 0.
    CHECK_EQ(AspiTestUnitReady(0, 0, 0, 0), 0);
    CHECK_EQ(AspiInquiry(0, 0, 0, buf), 0);
    CHECK_EQ(AspiReadSector(0, 0, 0, buf, 100, 1), 0);
}

// ----------------------------------------------------------------------------
// READ TOC bounds (0x140e610): CDB bytes + big-endian LBA byteswap parse.
// ----------------------------------------------------------------------------
TEST(DiscAspi, ReadTocBounds_Cdb) {
    ScopedAspi s;
    u32 a, b;
    AspiReadTocBounds(0, &a, &b, 0);
    const u8* c = s.rec.cdb();
    CHECK_EQ(s.rec.cmd(), kSC_EXEC_SCSI_CMD);
    CHECK_EQ(s.rec.flags(), (u8)0x48);   // DIR_IN | EVENT
    CHECK_EQ(s.rec.senseLenF(), (u8)14);
    CHECK_EQ(s.rec.cdbLen(), (u8)10);
    CHECK_EQ(s.rec.bufLen(), (u32)28);
    CHECK_EQ(c[0], (u8)0x43);            // READ TOC
    CHECK_EQ(c[1], (u8)0x00);
    CHECK_EQ(c[2], (u8)0x04);            // format 4
    CHECK_EQ(c[7], (u8)0x00);
    CHECK_EQ(c[8], (u8)28);              // alloc len
    CHECK_EQ(c[9], (u8)0x00);
}

TEST(DiscAspi, ReadTocBounds_ParseByteswap) {
    ScopedAspi s;
    // 28-byte reply: [0..1] data length (>=0xD), [7..10] LBA-A, [11..14] LBA-B.
    u8 reply[28] = {};
    reply[0] = 0x00; reply[1] = 0x14;        // length = 20 (>= 0xD)
    // LBA-A little-endian dword at +7 = 0x11223344 -> byteswapped 0x44332211
    reply[7] = 0x44; reply[8] = 0x33; reply[9] = 0x22; reply[10] = 0x11;
    // LBA-B at +11 = 0xAABBCCDD -> byteswapped 0xDDCCBBAA
    reply[11] = 0xDD; reply[12] = 0xCC; reply[13] = 0xBB; reply[14] = 0xAA;
    s.rec.replyStatus = kSS_COMP;
    s.rec.replyData = reply; s.rec.replyLen = sizeof(reply);
    u32 a = 0, b = 0;
    int r = AspiReadTocBounds(0, &a, &b, 0);
    CHECK_EQ(r, 1);
    CHECK_EQ(a, (u32)0x44332211u);
    CHECK_EQ(b, (u32)0xDDCCBBAAu);
}

TEST(DiscAspi, ReadTocBounds_ShortReplyZeroes) {
    ScopedAspi s;
    u8 reply[28] = {};
    reply[0] = 0; reply[1] = 0x0C;           // length = 12 (< 0xD) -> zeroes
    s.rec.replyStatus = kSS_COMP;
    s.rec.replyData = reply; s.rec.replyLen = sizeof(reply);
    u32 a = 0xdead, b = 0xbeef;
    int r = AspiReadTocBounds(0, &a, &b, 0);
    CHECK_EQ(r, 1);                          // still returns 1 (status==1)
    CHECK_EQ(a, (u32)0);
    CHECK_EQ(b, (u32)0);
}

TEST(DiscAspi, ReadTocBounds_FailZeroes) {
    ScopedAspi s;
    s.rec.replyStatus = kSS_PENDING;         // not completed
    u32 a = 0xdead, b = 0xbeef;
    int r = AspiReadTocBounds(0, &a, &b, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ(a, (u32)0);
    CHECK_EQ(b, (u32)0);
}

// ----------------------------------------------------------------------------
// SET (start/stop) speed (0x1412460): bit assembly into CDB[4].
// ----------------------------------------------------------------------------
TEST(DiscAspi, SetSpeed_Cdb) {
    ScopedAspi s;
    AspiSetSpeed(/*a1=*/1, /*a2=*/1);   // a2->bit0, a1->bit1 -> 0x03
    const u8* c = s.rec.cdb();
    CHECK_EQ(s.rec.flags(), (u8)0x50);  // DIR_OUT | EVENT
    CHECK_EQ(s.rec.cdbLen(), (u8)6);
    CHECK_EQ(c[0], (u8)0x1B);
    CHECK_EQ(c[1], (u8)0x00);           // 32*lun, lun=0
    CHECK_EQ(c[4], (u8)0x03);
}

TEST(DiscAspi, SetSpeed_BitSelect) {
    {
        ScopedAspi s; AspiSetSpeed(0, 1);   // only a2 -> bit0
        CHECK_EQ(s.rec.cdb()[4], (u8)0x01);
    }
    {
        ScopedAspi s; AspiSetSpeed(1, 0);   // only a1 -> bit1
        CHECK_EQ(s.rec.cdb()[4], (u8)0x02);
    }
    {
        ScopedAspi s; AspiSetSpeed(0, 0);
        CHECK_EQ(s.rec.cdb()[4], (u8)0x00);
    }
}

// ----------------------------------------------------------------------------
// SET CD SPEED (0x1412660): KB/s big-endian + max sentinel.
// ----------------------------------------------------------------------------
TEST(DiscAspi, SetReadSpeed_Max) {
    ScopedAspi s;
    AspiSetReadSpeed(256);
    const u8* c = s.rec.cdb();
    CHECK_EQ(s.rec.cdbLen(), (u8)12);
    CHECK_EQ(c[0], (u8)0xBB);
    CHECK_EQ(c[2], (u8)0xFF);
    CHECK_EQ(c[3], (u8)0xFF);
    CHECK_EQ(c[4], (u8)0xFF);
    CHECK_EQ(c[5], (u8)0xFF);
}

TEST(DiscAspi, SetReadSpeed_Computed) {
    ScopedAspi s;
    // speed 4 -> (1764*4 + 9)/10 = 7065/10 = 706 = 0x02C2.
    AspiSetReadSpeed(4);
    const u8* c = s.rec.cdb();
    CHECK_EQ(c[0], (u8)0xBB);
    CHECK_EQ(c[2], (u8)0x02);   // 706 / 256
    CHECK_EQ(c[3], (u8)0xC2);   // 706 & 0xFF
    CHECK_EQ(c[4], (u8)0xFF);
    CHECK_EQ(c[5], (u8)0xFF);
}

// ----------------------------------------------------------------------------
// READ TOC MSF + decode (0x1412950): CDB, MSF->LBA, obf subtract, transform.
// ----------------------------------------------------------------------------
TEST(DiscAspi, ReadTocAndDecode_Cdb) {
    ScopedAspi s;
    u8 buf[16] = {};
    u32 lba, dw;
    AspiReadTocAndDecode(/*ha*/0x11, /*tgt*/0x22, /*lun*/0x01, buf, &lba, &dw, 0);
    const u8* c = s.rec.cdb();
    CHECK_EQ(s.rec.haId(), (u8)0x11);
    CHECK_EQ(s.rec.target(), (u8)0x22);
    CHECK_EQ(s.rec.lun(), (u8)0x01);
    CHECK_EQ(s.rec.flags(), (u8)0x48);
    CHECK_EQ(s.rec.cdbLen(), (u8)10);
    CHECK_EQ(s.rec.bufLen(), (u32)16);
    CHECK_EQ(c[0], (u8)0x42);
    CHECK_EQ(c[1], (u8)((32 * 1) | 2));  // 0x22
    CHECK_EQ(c[2], (u8)0x40);
    CHECK_EQ(c[3], (u8)0x01);
    CHECK_EQ(c[8], (u8)16);
}

TEST(DiscAspi, ReadTocAndDecode_MsfToLba) {
    ScopedAspi s;
    // *a5 = 75*buf[10] + 4500*buf[9] + buf[11] - 150, then -= obf[counter%9].
    u8 buf[16] = {};
    buf[9] = 2;    // minutes-ish
    buf[10] = 30;  // seconds-ish
    buf[11] = 10;  // frames-ish
    buf[12] = 0x44; buf[13] = 0x33; buf[14] = 0x22; buf[15] = 0x11; // *a6 dword LE
    s.rec.replyStatus = kSS_COMP;
    s.rec.replyData = buf; s.rec.replyLen = sizeof(buf);
    u32 lba = 0, dw = 0;
    int r = AspiReadTocAndDecode(0, 0, 0, buf, &lba, &dw, /*transform*/0);
    CHECK_EQ(r, 1);
    u32 raw = 75u * 30 + 4500u * 2 + 10 - 150;       // = 2250+9000+10-150 = 11110
    u32 expect = raw - kObfTable[ProtectionCounter() % 9];
    CHECK_EQ(lba, expect);
    CHECK_EQ(dw, (u32)0x11223344u);
}

TEST(DiscAspi, ReadTocAndDecode_FailPath) {
    ScopedAspi s;
    s.rec.replyStatus = kSS_PENDING;
    u8 buf[16] = {};
    u32 lba = 1, dw = 1;
    int r = AspiReadTocAndDecode(0, 0, 0, buf, &lba, &dw, 0);
    CHECK_EQ(r, 0);
    // fail path sets *a5=0,*a6=0 then *a5 -= obf[counter%9] (wraps).
    CHECK_EQ(lba, (u32)(0u - kObfTable[ProtectionCounter() % 9]));
    CHECK_EQ(dw, (u32)0);
}

// Reference reimplementation of the nibble-shuffle transform, to assert the
// builder's transform output matches exactly for a known input LBA.
static u32 RefTransform(u32 a5) {
    int v13 = 0, v12 = 0; unsigned char v8 = 0; char v15 = 0;
    for (int i = 0; i < 4; ++i) {
        int v17 = (int)(a5 >> (4 * i)) & 0xF;
        v13 += (int)(a5 >> (4 * i)) & 0xF;
        if (i < 3) v8 += (unsigned char)(a5 >> (4 * i));
        v12 |= v17 << (8 * i);
    }
    v12 |= ((unsigned char)v13 >> 4) << 28;
    v12 |= (v13 & 0xF) << 20;
    v12 |= (v8 >> 4) << 12;
    v12 |= 16 * (v8 & 0xF);
    unsigned char* v14 = reinterpret_cast<unsigned char*>(&v12);
    int v11 = (int)a5 & 0xF;
    int j;
    for (j = 3; j > 0; --j) {
        v14[j] ^= kObfTable[(j + v11) % 9];
        v15 += (char)v14[j];
    }
    v14[j] ^= (unsigned char)v15;
    return (u32)v12;
}

TEST(DiscAspi, ReadTocAndDecode_Transform) {
    ScopedAspi s;
    u8 buf[16] = {};
    buf[9] = 5; buf[10] = 7; buf[11] = 3;     // arbitrary MSF
    s.rec.replyStatus = kSS_COMP;
    s.rec.replyData = buf; s.rec.replyLen = sizeof(buf);
    u32 lba = 0, dw = 0;
    AspiReadTocAndDecode(0, 0, 0, buf, &lba, &dw, /*transform*/1);
    u32 raw = (75u * 7 + 4500u * 5 + 3 - 150) - kObfTable[ProtectionCounter() % 9];
    CHECK_EQ(lba, RefTransform(raw));
}

// ----------------------------------------------------------------------------
// READ CD-DA raw (0x1412e10): mode-dependent buffer/CDB[10], big-endian LBA.
// ----------------------------------------------------------------------------
TEST(DiscAspi, ReadRawSector_Cdb) {
    ScopedAspi s;
    u8 buf[2448] = {};
    AspiReadRawSector(0, 0, 1, buf, 0x010203, /*mode*/2);
    const u8* c = s.rec.cdb();
    CHECK_EQ(s.rec.bufLen(), (u32)2448);
    CHECK_EQ(s.rec.flags(), (u8)0x48);
    CHECK_EQ(s.rec.cdbLen(), (u8)12);
    CHECK_EQ(c[0], (u8)0xD8);
    CHECK_EQ(c[1], (u8)(32 * 1));
    CHECK_EQ(c[2], (u8)0x00);   // LBA 0x00010203 big-endian
    CHECK_EQ(c[3], (u8)0x01);
    CHECK_EQ(c[4], (u8)0x02);
    CHECK_EQ(c[5], (u8)0x03);
    CHECK_EQ(c[9], (u8)0x01);   // transfer length = 1
    CHECK_EQ(c[10], (u8)0x02);  // mode 2 -> subch 2
}

TEST(DiscAspi, ReadRawSector_ModeBuffers) {
    u8 buf[2448] = {};
    { ScopedAspi s; AspiReadRawSector(0,0,0,buf,0,0); CHECK_EQ(s.rec.bufLen(),(u32)2352); CHECK_EQ(s.rec.cdb()[10],(u8)0); }
    { ScopedAspi s; AspiReadRawSector(0,0,0,buf,0,1); CHECK_EQ(s.rec.bufLen(),(u32)2368); CHECK_EQ(s.rec.cdb()[10],(u8)1); }
    { ScopedAspi s; AspiReadRawSector(0,0,0,buf,0,2); CHECK_EQ(s.rec.bufLen(),(u32)2448); CHECK_EQ(s.rec.cdb()[10],(u8)2); }
}

// ----------------------------------------------------------------------------
// READ CD with ECC (0x1412fd0): CDB[8]=1, CDB[9]=0xF8, mode->CDB[10] mapping.
// ----------------------------------------------------------------------------
TEST(DiscAspi, ReadSectorEcc_Cdb) {
    ScopedAspi s;
    u8 buf[2448] = {};
    AspiReadSectorEcc(0, 0, 0, buf, 0xAABBCCDD, /*mode*/1);
    const u8* c = s.rec.cdb();
    CHECK_EQ(c[0], (u8)0xBE);
    CHECK_EQ(c[2], (u8)0xAA);
    CHECK_EQ(c[3], (u8)0xBB);
    CHECK_EQ(c[4], (u8)0xCC);
    CHECK_EQ(c[5], (u8)0xDD);
    CHECK_EQ(c[8], (u8)0x01);   // transfer length
    CHECK_EQ(c[9], (u8)0xF8);   // sync+hdr+user+EDC/ECC
    CHECK_EQ(c[10], (u8)0x02);  // mode 1 -> 2
}

TEST(DiscAspi, ReadSectorEcc_SubchMap) {
    u8 buf[2448] = {};
    { ScopedAspi s; AspiReadSectorEcc(0,0,0,buf,0,0); CHECK_EQ(s.rec.cdb()[10],(u8)0); }
    { ScopedAspi s; AspiReadSectorEcc(0,0,0,buf,0,1); CHECK_EQ(s.rec.cdb()[10],(u8)2); }
    { ScopedAspi s; AspiReadSectorEcc(0,0,0,buf,0,2); CHECK_EQ(s.rec.cdb()[10],(u8)1); }
}

// ----------------------------------------------------------------------------
// READ(10) (0x1413430): mode==0 obf perturb of LBA, big-endian fill.
// ----------------------------------------------------------------------------
TEST(DiscAspi, ReadSector_Cooked) {
    ScopedAspi s;
    u8 buf[2048] = {};
    AspiReadSector(0, 0, 0, buf, 0x01020304, /*mode*/1);  // mode!=0: no perturb
    const u8* c = s.rec.cdb();
    CHECK_EQ(s.rec.bufLen(), (u32)2048);
    CHECK_EQ(s.rec.cdbLen(), (u8)10);
    CHECK_EQ(c[0], (u8)0x28);
    CHECK_EQ(c[2], (u8)0x01);
    CHECK_EQ(c[3], (u8)0x02);
    CHECK_EQ(c[4], (u8)0x03);
    CHECK_EQ(c[5], (u8)0x04);
    CHECK_EQ(c[8], (u8)0x01);   // transfer length = (mode?1:0)
}

TEST(DiscAspi, ReadSector_ZeroModePerturb) {
    ScopedAspi s;
    u8 buf[2048] = {};
    u32 lba = 0x00010000;
    AspiReadSector(0, 0, 0, buf, lba, /*mode*/0);
    u32 adj = lba - kObfTable[(ProtectionCounter() - 1) % 9];
    const u8* c = s.rec.cdb();
    CHECK_EQ(s.rec.bufLen(), (u32)0);
    CHECK_EQ(c[2], (u8)(adj >> 24));
    CHECK_EQ(c[3], (u8)(adj >> 16));
    CHECK_EQ(c[4], (u8)(adj >> 8));
    CHECK_EQ(c[5], (u8)(adj));
    CHECK_EQ(c[8], (u8)0x00);   // transfer length = 0 when mode==0
}

// ----------------------------------------------------------------------------
// TEST UNIT READY (0x1413790) and INQUIRY (0x1413900).
// ----------------------------------------------------------------------------
TEST(DiscAspi, TestUnitReady_Cdb) {
    ScopedAspi s;
    AspiTestUnitReady(0x05, 0x06, 0x01, 0);
    const u8* c = s.rec.cdb();
    CHECK_EQ(s.rec.haId(), (u8)0x05);
    CHECK_EQ(s.rec.target(), (u8)0x06);
    CHECK_EQ(s.rec.lun(), (u8)0x01);
    CHECK_EQ(s.rec.flags(), (u8)0x50);
    CHECK_EQ(s.rec.cdbLen(), (u8)6);
    CHECK_EQ(c[0], (u8)0x00);
    CHECK_EQ(c[1], (u8)(32 * 1));
}

TEST(DiscAspi, Inquiry_Cdb) {
    ScopedAspi s;
    u8 buf[36] = {};
    AspiInquiry(0x05, 0x06, 0x00, buf);
    const u8* c = s.rec.cdb();
    CHECK_EQ(s.rec.bufLen(), (u32)36);
    CHECK_EQ(s.rec.flags(), (u8)0x48);
    CHECK_EQ(s.rec.cdbLen(), (u8)6);
    CHECK_EQ(c[0], (u8)0x12);
    CHECK_EQ(c[4], (u8)36);   // alloc len in CDB[4]
}

TEST(DiscAspi, Inquiry_BufferReceivesReply) {
    ScopedAspi s;
    // Standard INQUIRY data: vendor at +8 (8 bytes), product at +16 (16 bytes).
    u8 reply[36] = {};
    const char vendor[8]  = {'A','D','A','P','T','E','C',' '};
    const char product[8] = {'C','D','-','R','O','M',' ',' '};
    std::memcpy(reply + 8, vendor, 8);
    std::memcpy(reply + 16, product, 8);
    s.rec.replyStatus = kSS_COMP;
    s.rec.replyData = reply; s.rec.replyLen = sizeof(reply);
    u8 buf[36] = {};
    int r = AspiInquiry(0, 0, 0, buf);
    CHECK_EQ(r, 1);
    CHECK(std::memcmp(buf + 8, "ADAPTEC ", 8) == 0);
    CHECK(std::memcmp(buf + 16, "CD-ROM  ", 8) == 0);
}

// ----------------------------------------------------------------------------
// EnumScsiDevices (0x1412120): pure enumeration-loop shape via hooks.
// ----------------------------------------------------------------------------
namespace {
struct EnumStub : ScsiEnumHooks {
    // Scenario knobs.
    bool firstOk = true;
    u32  errAfterFirst = 0;
    bool fallbackEnumOk = false;
    int  sizeProbeRet = 0;     // getDetail probe return (0 == ok in our flow? no)
    u32  errAfterProbe = 122;
    int  fillRet = 0;          // 0 == success
    bool openOk = true;
    u32  count = 0;
    u32  ids[4] = {};
    int  hitAt = -1;           // index whose probe returns 1 (-1 = none)

    int calls = 0;
    u8  primedV9_5 = 0xEE;     // capture v9[5]
    void* lastOpenSpec = nullptr;
    u32 lastOpenAccess = 0, lastOpenShare = 0, lastOpenFlags = 0;

    int getDetailCalls = 0;

    void* openClassDevs() override { return reinterpret_cast<void*>(0x1); }
    void* openClassDevsFallback() override { return reinterpret_cast<void*>(0x2); }
    bool enumFirst(void* set, u32, void* outDetail) override {
        if (set == reinterpret_cast<void*>(0x2)) return fallbackEnumOk;
        *reinterpret_cast<void**>(outDetail) = reinterpret_cast<void*>(0x10);
        return firstOk;
    }
    u32 lastError() override {
        return (getDetailCalls == 0) ? errAfterFirst : errAfterProbe;
    }
    int getDetail(void* /*detail*/, u32, void* buf, u32, u32* outLen) override {
        if (getDetailCalls++ == 0) {
            *outLen = (u32)(count ? count * 2 * 4 + 8 : 8);
            return sizeProbeRet;   // !=0 -> caller bails before lastError check
        }
        // fill phase: lay out [count, then pairs (?, id)].
        u32* w = reinterpret_cast<u32*>(buf);
        w[0] = count;
        for (u32 i = 0; i < count && i < 4; ++i) {
            w[2 * i + 1] = ids[i];
        }
        return fillRet;
    }
    bool openDevice(void* spec, u32 access, u32 share, u32 flags, void** out) override {
        lastOpenSpec = spec; lastOpenAccess = access; lastOpenShare = share;
        lastOpenFlags = flags;
        primedV9_5 = reinterpret_cast<u8*>(spec)[5];
        *out = reinterpret_cast<void*>(0x99);
        return openOk;
    }
    int probeDevice(u32 deviceId, void*) override {
        for (u32 i = 0; i < count && i < 4; ++i)
            if ((int)i == hitAt && ids[i] == deviceId) return 1;
        return 0;
    }
};
} // namespace

TEST(DiscAspi, Enum_HitReturnsOne) {
    EnumStub h; SetScsiEnumHooks(&h);
    h.count = 3; h.ids[0] = 0xAA; h.ids[1] = 0xBB; h.ids[2] = 0xCC; h.hitAt = 1;
    int r = EnumScsiDevicesAspi();
    SetScsiEnumHooks(nullptr);
    CHECK_EQ(r, 1);
    CHECK_EQ(h.primedV9_5, (u8)5);          // descriptor primed v9[5]=5
    CHECK_EQ(h.lastOpenAccess, (u32)2);
    CHECK_EQ(h.lastOpenShare, (u32)32);
    CHECK_EQ(h.lastOpenFlags, (u32)544);
}

TEST(DiscAspi, Enum_NoHitReturnsZero) {
    EnumStub h; SetScsiEnumHooks(&h);
    h.count = 2; h.ids[0] = 1; h.ids[1] = 2; h.hitAt = -1;
    int r = EnumScsiDevicesAspi();
    SetScsiEnumHooks(nullptr);
    CHECK_EQ(r, 0);
}

TEST(DiscAspi, Enum_FirstFailNon1008Bails) {
    EnumStub h; SetScsiEnumHooks(&h);
    h.firstOk = false; h.errAfterFirst = 5;   // not 1008
    int r = EnumScsiDevicesAspi();
    SetScsiEnumHooks(nullptr);
    CHECK_EQ(r, 0);
}

TEST(DiscAspi, Enum_FirstFail1008Fallback) {
    EnumStub h; SetScsiEnumHooks(&h);
    h.firstOk = false; h.errAfterFirst = 1008; h.fallbackEnumOk = true;
    h.count = 1; h.ids[0] = 7; h.hitAt = 0;
    int r = EnumScsiDevicesAspi();
    SetScsiEnumHooks(nullptr);
    CHECK_EQ(r, 1);
}

TEST(DiscAspi, Enum_OpenDeviceFailBails) {
    EnumStub h; SetScsiEnumHooks(&h);
    h.count = 1; h.ids[0] = 7; h.hitAt = 0; h.openOk = false;
    int r = EnumScsiDevicesAspi();
    SetScsiEnumHooks(nullptr);
    CHECK_EQ(r, 0);
}
