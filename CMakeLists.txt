cmake_minimum_required(VERSION 3.21)
project(bobby_carrot LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CORE_SOURCES
    src/core/level.cpp
    src/core/rules.cpp
    src/core/player.cpp
    src/core/game.cpp
    src/core/generator.cpp
    src/core/sat.cpp
    src/core/hint.cpp
)

# CaDiCaL SAT solver, used by the hint (src/core/sat.cpp). Built from its
# sources without its own configure script: NBUILD makes it guess the build
# info, and the other switches drop logging, tracing and the fork-based file
# helpers that the browser has no use for. Same flags as tools/build_hint_tests.sh.
file(GLOB CADICAL_SOURCES CONFIGURE_DEPENDS
     src/third_party/cadical/*.cpp src/third_party/cadical/*.c)
add_library(cadical STATIC ${CADICAL_SOURCES})
target_compile_definitions(cadical PRIVATE
    NDEBUG NBUILD QUIET NTRACING NCONTRACTS NCLOSEFROM)
target_compile_options(cadical PRIVATE -O2 -w)
set_target_properties(cadical PROPERTIES CXX_STANDARD 17)

set(GAME_SOURCES
    ${CORE_SOURCES}
    src/gfx/texture.cpp
    src/gfx/renderer.cpp
    src/gfx/hint_overlay.cpp  # HINT OVERLAY (temporary), see src/gfx/hint_overlay.hpp
    src/audio/audio.cpp
    src/platform/storage.cpp
    src/platform/app.cpp
    src/platform/main.cpp
    src/third_party/stb_vorbis_impl.c
)

add_executable(bobby ${GAME_SOURCES})
target_link_libraries(bobby PRIVATE cadical)

# stb_vorbis is third-party C that warns a lot; keep our own warnings loud.
set_source_files_properties(src/third_party/stb_vorbis_impl.c
    PROPERTIES COMPILE_OPTIONS "-w")

if(EMSCRIPTEN)
    # SDL2 comes from the Emscripten port; PNG decoding is stb_image and audio
    # decoding is stb_vorbis, so no SDL_image or SDL_mixer port is needed.
    set(EM_COMMON_FLAGS "-sUSE_SDL=2")

    target_compile_options(bobby PRIVATE ${EM_COMMON_FLAGS})
    target_link_options(bobby PRIVATE
        ${EM_COMMON_FLAGS}
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        -sFILESYSTEM=1
        -sENVIRONMENT=web
        -sEXIT_RUNTIME=0
        --preload-file "${CMAKE_SOURCE_DIR}/assets/v1@/assets/v1"
        --shell-file "${CMAKE_SOURCE_DIR}/web/shell.html"
    )
    set_target_properties(bobby PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html"
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(bobby PRIVATE -sASSERTIONS=2 -sSAFE_HEAP=1
                                          -sSTACK_OVERFLOW_CHECK=2 -g3)
    else()
        # -O2 rather than -O3: the extra wasm-opt passes buy nothing for a game
        # this small, and older Emscripten releases mis-handle them.
        target_compile_options(bobby PRIVATE -O2)
        target_link_options(bobby PRIVATE -O2 --closure 0)
    endif()
else()
    find_package(SDL2 REQUIRED)
    if(TARGET SDL2::SDL2)
        target_link_libraries(bobby PRIVATE SDL2::SDL2 SDL2::SDL2main)
    else()
        target_include_directories(bobby PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(bobby PRIVATE ${SDL2_LIBRARIES})
    endif()
    if(UNIX)
        target_link_libraries(bobby PRIVATE m)
    endif()

    # Rule verification harness: proves every original level is still solvable
    # under the reconstructed tile semantics. Native only -- it is a test, not
    # something to ship to the browser.
    add_executable(solve_levels tests/solve_levels.cpp ${CORE_SOURCES})
    target_link_libraries(solve_levels PRIVATE cadical)

    # Hint through the real Game class: follow it to the exit on levels 1-7.
    add_executable(hint_game tests/hint_game.cpp ${CORE_SOURCES})
    target_link_libraries(hint_game PRIVATE cadical)

    enable_testing()
    add_test(NAME levels_solvable
             COMMAND solve_levels "${CMAKE_SOURCE_DIR}/assets/v1" 8000000)
    add_test(NAME hint_in_game
             COMMAND hint_game "${CMAKE_SOURCE_DIR}/assets/v1")
endif()

target_compile_options(bobby PRIVATE -Wall -Wextra -Wno-unused-parameter)
