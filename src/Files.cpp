/**
 * @file Files.cpp
 * @brief The player's folder and writing files whole.
 * @author Rene Weller
 */
#include "Files.h"

#include <raylib.h>

#include <cstdlib>
#include <filesystem>

namespace grave
{

namespace fs = std::filesystem;

namespace
{

/** @brief Copies a file byte for byte (over what is there). */
bool copyFile( const std::string &from, const std::string &to )
{
    FILE *in = fopen( from.c_str(), "rb" );
    if ( !in ) return false;
    FILE *out = fopen( to.c_str(), "wb" );
    if ( !out )
    {
        fclose( in );
        return false;
    }
    char   buffer[1 << 15];
    size_t n = 0;
    bool   ok = true;
    while ( ok && ( n = fread( buffer, 1, sizeof( buffer ), in ) ) > 0 ) ok = fwrite( buffer, 1, n, out ) == n;
    fclose( in );
    return fclose( out ) == 0 && ok;
}

}   // namespace

std::string userDir()
{
    static std::string dir;
    if ( !dir.empty() ) return dir;
    std::string beside = GetApplicationDirectory();
    // Tests keep their saves and settings apart.
    if ( const char *home = getenv( "GRAVE_HOME" ) )
        if ( *home )
        {
            std::error_code e;
            dir = std::string( home ) + "/";
            fs::create_directories( fs::path( dir ), e );
            return dir;
        }
#ifdef _WIN32
    const char *app = getenv( "APPDATA" );
    std::string root = app && *app ? app : beside;
#else
    const char *xdg = getenv( "XDG_DATA_HOME" ), *home = getenv( "HOME" );
    std::string root = xdg && *xdg ? xdg : ( home && *home ? std::string( home ) + "/.local/share" : beside );
#endif
    std::error_code e;
    dir = root + "/GravePrix/";
    fs::create_directories( fs::path( dir ), e );
    if ( e ) return dir = beside;
    return dir;
}

std::string photoDir()
{
    std::string     dir = userDir() + "shots/";
    std::error_code e;
    fs::create_directories( fs::path( dir ), e );
    return dir;
}

FILE *beginWrite( const std::string &path )
{
    return fopen( ( path + ".tmp" ).c_str(), "w" );
}

bool endWrite( FILE *f, const std::string &path )
{
    if ( !f ) return false;
    bool ok = !ferror( f ) && fflush( f ) == 0;
    ok = fclose( f ) == 0 && ok;
    std::error_code e;
    fs::path        file = fs::path( path ), tmp = fs::path( path + ".tmp" );
    if ( !ok )
    {
        fs::remove( tmp, e );
        return false;
    }
    if ( fs::exists( file, e ) ) copyFile( path, path + ".bak" );
    fs::rename( tmp, file, e );
    return !e;
}

}   // namespace grave
