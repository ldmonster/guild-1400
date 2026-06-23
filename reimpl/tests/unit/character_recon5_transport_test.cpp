// Golden-vector tests for the VIBE_Character transport cluster (recon5).
#include "tests/framework/test.h"

#include "src/sim/character_recon5_transport.h"

#include <cmath>

using namespace guild::sim;

// Identity-ish object: position + a known affine so attach math is checkable.
static TObject MakeObject() {
    TObject o;
    o.pos[0] = 10.0f; o.pos[1] = 20.0f; o.pos[2] = 30.0f;
    // affine: basis = identity, translation = (100, 200, 300)
    // m index: +396=m0 +400=m1 +404=m2 / +412=m3 +416=m4 +420=m5
    //          +428=m6 +432=m7 +436=m8 / +444=m9 +448=m10 +452=m11
    o.m[0]=1; o.m[1]=0; o.m[2]=0;     // col X (M396/M400/M404)
    o.m[3]=0; o.m[4]=1; o.m[5]=0;     // col Y
    o.m[6]=0; o.m[7]=0; o.m[8]=1;     // col Z
    o.m[9]=100; o.m[10]=200; o.m[11]=300;  // translation
    return o;
}

TEST(CharacterRecon5Transport, AttachComputesLocalZTransform) {
    TObject obj = MakeObject();
    TObject transport;            // gets the computed position
    TChar ch; ch.object = &obj;

    static f32 captured[3];
    TransportHooks H;
    H.setPosition = [](TObject*, const f32* v){ captured[0]=v[0]; captured[1]=v[1]; captured[2]=v[2]; };

    TransportAttach* rec = AttachTransport(&ch, H, &transport);
    CHECK(rec != nullptr);
    CHECK(ch.transport == rec);
    // local (0,0,-70) through identity basis + translation:
    // x = 0*1 + 0*0 + -70*0 + 100 = 100
    // y = 0*0 + 0*1 + -70*0 + 200 = 200
    // z = 0*0 + 0*0 + -70*1 + 300 = 230
    CHECK_EQ(captured[0], 100.0f);
    CHECK_EQ(captured[1], 200.0f);
    CHECK_EQ(captured[2], 230.0f);
    delete rec;
}

TEST(CharacterRecon5Transport, AttachSetsFlagBits) {
    TObject obj = MakeObject();
    TObject transport;
    transport.f529 = 0xFF; transport.f530 = 0xFF; transport.f531 = 0xFF;
    TChar ch; ch.object = &obj;
    TransportHooks H;
    TransportAttach* rec = AttachTransport(&ch, H, &transport);
    // f531 &= ~4  ;  f529 = old & ~2  ;  f530 = (old&0xF3)|4
    CHECK_EQ((int)(transport.f531 & 4), 0);
    CHECK_EQ((int)(transport.f529 & 2), 0);
    CHECK_EQ((int)(transport.f530 & 4), 4);
    CHECK_EQ((int)(transport.f530 & 8), 0);   // bit3 cleared by &0xF3
    delete rec;
}

TEST(CharacterRecon5Transport, MoveToUniverseNullChars) {
    TransportHooks H;
    CHECK_EQ(MoveToUniverse(nullptr, 5, H, 0, 0), 0);
}

TEST(CharacterRecon5Transport, MoveToUniverseRequiresBothUniverses) {
    TObject obj; TChar ch; ch.object=&obj; ch.universe = 0;  // old==0
    TransportHooks H;
    H.moveBetweenUniverses = [](TObject*,int,int){ return 1; };
    CHECK_EQ(MoveToUniverse(&ch, 7, H, 0, 0), 0);   // oldUniverse 0 -> fail
}

TEST(CharacterRecon5Transport, MoveToUniverseSuccessSetsUniverseAndDeflateBit) {
    TObject obj; TChar ch; ch.object=&obj; ch.universe = 3;
    obj.f529 = 0;
    TransportHooks H;
    H.moveBetweenUniverses = [](TObject*,int,int){ return 1; };
    H.indexFromPointer = [](int){ return 0; };   // global universe -> deflate path
    // globalDeflateBit=1 -> sets bit3 of f529
    int r = MoveToUniverse(&ch, 9, H, /*deflateBit*/1, /*globalUniverse*/0);
    CHECK_EQ(r, 1);
    CHECK_EQ(ch.universe, 9);
    CHECK_EQ((int)(obj.f529 & 8), 8);
}

TEST(CharacterRecon5Transport, MoveToUniverseRealUniverseClearsDeflate) {
    TObject obj; TChar ch; ch.object=&obj; ch.universe = 3;
    obj.f529 = 8;                                 // bit3 set
    TransportHooks H;
    H.moveBetweenUniverses = [](TObject*,int,int){ return 1; };
    H.indexFromPointer = [](int){ return 1; };    // real universe -> clear bit3
    int r = MoveToUniverse(&ch, 9, H, 1, 0);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)(obj.f529 & 8), 0);
}

