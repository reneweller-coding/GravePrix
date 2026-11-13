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
    int   m_finished = 0, m_wrecked = 0, m_stuck = 0, m_knocked = 0, m_walls = 0;
    bool  m_timeout = false;
    std::map<int, int> m_stuckAt;   ///< where (100 m bins) cars backed out
};

int    g_assist = 0;       ///< --assist [LEVEL]: every car with the driving assist (1 when no level is given)
float  g_hands = 0.0f;     ///< --hands: every car driven by a person's hands (World::hands)
float  g_novice = 0.0f;    ///< --novice: every car with a beginner's misjudged corners
bool   g_flatOut = false;  ///< --flatout: every driver keeps the throttle down in the bends (the brake help's test)
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
        s.m_novice = g_novice;
        s.m_flatOut = g_flatOut;
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
                if ( verbose )
                {
                    TrackPos tp = t.locate( c.m_pos, c.m_hint );
                    printf( "  %6.1fs %s stuck at s=%.0f m, %.1f m right of the centre (line %.1f, lane %.1f), facing %.0f deg off\n",
                            w.m_time, c.m_setup.m_driver.c_str(), c.m_s, tp.m_lat, t.m_s[tp.m_i].m_line, c.m_ai.m_lane,
                            std::fabs( wrapRad( c.m_head - headingOf( t.m_s[tp.m_i].m_dir ) ) ) / Deg );
                }
            }
            lastReverse[i] = c.m_ai.m_reverse;
        }
        for ( const Event &e : w.m_events )
        {
            r.m_knocked += e.m_type == EvE::LOOSE ? 1 : 0;
            r.m_walls += e.m_type == EvE::WALL ? 1 : 0;   // hits hard enough to hurt (above 7 m/s)
        }
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
    g_assist = opt.has( "--assist" ) ? std::clamp( (int)opt.num( "--assist", 1.0f ), 0, 2 ) : 0;
    g_hands = (float)opt.num( "--hands", 0.0f );
    g_novice = (float)opt.num( "--novice", 0.0f );
    g_flatOut = opt.has( "--flatout" );
    std::vector<std::string> names;
    if ( opt.has( "--simall" ) )
        names = g_data.m_trackOrder;
    else
        names.push_back( opt.value( "--sim" ) );
    int    bad = 0, stuckAll = 0, wallsAll = 0, races = 0;
    double paceAll = 0;
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
        printf( "%-22s %5.0f m  %s %6.1f s  avg %4.1f m/s  best lap %6.2f  finished %d  wrecked %d  stuck %d  walls %d  knocked %d/%d",
                name.c_str(), t.m_length, t.m_arena ? "derby" : "race ", r.m_time, r.m_avg, r.m_bestLap, r.m_finished, r.m_wrecked,
                r.m_stuck, r.m_walls, r.m_knocked, (int)t.m_loose.size() );
        stuckAll += r.m_stuck;
        wallsAll += r.m_walls;
        if ( !t.m_arena )
        {
            paceAll += r.m_avg;
            ++races;
        }
        if ( !r.m_stuckAt.empty() )
        {
            printf( "  (stuck near" );
            for ( auto &[bin, count] : r.m_stuckAt ) printf( " %d m x%d", bin * 100 + 50, count );
            printf( ")" );
        }
        printf( "%s\n", fail ? "  FAILED" : "" );
        if ( fail ) ++bad;
    }
    if ( names.size() > 1 )
        printf( "all: %d races at %.2f m/s on average, stuck %d, walls %d\n", races, races ? paceAll / races : 0.0, stuckAll,
                wallsAll );
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

