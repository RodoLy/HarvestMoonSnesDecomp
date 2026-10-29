# The native build: the game on the decompiled C alone.
#
#   cmake -S native -B build/native -DHM_RECOMP=../HarvestMoonSNESRecomp
#   cmake --build build/native
#
# It needs the recompilation project next to this one for two things it
# does not copy: hm_ram.h (the WRAM names) and the console's picture and
# sound chips (snesrecomp/runner/src/snes: PPU, DMA, APU with its SPC700
# and DSP), compiled as they are. No generated code and no CPU emulator
# are used. SDL3 is built from source if no installed one is found.
cmake_minimum_required(VERSION 3.20)
project(HarvestMoonNative C)

set(HM_RECOMP "${CMAKE_CURRENT_SOURCE_DIR}/../../HarvestMoonSNESRecomp" CACHE PATH
    "the recompilation project (for hm_ram.h and the chips)")
get_filename_component(HM_RECOMP "${HM_RECOMP}" ABSOLUTE)
set(RUNNER "${HM_RECOMP}/snesrecomp/runner/src")
if(NOT EXISTS "${RUNNER}/snes/ppu.c")
  message(FATAL_ERROR "HM_RECOMP=${HM_RECOMP} does not look like the recompilation project")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_STANDARD 11)

find_package(SDL3 CONFIG QUIET)
if(NOT TARGET SDL3::SDL3)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
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
add_executable(HarvestMoonNative ${HOST_SOURCES} ${GAME_SOURCES})
target_include_directories(HarvestMoonNative PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src ${HM_RECOMP}/src
  ${RUNNER} ${RUNNER}/snes)
target_link_libraries(HarvestMoonNative PRIVATE hm_chips SDL3::SDL3)
if(NOT MSVC)
  target_link_libraries(HarvestMoonNative PRIVATE m)
endif()
if(WIN32)
  set_target_properties(HarvestMoonNative PROPERTIES WIN32_EXECUTABLE ON)
endif()
