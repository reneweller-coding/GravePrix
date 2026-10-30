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

const Color Ink = { 238, 230, 214, 255 };
const Color Dim = { 150, 140, 130, 255 };
const Color Accent = { 214, 52, 44, 255 };
const Color Gold = { 222, 178, 92, 255 };
const Color Warn = { 255, 96, 72, 255 };
const Color Good = { 130, 224, 130, 255 };

namespace
{

Font  g_font = {};
bool  g_has = false;
float g_textSize = 1.0f;
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

void shutdown()
{
    if ( g_has ) UnloadFont( g_font );
    g_has = false;
}

void setTextSize( float s ) { g_textSize = s; }

float k() { return GetScreenHeight() / 1080.0f; }

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
