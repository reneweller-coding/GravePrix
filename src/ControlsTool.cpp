/**
 * @file ControlsTool.cpp
 * @brief The controls tried without a window. --aim: the pad's stick that points the way (Input::aimSteer) on open
 *        ground: how quickly the nose comes round to the stick, how far it swings past, whether it settles; and
 *        backing up. --resets: the car put back on the track (World::resetCar) again and again on every track.
 * @author Rene Weller
 */
#include "Input.h"
#include "Tools.h"

#include <cmath>
#include <cstdio>

namespace grave
{

int toolAim( const Options &opt )
{
    // The car alone on open asphalt at a speed, the stick turned by an angle, the throttle held: the seconds until
    // the nose points within 5 degrees of the stick and stays there, the degrees it swings past, and how often the
    // steering changes sides once the nose is near (a controller that swings changes them again and again).
    std::string car = opt.value( "--car" );
    if ( car.empty() ) car = "rustbucket";
    int lv = (int)opt.num( "--level", 0 );
    ensureTrack( "test_undertaker" );
    const TrackDef *def = g_data.track( "test_undertaker" );
    if ( !def ) return 1;
    std::vector<std::string> errors, warnings;
    Track                    t;
    if ( !t.build( *def, g_data.venue( def->m_venue ), false, false, errors, warnings ) ) return 1;
    int bad = 0;
    for ( int assist = 1; assist >= 0; --assist )
    {
        CarSetup s;
        s.m_car = car;
        s.m_human = s.m_player = true;
        s.m_assist = assist != 0;
        s.m_engine = s.m_tires = lv;
        World w;
        w.init( t, { s }, ModeE::TRIAL, 2, 1 );
        w.m_phase = World::PhaseE::RACE;
        Car &c = w.m_cars[0];
        auto reset = [&]( float v ) {
            c.m_pos = { 0, 0 };
            c.m_head = 0;
            c.m_yaw = 0;
            c.m_steerVis = 0;
            c.m_vel = mul( c.fwd(), v );
            c.m_surface = SurfaceE::ASPHALT;
        };
        printf( "%s, upgrades %d, assist %s, stick pointing the way, throttle held\n", car.c_str(), lv, assist ? "on" : "off" );
        printf( "  speed  stick  within 5 deg  swings past  side changes\n" );
        for ( float v : { 6.0f, 15.0f, 25.0f, 35.0f } )
            for ( float deg : { 30.0f, 90.0f, -135.0f, 180.0f } )
            {
                reset( v );
                float    want = deg * Deg, settled = 0.0f, past = 0.0f, last = 0.0f;
                int      side = 0, changes = 0;
                bool     near = false;
                CarInput in;
                in.m_throttle = 1;
                for ( int k = 1; k <= 60 * 6; ++k )
                {
                    in.m_steer = Input::aimSteer( want, c, side );
                    w.drive( 0, in );
                    float e = wrapRad( want - c.m_head ) / Deg;
                    if ( std::fabs( e ) >= 5.0f ) settled = k * Dt;
                    if ( std::fabs( e ) < 15.0f ) near = true;
                    // Past the stick: the error's sign turned against the first turn.
                    if ( near && e * ( deg > 0 ? 1.0f : -1.0f ) < 0 ) past = std::max( past, std::fabs( e ) );
                    if ( near && std::fabs( in.m_steer ) > 0.2f )
                    {
                        if ( last != 0 && ( in.m_steer > 0 ) != ( last > 0 ) ) ++changes;
                        last = in.m_steer;
                    }
                }
                bool ok = settled < 5.0f && past < 12.0f && changes <= 2;
                bad += ok ? 0 : 1;
                printf( "  %5.0f  %5.0f  %10.2f s  %9.1f deg  %6d%s\n", v, deg, settled, past, changes, ok ? "" : "   <- swings" );
            }
        // Backing up from a standstill with the stick pointing behind the car: the nose comes round all the same.
        reset( 0 );
        CarInput back;
        back.m_brake = 1;
        int side = 0;
        printf( "  backing up, stick behind the car (180 deg):" );
        for ( int k = 1; k <= 60 * 4; ++k )
        {
            back.m_steer = Input::aimSteer( 179.0f * Deg, c, side );
            w.drive( 0, back );
            if ( k % 60 == 0 ) printf( "  %d s %.0f deg off", k / 60, std::fabs( wrapRad( 179.0f * Deg - c.m_head ) ) / Deg );
        }
        printf( "\n" );
    }
    printf( "%d of the runs swing\n", bad );
    return 0;
}

int toolResets( const Options &opt )
{
    // Every track (both ways where it may be raced backwards), six computer drivers, and every 6 s the first of them
    // is put back on the track. Each reset must land inside the walls with room, facing along the track, standing, at
    // most 62 m back along the lap; the race may refuse only a car that no longer races.
    std::vector<std::string> cars;
    for ( const std::string &c : g_data.m_carOrder )
        if ( g_data.m_cars.at( c ).m_class <= 5 ) cars.push_back( c );
    if ( cars.empty() ) return 1;
    int fails = 0, total = 0;
    for ( const std::string &name : g_data.m_trackOrder )
    {
        const TrackDef *def = g_data.track( name );
        if ( !def ) continue;
        for ( int rev = 0; rev <= ( def->m_reverse ? 1 : 0 ); ++rev )
        {
            std::vector<std::string> errors, warnings;
            Track                    t;
            if ( !t.build( *def, g_data.venue( def->m_venue ), rev != 0, false, errors, warnings ) ) continue;
            std::vector<CarSetup> field( 6 );
            for ( size_t k = 0; k < field.size(); ++k )
            {
                field[k].m_car = cars[( k * 3 + name.size() ) % cars.size()];
                field[k].m_driver = "AI " + std::to_string( k + 1 );
                field[k].m_skill = 0.7f;
            }
            World w;
            w.init( t, field, t.m_arena ? ModeE::DERBY : ModeE::RACE, 2, 7u + (uint32_t)rev );
            std::vector<CarInput> none( field.size() );
            int   done = 0, bad = 0;
            float next = 6.0f;
            while ( w.m_phase != World::PhaseE::DONE && w.m_time < 150.0f )
            {
                w.step( none );
                if ( w.m_time < next ) continue;
                next = w.m_time + 6.0f;
                Car  &c = w.m_cars[0];
                float before = c.m_s;
                bool  can = c.racing();
                bool  ok = w.resetCar( 0 );
                if ( ok != can )
                {
                    ++bad;
                    printf( "  %s: at %.0f s the reset was %s\n", name.c_str(), w.m_time, ok ? "allowed" : "refused" );
                }
                if ( !ok ) continue;
                ++done;
                Vector2 push;
                float   inside = t.outside( c.m_pos, push );
                float   back = t.m_arena ? 0.0f : -t.delta( before, c.m_s );
                float   along = dot( c.fwd(), t.m_s[(size_t)c.m_hint].m_dir );
                if ( inside > -2.4f || back < 0.0f || back > 64.0f || along < 0.9f || len( c.m_vel ) > 0.0f )
                {
                    ++bad;
                    printf( "  %s: at %.0f s from %.0f m to %.0f m (back %.1f m), %.1f m inside, facing %.2f\n", name.c_str(),
                            w.m_time, before, c.m_s, back, -inside, along );
                }
            }
            printf( "%-22s %s %3d resets  %s\n", name.c_str(), rev ? "reverse" : "       ", done, bad ? "FAILED" : "ok" );
            fails += bad;
            total += done;
        }
    }
    printf( "%d resets, %d failed\n", total, fails );
    return fails ? 1 : 0;
}

}   // namespace grave
