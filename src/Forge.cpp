/**
 * @file Forge.cpp
 * @brief The model builder.
 * @author Rene Weller
 */
#include "Forge.h"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace grave
{

void MeshBuilder::tri( Vector3 a, Vector3 b, Vector3 c, Color col, Vector3 inside )
{
    Vector3 n = Vector3CrossProduct( Vector3Subtract( b, a ), Vector3Subtract( c, a ) );
    float   l = Vector3Length( n );
    if ( l < 1e-9f ) return;
    n = Vector3Scale( n, 1.0f / l );
    Vector3 mid = Vector3Scale( Vector3Add( Vector3Add( a, b ), c ), 1.0f / 3.0f );
    if ( Vector3DotProduct( n, Vector3Subtract( mid, inside ) ) < 0 )
    {
        std::swap( b, c );
        n = Vector3Negate( n );
    }
    float k = m_noise > 0 ? 1.0f + m_rng.range( -m_noise, m_noise ) : 1.0f;
    Color fc = { (unsigned char)clampf( col.r * k, 0, 255 ), (unsigned char)clampf( col.g * k, 0, 255 ),
                 (unsigned char)clampf( col.b * k, 0, 255 ), col.a };
    for ( const Vector3 &v : { a, b, c } ) vert( v, n, fc );
}

void MeshBuilder::quad( Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col, Vector3 inside )
{
    tri( a, b, c, col, inside );
    tri( a, c, d, col, inside );
}

void MeshBuilder::vert( Vector3 p, Vector3 n, Color c, Vector2 uv, Vector2 uv2 )
{
    m_pos.insert( m_pos.end(), { p.x, p.y, p.z } );
    m_nrm.insert( m_nrm.end(), { n.x, n.y, n.z } );
    m_col.insert( m_col.end(), { c.r, c.g, c.b, c.a } );
    if ( m_withUv )
    {
        m_uv.insert( m_uv.end(), { uv.x, uv.y } );
        m_uv2.insert( m_uv2.end(), { uv2.x, uv2.y } );
    }
}

void MeshBuilder::box( Vector3 center, Vector3 half, float yaw, Color col, float taper )
{
    Vector3 v[8];
    int     k = 0;
    for ( float y : { -1.0f, 1.0f } )
        for ( float z : { -1.0f, 1.0f } )
            for ( float x : { -1.0f, 1.0f } )
            {
                float   t = y > 0 ? 1.0f - taper : 1.0f;
                Vector3 p = { x * half.x * t, y * half.y, z * half.z * t };
                p = Vector3RotateByAxisAngle( p, { 0, 1, 0 }, -yaw );
                v[k++] = Vector3Add( p, center );
            }
    // 0..3 bottom (x-,z-)(x+,z-)(x-,z+)(x+,z+), 4..7 top
    quad( v[4], v[5], v[7], v[6], col, center );
    quad( v[0], v[1], v[5], v[4], col, center );
    quad( v[2], v[3], v[7], v[6], col, center );
    quad( v[0], v[2], v[6], v[4], col, center );
    quad( v[1], v[3], v[7], v[5], col, center );
}

void MeshBuilder::append( const Mesh &m, Matrix xf, Color tint )
{
    Matrix nm = MatrixTranspose( MatrixInvert( xf ) );
    for ( int i = 0; i < m.vertexCount; ++i )
    {
        Vector3 p = Vector3Transform( { m.vertices[i * 3], m.vertices[i * 3 + 1], m.vertices[i * 3 + 2] }, xf );
        Vector3 n = { m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2] };
        n = Vector3Normalize( { nm.m0 * n.x + nm.m4 * n.y + nm.m8 * n.z, nm.m1 * n.x + nm.m5 * n.y + nm.m9 * n.z,
                                nm.m2 * n.x + nm.m6 * n.y + nm.m10 * n.z } );
        Color c = { (unsigned char)( m.colors[i * 4] * tint.r / 255 ), (unsigned char)( m.colors[i * 4 + 1] * tint.g / 255 ),
                    (unsigned char)( m.colors[i * 4 + 2] * tint.b / 255 ), m.colors[i * 4 + 3] };
        vert( p, n, c );
    }
}

Mesh MeshBuilder::mesh( bool upload )
{
    Mesh m = {};
    m.vertexCount = (int)( m_pos.size() / 3 );
    m.triangleCount = m.vertexCount / 3;
    if ( m.vertexCount == 0 ) return m;
    m.vertices = (float *)MemAlloc( (unsigned)( m_pos.size() * sizeof( float ) ) );
    m.normals = (float *)MemAlloc( (unsigned)( m_nrm.size() * sizeof( float ) ) );
    m.colors = (unsigned char *)MemAlloc( (unsigned)m_col.size() );
    memcpy( m.vertices, m_pos.data(), m_pos.size() * sizeof( float ) );
    memcpy( m.normals, m_nrm.data(), m_nrm.size() * sizeof( float ) );
    memcpy( m.colors, m_col.data(), m_col.size() );
    if ( m_withUv && m_uv.size() == (size_t)m.vertexCount * 2 )
    {
        m.texcoords = (float *)MemAlloc( (unsigned)( m_uv.size() * sizeof( float ) ) );
        m.texcoords2 = (float *)MemAlloc( (unsigned)( m_uv2.size() * sizeof( float ) ) );
        memcpy( m.texcoords, m_uv.data(), m_uv.size() * sizeof( float ) );
        memcpy( m.texcoords2, m_uv2.data(), m_uv2.size() * sizeof( float ) );
    }
    if ( upload ) UploadMesh( &m, false );
    return m;
}

