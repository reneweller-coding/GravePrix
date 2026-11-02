# GRAVE PRIX -- a top-down combat racer with a career (five leagues, a funeral tycoon as the final boss), a garage
# and a black market between the races, five difficulties, quick race, derby and time trial.
# C++20 on raylib, like Zenith Lancer and Kip Kepler.
cmake_minimum_required( VERSION 3.22 )

# raylib 5.5 and its GLFW still ask for old CMake versions; CMake 4 refuses those without this floor.
set( CMAKE_POLICY_VERSION_MINIMUM 3.5 )
set( CMAKE_POLICY_DEFAULT_CMP0091 NEW )
set( CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )

project( GravePrix VERSION 1.0.1 LANGUAGES C CXX )

set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

set( GRAVE_BIN_DIR "" CACHE PATH "where the game is copied after each build (bin/<preset>)" )

include( FetchContent )
set( GRAVE_EXTRACT_TIMESTAMP "" )
if( CMAKE_VERSION VERSION_GREATER_EQUAL 3.24 )
    set( GRAVE_EXTRACT_TIMESTAMP DOWNLOAD_EXTRACT_TIMESTAMP TRUE )
endif()
set( BUILD_EXAMPLES OFF CACHE BOOL "" FORCE )
set( BUILD_GAMES OFF CACHE BOOL "" FORCE )
set( BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE )
# raylib 5.5's sources travel with the game in third_party/raylib, so a build needs no network; without that folder
# the tag is fetched.
if( EXISTS ${CMAKE_SOURCE_DIR}/third_party/raylib/CMakeLists.txt )
    set( FETCHCONTENT_SOURCE_DIR_RAYLIB ${CMAKE_SOURCE_DIR}/third_party/raylib )
endif()
FetchContent_Declare( raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    ${GRAVE_EXTRACT_TIMESTAMP} )
FetchContent_MakeAvailable( raylib )

function( grave_options target )
    if( MSVC )
        target_compile_options( ${target} PRIVATE /W4 /permissive- /utf-8 /wd4244 /wd4267 /wd4305 /wd4100 /MP )
        target_compile_definitions( ${target} PRIVATE _CRT_SECURE_NO_WARNINGS )
    else()
        # -fwrapv: an int that overflows wraps round as with MSVC, so a seed plays the same everywhere.
        target_compile_options( ${target} PRIVATE -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter
                                                  -fwrapv )
        if( NOT APPLE )
            # The C++ runtime inside the program: it runs on any glibc 2.35 or newer (Apple's libc++ is always there).
            target_link_options( ${target} PRIVATE -static-libstdc++ -static-libgcc )
        endif()
    endif()
endfunction()

file( GLOB GRAVE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.h )
add_executable( GravePrix WIN32 ${GRAVE_SOURCES} )
target_link_libraries( GravePrix PRIVATE raylib )
# The data (cars, tracks, rivals, texts) and the assets are read from beside the program, else from the source
# tree; --data DIR points elsewhere.
target_compile_definitions( GravePrix PRIVATE GRAVE_ROOT="${CMAKE_SOURCE_DIR}" GRAVE_VERSION="${PROJECT_VERSION}" )
grave_options( GravePrix )
if( MSVC )
    if( EXISTS ${CMAKE_SOURCE_DIR}/res/graveprix.rc )
        target_sources( GravePrix PRIVATE ${CMAKE_SOURCE_DIR}/res/graveprix.rc )
    endif()
    # A window program without a console that still starts in main().
    target_link_options( GravePrix PRIVATE /ENTRY:mainCRTStartup )
endif()

# GraveSfx: writes every synthesised sound effect and engine loop as WAV (to listen to them, to level them).
if( EXISTS ${CMAKE_SOURCE_DIR}/tools/sfx/SfxDump.cpp AND EXISTS ${CMAKE_SOURCE_DIR}/src/Audio.cpp )
    add_executable( GraveSfx ${CMAKE_SOURCE_DIR}/tools/sfx/SfxDump.cpp ${CMAKE_SOURCE_DIR}/src/Audio.cpp
                             ${CMAKE_SOURCE_DIR}/src/Audio.h )
    target_include_directories( GraveSfx PRIVATE ${CMAKE_SOURCE_DIR}/src )
    target_link_libraries( GraveSfx PRIVATE raylib )
    grave_options( GraveSfx )
endif()

if( GRAVE_BIN_DIR )
    add_custom_command( TARGET GravePrix POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GRAVE_BIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:GravePrix> "${GRAVE_BIN_DIR}" )
endif()

enable_testing()
# Every data file parses without an error and every track is sound (closed, wide enough, no overlaps).
add_test( NAME data COMMAND GravePrix --validate )
# Every track can be raced to the end by the computer drivers.
add_test( NAME races COMMAND GravePrix --simall --difficulty 2 )
# A career played through by the computer (the economy and the leagues hold together).
add_test( NAME career COMMAND GravePrix --careersim --difficulty 2 )
# The screens played through by scripts (a hidden window; the saves go to a test folder in the build).
foreach( flow career quick extras )
    add_test( NAME app_${flow} COMMAND GravePrix --apptest ${CMAKE_SOURCE_DIR}/tools/apptest_${flow}.txt )
    set_tests_properties( app_${flow} PROPERTIES ENVIRONMENT "GRAVE_HOME=${CMAKE_BINARY_DIR}/testhome_${flow}" )
endforeach()
