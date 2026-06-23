# Wave-15 TRUE 1:1 binary diff — CAMERA segment (W15-CAMERA)

MCP was LIVE. Every brief target was decompiled and compared LINE-FOR-LINE against
the reconstruction. The wave-13 NEEDS-LIVE-MCP queue is RESOLVED. No source
divergences were found — the reconstruction matches the binary on all targets.
The previously-unpinned numerics are now golden-pinned against the live decompile.

## Result summary

- Segment build: GREEN. All camera test binaries pass.
  - `camera_control_test` (NEW): 26 checks, 0 failures.
  - `camera_recon2_test`: 61 checks, 0 failures (was 42; +19 from this wave).
  - camera_recon_test 40, camera_edge_scroll_test 75, camera_update_recon_test 71,
    camera_recon5_flight_test 9, camera_project_test 14, camera_pick_test 43,
    camera_controls_test 34, session_camera_test 71, render_camera_test 117 — all 0 fail.
- Source edits: NONE. Every constant / control-flow / rounding claim in the source
  matches the live decompile. The wave-13 "UNDER-VERIFIED" items are confirmed 1:1.
- Test additions:
  - NEW `tests/unit/camera_control_test.cpp` — 6 tests pinning the REAL binary
    orbit/frustum constants (the wave-13-flagged missing golden file).
  - `tests/unit/camera_recon2_test.cpp` — +3 tests: ZoomOut 3-band dist clamp
    (band 3, dist=50), and two track-target axis-routing branches (no-lock,
    671D8A-lock); extended the listener-capture helper to record distance and
    added an applyConstraints capture helper.

## Constants recovered/confirmed via get_bytes (wave-15)

ZoomReset (0x4b5250) / ZoomOut (0x4b5974) — identical constant set:
- flt_61DE5C / flt_61DE8C = `0xC4160000` = **-600.0** (dolly distance)
- flt_61DE60 / flt_61DE90 = `0x3F000000` = **0.5** (mesh height-range weight)
- dbl_61DE68 / dbl_61DE98 = `0x3FFCCCCCCCCCCCCD` = **1.8** (back-off term)
- flt_61DE70 / flt_61DEA0 = `0x3A03126F` = **0.0005** (dist easing scale)
- flt_5CA2B0/B4/B8 = (0,0,1) forward axis; flt_6316B4=450, flt_6316BC=1600, flt_6316DC=0.

Orbit/track rate constants (flt_62BF10.. / flt_62BF2C..):
- flt_62BF10 / flt_62BF2C = `0x40490FDB` = **pi** (panSens)
- flt_62BF14 / flt_62BF30 = `0x41F00000` = **30.0** (accelScale)
- flt_62BF18 / flt_62BF34 = `0x40400000` = **3.0** (accelClamp ceiling)
- flt_62BF1C / flt_62BF38 = `0x43800000` = **256.0** (rateX / yaw)
- flt_62BF20 / flt_62BF3C = `0xC3800000` = **-256.0** (rateY / pitch)
- flt_62BF24 = -100.0, flt_62BF28 = +100.0 (orbit roll steps).

Frustum side-plane eps raw dwords (BuildViewMatrix 0x5ACCD0):
- dword_13DCDAC = -1241106499 = **-1.9999999949504854e-06** (side0 w)
- dword_13DCDBC =  906377149  = **+1.9999999949504854e-06** (side1 w)
- dword_13DCDCC =  906377149  = **+eps** (side2 w)
- dword_13DCDDC = -1241106499 = **-eps** (side3 w)
  -> sign pattern side0 -eps, side1 +eps, side2 +eps, side3 -eps. Source MATCHES.

## Per-function verdicts (line-for-line vs live decompile)

| addr | function | verdict |
|------|----------|---------|
| 0x4b4c68 | Camera_Update | VERIFIED-1:1 (early-box path, main gate, updatePan/UpdateMovement/Clamp dispatch, stricter family gate `!631744 && !631748`, history mirrors, returns 631628) |
| 0x4b2c34 | EdgeScroll | VERIFIED-1:1 (wave-13 golden; structure confirmed) |
| 0x4b2900 | AnchorToTerrain | VERIFIED-1:1 (zoomT bit-alias, baseH+(spanH-baseH)*zoomT height, baseAngle+(spanAngle-baseAngle)*zoomTbits worldX, guard `!dword_649D60`) |
| 0x4b2a0c | ClampToTerrainHeight | VERIFIED-1:1 (5.0 deadzone, step*0.06666667, +/-10 saturation, alt-mode `dword_62D4E8` no-commit secondary triple, LABEL_12 world-X ease +/-0.005, result local-only) |
| 0x4b300c | RotateView | VERIFIED-1:1 (both axes; 0.0025/2pi/2.5 consts; world-translation yaw + position-orbit branches) |
| 0x4b41a8 | UpdateMovement | VERIFIED-1:1 (latch / pan-init / wheel-zoom / rotate branches; consts confirmed) |
| 0x4b5250 | ZoomReset | VERIFIED-1:1 (RESOLVED) — bone-chain dolly (-600 fwd), (hi-lo)*0.5 height term, v22*v31 - v22*1.8 back-off, sqrt(...)*minZoom*0.0005 easing, max(minZoom,..); returns mesh handle, clears 62D4E4/E8. NO 3-band clamp (that is ZoomOut). |
| 0x4b5974 | ZoomOut | VERIFIED-1:1 (RESOLVED) — same dolly math + the 3-BAND dist clamp: `if (3*min < v10) dist=3*min; else if (min <= v38b) dist=v38b; else dist=min`; listener kind 104, posVec==angVec==world triple (+132/136/140). Golden-pinned band 3 (dist=50). |
| 0x4b562c | OrientToTarget | VERIFIED-1:1 (eye-offset 600/900/-1.875; yaw via forward-axis acos w/ sign flip on v19>0; pitch acos w/ sign flip on v20<0; listener kind 40) |
| 0x5e967c | UpdateTrackTargetFromMouse | VERIFIED-1:1 (RESOLVED) — basis selector switch, pan-drag(672220)/tilt-drag(672234) edge latches, the FULL axis-routing tree (649EFC==active branch; 64A024 alt branch; 671D8A/671D7D lock branches), ApplyTransformConstraints r&1/r>>1 bit split, latched-release light refresh, debug-toggle OR return. Golden-pinned two routes. |
| 0x5ACCD0 | BuildViewMatrix / BuildViewFrustum + BBox table | VERIFIED-1:1 (RESOLVED) — theta=atan2(lookZ,lookX), phi=atan2(cos,sin), 6 planes w/ exact eps signs, near(0,0,1,nearZ)/far(0,0,-1,-farZ), 64-entry popcount+bit-order table) |
| 0x5E9024 | UpdateOrbitFromMouse / ComputeOrbitRates | VERIFIED-1:1 (RESOLVED) — pan=(d/extent)*pi, accel=(|d|+1)*30/extent clamped at 3.0->3.0, yaw*256 / pitch*-256) |

