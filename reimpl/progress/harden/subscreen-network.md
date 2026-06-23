# Sub-screen 1:1 VIEW — NETWORK (hub + Join-IP + Search-network)

The main-menu "Сетевая игра" leaf tree, rendered as its REAL view (proven
`native_main_menu.cpp` model: real `_OPTIONEN_PIC` background, native 800×600 form
windows CENTER-TRANSLATED only, `_BUTTON_RED` 3-slice buttons + real `_FONT`
Cyrillic glyphs with localized `_OPTIONEN_NETZWERK_*` captions). Asset-less
fallback kept so headless tests still pass.

## Files changed
- `src/play/menu_recon_network_screens.h` — added the VIEW API: `NetViewContext`,
  `NetViewRect`, `NetHubLayout` / `NetIpLayout` / `NetSearchLayout`,
  `RenderNetworkHubView` / `RenderNetworkIpView` / `RenderNetworkSearchView`,
  `StripNetRichMarkup`. (The pre-existing data drivers / `MenuScreenHooks` /
  `CopyFatString` contract is untouched.)
- `src/play/menu_recon_network_screens.cpp` — implemented the three renderers +
  shared helpers (background blit, `_BUTTON_RED` 3-slice, center-translate,
  rich-text strip, localized-label resolve). No change to the existing screen
  drivers' behavior/result.
- `tests/unit/menu_network_screens_view_test.cpp` — NEW; pins layout + labels.
- `progress/harden/subscreen-network.md` — this report.

## Addresses disasm'd (IDA MCP, module gilde.exe, imagebase 0x400000)
| Addr | Function | What it told us |
|------|----------|-----------------|
| 0x529a64 | `VIBE_Menu_ChooseNetworkMode` | The hub is a **3-radio column** (NOT 5 buttons): `RadioGroup_Create(3, host)`; rows are host / search / load-profile via rich-string ids **0x18B3 / 0x18B4 / 0x18B5** (title 0x18B2). Each pick: hide objects → run child (`RunHostNetworkSetup` / `SearchNetworkGames(0)` / `ChooseNetworkProfile`) → on success arm `dword_631614=1`, `v12=1`; cancel via `dword_672230 \|\| byte_67225C==1`. |
| 0x529074 | `VIBE_Menu_EnterNetworkIp` | `Menu\CHOOSENETWORK_IP`. Seeds the edit field from INI `[Network]Host` (default `127.0.0.1`). Title rich-string **6331**, OK rich-string **6333**, one edit field. On confirm copies the fat-string into `byte_122EE90`, writes the INI, arms close, returns 1. |
| 0x529248 | `VIBE_Menu_SearchNetworkGames` | `Menu\SEARCH_NETWORK`. Title rich-string **6322**. `Hud_BuildButtonRow(..., 3, ...)` → 3 buttons `v50[0..2]` = CONNECT / REFRESH / DIRECT(=Enter-IP). Discovers servers (`Net_DiscoverServers(0x3039,0xBB8)`), builds a radio list with `_STADTWAPPEN_%s` icons, row text via rich-string **6345**. `v50[2]` → `Menu_EnterNetworkIp`. |
| 0x59d6e8 | `VIBE_Text_RenderRichString` | Confirmed rich-string ids are text-definition/format-2 ids resolved through the textbin; the live captions are the named `_OPTIONEN_NETZWERK_*` array entries. |

## Label resolution (decompile = reference of record)
The rich-string ids (0x18B2.. / 6331 / 6333 / 6345) ultimately render the localized
`_OPTIONEN_NETZWERK_*` textbin entries. Resolved by NAME via `ResolveOptionLabels`
(textbin keys carry the `+N` suffix in the name itself), then `StripNetRichMarkup`
removes the `$X`/`%X` markup codes and `[..]` brackets:

| Screen | Key | Resolved (RU loc) |
|--------|-----|-------------------|
| Hub title | `_OPTIONEN_NETZWERK_MENUE+0` | Сетевая игра |
| Hub row 0 (host) | `_OPTIONEN_NETZWERK_MENUE+1` | Создать новую игру |
| Hub row 1 (search) | `_OPTIONEN_NETZWERK_MENUE+2` | Присоединиться к игре |
| Hub row 2 (profile) | `_OPTIONEN_NETZWERK_MENUE+3` | Найти игру |
| IP title | `_OPTIONEN_NETZWERK_JOINEN+0` | Сетевая игра |
| IP prompt | `_OPTIONEN_NETZWERK_JOINEN+1` | Введите IP: |
| IP OK | `_OPTIONEN_NETZWERK_JOINEN+2` | Подключиться |
| Search title | `_OPTIONEN_NETZWERK_SUCHE_SERVER+0` | Поиск серверов: |
| Search status | `_OPTIONEN_NETZWERK_WARTEN+0` | Идёт поиск |
| Buttons | `_OPTIONEN_NETZWERK_BUTTON_CONNECT/REFRESH/DIRECT/CANCEL+0` | Подключить / Обновить / Прямое подключение / Назад |

