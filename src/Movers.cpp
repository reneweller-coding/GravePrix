/**
 * @file Movers.cpp
 * @brief The track's clockwork: trains over level crossings, drawbridges that rise, rocks that fall, and the weather
 *        that turns in the middle of a race. Everything follows the race clock, so the same race runs the same way.
 * @author Rene Weller
 */
#include "Sim.h"

#include <algorithm>
#include <cmath>

namespace grave
{

namespace
{

constexpr float TrainSpeed = 28.0f;    ///< m/s
constexpr float TrainLength = 58.0f;   ///< the locomotive and three wagons
constexpr float Warn = 5.0f;           ///< seconds of bells before a train or a bridge

/** @return seconds a train needs to cross the road */
float trainPass( const MoverSpot &m ) { return ( 2.0f * ( m.m_half + 18.0f ) + TrainLength ) / TrainSpeed; }

uint32_t mix( uint32_t h )
{
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

}   // namespace

float World::moverPhase( const MoverSpot &m, float t ) const
{
    float p = std::fmod( t + m.m_offset, m.m_period );
    return p < 0 ? p + m.m_period : p;
}

int World::moverState( const MoverSpot &m, float &amount, float ahead ) const
{
    float p = moverPhase( m, m_time + ahead );
    amount = 0.0f;
    if ( m.m_kind == "train" )
    {
        if ( p < Warn ) return 1;
        float pass = trainPass( m );
        if ( p < Warn + pass )
        {
            amount = ( p - Warn ) / pass;
            return 2;
        }
        return 0;
    }
    if ( m.m_kind == "drawbridge" )
    {
        // Bells, three seconds up, eight seconds up, three seconds down.
        if ( p < Warn ) return 1;
        if ( p < Warn + 3 ) amount = ( p - Warn ) / 3;
        else if ( p < Warn + 11 ) amount = 1;
        else if ( p < Warn + 14 ) amount = 1 - ( p - Warn - 11 ) / 3;
        else return 0;
        amount = amount * amount * ( 3 - 2 * amount );
        return 2;
    }
    return 0;
}

float World::trainFront( const MoverSpot &m ) const
{
    float amount;
    moverState( m, amount );
    return -( m.m_half + 18.0f ) + amount * trainPass( m ) * TrainSpeed;
}

bool World::moverBlocks( const MoverSpot &m, float ahead ) const
{
    float amount;
    int   st = moverState( m, amount, ahead );
    if ( m.m_kind == "train" ) return st == 1 || ( st == 2 && amount < 0.85f );
    if ( m.m_kind == "drawbridge" ) return st >= 1;
    return false;
}

void World::movers()
{
    const Track &T = *m_track;
    // The weather turns when the leader starts the lap the track names.
    if ( !T.m_weatherTo.empty() && !m_weatherTurned && m_cars.size() > 0 &&
         m_cars[m_leader].m_lap + 1 >= std::max( 1, T.m_weatherLap ) && T.m_weatherLap > 0 )
    {
        m_weatherTurned = true;
        int kind = T.m_weatherTo == "rain" ? 0 : T.m_weatherTo == "fog" ? 1 : 2;
        event( EvE::WEATHER, -1, { 0, 0 }, 1, kind );
    }
    if ( m_weatherTurned )
    {
        if ( T.m_weatherTo == "rain" ) m_wetness = std::min( 1.0f, m_wetness + Dt / 20.0f );
        else m_fogAdd = std::min( 0.012f, m_fogAdd + Dt * 0.0006f );
    }
    if ( m_moverCycle.size() != T.m_movers.size() ) m_moverCycle.assign( T.m_movers.size(), -1 );
    for ( size_t k = 0; k < T.m_movers.size(); ++k )
    {
        const MoverSpot &m = T.m_movers[k];
        float            amount;
        int              st = moverState( m, amount );
        float            before = moverPhase( m, m_time - Dt ), now = moverPhase( m, m_time );
        if ( m.m_kind == "train" || m.m_kind == "drawbridge" )
        {
            if ( now < before ) event( m.m_kind == "train" ? EvE::TRAIN : EvE::BRIDGE, -1, m.m_pos, 1, 0 );   // the bells
            if ( before < Warn && now >= Warn ) event( m.m_kind == "train" ? EvE::TRAIN : EvE::BRIDGE, -1, m.m_pos, 1, 1 );
        }
        if ( m.m_kind == "train" && st == 2 )
        {
            // The train: a long box across the road; whoever is in it is hit hard and thrown along.
            float front = trainFront( m );
            for ( size_t i = 0; i < m_cars.size(); ++i )
            {
                Car    &c = m_cars[i];
                Vector2 d = sub( c.m_pos, m.m_pos );
                float   u = dot( d, m.m_dir ), v = dot( d, m.m_right );
                if ( std::fabs( u ) > 1.6f + c.m_len * 0.45f || v > front + c.m_wid || v < front - TrainLength - c.m_wid ) continue;
                if ( m_time - c.m_moverHit < 2.0f ) continue;
                c.m_moverHit = m_time;
                c.m_vel = add( mul( m.m_right, TrainSpeed * 0.9f ), mul( m.m_dir, dot( c.m_vel, m.m_dir ) * 0.3f ) );
                c.m_yaw += m_rng.range( -3.0f, 3.0f );
                m_source = 5;
                event( EvE::CRASH, (int)i, c.m_pos, 30.0f );
                damage( (int)i, 85.0f, -1, c.m_pos );
            }
        }
        else if ( m.m_kind == "drawbridge" && st == 2 )
        {
            // A raised leaf: a wall for whoever comes, a slope that tips off whoever is on it.
            float half = m.m_len * 0.5f;
            for ( size_t i = 0; i < m_cars.size(); ++i )
            {
                Car    &c = m_cars[i];
                Vector2 d = sub( c.m_pos, m.m_pos );
                float   u = dot( d, m.m_dir ), v = dot( d, m.m_right );
                if ( std::fabs( v ) > m.m_half + 2.0f ) continue;
                float edge = c.m_len * 0.5f;
                if ( std::fabs( u ) < half && amount > 0.1f )
                {
                    float side = u < 0 ? -1.0f : 1.0f;
                    c.m_vel = add( c.m_vel, mul( m.m_dir, side * 9.0f * amount * Dt ) );
                    continue;
                }
                if ( amount < 0.15f ) continue;
                float out = std::fabs( u ) - half;
                if ( out > edge + 0.2f || out < -0.5f ) continue;
                float   side = u < 0 ? -1.0f : 1.0f;
                float   vn = dot( c.m_vel, m.m_dir ) * side;   // < 0: towards the bridge
                Vector2 hit = add( m.m_pos, mul( m.m_dir, side * half ) );
                c.m_pos = add( c.m_pos, mul( m.m_dir, side * ( edge + 0.2f - out ) ) );
                if ( vn < 0 )
                {
                    c.m_vel = sub( c.m_vel, mul( m.m_dir, side * vn * 1.4f ) );
                    if ( -vn > 7.0f && !c.m_wrecked )
                    {
                        m_source = 3;
                        damage( (int)i, ( -vn - 7.0f ) * 0.8f, -1, hit );
                        event( EvE::WALL, (int)i, hit, -vn );
                    }
                }
            }
        }
        else if ( m.m_kind == "rockfall" )
        {
            int cycle = (int)std::floor( ( m_time + m.m_offset ) / m.m_period );
            if ( cycle != m_moverCycle[k] )
            {
                if ( m_moverCycle[k] >= 0 )
                {
                    uint32_t h = mix( m_seed * 977u + (uint32_t)k * 131u + (uint32_t)cycle * 7919u );
                    float    s = m.m_s + ( h % 1000 ) / 1000.0f * m.m_len;
                    const TrackSample &ts = T.sampleAt( s );
                    float lat = ( ( h >> 10 ) % 1000 / 1000.0f * 2.0f - 1.0f ) * std::max( 0.5f, ts.m_half - 4.0f );
                    Rock  r;
                    r.m_pos = T.pointAt( s, lat );
                    r.m_r = 1.1f + ( ( h >> 20 ) % 100 ) / 100.0f * 0.6f;
                    r.m_age = -1.8f;
                    m_rocks.push_back( r );
                    event( EvE::ROCK, -1, r.m_pos, r.m_r, 0 );
                }
                m_moverCycle[k] = cycle;
            }
        }
    }
    // The rocks: falling, landing, lying a while, crumbling (one at a time, so the road stays open).
    for ( Rock &r : m_rocks )
    {
        float was = r.m_age;
        r.m_age += Dt;
        if ( was < 0 && r.m_age >= 0 )
        {
            event( EvE::ROCK, -1, r.m_pos, r.m_r, 1 );
            for ( size_t i = 0; i < m_cars.size(); ++i )
            {
                Car  &c = m_cars[i];
                float d = len( sub( c.m_pos, r.m_pos ) );
                if ( d > r.m_r + 1.6f ) continue;
                m_source = 5;
                damage( (int)i, 50.0f, -1, r.m_pos );
                c.m_vel = mul( c.m_vel, 0.3f );
            }
        }
    }
    m_rocks.erase( std::remove_if( m_rocks.begin(), m_rocks.end(), []( const Rock &r ) { return r.m_age > 12.0f; } ), m_rocks.end() );
}

}   // namespace grave
