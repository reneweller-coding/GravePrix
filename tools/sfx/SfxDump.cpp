/**
 * @file SfxDump.cpp
 * @brief GraveSfx <dir>: writes every synthesised effect, loop and engine voice of the game as WAV (44.1 kHz,
 *        16 bit), to listen to them and to level them (tools/measure_sfx.py). Needs no audio device.
 * @author Rene Weller
 */
#include "Audio.h"

#include <chrono>
#include <cstdio>
#include <string>

/**
 * @brief The program.
 * @param[in] argc  the number of arguments
 * @param[in] argv  the folder to write into (default work/sfx)
 * @return 0
 */
int main( int argc, char **argv )
{
    std::string dir = argc > 1 ? argv[1] : "work/sfx";
    SetTraceLogLevel( LOG_WARNING );
    auto t0 = std::chrono::steady_clock::now();
    grave::Audio::dump( dir );
    double s = std::chrono::duration<double>( std::chrono::steady_clock::now() - t0 ).count();
    std::printf( "GraveSfx: wrote the effects, loops and engines to %s in %.1f s\n", dir.c_str(), s );
    return 0;
}