int toolHandling( const Options &opt )
{
    // The car alone on open asphalt: how fast it backs up, and what a full turn costs at each speed (the width of a
    // U-turn and the speed left after it), with and without the assist, full throttle and coasting.
    std::string car = opt.value( "--car" );
    if ( car.empty() ) car = "rustbucket";
    int lv = (int)opt.num( "--level", 0 );
    if ( opt.has( "--tracks" ) )
    {
        // What the tracks ask of that car: the bends of the racing line and the speed its tyres carry there (with the
        // assist) against its top speed.
        const CarDef *cd = g_data.car( car );
        if ( !cd ) return 1;
        CarSetup s;
        s.m_car = car;
        s.m_engine = s.m_tires = lv;
        s.m_assist = 1;
        CarStats st = carStats( s );
        Car      probe;
        probe.m_setup = s;
        probe.m_grip = st.m_grip;
        float    side = sideGrip( probe );
        double   all = 0, slow90 = 0, slow70 = 0, slow50 = 0, widthSum = 0;
        std::vector<float> radii, slowest;
        for ( const std::string &name : g_data.m_trackOrder )
        {
            ensureTrack( name );
            const TrackDef *td = g_data.track( name );
            std::vector<std::string> e, w;
            Track                    tr;
            if ( !td || !tr.build( *td, g_data.venue( td->m_venue ), false, false, e, w ) || tr.m_arena ) continue;
            float least = 1e9f;
            for ( const TrackSample &ts : tr.m_s )
            {
                float R = 1.0f / std::max( ts.m_lineCurv, 1e-4f );
                float v = std::sqrt( side * R );
                all += 1;
                widthSum += ts.m_half * 2;
                if ( v < st.m_top * 0.9f ) slow90 += 1;
                if ( v < st.m_top * 0.7f ) slow70 += 1;
                if ( v < st.m_top * 0.5f ) slow50 += 1;
                if ( R < 200 ) radii.push_back( R );
                least = std::min( least, v );
            }
            slowest.push_back( least / st.m_top );
            printf( "%-22s slowest bend %5.1f m/s (%3.0f %% of top)\n", name.c_str(), least, least / st.m_top * 100 );
        }
        std::sort( radii.begin(), radii.end() );
        std::sort( slowest.begin(), slowest.end() );
        auto q = [&]( float f ) { return radii.empty() ? 0.0f : radii[(size_t)( f * ( radii.size() - 1 ) )]; };
        printf( "%s level %d: top %.1f m/s, grip %.2f, side grip %.1f m/s2\n", car.c_str(), lv, st.m_top, st.m_grip, side );
        if ( !slowest.empty() )
            printf( "slowest bend of a track: %.0f .. %.0f %% of top, median %.0f %%\n", slowest.front() * 100, slowest.back() * 100,
                    slowest[slowest.size() / 2] * 100 );
        printf( "road %.1f m wide on average; of the racing line's length the tyres carry less than 90 %% of top on %.0f %%, "
                "less than 70 %% on %.0f %%, less than 50 %% on %.0f %%\n",
                widthSum / all, slow90 / all * 100, slow70 / all * 100, slow50 / all * 100 );
        printf( "bends (radius < 200 m): radius 10 %% %.0f m, 25 %% %.0f m, median %.0f m, 75 %% %.0f m\n", q( 0.1f ), q( 0.25f ),
                q( 0.5f ), q( 0.75f ) );
        return 0;
    }
    ensureTrack( "test_undertaker" );
    const TrackDef *def = g_data.track( "test_undertaker" );
    if ( !def ) return 1;
    std::vector<std::string> errors, warnings;
    Track                    t;
    if ( !t.build( *def, g_data.venue( def->m_venue ), false, false, errors, warnings ) ) return 1;
    for ( int assist = 1; assist >= 0; --assist )
    {
        CarSetup s;
        s.m_car = car;
        s.m_human = s.m_player = true;
        s.m_assist = assist;
        s.m_engine = s.m_tires = lv;
        World w;
        w.init( t, { s }, ModeE::TRIAL, 2, 1 );
        w.m_phase = World::PhaseE::RACE;
        auto reset = [&]( float v )
        {
            Car &c = w.m_cars[0];
            c.m_pos = { 0, 0 };
            c.m_head = 0;
            c.m_yaw = 0;
            c.m_steerVis = c.m_lock = 0;
            c.m_vel = mul( c.fwd(), v );
            c.m_surface = SurfaceE::ASPHALT;
        };
        // How far the path has turned (radians, clockwise) and the slide (m/s sideways).
        auto pathDir = [&] { return headingOf( w.m_cars[0].m_vel ); };
        auto slide = [&] { return std::fabs( dot( w.m_cars[0].m_vel, w.m_cars[0].right() ) ); };
        printf( "%s, upgrades %d, assist %s\n", car.c_str(), lv, assist ? "on" : "off" );
        reset( 0 );
        CarInput back;
        back.m_brake = 1;
        printf( "  reverse (brake held from a stop):" );
        for ( int k = 1; k <= 180; ++k )
        {
            w.drive( 0, back );
            if ( k % 60 == 0 ) printf( "  %d s %.1f m/s", k / 60, w.m_cars[0].speed() );
        }
        printf( "\n  speed  throttle  90 deg after  U-turn: time  width  speed after   full lock 5 s: radius  speed\n" );
        for ( float v : { 6.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f } )
            for ( int thr = 1; thr >= 0; --thr )
            {
                reset( v );
                CarInput in;
                in.m_steer = 1;
                in.m_throttle = (float)thr;
                float turned = 0, t90 = 0, t180 = 0, width = 0, after = 0, dirAt = 0, radius = 0, circleV = 0;
                for ( int k = 1; k <= 60 * 5; ++k )
                {
                    float before = w.m_cars[0].m_head;
                    w.drive( 0, in );
                    turned += wrapRad( w.m_cars[0].m_head - before );
                    if ( t90 == 0 && turned >= Pi / 2 ) t90 = k * Dt;
                    if ( t180 == 0 && turned >= Pi )
                    {
                        t180 = k * Dt;
                        width = std::fabs( w.m_cars[0].m_pos.x );
                        after = len( w.m_cars[0].m_vel );
                    }
                    // The circle it settles on: the path's turn over the last half second.
                    if ( k == 60 * 5 - 30 ) dirAt = pathDir();
                    if ( k == 60 * 5 )
                    {
                        circleV = len( w.m_cars[0].m_vel );
                        float rate = std::fabs( wrapRad( pathDir() - dirAt ) ) / 0.5f;
                        radius = rate > 1e-3f ? circleV / rate : 999.0f;
                    }
                }
                printf( "  %5.0f  %8s  %10.2f s  %10.2f s  %5.1f m  %5.1f m/s  %19.1f m  %5.1f m/s\n", v, thr ? "full" : "off", t90,
                        t180, width, after, radius, circleV );
            }
        // A slide settles: 1.5 s of full lock at full throttle from 25 m/s, then straight on.
        {
            reset( 25 );
            CarInput in;
            in.m_throttle = 1;
            in.m_steer = 1;
            for ( int k = 0; k < 90; ++k ) w.drive( 0, in );
            float at = slide(), settle = 0;
            in.m_steer = 0;
            for ( int k = 1; k <= 60 * 4 && settle == 0; ++k )
            {
                w.drive( 0, in );
                if ( slide() < 1.0f ) settle = k * Dt;
            }
            printf( "  settle: sliding %.1f m/s after 1.5 s of full lock from 25 m/s; below 1 m/s %.2f s after letting go\n", at,
                    settle );
        }
        // The handbrake: half a second of it with full lock from 20 m/s, then the throttle.
        {
            reset( 20 );
            CarInput in;
            in.m_steer = 1;
            in.m_handbrake = true;
            for ( int k = 0; k < 30; ++k ) w.drive( 0, in );
            printf( "  handbrake: half a second with full lock from 20 m/s turns the car %.0f deg, the path %.0f deg, %.1f m/s left\n",
                    w.m_cars[0].m_head / Deg, pathDir() / Deg, len( w.m_cars[0].m_vel ) );
        }
        // Ice: full lock at full throttle from 20 m/s for two seconds.
        {
            reset( 20 );
            CarInput in;
            in.m_steer = 1;
            in.m_throttle = 1;
            for ( int k = 0; k < 120; ++k )
            {
                w.m_cars[0].m_surface = SurfaceE::ICE;
                w.drive( 0, in );
            }
            printf( "  ice: two seconds of full lock from 20 m/s turn the car %.0f deg, the path %.0f deg, %.1f m/s left\n",
                    w.m_cars[0].m_head / Deg, pathDir() / Deg, len( w.m_cars[0].m_vel ) );
        }
        // A tap: a tenth of a second of full lock at 25 m/s, full throttle.
        {
            reset( 25 );
            CarInput in;
            in.m_throttle = 1;
            float most = 0;
            for ( int k = 0; k < 120; ++k )
            {
                in.m_steer = k < 6 ? 1.0f : 0.0f;
                w.drive( 0, in );
                most = std::max( most, slide() );
            }
            printf( "  tap: 0.1 s of full lock at 25 m/s turns the car %.1f deg, the path %.1f deg (slide at most %.1f m/s)\n",
                    w.m_cars[0].m_head / Deg, pathDir() / Deg, most );
        }
    }
    if ( opt.has( "--wall" ) )
    {
        // On the track with its walls: full throttle and full right lock into the wall, then the brake held.
        CarSetup s;
        s.m_car = car;
        s.m_human = s.m_player = true;
        s.m_assist = true;
        World w;
        w.init( t, { s }, ModeE::TRIAL, 2, 1 );
        std::vector<CarInput> in( 1 );
        while ( w.m_phase == World::PhaseE::COUNTDOWN ) w.step( in );
        for ( int k = 1; k <= 60 * 10; ++k )
        {
            in[0] = CarInput();
            if ( k <= 60 * 5 )
            {
                in[0].m_throttle = 1;
                in[0].m_steer = k > 60 ? 1.0f : 0.0f;
            }
            else
            {
                in[0].m_brake = 1;
                in[0].m_steer = -1;
            }
            w.step( in );
            const Car &c = w.m_cars[0];
            if ( k % 30 == 0 ) printf( "  %4.1f s  %s  speed %6.1f  pos %6.1f %6.1f  head %5.0f\n", k * Dt, k <= 300 ? "gas  " : "brake",
                                       c.speed(), c.m_pos.x, c.m_pos.y, c.m_head / Deg );
        }
    }
    return 0;
}

}   // namespace grave
