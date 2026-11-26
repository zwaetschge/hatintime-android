# libmain.so: Win64 runtime + lifted HatinTimeGame.exe + Direct3D 9 bridge + DXVK, and libSDL3.so,
# for arm64-v8a. Built with tools/build_android_so.sh; the results go to app/src/main/jniLibs and
# are never committed (derived from the game).
cmake_minimum_required(VERSION 3.22)
project(hatrecomp C CXX)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(GEN_DIR "${ROOT}/gen" CACHE PATH "generated code: <GEN_DIR>/<Module>/recomp_*.c")
set(GUEST_OPT "-O2;-march=armv8.2-a" CACHE STRING "optimisation for generated code")
set(DXVK_SOURCE "${ROOT}/build/dxvk-src" CACHE PATH "DXVK prepared by gfx/dxvk/prepare.py + shaders.py")

# SDL3 (shared: SDLActivity loads libSDL3.so itself)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${ROOT}/third_party/SDL3-3.2.12 sdl3)

# DXVK native 1.9.2b (d3d9), Android WSI
add_subdirectory(${ROOT}/gfx/dxvk dxvk)

# lifted modules: each with its own dispatch/import table names (tools/build_host.sh does the same)
set(MODULES HatinTimeGame)
file(STRINGS ${ROOT}/config/o0_host.txt O0_UNITS REGEX "^[A-Za-z]")
set(GUEST_OBJS)
foreach(M ${MODULES})
  file(GLOB SRC CONFIGURE_DEPENDS ${GEN_DIR}/${M}/recomp_*.c)
  add_library(g_${M} OBJECT ${SRC})
  target_include_directories(g_${M} PRIVATE ${ROOT}/runtime/include ${GEN_DIR}/${M})
  target_compile_definitions(g_${M} PRIVATE recomp_dispatch_table=${M}_dispatch_table
      recomp_dispatch_count=${M}_dispatch_count recomp_imports=${M}_imports)
  target_compile_options(g_${M} PRIVATE ${GUEST_OPT} -w -fwrapv -fno-strict-aliasing -ffp-contract=off -g0
      -fvisibility=hidden -fno-unwind-tables -fno-asynchronous-unwind-tables)
  set_property(TARGET g_${M} PROPERTY POSITION_INDEPENDENT_CODE ON)
  list(APPEND GUEST_OBJS $<TARGET_OBJECTS:g_${M}>)
endforeach()
# single functions of 15k-30k lines: -O0 (as on the host)
foreach(u ${O0_UNITS})
  string(STRIP "${u}" u)
  set_source_files_properties(${GEN_DIR}/${u}.c PROPERTIES COMPILE_OPTIONS "-O0")
endforeach()

file(GLOB RT_SOURCES CONFIGURE_DEPENDS ${ROOT}/runtime/src/*.c)
add_library(main SHARED ${CMAKE_CURRENT_SOURCE_DIR}/android_main.c ${RT_SOURCES} ${ROOT}/runtime/include/eh64.c
    ${GEN_DIR}/modules_table.c ${ROOT}/gfx/hat_d3d9.cpp ${GUEST_OBJS})
target_include_directories(main PRIVATE ${ROOT}/runtime/include ${ROOT}/runtime/src)
# -fsigned-char: plain char is unsigned on arm64 but signed on the x86 host the runtime is developed on.
target_compile_options(main PRIVATE $<$<COMPILE_LANGUAGE:C>:-O2 -g -fwrapv -fno-strict-aliasing -ffp-contract=off -fsigned-char -Wno-unused-function>
    $<$<COMPILE_LANGUAGE:CXX>:-O2 -std=c++17 -w>)
target_link_libraries(main PRIVATE SDL3::SDL3-shared hp_dxvk vulkan log android m)
target_link_options(main PRIVATE "-Wl,-z,max-page-size=16384")
set_target_properties(main PROPERTIES LINKER_LANGUAGE CXX)
