cmake_minimum_required(VERSION 3.21)
project(bobby_carrot LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BOBBY_BUILD_APP "Build the SDL game executable" ON)

set(CORE_SOURCES
    src/core/level.cpp
    src/core/rules.cpp
    src/core/player.cpp
    src/core/game.cpp
    src/core/roomgen.cpp
    src/core/autotile.cpp
    src/core/wfc.cpp
    src/core/sat.cpp
    src/core/hint.cpp
    src/core/difficulty.cpp
)

# Disable CaDiCaL tracing and fork-based helpers, including in browser builds.
file(GLOB CADICAL_SOURCES CONFIGURE_DEPENDS
     src/third_party/cadical/*.cpp src/third_party/cadical/*.c)
add_library(cadical STATIC ${CADICAL_SOURCES})
target_compile_definitions(cadical PRIVATE
    NDEBUG NBUILD QUIET NTRACING NCONTRACTS NCLOSEFROM)
target_compile_options(cadical PRIVATE -O2 -w)
set_target_properties(cadical PROPERTIES CXX_STANDARD 17)

add_library(bobby_core STATIC ${CORE_SOURCES})
target_link_libraries(bobby_core PUBLIC cadical)
target_compile_options(bobby_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(EMSCRIPTEN AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(bobby_core PRIVATE -O2)
endif()

if(BOBBY_BUILD_APP)
    add_executable(bobby
        src/gfx/texture.cpp
        src/gfx/renderer.cpp
        src/gfx/hint_overlay.cpp
        src/audio/audio.cpp
        src/platform/storage.cpp
        src/platform/app.cpp
        src/platform/main.cpp
        src/third_party/stb_vorbis_impl.c
    )
    target_link_libraries(bobby PRIVATE bobby_core)
    target_compile_options(bobby PRIVATE -Wall -Wextra -Wno-unused-parameter)
    set_source_files_properties(src/third_party/stb_vorbis_impl.c
        PROPERTIES COMPILE_OPTIONS "-w")

    if(EMSCRIPTEN)
        # PNG and audio decoding use stb; SDL2 is the only required Emscripten port.
        set(EM_COMMON_FLAGS "-sUSE_SDL=2")

        target_compile_options(bobby PRIVATE ${EM_COMMON_FLAGS})
        target_link_options(bobby PRIVATE
            ${EM_COMMON_FLAGS}
            -sALLOW_MEMORY_GROWTH=1
            -sINITIAL_MEMORY=67108864
            -sFILESYSTEM=1
            -sENVIRONMENT=web
            -sEXIT_RUNTIME=0
            -lembind
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
            # Keep -O2 for compatibility with older Emscripten wasm-opt passes.
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
    endif()
endif()

if(NOT EMSCRIPTEN)
    include(CTest)
    if(BUILD_TESTING)
        foreach(test hint_game gen_bench)
            add_executable(${test} tests/${test}.cpp)
            target_link_libraries(${test} PRIVATE bobby_core)
            target_compile_options(${test} PRIVATE -Wall -Wextra -Wno-unused-parameter)
        endforeach()

        add_test(NAME hint_in_game
                 COMMAND hint_game "${CMAKE_SOURCE_DIR}/assets/v1")
        add_test(NAME generated_levels_winnable
                 COMMAND gen_bench --count 200 -q --assets "${CMAKE_SOURCE_DIR}/assets/v1")
        add_test(NAME generated_big_levels_winnable
                 COMMAND gen_bench --count 50 -q --size 32x32 --assets "${CMAKE_SOURCE_DIR}/assets/v1")
    endif()
endif()