## Wave-13 NEEDS-LIVE-MCP queue — all RESOLVED

1. **ZoomReset bone-chain/height/back-off/easing numerics** — confirmed against the
   0x4b5250 decompile. meshHeightRange (0x42698c) is called twice (initial + with the
   bone-result handle); PointThroughBoneChain (0x5c8b38) takes (frame, frame+19, &out).
   All four consts (-600/0.5/1.8/0.0005) match. No fix needed.
2. **ZoomOut 3-band dist clamp + listener vectors** — confirmed against 0x4b5974.
   The 3 bands (`3*minZoom` cap / raw-v38b / `minZoom` floor) match the source exactly;
   posVec and angVec are BOTH the world-translation triple `v31`. Pinned (dist=50).
3. **Track-target axis-routing tree + ApplyTransformConstraints bits** — confirmed
   against 0x5e967c. The whole decision tree (default 649EFC, alt 64A024, the
   !671D8A/671D7D nested branches and LABEL_15/LABEL_16 fall-throughs) matches the
   source statement-for-statement. The constraint result is split `v0=r&1`,
   `v34=r>>1`. Two representative routes golden-pinned.
4. **Frustum eps signs + orbit-rate globals** — confirmed byte-for-byte via get_bytes
   (see Constants above). Source eps sign pattern matches; orbit consts pi/30/3/256/-256.
   NEW camera_control_test.cpp pins the real constants through ComputeOrbitRates and the
   frustum (the wave-13-flagged missing golden file). NOTE: render_camera_test.cpp
   already exercised camera_control with SYNTHETIC config values; the new file adds the
   binary-constant goldens without touching the cross-segment render_camera_test.

## New golden pins (this wave)

`tests/unit/camera_control_test.cpp` (NEW, 26 checks):
- ViewPlaneEps == 1.9999999949504854e-06 (raw-byte exact).
- Frustum eps sign pattern (-,+,+,-) + near/far planes.
- ComputeOrbitRates with REAL consts: dx=10,dy=6,640x480 ->
  pan=0.04908739, panY=0.03926991, yaw=2.0625, pitch=-1.4 (IDA-python traced).
- Accel clamp saturation -> 3.0 (yaw=768 for dx=200@200px).
- BBox table popcount + bit-order (bit0->side0 .. bit5->far), pad==0.

`tests/unit/camera_recon2_test.cpp` (+19 checks):
- `Camera2ReconZoomOut.ThreeBandDistClampBand3` — band-3 floor: listener dist=50,
  posVec/angVec == world triple (0.1,0.2,0.3).
- `Camera2ReconTrackTarget.AxisRouteNoLockGoesToPos` — !lock route: dpos=(76.8,-21.12,0),
  dworld=0 (tilt drag dx=20,dy=10@200px).
- `Camera2ReconTrackTarget.AxisRouteLock8AGoesToDposZ` — 671D8A-lock route:
  dpos=(0,0,-21.12), dworld=0.

## Still-deferred (rule 8 — unchanged, genuine out-of-tree leaves)

- Edge-view blocks producer (+180/204/228/252) — scene/cutscene setup, not in tree.
- meshHeightRange (0x42698c) / PointThroughBoneChain (0x5c8b38) bodies remain inert
  default hooks for the unit goldens; the ZoomReset/ZoomOut numeric structure is
  confirmed 1:1 against the decompile regardless of those leaves' real output.
- ZoomReset/ZoomOut callers (RunMainFrameLoop 0x50f0c0 / building loader 0x50d01c)
  still not reconstructed -> these movers are reachable-but-not-yet-wired to the live
  session loop. Their bodies are now fully VERIFIED-1:1.
- dword_1233564 scroll_speed = [Game] INI knob (read 0x56bcd3); not a binary constant.

## Files touched this wave

- `tests/unit/camera_control_test.cpp` — NEW (binary-constant goldens for frustum + orbit).
- `tests/unit/camera_recon2_test.cpp` — +3 golden tests + 2 capture helpers (dist, constraints).
- No `src/` edits (no divergence found).
