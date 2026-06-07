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
  message(STATUS "GUILD_BACKEND=ON -> Vulkan ${Vulkan_VERSION} + SDL2; movie=STUB(skip videos), drm=SKIP")

  # Real-backend playable/headless entry point. Lives in apps/ (NOT src/) so the
  # src/**/*.cpp glob doesn't pull its main() into libguild.
  if(EXISTS ${CMAKE_SOURCE_DIR}/apps/guild_run.cpp)
    add_executable(guild_run ${CMAKE_SOURCE_DIR}/apps/guild_run.cpp)
    target_link_libraries(guild_run PRIVATE guild)
  endif()
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