## Form geometry (forms.BIN FRM2 — native window x/y/w/h)
- CHOOSENETWORK     (144,160,297,400) flags0x110 — button hub
- CHOOSENETWORK_IP  (152,192,224,361) flags0x110 — prompt(16,56) + input(16,88) + OK
- SEARCH_NETWORK    (120,120,451,575) flags0x111 — list + CONNECT/REFRESH/DIRECT/CANCEL
All center-translated by `((fbW-800)/2,(fbH-600)/2)`; widget sizes stay native.

## Gfx (all decode OK from gfx/gilde.gfx)
- `_OPTIONEN_PIC` #1770 800×600 (bg; `_1024`/`_1152` variants by fbW)
- `_BUTTON_RED` #174 6 shapes — 3-slice {0=L,2=C,1=R} normal / {3,5,4} hover
- `_FONT` #66 — real glyph metrics + bitmaps (Cyrillic CP1251 0xC0..0xFF land on
  shape `ch` directly); lineHeight 24.

A latent bug was found + fixed while wiring this: `MenuAssets::SpriteByName` appends
to an internal cache vector that can reallocate and invalidate previously returned
pointers — `DrawButton3Slice` now copies each `_BUTTON_RED` slice by value before
blitting (it fetches L, C, R in sequence).

## Tests (tests/unit/menu_network_screens_view_test.cpp) — 9 tests, 84 checks, 0 failures
- `StripMarkupKeepsBracketedInner` — the rich-text stripper.
- `HubLayoutHeadless` / `IpLayoutHeadless` / `SearchLayoutHeadless` — center-
  translated form geometry, 3 hub buttons (matches the decompile), uniform button
  width, prompt/input at form (16,56)/(16,88), 4-button search row L→R.
- `HubCenterTranslateNotScaled` — 800×600 vs 1024×768: every rect offsets by the
  centering delta, sizes unchanged (native, never scaled).
- `HeadlessPaintsPixels` — the asset-less render still paints the layout.
- `HubLabelsResolveWithAssets` / `IpLabelsResolveWithAssets` /
  `SearchLabelsResolveWithAssets` — GUILD_GAME_DIR-guarded: real `_OPTIONEN_NETZWERK_*`
  captions resolve non-empty + rich-text-stripped, real gfx used (`usedArt`). Skip
  cleanly when the install is absent.

## Build / regression status
- `cmake --build build --target menu_network_screens_view_test -j` — clean.
- 9/9 tests pass headless AND with `GUILD_GAME_DIR=$PWD/europe_guild_1400_original`.
- No regressions: `script_recon_purchase_menu_test` (69 checks, includes my files),
  `mode_fsm_test` (66), `app_menu_loop_test` (40) all green. `libguild.a` rebuilds
  clean (no ODR clashes; helpers live in an anonymous namespace).

## Preserved contract
The existing network screen drivers (`Menu_EnterNetworkIp` etc.), `MenuScreenHooks`
dispatch, `CopyFatString`, and the `Menu_ChooseNetworkMode` routing in
`src/gui/netfile_run.cpp` are unchanged — only the VIEW + real labels/assets were
added. The asset-less path still works.

---

## UPDATE — wired into the live menu + driver (user: "do Сетевая игра 1:1")

The render functions existed but were UNREACHABLE: `NativeMenuHooks::ChooseNetworkMode`
was not overridden, so clicking "Сетевая игра" in the live menu was a dead no-op.

Re-disassembled `VIBE_Menu_ChooseNetworkMode @0x529a64`: a 3-row RADIO group
(`RadioGroup_Create(3,..)`) — title `0x18B2` + options `0x18B3/0x18B4/0x18B5` =
`_OPTIONEN_NETZWERK_MENUE+0/+1/+2/+3` ("Сетевая игра" / "Создать игру" /
"Присоединиться к игре" / "Загрузить игру"). Dispatch: host(0)→word_63C740=5 +
`RunHostNetworkSetup @0x528dac`; search(1)→5 + `SearchNetworkGames @0x529248`;
profile(2)→4 + `ChooseNetworkProfile @0x52991c`.

