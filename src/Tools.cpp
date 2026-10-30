/**
 * @file Tools.cpp
 * @brief The designers' and the tests' tools that need no window: --validate, --trackshot, --trackshots.
 * @author Rene Weller
 */
#include "Tools.h"

#include "Data.h"
#include "Track.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace grave
{

namespace fs = std::filesystem;

bool Options::has( const std::string &name ) const
{
    for ( const std::string &a : m_args )
        if ( a == name ) return true;
    return false;
}

std::string Options::value( const std::string &name, const std::string &def ) const
{
    for ( size_t i = 0; i + 1 < m_args.size(); ++i )
        if ( m_args[i] == name && m_args[i + 1].rfind( "--", 0 ) != 0 ) return m_args[i + 1];
    return def;
}

std::string Options::value2( const std::string &name, const std::string &def ) const
{
    for ( size_t i = 0; i + 2 < m_args.size(); ++i )
        if ( m_args[i] == name && m_args[i + 2].rfind( "--", 0 ) != 0 ) return m_args[i + 2];
    return def;
}

float Options::num( const std::string &name, float def ) const
{
    std::string v = value( name );
    return v.empty() ? def : (float)atof( v.c_str() );
}

namespace
{

void print( const std::vector<std::string> &lines, const char *what )
{
    for ( const std::string &l : lines ) printf( "%s: %s\n", what, l.c_str() );
}

/** @brief Builds a track as the race would (its venue, forwards). */
bool buildTrack( Track &t, const TrackDef &def, std::vector<std::string> &errors, std::vector<std::string> &warnings,
                 bool reverse = false, bool mirror = false )
{
    return t.build( def, g_data.venue( def.m_venue ), reverse, mirror, errors, warnings );
}

}   // namespace

int toolValidate( const Options &opt )
{
    std::vector<std::string> errors = g_data.m_errors, warnings = g_data.m_warnings;
    int                      built = 0;
    float                    total = 0;
    for ( const std::string &n : g_data.m_trackOrder )
    {
        const TrackDef &def = g_data.m_tracks.at( n );
        Track           t;
        if ( buildTrack( t, def, errors, warnings ) )
        {
            ++built;
            total += t.m_length;
            if ( def.m_reverse )
            {
                Track r;
                buildTrack( r, def, errors, warnings, true, false );
            }
        }
    }
    print( warnings, "warning" );
    print( errors, "error" );
    printf( "%zu models, %zu cars, %zu guns, %zu gear, %zu venues, %d/%zu tracks (%.1f km), %zu rivals, %zu news, %zu ads, "
            "%zu stories, %zu sponsors, %zu leagues, %zu difficulties: %zu errors, %zu warnings\n",
            g_data.m_models.size(), g_data.m_cars.size(), g_data.m_guns.size(), g_data.m_gear.size(), g_data.m_venues.size(),
            built, g_data.m_tracks.size(), total / 1000.0f, g_data.m_rivals.size(), g_data.m_news.size(), g_data.m_ads.size(),
            g_data.m_stories.size(), g_data.m_sponsors.size(), g_data.m_leagues.size(), g_data.m_difficulties.size(),
            errors.size(), warnings.size() );
    return errors.empty() ? 0 : 1;
}

int toolTrackshot( const Options &opt )
{
    std::string name = opt.value( "--trackshot" ), file = opt.value2( "--trackshot", "work/maps/" + name + ".png" );
    const TrackDef *def = g_data.track( name );
    if ( !def )
    {
        printf( "error: no track \"%s\"\n", name.c_str() );
        return 1;
    }
    std::vector<std::string> errors, warnings;
    Track                    t;
    bool                     ok = buildTrack( t, *def, errors, warnings, opt.has( "--reverse" ), opt.has( "--mirror" ) );
    print( warnings, "warning" );
    print( errors, "error" );
    if ( !ok ) return 1;
    std::error_code ec;
    fs::create_directories( fs::path( file ).parent_path(), ec );
    t.map( file, g_data );
    printf( "%s: %.0f m, %d laps, %zu samples, %d crossings, %zu pickups, %zu props -> %s\n", name.c_str(), t.m_length,
            t.m_laps, t.m_s.size(), t.m_crossings, t.m_pickups.size(), t.m_props.size(), file.c_str() );
    return errors.empty() ? 0 : 1;
}

int toolTrackshots( const Options &opt )
{
    std::string     dir = opt.value( "--trackshots", "work/maps" );
    std::error_code ec;
    fs::create_directories( dir, ec );
    int bad = 0;
    for ( const std::string &n : g_data.m_trackOrder )
    {
        std::vector<std::string> errors, warnings;
        Track                    t;
        if ( !buildTrack( t, g_data.m_tracks.at( n ), errors, warnings ) )
        {
            print( errors, "error" );
            ++bad;
            continue;
        }
        t.map( dir + "/" + n + ".png", g_data );
        printf( "%-24s %6.0f m  %zu warnings\n", n.c_str(), t.m_length, warnings.size() );
    }
    return bad ? 1 : 0;
}

}   // namespace grave