void MeshBuilder::clear()
{
    m_pos.clear();
    m_nrm.clear();
    m_col.clear();
    m_uv.clear();
    m_uv2.clear();
}

namespace
{

using MeshB = MeshBuilder;

/** @brief A part's place and turn: applied to its own points. */
struct Xf
{
    Vector3 m_at = { 0, 0, 0 };
    float   m_yaw = 0.0f;     ///< degrees about y
    float   m_pitch = 0.0f;   ///< degrees about x
    float   m_roll = 0.0f;    ///< degrees about z
    bool    m_flip = false;   ///< mirrored to -x

    Vector3 operator()( Vector3 p ) const
    {
        if ( m_roll != 0 ) p = Vector3RotateByAxisAngle( p, { 0, 0, 1 }, m_roll * Deg );
        if ( m_pitch != 0 ) p = Vector3RotateByAxisAngle( p, { 1, 0, 0 }, m_pitch * Deg );
        if ( m_yaw != 0 ) p = Vector3RotateByAxisAngle( p, { 0, 1, 0 }, -m_yaw * Deg );
        p = Vector3Add( p, m_at );
        if ( m_flip ) p.x = -p.x;
        return p;
    }
};

/** @brief The width of a hull along its length (u = 0 nose .. 1 tail). */
float hullWidth( const std::string &shape, float u )
{
    u = clampf( u, 0.0f, 1.0f );
    if ( shape == "cigar" ) return std::pow( std::sin( Pi * u ), 0.5f );
    if ( shape == "wedge" ) return 0.18f + 0.82f * std::pow( u, 0.8f );
    if ( shape == "blob" ) return std::pow( std::sin( Pi * u ), 0.7f ) * ( 0.85f + 0.15f * std::sin( Pi * u ) );
    if ( shape == "box" ) return u < 0.08f ? 0.75f + u * 3.1f : u > 0.92f ? 0.75f + ( 1 - u ) * 3.1f : 1.0f;
    if ( shape == "needle" ) return std::pow( std::sin( Pi * u ), 1.4f );
    if ( shape == "tear" ) return u < 0.35f ? std::sin( u / 0.35f * Pi * 0.5f ) : std::pow( std::cos( ( u - 0.35f ) / 0.65f * Pi * 0.5f ), 0.7f );
    if ( shape == "hammer" ) return u < 0.28f ? 1.0f : 0.42f + 0.1f * ( 1 - u );
    if ( shape == "arrow" ) return u < 0.55f ? u / 0.55f : ( u < 0.72f ? 0.45f : 0.45f + ( u - 0.72f ) * 1.4f );
    if ( shape == "hex" ) return u < 0.25f ? 0.5f + u * 2.0f : u > 0.75f ? 0.5f + ( 1 - u ) * 2.0f : 1.0f;
    // dart: a sharp nose, widest at two thirds, a little narrower at the tail
    return u < 0.68f ? std::pow( u / 0.68f, 0.85f ) : 1.0f - ( u - 0.68f ) / 0.32f * 0.3f;
}

/** @brief The height of a hull along its length. */
float hullHeight( const std::string &shape, float u )
{
    if ( shape == "box" || shape == "hex" ) return hullWidth( shape, u );
    if ( shape == "wedge" ) return 0.35f + 0.65f * std::sin( Pi * clampf( u * 0.85f + 0.1f, 0, 1 ) );
    return std::pow( std::max( 0.05f, std::sin( Pi * clampf( u * 0.92f + 0.05f, 0, 1 ) ) ), 0.6f );
}

/** @brief A superellipse point: round 2 is an ellipse, larger is boxier. */
Vector2 superellipse( float a, float round )
{
    float c = std::cos( a ), s = std::sin( a );
    float e = 2.0f / std::max( 0.5f, round );
    return { ( c < 0 ? -1.0f : 1.0f ) * std::pow( std::fabs( c ), e ), ( s < 0 ? -1.0f : 1.0f ) * std::pow( std::fabs( s ), e ) };
}

/** @brief Builds one part into a builder. */
void buildPart( MeshB &mb, const Line &p, const Xf &xf, ModelMesh &out )
{
    const std::string &k = p.m_key;
    Color   col = p.color( "color", { 150, 160, 172, 255 } );
    float   emit = clampf( p.num( "emit", 0.0f ), 0.0f, 1.0f );
    col.a = (unsigned char)( emit * 255 );
    mb.m_noise = p.num( "noise", 0.06f );
    Vector3 center = xf( { 0, 0, 0 } );
    if ( k == "wheel" )
    {
        // Wheels are drawn on their own (they spin and steer); the model keeps where they are.
        float   r = p.num( "r", 0.32f ), w = p.num( "w", 0.22f );
        Vector3 at = xf( { 0, p.has( "y" ) ? 0.0f : r, 0 } );
        out.m_wheels.push_back( { at, r, w, p.integer( "front", 0 ) != 0, p.color( "hub", { 170, 172, 178, 255 } ) } );
        return;
    }
    if ( k == "bar" )
    {
        // A tube between two points (roll bars, push bars, aerials, landau irons).
        Vector3 a = { 0, 0, 0 }, b = { 0, 1, 0 };
        sscanf( p.str( "from", "0,0,0" ).c_str(), "%f,%f,%f", &a.x, &a.y, &a.z );
        sscanf( p.str( "to", "0,1,0" ).c_str(), "%f,%f,%f", &b.x, &b.y, &b.z );
        float   r = p.num( "r", 0.04f );
        int     seg = std::clamp( p.integer( "seg", 6 ), 3, 16 );
        Vector3 d = Vector3Subtract( b, a );
        if ( Vector3Length( d ) < 1e-5f ) return;
        Vector3 axis = Vector3Normalize( d );
        Vector3 u = std::fabs( axis.y ) < 0.9f ? Vector3{ 0, 1, 0 } : Vector3{ 1, 0, 0 };
        Vector3 s1 = Vector3Normalize( Vector3CrossProduct( axis, u ) ), s2 = Vector3CrossProduct( axis, s1 );
        Vector3 mid = xf( Vector3Scale( Vector3Add( a, b ), 0.5f ) );
        for ( int i = 0; i < seg; ++i )
        {
            float   a0 = 2 * Pi * i / seg, a1 = 2 * Pi * ( i + 1 ) / seg;
            Vector3 o0 = Vector3Add( Vector3Scale( s1, std::cos( a0 ) * r ), Vector3Scale( s2, std::sin( a0 ) * r ) );
            Vector3 o1 = Vector3Add( Vector3Scale( s1, std::cos( a1 ) * r ), Vector3Scale( s2, std::sin( a1 ) * r ) );
            mb.quad( xf( Vector3Add( a, o0 ) ), xf( Vector3Add( a, o1 ) ), xf( Vector3Add( b, o1 ) ), xf( Vector3Add( b, o0 ) ), col, mid );
        }
        return;
    }
    if ( k == "body" )
    {
        // A car body lofted along z: a rounded box whose top drops towards the nose and the tail.
        float len = p.num( "len", 4.4f ), wid = p.num( "wid", 1.8f ), hgt = p.num( "hgt", 0.7f );
        float round = p.num( "round", 4.0f ), nose = clampf( p.num( "nose", 0.2f ), 0.01f, 0.9f );
        float tail = clampf( p.num( "tail", 0.15f ), 0.01f, 0.9f ), drop = clampf( p.num( "drop", 0.35f ), 0.0f, 0.95f );
        float pinch = clampf( p.num( "pinch", 0.12f ), 0.0f, 0.9f );
        int   seg = std::clamp( p.integer( "seg", 12 ), 4, 32 );
        const int N = 18;
        std::vector<std::vector<Vector3>> rings;
        for ( int i = 0; i <= N; ++i )
        {
            float u = i / (float)N;
            float hf = 1.0f;
            if ( u < nose ) hf -= drop * ( 1.0f - std::sin( u / nose * Pi * 0.5f ) );
            if ( u > 1.0f - tail ) hf -= drop * ( 1.0f - std::sin( ( 1.0f - u ) / tail * Pi * 0.5f ) );
            float e = std::min( u, 1.0f - u ) / 0.12f;
            float wf = 1.0f - pinch * ( 1.0f - std::sin( std::min( e, 1.0f ) * Pi * 0.5f ) );
            float z = -len * 0.5f + len * u;
            float bottom = -hgt * 0.5f, top = bottom + hgt * std::max( hf, 0.05f );
            std::vector<Vector3> ring;
            for ( int sg = 0; sg < seg; ++sg )
            {
                float   a = 2 * Pi * sg / seg + Pi / seg;
                Vector2 q = superellipse( a, round );
                ring.push_back( xf( { q.x * wid * 0.5f * wf, bottom + ( q.y + 1.0f ) * 0.5f * ( top - bottom ), z } ) );
            }
            rings.push_back( ring );
        }
        for ( int i = 0; i < N; ++i )
            for ( int sg = 0; sg < seg; ++sg )
            {
                int     t = ( sg + 1 ) % seg;
                Vector3 in = xf( { 0, 0, -len * 0.5f + len * ( i + 0.5f ) / N } );
                mb.quad( rings[i][sg], rings[i][t], rings[i + 1][t], rings[i + 1][sg], col, in );
            }
        for ( int end = 0; end < 2; ++end )
        {
            const std::vector<Vector3> &r = rings[end == 0 ? 0 : N];
            Vector3 mid = { 0, 0, 0 };
            for ( const Vector3 &v : r ) mid = Vector3Add( mid, v );
            mid = Vector3Scale( mid, 1.0f / r.size() );
            Vector3 in = xf( { 0, 0, end == 0 ? len * 0.3f : -len * 0.3f } );
            for ( int sg = 0; sg < seg; ++sg ) mb.tri( mid, r[sg], r[( sg + 1 ) % seg], col, in );
        }
        return;
    }
    if ( k == "cabin" )
    {
        // The greenhouse: a prism with a shorter, narrower roof; the windows are glass, the roof the colour.
        float len = p.num( "len", 2.0f ), wid = p.num( "wid", 1.5f ), hgt = p.num( "hgt", 0.5f );
        float fr = clampf( p.num( "front", 0.35f ), 0.0f, 0.9f ), bk = clampf( p.num( "back", 0.3f ), 0.0f, 0.9f );
        float top = clampf( p.num( "top", 0.85f ), 0.2f, 1.0f );
        if ( fr + bk > 0.95f ) bk = 0.95f - fr;
        Color glass = p.color( "glass", { 26, 34, 40, 255 } );
        glass.a = col.a;
        float   y0 = -hgt * 0.5f, y1 = hgt * 0.5f, w0 = wid * 0.5f, w1 = wid * 0.5f * top;
        float   z0 = -len * 0.5f, z1 = len * 0.5f, t0 = z0 + fr * len, t1 = z1 - bk * len;
        Vector3 b[4] = { xf( { -w0, y0, z0 } ), xf( { w0, y0, z0 } ), xf( { w0, y0, z1 } ), xf( { -w0, y0, z1 } ) };
        Vector3 t[4] = { xf( { -w1, y1, t0 } ), xf( { w1, y1, t0 } ), xf( { w1, y1, t1 } ), xf( { -w1, y1, t1 } ) };
        mb.quad( t[0], t[1], t[2], t[3], col, center );     // roof
        mb.quad( b[0], b[1], t[1], t[0], glass, center );   // windscreen
        mb.quad( b[2], b[3], t[3], t[2], glass, center );   // rear window
        mb.quad( b[1], b[2], t[2], t[1], glass, center );   // right
        mb.quad( b[3], b[0], t[0], t[3], glass, center );   // left
        // The pillars in the body colour, a little proud of the glass.
        auto bar = [&]( Vector3 a, Vector3 c2 ) {
            Vector3 d = Vector3Subtract( c2, a );
            Vector3 side = Vector3CrossProduct( d, { 0, 1, 0 } );
            side = Vector3Length( side ) < 1e-4f ? Vector3{ 1, 0, 0 } : Vector3Normalize( side );
            Vector3 o = Vector3Scale( side, 0.05f ), up = { 0, 0.03f, 0 };
            mb.quad( Vector3Add( a, o ), Vector3Add( c2, o ), Vector3Add( Vector3Add( c2, o ), up ),
                     Vector3Add( Vector3Add( a, o ), up ), col, center );
            mb.quad( Vector3Subtract( a, o ), Vector3Subtract( c2, o ), Vector3Add( Vector3Subtract( c2, o ), up ),
                     Vector3Add( Vector3Subtract( a, o ), up ), col, center );
        };
        for ( int i = 0; i < 4; ++i ) bar( b[i], t[i] );
        return;
    }
    if ( k == "hull" )
    {
        float       len = p.num( "len", 40 ), wid = p.num( "wid", 14 ), hgt = p.num( "hgt", 8 );
        std::string shape = p.str( "shape", "dart" );
        int         seg = std::clamp( p.integer( "seg", 8 ), 3, 24 );
        float       round = p.num( "round", 2.0f );
        float       belly = p.num( "belly", 0.55f );
        float       nose = p.num( "nose", 0.0f ), tail = p.num( "tail", 0.0f );
        const int   N = 14;
        std::vector<std::vector<Vector3>> rings;
        for ( int i = 0; i <= N; ++i )
        {
            float u = i / (float)N;
            float w = hullWidth( shape, u ), h = hullHeight( shape, u );
            if ( nose > 0 && u < 0.3f ) w = std::max( w, nose * ( 1 - u / 0.3f ) + w * u / 0.3f );
            if ( tail > 0 && u > 0.7f ) w = std::max( w, tail );
            w = std::max( w, 0.02f );
            h = std::max( h * ( 0.4f + 0.6f * w ), 0.03f );
            float z = -len * 0.5f + len * u;
            std::vector<Vector3> ring;
            for ( int s = 0; s < seg; ++s )
            {
                float   a = 2 * Pi * s / seg + Pi / seg;
                Vector2 q = superellipse( a, round );
                float   y = q.y > 0 ? q.y * hgt * 0.5f * h : q.y * hgt * 0.5f * h * belly;
                ring.push_back( xf( { q.x * wid * 0.5f * w, y, z } ) );
            }
            rings.push_back( ring );
        }
        for ( int i = 0; i < N; ++i )
            for ( int s = 0; s < seg; ++s )
            {
                int     t = ( s + 1 ) % seg;
                Vector3 in = xf( { 0, 0, -len * 0.5f + len * ( i + 0.5f ) / N } );
                mb.quad( rings[i][s], rings[i][t], rings[i + 1][t], rings[i + 1][s], col, in );
            }
        // Caps.
        for ( int end = 0; end < 2; ++end )
        {
            const std::vector<Vector3> &r = rings[end == 0 ? 0 : N];
            Vector3 mid = { 0, 0, 0 };
            for ( const Vector3 &v : r ) mid = Vector3Add( mid, v );
            mid = Vector3Scale( mid, 1.0f / r.size() );
            Vector3 in = xf( { 0, 0, end == 0 ? -len * 0.3f : len * 0.3f } );
            for ( int s = 0; s < seg; ++s ) mb.tri( mid, r[s], r[( s + 1 ) % seg], col, in );
        }
    }
    else if ( k == "wing" || k == "fin" )
    {
        bool  fin = k == "fin";
        float span = p.num( fin ? "h" : "span", fin ? 10 : 24 ), root = p.num( "root", 16 ), tip = p.num( "tip", 6 );
        float sweep = p.num( "sweep", root * 0.5f ), thick = p.num( "thick", fin ? 1.2f : 1.6f );
        float dihedral = p.num( "dihedral", 0.0f );
        Color stripe = p.color( "stripe", col );
        stripe.a = col.a;
        // The root chord's leading edge is at z - root/2.
        float   rz = -root * 0.5f;
        Vector3 a, b, c, d;   // root lead, tip lead, tip trail, root trail (top face)
        if ( fin )
        {
            a = { 0, 0, rz };
            b = { 0, span, rz + sweep };
            c = { 0, span, rz + sweep + tip };
            d = { 0, 0, rz + root };
        }
        else
        {
            a = { 0, 0, rz };
            b = { span, dihedral, rz + sweep };
            c = { span, dihedral, rz + sweep + tip };
            d = { 0, 0, rz + root };
        }
        Vector3 off = fin ? Vector3{ thick * 0.5f, 0, 0 } : Vector3{ 0, thick * 0.5f, 0 };
        Vector3 offTip = fin ? off : Vector3{ 0, thick * 0.25f, 0 };
        Vector3 A = Vector3Add( a, off ), B = Vector3Add( b, offTip ), C = Vector3Add( c, offTip ), D = Vector3Add( d, off );
        Vector3 A2 = Vector3Subtract( a, off ), B2 = Vector3Subtract( b, offTip ), C2 = Vector3Subtract( c, offTip ),
                D2 = Vector3Subtract( d, off );
        Vector3 in = xf( Vector3Scale( Vector3Add( Vector3Add( a, b ), Vector3Add( c, d ) ), 0.25f ) );
        if ( p.has( "stripe" ) && !fin )
        {
            // The outer quarter in the stripe's colour.
            Vector3 m1 = Vector3Lerp( A, B, 0.72f ), m2 = Vector3Lerp( D, C, 0.72f );
            Vector3 m1b = Vector3Lerp( A2, B2, 0.72f ), m2b = Vector3Lerp( D2, C2, 0.72f );
            mb.quad( xf( A ), xf( m1 ), xf( m2 ), xf( D ), col, in );
            mb.quad( xf( m1 ), xf( B ), xf( C ), xf( m2 ), stripe, in );
            mb.quad( xf( A2 ), xf( m1b ), xf( m2b ), xf( D2 ), col, in );
            mb.quad( xf( m1b ), xf( B2 ), xf( C2 ), xf( m2b ), stripe, in );
        }
        else
        {
            mb.quad( xf( A ), xf( B ), xf( C ), xf( D ), col, in );
            mb.quad( xf( A2 ), xf( B2 ), xf( C2 ), xf( D2 ), col, in );
        }
        mb.quad( xf( A ), xf( B ), xf( B2 ), xf( A2 ), col, in );
        mb.quad( xf( B ), xf( C ), xf( C2 ), xf( B2 ), col, in );
        mb.quad( xf( C ), xf( D ), xf( D2 ), xf( C2 ), col, in );
        mb.quad( xf( D ), xf( A ), xf( A2 ), xf( D2 ), col, in );
    }
    else if ( k == "engine" || k == "gun" || k == "disc" || k == "cone" || k == "spike" )
    {
        // Cylinders and cones along z (disc: about y).
        bool  disc = k == "disc", cone = k == "cone" || k == "spike";
        float r = p.num( "r", k == "gun" ? 1.4f : k == "spike" ? 2.0f : 4.0f );
        float r2 = cone ? p.num( "r2", 0.0f ) : p.num( "r2", r );
        float l = p.num( disc ? "h" : "len", k == "gun" ? 12.0f : disc ? 3.0f : 14.0f );
        int   seg = std::clamp( p.integer( "seg", disc ? 16 : 10 ), 3, 32 );
        auto  P = [&]( float a, float rr, float z ) {
            Vector3 v = disc ? Vector3{ std::cos( a ) * rr, z, std::sin( a ) * rr } : Vector3{ std::cos( a ) * rr, std::sin( a ) * rr, z };
            return xf( v );
        };
        // Engines and guns: the front (z0) is -l/2; a cone points forwards (its tip at -l/2).
        float z0 = disc ? -l * 0.5f : -l * 0.5f, z1 = l * 0.5f;
        float rf = cone ? r2 : r, rb = cone ? r : r2;
        for ( int s = 0; s < seg; ++s )
        {
            float a0 = 2 * Pi * s / seg, a1 = 2 * Pi * ( s + 1 ) / seg;
            mb.quad( P( a0, rf, z0 ), P( a1, rf, z0 ), P( a1, rb, z1 ), P( a0, rb, z1 ), col, center );
            if ( rf > 0.01f ) mb.tri( xf( disc ? Vector3{ 0, z0, 0 } : Vector3{ 0, 0, z0 } ), P( a0, rf, z0 ), P( a1, rf, z0 ), col, center );
        }
        Color back = col;
        if ( k == "engine" )
        {
            back = p.color( "glow", { 255, 150, 60, 255 } );
            back.a = 255;
            Vector3 nozzle = xf( { 0, 0, z1 + 0.3f } );
            out.m_engines.push_back( nozzle );
            out.m_engineColors.push_back( back );
        }
        for ( int s = 0; s < seg; ++s )
        {
            float   a0 = 2 * Pi * s / seg, a1 = 2 * Pi * ( s + 1 ) / seg;
            Vector3 mid = xf( disc ? Vector3{ 0, z1, 0 } : Vector3{ 0, 0, z1 } );
            Vector3 inner = xf( disc ? Vector3{ 0, z0, 0 } : Vector3{ 0, 0, z0 } );
            if ( rb > 0.01f ) mb.tri( mid, P( a0, rb * ( k == "engine" ? 0.8f : 1.0f ), z1 + ( k == "engine" ? 0.2f : 0 ) ), P( a1, rb * ( k == "engine" ? 0.8f : 1.0f ), z1 + ( k == "engine" ? 0.2f : 0 ) ), back, inner );
        }
    }
    else if ( k == "dome" || k == "sphere" || k == "ball" || k == "light" )
    {
        bool  half = k == "dome";
        float r = p.num( "r", k == "light" ? 1.6f : 6.0f );
        float rx = p.num( "rx", r ), ry = p.num( "ry", half ? r * 0.7f : r ), rz = p.num( "rz", r );
        int   seg = std::clamp( p.integer( "seg", k == "light" ? 6 : 12 ), 4, 32 );
        int   rings = half ? seg / 3 + 1 : seg / 2;
        if ( k == "light" )
        {
            Color g = p.color( "color", { 255, 220, 140, 255 } );
            g.a = 255;
            col = g;
        }
        if ( k == "dome" && p.num( "glass", 0 ) > 0 ) col.a = (unsigned char)std::max<int>( col.a, 70 );
        auto P = [&]( int i, int j ) {
            float th = half ? ( Pi * 0.5f ) * i / rings : Pi * i / rings;   // from the top
            float ph = 2 * Pi * j / seg;
            return xf( { std::sin( th ) * std::cos( ph ) * rx, std::cos( th ) * ry, std::sin( th ) * std::sin( ph ) * rz } );
        };
        for ( int i = 0; i < rings; ++i )
            for ( int j = 0; j < seg; ++j )
                mb.quad( P( i, j ), P( i, j + 1 ), P( i + 1, j + 1 ), P( i + 1, j ), col, center );
    }
    else if ( k == "box" )
    {
        float w = p.num( "w", 8 ) * 0.5f, h = p.num( "h", 4 ) * 0.5f, d = p.num( "d", 8 ) * 0.5f;
        float bev = clampf( p.num( "taper", 0.0f ), 0.0f, 0.95f );   // the top smaller than the bottom
        Vector3 v[8] = { { -w, -h, -d }, { w, -h, -d }, { w, -h, d }, { -w, -h, d },
                         { -w * ( 1 - bev ), h, -d * ( 1 - bev ) }, { w * ( 1 - bev ), h, -d * ( 1 - bev ) },
                         { w * ( 1 - bev ), h, d * ( 1 - bev ) }, { -w * ( 1 - bev ), h, d * ( 1 - bev ) } };
        for ( Vector3 &q : v ) q = xf( q );
        int f[6][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 } };
        for ( auto &q : f ) mb.quad( v[q[0]], v[q[1]], v[q[2]], v[q[3]], col, center );
    }
    else if ( k == "ring" )
    {
        float R = p.num( "r", 14 ), t = p.num( "thick", 2.5f );
        int   seg = std::clamp( p.integer( "seg", 20 ), 6, 48 ), side = 6;
        auto  P = [&]( int i, int j ) {
            float a = 2 * Pi * i / seg, b = 2 * Pi * j / side;
            float rr = R + std::cos( b ) * t;
            return xf( { std::cos( a ) * rr, std::sin( b ) * t, std::sin( a ) * rr } );
        };
        for ( int i = 0; i < seg; ++i )
            for ( int j = 0; j < side; ++j )
            {
                float   a = 2 * Pi * ( i + 0.5f ) / seg;
                Vector3 in = xf( { std::cos( a ) * R, 0, std::sin( a ) * R } );
                mb.quad( P( i, j ), P( i + 1, j ), P( i + 1, j + 1 ), P( i, j + 1 ), col, in );
            }
    }
    else if ( k == "crystal" )
    {
        float   r = p.num( "r", 5 ), l = p.num( "len", 16 );
        int     seg = std::clamp( p.integer( "seg", 4 ), 3, 8 );
        Vector3 top = xf( { 0, 0, -l * 0.5f } ), bot = xf( { 0, 0, l * 0.5f } );
        for ( int s = 0; s < seg; ++s )
        {
            float   a0 = 2 * Pi * s / seg, a1 = 2 * Pi * ( s + 1 ) / seg;
            Vector3 p0 = xf( { std::cos( a0 ) * r, std::sin( a0 ) * r, -l * 0.1f } );
            Vector3 p1 = xf( { std::cos( a1 ) * r, std::sin( a1 ) * r, -l * 0.1f } );
            mb.tri( top, p0, p1, col, center );
            mb.tri( bot, p1, p0, col, center );
        }
    }
    else if ( k == "plate" )
    {
        // A flat outline "x,z;x,z;..." raised by its thickness (a fan from its middle: keep it star-shaped).
        std::vector<Vector2> pts;
        std::string          s = p.str( "pts" );
        size_t               a = 0;
        while ( a < s.size() )
        {
            size_t      b = s.find( ';', a );
            std::string one = s.substr( a, b == std::string::npos ? std::string::npos : b - a );
            float       x = 0, z = 0;
            if ( sscanf( one.c_str(), "%f,%f", &x, &z ) == 2 ) pts.push_back( { x, z } );
            if ( b == std::string::npos ) break;
            a = b + 1;
        }
        if ( pts.size() >= 3 )
        {
            float   t = p.num( "thick", 2.0f ) * 0.5f;
            Vector2 mid = { 0, 0 };
            for ( const Vector2 &q : pts ) mid = add( mid, q );
            mid = mul( mid, 1.0f / pts.size() );
            Vector3 in = xf( { mid.x, 0, mid.y } );
            for ( size_t i = 0; i < pts.size(); ++i )
            {
                Vector2 q0 = pts[i], q1 = pts[( i + 1 ) % pts.size()];
                mb.tri( xf( { mid.x, t, mid.y } ), xf( { q0.x, t, q0.y } ), xf( { q1.x, t, q1.y } ), col, in );
                mb.tri( xf( { mid.x, -t, mid.y } ), xf( { q1.x, -t, q1.y } ), xf( { q0.x, -t, q0.y } ), col, in );
                mb.quad( xf( { q0.x, t, q0.y } ), xf( { q1.x, t, q1.y } ), xf( { q1.x, -t, q1.y } ), xf( { q0.x, -t, q0.y } ), col, in );
            }
        }
    }
    else if ( k == "greeble" )
    {
        int   n = std::clamp( p.integer( "n", 8 ), 1, 200 );
        float w = p.num( "w", 16 ), d = p.num( "d", 24 ), sz = p.num( "size", 2.5f );
        Rng   r;
        r.seed( (uint32_t)p.integer( "seed", 7 ) );
        for ( int i = 0; i < n; ++i )
        {
            float   bw = sz * r.range( 0.5f, 1.4f ), bh = sz * r.range( 0.3f, 1.0f ), bd = sz * r.range( 0.5f, 1.8f );
            Vector3 at = { r.range( -w * 0.5f, w * 0.5f ), bh * 0.5f, r.range( -d * 0.5f, d * 0.5f ) };
            Color   c = scaleColor( col, r.range( 0.75f, 1.15f ) );
            c.a = col.a;
            Vector3 v[8] = { { -bw, -bh, -bd }, { bw, -bh, -bd }, { bw, -bh, bd }, { -bw, -bh, bd },
                             { -bw, bh, -bd },  { bw, bh, -bd },  { bw, bh, bd },  { -bw, bh, bd } };
            for ( Vector3 &q : v ) q = xf( Vector3Add( Vector3Scale( q, 0.5f ), at ) );
            Vector3 in = xf( at );
            int f[5][4] = { { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 } };
            for ( auto &q : f ) mb.quad( v[q[0]], v[q[1]], v[q[2]], v[q[3]], c, in );
        }
    }
}

