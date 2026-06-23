// tests/unit/slice_production_test.cpp — Wave 27 P5 BUILDINGS/PRODUCTION slice, unit.
//
// On a SYNTHETIC live world (no assets): assert the classifier picks the production
// order for a workshop + the right opcode/prot; assert the apply mutates the expected
// production-slot record; assert the step sequence is deterministic and the order +
// the real production tick mutate the world while a non-write item does not.
#include "test.h"

#include "play/slice_production.h"
#include "play/interact_building.h"
#include "sim/building_production.h"   // ProdBuildingAt / g_prodStore
#include "sim/command.h"

using namespace guild;
using namespace guild::play;

namespace {
const i16 kProt = 42;
const int kBld  = 1;
}

// ---------------------------------------------------------------------------
// Golden: pin the QueueRequestSlotReset28 opcode + the 248-byte StagePendingBlock
// body size (StagePendingBlock(0xF8,&scratch)). Traced to slice_production.h.
// ---------------------------------------------------------------------------
TEST(SliceProductionUnit, OpcodeAndBodyBytesGolden) {
    CHECK_EQ((int)kProductionCmdOpcode, 28);       // 0x1C
    CHECK_EQ((unsigned)kProductionBodyBytes, 0xF8u); // 248-byte StagePendingBlock body
    CHECK_EQ((int)kProductionBodyBytes, 248);
}

// ---------------------------------------------------------------------------
// Classifier golden: a production workshop kind + WriteProduction -> opcode 28.
// ---------------------------------------------------------------------------
TEST(SliceProductionUnit, ClassifierGolden) {
    // Each production-workshop kind classifies a write order.
    for (int kind : {116, 133, 155, 247}) {
        ProductionCommand c =
            ClassifyProductionAction(kind, ProductionMenuItem::kWriteProduction, kProt);
        CHECK(c.issued);
        CHECK_EQ((int)c.opcode, (int)kProductionCmdOpcode);
        CHECK_EQ((int)c.opcode, 28);
        CHECK_EQ((int)c.prot, (int)kProt);
        CHECK(GroupIsProductionWorkshop(c.group));
    }
    // A non-production building kind (treasury 276) -> no production order.
    {
        ProductionCommand c =
            ClassifyProductionAction(276, ProductionMenuItem::kWriteProduction, kProt);
        CHECK(!c.issued);
        CHECK(!GroupIsProductionWorkshop(c.group));
    }
    // A production workshop but a non-write menu item -> no order.
    {
        ProductionCommand c =
            ClassifyProductionAction(247, ProductionMenuItem::kNone, kProt);
        CHECK(!c.issued);
    }
}

// ---------------------------------------------------------------------------
// Apply golden: ProductionApplyOrder writes the prot into the first free slot
// and activates it.
// ---------------------------------------------------------------------------
TEST(SliceProductionUnit, ApplyWritesSlotRecord) {
    sim::ResetProductionTables();
    sim::ProdBuilding pb = sim::ProdBuildingAt(kBld);
    // No slot holds the prot yet.
    CHECK_EQ((int)(i16)(pb.slotProtPacked(0) >> 16), 0);

    int slot = ProductionApplyOrder(kBld, kProt);
    CHECK_EQ(slot, 0);                                  // first free slot
    CHECK_EQ((int)(i16)(pb.slotProtPacked(0) >> 16), (int)kProt);   // prot written
    CHECK_EQ((int)pb.slotActive(0), 1);                 // slot activated

    // A second order lands in the next free slot.
    int slot2 = ProductionApplyOrder(kBld, (i16)(kProt + 1));
    CHECK_EQ(slot2, 1);
    CHECK_EQ((int)(i16)(pb.slotProtPacked(1) >> 16), (int)(kProt + 1));
}

// ---------------------------------------------------------------------------
// The bridge: a click on a workshop issues + enqueues the REAL opcode-28 packet
// and applies it (the apply mutates the slot table).
// ---------------------------------------------------------------------------
TEST(SliceProductionUnit, IssueProductionClickEnqueuesAndApplies) {
    sim::ResetProductionTables();
    sim::CommandQueue q;
    q.Init();
    q.set_standalone(true);
    InstallProductionCommandHandler(q);
    SetProductionApplyHook(nullptr);

    ScenePickObject obj; obj.id = kBld + 1;
    float eye[3] = {0,0,0};
    CityViewCamera cam = MakeCityViewCamera(eye, 1.0f, 64, 64);
    auto kindOf = [](i32){ return 247; };   // generic production location

    u32 before = q.send_count();
    ProductionClickResult pc =
        IssueProductionClick(q, cam, 0.0f, 0.0f, &obj, 1, 64.0f, kindOf,
                             kBld, kProt, ProductionMenuItem::kWriteProduction);

    CHECK(pc.opened);
    CHECK(pc.command.issued);
    CHECK_EQ((int)pc.command.opcode, 28);
    CHECK(pc.enqueued);
    CHECK(q.send_count() != before);
    CHECK(pc.applied);
    CHECK(pc.appliedSlot >= 0);

    // The slot table was mutated by the applied order.
    sim::ProdBuilding pb = sim::ProdBuildingAt(kBld);
    CHECK_EQ((int)(i16)(pb.slotProtPacked(pc.appliedSlot) >> 16), (int)kProt);
}

// ---------------------------------------------------------------------------
// Step sequence: order + the real production tick mutate the world; deterministic.
// ---------------------------------------------------------------------------
TEST(SliceProductionUnit, StepSequenceMutatesAndDeterministic) {
    ProductionStepHash a[4]{}, b[4]{};
    int na = RunProductionStepsSynthetic(0xBEEF, kBld, kProt, 0xC0DE, a, 4);
    int nb = RunProductionStepsSynthetic(0xBEEF, kBld, kProt, 0xC0DE, b, 4);
    CHECK_EQ(na, 4);
    CHECK_EQ(nb, 4);

    // load -> command must mutate (order writes the slot record).
    CHECK(a[1].mutated);
    // command -> tick must mutate (the real production tick refreshes yield/in).
    CHECK(a[2].mutated);
    // tick -> day must mutate (the real economy passes + RNG).
    CHECK(a[3].mutated);

    // Byte-identical across the two runs.
    for (int i = 0; i < 4; ++i)
        CHECK_EQ(a[i].hashAfter, b[i].hashAfter);
}
