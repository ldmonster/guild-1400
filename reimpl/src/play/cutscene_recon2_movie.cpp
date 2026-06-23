// guild::play — VIBE_Movie_PlayOutro control-flow reconstruction.
// See cutscene_recon2_movie.h for the rule-5/6 boundary notes.
//
// gilde.exe 0x534924 — VIBE_Movie_PlayOutro. Disasm-faithful translation of the
// control flow (the moveahead.dll mov_* calls are inert hooks; the codec is NOT
// reimplemented).

#include "play/cutscene_recon2_movie.h"

namespace guild::play {

u8 Movie_PlayOutro(MovieDllHooks& dll, MovieControlHooks& ctl, MovieGlobals& g,
                   const std::string& movieDir, int width, int height,
                   bool dllAvailable) {
    (void)width;
    (void)height;

    // --- 0x534937: one-time lazy bind of moveahead.dll (guarded by dword_63C8F0).
    if (!dll.bound) {
        // 0x534988: LoadLibraryA("moveahead.dll"); on failure the original returns
        // the (null) handle in al WITHOUT running any of the rest of the routine.
        if (!dllAvailable) {
            // hModule == 0 -> return (char)LibraryA == 0. (0x534996 -> 0x534BAC)
            return 0;
        }
        // 0x53499c..0x534a6d: GetProcAddress for each mov_* export. Our hooks model
        // those bindings; we simply mark bound and run init.
        dll.bound = true;                  // 0x534a72: dword_63C8F0 = 1
        if (dll.init) dll.init();          // 0x534a78: dword_63C768() (mov_Init_)
    }

    // --- 0x534939 (loc): music fade-out + fade-to-BLACK pre-roll.
    if (ctl.musicSetTrackFade)
        ctl.musicSetTrackFade(0.0f, kMovieMusicFadeMs);   // 0x534942

    // 0x534964: VIBE_Fade_Register(0, .., height>>16, width>>16, "BLACK", 90, 1)
    const u8* overlay = nullptr;
    if (ctl.fadeRegister) overlay = ctl.fadeRegister();
    (void)overlay;

    // 0x53496B: spin the game frame loop until the fade overlay reports done
    // (status & 0x4) AND the fade timer has elapsed (0.0 > flt_62DA00 keeps it
    // spinning).  Loop body: VIBE_GameLogic_RunFrameLoop.
    //   while ( (status & 4) == 0 || 0.0 > flt_62DA00 ) runFrame();
    for (;;) {
        u8 status = ctl.fadeStatusByte ? ctl.fadeStatusByte() : 0x4;     // *esi
        if ((status & 0x4) != 0) {
            // 0x534A83: fldz / fcomp flt_62DA00 ; jbe (0.0 <= timer) -> exit loop
            float timer = ctl.fadeTimer ? ctl.fadeTimer() : 0.0f;
            if (!(0.0f > timer))   // jbe: 0.0 <= timer -> done
                break;
        }
        if (ctl.runFrameLoop) ctl.runFrameLoop();                        // 0x53497C
    }

    // --- 0x534AA5: save & disable the "movies enabled" byte for the duration.
    const u8 savedMoviesEnabled = g.moviesEnabled;   // v14 = byte_642008
    g.moviesEnabled = 0;                             // byte_642008 = 0

    // 0x534AB9: build "<movieDir>outro.mpg" (the clip path handed to mov_Prepare_).
    const std::string clipPath = movieDir + "outro.mpg";
    (void)clipPath;   // consumed by the (inert) codec; control logic only builds it

    // 0x534ABE: dword_62D580 == 1 -> present a frame before going into the clip.
    if (g.renderMode == 1 && ctl.presentFrame)
        ctl.presentFrame();                          // 0x534ACC

    // 0x534AD6..0x534AE8: release input (keyboard poll with focus-grab gate set,
    // then mouse unacquire).
    if (ctl.pollKeyboard) { ctl.pollKeyboard(true); }    // dword_62D0E0=1; poll; =0
    if (ctl.acquireMouse) ctl.acquireMouse(false);       // VIBE_Input_AcquireMouseDevice(0)
    if (ctl.pumpMessages) ctl.pumpMessages();            // 0x534AED
    if (ctl.sleep) ctl.sleep(kMoviePrePlaySleepMs);      // 0x534AF7: Sleep(500)

    // 0x534B17: dword_63C778(0,0,0,0,hInstance,138) -> playback handle (mov_Prepare_)
    dll.handle = dll.prepare ? dll.prepare(ctl.hInstance, kMoviePrepareEventArg) : 0;
    if (ctl.sleep) ctl.sleep(kMoviePostPrepSleepMs);     // 0x534B27: Sleep(200)

    // 0x534B33: dword_63C770(handle) (mov_Play_ — runs the proprietary playback).
    if (dll.play) dll.play(dll.handle);

    // 0x534B3B: tear down the fade overlay; pump; restore window focus.
    if (ctl.fadeUnregister) ctl.fadeUnregister(overlay);
    if (ctl.pumpMessages) ctl.pumpMessages();            // 0x534B40
    if (ctl.setFocus) ctl.setFocus();                    // 0x534B4C: SetFocus(dword_63CC18)

    // 0x534B55: re-acquire input.
    if (ctl.acquireMouse) ctl.acquireMouse(true);        // VIBE_Input_AcquireMouseDevice(1)
    if (ctl.pollKeyboard) { ctl.pollKeyboard(true); }    // dword_62D0E0=1; poll

    // 0x534B67..0x534B92: restore the master music volume:
    //   v12 = (float)(u8)byte_1233552 * flt_623640 ; clear byte_67225C ;
    //   VIBE_Music_SetTrackFade(v12, 1000).
    const float restoredVol =
        static_cast<float>(static_cast<u8>(g.musicVolume)) * kMovieMusicVolumeScale;
    g.someFlag67225C = 0;                                // 0x534B95: byte_67225C = 0
    if (ctl.musicSetTrackFade)
        ctl.musicSetTrackFade(restoredVol, kMovieMusicFadeMs);   // 0x534B9B

    // 0x534BA7: restore byte_642008 = saved; return it (al) — 0x534BAC.
    g.moviesEnabled = savedMoviesEnabled;
    return savedMoviesEnabled;
}

} // namespace guild::play