/** @brief The stand-in for a missing model: a magenta box on wheels. */
ModelDef standIn()
{
    ModelDef d;
    d.m_name = "?";
    d.m_parts.push_back( parseLine( "body len=4.2 wid=1.8 hgt=0.7 y=0.6 color=#e040c0" ) );
    d.m_parts.push_back( parseLine( "cabin len=2.0 wid=1.5 hgt=0.5 y=1.2 z=0.2 color=#e040c0" ) );
    d.m_parts.push_back( parseLine( "wheel r=0.33 w=0.22 x=0.78 z=-1.3 front=1" ) );
    d.m_parts.push_back( parseLine( "wheel r=0.33 w=0.22 x=0.78 z=1.3" ) );
    return d;
}

}   // namespace

namespace
{

/** @brief The driver's colours put in for "paint", "paint2" and "paint3". */
void paintLine( Line &l, const Paint &paint )
{
    for ( const char *key : { "color", "stripe", "glass", "hub" } )
    {
        auto it = l.m_kv.find( key );
        if ( it == l.m_kv.end() ) continue;
        const std::string &v = it->second;
        int                i = v == "paint" ? 0 : v == "paint2" ? 1 : v == "paint3" ? 2 : -1;
        if ( i < 0 ) continue;
        char hex[16];
        snprintf( hex, sizeof hex, "#%02x%02x%02x", paint.m_c[i].r, paint.m_c[i].g, paint.m_c[i].b );
        it->second = hex;
    }
}

}   // namespace

