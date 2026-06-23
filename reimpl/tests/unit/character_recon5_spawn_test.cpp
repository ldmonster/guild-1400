// Golden-vector tests for the VIBE_Character spawn cluster (recon5).
#include "tests/framework/test.h"

#include "src/sim/character_recon5_spawn.h"

#include <cstring>

using namespace guild::sim;

TEST(CharacterRecon5Spawn, SpawnNegativeUniverseReturnsZero) {
    ActorRec a; SpawnHooks H;
    CHECK_EQ(SpawnOfficeStaffActor(&a, -1, nullptr, nullptr, nullptr, 0, H), 0);
}

TEST(CharacterRecon5Spawn, SpawnSwitchesAndRestoresActiveSlot) {
    ActorRec a; a.modelKey=5; a.personId=42;
    SpawnHooks H; H.prevActiveSlot = 17;
    static int switches[4]; static int n; n=0;
    H.switchActiveSlot = [](int u,int,int,int){ if (n<4) switches[n++]=u; };
    // no model resolved -> create skipped, but slot switch + restore still happen
    SpawnOfficeStaffActor(&a, 3, nullptr, nullptr, nullptr, 0, H);
    CHECK_EQ(n, 2);
    CHECK_EQ(switches[0], 3);    // switch to target universe
    CHECK_EQ(switches[1], 17);   // restore previous
}

TEST(CharacterRecon5Spawn, SpawnCreatesFromModelAndNames) {
    ActorRec a; a.modelKey=5; a.personId=42;
    static StaffModelDef def; std::strcpy(def.name, "Wirt");
    SpawnHooks H;
    H.switchActiveSlot=[](int,int,int,int){};
    H.resolveStaffModel=[](guild::u16){ return &def; };
    static char createdName[64]; createdName[0]=0;
    H.createFromModel=[](const char* m, int){ std::strncpy(createdName, m, 63); return 9001; };
    H.applyHeadVariant=[](ActorRec*){};
    H.resolveHeadBone=[](ActorRec*,int){};
    int r = SpawnOfficeStaffActor(&a, 3, nullptr, nullptr, nullptr, 0, H);
    CHECK_EQ(r, 9001);
    CHECK_EQ(a.characterHandle, 9001);
    CHECK_EQ(std::strcmp(createdName, "Wirt"), 0);
}

// Pins the EXACT head-variant copy loop at 0x57c866..0x57c87c.  The binary reads
// the source 2 bytes at a time but advances the destination by only 1 byte per
// iteration (post-increment v15++ after writing dst[1]).  This is NOT a plain
// strcpy: each iteration overwrites the byte the previous iteration wrote at
// dst[1].  For src "Wirt" (W,i,r,t,0) the resulting actor head-variant buffer is
// "Wr\0" — verified by tracing the decompiled loop:
//   it1: dst[0]='W'; dst[1]='i'; dst++           (buf: W i ..)
//   it2: dst[0]='r' (overwrites 'i'); dst[1]='t'; dst++   (buf: W r t ..)
//   it3: dst[0]=0 (overwrites 't'); break        (buf: W r \0 ..)
TEST(CharacterRecon5Spawn, SpawnHeadVariantCopyTwoByteOneAdvance) {
    ActorRec a; a.modelKey=5; a.personId=1;
    static StaffModelDef def; std::strcpy(def.name, "Wirt");
    SpawnHooks H;
    H.switchActiveSlot=[](int,int,int,int){};
    H.resolveStaffModel=[](guild::u16){ return &def; };
    H.createFromModel=[](const char*, int){ return 1; };
    H.applyHeadVariant=[](ActorRec*){};
    H.resolveHeadBone=[](ActorRec*,int){};
    SpawnOfficeStaffActor(&a, 0, nullptr, nullptr, nullptr, 0, H);
    // The quirky 2-byte-read / 1-byte-advance copy yields "Wr", not "Wirt".
    CHECK_EQ((int)a.headVariant[0], (int)'W');
    CHECK_EQ((int)a.headVariant[1], (int)'r');
    CHECK_EQ((int)a.headVariant[2], 0);
}

