/**
 * @file Input.cpp
 * @brief Keys and pads.
 * @author Rene Weller
 */
#include "Input.h"

#include "Lang.h"

#include <raylib.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace grave
{

namespace
{

constexpr int MaxPads = 4;

bool padReady( int p ) { return IsGamepadAvailable( p ); }

float anyAxis( int axis )
{
    float v = 0;
    for ( int p = 0; p < MaxPads; ++p )
        if ( padReady( p ) )
        {
            float a = GetGamepadAxisMovement( p, axis );
            if ( std::fabs( a ) > std::fabs( v ) ) v = a;
        }
    return v;
}

bool anyPad( int b )
{
    for ( int p = 0; p < MaxPads; ++p )
        if ( padReady( p ) && IsGamepadButtonDown( p, b ) ) return true;
    return false;
}

bool anyPadPressed( int b )
{
    for ( int p = 0; p < MaxPads; ++p )
        if ( padReady( p ) && IsGamepadButtonPressed( p, b ) ) return true;
    return false;
}

/** @return a trigger's travel 0..1 (axes run -1..1 on most pads, 0..1 on some) */
float trigger( int pad, int axis )
{
    float a = GetGamepadAxisMovement( pad, axis );
    // raylib reports -1 at rest for XInput triggers; a pad that rests at 0 never goes below.
    float v = a < -0.01f || a > 0.99f ? ( a + 1.0f ) * 0.5f : a;
    return v < 0.06f ? 0.0f : clampf( ( v - 0.06f ) / 0.9f, 0, 1 );
}

}   // namespace

Input::Input() { reset(); }

void Input::reset()
{
    auto set = [&]( ActE a, int k, int k2, int p, int p2 ) { m_binds[(int)a] = { k, k2, p, p2 }; };
    set( ActE::ACCEL, KEY_UP, KEY_W, GAMEPAD_BUTTON_RIGHT_TRIGGER_2, 0 );
    set( ActE::BRAKE, KEY_DOWN, KEY_S, GAMEPAD_BUTTON_LEFT_TRIGGER_2, 0 );
    set( ActE::LEFT, KEY_LEFT, KEY_A, GAMEPAD_BUTTON_LEFT_FACE_LEFT, 0 );
    set( ActE::RIGHT, KEY_RIGHT, KEY_D, GAMEPAD_BUTTON_LEFT_FACE_RIGHT, 0 );
    set( ActE::HANDBRAKE, KEY_LEFT_CONTROL, KEY_B, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, 0 );
    set( ActE::FIRE, KEY_SPACE, KEY_J, GAMEPAD_BUTTON_RIGHT_FACE_DOWN, 0 );
    set( ActE::DROP, KEY_E, KEY_K, GAMEPAD_BUTTON_RIGHT_FACE_LEFT, 0 );
    set( ActE::ROCKET, KEY_Q, KEY_L, GAMEPAD_BUTTON_RIGHT_FACE_UP, 0 );
    set( ActE::NITRO, KEY_LEFT_SHIFT, KEY_I, GAMEPAD_BUTTON_RIGHT_TRIGGER_1, 0 );
    set( ActE::CYCLE, KEY_TAB, KEY_R, GAMEPAD_BUTTON_LEFT_TRIGGER_1, 0 );
    set( ActE::HORN, KEY_H, 0, GAMEPAD_BUTTON_LEFT_THUMB, 0 );
    set( ActE::PAUSE, KEY_ESCAPE, KEY_P, GAMEPAD_BUTTON_MIDDLE_RIGHT, 0 );
}

bool Input::padButtonDown( int pad, int b ) const
{
    if ( b <= 0 || !padReady( pad ) ) return false;
    if ( b == GAMEPAD_BUTTON_LEFT_TRIGGER_2 || b == GAMEPAD_BUTTON_RIGHT_TRIGGER_2 )
        return m_trigNow[pad][b == GAMEPAD_BUTTON_RIGHT_TRIGGER_2 ? 1 : 0];
    return IsGamepadButtonDown( pad, b );
}

bool Input::padButtonPressed( int pad, int b ) const
{
    if ( b <= 0 || !padReady( pad ) ) return false;
    if ( b == GAMEPAD_BUTTON_LEFT_TRIGGER_2 || b == GAMEPAD_BUTTON_RIGHT_TRIGGER_2 )
    {
        int t = b == GAMEPAD_BUTTON_RIGHT_TRIGGER_2 ? 1 : 0;
        return m_trigNow[pad][t] && !m_trigPrev[pad][t];
    }
    return IsGamepadButtonPressed( pad, b );
}

void Input::script( const std::vector<std::string> &words )
{
    m_script = words;
    m_scriptAt = 0;
    m_scripted = true;
    m_idle = 0;   // the first word at once (the app test waits between its words itself)
}

bool Input::take( const char *w )
{
    if ( m_word != w ) return false;
    m_word.clear();
    return true;
}

void Input::update()
{
    if ( m_scripted )
    {
        m_word.clear();
        if ( m_idle > 0 )
            --m_idle;
        else if ( m_scriptAt < m_script.size() )
        {
            m_word = m_script[m_scriptAt++];
            m_idle = 4;
        }
        return;
    }
    for ( int p = 0; p < MaxPads; ++p )
    {
        for ( int t = 0; t < 2; ++t ) m_trigPrev[p][t] = m_trigNow[p][t];
        if ( !padReady( p ) )
        {
            m_trigNow[p][0] = m_trigNow[p][1] = false;
            continue;
        }
        m_trigNow[p][0] = IsGamepadButtonDown( p, GAMEPAD_BUTTON_LEFT_TRIGGER_2 ) || trigger( p, GAMEPAD_AXIS_LEFT_TRIGGER ) > 0.3f;
        m_trigNow[p][1] = IsGamepadButtonDown( p, GAMEPAD_BUTTON_RIGHT_TRIGGER_2 ) || trigger( p, GAMEPAD_AXIS_RIGHT_TRIGGER ) > 0.3f;
        if ( GetGamepadButtonPressed() != 0 || std::fabs( GetGamepadAxisMovement( p, GAMEPAD_AXIS_LEFT_X ) ) > 0.5f ||
             std::fabs( GetGamepadAxisMovement( p, GAMEPAD_AXIS_LEFT_Y ) ) > 0.5f || m_trigNow[p][1] )
            m_padUsed = true;
    }
    if ( GetKeyPressed() != 0 ) m_padUsed = false;
    // Presses wait until a step takes them.
    for ( int i = 0; i < 2; ++i )
    {
        int  mask = 0;
        bool keys = false;
        route( i, mask, keys );
        for ( ActE a : { ActE::DROP, ActE::ROCKET, ActE::NITRO, ActE::CYCLE, ActE::HORN } )
            if ( pressedAct( a, mask, keys ) ) m_latch[i][(int)a] = true;
    }
}

void Input::route( int i, int &mask, bool &keys ) const
{
    // One driver: the keys and every pad. Two: the first pad (and the keys) for the first, the second pad for the
    // second; with only one pad the first driver keeps the keys and the second takes the pad.
    if ( m_players <= 1 )
    {
        mask = i == 0 ? 0xF : 0;
        keys = i == 0;
    }
    else if ( padReady( 1 ) )
    {
        mask = i == 0 ? 1 : 2;
        keys = i == 0;
    }
    else
    {
        mask = i == 0 ? 0 : 1;
        keys = i == 0;
    }
}

bool Input::held( ActE a, int mask, bool keys ) const
{
    const Bind &b = m_binds[(int)a];
    if ( keys && ( ( b.m_key && IsKeyDown( b.m_key ) ) || ( b.m_key2 && IsKeyDown( b.m_key2 ) ) ) ) return true;
    for ( int p = 0; p < MaxPads; ++p )
        if ( ( mask >> p ) & 1 )
            if ( padButtonDown( p, b.m_pad ) || padButtonDown( p, b.m_pad2 ) ) return true;
    return false;
}

bool Input::pressedAct( ActE a, int mask, bool keys ) const
{
    const Bind &b = m_binds[(int)a];
    if ( keys && ( ( b.m_key && IsKeyPressed( b.m_key ) ) || ( b.m_key2 && IsKeyPressed( b.m_key2 ) ) ) ) return true;
    for ( int p = 0; p < MaxPads; ++p )
        if ( ( mask >> p ) & 1 )
            if ( padButtonPressed( p, b.m_pad ) || padButtonPressed( p, b.m_pad2 ) ) return true;
    return false;
}

CarInput Input::car( int i, int players )
{
    m_players = players;
    CarInput in;
    int      mask = 0;
    bool     keys = false;
    route( i, mask, keys );
    float steer = 0, thr = 0, brk = 0;
    if ( held( ActE::LEFT, mask, keys ) ) steer -= 1;
    if ( held( ActE::RIGHT, mask, keys ) ) steer += 1;
    if ( held( ActE::ACCEL, mask, keys ) ) thr = 1;
    if ( held( ActE::BRAKE, mask, keys ) ) brk = 1;
    for ( int p = 0; p < MaxPads; ++p )
    {
        if ( !( ( mask >> p ) & 1 ) || !padReady( p ) ) continue;
        float ax = GetGamepadAxisMovement( p, GAMEPAD_AXIS_LEFT_X );
        if ( std::fabs( ax ) > 0.12f )
        {
            // A dead zone, then a curve that is gentle around the middle.
            float k = ( std::fabs( ax ) - 0.12f ) / 0.88f;
            steer += ( ax > 0 ? 1.0f : -1.0f ) * ( 0.35f * k + 0.65f * k * k );
        }
        // The triggers are pedals.
        if ( m_binds[(int)ActE::ACCEL].m_pad == GAMEPAD_BUTTON_RIGHT_TRIGGER_2 ) thr = std::max( thr, trigger( p, GAMEPAD_AXIS_RIGHT_TRIGGER ) );
        if ( m_binds[(int)ActE::BRAKE].m_pad == GAMEPAD_BUTTON_LEFT_TRIGGER_2 ) brk = std::max( brk, trigger( p, GAMEPAD_AXIS_LEFT_TRIGGER ) );
    }
    in.m_steer = clampf( steer, -1, 1 );
    in.m_throttle = clampf( thr, 0, 1 );
    in.m_brake = clampf( brk, 0, 1 );
    in.m_handbrake = held( ActE::HANDBRAKE, mask, keys );
    in.m_fire = held( ActE::FIRE, mask, keys );
    in.m_drop = m_latch[i][(int)ActE::DROP];
    in.m_rocket = m_latch[i][(int)ActE::ROCKET];
    in.m_nitro = m_latch[i][(int)ActE::NITRO];
    in.m_cycle = m_latch[i][(int)ActE::CYCLE];
    in.m_horn = held( ActE::HORN, mask, keys );
    return in;
}

void Input::consumed()
{
    for ( int i = 0; i < 2; ++i )
        for ( int a = 0; a < (int)ActE::COUNT; ++a ) m_latch[i][a] = false;
}

bool Input::repeat( bool down, int slot )
{
    bool fire = false;
    if ( down && !m_prev[slot] )
    {
        fire = true;
        m_rep[slot] = 0.38f;
    }
    else if ( down )
    {
        m_rep[slot] -= GetFrameTime();
        if ( m_rep[slot] <= 0 )
        {
            fire = true;
            m_rep[slot] = 0.075f;
        }
    }
    m_prev[slot] = down;
    return fire;
}

bool Input::up() { if ( m_scripted ) return take( "up" ); return repeat( IsKeyDown( KEY_UP ) || IsKeyDown( KEY_W ) || anyPad( GAMEPAD_BUTTON_LEFT_FACE_UP ) || anyAxis( GAMEPAD_AXIS_LEFT_Y ) < -0.55f, 0 ); }
bool Input::down() { if ( m_scripted ) return take( "down" ); return repeat( IsKeyDown( KEY_DOWN ) || IsKeyDown( KEY_S ) || anyPad( GAMEPAD_BUTTON_LEFT_FACE_DOWN ) || anyAxis( GAMEPAD_AXIS_LEFT_Y ) > 0.55f, 1 ); }
bool Input::left() { if ( m_scripted ) return take( "left" ); return repeat( IsKeyDown( KEY_LEFT ) || IsKeyDown( KEY_A ) || anyPad( GAMEPAD_BUTTON_LEFT_FACE_LEFT ) || anyAxis( GAMEPAD_AXIS_LEFT_X ) < -0.55f, 2 ); }
bool Input::right() { if ( m_scripted ) return take( "right" ); return repeat( IsKeyDown( KEY_RIGHT ) || IsKeyDown( KEY_D ) || anyPad( GAMEPAD_BUTTON_LEFT_FACE_RIGHT ) || anyAxis( GAMEPAD_AXIS_LEFT_X ) > 0.55f, 3 ); }
bool Input::accept() { if ( m_scripted ) return take( "accept" ); return IsKeyPressed( KEY_ENTER ) || IsKeyPressed( KEY_KP_ENTER ) || IsKeyPressed( KEY_SPACE ) || anyPadPressed( GAMEPAD_BUTTON_RIGHT_FACE_DOWN ); }
bool Input::back() { if ( m_scripted ) return take( "back" ); return IsKeyPressed( KEY_ESCAPE ) || IsKeyPressed( KEY_BACKSPACE ) || anyPadPressed( GAMEPAD_BUTTON_RIGHT_FACE_RIGHT ); }
bool Input::pause() { if ( m_scripted ) return take( "pause" ); return IsKeyPressed( KEY_ESCAPE ) || IsKeyPressed( KEY_P ) || anyPadPressed( GAMEPAD_BUTTON_MIDDLE_RIGHT ); }
bool Input::prevTab() { if ( m_scripted ) return take( "prevtab" ); return IsKeyPressed( KEY_Q ) || IsKeyPressed( KEY_PAGE_UP ) || anyPadPressed( GAMEPAD_BUTTON_LEFT_TRIGGER_1 ); }
bool Input::nextTab() { if ( m_scripted ) return take( "nexttab" ); return IsKeyPressed( KEY_E ) || IsKeyPressed( KEY_PAGE_DOWN ) || anyPadPressed( GAMEPAD_BUTTON_RIGHT_TRIGGER_1 ); }

int Input::capturedKey()
{
    int k = GetKeyPressed();
    if ( k == KEY_ESCAPE || k == KEY_F1 || k == KEY_F11 || k == KEY_F12 ) return 0;
    return k;
}

int Input::capturedPad() const
{
    for ( int p = 0; p < MaxPads; ++p )
    {
        if ( !padReady( p ) ) continue;
        for ( int b = 1; b <= GAMEPAD_BUTTON_RIGHT_THUMB; ++b )
            if ( b != GAMEPAD_BUTTON_MIDDLE_RIGHT && padButtonPressed( p, b ) ) return b;
    }
    return 0;
}

const char *Input::name( ActE a )
{
    static const char *n[] = { "accel", "brake", "left", "right", "handbrake", "fire", "drop", "rocket", "nitro", "cycle", "horn", "pause" };
    return n[(int)a];
}

std::string Input::keyLabel( int key )
{
    switch ( key )
    {
    case 0: return "-";
    case KEY_SPACE: return T( "Leertaste" );
    case KEY_LEFT_SHIFT: return T( "Umschalt" );
    case KEY_RIGHT_SHIFT: return T( "Umschalt rechts" );
    case KEY_LEFT_CONTROL: return T( "Strg" );
    case KEY_RIGHT_CONTROL: return T( "Strg rechts" );
    case KEY_LEFT_ALT: return "Alt";
    case KEY_ENTER: return T( "Eingabe" );
    case KEY_ESCAPE: return "Esc";
    case KEY_TAB: return "Tab";
    case KEY_BACKSPACE: return T( "Rücktaste" );
    case KEY_UP: return T( "Pfeil hoch" );
    case KEY_DOWN: return T( "Pfeil runter" );
    case KEY_LEFT: return T( "Pfeil links" );
    case KEY_RIGHT: return T( "Pfeil rechts" );
    default: break;
    }
    if ( key >= KEY_A && key <= KEY_Z ) return std::string( 1, (char)( 'A' + key - KEY_A ) );
    if ( key >= KEY_ZERO && key <= KEY_NINE ) return std::string( 1, (char)( '0' + key - KEY_ZERO ) );
    if ( key >= KEY_F1 && key <= KEY_F12 ) return "F" + std::to_string( key - KEY_F1 + 1 );
    return "#" + std::to_string( key );
}

std::string Input::padLabel( int b )
{
    switch ( b )
    {
    case 0: return "-";
    case GAMEPAD_BUTTON_RIGHT_FACE_DOWN: return "A";
    case GAMEPAD_BUTTON_RIGHT_FACE_RIGHT: return "B";
    case GAMEPAD_BUTTON_RIGHT_FACE_LEFT: return "X";
    case GAMEPAD_BUTTON_RIGHT_FACE_UP: return "Y";
    case GAMEPAD_BUTTON_LEFT_TRIGGER_1: return "LB";
    case GAMEPAD_BUTTON_RIGHT_TRIGGER_1: return "RB";
    case GAMEPAD_BUTTON_LEFT_TRIGGER_2: return "LT";
    case GAMEPAD_BUTTON_RIGHT_TRIGGER_2: return "RT";
    case GAMEPAD_BUTTON_MIDDLE_RIGHT: return "Start";
    case GAMEPAD_BUTTON_MIDDLE_LEFT: return "Select";
    case GAMEPAD_BUTTON_LEFT_FACE_UP: return T( "Kreuz hoch" );
    case GAMEPAD_BUTTON_LEFT_FACE_DOWN: return T( "Kreuz runter" );
    case GAMEPAD_BUTTON_LEFT_FACE_LEFT: return T( "Kreuz links" );
    case GAMEPAD_BUTTON_LEFT_FACE_RIGHT: return T( "Kreuz rechts" );
    case GAMEPAD_BUTTON_LEFT_THUMB: return "L3";
    case GAMEPAD_BUTTON_RIGHT_THUMB: return "R3";
    default: return "#" + std::to_string( b );
    }
}

std::string Input::label( ActE a, bool pad ) const
{
    const Bind &b = m_binds[(int)a];
    if ( pad ) return padLabel( b.m_pad ) + ( b.m_pad2 ? " / " + padLabel( b.m_pad2 ) : "" );
    return keyLabel( b.m_key ) + ( b.m_key2 ? " / " + keyLabel( b.m_key2 ) : "" );
}

std::vector<std::string> Input::save() const
{
    std::vector<std::string> out;
    for ( int a = 0; a < (int)ActE::COUNT; ++a )
    {
        char        buf[128];
        const Bind &b = m_binds[a];
        snprintf( buf, sizeof( buf ), "bind %s %d %d %d %d", name( (ActE)a ), b.m_key, b.m_key2, b.m_pad, b.m_pad2 );
        out.push_back( buf );
    }
    return out;
}

void Input::load( const std::string &line )
{
    char n[32] = {};
    Bind b;
    if ( sscanf( line.c_str(), "bind %31s %d %d %d %d", n, &b.m_key, &b.m_key2, &b.m_pad, &b.m_pad2 ) != 5 ) return;
    for ( int a = 0; a < (int)ActE::COUNT; ++a )
        if ( strcmp( n, name( (ActE)a ) ) == 0 ) m_binds[a] = b;
}

}   // namespace grave