ModelMesh Forge::build( const ModelDef &def, bool upload, const Paint &paint )
{
    ModelMesh out;
    MeshB     base, head, spin;
    uint32_t  h = 2166136261u;
    for ( char c : def.m_name ) h = ( h ^ (unsigned char)c ) * 16777619u;
    base.m_rng.seed( h );
    head.m_rng.seed( h + 1 );
    spin.m_rng.seed( h + 2 );
    float reach = 1.0f;
    for ( const Line &part : def.m_parts )
    {
        Line p = part;
        paintLine( p, paint );
        if ( p.m_key == "light" )
        {
            Color   lc = p.color( "color", { 255, 220, 160, 255 } );
            Vector3 at = { p.num( "x" ), p.num( "y" ), p.num( "z" ) };
            out.m_lights.push_back( { Vector3Scale( at, def.m_scale ), lc } );
            if ( at.x != 0 && p.integer( "mirror", 0 ) )
                out.m_lights.push_back( { Vector3Scale( { -at.x, at.y, at.z }, def.m_scale ), lc } );
        }
        std::string group = p.str( "group", "base" );
        MeshB      &mb = group == "head" ? head : group == "spin" ? spin : base;
        if ( group == "head" && p.has( "pivot" ) )
        {
            float px = 0, pz = 0;
            sscanf( p.str( "pivot" ).c_str(), "%f,%f", &px, &pz );
            out.m_pivot = { px * def.m_scale, 0, pz * def.m_scale };
        }
        if ( group == "spin" ) out.m_spinSpeed = p.num( "spin", out.m_spinSpeed );
        bool autoMirror = p.m_key == "wing" || ( ( p.m_key == "engine" || p.m_key == "gun" || p.m_key == "wheel" ) &&
                                                    p.num( "x" ) != 0 );
        bool mirror = p.integer( "mirror", autoMirror ? 1 : 0 ) != 0;
        for ( int m = 0; m < ( mirror ? 2 : 1 ); ++m )
        {
            Xf xf;
            xf.m_at = { p.num( "x" ), p.num( "y" ), p.num( "z" ) };
            xf.m_yaw = p.num( "rot", 0.0f );
            xf.m_pitch = p.num( "pitch", 0.0f );
            xf.m_roll = p.num( "roll", 0.0f );
            xf.m_flip = m == 1;
            size_t before = mb.m_pos.size();
            buildPart( mb, p, xf, out );
            for ( size_t i = before; i < mb.m_pos.size(); ++i ) mb.m_pos[i] *= def.m_scale;
        }
    }
    for ( Vector3 &e : out.m_engines ) e = Vector3Scale( e, def.m_scale );
    for ( WheelSpot &w : out.m_wheels )
    {
        w.m_pos = Vector3Scale( w.m_pos, def.m_scale );
        w.m_r *= def.m_scale;
        w.m_w *= def.m_scale;
    }
    out.m_lamp = def.m_lamp;
    for ( MeshB *mb : { &base, &head, &spin } )
        for ( size_t i = 0; i + 2 < mb->m_pos.size(); i += 3 )
            reach = std::max( reach, std::sqrt( mb->m_pos[i] * mb->m_pos[i] + mb->m_pos[i + 2] * mb->m_pos[i + 2] ) );
    // A mirrored triangle turns its winding round; the normals were oriented outwards already, so only the order
    // of the vertices matters to nobody (both sides are drawn).
    out.m_radius = reach;
    out.m_height = 0.0f;
    for ( MeshB *mb : { &base, &head, &spin } )
        for ( size_t i = 1; i < mb->m_pos.size(); i += 3 ) out.m_height = std::max( out.m_height, mb->m_pos[i] );
    out.m_hasBase = !base.m_pos.empty();
    out.m_hasHead = !head.m_pos.empty();
    out.m_hasSpin = !spin.m_pos.empty();
    if ( out.m_hasBase ) out.m_base = base.mesh( upload );
    if ( out.m_hasHead ) out.m_head = head.mesh( upload );
    if ( out.m_hasSpin ) out.m_spin = spin.mesh( upload );
    return out;
}

