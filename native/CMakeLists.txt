# The native build: the game on the decompiled C alone.
#
#   cmake -S native -B build/native
#   cmake --build build/native
#
# The console's picture and sound chips (snesrecomp's runner: PPU, DMA,
# APU with its SPC700 and DSP) are in native/chips, unmodified, with their
# licenses; HM_RECOMP (the recompilation project) can be given instead. No
# generated code and no CPU emulator are used. SDL3 is built from source if
# no installed one is found.
cmake_minimum_required(VERSION 3.20)
if(ANDROID)
  # SDL's Android parts (hidapi) are C++
  project(HarvestMoonNative C CXX)
else()
  project(HarvestMoonNative C)
endif()

set(HM_RECOMP "" CACHE PATH "the recompilation project (only if native/chips is missing)")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/chips/snes/ppu.c")
  set(RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/chips")
else()
  if(NOT HM_RECOMP)
    set(HM_RECOMP "${CMAKE_CURRENT_SOURCE_DIR}/../../HarvestMoonSNESRecomp")
  endif()
  get_filename_component(HM_RECOMP "${HM_RECOMP}" ABSOLUTE)
  set(RUNNER "${HM_RECOMP}/snesrecomp/runner/src")
  if(NOT EXISTS "${RUNNER}/snes/ppu.c")
    message(FATAL_ERROR "no native/chips and HM_RECOMP=${HM_RECOMP} does not look like the recompilation project")
  endif()
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_STANDARD 11)
if(ANDROID)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)     # everything ends in a shared library
endif()

if(NOT ANDROID)
  find_package(SDL3 CONFIG QUIET)
endif()
if(NOT TARGET SDL3::SDL3)
  include(FetchContent)
  if(ANDROID)
    # Android: libSDL3.so beside the game's libmain.so, as SDL's Java side
    # (SDLActivity, android/) loads them
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
  endif()
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  if(DEFINED HM_SDL3_SOURCE_DIR AND EXISTS "${HM_SDL3_SOURCE_DIR}/CMakeLists.txt")
    FetchContent_Declare(SDL3 SOURCE_DIR "${HM_SDL3_SOURCE_DIR}")
  else()
    FetchContent_Declare(SDL3
      URL "https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/SDL3-3.4.10.tar.gz"
      URL_HASH "SHA256=12b34280415ec8418c864408b93d008a20a6530687ee613d60bfbd20411f2785"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  endif()
  FetchContent_MakeAvailable(SDL3)
endif()

# the decompiled game: every src/*.c but the checker (hm_decomp.c, hm_io.c)
file(GLOB GAME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../src/*.c")
list(FILTER GAME_SOURCES EXCLUDE REGEX "/(hm_decomp|hm_io)\\.c$")

# the console's chips, from the runner
set(CHIP_SOURCES
  ${RUNNER}/snes/ppu.c ${RUNNER}/snes/ppu_legacy.c ${RUNNER}/snes/ws_shadow.c
  ${RUNNER}/snes/dma.c ${RUNNER}/snes/sdd1.c
  ${RUNNER}/snes/apu.c ${RUNNER}/snes/spc.c ${RUNNER}/snes/dsp.c
  ${RUNNER}/snes/dsp_shadow.c ${RUNNER}/snes/audio_shadow.c ${RUNNER}/snes/color_lut.c
  ${RUNNER}/audio_trace.c)
add_library(hm_chips STATIC ${CHIP_SOURCES})
target_include_directories(hm_chips PRIVATE ${RUNNER} ${RUNNER}/snes)
if(NOT MSVC)
  target_compile_options(hm_chips PRIVATE -w)
endif()

# the host: the window, the launcher, the improvements (native/*.c)
file(GLOB HOST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
if(ANDROID)
  # the game is libmain.so; SDLActivity calls its SDL_main (main.c's main)
  add_library(HarvestMoonNative SHARED ${HOST_SOURCES} ${GAME_SOURCES})
  set_target_properties(HarvestMoonNative PROPERTIES OUTPUT_NAME main)
else()
  add_executable(HarvestMoonNative ${HOST_SOURCES} ${GAME_SOURCES})
endif()
target_include_directories(HarvestMoonNative PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${RUNNER} ${RUNNER}/snes)
target_link_libraries(HarvestMoonNative PRIVATE hm_chips SDL3::SDL3)
if(NOT MSVC)
  target_link_libraries(HarvestMoonNative PRIVATE m)
endif()
if(ANDROID)
  target_link_libraries(HarvestMoonNative PRIVATE log android)
endif()
if(WIN32)
  set_target_properties(HarvestMoonNative PROPERTIES WIN32_EXECUTABLE ON)
endif()
