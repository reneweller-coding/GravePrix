/**
 * @file Sim.cpp
 * @brief The race: physics, collisions, guns and gear, pickups, laps and places.
 * @author Rene Weller
 */
#include "Sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace grave
{

namespace
{

constexpr float LatAccel = 15.0f;     ///< side grip of an ordinary car on asphalt (m/s²)
constexpr float BrakeAccel = 13.0f;   ///< braking of an ordinary car (m/s²)
constexpr float SteerRate = 2.4f;     ///< turning at low speed (radians a second)

/** @return the first gear of a kind (nullptr none) */
const GearDef *gearOf( GearE kind )
{
    for ( const std::string &n : g_data.m_gearOrder )
    {
        const GearDef &g = g_data.m_gear.at( n );
        if ( g.m_kind == kind ) return &g;
    }
    return nullptr;
}

/** @brief A gear's value or a default when the data has none. */
template <typename F> float gearVal( GearE kind, F f, float def )
{
    const GearDef *g = gearOf( kind );
    return g ? f( *g ) : def;
}

const CarDef &fallbackCar()
{
    static CarDef c;
    c.m_name = "?";
    c.m_model = "?";
    return c;
}

const GunDef &fallbackGun()
{
    static GunDef g;
    g.m_name = "mg";
    g.m_damage = { 2, 4 };
    g.m_rate = { 8, 12 };
    return g;
}

/** @return a slab test of segment a-b against the box |x| <= hx, |y| <= hy (t of entry, or -1) */
float segBox( Vector2 a, Vector2 b, float hx, float hy )
{
    float t0 = 0, t1 = 1;
    Vector2 d = sub( b, a );
    float   p[2] = { a.x, a.y }, dd[2] = { d.x, d.y }, h[2] = { hx, hy };
    for ( int k = 0; k < 2; ++k )
    {
        if ( std::fabs( dd[k] ) < 1e-8f )
        {
            if ( p[k] < -h[k] || p[k] > h[k] ) return -1;
            continue;
        }
        float u0 = ( -h[k] - p[k] ) / dd[k], u1 = ( h[k] - p[k] ) / dd[k];
        if ( u0 > u1 ) std::swap( u0, u1 );
        t0 = std::max( t0, u0 );
        t1 = std::min( t1, u1 );
        if ( t0 > t1 ) return -1;
    }
    return t0;
}

/** @return a point in a car's frame: x to its right, y forwards */
Vector2 local( const Car &c, Vector2 p )
{
    Vector2 d = sub( p, c.m_pos );
    return { dot( d, c.right() ), dot( d, c.fwd() ) };
}

}   // namespace

CarStats carStats( const CarSetup &s )
{
    const CarDef   &d = g_data.car( s.m_car ) ? *g_data.car( s.m_car ) : fallbackCar();
    const GunDef   *gp = g_data.gun( s.m_gun.empty() ? d.m_gun : s.m_gun );
    const GunDef   &gun = gp ? *gp : fallbackGun();
    const UpgradeDef *ue = g_data.upgrade( "engine" ), *ut = g_data.upgrade( "tires" ), *ua = g_data.upgrade( "armor" );
    CarStats st;
    st.m_power = d.m_power * ( 1.0f + s.m_engine * ( ue ? ue->m_power : 0.07f ) );
    st.m_top = d.m_top * ( 1.0f + s.m_engine * ( ue ? ue->m_top : 0.025f ) );
    st.m_grip = d.m_grip * ( 1.0f + s.m_tires * ( ut ? ut->m_grip : 0.05f ) );
    st.m_brake = d.m_brake * ( 1.0f + s.m_tires * ( ut ? ut->m_brake : 0.05f ) );
    st.m_offroad = d.m_offroad * ( 1.0f + s.m_tires * ( ut ? ut->m_offroad : 0.05f ) );
    st.m_steer = d.m_steer;
    // HpScale: the data's armour is relative; races last about two and a half minutes and should leave one or two
    // wrecks, not four.
    constexpr float HpScale = 1.6f;
    st.m_hp = HpScale * d.m_armor * ( 1.0f + s.m_armor * ( ua ? ua->m_hp : 0.18f ) ) +
              s.m_plating * gearVal( GearE::PLATING, []( const GearDef &g ) { return g.m_hp; }, 40.0f );
    float t = d.m_upGun > 0 ? clampf( s.m_gunLv / (float)d.m_upGun, 0, 1 ) : 0.0f;
    st.m_ammo = gun.m_ammo.at( t ) * d.m_ammo;
    st.m_damage = gun.m_damage.at( t );
    st.m_rate = gun.m_rate.at( t );
    return st;
}

void World::init( const Track &track, const std::vector<CarSetup> &cars, ModeE mode, int difficulty, uint32_t seed )
{
    m_track = &track;
    m_mode = mode;
    m_difficulty = difficulty;
    m_seed = seed;
    m_rng.seed( seed );
    m_phase = PhaseE::COUNTDOWN;
    m_countdown = mode == ModeE::TRIAL ? 1.5f : 3.5f;
    m_time = 0;
    m_laps = std::max( 1, track.m_laps );
    m_cars.clear();
    m_bullets.clear();
    m_rockets.clear();
    m_hazards.clear();
    m_pickups.clear();
    m_barrels.clear();
    m_finishOrder.clear();
    const DifficultyDef &dif = g_data.difficulty( difficulty );
    auto grid = track.grid( (int)cars.size() );
    for ( size_t i = 0; i < cars.size(); ++i )
    {
        Car c;
        c.m_setup = cars[i];
        c.m_def = g_data.car( c.m_setup.m_car );
        if ( !c.m_def ) c.m_def = &fallbackCar();
        c.m_gun = g_data.gun( c.m_setup.m_gun.empty() ? c.m_def->m_gun : c.m_setup.m_gun );
        if ( !c.m_gun ) c.m_gun = &fallbackGun();
        CarStats st = carStats( c.m_setup );
        c.m_power = st.m_power;
        c.m_top = st.m_top;
        c.m_grip = st.m_grip;
        c.m_brakeF = st.m_brake;
        c.m_steerF = st.m_steer;
        c.m_offroad = st.m_offroad;
        c.m_maxHp = st.m_hp;
        c.m_hp = st.m_hp * clampf( c.m_setup.m_hpShare, 0.05f, 1.0f );
        c.m_maxAmmo = c.m_ammo = st.m_ammo;
        c.m_mass = c.m_def->m_mass;
        c.m_len = c.m_def->m_length;
        c.m_wid = c.m_def->m_width;
        c.m_inertia = c.m_mass * ( c.m_len * c.m_len + c.m_wid * c.m_wid ) / 12.0f;
        if ( c.m_setup.m_player )
        {
            c.m_dmgTaken = dif.m_damageTaken;
            c.m_dmgDealt = dif.m_damageDealt;
        }
        else
        {
            // The computer's pace: the difficulty's share of the ideal, and its own skill.
            float k = dif.m_aiSpeed * ( 0.9f + 0.1f * c.m_setup.m_skill );
            c.m_power *= k;
            c.m_top *= 0.94f + 0.06f * k;
        }
        for ( int r = 0; r < (int)RearE::COUNT; ++r ) c.m_rear[r] = c.m_setup.m_rear[r];
        c.m_rear[(int)RearE::MINE] += c.m_def->m_mines;
        c.m_rockets = c.m_setup.m_rockets;
        c.m_nitro = c.m_setup.m_nitro + c.m_def->m_nitro;
        c.m_pos = grid[i].first;
        c.m_head = grid[i].second;
        TrackPos tp = track.locate( c.m_pos, -1 );
        c.m_hint = tp.m_i;
        c.m_s = tp.m_s;
        c.m_dist = track.delta( 0.0f, tp.m_s );
        c.m_ai.m_lane = c.m_ai.m_laneGoal = tp.m_lat - track.m_s[tp.m_i].m_line;
        c.m_ai.m_think = m_rng.range( 0.5f, 2.0f );
        m_cars.push_back( c );
    }
    for ( const PickupSpot &p : track.m_pickups ) m_pickups.push_back( { p.m_kind, p.m_pos, 0.0f, m_rng.range( 0, 6 ) } );
    for ( const HazardSpot &h : track.m_hazards )
    {
        if ( h.m_kind == "barrel" )
        {
            m_barrels.push_back( { h.m_pos, 12.0f, false } );
            continue;
        }
        Hazard z;
        z.m_pos = h.m_pos;
        z.m_r = h.m_r;
        z.m_life = 1e9f;
        z.m_arm = 0.0f;
        if ( h.m_kind == "mine" )
        {
            z.m_kind = Hazard::MINE;
            z.m_damage = gearVal( GearE::MINE, []( const GearDef &g ) { return g.m_damage; }, 40.0f );
            z.m_radius = gearVal( GearE::MINE, []( const GearDef &g ) { return g.m_radius; }, 4.0f );
            z.m_r = 0.6f;
        }
        else
            z.m_kind = Hazard::OIL;
        m_hazards.push_back( z );
    }
    places();
}

void World::event( EvE type, int car, Vector2 pos, float power, int kind, int other, const std::string &text )
{
    Event e;
    e.m_type = type;
    e.m_car = car;
    e.m_other = other;
    e.m_pos = pos;
    e.m_power = power;
    e.m_kind = kind;
    e.m_text = text;
    m_events.push_back( e );
}

void World::step( const std::vector<CarInput> &inputs )
{
    m_events.clear();
    if ( m_phase == PhaseE::COUNTDOWN )
    {
        int before = (int)std::ceil( m_countdown - 0.5f );
        m_countdown -= Dt;
        int after = (int)std::ceil( m_countdown - 0.5f );
        if ( after != before && after >= 1 && after <= 3 ) event( EvE::COUNTDOWN, -1, { 0, 0 }, 1, after );
        for ( size_t i = 0; i < m_cars.size(); ++i )
        {
            // Revving on the grid.
            Car  &c = m_cars[i];
            float t = c.m_setup.m_human && i < inputs.size() ? inputs[i].m_throttle : m_rng.range( 0.2f, 0.7f );
            c.m_rpm += ( 0.15f + 0.7f * t - c.m_rpm ) * 0.1f;
            c.m_load = t;
        }
        if ( m_countdown <= 0.5f )
        {
            m_phase = PhaseE::RACE;
            event( EvE::GO, -1, { 0, 0 } );
        }
        return;
    }
    m_time += Dt;
    if ( m_phase == PhaseE::DONE ) m_doneTime += Dt;
    for ( size_t i = 0; i < m_cars.size(); ++i )
    {
        Car     &c = m_cars[i];
        CarInput in = c.m_setup.m_human && !c.m_finished && i < inputs.size() ? inputs[i] : ai( (int)i );
        if ( m_noAi && !c.m_setup.m_human ) in = CarInput();
        stepCar( (int)i, in );
    }
    for ( size_t a = 0; a < m_cars.size(); ++a )
        for ( size_t b = a + 1; b < m_cars.size(); ++b ) collide( (int)a, (int)b );
    bullets();
    rockets();
    hazards();
    pickups();
    movers();
    for ( size_t i = 0; i < m_cars.size(); ++i ) progress( (int)i );
    places();
    if ( m_phase == PhaseE::RACE )
    {
        int alive = 0, running = 0;
        for ( const Car &c : m_cars )
        {
            if ( !c.m_wrecked ) ++alive;
            if ( c.racing() ) ++running;
        }
        bool done = m_mode == ModeE::DERBY ? alive <= 1 || m_time > 240.0f : running == 0;
        if ( done )
        {
            m_phase = PhaseE::DONE;
            m_doneTime = 0;
        }
    }
}

bool World::humansDone() const
{
    if ( m_phase == PhaseE::DONE ) return true;
    bool any = false;
    for ( const Car &c : m_cars )
        if ( c.m_setup.m_human )
        {
            any = true;
            if ( m_mode == ModeE::DERBY ? !c.m_wrecked : c.racing() ) return false;
        }
    return any;
}

void World::stepCar( int i, const CarInput &inRaw )
{
    Car     &c = m_cars[i];
    CarInput in = inRaw;
    bool     hornBefore = c.m_in.m_horn;
    c.m_in = in;
    if ( in.m_horn && !hornBefore ) event( EvE::HORN, i, c.m_pos );
    if ( c.m_wrecked )
    {
        c.m_wreckTime += Dt;
        c.m_vel = mul( c.m_vel, 0.94f );
        c.m_yaw *= 0.9f;
        c.m_pos = add( c.m_pos, mul( c.m_vel, Dt ) );
        c.m_head = wrapRad( c.m_head + c.m_yaw * Dt );
        walls( c, i );
        c.m_rpm *= 0.95f;
        c.m_load = 0;
        c.m_skid = 0;
        c.m_firing = false;
        return;
    }
    if ( c.m_def->m_regen > 0 ) c.m_hp = std::min( c.m_maxHp, c.m_hp + c.m_def->m_regen * Dt );
    for ( float *t : { &c.m_oilSlip, &c.m_spiked, &c.m_smoked, &c.m_nitroTime, &c.m_dropTimer, &c.m_rocketTimer } )
        *t = std::max( 0.0f, *t - Dt );
    c.m_hitFlash = std::max( 0.0f, c.m_hitFlash - Dt * 4 );
    c.m_scrape = std::max( 0.0f, c.m_scrape - Dt * 6 );
    c.m_surface = m_track->surfaceAt( c.m_pos, c.m_hint );
    {
        TrackPos           tp = m_track->locate( c.m_pos, c.m_hint );
        const TrackSample &t = m_track->m_s[tp.m_i];
        c.m_offRoad = std::fabs( tp.m_lat ) > t.m_half + 0.3f;
    }
    for ( int k = 0; k < 2; ++k )
    {
        physics( c, in, Dt * 0.5f );
        walls( c, i );
        obstacles( c, i );
    }
    weapons( i, in );
}

void World::physics( Car &c, const CarInput &in, float dt )
{
    Vector2 f = c.fwd(), r = c.right();
    float   vl = dot( c.m_vel, f ), vlat = dot( c.m_vel, r );
    float   gS = surfaceGrip( c.m_surface );
    if ( surfaceLoose( c.m_surface ) ) gS = std::min( 1.05f, gS * c.m_offroad );
    else gS *= 1.0f - 0.16f * m_wetness;   // rain that came during the race
    if ( c.m_oilSlip > 0 ) gS *= 0.18f;
    if ( c.m_spiked > 0 ) gS *= 0.55f;
    float drag = surfaceDrag( c.m_surface ) + ( c.m_spiked > 0 ? 0.12f : 0.0f );
    bool  nitro = c.m_nitroTime > 0;
    float top = c.m_top * ( nitro ? 1.18f : 1.0f ) * ( c.m_spiked > 0 ? 0.8f : 1.0f );
    float a = 0;
    float thr = clampf( in.m_throttle, 0, 1 ), brk = clampf( in.m_brake, 0, 1 );
    if ( thr > 0 )
    {
        if ( vl < -0.5f )
            a += c.m_brakeF * BrakeAccel * thr;
        else
            a += c.m_power * thr * std::max( 0.0f, 1.0f - ( vl / top ) * ( vl / top ) ) * ( 0.55f + 0.45f * std::min( gS, 1.0f ) );
    }
    if ( nitro )
    {
        float boost = gearVal( GearE::NITRO, []( const GearDef &g ) { return g.m_boost; }, 12.0f );
        a += boost * std::max( 0.0f, 1.0f - std::pow( std::max( vl, 0.0f ) / top, 4.0f ) );
    }
    if ( brk > 0 )
    {
        if ( vl > 0.5f )
            a -= c.m_brakeF * BrakeAccel * brk * ( 0.45f + 0.55f * std::min( gS, 1.0f ) );
        else
        {
            float rtop = top * 0.35f;
            a -= c.m_power * 0.6f * brk * std::max( 0.0f, 1.0f - ( vl / rtop ) * ( vl / rtop ) );
        }
    }
    a -= vl * ( 0.035f + drag );
    if ( thr <= 0 && brk <= 0 ) a -= ( vl > 0 ? 1.0f : -1.0f ) * std::min( std::fabs( vl ) / dt, 1.4f );
    if ( in.m_handbrake ) a -= ( vl > 0 ? 1.0f : -1.0f ) * std::min( std::fabs( vl ) / dt, 3.5f );
    vl += a * dt;
    float maxLat = LatAccel * c.m_grip * gS * ( in.m_handbrake ? 0.35f : 1.0f );
    float dl = clampf( vlat, -maxLat * dt, maxLat * dt );
    vlat -= dl;
    float slide = std::fabs( vlat );
    float spin = ( thr > 0.9f && vl < 12.0f && vl > -1 && gS < 0.95f ) ? 0.5f : 0.0f;
    float skid = clampf( ( slide - 1.5f ) / 7.0f, 0, 1 ) + spin + ( in.m_handbrake && std::fabs( vl ) > 6 ? 0.6f : 0.0f );
    if ( brk > 0.6f && vl > 18 && gS < 0.9f ) skid += 0.3f;
    c.m_skid += ( clampf( skid, 0, 1 ) - c.m_skid ) * std::min( 1.0f, dt * 12 );
    c.m_vel = add( mul( f, vl ), mul( r, vlat ) );
    // Steering: quick at low speed, calmer at full speed, held to what the tyres can carry (the handbrake lets go).
    float sp = std::fabs( vl );
    float sf = clampf( sp / 6.0f, 0, 1 ) * ( 1.0f - 0.45f * clampf( sp / ( c.m_top * 1.1f ), 0, 1 ) );
    float dir = vl >= -0.3f ? 1.0f : -1.0f;
    float target = clampf( in.m_steer, -1, 1 ) * SteerRate * c.m_steerF * sf * dir * ( in.m_handbrake ? 1.5f : 1.0f );
    if ( !in.m_handbrake && sp > 4 )
    {
        float lim = LatAccel * c.m_grip * gS * 1.2f / sp + 0.25f;
        target = clampf( target, -lim, lim );
    }
    float resp = clampf( dt * 9.0f * ( 0.35f + 0.65f * std::min( gS, 1.0f ) ), 0, 1 );
    c.m_yaw += ( target - c.m_yaw ) * resp;
    c.m_head = wrapRad( c.m_head + c.m_yaw * dt );
    c.m_pos = add( c.m_pos, mul( c.m_vel, dt ) );
    // What it feels like.
    c.m_steerVis += ( in.m_steer - c.m_steerVis ) * std::min( 1.0f, dt * 10 );
    c.m_wheelTurn += vl * dt / 0.33f;
    c.m_lean += ( clampf( vl * c.m_yaw / 18.0f, -1, 1 ) - c.m_lean ) * std::min( 1.0f, dt * 6 );
    c.m_pitch += ( clampf( -a / 20.0f, -1, 1 ) - c.m_pitch ) * std::min( 1.0f, dt * 6 );
    c.m_throttle = thr;
    c.m_brakeLight = brk > 0.1f && vl > 0.5f ? 1.0f : 0.0f;
    // The engine: five gears over the top speed, the revs falling at each change.
    float share = clampf( std::fabs( vl ) / std::max( 1.0f, c.m_top ), 0, 1.2f );
    int   gear = std::clamp( 1 + (int)( share * 5.0f ), 1, 5 );
    float inGear = share * 5.0f - ( gear - 1 );
    float rpm = gear == 1 ? 0.15f + 0.8f * clampf( inGear, 0, 1 ) : 0.45f + 0.5f * clampf( inGear, 0, 1 );
    if ( spin > 0 || ( c.m_skid > 0.5f && thr > 0.5f ) ) rpm = std::min( 1.0f, rpm + 0.25f );
    if ( thr > 0.1f && sp < 1.0f ) rpm = std::max( rpm, 0.2f + 0.3f * thr );
    c.m_gear = gear;
    c.m_rpm += ( rpm - c.m_rpm ) * std::min( 1.0f, dt * 14 );
    c.m_load += ( std::max( thr, nitro ? 1.0f : 0.0f ) - c.m_load ) * std::min( 1.0f, dt * 10 );
}

void World::walls( Car &c, int i )
{
    Vector2 f = c.fwd(), r = c.right();
    float   hl = c.m_len * 0.5f * 0.94f, hw = c.m_wid * 0.5f * 0.95f;
    const Vector2 pts[8] = { { hw, hl }, { -hw, hl }, { hw, -hl }, { -hw, -hl }, { hw, 0 }, { -hw, 0 }, { 0, hl }, { 0, -hl } };
    float invM = 1.0f / ( c.m_mass * ( c.m_wrecked ? 2.5f : 1.0f ) ), invI = 1.0f / ( c.m_inertia * ( c.m_wrecked ? 2.5f : 1.0f ) );
    for ( const Vector2 &q : pts )
    {
        Vector2 rc = add( mul( r, q.x ), mul( f, q.y ) );
        Vector2 p = add( c.m_pos, rc );
        Vector2 push;
        float   ex = m_track->outside( p, push );
        if ( ex <= 0 ) continue;
        c.m_pos = add( c.m_pos, mul( push, ex ) );
        Vector2 vc = add( c.m_vel, mul( perp( rc ), c.m_yaw ) );
        float   vn = dot( vc, push );
        if ( vn >= 0 ) continue;
        float rn = cross( rc, push );
        float j = -( 1.0f + 0.15f ) * vn / ( invM + rn * rn * invI );
        c.m_vel = add( c.m_vel, mul( push, j * invM ) );
        c.m_yaw += rn * j * invI;
        Vector2 t = perp( push );
        float   vt = dot( vc, t );
        float   rt = cross( rc, t );
        float   jt = clampf( -vt / ( invM + rt * rt * invI ), -0.3f * j, 0.3f * j );
        c.m_vel = add( c.m_vel, mul( t, jt * invM ) );
        c.m_yaw += rt * jt * invI;
        if ( std::fabs( vt ) > 3 ) c.m_scrape = 1.0f;
        float impact = -vn;
        if ( impact > 7.0f && !c.m_wrecked )
        {
            m_source = 3;
            damage( i, ( impact - 7.0f ) * 0.8f, -1, p );
            event( EvE::WALL, i, p, impact );
        }
        else if ( std::fabs( vt ) > 6 && m_rng.chance( 0.2f ) )
            event( EvE::SCRAPE, i, p, std::fabs( vt ) );
    }
}

void World::obstacles( Car &c, int i )
{
    Vector2 f = c.fwd(), r = c.right();
    float   hl = c.m_len * 0.5f, hw = c.m_wid * 0.5f;
    const Vector2 pts[8] = { { hw, hl }, { -hw, hl }, { hw, -hl }, { -hw, -hl }, { hw, 0 }, { -hw, 0 }, { 0, hl }, { 0, -hl } };
    auto resolve = [&]( Vector2 o, float rad ) {
        if ( dist2( o, c.m_pos ) > ( rad + hl + 1 ) * ( rad + hl + 1 ) ) return false;
        bool hit = false;
        for ( const Vector2 &q : pts )
        {
            Vector2 rc = add( mul( r, q.x ), mul( f, q.y ) );
            Vector2 p = add( c.m_pos, rc );
            float   d = len( sub( p, o ) ) - rad;
            if ( d >= 0 ) continue;
            hit = true;
            Vector2 push = norm( sub( p, o ) );
            c.m_pos = add( c.m_pos, mul( push, -d ) );
            Vector2 vc = add( c.m_vel, mul( perp( rc ), c.m_yaw ) );
            float   vn = dot( vc, push );
            if ( vn >= 0 ) continue;
            float invM = 1.0f / c.m_mass, invI = 1.0f / c.m_inertia, rn = cross( rc, push );
            float j = -1.2f * vn / ( invM + rn * rn * invI );
            c.m_vel = add( c.m_vel, mul( push, j * invM ) );
            c.m_yaw += rn * j * invI;
            if ( -vn > 7.0f && !c.m_wrecked )
            {
                m_source = 3;
                damage( i, ( -vn - 7.0f ) * 0.8f, -1, p );
                event( EvE::WALL, i, p, -vn );
            }
        }
        return hit;
    };
    for ( const ObstacleSpot &o : m_track->m_obstacles ) resolve( o.m_pos, o.m_r );
    for ( const Rock &rock : m_rocks )
        if ( rock.m_age >= 0 ) resolve( rock.m_pos, rock.m_r );
    for ( Barrel &b : m_barrels )
        if ( !b.m_gone && resolve( b.m_pos, 0.6f ) && !c.m_wrecked )
        {
            b.m_gone = true;
            event( EvE::BARREL, i, b.m_pos );
            explode( b.m_pos, 7.0f, 35.0f, i );
        }
}

void World::collide( int ia, int ib )
{
    Car &A = m_cars[ia], &B = m_cars[ib];
    if ( dist2( A.m_pos, B.m_pos ) > 64.0f ) return;
    Vector2 fa = A.fwd(), ra = A.right(), fb = B.fwd(), rb = B.right();
    Vector2 d = sub( B.m_pos, A.m_pos );
    float   ha = A.m_len * 0.5f, wa = A.m_wid * 0.5f, hb = B.m_len * 0.5f, wb = B.m_wid * 0.5f;
    Vector2 axes[4] = { fa, ra, fb, rb };
    float   best = 1e9f;
    Vector2 n = { 1, 0 };
    for ( Vector2 ax : axes )
    {
        float pa = ha * std::fabs( dot( fa, ax ) ) + wa * std::fabs( dot( ra, ax ) );
        float pb = hb * std::fabs( dot( fb, ax ) ) + wb * std::fabs( dot( rb, ax ) );
        float dd = dot( d, ax );
        float ov = pa + pb - std::fabs( dd );
        if ( ov <= 0 ) return;
        if ( ov < best )
        {
            best = ov;
            n = dd >= 0 ? ax : mul( ax, -1.0f );
        }
    }
    // The contact: between B's corner deepest in A and A's corner deepest in B.
    auto corner = [&]( const Car &c, Vector2 f, Vector2 r, float h, float w, float sign ) {
        Vector2 bestC = c.m_pos;
        float   bestD = 1e9f;
        for ( float sx : { -1.0f, 1.0f } )
            for ( float sy : { -1.0f, 1.0f } )
            {
                Vector2 p = add( c.m_pos, add( mul( f, sy * h ), mul( r, sx * w ) ) );
                float   v = dot( p, n ) * sign;
                if ( v < bestD )
                {
                    bestD = v;
                    bestC = p;
                }
            }
        return bestC;
    };
    Vector2 cp = mul( add( corner( B, fb, rb, hb, wb, 1.0f ), corner( A, fa, ra, ha, wa, -1.0f ) ), 0.5f );
    float   mA = A.m_mass * ( A.m_wrecked ? 2.5f : 1.0f ), mB = B.m_mass * ( B.m_wrecked ? 2.5f : 1.0f );
    float   imA = 1.0f / mA, imB = 1.0f / mB, iiA = 1.0f / ( A.m_inertia * mA / A.m_mass ), iiB = 1.0f / ( B.m_inertia * mB / B.m_mass );
    A.m_pos = sub( A.m_pos, mul( n, best * imA / ( imA + imB ) ) );
    B.m_pos = add( B.m_pos, mul( n, best * imB / ( imA + imB ) ) );
    Vector2 rA = sub( cp, A.m_pos ), rB = sub( cp, B.m_pos );
    Vector2 vA = add( A.m_vel, mul( perp( rA ), A.m_yaw ) ), vB = add( B.m_vel, mul( perp( rB ), B.m_yaw ) );
    float   vrel = dot( sub( vB, vA ), n );
    if ( vrel >= 0 ) return;
    float cnA = cross( rA, n ), cnB = cross( rB, n );
    float k = imA + imB + cnA * cnA * iiA + cnB * cnB * iiB;
    float j = -( 1.0f + 0.25f ) * vrel / k;
    A.m_vel = sub( A.m_vel, mul( n, j * imA ) );
    A.m_yaw -= cnA * j * iiA;
    B.m_vel = add( B.m_vel, mul( n, j * imB ) );
    B.m_yaw += cnB * j * iiB;
    Vector2 t = perp( n );
    float   vt = dot( sub( vB, vA ), t );
    float   ctA = cross( rA, t ), ctB = cross( rB, t );
    float   jt = clampf( -vt / ( imA + imB + ctA * ctA * iiA + ctB * ctB * iiB ), -0.3f * j, 0.3f * j );
    A.m_vel = sub( A.m_vel, mul( t, jt * imA ) );
    A.m_yaw -= ctA * jt * iiA;
    B.m_vel = add( B.m_vel, mul( t, jt * imB ) );
    B.m_yaw += ctB * jt * iiB;
    float impact = -vrel;
    if ( impact < 6.0f ) return;
    float base = ( impact - 6.0f ) * 0.8f;
    bool  frontA = dot( fa, n ) > 0.6f, frontB = dot( fb, mul( n, -1.0f ) ) > 0.6f;
    float ramMult = gearVal( GearE::RAM, []( const GearDef &g ) { return g.m_mult; }, 2.0f );
    float ramSelf = gearVal( GearE::RAM, []( const GearDef &g ) { return g.m_self; }, 0.5f );
    float toB = base * 2.0f * A.m_mass / ( A.m_mass + B.m_mass ) * ( frontA && A.m_setup.m_ram ? ramMult : 1.0f ) *
                ( frontB ? ( B.m_setup.m_ram ? ramSelf : 0.7f ) : 1.0f ) * B.m_def->m_ramproof;
    float toA = base * 2.0f * B.m_mass / ( A.m_mass + B.m_mass ) * ( frontB && B.m_setup.m_ram ? ramMult : 1.0f ) *
                ( frontA ? ( A.m_setup.m_ram ? ramSelf : 0.7f ) : 1.0f ) * A.m_def->m_ramproof;
    m_source = 2;
    if ( !A.m_wrecked ) damage( ia, toA, B.m_wrecked ? -1 : ib, cp );
    m_source = 2;
    if ( !B.m_wrecked ) damage( ib, toB, A.m_wrecked ? -1 : ia, cp );
    // A wreck still burning: the fire jumps over.
    m_source = 5;
    if ( A.m_wrecked && A.m_wreckTime < 25.0f && !B.m_wrecked ) damage( ib, 4.0f, -1, cp );
    if ( B.m_wrecked && B.m_wreckTime < 25.0f && !A.m_wrecked ) damage( ia, 4.0f, -1, cp );
    event( EvE::CRASH, ia, cp, impact, 0, ib );
}

void World::weapons( int i, const CarInput &in )
{
    Car &c = m_cars[i];
    const GunDef &g = *c.m_gun;
    c.m_firing = false;
    bool flame = g.m_heat > 0;
    if ( flame ) c.m_heat = std::max( 0.0f, c.m_heat - Dt * 0.35f );
    float t = c.m_def->m_upGun > 0 ? clampf( c.m_setup.m_gunLv / (float)c.m_def->m_upGun, 0, 1 ) : 0.0f;
    c.m_fireTimer = std::max( -0.05f, c.m_fireTimer - Dt );
    bool canFire = flame ? c.m_heat < 1.0f && !( c.m_heat > 0.7f && !c.m_in.m_fire ) : c.m_ammo >= 1.0f;
    if ( in.m_fire && canFire && m_phase != PhaseE::COUNTDOWN )
    {
        c.m_firing = true;
        float rate = g.m_rate.at( t );
        int   guard = 0;
        while ( c.m_fireTimer <= 0 && guard++ < 4 )
        {
            c.m_fireTimer += 1.0f / std::max( 1.0f, rate );
            Bullet b;
            b.m_owner = i;
            b.m_kind = g.m_kind == "flame" ? 1 : g.m_kind == "shell" ? 2 : 0;
            float side = ( g.m_name.find( "twin" ) != std::string::npos ) ? ( ( c.m_shots % 2 ) ? 0.5f : -0.5f ) : 0.0f;
            Vector2 muzzle = add( add( c.m_pos, mul( c.fwd(), c.m_len * 0.5f + 0.3f ) ), mul( c.right(), side ) );
            // Computer drivers aim worse the less skilled they are.
            float aimErr = c.m_setup.m_human ? 0.0f : ( 1.0f - c.m_setup.m_skill ) * 4.0f + 1.0f + c.m_setup.m_novice * 6.0f;
            float ang = c.m_head + ( m_rng.range( -g.m_spread, g.m_spread ) + m_rng.range( -aimErr, aimErr ) ) * Deg;
            float   speed = g.m_speed * ( b.m_kind == 1 ? m_rng.range( 0.8f, 1.1f ) : 1.0f );
            b.m_pos = muzzle;
            b.m_vel = add( mul( headingDir( ang ), speed ), c.m_vel );
            b.m_life = g.m_range / std::max( 1.0f, speed );
            b.m_damage = g.m_damage.at( t ) * c.m_dmgDealt;
            b.m_color = g.m_color;
            m_bullets.push_back( b );
            ++c.m_shots;
            if ( flame )
                c.m_heat += 1.0f / std::max( 1.0f, g.m_heat * rate );
            else
                c.m_ammo -= 1.0f;
            if ( !flame || c.m_shots % 6 == 0 ) event( flame ? EvE::FLAME : EvE::SHOT, i, muzzle, 1, b.m_kind );
        }
    }
    // The rear gear.
    auto nextRear = [&]( int from ) {
        for ( int k = 1; k <= (int)RearE::COUNT; ++k )
        {
            int s = ( from + k ) % (int)RearE::COUNT;
            if ( c.m_rear[s] > 0 ) return s;
        }
        return from;
    };
    if ( in.m_cycle ) c.m_rearSel = nextRear( c.m_rearSel );
    if ( c.m_rear[c.m_rearSel] <= 0 ) c.m_rearSel = nextRear( c.m_rearSel );
    if ( in.m_drop && c.m_dropTimer <= 0 && c.m_rear[c.m_rearSel] > 0 )
    {
        c.m_dropTimer = 0.35f;
        --c.m_rear[c.m_rearSel];
        Hazard z;
        z.m_owner = i;
        z.m_pos = sub( c.m_pos, mul( c.fwd(), c.m_len * 0.5f + 1.3f ) );
        z.m_dir = c.right();
        switch ( (RearE)c.m_rearSel )
        {
        case RearE::MINE:
        {
            const GearDef *gd = gearOf( GearE::MINE );
            z.m_kind = Hazard::MINE;
            z.m_r = 0.6f;
            z.m_arm = gd ? gd->m_arm : 0.6f;
            z.m_life = gd ? gd->m_life : 90.0f;
            z.m_damage = gd ? gd->m_damage : 40.0f;
            z.m_radius = gd ? gd->m_radius : 4.0f;
            event( EvE::MINE_DROP, i, z.m_pos );
            break;
        }
        case RearE::SPIKES:
        {
            const GearDef *gd = gearOf( GearE::SPIKES );
            z.m_kind = Hazard::SPIKES;
            z.m_r = 3.2f;
            z.m_life = gd ? gd->m_life : 40.0f;
            z.m_damage = gd ? gd->m_damage : 10.0f;
            z.m_slow = gd ? gd->m_slow : 3.0f;
            z.m_arm = 0.3f;
            event( EvE::SPIKES, i, z.m_pos );
            break;
        }
        case RearE::OIL:
        {
            const GearDef *gd = gearOf( GearE::OIL );
            z.m_kind = Hazard::OIL;
            z.m_r = 3.4f;
            z.m_life = ( gd ? gd->m_life : 25.0f ) * ( c.m_def->m_slick ? 2.0f : 1.0f );
            z.m_arm = 0.4f;
            event( EvE::OIL, i, z.m_pos );
            break;
        }
        default:
        {
            const GearDef *gd = gearOf( GearE::SMOKE );
            z.m_kind = Hazard::SMOKE;
            z.m_r = 7.0f;
            z.m_life = gd ? gd->m_life : 8.0f;
            z.m_arm = 0.0f;
            event( EvE::SMOKE, i, z.m_pos );
            break;
        }
        }
        m_hazards.push_back( z );
    }
    if ( in.m_rocket && c.m_rockets > 0 && c.m_rocketTimer <= 0 )
    {
        const GearDef *gd = gearOf( GearE::ROCKET );
        c.m_rocketTimer = 0.6f;
        --c.m_rockets;
        Rocket rk;
        rk.m_owner = i;
        rk.m_head = c.m_head;
        rk.m_pos = add( c.m_pos, mul( c.fwd(), c.m_len * 0.5f + 0.5f ) );
        rk.m_damage = ( gd ? gd->m_damage : 55.0f ) * c.m_dmgDealt;
        rk.m_radius = gd ? gd->m_radius : 5.0f;
        rk.m_speed = gd ? gd->m_speed : 75.0f;
        rk.m_turn = ( gd ? gd->m_turn : 110.0f ) * Deg;
        rk.m_vel = add( mul( c.fwd(), 25.0f ), c.m_vel );
        float cone = ( gd ? gd->m_cone : 30.0f ) * Deg, bestScore = 1e9f;
        for ( size_t k = 0; k < m_cars.size(); ++k )
        {
            const Car &o = m_cars[k];
            if ( (int)k == i || o.m_wrecked ) continue;
            Vector2 d = sub( o.m_pos, c.m_pos );
            float   dl = len( d );
            if ( dl > 140 ) continue;
            float ang = std::fabs( wrapRad( headingOf( d ) - c.m_head ) );
            if ( ang > cone ) continue;
            float score = dl * ( 1.0f + ang * 3.0f );
            if ( score < bestScore )
            {
                bestScore = score;
                rk.m_target = (int)k;
            }
        }
        m_rockets.push_back( rk );
        event( EvE::ROCKET, i, rk.m_pos, 1, 0, rk.m_target );
    }
    if ( in.m_nitro && c.m_nitro > 0 && c.m_nitroTime <= 0 )
    {
        --c.m_nitro;
        c.m_nitroTime = gearVal( GearE::NITRO, []( const GearDef &g ) { return g.m_dur; }, 2.2f );
        event( EvE::NITRO, i, c.m_pos );
    }
}

void World::bullets()
{
    for ( Bullet &b : m_bullets )
    {
        b.m_age += Dt;
        Vector2 from = b.m_pos, to = add( b.m_pos, mul( b.m_vel, Dt ) );
        b.m_pos = to;
        if ( b.m_kind == 1 ) b.m_vel = mul( b.m_vel, 0.93f );
        bool gone = b.m_age >= b.m_life;
        for ( size_t k = 0; k < m_cars.size() && !gone; ++k )
        {
            Car &c = m_cars[k];
            if ( (int)k == b.m_owner ) continue;
            if ( dist2( c.m_pos, to ) > 60.0f && dist2( c.m_pos, from ) > 60.0f ) continue;
            float t = segBox( local( c, from ), local( c, to ), c.m_wid * 0.5f, c.m_len * 0.5f );
            if ( t < 0 ) continue;
            Vector2 at = add( from, mul( sub( to, from ), t ) );
            if ( !c.m_wrecked )
            {
                // Bullets lose their bite towards the end of their range.
                float bite = b.m_kind == 2 ? 1.0f : clampf( 1.3f - b.m_age / std::max( 0.01f, b.m_life ), 0.45f, 1.0f );
                m_source = 0;
                damage( (int)k, b.m_damage * bite, b.m_owner, at );
                if ( b.m_owner >= 0 ) ++m_cars[b.m_owner].m_hits;
                c.m_vel = add( c.m_vel, mul( norm( b.m_vel ), b.m_kind == 2 ? 1.5f : 0.08f ) );
            }
            event( EvE::HIT, (int)k, at, b.m_damage, b.m_kind, b.m_owner );
            gone = true;
            if ( b.m_kind == 2 ) explode( at, 2.5f, b.m_damage * 0.3f, b.m_owner );
        }
        if ( !gone )
            for ( Barrel &br : m_barrels )
                if ( !br.m_gone && dist2( br.m_pos, to ) < 0.6f )
                {
                    br.m_hp -= b.m_damage;
                    gone = true;
                    if ( br.m_hp <= 0 )
                    {
                        br.m_gone = true;
                        event( EvE::BARREL, b.m_owner, br.m_pos );
                        explode( br.m_pos, 7.0f, 35.0f, b.m_owner );
                    }
                }
        if ( !gone )
            for ( Hazard &h : m_hazards )
                if ( !h.m_gone && h.m_kind == Hazard::MINE && dist2( h.m_pos, to ) < 0.5f )
                {
                    h.m_gone = true;
                    gone = true;
                    explode( h.m_pos, h.m_radius, h.m_damage, b.m_owner );
                }
        if ( !gone )
        {
            Vector2 push;
            if ( m_track->outside( to, push ) > 0.4f )
            {
                gone = true;
                if ( b.m_kind != 1 ) event( EvE::RICOCHET, b.m_owner, to );
                if ( b.m_kind == 2 ) explode( to, 2.5f, b.m_damage * 0.3f, b.m_owner );
            }
            for ( const ObstacleSpot &o : m_track->m_obstacles )
                if ( !gone && dist2( o.m_pos, to ) < o.m_r * o.m_r )
                {
                    gone = true;
                    event( EvE::RICOCHET, b.m_owner, to );
                }
        }
        if ( gone ) b.m_life = -1;
    }
    m_bullets.erase( std::remove_if( m_bullets.begin(), m_bullets.end(), []( const Bullet &b ) { return b.m_life < 0; } ),
                     m_bullets.end() );
}

void World::rockets()
{
    for ( Rocket &r : m_rockets )
    {
        r.m_life -= Dt;
        if ( r.m_target >= 0 && !m_cars[r.m_target].m_wrecked )
        {
            float want = headingOf( sub( m_cars[r.m_target].m_pos, r.m_pos ) );
            float d = wrapRad( want - r.m_head );
            r.m_head = wrapRad( r.m_head + clampf( d, -r.m_turn * Dt, r.m_turn * Dt ) );
        }
        float sp = std::min( r.m_speed, len( r.m_vel ) + 90.0f * Dt );
        r.m_vel = mul( headingDir( r.m_head ), sp );
        r.m_pos = add( r.m_pos, mul( r.m_vel, Dt ) );
        bool boom = r.m_life <= 0;
        for ( size_t k = 0; k < m_cars.size() && !boom; ++k )
        {
            if ( (int)k == r.m_owner && r.m_life > 3.6f ) continue;
            Vector2 q = local( m_cars[k], r.m_pos );
            if ( std::fabs( q.x ) < m_cars[k].m_wid * 0.5f + 0.4f && std::fabs( q.y ) < m_cars[k].m_len * 0.5f + 0.4f ) boom = true;
        }
        Vector2 push;
        if ( !boom && m_track->outside( r.m_pos, push ) > 0.3f ) boom = true;
        for ( const ObstacleSpot &o : m_track->m_obstacles )
            if ( !boom && dist2( o.m_pos, r.m_pos ) < o.m_r * o.m_r ) boom = true;
        if ( boom )
        {
            explode( r.m_pos, r.m_radius, r.m_damage, r.m_owner );
            r.m_life = -100;
        }
    }
    m_rockets.erase( std::remove_if( m_rockets.begin(), m_rockets.end(), []( const Rocket &r ) { return r.m_life <= -100; } ),
                     m_rockets.end() );
}

void World::hazards()
{
    std::vector<Hazard> blasts;
    for ( size_t hi = 0; hi < m_hazards.size(); ++hi )
    {
        Hazard &h = m_hazards[hi];
        if ( h.m_gone ) continue;
        h.m_age += Dt;
        if ( h.m_age > h.m_life )
        {
            h.m_gone = true;
            continue;
        }
        bool armed = h.m_age >= h.m_arm;
        if ( h.m_kind == Hazard::MINE && armed && h.m_owner >= 0 && h.m_age - Dt < h.m_arm )
            event( EvE::MINE_ARM, h.m_owner, h.m_pos );
        for ( size_t k = 0; k < m_cars.size() && !h.m_gone; ++k )
        {
            Car &c = m_cars[k];
            if ( c.m_wrecked && h.m_kind != Hazard::MINE ) continue;
            float d = len( sub( c.m_pos, h.m_pos ) );
            switch ( h.m_kind )
            {
            case Hazard::MINE:
                if ( armed && d < c.m_wid * 0.5f + 1.0f && ( (int)k != h.m_owner || h.m_age > 2.5f ) )
                {
                    h.m_gone = true;
                    blasts.push_back( h );
                }
                break;
            case Hazard::OIL:
                if ( armed && d < h.m_r )
                {
                    if ( c.m_oilSlip <= 0 ) event( EvE::OIL, (int)k, c.m_pos, 1, 1, h.m_owner );
                    c.m_oilSlip = 0.7f;
                }
                break;
            case Hazard::SPIKES:
            {
                Vector2 a = sub( h.m_pos, mul( h.m_dir, h.m_r ) ), b = add( h.m_pos, mul( h.m_dir, h.m_r ) );
                Vector2 ab = sub( b, a );
                float   t = clampf( dot( sub( c.m_pos, a ), ab ) / std::max( 1e-6f, dot( ab, ab ) ), 0, 1 );
                float   dd = len( sub( c.m_pos, add( a, mul( ab, t ) ) ) );
                if ( armed && dd < 1.4f && c.m_spiked <= 0.0f )
                {
                    c.m_spiked = h.m_slow;
                    m_source = 4;
                    damage( (int)k, h.m_damage, h.m_owner, c.m_pos );
                    event( EvE::TIRE_BURST, (int)k, c.m_pos, 1, 0, h.m_owner );
                    h.m_life = std::min( h.m_life, h.m_age + 6.0f );
                }
                break;
            }
            case Hazard::SMOKE:
                if ( d < h.m_r * std::min( 1.0f, 0.4f + h.m_age ) ) c.m_smoked = 0.6f;
                break;
            }
        }
    }
    for ( const Hazard &h : blasts ) explode( h.m_pos, h.m_radius, h.m_damage, h.m_owner );
    m_hazards.erase( std::remove_if( m_hazards.begin(), m_hazards.end(), []( const Hazard &h ) { return h.m_gone; } ),
                     m_hazards.end() );
}

void World::pickups()
{
    for ( Pickup &p : m_pickups )
    {
        p.m_spin += Dt;
        if ( p.m_respawn > 0 )
        {
            p.m_respawn -= Dt;
            continue;
        }
        for ( size_t k = 0; k < m_cars.size(); ++k )
        {
            Car &c = m_cars[k];
            if ( c.m_wrecked || dist2( c.m_pos, p.m_pos ) > 2.8f * 2.8f ) continue;
            p.m_respawn = 20.0f;
            std::string kind = p.m_kind;
            if ( kind == "mystery" )
            {
                static const char *luck[] = { "cash3", "nitro2", "mine3", "rocket2", "repair", "oil2", "smoke", "spikes", "curse", "ammo" };
                kind = luck[m_rng.below( 10 )];
            }
            if ( kind == "cash" ) c.m_cash += 1;
            else if ( kind == "cash3" ) c.m_cash += 3;
            else if ( kind == "wrench" ) c.m_hp = std::min( c.m_maxHp, c.m_hp + c.m_maxHp * 0.3f );
            else if ( kind == "repair" ) c.m_hp = c.m_maxHp;
            else if ( kind == "ammo" )
            {
                c.m_ammo = std::min( c.m_maxAmmo, c.m_ammo + c.m_maxAmmo * 0.4f );
                c.m_heat = 0;
            }
            else if ( kind == "nitro" ) c.m_nitro += 1;
            else if ( kind == "nitro2" ) c.m_nitro += 2;
            else if ( kind == "mine" ) c.m_rear[(int)RearE::MINE] += 2;
            else if ( kind == "mine3" ) c.m_rear[(int)RearE::MINE] += 3;
            else if ( kind == "rocket" ) c.m_rockets += 1;
            else if ( kind == "rocket2" ) c.m_rockets += 2;
            else if ( kind == "oil2" ) c.m_rear[(int)RearE::OIL] += 2;
            else if ( kind == "smoke" ) c.m_rear[(int)RearE::SMOKE] += 1;
            else if ( kind == "spikes" ) c.m_rear[(int)RearE::SPIKES] += 1;
            else if ( kind == "curse" )
            {
                // A gift from the other side: the car's own oil, right under it.
                Hazard z;
                z.m_kind = Hazard::OIL;
                z.m_pos = c.m_pos;
                z.m_r = 3.4f;
                z.m_life = 12.0f;
                m_hazards.push_back( z );
            }
            event( EvE::PICKUP, (int)k, p.m_pos, 1, 0, -1, kind );
            break;
        }
    }
}

void World::progress( int i )
{
    Car     &c = m_cars[i];
    TrackPos tp = m_track->locate( c.m_pos, c.m_hint );
    c.m_hint = tp.m_i;
    float ds = m_track->delta( c.m_s, tp.m_s );
    c.m_s = tp.m_s;
    if ( c.m_wrecked || c.m_finished || m_mode == ModeE::DERBY ) return;
    c.m_dist += ds;
    float L = m_track->m_length;
    int   lap = (int)std::floor( c.m_dist / L );
    if ( lap > c.m_lap && c.m_dist > 0 )
    {
        c.m_lap = lap;
        c.m_lastLap = m_time - c.m_lapStart;
        c.m_lapStart = m_time;
        if ( lap >= 2 || m_mode == ModeE::TRIAL )
        {
            if ( c.m_bestLap <= 0 || c.m_lastLap < c.m_bestLap ) c.m_bestLap = c.m_lastLap;
            if ( m_bestLap <= 0 || c.m_lastLap < m_bestLap )
            {
                m_bestLap = c.m_lastLap;
                m_bestLapCar = i;
            }
        }
        if ( c.m_lap >= m_laps )
        {
            c.m_finished = true;
            c.m_finishTime = m_time;
            m_finishOrder.push_back( i );
            event( EvE::FINISH, i, c.m_pos, 1, (int)m_finishOrder.size() );
        }
        else
        {
            event( EvE::LAP, i, c.m_pos, 1, c.m_lap );
            if ( c.m_lap == m_laps - 1 ) event( EvE::FINAL_LAP, i, c.m_pos );
        }
    }
    // Driving the wrong way round.
    Vector2 dir = m_track->m_s[tp.m_i].m_dir;
    if ( dot( c.m_vel, dir ) < -3.0f )
    {
        c.m_wrongWay += Dt;
        if ( c.m_wrongWay > 1.5f && c.m_wrongWay - Dt <= 1.5f ) event( EvE::WRONG_WAY, i, c.m_pos );
    }
    else
        c.m_wrongWay = 0;
}

std::vector<int> World::order() const
{
    std::vector<int> o( m_cars.size() );
    for ( size_t i = 0; i < o.size(); ++i ) o[i] = (int)i;
    std::stable_sort( o.begin(), o.end(), [&]( int a, int b ) {
        const Car &A = m_cars[a], &B = m_cars[b];
        if ( m_mode == ModeE::DERBY )
        {
            if ( A.m_wrecked != B.m_wrecked ) return !A.m_wrecked;
            if ( A.m_wrecked ) return A.m_wreckTime < B.m_wreckTime;   // the later wreck ranks higher
            return A.m_hp / A.m_maxHp > B.m_hp / B.m_maxHp;
        }
        if ( A.m_finished != B.m_finished ) return A.m_finished;
        if ( A.m_finished ) return A.m_finishTime < B.m_finishTime;
        if ( A.m_wrecked != B.m_wrecked ) return !A.m_wrecked;
        if ( A.m_wrecked ) return A.m_dist > B.m_dist;
        return A.m_dist > B.m_dist;
    } );
    return o;
}

void World::places()
{
    std::vector<int> o = order();
    for ( size_t p = 0; p < o.size(); ++p )
    {
        Car &c = m_cars[o[p]];
        int  place = (int)p + 1;
        if ( c.m_setup.m_human && c.m_place > 0 && place < c.m_place && m_phase == PhaseE::RACE && !c.m_wrecked &&
             p + 1 < o.size() )
            event( EvE::OVERTAKE, o[p], c.m_pos, 1, 0, o[p + 1] );
        c.m_place = place;
    }
    if ( !o.empty() ) m_leader = o[0];
}

void World::damage( int victim, float amount, int by, Vector2 at )
{
    Car &c = m_cars[victim];
    if ( c.m_wrecked || amount <= 0 ) return;
    amount *= c.m_dmgTaken;
    if ( by >= 0 && by != victim ) amount *= m_cars[by].m_dmgDealt;
    c.m_hp -= amount;
    c.m_damage += amount;
    m_dmgBy[std::clamp( m_source, 0, 5 )] += amount;
    c.m_lastSource = std::clamp( m_source, 0, 5 );
    c.m_hitFlash = 1.0f;
    if ( by >= 0 && by != victim )
    {
        c.m_lastHitBy = by;
        c.m_lastHitTime = m_time;
    }
    if ( c.m_hp <= 0 )
    {
        int killer = by >= 0 && by != victim ? by : ( m_time - c.m_lastHitTime < 6.0f ? c.m_lastHitBy : -1 );
        wreck( victim, killer );
    }
}

void World::explode( Vector2 at, float radius, float dmg, int by )
{
    event( EvE::EXPLODE, by, at, radius );
    for ( size_t k = 0; k < m_cars.size(); ++k )
    {
        Car  &c = m_cars[k];
        float d = std::max( 0.0f, len( sub( c.m_pos, at ) ) - 1.0f );
        if ( d >= radius ) continue;
        float f = 1.0f - d / radius;
        m_source = 1;
        damage( (int)k, dmg * ( 0.35f + 0.65f * f ), by, at );
        c.m_vel = add( c.m_vel, mul( norm( sub( c.m_pos, at ) ), 11.0f * f * 1400.0f / c.m_mass ) );
        c.m_yaw += m_rng.range( -2.5f, 2.5f ) * f;
    }
    for ( Barrel &b : m_barrels )
        if ( !b.m_gone && len( sub( b.m_pos, at ) ) < radius * 0.8f )
        {
            b.m_gone = true;
            event( EvE::BARREL, by, b.m_pos );
            explode( b.m_pos, 7.0f, 35.0f, by );
        }
}

void World::wreck( int i, int by )
{
    Car &c = m_cars[i];
    if ( c.m_wrecked ) return;
    c.m_wrecked = true;
    c.m_hp = 0;
    c.m_wreckTime = 0;
    c.m_wreckedBy = by;
    c.m_firing = false;
    if ( by >= 0 && by != i ) ++m_cars[by].m_wrecks;
    event( EvE::WRECK, i, c.m_pos, 1, 0, by );
    explode( c.m_pos, 5.0f, 8.0f, by );
}

}   // namespace grave