void Forge::clear()
{
    if ( m_hasWheel ) UnloadMesh( m_wheel );
    m_hasWheel = false;
    for ( auto &[n, m] : m_cache )
    {
        if ( m.m_hasBase ) UnloadMesh( m.m_base );
        if ( m.m_hasHead ) UnloadMesh( m.m_head );
        if ( m.m_hasSpin ) UnloadMesh( m.m_spin );
    }
    m_cache.clear();
}

const ModelMesh &Forge::get( const Catalog &cat, const std::string &name, const Paint &paint )
{
    char key[64];
    snprintf( key, sizeof key, "#%02x%02x%02x%02x%02x%02x%02x%02x%02x", paint.m_c[0].r, paint.m_c[0].g, paint.m_c[0].b,
              paint.m_c[1].r, paint.m_c[1].g, paint.m_c[1].b, paint.m_c[2].r, paint.m_c[2].g, paint.m_c[2].b );
    std::string id = name + key;
    auto        it = m_cache.find( id );
    if ( it != m_cache.end() ) return it->second;
    const ModelDef *d = cat.model( name );
    ModelMesh       m = d ? build( *d, true, paint ) : build( standIn(), true, paint );
    return m_cache[id] = m;
}

const Mesh &Forge::wheel()
{
    if ( m_hasWheel ) return m_wheel;
    m_wheel = buildWheel( true );
    m_hasWheel = true;
    return m_wheel;
}