Implemented:
- Refactored the three Render*View functions into load-free Paint helpers + one-shot
  public wrappers, so a frame loop no longer re-reads the 59MB gfx archive / re-mounts
  the textbin every frame (it did before — fatal for a live loop).
- `RunNetworkScreen(device, plat, cfg)` driver: loads gfx/font + resolves all labels
  ONCE, then runs the hub frame loop (hover/click the 3 rows, ESC/close → back), and
  enters the LAN server-search sub-view (DIRECT → host-IP entry), all cancellable.
- Wired `NativeMenuHooks::ChooseNetworkMode` → `RunNetworkScreen`. Clicking "Сетевая
  игра" now shows the real 1:1 hub.
- Verified the hub render over real assets (GUILD_NETWORK_DUMP): `_OPTIONEN_PIC`
  background, gold banner, 3 red `_BUTTON_RED` rows with real Cyrillic labels.

RULE 6 (networking is NOT a pre-approved swap): no LAN discovery / socket connect /
hosting is performed. The search list stays empty (no fake servers); HOST and PROFILE
options are gated no-ops (not routed to a wrong screen — rule 8); JOIN opens the real
search-screen UI. Real multiplayer transport needs the user's go-ahead.

Tests: `menu_network_screens_view_test` +2 driver tests (hub ESC-back; click-join→
search), 92 checks. Suite 1552/1552 green.

---

## UPDATE — real transport on SDL_net (user: "use SDL for the network layer, 1:1 behavioral")

Rule-6 swap approved by the user: wsock32 -> **SDL_net** (SDL2_net), protocol/discovery
SEMANTICS kept byte-for-byte (the src/net reconstruction is unchanged).

Disassembled the transport entry points:
- `VIBE_Net_DiscoverServers @0x43abcc` — UDP **broadcast** discovery: `socket(AF_INET,
  SOCK_DGRAM)` + `bind(port=12345)` + non-blocking `recvfrom` for 3000ms, accept packets
  matching `buf[0]==1, buf[2]∈{1,4}, buf[4]==106`, dedup by source IP, 128-byte records
  tagged 63 (kind1) / 127 (kind4 saved-game).
- `gilde.exe` imports the full wsock32 client+broadcast set (socket/bind/connect/send/
  recv/sendto/recvfrom/select/setsockopt/inet_ntoa/...). The 1:1 protocol already lived in
  `src/net/` (discovery/transport/lobby/session) behind `net::INetDatagram` (UDP) and
  `shim::INetSocket` (TCP) — but the only backend was in-process loopback.

Implemented:
- SDL2_net installed via **vcpkg** (`sdl2[core] sdl2-net`, x64-linux static).
- `src/shim_impl/sdlnet_backend.{h,cpp}`: `SdlNetDatagram` (UDP + SO_BROADCAST via
  `SDLNet_UDP_Open`; sendTo -> 255.255.255.255:port; non-blocking `recvFrom`) and
  `SdlNetSocket` (TCP client; non-blocking `recv` via `SDLNet_CheckSockets`). Guarded by
  `GUILD_HAVE_SDL2_NET`; portable build keeps loopback.
- CMake `GUILD_NET_SDL` option: `find_package(SDL2_net CONFIG)` + explicit SDL2 link
  (vcpkg's SDL2_net target omits its SDL2 dependency). Build:
  `cmake -S . -B build-net -DGUILD_NET_SDL=ON -DCMAKE_PREFIX_PATH=<vcpkg>/installed/x64-linux`.
- Wired `RunNetworkScreen` search: "Присоединиться к игре"/REFRESH now run the real 1:1
  `net::DiscoverServers` over `SdlNetDatagram` (3000ms window) and list discovered LAN
  hosts (clickable to latch the join IP). Loopback build: discovery is a no-op (no fake
  servers), as before.

Verified (`tests/e2e/sdlnet_transport_e2e_test.cpp`, GUILD_NET_SDL build):
- a host broadcasts a kind-1 advert -> the reconstructed `DiscoverServers` over SDL_net
  **really finds + decodes it** (type 63, source IP, blob) — no environment skip.
- `SdlNetSocket` TCP round-trips to a local SDL_net listener; non-blocking recv honored.

NOT done (still gated): the in-game TCP **session** is the original's separate `server.dll`
(ws2_32 TCP game server) — a distinct binary, out of this scope. The CONNECT action and
host/profile flows need that server side; the discovery/advertise + join-target selection
are real. Portable suite 1553/1553; SDL_net transport e2e 20/20.
