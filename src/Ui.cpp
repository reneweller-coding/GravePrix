/**
 * @file Ui.cpp
 * @brief The interface's drawing.
 * @author Rene Weller
 */
#include "Ui.h"

#include "Lang.h"

#include <rlgl.h>

#include <cmath>
#include <vector>

namespace grave
{
namespace ui
{

Color Ink = { 238, 230, 214, 255 };
Color Dim = { 150, 140, 130, 255 };
Color Accent = { 214, 52, 44, 255 };
Color Gold = { 222, 178, 92, 255 };
Color Warn = { 255, 96, 72, 255 };
Color Good = { 130, 224, 130, 255 };

namespace
{

Font  g_font = {};
bool  g_has = false;
float g_textSize = 1.0f;
float g_scale = 1.0f;
constexpr int Base = 72;   ///< the size the font is rendered at

}   // namespace

void init( const std::string &assets )
{
    // Latin-1 and the few marks the texts use.
    std::vector<int> cps;
    for ( int c = 32; c < 127; ++c ) cps.push_back( c );
    for ( int c = 160; c < 384; ++c ) cps.push_back( c );   // Latin-1 and Latin Extended-A (names like Ilić)
    for ( int c : { 0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E, 0x2026, 0x2022, 0x2192, 0x2190,
                    0x2191, 0x2193, 0x00B7, 0x2122, 0x221E, 0x00D7, 0x25B2, 0x25BC, 0x25C6, 0x2605, 0x2212, 0x271D, 0x2020 } )
        cps.push_back( c );
    std::string path = assets + "/fonts/Barlow-Regular.ttf";
    if ( FileExists( path.c_str() ) )
    {
        g_font = LoadFontEx( path.c_str(), Base, cps.data(), (int)cps.size() );
        GenTextureMipmaps( &g_font.texture );
        SetTextureFilter( g_font.texture, TEXTURE_FILTER_TRILINEAR );
        g_has = true;
    }
    else
        g_font = GetFontDefault();
}

const Font &font() { return g_font; }

void shutdown()
{
    if ( g_has ) UnloadFont( g_font );
    g_has = false;
}

void setTextSize( float s ) { g_textSize = s; }

float k() { return GetScreenHeight() / 1080.0f * g_scale; }

static float px( float size );

void setScale( float s ) { g_scale = s; }

void setPalette( int mode )
{
    Gold = mode == 2 ? Color{ 240, 130, 170, 255 } : Color{ 222, 178, 92, 255 };
    Warn = mode == 1 ? Color{ 255, 150, 30, 255 } : Color{ 255, 96, 72, 255 };
    Good = mode == 1 ? Color{ 90, 170, 255, 255 } : mode == 2 ? Color{ 90, 220, 200, 255 } : Color{ 130, 224, 130, 255 };
}

float glyph( const std::string &label, float x, float y, float size )
{
    // The face buttons in the colours printed on most pads (and the Deck's).
    struct Face
    {
        const char *name;
        Color       c;
    };
    static const Face faces[] = { { "A", { 60, 170, 80, 255 } }, { "B", { 200, 60, 50, 255 } },
                                  { "X", { 50, 110, 200, 255 } }, { "Y", { 220, 180, 40, 255 } } };
    float h = px( size ) * 1.15f, cy = y + h * 0.5f;
    for ( const Face &f : faces )
        if ( label == f.name )
        {
            DrawCircleV( { x + h * 0.5f, cy }, h * 0.5f, { 12, 10, 12, 255 } );
            DrawCircleV( { x + h * 0.5f, cy }, h * 0.44f, f.c );
            float w = width( label, size * 0.8f );
            DrawTextEx( g_font, label.c_str(), { std::floor( x + ( h - w ) * 0.5f ), std::floor( cy - px( size * 0.8f ) * 0.55f ) },
                        px( size * 0.8f ), px( size * 0.8f ) * 0.02f, { 250, 250, 245, 255 } );
            return h;
        }
    // The cross's directions (D_UP, D_DOWN, D_LEFT, D_RIGHT): a triangle in a square pill.
    if ( label.rfind( "D_", 0 ) == 0 )
    {
        Rectangle r = { x, y + h * 0.1f, h * 0.9f, h * 0.8f };
        DrawRectangleRounded( r, 0.4f, 6, { 12, 10, 12, 255 } );
        DrawRectangleRoundedLinesEx( r, 0.4f, 6, std::max( 1.0f, k() * 1.5f ), { 200, 200, 195, 255 } );
        Vector2 c = { r.x + r.width * 0.5f, r.y + r.height * 0.5f };
        float   s = h * 0.22f;
        Color   w = { 240, 240, 235, 255 };
        if ( label == "D_UP" ) DrawTriangle( { c.x, c.y - s }, { c.x - s, c.y + s }, { c.x + s, c.y + s }, w );
        else if ( label == "D_DOWN" ) DrawTriangle( { c.x - s, c.y - s }, { c.x, c.y + s }, { c.x + s, c.y - s }, w );
        else if ( label == "D_LEFT" ) DrawTriangle( { c.x - s, c.y }, { c.x + s, c.y + s }, { c.x + s, c.y - s }, w );
        else DrawTriangle( { c.x + s, c.y }, { c.x - s, c.y - s }, { c.x - s, c.y + s }, w );
        return r.width;
    }
    static const char *pills[] = { "LB", "RB", "LT", "RT", "Start", "Select", "L3", "R3" };
    for ( const char *p : pills )
        if ( label == p )
        {
            float     w = width( label, size * 0.7f ) + h * 0.7f;
            Rectangle r = { x, y + h * 0.1f, w, h * 0.8f };
            DrawRectangleRounded( r, 0.6f, 6, { 12, 10, 12, 255 } );
            DrawRectangleRoundedLinesEx( r, 0.6f, 6, std::max( 1.0f, k() * 1.5f ), { 200, 200, 195, 255 } );
            DrawTextEx( g_font, label.c_str(), { std::floor( x + h * 0.35f ), std::floor( cy - px( size * 0.7f ) * 0.55f ) },
                        px( size * 0.7f ), px( size * 0.7f ) * 0.02f, { 240, 240, 235, 255 } );
            return w;
        }
    return 0.0f;
}

static float px( float size ) { return size * k() * g_textSize; }

float width( const std::string &s, float size )
{
    return MeasureTextEx( g_font, s.c_str(), px( size ), px( size ) * 0.02f ).x;
}

float height( float size ) { return px( size ); }

float text( const std::string &s0, float x, float y, float size, Color c, AlignE align )
{
    // Every text is looked up in the language's table (a whole literal is its own key; data texts, numbers and
    // texts built of parts pass unchanged).
    const std::string s = T( s0 );
    float w = width( s, size );
    if ( align == AlignE::CENTER ) x -= w * 0.5f;
    else if ( align == AlignE::RIGHT ) x -= w;
    DrawTextEx( g_font, s.c_str(), { std::floor( x ), std::floor( y ) }, px( size ), px( size ) * 0.02f, c );
    return w;
}

std::vector<std::string> wrap( const std::string &s, float w, float size )
{
    std::vector<std::string> out;
    std::string              line, word;
    auto                     flush = [&]() {
        if ( word.empty() ) return;
        std::string tryLine = line.empty() ? word : line + " " + word;
        if ( !line.empty() && width( tryLine, size ) > w )
        {
            out.push_back( line );
            line = word;
        }
        else
            line = tryLine;
        word.clear();
    };
    for ( char c : s )
    {
        if ( c == ' ' )
            flush();
        else if ( c == '\n' )
        {
            flush();
            out.push_back( line );
            line.clear();
        }
        else
            word += c;
    }
    flush();
    if ( !line.empty() ) out.push_back( line );
    return out;
}

void panel( Rectangle r, float alpha, Color edge )
{
    DrawRectangleGradientV( (int)r.x, (int)r.y, (int)r.width, (int)r.height, { 22, 14, 16, (unsigned char)( 255 * alpha ) },
                            { 8, 5, 7, (unsigned char)( 255 * alpha ) } );
    DrawRectangleLinesEx( r, std::max( 1.0f, k() * 1.5f ), { edge.r, edge.g, edge.b, 140 } );
    // Corner marks.
    float c = 14 * k(), t = std::max( 2.0f, 3 * k() );
    Color e = { edge.r, edge.g, edge.b, 230 };
    DrawRectangleRec( { r.x, r.y, c, t }, e );
    DrawRectangleRec( { r.x, r.y, t, c }, e );
    DrawRectangleRec( { r.x + r.width - c, r.y + r.height - t, c, t }, e );
    DrawRectangleRec( { r.x + r.width - t, r.y + r.height - c, t, c }, e );
}

void bar( Rectangle r, float share, Color c, Color back )
{
    share = share < 0 ? 0 : share > 1 ? 1 : share;
    DrawRectangleRec( r, back );
    Rectangle f = { r.x, r.y, r.width * share, r.height };
    DrawRectangleGradientH( (int)f.x, (int)f.y, (int)f.width, (int)f.height, { (unsigned char)( c.r * 0.6f ), (unsigned char)( c.g * 0.6f ), (unsigned char)( c.b * 0.6f ), 255 }, c );
    DrawRectangleLinesEx( r, std::max( 1.0f, k() ), { 255, 255, 255, 50 } );
}

void pips( float x, float y, int n, int max, float size, Color on, Color off )
{
    float s = size * k(), gap = s * 0.3f;
    for ( int i = 0; i < max; ++i )
    {
        Rectangle r = { x + i * ( s + gap ), y, s, s * 1.4f };
        DrawRectangleRec( r, i < n ? on : off );
    }
}

void highlight( Rectangle r, float t )
{
    float a = 0.55f + 0.2f * std::sin( t * 6.0f );
    DrawRectangleGradientH( (int)r.x, (int)r.y, (int)r.width, (int)r.height, { 170, 30, 30, (unsigned char)( 130 * a ) }, { 170, 30, 30, 0 } );
    DrawRectangleRec( { r.x, r.y, std::max( 2.0f, 4 * k() ), r.height }, Accent );
}

}   // namespace ui
}   // namespace grave
