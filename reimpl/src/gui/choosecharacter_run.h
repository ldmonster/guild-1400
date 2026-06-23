#pragma once
// guild::gui — the dynasty/ancestry scene RUN LOOP, reconstructed 1:1.
//
//   VIBE_Menu_RunChooseCharacter @0x52bcd4 — the spine's "manual family tree" branch (from
//   the ChooseCharacterIntro router). A 3D scene (reusing the "Menu\\CHOOSECITY_HEADER" form
//   + Sky_Mittel_02 + band-4 lighting) with NINE clickable animated ancestor actors
//   (VIBE_Character_CreateMenuDummyActor). Clicking actors fills the six dynasty slots
//   dword_122F258[0..5] (paternal grandfather/grandmother, maternal grandfather/grandmother,
//   father, mother); each click records the actor's PROFESSION code into the next free slot
//   of the actor's gender parity (even slot = male, odd = female) and plays its animation.
//
// Once all six slots are filled (v7 >= 6) the scene chains:
//   if (!ChooseCharacterTalent())                     -> v33 = 0, exit   (talent cancelled)
//   else if (ChooseProfession() && BuildPreview())    -> v33 = 1, exit   (start the game)
//   else                                              -> re-show form, retry the chain
// and returns v33 (1 = the new game is armed, 0 = aborted). A fade-to-black plays on success.
//
// The dynasty slot-fill + completion LOGIC is gui/charcreate.* (ChooseCharacter_ApplyActorClick
// / DynastyTable / ChooseCharacter_IsComplete). THIS module reconstructs the RUN LOOP that
// drives it. The scene render + the actor pick + the animated actors themselves + the
// Talent/Profession/Preview sub-screens are host leaves (CharSceneRunHooks); the native 3D
// scene with skinned-mesh actors is DEFERRED (it needs the character-animation subsystem —
// see the progress notes; rule 8: named, not faked).
#include "gui/charcreate.h"

namespace guild::gui {

struct ChooseCharacterState {
    DynastyTable dynasty;     // dword_122F258[6]
    int fillCounter = 0;      // v6/v7 walk over the filled slots
    int close  = 0;           // dword_631614
    int result = 0;           // v33 (1 = start armed, 0 = aborted)
};

struct CharSceneRunHooks {
    virtual ~CharSceneRunHooks() = default;

    virtual void SceneSetup() {}                       // create actors / form / sky
    virtual void SceneTeardown() {}                    // destroy actors / form / fade
    virtual int  RunFrameLoop(int frame) { (void)frame; return 0; }  // 0 ends the loop
    virtual bool WindowClosed(int frame) { (void)frame; return false; }  // dword_672230
    // The ancestor actor under the pick this frame (dword_631720 -> actor), or kNone.
    virtual ChooseCharActor PickActor(int frame) { (void)frame; return ChooseCharActor::kNone; }
    // Play the clicked actor's animation in the scene (VIBE_Command_Handler) after a fill.
    virtual void PlayActorAnim(ChooseCharActor actor, int slot) { (void)actor; (void)slot; }

    // The completion chain (each its own reconstructed screen):
    virtual bool ChooseCharacterTalent() { return false; }   // 0x52b088 (false = cancelled)
    virtual bool ChooseProfession() { return false; }        // 0x52c50c
    virtual bool BuildCharacterPreviewScene() { return false; } // 0x52b6b8
};

CharSceneRunHooks* Menu_SetChooseCharacterHooks(CharSceneRunHooks* hooks);

struct ChooseCharacterRecord {
    int  frames = 0;
    int  actorsPlaced = 0;     // slots filled by a valid click
    bool completed = false;    // all six slots filled
    bool talentRan = false, professionRan = false, previewRan = false;
    bool started = false;      // v33 == 1
    bool cancelled = false;    // window-close before complete
};

// gilde.exe 0x52bcd4 — VIBE_Menu_RunChooseCharacter. Returns 1 (start armed) or 0 (aborted).
int Menu_RunChooseCharacter(ChooseCharacterState& st, ChooseCharacterRecord* rec, int maxFrames);

} // namespace guild::gui
