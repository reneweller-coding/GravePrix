/**
 * @file RandomTrack.cpp
 * @brief Tracks from the dice: a closed loop of straights and corners for a venue, made from a seed (the same seed
 *        gives the same track) and checked like the hand-made ones. For the quick race's random track, the endless
 *        tournament and the daily challenge.
 * @author Rene Weller
 */
#include "Data.h"
#include "Parse.h"
#include "Track.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace grave
{

namespace
{

/** @brief A venue's lamps and road width for random tracks. */
struct VenueLook
{
    const char *venue, *lamp;
    float       width;
};

const VenueLook Looks[] = { { "junkyard", "", 15 },        { "cemetery", "lamp_old", 15 }, { "docks", "lamp_harbor", 13 },
                            { "desert", "", 13 },          { "refinery", "lamp_sodium", 14 }, { "swamp", "lantern", 13 },
                            { "downtown", "lamp_city", 16 }, { "pass", "lamp_street", 12 }, { "fairground", "lamp_street", 16 },
                            { "proving", "", 17 },         { "estate", "lamp_gothic", 17 } };

}   // namespace

std::string randomTrack( const std::string &venue, uint32_t seed )
{
    std::string name = "rnd_" + venue + "_" + std::to_string( seed );
    if ( g_data.track( name ) ) return name;
    const VenueDef *v = g_data.venue( venue );
    if ( !v ) return "";
    VenueLook look = { venue.c_str(), "", 14 };
    for ( const VenueLook &l : Looks )
        if ( venue == l.venue ) look = l;
    for ( int attempt = 0; attempt < 600; ++attempt )
    {
        Rng r;
        uint32_t place = 2166136261u;   // the venue shapes the dice too: the same number differs from venue to venue
        for ( char ch : venue ) place = ( place ^ (uint8_t)ch ) * 16777619u;
        r.seed( seed * 2654435761u + (uint32_t)attempt * 40503u + place );
        // The corners: right turns that add up to a full circle plus whatever the left ones take away.
        std::vector<float> turns, radii, straights;
        int                corners = 5 + r.below( 6 );
        float              lefts = 0, weights = 0;
        std::vector<float> w;
        for ( int i = 0; i < corners; ++i )
        {
            if ( i > 0 && r.chance( 0.22f ) )
            {
                float a = r.range( 25.0f, 80.0f );
                turns.push_back( -a );
                lefts += a;
            }
            turns.push_back( 0 );
            w.push_back( r.range( 0.5f, 1.5f ) );
            weights += w.back();
        }
        bool ok = true;
        for ( size_t i = 0, k = 0; i < turns.size(); ++i )
            if ( turns[i] == 0 )
            {
                turns[i] = ( 360.0f + lefts ) * w[k++] / weights;
                if ( turns[i] > 175.0f || turns[i] < 20.0f ) ok = false;
            }
        if ( !ok ) continue;
        for ( float t : turns )
        {
            float a = std::fabs( t );
            radii.push_back( std::round( a > 120 ? r.range( 18.0f, 30.0f ) : a > 60 ? r.range( 22.0f, 45.0f ) : r.range( 35.0f, 80.0f ) ) );
            straights.push_back( std::round( r.range( 25.0f, 150.0f ) / 5 ) * 5 );
        }
        float first = std::round( r.range( 140.0f, 220.0f ) / 10 ) * 10;
        // Where the turtle ends: the closing curve has to bridge that gap.
        float   head = 90.0f, total = first;
        Vector2 p = { first, 0 };
        for ( size_t i = 0; i < turns.size(); ++i )
        {
            float a = std::fabs( turns[i] ), sgn = turns[i] > 0 ? 1.0f : -1.0f;
            float chord = 2.0f * radii[i] * std::sin( a * Deg * 0.5f ), mid = ( head + sgn * a * 0.5f ) * Deg;
            p = add( p, { chord * std::sin( mid ), -chord * std::cos( mid ) } );
            head += sgn * a;
            total += radii[i] * a * Deg;
            if ( i + 1 < turns.size() )
            {
                float h = head * Deg;
                p = add( p, { straights[i] * std::sin( h ), -straights[i] * std::cos( h ) } );
                total += straights[i];
            }
        }
        float gap = len( p );
        if ( gap > 0.3f * total || gap < 20.0f || total < 700.0f || total > 2200.0f ) continue;
        total += gap;
        // The track as data, read like the hand-made ones.
        char buf[256];
        Block b;
        b.m_type = "track";
        b.m_name = name;
        b.m_head = parseLine( "track " + name );
        auto add = [&]( const std::string &s ) { b.m_lines.push_back( parseLine( s ) ); };
        snprintf( buf, sizeof( buf ), "title \"Zufallsstrecke %u\" title_en \"Random Track %u\"", seed % 100000, seed % 100000 );
        add( buf );
        add( "venue " + venue );
        add( "laps " + std::to_string( std::clamp( (int)std::lround( 4200.0f / total ), 3, 6 ) ) );
        snprintf( buf, sizeof( buf ), "width %.0f", look.width );
        add( buf );
        add( "shoulder 3" );
        add( "reverse 1" );
        add( "start 0 0 90" );
        snprintf( buf, sizeof( buf ), "straight %.0f", first );
        add( buf );
        for ( size_t i = 0; i < turns.size(); ++i )
        {
            snprintf( buf, sizeof( buf ), "%s %.0f r=%.0f", turns[i] > 0 ? "right" : "left", std::fabs( turns[i] ), radii[i] );
            add( buf );
            if ( i + 1 < turns.size() )
            {
                snprintf( buf, sizeof( buf ), "straight %.0f", straights[i] );
                add( buf );
            }
        }
        add( "close" );
        static const char *kinds[] = { "cash", "wrench", "ammo", "mystery", "nitro", "rocket", "cash", "mine" };
        int n = 0;
        for ( float s = 90; s < total - 60; s += r.range( 130.0f, 200.0f ), ++n )
        {
            snprintf( buf, sizeof( buf ), "pickup %s at=%.0f lat=%.1f", kinds[n % 8], s, r.range( -0.4f, 0.4f ) );
            add( buf );
        }
        for ( int k = 0; k < 3; ++k )
        {
            snprintf( buf, sizeof( buf ), "hazard barrel at=%.0f lat=%.2f", r.range( 250.0f, total - 100 ), r.chance( 0.5f ) ? 0.86f : -0.86f );
            add( buf );
        }
        if ( look.lamp[0] )
        {
            snprintf( buf, sizeof( buf ), "lamps %s every=45 side=both", look.lamp );
            add( buf );
        }
        std::vector<std::string> errors, warnings;
        TrackDef                *def = g_data.addTrack( b, errors );
        if ( !def ) continue;
        def->m_where = "random";
        Track t;
        if ( t.build( *def, v, false, false, errors, warnings ) && errors.empty() && warnings.empty() )
        {
            Track back;
            if ( back.build( *def, v, true, false, errors, warnings ) && errors.empty() && warnings.empty() ) return name;
        }
        g_data.m_tracks.erase( name );
    }
    return "";
}

void ensureTrack( const std::string &name )
{
    if ( name.rfind( "rnd_", 0 ) != 0 || g_data.track( name ) ) return;
    size_t cut = name.rfind( '_' );
    if ( cut == std::string::npos || cut <= 4 ) return;
    randomTrack( name.substr( 4, cut - 4 ), (uint32_t)std::strtoul( name.c_str() + cut + 1, nullptr, 10 ) );
}

}   // namespace grave
