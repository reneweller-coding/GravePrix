/**
 * @file SimTool.cpp
 * @brief --sim and --simall: computer races without a window, for the track designers and the tests: lap times,
 *        wrecks, how often cars got stuck and where.
 * @author Rene Weller
 */
#include "Sim.h"
#include "Tools.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace grave
{

namespace
{

/** @brief A field of computer drivers for a test race. */
std::vector<CarSetup> testField( int n, Rng &rng, const std::string &carName )
{
    std::vector<CarSetup> out;
    std::vector<std::string> cars;
    if ( !carName.empty() )
        cars.push_back( carName );
    else
        for ( const std::string &c : g_data.m_carOrder )
            if ( g_data.m_cars.at( c ).m_class <= 5 ) cars.push_back( c );
    for ( int k = 0; k < n; ++k )
    {
        CarSetup s;
        s.m_car = cars.empty() ? "?" : cars[(size_t)k % cars.size()];
        if ( carName.empty() && !cars.empty() ) s.m_car = cars[rng.below( (int)cars.size() )];
        s.m_driver = "AI " + std::to_string( k + 1 );
        s.m_skill = rng.range( 0.4f, 0.95f );
        s.m_aggression = rng.range( 0.2f, 0.9f );
        s.m_weapons = rng.range( 0.3f, 0.9f );
        s.m_engine = s.m_tires = s.m_armor = s.m_gunLv = 2;
        s.m_rear[(int)RearE::MINE] = 3;
        s.m_rockets = 2;
        s.m_nitro = 2;
        static const char *likes[] = { "mine", "oil", "spikes", "smoke" };
        s.m_likes = likes[k % 4];
        s.m_rear[(int)RearE::OIL] = 1;
        out.push_back( s );
    }
    return out;
}

float g_dmg[6] = {};

struct SimResult
{
    float m_time = 0, m_avg = 0, m_bestLap = 0;
    int   m_finished = 0, m_wrecked = 0, m_stuck = 0, m_knocked = 0;
    bool  m_timeout = false;
    std::map<int, int> m_stuckAt;   ///< where (100 m bins) cars backed out
};

bool   g_assist = false;   ///< --assist: every car with the driving assist
float  g_hands = 0.0f;     ///< --hands: every car driven by a person's hands (World::hands)
double g_slide = 0, g_slideN = 0;

SimResult runRace( const Track &t, int difficulty, uint32_t seed, int cars, bool verbose, const std::string &car,
                   bool noWeapons )
{
    Rng rng;
    rng.seed( seed );
    World w;
    std::vector<CarSetup> field = testField( cars, rng, car );
    if ( noWeapons )
        for ( CarSetup &s : field )
        {
            s.m_weapons = 0;
            s.m_aggression = 0;
        }
    for ( CarSetup &s : field )
    {
        s.m_assist = g_assist;
        s.m_hands = g_hands;
    }
    w.init( t, field, t.m_arena ? ModeE::DERBY : ModeE::RACE, difficulty, seed );
    std::vector<CarInput> none( field.size() );
    SimResult             r;
    std::vector<float>    lastReverse( field.size(), 0 );
    const float           limit = 60.0f * 12;
    while ( w.m_phase != World::PhaseE::DONE && w.m_time < limit )
    {
        w.step( none );
        for ( size_t i = 0; i < w.m_cars.size(); ++i )
        {
            const Car &c = w.m_cars[i];
            if ( c.racing() && len( c.m_vel ) > 8.0f )
            {
                g_slide += std::fabs( dot( c.m_vel, c.right() ) );
                g_slideN += 1;
            }
            if ( c.m_ai.m_reverse > 0.95f && lastReverse[i] <= 0.95f )
            {
                ++r.m_stuck;
                r.m_stuckAt[(int)( c.m_s / 100.0f )]++;
                if ( verbose ) printf( "  %6.1fs %s stuck at s=%.0f m\n", w.m_time, c.m_setup.m_driver.c_str(), c.m_s );
            }
            lastReverse[i] = c.m_ai.m_reverse;
        }
        for ( const Event &e : w.m_events ) r.m_knocked += e.m_type == EvE::LOOSE ? 1 : 0;
        if ( verbose )
            for ( const Event &e : w.m_events )
                if ( e.m_type == EvE::WRECK )
                    printf( "  %6.1fs %s wrecked%s%s\n", w.m_time, w.m_cars[e.m_car].m_setup.m_driver.c_str(),
                            e.m_other >= 0 ? " by " : "", e.m_other >= 0 ? w.m_cars[e.m_other].m_setup.m_driver.c_str() : "" );
    }
    for ( int k = 0; k < 6; ++k ) g_dmg[k] += w.m_dmgBy[k];
    r.m_timeout = w.m_phase != World::PhaseE::DONE;
    r.m_time = w.m_time;
    r.m_bestLap = w.m_bestLap;
    float sum = 0;
    int   n = 0;
    for ( const Car &c : w.m_cars )
    {
        if ( c.m_finished )
        {
            ++r.m_finished;
            sum += t.m_length * w.m_laps / c.m_finishTime;
            ++n;
        }
        if ( c.m_wrecked ) ++r.m_wrecked;
    }
    r.m_avg = n ? sum / n : 0;
    if ( verbose )
    {
        printf( "  place  driver   car             time    best lap  wrecks  hp\n" );
        std::vector<int> o = w.order();
        for ( size_t p = 0; p < o.size(); ++p )
        {
            const Car &c = w.m_cars[o[p]];
            printf( "  %5zu  %-8s %-14s %7.1f  %8.2f  %6d  %3.0f%s\n", p + 1, c.m_setup.m_driver.c_str(),
                    c.m_setup.m_car.c_str(), c.m_finished ? c.m_finishTime : 0.0f, c.m_bestLap, c.m_wrecks, c.m_hp,
                    c.m_wrecked ? " (wreck)" : "" );
        }
    }
    return r;
}

}   // namespace

int toolSim( const Options &opt )
{
    int         difficulty = (int)opt.num( "--difficulty", 2 );
    uint32_t    seed = (uint32_t)opt.num( "--seed", 7 );
    int         cars = (int)opt.num( "--cars", 6 );
    std::string car = opt.value( "--car" );
    bool        noWeapons = opt.has( "--peaceful" );
    g_assist = opt.has( "--assist" );
    g_hands = (float)opt.num( "--hands", 0.0f );
    std::vector<std::string> names;
    if ( opt.has( "--simall" ) )
        names = g_data.m_trackOrder;
    else
        names.push_back( opt.value( "--sim" ) );
    int bad = 0;
    for ( const std::string &name : names )
    {
        ensureTrack( name );
        const TrackDef *def = g_data.track( name );
        if ( !def )
        {
            printf( "error: no track \"%s\"\n", name.c_str() );
            ++bad;
            continue;
        }
        std::vector<std::string> errors, warnings;
        Track                    t;
        if ( !t.build( *def, g_data.venue( def->m_venue ), opt.has( "--reverse" ), false, errors, warnings ) )
        {
            for ( const std::string &e : errors ) printf( "error: %s\n", e.c_str() );
            ++bad;
            continue;
        }
        if ( opt.has( "--laps" ) ) t.m_laps = (int)opt.num( "--laps", 3 );
        bool      verbose = !opt.has( "--simall" );
        if ( verbose ) printf( "%s (%.0f m, %d laps)\n", name.c_str(), t.m_length, t.m_laps );
        SimResult r = runRace( t, difficulty, seed, cars, verbose, car, noWeapons );
        bool      fail = r.m_timeout || ( !t.m_arena && r.m_finished == 0 );
        printf( "%-22s %5.0f m  %s %6.1f s  avg %4.1f m/s  best lap %6.2f  finished %d  wrecked %d  stuck %d  knocked %d/%d", name.c_str(),
                t.m_length, t.m_arena ? "derby" : "race ", r.m_time, r.m_avg, r.m_bestLap, r.m_finished, r.m_wrecked, r.m_stuck,
                r.m_knocked, (int)t.m_loose.size() );
        if ( !r.m_stuckAt.empty() )
        {
            printf( "  (stuck near" );
            for ( auto &[bin, count] : r.m_stuckAt ) printf( " %d m x%d", bin * 100 + 50, count );
            printf( ")" );
        }
        printf( "%s\n", fail ? "  FAILED" : "" );
        if ( fail ) ++bad;
    }
    float total = 0;
    for ( float d : g_dmg ) total += d;
    if ( total > 0 )
    {
        printf( "slide %.2f m/s sideways on average while racing\n", g_slideN > 0 ? g_slide / g_slideN : 0.0 );
        printf( "damage: guns %.0f%%  explosions %.0f%%  crashes %.0f%%  walls %.0f%%  spikes %.0f%%  other %.0f%%\n", g_dmg[0] / total * 100,
                g_dmg[1] / total * 100, g_dmg[2] / total * 100, g_dmg[3] / total * 100, g_dmg[4] / total * 100, g_dmg[5] / total * 100 );
    }
    return bad ? 1 : 0;
}

}   // namespace grave
