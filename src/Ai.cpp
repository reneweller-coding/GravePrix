/**
 * @file Ai.cpp
 * @brief The computer drivers: they follow the racing line at the speed its corners allow (looking ahead as far as
 *        they need to brake), change lanes to pass and to miss mines, oil and wrecks, ram when they are in the mood,
 *        shoot what is in front of them, drop gear on who is behind, and back out when stuck. Their skill, temper
 *        and love of weapons come from the rival and the difficulty. In a derby they hunt.
 * @author Rene Weller
 */
#include "Sim.h"

#include <algorithm>
#include <cmath>

namespace grave
{

namespace
{

constexpr float BrakeAccel = 13.0f;
constexpr float SteerGain = 1.3f;   ///< the wheels a little beyond the turn the arc asks (the tyres slip)
constexpr float HelpGrip = 0.95f;   ///< the brake help: the share of the side grip it plans the bends with
constexpr float HelpSlack = 1.0f;   ///< the brake help: m/s over that speed before it brakes (it lifts at once)

/** @return the rear slot of a liked gear name (-1 none) */
int rearOf( const std::string &likes )
{
    if ( likes == "mine" ) return (int)RearE::MINE;
    if ( likes == "spikes" ) return (int)RearE::SPIKES;
    if ( likes == "oil" ) return (int)RearE::OIL;
    if ( likes == "smoke" ) return (int)RearE::SMOKE;
    return -1;
}

}   // namespace

float World::cornerSpeed( int i, const TrackPos &tp, float corner, float &decel ) const
{
    const Car   &c = m_cars[i];
    const Track &T = *m_track;
    int          n = (int)T.m_s.size();
    float        v = c.speed();
    // Braking as the ground allows (the physics brakes at 45 % plus 55 % of the grip), with a margin.
    auto gripOf = [&]( SurfaceE sf, float extra ) {
        float g = std::min( surfaceGrip( sf ), extra );
        return surfaceLoose( sf ) ? std::min( 1.05f, g * c.m_offroad ) : g;
    };
    float gMin = gripOf( c.m_surface, 1.0f );
    float decelFull = BrakeAccel * c.m_brakeF * 0.82f;
    decel = decelFull * ( 0.45f + 0.55f * std::min( gMin, 1.0f ) );
    float side = sideGrip( c ) * corner;
    float want = c.m_top * 1.2f;
    float reach = v * v / ( 2 * decel ) + 25.0f;
    int   steps = std::min( n, (int)( reach / T.m_step ) + 2 );
    float d = 0;
    for ( int k = 0; k < steps; ++k )
    {
        const TrackSample &s = T.m_s[( tp.m_i + k ) % n];
        float g = gripOf( s.m_surface, s.m_lineGrip ) *
                  ( surfaceLoose( s.m_surface ) ? 1.0f : 1.0f - 0.16f * m_wetness );
        gMin = std::min( gMin, g );
        float dec = decelFull * ( 0.45f + 0.55f * std::min( gMin, 1.0f ) );
        float vmax = std::sqrt( side * g / std::max( s.m_lineCurv, 1e-4f ) );
        float allowed = std::sqrt( vmax * vmax + 2 * dec * d );
        want = std::min( want, allowed );
        d += T.m_step;
    }
    return want;
}

void World::speedPlan( int i, float corner, std::vector<float> &plan ) const
{
    const Car   &c = m_cars[i];
    const Track &T = *m_track;
    int          n = (int)T.m_s.size();
    plan.assign( (size_t)n, c.m_top );
    if ( n == 0 ) return;
    // As cornerSpeed plans it, for the whole lap at once: each sample's limit, then braked back from the next one.
    auto gripOf = [&]( SurfaceE sf, float extra ) {
        float g = std::min( surfaceGrip( sf ), extra );
        return surfaceLoose( sf ) ? std::min( 1.05f, g * c.m_offroad ) : g;
    };
    float              decelFull = BrakeAccel * c.m_brakeF * 0.82f;
    float              side = sideGrip( c ) * corner;
    std::vector<float> dec( (size_t)n );
    for ( int k = 0; k < n; ++k )
    {
        const TrackSample &s = T.m_s[k];
        float g = gripOf( s.m_surface, s.m_lineGrip ) * ( surfaceLoose( s.m_surface ) ? 1.0f : 1.0f - 0.16f * m_wetness );
        dec[k] = decelFull * ( 0.45f + 0.55f * std::min( g, 1.0f ) );
        plan[k] = std::min( c.m_top, std::sqrt( side * g / std::max( s.m_lineCurv, 1e-4f ) ) );
    }
    for ( int pass = 0; pass < 2; ++pass )
        for ( int k = n - 1; k >= 0; --k )
        {
            float next = plan[(size_t)( ( k + 1 ) % n )];
            plan[k] = std::min( plan[k], std::sqrt( next * next + 2.0f * dec[k] * T.m_step ) );
        }
}

CarInput World::brakeHelp( int i, const CarInput &own )
{
    // The computer drivers' plan for the bends, at nearly the whole grip: above it the throttle goes, a little more
    // and the brake comes on. Never more throttle than the driver gives, never less brake.
    const Car &c = m_cars[i];
    CarInput   in = own;
    float      v = c.speed();
    if ( m_phase != PhaseE::RACE || m_mode == ModeE::DERBY || v < 8.0f || in.m_handbrake ) return in;
    TrackPos tp = m_track->locate( c.m_pos, c.m_hint );
    if ( dot( c.fwd(), m_track->m_s[tp.m_i].m_dir ) < 0.5f ) return in;   // across or against the road: no plan fits
    float decel = 0;
    float want = cornerSpeed( i, tp, HelpGrip, decel );
    if ( v > want ) in.m_throttle = 0;
    if ( v > want + HelpSlack ) in.m_brake = std::max( in.m_brake, clampf( ( v - want ) / 5.0f, 0.25f, 1.0f ) );
    return in;
}

CarInput World::ai( int i )
{
    Car     &c = m_cars[i];
    AiState &a = c.m_ai;
    CarInput in;
    if ( c.m_wrecked || m_phase == PhaseE::COUNTDOWN ) return in;
    const Track         &T = *m_track;
    const DifficultyDef &dif = g_data.difficulty( m_difficulty );
    int                  n = (int)T.m_s.size();
    // The player's car (a computer driving for the player in tests) drives by its own skill, whatever the difficulty.
    bool  own = c.m_setup.m_player;
    float skill = clampf( ( 0.35f + 0.65f * c.m_setup.m_skill ) * ( own ? 1.0f : 0.55f + 0.45f * dif.m_aiSkill ), 0.1f, 1.0f );
    float temper = clampf( c.m_setup.m_aggression * ( own ? 1.0f : dif.m_aiAggression ), 0, 1 );
    float trigger = clampf( c.m_setup.m_weapons * ( own ? 1.0f : dif.m_aiWeapons ), 0, 1 );
    bool  calm = c.m_finished;   // a lap of honour after the flag (also for the humans' cars)
    if ( calm ) skill = std::max( skill, 0.7f );
    float    v = c.speed();
    TrackPos tp = T.locate( c.m_pos, c.m_hint );
    const TrackSample &here = T.m_s[tp.m_i];
    a.m_think -= Dt;
    bool think = a.m_think <= 0;
    if ( think ) a.m_think = m_rng.range( 0.18f, 0.32f );

    if ( m_mode == ModeE::DERBY && !calm )
    {
        // Hunt the nearest car (the human first when in a temper), ram it, shoot it, mine it.
        if ( think || a.m_target < 0 || m_cars[a.m_target].m_wrecked )
        {
            float best = 1e9f;
            a.m_target = -1;
            for ( size_t k = 0; k < m_cars.size(); ++k )
            {
                if ( (int)k == i || m_cars[k].m_wrecked ) continue;
                float d = len( sub( m_cars[k].m_pos, c.m_pos ) ) *
                          ( m_cars[k].m_setup.m_player ? 1.0f - 0.4f * temper - 0.15f * c.m_setup.m_grudge : 1.0f );
                if ( d < best )
                {
                    best = d;
                    a.m_target = (int)k;
                }
            }
        }
        if ( a.m_target < 0 ) return in;
        const Car &t = m_cars[a.m_target];
        Vector2 aim = add( t.m_pos, mul( t.m_vel, 0.35f ) );
        Vector2 d = sub( aim, c.m_pos );
        float   ang = wrapRad( headingOf( d ) - c.m_head );
        float   dl = len( d );
        if ( a.m_reverse > 0 )
        {
            a.m_reverse -= Dt;
            in.m_brake = 1;
            in.m_steer = ang > 0 ? -1.0f : 1.0f;
            return in;
        }
        // Far round: full lock (and the handbrake when fast); else the arc to the target, as on a race track, but no
        // more lock than the tyres carry (scrubbing would cost the speed the ram needs).
        float lock = std::max( fullLock( c, v ), 0.3f );
        float sp = std::max( std::fabs( v ), 4.0f );
        float turn = 2.0f * sp * std::sin( clampf( ang, -1.5f, 1.5f ) ) / std::max( dl, 3.0f );
        float carry = clampf( sideGrip( c ) * std::min( surfaceGrip( c.m_surface ), 1.0f ) / sp / lock, 0.2f, 1.0f );
        if ( std::fabs( ang ) > 1.4f )
            in.m_steer = ang > 0 ? 1.0f : -1.0f;
        else
            in.m_steer = clampf( SteerGain * turn / lock - ( c.m_yaw - turn ) * 0.1f, -carry, carry );
        in.m_throttle = std::fabs( ang ) > 2.2f && dl < 15 ? 0.3f : 1.0f;
        in.m_handbrake = std::fabs( ang ) > 1.4f && v > 10;
        if ( std::fabs( v ) < 1.5f && in.m_throttle > 0.5f ) a.m_stuck += Dt;
        else a.m_stuck = std::max( 0.0f, a.m_stuck - Dt );
        if ( a.m_stuck > 1.0f )
        {
            a.m_stuck = 0;
            a.m_reverse = 0.9f;
        }
        in.m_fire = std::fabs( ang ) < 0.12f && dl < c.m_gun->m_range * 0.8f && m_rng.chance( 0.3f + trigger * 0.7f );
        if ( think && m_rng.chance( trigger * 0.25f ) )
        {
            in.m_rocket = std::fabs( ang ) < 0.3f && dl > 15;
            float back = std::fabs( wrapRad( headingOf( sub( t.m_pos, c.m_pos ) ) - c.m_head ) );
            in.m_drop = back > 2.4f && dl < 20;
        }
        in.m_nitro = think && std::fabs( ang ) < 0.2f && dl > 25 && m_rng.chance( 0.1f );
        return in;
    }

    // Backing out of a corner it got stuck in.
    if ( a.m_reverse > 0 )
    {
        a.m_reverse -= Dt;
        float back = wrapRad( headingOf( here.m_dir ) - c.m_head );
        in.m_brake = 1;
        in.m_steer = back > 0 ? -1.0f : 1.0f;
        return in;
    }

    // Lanes: around slower cars, away from what lies on the road, into a victim when in a temper.
    if ( think )
    {
        float goal = a.m_laneGoal * 0.6f;
        float myLat = tp.m_lat;
        for ( size_t k = 0; k < m_cars.size(); ++k )
        {
            if ( (int)k == i || m_cars[k].m_gone ) continue;
            const Car &o = m_cars[k];
            float      ds = T.delta( c.m_s, o.m_s );
            if ( ds < 0 || ds > 28 ) continue;
            TrackPos   op = T.locate( o.m_pos, o.m_hint );
            float      dlat = op.m_lat - myLat;
            if ( std::fabs( dlat ) > 3.2f ) continue;
            if ( !o.m_wrecked && o.speed() > v - 2 ) continue;
            // Pass on the side with more room.
            float roomL = op.m_lat + here.m_half, roomR = here.m_half - op.m_lat;
            goal = ( roomR > roomL ? op.m_lat + 4.0f : op.m_lat - 4.0f ) - here.m_line;
        }
        for ( const Hazard &h : m_hazards )
        {
            if ( h.m_kind == Hazard::SMOKE || ( h.m_owner == i && h.m_age < 3 ) ) continue;
            TrackPos hp = T.locate( h.m_pos, -1 < c.m_hint ? c.m_hint : -1 );
            float    ds = T.delta( c.m_s, hp.m_s );
            if ( ds < 2 || ds > 40 ) continue;
            float lineLat = here.m_line + a.m_lane;
            if ( std::fabs( hp.m_lat - lineLat ) > h.m_r + 2.2f ) continue;
            if ( m_rng.chance( 0.35f + 0.6f * skill ) ) goal = ( hp.m_lat > 0 ? hp.m_lat - h.m_r - 3.0f : hp.m_lat + h.m_r + 3.0f ) - here.m_line;
        }
        for ( const PatchSpot &pa : T.m_patches )
        {
            if ( pa.m_surface != SurfaceE::OIL && pa.m_surface != SurfaceE::ICE ) continue;
            float ds = T.delta( c.m_s, pa.m_s0 );
            if ( ds < 0 || ds > 40 ) continue;
            float lineLat = here.m_line + a.m_lane;
            if ( pa.m_lat0 < -here.m_half + 2.5f && pa.m_lat1 > here.m_half - 2.5f ) continue;   // no way round
            if ( lineLat > pa.m_lat0 - 1.5f && lineLat < pa.m_lat1 + 1.5f )
            {
                float left = pa.m_lat0 - 2.0f, right = pa.m_lat1 + 2.0f;
                goal = ( std::fabs( left - lineLat ) < std::fabs( right - lineLat ) ? left : right ) - here.m_line;
            }
        }
        for ( const Rock &r : m_rocks )
        {
            TrackPos rp = T.locate( r.m_pos, c.m_hint );
            float    ds = T.delta( c.m_s, rp.m_s );
            if ( ds < -c.m_len - r.m_r || ds > 45 ) continue;   // also beside it: else it backs out and drives in again
            float lineLat = here.m_line + a.m_lane;
            if ( std::fabs( rp.m_lat - lineLat ) < r.m_r + 2.5f )
                goal = ( rp.m_lat > 0 ? rp.m_lat - r.m_r - 3.0f : rp.m_lat + r.m_r + 3.0f ) - here.m_line;
        }
        for ( const ObstacleSpot &o : T.m_obstacles )
        {
            TrackPos op = T.locate( o.m_pos, c.m_hint );
            float    ds = T.delta( c.m_s, op.m_s );
            if ( ds < -c.m_len - o.m_r || ds > 45 ) continue;
            float lineLat = here.m_line + a.m_lane;
            if ( std::fabs( op.m_lat - lineLat ) < o.m_r + 2.5f )
                goal = ( op.m_lat > 0 ? op.m_lat - o.m_r - 3.0f : op.m_lat + o.m_r + 3.0f ) - here.m_line;
        }
        if ( a.m_ram <= 0 && !calm && !c.m_setup.m_escort && m_rng.chance( temper * 0.05f ) )
        {
            for ( size_t k = 0; k < m_cars.size(); ++k )
            {
                const Car &o = m_cars[k];
                if ( (int)k == i || o.m_wrecked ) continue;
                float ds = T.delta( c.m_s, o.m_s );
                if ( ds < -4 || ds > 16 ) continue;
                // A feud rams only the player; Graves' hearse is everyone's favourite.
                if ( !o.m_setup.m_player && !o.m_setup.m_escort && ( c.m_setup.m_grudge > 0 || m_rng.chance( 0.6f ) ) ) continue;
                a.m_ram = 1.5f;
                a.m_target = (int)k;
                break;
            }
        }
        if ( a.m_ram > 0 && a.m_target >= 0 )
        {
            TrackPos op = T.locate( m_cars[a.m_target].m_pos, m_cars[a.m_target].m_hint );
            goal = op.m_lat - here.m_line;
        }
        a.m_laneGoal = goal;
        // Low skill: now and then a wobble or a late brake.
        float nov = c.m_setup.m_novice;
        if ( m_rng.chance( ( 1.0f - skill ) * 0.04f + nov * 0.06f ) )
        {
            a.m_wobble = m_rng.range( -0.5f, 0.5f ) * ( 1.0f - skill + nov * 0.6f );
            a.m_wobbleTime = m_rng.range( 0.3f, 0.9f );
        }
        if ( m_rng.chance( ( 1.0f - skill ) * 0.02f + nov * 0.03f ) ) a.m_mistake = m_rng.range( 0.4f, 1.0f + nov );
    }
    a.m_ram = std::max( 0.0f, a.m_ram - Dt );
    a.m_wobbleTime = std::max( 0.0f, a.m_wobbleTime - Dt );
    a.m_mistake = std::max( 0.0f, a.m_mistake - Dt );
    float limit = std::max( 0.0f, here.m_half - 1.6f );
    a.m_laneGoal = clampf( a.m_laneGoal, -limit - here.m_line, limit - here.m_line );
    a.m_lane += ( a.m_laneGoal - a.m_lane ) * std::min( 1.0f, Dt * 1.6f * ( 1.0f - 0.6f * c.m_setup.m_novice ) );

    // Steering at a point ahead on its lane.
    float   look = 6.0f + std::fabs( v ) * 0.42f;
    float   st = tp.m_s + look;
    const TrackSample &ahead = T.sampleAt( st );
    float   lat = clampf( ahead.m_line + a.m_lane, -( ahead.m_half - 1.4f ), ahead.m_half - 1.4f );
    // The arc from where the car is going (its path: the nose slips a little ahead in a bend) through that point asks
    // for a turn; full lock turns the nose at a known rate (fullLock), so the wheels turn by that share, a little more
    // to make up for the tyres' slip, the nose's own swing damped. (Holding the lock to what the tyres carry, as in a
    // derby, cost 4 % of the pace and half again as many wall hits: the line needs its corrections.)
    Vector2 P = T.pointAt( st, lat );
    Vector2 to = sub( P, c.m_pos );
    float   path = std::fabs( v ) > 3.0f ? headingOf( c.m_vel ) : c.m_head;
    float   ang = clampf( wrapRad( headingOf( to ) - path ), -1.5f, 1.5f );
    float   turn = 2.0f * std::max( std::fabs( v ), 4.0f ) * std::sin( ang ) / std::max( len( to ), 2.0f );
    in.m_steer = clampf( SteerGain * turn / std::max( fullLock( c, v ), 0.3f ) - ( c.m_yaw - turn ) * 0.1f +
                             ( a.m_wobbleTime > 0 ? a.m_wobble : 0.0f ),
                         -1, 1 );
    if ( c.m_smoked > 0 ) in.m_steer = clampf( in.m_steer + m_rng.range( -0.7f, 0.7f ), -1, 1 );

    // The speed the corners ahead allow.
    float corner = ( 0.8f + 0.2f * skill ) * ( 1.0f - 0.14f * c.m_setup.m_novice ) * ( 1.0f - 0.5f * c.m_slip );
    if ( c.m_setup.m_flatOut ) corner = 50.0f;
    float decel = 0;
    float want = cornerSpeed( i, tp, corner, decel );
    // Trains and raised bridges ahead: stop short of them (a beginner a little late).
    for ( const MoverSpot &m : T.m_movers )
    {
        if ( m.m_kind == "rockfall" ) continue;
        float before = m.m_kind == "drawbridge" ? m.m_len * 0.5f + c.m_len * 0.5f + 2.0f : 5.5f + c.m_len * 0.5f;
        float ds = T.delta( c.m_s, m.m_s ) - before;
        if ( ds < -1.0f || ds > 140.0f ) continue;
        if ( !moverBlocks( m, ds / std::max( std::fabs( v ), 6.0f ) ) && !moverBlocks( m, 0.0f ) ) continue;
        // Too close to stop while the bells ring: through, before the train or the leaf comes.
        float amount;
        if ( v * v / ( 2.0f * decel ) > ds + 1.0f && moverState( m, amount ) == 1 ) continue;
        float stop = std::sqrt( 2.0f * decel * std::max( 0.0f, ds - 3.0f * c.m_setup.m_novice ) );
        want = std::min( want, stop );
    }
    // The car in front on its lane (wrecks too): slow enough to stop behind it, unless there is room beside it.
    {
        float myLat = here.m_line + a.m_lane;
        for ( size_t k = 0; k < m_cars.size(); ++k )
        {
            if ( (int)k == i || m_cars[k].m_gone ) continue;
            const Car &o = m_cars[k];
            float      ds = T.delta( c.m_s, o.m_s );
            if ( ds <= 0 || ds > 45 ) continue;
            TrackPos op = T.locate( o.m_pos, o.m_hint );
            if ( std::fabs( op.m_lat - myLat ) > ( c.m_wid + o.m_wid ) * 0.5f + 0.8f ) continue;
            float ov = o.m_wrecked ? 0.0f : std::max( 0.0f, o.speed() );
            float gap = std::max( 0.0f, ds - ( c.m_len + o.m_len ) * 0.5f - 2.5f );
            float allowed = std::sqrt( ov * ov + 2 * decel * gap ) + ( a.m_ram > 0 && a.m_target == (int)k ? 6.0f : 0.0f );
            want = std::min( want, allowed );
        }
    }
    // Far off its line: slower.
    float off = std::fabs( tp.m_lat - ( here.m_line + a.m_lane ) );
    if ( off > 3 ) want *= clampf( 1.0f - ( off - 3 ) * 0.04f, 0.6f, 1.0f );
    // The field waits for the human a little, and chases them a little.
    for ( const Car &h : m_cars )
        if ( h.m_setup.m_player && h.racing() && !calm )
        {
            float gap = c.m_dist - h.m_dist;
            want *= 1.0f - dif.m_rubber * clampf( gap / 160.0f, -1, 1 ) * 0.14f;
            break;
        }
    if ( calm ) want *= 0.65f;
    if ( a.m_mistake > 0 ) want *= 1.15f;
    if ( v < want - 0.8f )
        in.m_throttle = 1.0f;
    else if ( v > want + 1.2f )
        in.m_brake = clampf( ( v - want ) / 5.0f, 0.25f, 1.0f );
    else
        in.m_throttle = 0.45f;

    // Stuck against a wall, or turned round.
    float facing = dot( c.fwd(), here.m_dir );
    if ( ( in.m_throttle > 0.5f && std::fabs( v ) < 1.5f ) || ( facing < -0.2f && v < 6 ) )
        a.m_stuck += Dt;
    else
        a.m_stuck = std::max( 0.0f, a.m_stuck - Dt * 0.5f );
    if ( a.m_stuck > 1.1f )
    {
        a.m_stuck = 0;
        a.m_reverse = 1.0f;
    }

    if ( calm || m_mode == ModeE::TRIAL ) return in;

    // Guns: whoever is in front, within range and in the sights.
    int   inSights = -1, behind = -1, rocketT = -1;
    float gunRange = c.m_gun->m_range * 0.7f;
    for ( size_t k = 0; k < m_cars.size(); ++k )
    {
        const Car &o = m_cars[k];
        if ( (int)k == i || o.m_wrecked ) continue;
        Vector2 dd = sub( o.m_pos, c.m_pos );
        float   dl = len( dd );
        float   oa = std::fabs( wrapRad( headingOf( dd ) - c.m_head ) );
        // Graves' hearse draws fire from a wider angle.
        float wide = o.m_setup.m_escort ? 2.0f : 1.0f;
        if ( dl < gunRange && oa < ( 0.12f + 0.6f / std::max( dl, 1.0f ) ) * wide ) inSights = (int)k;
        if ( dl > 25 && dl < 120 && oa < 0.35f * wide ) rocketT = (int)k;
        // Behind: close and in its tracks (what it drops will be driven over).
        float side = std::fabs( dot( dd, c.right() ) );
        if ( dl > 4 && dl < 20 && oa > 2.75f && side < 2.4f ) behind = (int)k;
    }
    if ( inSights >= 0 && think && m_time > 4.0f && trigger > 0.05f && m_rng.chance( 0.45f * trigger ) )
        a.m_fireHold = m_rng.range( 0.25f, 0.7f );
    if ( inSights < 0 && a.m_fireHold > 0.2f ) a.m_fireHold = 0.2f;
    a.m_fireHold = std::max( 0.0f, a.m_fireHold - Dt );
    in.m_fire = a.m_fireHold > 0;
    if ( think && m_time > 5.0f )
    {
        a.m_gearWait = std::max( 0.0f, a.m_gearWait - 0.25f );
        if ( behind >= 0 && a.m_gearWait <= 0 && m_rng.chance( trigger * 0.3f ) )
        {
            int like = rearOf( c.m_setup.m_likes );
            if ( like >= 0 && c.m_rear[like] > 0 && c.m_rearSel != like ) in.m_cycle = true;
            in.m_drop = true;
            a.m_gearWait = m_rng.range( 6.0f, 12.0f ) * ( 1.3f - trigger * 0.5f );
        }
        if ( rocketT >= 0 && c.m_rockets > 0 && a.m_gearWait <= 0 && m_rng.chance( trigger * 0.15f ) )
        {
            in.m_rocket = true;
            a.m_gearWait = m_rng.range( 6.0f, 12.0f );
        }
        if ( c.m_nitro > 0 && want > v + 6 && v > 12 && m_rng.chance( 0.06f + 0.1f * skill ) )
        {
            float maxCurv = 0;
            for ( int k = 0; k < 40; ++k ) maxCurv = std::max( maxCurv, T.m_s[( tp.m_i + k ) % n].m_lineCurv );
            if ( maxCurv < 0.006f ) in.m_nitro = true;
        }
    }
    return in;
}

}   // namespace grave
