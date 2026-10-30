/**
 * @file RenderTool.cpp
 * @brief The tool that needs a (hidden) window: --shot (a picture of a race). It refuses while another session
 *        holds the graphics card (G:/Tools/Games/GPU_BELEGT.txt).
 * @author Rene Weller
 */
#include "Render.h"
#include "Tools.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace grave
{

namespace fs = std::filesystem;

namespace
{

void hiddenWindow( int w, int h )
{
    SetConfigFlags( FLAG_WINDOW_HIDDEN | FLAG_WINDOW_UNFOCUSED );
    InitWindow( w, h, "Grave Prix tool" );
}

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

int toolShot( const Options &opt )
{
    // One GPU user at a time across all sessions: while another session's GPU job runs, no picture.
    std::string flag = ( fs::path( opt.m_root ).parent_path() / "GPU_BELEGT.txt" ).string();
    if ( fs::exists( flag ) && !opt.has( "--force" ) )
    {
        printf( "GPU busy (%s): no picture now, try again later\n", flag.c_str() );
        return 2;
    }
    std::string name = opt.value( "--shot" ), file = opt.value2( "--shot", "work/shot.png" );
    const TrackDef *def = g_data.track( name );
    if ( !def )
    {
        printf( "error: no track \"%s\"\n", name.c_str() );
        return 1;
    }
    int w = (int)opt.num( "--w", 1600 ), h = (int)opt.num( "--h", 900 );
    hiddenWindow( 320, 240 );
    std::vector<std::string> errors, warnings;
    Track                    t;
    if ( !t.build( *def, g_data.venue( def->m_venue ), opt.has( "--reverse" ), false, errors, warnings ) )
    {
        CloseWindow();
        return 1;
    }
    Renderer r;
    r.init( w, h, (int)opt.num( "--quality", 2 ) );
    r.setTrack( t );
    World                 world;
    std::vector<CarSetup> field;
    std::vector<std::string> cars;
    std::string only = opt.value( "--car" );
    for ( const std::string &c : g_data.m_carOrder )
        if ( only.empty() || c == only ) cars.push_back( c );
    for ( int k = 0; k < 6; ++k )
    {
        CarSetup s;
        s.m_car = cars.empty() ? "?" : cars[(size_t)k % cars.size()];
        s.m_driver = "AI";
        s.m_paint = paintOf( k );
        s.m_skill = 0.7f;
        s.m_weapons = 0.8f;
        s.m_aggression = 0.6f;
        s.m_rear[0] = 3;
        s.m_rockets = 2;
        field.push_back( s );
    }
    world.init( t, field, t.m_arena ? ModeE::DERBY : ModeE::RACE, 2, 11 );
    float seconds = opt.num( "--time", 6.0f );
    std::vector<CarInput> none( field.size() );
    for ( int k = 0; k < (int)( seconds * 60 ); ++k )
    {
        world.step( none );
        r.events( world );
        r.update( world, Dt );
    }
    View v;
    if ( opt.has( "--at" ) )
        v.m_focus = t.pointAt( opt.num( "--at", 0 ), 0 );
    else
        v.m_focus = world.m_cars[world.m_leader].m_pos;
    v.m_height = opt.num( "--height", 80.0f );
    r.render( world, v );
    Image img = LoadImageFromTexture( r.texture() );
    ImageFlipVertical( &img );
    std::error_code ec;
    fs::create_directories( fs::path( file ).parent_path(), ec );
    ExportImage( img, file.c_str() );
    UnloadImage( img );
    r.shutdown();
    CloseWindow();
    printf( "%s -> %s\n", name.c_str(), file.c_str() );
    return 0;
}

}   // namespace grave