// Single-character source: src "A" (A,0).  it1 writes dst[0]='A', sees c0!=0,
// reads c1=src[1]=0, dst[1]=0, dst++, loop condition (c1) is false -> stop.
// Result buffer: 'A','\0' starting at index0, but note dst[1] (index1) was set to
// 0 BEFORE the advance, so index1==0 and index0=='A'.
TEST(CharacterRecon5Spawn, SpawnHeadVariantCopySingleChar) {
    ActorRec a; a.modelKey=5;
    static StaffModelDef def; std::strcpy(def.name, "A");
    SpawnHooks H;
    H.switchActiveSlot=[](int,int,int,int){};
    H.resolveStaffModel=[](guild::u16){ return &def; };
    H.createFromModel=[](const char*, int){ return 1; };
    H.applyHeadVariant=[](ActorRec*){};
    H.resolveHeadBone=[](ActorRec*,int){};
    SpawnOfficeStaffActor(&a, 0, nullptr, nullptr, nullptr, 0, H);
    CHECK_EQ((int)a.headVariant[0], (int)'A');
    CHECK_EQ((int)a.headVariant[1], 0);
}

// Empty source: src "" (0).  it1: c0=0, dst[0]=0, break immediately.
TEST(CharacterRecon5Spawn, SpawnHeadVariantCopyEmpty) {
    ActorRec a; a.modelKey=5;
    static StaffModelDef def; def.name[0]=0;
    SpawnHooks H;
    H.switchActiveSlot=[](int,int,int,int){};
    H.resolveStaffModel=[](guild::u16){ return &def; };
    H.createFromModel=[](const char*, int){ return 1; };
    H.applyHeadVariant=[](ActorRec*){};
    H.resolveHeadBone=[](ActorRec*,int){};
    SpawnOfficeStaffActor(&a, 0, nullptr, nullptr, nullptr, 0, H);
    CHECK_EQ((int)a.headVariant[0], 0);
}

TEST(CharacterRecon5Spawn, SpawnAnchorBoneTransform) {
    ActorRec a; a.modelKey=1;
    static TObject anchor; anchor.pos[0]=11; anchor.pos[1]=22; anchor.pos[2]=33;
    SpawnHooks H;
    H.switchActiveSlot=[](int,int,int,int){};
    H.findByHandle=[](void*,int,int,int,int)->TObject*{ return &anchor; };
    static f32 transformedIn[3]; static bool called; called=false;
    H.pointThroughBoneChain=[](TObject* o, const f32* in, f32*){
        called=true; transformedIn[0]=in[0]; transformedIn[1]=in[1]; transformedIn[2]=in[2]; (void)o; };
    // no staff model -> create skipped, but the bone-chain transform must run
    SpawnOfficeStaffActor(&a, 0, nullptr, nullptr, nullptr, /*findHandle*/55, H);
    CHECK(called);
    CHECK_EQ(transformedIn[0], 11.0f);  // transforms anchor.pos (+76)
    CHECK_EQ(transformedIn[2], 33.0f);
}

TEST(CharacterRecon5Spawn, EntranceNoPersonReturnsZero) {
    EntranceHooks H;
    CHECK_EQ(SpawnAtBuildingEntrance(7, 1, -1, nullptr, nullptr, H), 0);
}

TEST(CharacterRecon5Spawn, EntranceExistingCharacterReturnsZero) {
    static ActorRec person; person.characterHandle = 1;  // already spawned
    EntranceHooks H;
    H.findPersonById=[](int)->ActorRec*{ return &person; };
    CHECK_EQ(SpawnAtBuildingEntrance(7, 1, -1, nullptr, nullptr, H), 0);
}

