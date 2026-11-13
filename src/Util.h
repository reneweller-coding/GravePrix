/**
 * @file Util.h
 * @brief Small things everybody needs: the clock, a seeded random source, vectors on the ground, colours from text.
 *        On the ground x grows east and y (the world's z) south; headings are radians, 0 north, clockwise.
 * @author Rene Weller
 */
#pragma once

#include <raylib.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace grave
{

constexpr float Dt = 1.0f / 60.0f;          ///< one step of the game (60 a second, always)
constexpr float Pi = 3.14159265358979f;     ///< pi
constexpr float Deg = Pi / 180.0f;          ///< one degree in radians
constexpr float Gravity = 9.81f;            ///< m/s²

/** @brief A small, fast random source (xorshift); the same seed gives the same game everywhere. */
struct Rng
{
    uint32_t m_state = 0x9e3779b9u;   ///< the state (never 0)

    /** @brief Starts over. @param[in] seed  any number */
    void seed( uint32_t seed ) { m_state = seed ? seed * 2654435761u + 1u : 0x9e3779b9u; }

    /** @return the next 32 bits */
    uint32_t next()
    {
        uint32_t x = m_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return m_state = x;
    }

    /** @return a number in [0, 1) */
    float unit() { return ( next() >> 8 ) * ( 1.0f / 16777216.0f ); }

    /** @return a number in [a, b) */
    float range( float a, float b ) { return a + ( b - a ) * unit(); }

    /** @return a whole number in [0, n) */
    int below( int n ) { return n > 0 ? (int)( next() % (uint32_t)n ) : 0; }

    /** @return true with the chance p */
    bool chance( float p ) { return unit() < p; }
};

/** @return a + b */
inline Vector2 add( Vector2 a, Vector2 b ) { return { a.x + b.x, a.y + b.y }; }
/** @return a - b */
inline Vector2 sub( Vector2 a, Vector2 b ) { return { a.x - b.x, a.y - b.y }; }
/** @return a * s */
inline Vector2 mul( Vector2 a, float s ) { return { a.x * s, a.y * s }; }
/** @return the length of a */
inline float len( Vector2 a ) { return std::sqrt( a.x * a.x + a.y * a.y ); }
/** @return the squared distance of a and b */
inline float dist2( Vector2 a, Vector2 b ) { return ( a.x - b.x ) * ( a.x - b.x ) + ( a.y - b.y ) * ( a.y - b.y ); }
/** @return a of length 1 (or (0, 1) when a is 0) */
inline Vector2 norm( Vector2 a )
{
    float l = len( a );
    return l > 1e-6f ? Vector2{ a.x / l, a.y / l } : Vector2{ 0.0f, 1.0f };
}
/** @return the forward direction of a heading (radians, 0 north = -y, clockwise) */
inline Vector2 headingDir( float a ) { return { std::sin( a ), -std::cos( a ) }; }
/** @return the right-hand direction of a heading */
inline Vector2 headingRight( float a ) { return { std::cos( a ), std::sin( a ) }; }
/** @return the heading of a direction (the inverse of headingDir) */
inline float headingOf( Vector2 d ) { return std::atan2( d.x, -d.y ); }
/** @return the dot product */
inline float dot( Vector2 a, Vector2 b ) { return a.x * b.x + a.y * b.y; }
/** @return the 2D cross product (z of a x b) */
inline float cross( Vector2 a, Vector2 b ) { return a.x * b.y - a.y * b.x; }
/** @return a turned a quarter clockwise on the ground (x east, y south): (x, y) -> (-y, x) */
inline Vector2 perp( Vector2 a ) { return { -a.y, a.x }; }
/** @return an angle in radians brought into (-pi, pi] */
inline float wrapRad( float a )
{
    while ( a > Pi ) a -= 2.0f * Pi;
    while ( a <= -Pi ) a += 2.0f * Pi;
    return a;
}
/** @return v held between lo and hi */
inline float clampf( float v, float lo, float hi ) { return v < lo ? lo : ( v > hi ? hi : v ); }
/** @return a mix of a and b */
inline float lerpf( float a, float b, float t ) { return a + ( b - a ) * t; }
/** @return 0 below e0, 1 above e1, a smooth S between */
inline float smoothstepf( float e0, float e1, float x )
{
    float t = clampf( ( x - e0 ) / ( e1 - e0 ), 0.0f, 1.0f );
    return t * t * ( 3.0f - 2.0f * t );
}
/** @return an angle in degrees brought into (-180, 180] */
inline float wrapDeg( float a )
{
    while ( a > 180.0f ) a -= 360.0f;
    while ( a <= -180.0f ) a += 360.0f;
    return a;
}

/**
 * @brief Reads a colour: "#rgb", "#rrggbb", "#rrggbbaa" or a name of the palette (steel, crimson ...).
 * @param[in] text  the colour
 * @param[in] def   when it cannot be read
 * @return the colour
 */
Color parseColor( const std::string &text, Color def = WHITE );

/** @return a colour mixed of a and b */
inline Color mixColor( Color a, Color b, float t )
{
    return { (unsigned char)lerpf( a.r, b.r, t ), (unsigned char)lerpf( a.g, b.g, t ),
             (unsigned char)lerpf( a.b, b.b, t ), (unsigned char)lerpf( a.a, b.a, t ) };
}

/** @return a colour scaled in brightness (alpha kept) */
inline Color scaleColor( Color c, float s )
{
    return { (unsigned char)clampf( c.r * s, 0, 255 ), (unsigned char)clampf( c.g * s, 0, 255 ),
             (unsigned char)clampf( c.b * s, 0, 255 ), c.a };
}

}   // namespace grave
