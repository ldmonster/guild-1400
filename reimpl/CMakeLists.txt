cmake_minimum_required(VERSION 3.16)
project(guild_reimpl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
enable_testing()

include_directories(
  ${CMAKE_SOURCE_DIR}          # repo root, so "shim/IPlatform.h" etc. resolve
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/shim
)

# ---- core reconstructed library (all of src/**) ----
file(GLOB_RECURSE GUILD_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_library(guild STATIC ${GUILD_SOURCES})

# ---------------------------------------------------------------------------
# Real host backends (opt-in): Vulkan graphics + SDL2 audio/window/input.
#
#   cmake --preset vulkan-sdl          # deps via vcpkg manifest (vcpkg.json)
#   cmake --preset vulkan-sdl-system   # deps via system find_package
#
# OFF by default so the portable headless build + full test suite need no
# third-party libraries. When ON, the backend .cpp files (guarded by
# GUILD_HAVE_VULKAN / GUILD_HAVE_SDL2) light up and the macros + link libs
# propagate to every test target through the static `guild` lib (PUBLIC).
#
# Movie playback (moveahead.dll) is stubbed -> all videos skipped.
# Copy-protection (drm/disc) is skipped (intentional stub; IDB already decrypted).
# ---------------------------------------------------------------------------
option(GUILD_BACKEND "Build the Vulkan + SDL2 real host backends" OFF)
if(GUILD_BACKEND)
  find_package(Vulkan REQUIRED)
  find_package(SDL2 CONFIG QUIET)
  if(NOT SDL2_FOUND)
    find_package(SDL2 REQUIRED)   # system libsdl2-dev path
  endif()
  target_compile_definitions(guild PUBLIC GUILD_HAVE_VULKAN GUILD_HAVE_SDL2)
  target_link_libraries(guild PUBLIC Vulkan::Vulkan SDL2::SDL2)

  # Video codec (rule-6, user-approved): pl_mpeg (MIT, vendored single-header) backs
  # the IVideo shim, replacing the proprietary moveahead.dll MPEG player. Only the
  # GUILD_BACKEND build sees the header + macro; the portable build's CreateVideoDecoder
  # returns nullptr (no third-party dep).
  target_include_directories(guild PUBLIC ${CMAKE_SOURCE_DIR}/third_party)
  target_compile_definitions(guild PUBLIC GUILD_HAVE_PLMPEG)
  message(STATUS "GUILD_BACKEND=ON -> Vulkan ${Vulkan_VERSION} + SDL2; movie=pl_mpeg(MPEG-1), drm=1:1")

  # 3D scene GPU pipeline shaders: compile GLSL -> SPIR-V with glslc at build time
  # and embed each as a C uint32 array header (glslc -mfmt=c), so the Vulkan backend
  # has the SPIR-V with no runtime file lookup and the repo carries readable GLSL.
  find_program(GUILD_GLSLC NAMES glslc HINTS ${Vulkan_GLSLC_EXECUTABLE} ENV VULKAN_SDK
               PATH_SUFFIXES bin)
  if(NOT GUILD_GLSLC AND TARGET Vulkan::glslc)
    set(GUILD_GLSLC $<TARGET_FILE:Vulkan::glslc>)
  endif()
  if(GUILD_GLSLC)
    set(GUILD_SHADER_GEN_DIR ${CMAKE_BINARY_DIR}/generated/shaders)
    file(MAKE_DIRECTORY ${GUILD_SHADER_GEN_DIR})
    set(GUILD_SHADER_HEADERS "")
    foreach(stage vert frag)
      set(_src ${CMAKE_SOURCE_DIR}/src/shim_impl/shaders/scene.${stage})
      set(_out ${GUILD_SHADER_GEN_DIR}/scene_${stage}_spv.inc)
      add_custom_command(
        OUTPUT ${_out}
        COMMAND ${GUILD_GLSLC} -O -mfmt=c ${_src} -o ${_out}
        DEPENDS ${_src}
        COMMENT "glslc scene.${stage} -> SPIR-V (embedded)"
        VERBATIM)
      list(APPEND GUILD_SHADER_HEADERS ${_out})
    endforeach()
    add_custom_target(guild_scene_shaders DEPENDS ${GUILD_SHADER_HEADERS})
    add_dependencies(guild guild_scene_shaders)
    target_include_directories(guild PUBLIC ${CMAKE_BINARY_DIR}/generated)
    target_compile_definitions(guild PUBLIC GUILD_HAVE_SCENE_SHADERS)
    message(STATUS "  + 3D scene shaders via glslc (${GUILD_GLSLC})")
  else()
    message(STATUS "  - glslc not found -> 3D scene GPU pipeline disabled (CPU raster only)")
  endif()

  # Optional MP3 music decode (the menu/credits Miles tracks) via libmpg123. We
  # link the runtime soname directly so no -dev package is required; if it's
  # absent at build time the menu just runs silent (DecodeMp3File stubs to false).
  find_library(MPG123_LIB NAMES mpg123 libmpg123.so.0)
  if(NOT MPG123_LIB AND EXISTS /lib/x86_64-linux-gnu/libmpg123.so.0)
    set(MPG123_LIB -l:libmpg123.so.0)
  endif()
  if(MPG123_LIB)
    target_compile_definitions(guild PUBLIC GUILD_HAVE_MP3)
    target_link_libraries(guild PUBLIC ${MPG123_LIB})
    message(STATUS "  + MP3 music via libmpg123 (${MPG123_LIB})")
  else()
    message(STATUS "  - libmpg123 not found -> menu music disabled")
  endif()

  # Real-backend playable/headless entry point. Lives in apps/ (NOT src/) so the
  # src/**/*.cpp glob doesn't pull its main() into libguild.
  if(EXISTS ${CMAKE_SOURCE_DIR}/apps/guild_run.cpp)
    add_executable(guild_run ${CMAKE_SOURCE_DIR}/apps/guild_run.cpp)
    if(WIN32)
      # MinGW/PE: weak default symbols (the test-overridable gui edges) in a static
      # archive are not pulled the way they are on ELF, so whole-archive libguild to
      # bring them in. SDL also redefines main() on Windows, so link SDL2main for the
      # WinMain entry glue. (Linux keeps the plain link.)
      target_link_libraries(guild_run PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,guild>")
      if(TARGET SDL2::SDL2main)
        target_link_libraries(guild_run PRIVATE SDL2::SDL2main)
      endif()
    else()
      target_link_libraries(guild_run PRIVATE guild)
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------
# Optional REAL network transport on SDL_net (Rule 6 — user-approved swap of
# wsock32 -> SDL_net). The 1:1-reconstructed protocol/discovery logic in src/net/
# is unchanged; this lights up the SDL_net socket backend
# (src/shim_impl/sdlnet_backend.cpp: SdlNetDatagram UDP/broadcast + SdlNetSocket
# TCP). The portable/headless build keeps the in-process loopback backend.
# Supply SDL2_net via vcpkg, e.g.:
#   cmake -S . -B build-net -DGUILD_NET_SDL=ON \
#         -DCMAKE_PREFIX_PATH=<vcpkg>/installed/x64-linux
# ---------------------------------------------------------------------------
option(GUILD_NET_SDL "Build the SDL_net real network transport backend" OFF)
if(GUILD_NET_SDL)
  find_package(SDL2_net CONFIG REQUIRED)
  # The vcpkg SDL2_net target does NOT declare its SDL2 dependency, so link SDL2
  # explicitly AFTER SDL2_net (static link order: the consumer of SDL_* symbols
  # must precede the provider).
  find_package(SDL2 CONFIG REQUIRED)
  target_compile_definitions(guild PUBLIC GUILD_HAVE_SDL2_NET)
  if(TARGET SDL2_net::SDL2_net)
    target_link_libraries(guild PUBLIC SDL2_net::SDL2_net)
  else()
    target_link_libraries(guild PUBLIC SDL2_net::SDL2_net-static)
  endif()
  if(TARGET SDL2::SDL2)
    target_link_libraries(guild PUBLIC SDL2::SDL2)
  else()
    target_link_libraries(guild PUBLIC SDL2::SDL2-static)
  endif()
  message(STATUS "GUILD_NET_SDL=ON -> SDL_net transport backend (wsock32 swap)")
endif()

# ---- 1:1 differential-test harness (reconstruction side of tools/diff_test.py) ----
# Lives in apps/ so the src/**/*.cpp glob doesn't pull its main() into libguild.
# The Unicorn driver tools/diff_test.py runs the ORIGINAL machine code against
# this binary; the diff_harness_smoke unit test also exec's it to lock the output
# format into the CMake suite (see tests/unit/diff_harness_smoke_test.cpp).
if(EXISTS ${CMAKE_SOURCE_DIR}/apps/diff_harness.cpp)
  add_executable(diff_harness ${CMAKE_SOURCE_DIR}/apps/diff_harness.cpp)
  target_link_libraries(diff_harness PRIVATE guild)
  target_include_directories(diff_harness PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
endif()

# ---- minimal test framework ----
add_library(test_framework STATIC tests/framework/test_main.cpp)
target_include_directories(test_framework PUBLIC ${CMAKE_SOURCE_DIR}/tests/framework)

# ---- one executable per test file in tests/unit, tests/integration and tests/e2e ----
file(GLOB UNIT_TESTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/unit/*.cpp)
file(GLOB INTEGRATION_TESTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/integration/*.cpp)
file(GLOB E2E_TESTS  CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/e2e/*.cpp)
foreach(test_src ${UNIT_TESTS} ${INTEGRATION_TESTS} ${E2E_TESTS})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE guild test_framework)
  target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/tests/framework)
  add_test(NAME ${test_name} COMMAND ${test_name})
  # The diff-harness smoke test exec's the diff_harness binary; give it the path
  # and a build-order dependency so it runs against a fresh harness.
  if(${test_name} STREQUAL "diff_harness_smoke_test" AND TARGET diff_harness)
    target_compile_definitions(${test_name} PRIVATE
      DIFF_HARNESS_PATH="$<TARGET_FILE:diff_harness>")
    add_dependencies(${test_name} diff_harness)
  endif()
endforeach()