TEST(CharacterRecon5Spawn, EntranceProductionUsesBuildingAvatar) {
    static ActorRec person; person.characterHandle = 0;
    static SpawnHooks spawn; spawn.prevActiveSlot=0;
    spawn.switchActiveSlot=[](int,int,int,int){};
    spawn.resolveStaffModel=[](guild::u16)->StaffModelDef*{ return nullptr; };  // no create
    EntranceHooks H; H.spawn=&spawn;
    H.findPersonById=[](int)->ActorRec*{ return &person; };
    H.personQueryBegin=[](ActorRec*,int,int,int){ return 555; };
    H.isProductionType=[](int){ return 1; };
    static int ensuredBuilding; ensuredBuilding=-1;
    H.ensureBuildingAvatar=[](int rec,int,guild::i16){ ensuredBuilding=rec; return 4; };
    H.ensureObjectAvatar=[](int){ return -1; };
    // objHandle == -1 path ; person has no char after spawn -> returns 0
    int r = SpawnAtBuildingEntrance(7, 1, -1, nullptr, nullptr, H);
    CHECK_EQ(ensuredBuilding, 555);   // building avatar used for production
    CHECK_EQ(r, 0);                   // no character created (no model)
}

TEST(CharacterRecon5Spawn, EntranceNonProductionNoObjectUsesSlotZero) {
    static ActorRec person; person.characterHandle = 0;
    static SpawnHooks spawn;
    spawn.switchActiveSlot=[](int,int,int,int){};
    spawn.resolveStaffModel=[](guild::u16)->StaffModelDef*{ return nullptr; };
    EntranceHooks H; H.spawn=&spawn;
    H.findPersonById=[](int)->ActorRec*{ return &person; };
    H.personQueryBegin=[](ActorRec*,int,int,int){ return 1; };
    H.isProductionType=[](int){ return 0; };       // not production
    static bool objAvatarCalled; objAvatarCalled=false;
    H.ensureObjectAvatar=[](int){ objAvatarCalled=true; return 9; };
    // objHandle -1 -> objRec stays 0 -> slot 0 path, ensureObjectAvatar NOT called
    SpawnAtBuildingEntrance(7, 1, -1, nullptr, nullptr, H);
    CHECK(!objAvatarCalled);
}

// Pins the entrance->staff argument order at 0x57c9a9:
//   SpawnOfficeStaffActor(person, slot, /*rotPtr=*/entrance.rotPtr(a5),
//                                       /*posPtr=*/entrance.posPtr(a2), ...)
// A queue object (objHandle != -1, objRec != 0) takes the wait-anim branch which
// forwards both pointers.  In SpawnOfficeStaffActor, SetWorldTranslation is gated
// on the ROTATION pointer (a3 = staff rotPtr, 0x57c84a `if (v13) ...`) and is
// passed that same pointer.  Pass a non-null rotPtr and a NULL posPtr: with the
// correct order staff.rotPtr == our rotVec (non-null) -> SetWorldTranslation fires
// with rotVec.  With the (pre-fix) swap, staff.rotPtr == posPtr == null ->
// SetWorldTranslation would NOT fire.  This cleanly pins the a3/a4 order.
TEST(CharacterRecon5Spawn, EntrancePosRotForwardedInOrder) {
    static ActorRec person; person.characterHandle = 0;
    static SpawnHooks spawn; spawn.prevActiveSlot = 0;
    spawn.switchActiveSlot = [](int,int,int,int){};
    static StaffModelDef def; def.name[0] = 'x'; def.name[1] = 0;
    spawn.resolveStaffModel = [](guild::u16)->StaffModelDef*{ return &def; };
    spawn.createFromModel = [](const char*, int){ return 12345; };  // non-null handle
    spawn.applyHeadVariant = [](ActorRec*){};
    spawn.resolveHeadBone = [](ActorRec*,int){};
    static const f32* gotWtrans; gotWtrans = nullptr;
    spawn.setWorldTranslation = [](int, const f32* w){ gotWtrans = w; };

    EntranceHooks H; H.spawn = &spawn;
    H.findPersonById = [](int)->ActorRec*{ return &person; };
    H.personQueryBegin = [](ActorRec*,int,int,int){ return 555; };
    H.gameObjectQueryFind = [](int,int,int,int){ return 777; };   // objRec != 0 -> queue branch
    H.isProductionType = [](int){ return 1; };
    H.ensureBuildingAvatar = [](int,int,guild::i16){ return 4; };
    H.pickWaitAnimation = [](int)->const char*{ return "wait"; };

    static guild::u16 rotVec[4] = {9,9,9,0};
    SpawnAtBuildingEntrance(/*personId*/7, /*building*/1, /*objHandle*/2,
                            /*posPtr*/nullptr, /*rotPtr*/rotVec, H);
    // SetWorldTranslation must have fired with exactly our rotVec pointer.
    CHECK(gotWtrans == reinterpret_cast<const f32*>(rotVec));
}