// --- HARDENING: MoveToUniverse with an attached transport that has a bad/zero
// vehicle object pointer. The relocation re-parents the transport's scene object
// via the hook; with a degenerate (zero) object the cast yields a null TObject*
// and the function must still complete without dereferencing it itself.
TEST(CharacterRecon5Transport, MoveToUniverseBadVehicleObjectSafe) {
    TObject obj; TChar ch; ch.object = &obj; ch.universe = 3;
    obj.f529 = 0;
    TransportAttach tr; tr.object = 0;   // degenerate vehicle (null scene object)
    ch.transport = &tr;
    ch.secondary = nullptr;

    static int moveCalls; moveCalls = 0;
    static TObject* lastArg; lastArg = reinterpret_cast<TObject*>(intptr_t(-1));
    TransportHooks H;
    H.moveBetweenUniverses = [](TObject* o, int, int){ ++moveCalls; lastArg = o; return 1; };
    H.indexFromPointer = [](int){ return 1; };   // real universe -> clear-bit path

    int r = MoveToUniverse(&ch, 9, H, 0, 0);
    CHECK_EQ(r, 1);                  // succeeds despite the bad vehicle
    CHECK_EQ(ch.universe, 9);
    CHECK(moveCalls >= 2);           // char move + transport move both issued
    CHECK_EQ((void*)lastArg, (void*)nullptr);  // transport's null object passed through
}

TEST(CharacterRecon5Transport, UpdateNoTransportReturnsMeshCtx) {
    TObject obj; TChar ch; ch.object=&obj; ch.transport=nullptr;
    TransportHooks H;
    H.resolveMesh = [](TChar*){ return 42; };
    CHECK_EQ(UpdateTransportAttach(&ch, H), 42);
}

TEST(CharacterRecon5Transport, UpdateWithinToleranceNoMove) {
    TObject obj;
    obj.pos[0]=1; obj.pos[1]=2; obj.pos[2]=3;
    TChar ch; ch.object=&obj;
    TransportAttach tr;
    tr.last[0]=1; tr.last[1]=2; tr.last[2]=3;     // identical -> within tol
    ch.transport=&tr;
    TransportHooks H;
    H.resolveMesh = [](TChar*){ return 7; };
    bool setPosCalled=false;
    static bool* flag; flag=&setPosCalled;
    H.setPosition = [](TObject*, const f32*){ *flag=true; };
    int r = UpdateTransportAttach(&ch, H);
    // gilde.exe 0x402fa9/0x402fb0: when within tolerance the binary returns the
    // VectorWithinTolerance result (`result = VectorWithinTolerance(...); if(!result)`),
    // NOT the resolved-mesh context. The default tolerance test returns 1.
    CHECK_EQ(r, 1);
    CHECK(!setPosCalled);                          // no movement -> no reposition
    // anchor unchanged
    CHECK_EQ(tr.last[0], 1.0f);
}

TEST(CharacterRecon5Transport, UpdateMovedUpdatesAnchor) {
    TObject obj = MakeObject();
    obj.pos[0]=10; obj.pos[1]=20; obj.pos[2]=30;
    TChar ch; ch.object=&obj;
    // The transport's own scene object (v7 = *(a1[73])); identity basis so the
    // trailing math is well-defined.
    TObject transObj = MakeObject();
    transObj.pos[0]=0; transObj.pos[1]=0; transObj.pos[2]=0;
    TransportAttach tr; tr.last[0]=0; tr.last[1]=0; tr.last[2]=0;  // far -> moves
    tr.object = &transObj;
    ch.transport=&tr;
    TransportHooks H;
    H.resolveMesh = [](TChar*){ return 1; };
    H.angleToTargetSigned = [](TObject*){ return 0.0; };
    UpdateTransportAttach(&ch, H);
    // anchor becomes the char object position
    CHECK_EQ(tr.last[0], 10.0f);
    CHECK_EQ(tr.last[1], 20.0f);
    CHECK_EQ(tr.last[2], 30.0f);
}

TEST(CharacterRecon5Transport, Constants) {
    CHECK_EQ(kSeventy, 70.0f);
    CHECK_EQ(kHeightLerp, 0.25f);
    CHECK(std::fabs(kFadeRate - 0.02) < 1e-12);
    CHECK_EQ(kFadeFull, 255.0f);
    CHECK_EQ(kMorphHalfBias, 0.5f);
    CHECK(std::fabs((double)kTwoPi - 6.2831853) < 1e-6);
}
