/**
 * @file main.cpp
 * @brief The start: the command line (the game, or one of the tools: --validate, --trackshot, --sim ...).
 * @author Rene Weller
 */
#include "Data.h"
#include "Lang.h"
#include "Tools.h"
#include "Track.h"

#include <raylib.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace grave;

namespace
{

/** @return the folder holding data/ and assets/: beside the program, else the source tree */
std::string findRoot( const char *argv0 )
{
    std::error_code ec;
    fs::path        exe = fs::absolute( argv0, ec ).parent_path();
    for ( fs::path p = exe; !p.empty(); p = p.parent_path() )
    {
        if ( fs::exists( p / "data" / "venues.txt", ec ) || fs::exists( p / "data" / "cars", ec ) ) return p.string();
        if ( p == p.parent_path() ) break;
    }
    return GRAVE_ROOT;
}

}   // namespace

int main( int argc, char **argv )
{
    std::vector<std::string> args( argv + 1, argv + argc );
    std::string              root = findRoot( argv[0] );
    std::string              dataDir = root + "/data";
    for ( size_t i = 0; i + 1 < args.size(); ++i )
        if ( args[i] == "--data" ) dataDir = args[i + 1];
    Options opt;
    opt.m_root = root;
    opt.m_data = dataDir;
    opt.m_args = args;
    SetTraceLogLevel( LOG_WARNING );
    for ( size_t i = 0; i < args.size(); ++i )
    {
        const std::string &a = args[i];
        if ( a == "--lang" && i + 1 < args.size() ) setLanguage( args[i + 1] );
    }
    if ( !opt.has( "--lang" ) ) setLanguage( "de" );
    loadUiTexts( dataDir );
    g_data.load( dataDir );
    g_data.check();
    if ( opt.has( "--validate" ) ) return toolValidate( opt );
    if ( opt.has( "--trackshot" ) ) return toolTrackshot( opt );
    if ( opt.has( "--trackshots" ) ) return toolTrackshots( opt );
    if ( opt.has( "--sim" ) || opt.has( "--simall" ) ) return toolSim( opt );
    if ( opt.has( "--careersim" ) ) return toolCareerSim( opt );
    if ( opt.has( "--gallery" ) ) return toolGallery( opt );
    if ( opt.has( "--shot" ) ) return toolShot( opt );
    if ( opt.has( "--deckcheck" ) ) return toolDeckCheck( opt );
    return runGame( opt );
}