TEST(CharacterRecon5Spawn, PreloadWalkMatchesByKey) {
    SceneObject objs[3];
    objs[0].objPtr=0;  objs[0].key=5;  objs[0].matchVal=0;   // empty -> skip
    objs[1].objPtr=100; objs[1].key=5; objs[1].matchVal=99;  // key match
    objs[2].objPtr=200; objs[2].key=8; objs[2].matchVal=77;  // no match
    PreloadHooks H;
    static int preloaded[4]; static int n; n=0;
    H.collectByOwner=[](int,int){};
    H.preloadAniSet=[](int objPtr,int,const char* const*,int){ if(n<4) preloaded[n++]=objPtr; };
    // setKey=5 matches objs[1] by key; objs[2] key 8 != 5 and matchVal 77 != setMatch 0
    PreloadSceneAnimations(/*type*/1, objs, 3, /*setKey*/5, /*setMatch*/0, H);
    CHECK_EQ(n, 1);
    CHECK_EQ(preloaded[0], 100);
}

TEST(CharacterRecon5Spawn, PreloadMatchesBySecondaryField) {
    SceneObject objs[1];
    objs[0].objPtr=300; objs[0].key=8; objs[0].matchVal=42;  // key mismatch but matchVal hits
    PreloadHooks H;
    static int cnt; cnt=0;
    H.collectByOwner=[](int,int){};
    H.preloadAniSet=[](int,int,const char* const*,int){ cnt++; };
    PreloadSceneAnimations(1, objs, 1, /*setKey*/5, /*setMatch*/42, H);
    CHECK_EQ(cnt, 1);    // matchVal == setMatch
}

// Pins the PreloadAniSet argument shape at 0x50666c: the binary passes count=16
// and exactly 16 name-slice pointers (v6+1, v6+49, ... v6+721 ; stride 48 ->
// (721-1)/48 + 1 == 16 slices), NOT 18.
TEST(CharacterRecon5Spawn, PreloadAniSetCountIsSixteen) {
    SceneObject objs[1];
    objs[0].objPtr=400; objs[0].key=5; objs[0].matchVal=0;  // key match
    PreloadHooks H;
    static int gotCount; static int gotSliceCount; gotCount=-1; gotSliceCount=-1;
    H.collectByOwner=[](int,int){};
    H.preloadAniSet=[](int,int n,const char* const*,int sc){ gotCount=n; gotSliceCount=sc; };
    PreloadSceneAnimations(1, objs, 1, /*setKey*/5, /*setMatch*/0, H);
    CHECK_EQ(gotCount, 16);
    CHECK_EQ(gotSliceCount, 16);
}

TEST(CharacterRecon5Spawn, SpawnDefaultsAreZero) {
    CHECK_EQ(kDefaultSpawnPos[0], 0.0f);
    CHECK_EQ(kDefaultSpawnRot[2], 0.0f);
}