Mesh Forge::buildWheel( bool upload )
{
    // A tyre of radius 1 and width 1 along x, its hub a lighter disc on both faces (white: tinted when drawn).
    MeshB     mb;
    mb.m_noise = 0.0f;
    const int seg = 14;
    Color     tyre = { 34, 34, 38, 0 }, tread = { 26, 26, 28, 0 }, hub = { 255, 255, 255, 0 };
    for ( int s = 0; s < seg; ++s )
    {
        float   a0 = 2 * Pi * s / seg, a1 = 2 * Pi * ( s + 1 ) / seg;
        Vector3 o0 = { 0, std::sin( a0 ), std::cos( a0 ) }, o1 = { 0, std::sin( a1 ), std::cos( a1 ) };
        Vector3 L = { -0.5f, 0, 0 }, R = { 0.5f, 0, 0 };
        mb.quad( Vector3Add( L, o0 ), Vector3Add( R, o0 ), Vector3Add( R, o1 ), Vector3Add( L, o1 ), s % 2 ? tyre : tread,
                 { 0, 0, 0 } );
        for ( float side : { -0.5f, 0.5f } )
        {
            Vector3 p0 = { side, o0.y, o0.z }, p1 = { side, o1.y, o1.z };
            Vector3 h0 = { side * 1.04f, o0.y * 0.62f, o0.z * 0.62f }, h1 = { side * 1.04f, o1.y * 0.62f, o1.z * 0.62f };
            mb.quad( p0, p1, h1, h0, tyre, { 0, 0, 0 } );
            mb.tri( Vector3{ side * 1.06f, 0, 0 }, h0, h1, ( s % 3 == 0 ) ? scaleColor( hub, 0.6f ) : hub, { 0, 0, 0 } );
        }
    }
    return mb.mesh( upload );
}

}   // namespace grave
