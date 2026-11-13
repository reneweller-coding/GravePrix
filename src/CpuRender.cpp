/**
 * @file CpuRender.cpp
 * @brief --gallery without a window or a graphics card: every model (or those with a prefix) drawn by a small
 *        software rasteriser (flat facets, the sun and a sky light, glowing parts) onto a sheet with its name, so
 *        the designers can look at their models while the graphics card is busy elsewhere.
 * @author Rene Weller
 */
#include "Forge.h"
#include "Tools.h"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace grave
{

namespace fs = std::filesystem;

namespace
{

struct Tri
{
    Vector3 m_p[3];
    Vector3 m_n;
    Color   m_c;
    float   m_emit;
};

void gather( const Mesh &m, Matrix xf, Color tint, bool hubTint, std::vector<Tri> &out )
{
    if ( !m.vertices ) return;
    Matrix nm = MatrixTranspose( MatrixInvert( xf ) );
    for ( int i = 0; i + 2 < m.vertexCount; i += 3 )
    {
        Tri t;
        for ( int k = 0; k < 3; ++k )
            t.m_p[k] = Vector3Transform( { m.vertices[( i + k ) * 3], m.vertices[( i + k ) * 3 + 1], m.vertices[( i + k ) * 3 + 2] }, xf );
        Vector3 n = { m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2] };
        t.m_n = Vector3Normalize( { nm.m0 * n.x + nm.m4 * n.y + nm.m8 * n.z, nm.m1 * n.x + nm.m5 * n.y + nm.m9 * n.z,
                                    nm.m2 * n.x + nm.m6 * n.y + nm.m10 * n.z } );
        unsigned char *c = m.colors + i * 4;
        t.m_c = { c[0], c[1], c[2], 255 };
        if ( hubTint && c[0] > 120 ) t.m_c = { (unsigned char)( c[0] * tint.r / 255 ), (unsigned char)( c[1] * tint.g / 255 ), (unsigned char)( c[2] * tint.b / 255 ), 255 };
        t.m_emit = c[3] / 255.0f;
        out.push_back( t );
    }
}

void freeCpu( Mesh &m )
{
    MemFree( m.vertices );
    MemFree( m.normals );
    MemFree( m.colors );
    MemFree( m.texcoords );   // the marks of crowds and swaying models
    MemFree( m.texcoords2 );
    m = {};
}

/** @brief Draws triangles into a picture: a 3/4 view from the front right, the model filling it. */
void raster( std::vector<Tri> &tris, Image &img, float yaw )
{
    int    W = img.width, H = img.height;
    Color *px = (Color *)img.data;
    std::vector<float> depth( (size_t)W * H, 1e9f );
    Vector3 eye = Vector3Normalize( { 0.62f, 0.62f, 0.8f } );
    Vector3 fwd = Vector3Negate( eye ), right = Vector3Normalize( Vector3CrossProduct( fwd, { 0, 1, 0 } ) );
    Vector3 up = Vector3CrossProduct( right, fwd );
    Matrix  rot = MatrixRotateY( yaw );
    float   ext = 0.5f;
    Vector3 lo = { 1e9f, 1e9f, 1e9f }, hi = { -1e9f, -1e9f, -1e9f };
    for ( Tri &t : tris )
    {
        for ( Vector3 &p : t.m_p )
        {
            p = Vector3Transform( p, rot );
            lo = Vector3Min( lo, p );
            hi = Vector3Max( hi, p );
        }
        t.m_n = Vector3Transform( t.m_n, rot );
    }
    Vector3 mid = Vector3Scale( Vector3Add( lo, hi ), 0.5f );
    for ( const Tri &t : tris )
        for ( const Vector3 &p : t.m_p )
        {
            Vector3 d = Vector3Subtract( p, mid );
            ext = std::max( ext, std::max( std::fabs( Vector3DotProduct( d, right ) ), std::fabs( Vector3DotProduct( d, up ) ) ) );
        }
    float   scale = std::min( W, H ) * 0.44f / ext;
    Vector3 sun = Vector3Normalize( { -0.35f, 0.85f, 0.4f } );
    for ( const Tri &t : tris )
    {
        Vector2 s[3];
        float   z[3];
        for ( int k = 0; k < 3; ++k )
        {
            Vector3 d = Vector3Subtract( t.m_p[k], mid );
            s[k] = { W * 0.5f + Vector3DotProduct( d, right ) * scale, H * 0.5f - Vector3DotProduct( d, up ) * scale };
            z[k] = Vector3DotProduct( d, fwd );
        }
        float area = ( s[1].x - s[0].x ) * ( s[2].y - s[0].y ) - ( s[2].x - s[0].x ) * ( s[1].y - s[0].y );
        if ( std::fabs( area ) < 1e-6f ) continue;
        // Both sides drawn; the normal faces the viewer.
        Vector3 n = t.m_n;
        if ( Vector3DotProduct( n, eye ) < 0 ) n = Vector3Negate( n );
        float diff = std::max( 0.0f, Vector3DotProduct( n, sun ) );
        float sky = 0.5f + 0.5f * n.y;
        float rim = std::pow( 1.0f - std::max( 0.0f, Vector3DotProduct( n, eye ) ), 3.0f ) * 0.25f;
        float k = 0.28f + 0.3f * sky + 0.75f * diff + rim;
        float e = t.m_emit * 1.4f;
        Color c = { (unsigned char)clampf( t.m_c.r * ( k + e ), 0, 255 ), (unsigned char)clampf( t.m_c.g * ( k + e ), 0, 255 ),
                    (unsigned char)clampf( t.m_c.b * ( k + e ), 0, 255 ), 255 };
        int x0 = std::max( 0, (int)std::floor( std::min( { s[0].x, s[1].x, s[2].x } ) ) );
        int x1 = std::min( W - 1, (int)std::ceil( std::max( { s[0].x, s[1].x, s[2].x } ) ) );
        int y0 = std::max( 0, (int)std::floor( std::min( { s[0].y, s[1].y, s[2].y } ) ) );
        int y1 = std::min( H - 1, (int)std::ceil( std::max( { s[0].y, s[1].y, s[2].y } ) ) );
        for ( int y = y0; y <= y1; ++y )
            for ( int x = x0; x <= x1; ++x )
            {
                float px0 = x + 0.5f, py0 = y + 0.5f;
                float w0 = ( ( s[1].x - px0 ) * ( s[2].y - py0 ) - ( s[2].x - px0 ) * ( s[1].y - py0 ) ) / area;
                float w1 = ( ( s[2].x - px0 ) * ( s[0].y - py0 ) - ( s[0].x - px0 ) * ( s[2].y - py0 ) ) / area;
                float w2 = 1.0f - w0 - w1;
                if ( w0 < 0 || w1 < 0 || w2 < 0 ) continue;
                float zz = w0 * z[0] + w1 * z[1] + w2 * z[2];
                size_t i = (size_t)y * W + x;
                if ( zz >= depth[i] ) continue;
                depth[i] = zz;
                px[i] = c;
            }
    }
}

/** @brief A font's glyphs read without a window (for the labels). */
struct Glyphs
{
    GlyphInfo *m_g = nullptr;
    int        m_n = 0;
    int        m_size = 18;

    void load( const std::string &ttf )
    {
        std::ifstream in( ttf, std::ios::binary );
        if ( !in ) return;
        std::vector<unsigned char> data( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
        m_n = 95;
        m_g = LoadFontData( data.data(), (int)data.size(), m_size, nullptr, m_n, FONT_DEFAULT );
    }

    void draw( Image &dst, const std::string &text, int x, int y, Color c ) const
    {
        if ( !m_g ) return;
        for ( char ch : text )
        {
            int i = ch - 32;
            if ( i < 0 || i >= m_n ) continue;
            const GlyphInfo &g = m_g[i];
            Image            im = ImageCopy( g.image );
            ImageColorTint( &im, c );
            ImageDraw( &dst, im, { 0, 0, (float)im.width, (float)im.height },
                       { (float)( x + g.offsetX ), (float)( y + g.offsetY ), (float)im.width, (float)im.height }, WHITE );
            UnloadImage( im );
            x += g.advanceX ? g.advanceX : im.width;
        }
    }

    ~Glyphs()
    {
        if ( m_g ) UnloadFontData( m_g, m_n );
    }
};

Paint paintOf( int k )
{
    static const Color a[] = { { 160, 30, 30, 255 }, { 30, 70, 150, 255 }, { 200, 160, 30, 255 }, { 40, 120, 60, 255 },
                               { 120, 40, 140, 255 }, { 210, 210, 210, 255 }, { 30, 30, 34, 255 }, { 220, 110, 30, 255 } };
    Paint p;
    p.m_c[0] = a[k % 8];
    p.m_c[1] = a[( k + 3 ) % 8];
    p.m_c[2] = { 220, 220, 210, 255 };
    return p;
}

}   // namespace

int toolGallery( const Options &opt )
{
    std::string file = opt.value( "--gallery", "work/gallery.png" ), prefix = opt.value2( "--gallery" );
    // The prefix may be several, comma-separated ("scrap_,car_cube").
    std::vector<std::string> prefixes, names;
    for ( size_t a = 0, b; a <= prefix.size(); a = b + 1 )
    {
        b = prefix.find( ',', a );
        if ( b == std::string::npos ) b = prefix.size();
        prefixes.push_back( prefix.substr( a, b - a ) );
    }
    for ( const auto &[n, m] : g_data.m_models )
        for ( const std::string &pre : prefixes )
            if ( n.rfind( pre, 0 ) == 0 )
            {
                names.push_back( n );
                break;
            }
    if ( names.empty() )
    {
        printf( "no models%s%s\n", prefix.empty() ? "" : " starting with ", prefix.c_str() );
        return 1;
    }
    const int cell = (int)opt.num( "--cell", 300 ), label = 24, cols = std::min( (int)opt.num( "--cols", 5 ), (int)names.size() );
    int       rows = ( (int)names.size() + cols - 1 ) / cols;
    Image     sheet = GenImageColor( cols * cell, rows * ( cell + label ), { 18, 18, 22, 255 } );
    Glyphs    font;
    font.load( opt.m_root + "/assets/fonts/Barlow-Regular.ttf" );
    Mesh  wheel = Forge::buildWheel( false );
    float yaw = opt.num( "--yaw", 0.0f ) * Deg;
    for ( size_t i = 0; i < names.size(); ++i )
    {
        ModelMesh        m = Forge::build( g_data.m_models.at( names[i] ), false, paintOf( (int)i ) );
        // A modelled car (assets/cars/<model>.glb): its body from there, as in the game (not with --forge-cars).
        std::string glb = opt.m_root + "/assets/cars/" + names[i] + ".glb";
        Forge::Glb  g;
        std::error_code gec;
        if ( !opt.has( "--forge-cars" ) && fs::exists( glb, gec ) && Forge::readGlb( glb, g ) )
        {
            for ( Mesh *mesh : { &m.m_base, &m.m_head, &m.m_spin } )
                if ( mesh->vertices ) freeCpu( *mesh );
            m.m_base = Forge::glbMesh( g, paintOf( (int)i ), false );
            m.m_radius = std::max( m.m_radius, g.m_radius );
            m.m_height = g.m_height;
        }
        std::vector<Tri> tris;
        for ( Mesh *mesh : { &m.m_base, &m.m_head, &m.m_spin } ) gather( *mesh, MatrixIdentity(), WHITE, false, tris );
        for ( const WheelSpot &ws : m.m_wheels )
        {
            Matrix xf = MatrixMultiply( MatrixScale( ws.m_w, ws.m_r, ws.m_r ), MatrixTranslate( ws.m_pos.x, ws.m_pos.y, ws.m_pos.z ) );
            gather( wheel, xf, ws.m_hub, true, tris );
        }
        // Two pixels a pixel, then halved: smoother edges.
        Image big = GenImageColor( cell * 2, cell * 2, { 26, 26, 32, 255 } );
        raster( tris, big, yaw );
        ImageResize( &big, cell, cell );
        int x = (int)( i % cols ) * cell, y = (int)( i / cols ) * ( cell + label );
        ImageDraw( &sheet, big, { 0, 0, (float)cell, (float)cell }, { (float)x, (float)y, (float)cell, (float)cell }, WHITE );
        UnloadImage( big );
        char text[200];
        snprintf( text, sizeof text, "%s  r%.1f h%.1f  %zu tris", names[i].c_str(), m.m_radius, m.m_height, tris.size() );
        font.draw( sheet, text, x + 6, y + cell + 2, { 230, 230, 230, 255 } );
        for ( Mesh *mesh : { &m.m_base, &m.m_head, &m.m_spin } )
            if ( mesh->vertices ) freeCpu( *mesh );
    }
    freeCpu( wheel );
    std::error_code ec;
    fs::create_directories( fs::path( file ).parent_path(), ec );
    ExportImage( sheet, file.c_str() );
    UnloadImage( sheet );
    printf( "%zu models -> %s\n", names.size(), file.c_str() );
    return 0;
}

}   // namespace grave
