/**
 * @file RenderTool.cpp
 * @brief The tools that need a (hidden) window: --shot (a picture of a race) and --deckcheck (frame times at the
 *        Steam Deck's resolution). They refuse while another session holds the graphics card
 *        (G:/Tools/Games/GPU_BELEGT.txt).
 * @author Rene Weller
 */
#include "Render.h"
#include "Tools.h"

#include <algorithm>
#include <chrono>
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

/** @return whether another session holds the graphics card (then the tools wait for later) */
bool gpuBusy( const Options &opt )
{
    std::string flag = ( fs::path( opt.m_root ).parent_path() / "GPU_BELEGT.txt" ).string();
    if ( fs::exists( flag ) && !opt.has( "--force" ) )
    {
        printf( "GPU busy (%s): try again later\n", flag.c_str() );
        return true;
    }
    return false;
}

std::vector<CarSetup> testField( const std::string &only )
{
    std::vector<CarSetup>    field;
    std::vector<std::string> cars;
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
    return field;
}

}   // namespace

int toolDeckCheck( const Options &opt )
{
    if ( gpuBusy( opt ) ) return 2;
    // The Deck: 1280 x 800, the game's automatic quality there (0), and the next one up for comparison. Tracks with
    // night, rain, neon and the most props, the hardest pictures.
    std::vector<std::string> tracks = { "dwn_welle", "cem_overcoat", "ref_oelung", "fair_lastride", "est_finale" };
    if ( opt.has( "--deckcheck" ) && !opt.value( "--deckcheck" ).empty() ) tracks = { opt.value( "--deckcheck" ) };
    static const float scale[] = { 0.67f, 0.85f, 1.0f };   // App.cpp's QualityScale
    int                frames = (int)opt.num( "--frames", 600 );
    hiddenWindow( 1280, 800 );
    SetTargetFPS( 0 );
    printf( "track            quality  cpu ms (avg/p99)   frame ms (avg/p99)\n" );
    for ( const std::string &name : tracks )
    {
        const TrackDef *def = g_data.track( name );
        if ( !def ) continue;
        std::vector<std::string> errors, warnings;
        Track                    t;
        if ( !t.build( *def, g_data.venue( def->m_venue ), false, false, errors, warnings ) ) continue;
        for ( int q = 0; q <= 1; ++q )
        {
            Renderer r;
            r.init( (int)( 1280 * scale[q] ), (int)( 800 * scale[q] ), q );
            r.setTrack( t );
            r.m_miniature = opt.num( "--miniature", 1 ) != 0;
            World                 world;
            std::vector<CarSetup> field = testField( "" );
            world.init( t, field, t.m_arena ? ModeE::DERBY : ModeE::RACE, 2, 11 );
            std::vector<CarInput> none( field.size() );
            std::vector<double>   cpu, frame;
            using Clock = std::chrono::steady_clock;
            for ( int f = 0; f < frames + 60; ++f )
            {
                auto t0 = Clock::now();
                world.step( none );
                r.events( world );
                r.update( world, Dt );
                View v;
                v.m_focus = world.m_cars[world.m_leader].m_pos;
                r.render( world, v );
                auto t1 = Clock::now();
                BeginDrawing();
                DrawTexturePro( r.texture(), { 0, 0, (float)r.m_w, -(float)r.m_h }, { 0, 0, 1280, 800 }, { 0, 0 }, 0, WHITE );
                EndDrawing();
                auto t2 = Clock::now();
                if ( f < 60 ) continue;   // the first second: shaders and chunks being made
                cpu.push_back( std::chrono::duration<double, std::milli>( t1 - t0 ).count() );
                frame.push_back( std::chrono::duration<double, std::milli>( t2 - t0 ).count() );
            }
            auto stat = []( std::vector<double> v ) {
                std::sort( v.begin(), v.end() );
                double sum = 0;
                for ( double x : v ) sum += x;
                return std::pair<double, double>( sum / std::max<size_t>( 1, v.size() ), v.empty() ? 0.0 : v[v.size() * 99 / 100] );
            };
            auto c = stat( cpu ), fr = stat( frame );
            printf( "%-16s %7d  %6.2f / %6.2f    %6.2f / %6.2f\n", name.c_str(), q, c.first, c.second, fr.first, fr.second );
            r.shutdown();
        }
    }
    CloseWindow();
    return 0;
}

int toolShot( const Options &opt )
{
    // One GPU user at a time across all sessions: while another session's GPU job runs, no picture.
    if ( gpuBusy( opt ) ) return 2;
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
    r.m_miniature = opt.num( "--miniature", 1 ) != 0;
    if ( opt.has( "--sky" ) ) r.skyTest( (int)opt.num( "--sky", 0 ) );   // 0 the airship, 1 the helicopter, 2 lightning
    World                 world;
    std::vector<CarSetup> field = testField( opt.value( "--car" ) );
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
    if ( opt.has( "--tow" ) && !t.m_arena )
    {
        // Graves & Sons collecting a wreck: the car nearest to the view wrecked, the hook --tow seconds in (the
        // marshals wave beside it), a little of the pictures' time run on so their flags move.
        size_t near = 0;
        for ( size_t i = 0; i < world.m_cars.size(); ++i )
            if ( dist2( world.m_cars[i].m_pos, v.m_focus ) < dist2( world.m_cars[near].m_pos, v.m_focus ) ) near = i;
        Car &c = world.m_cars[near];
        c.m_wrecked = true;
        c.m_wreckTime = World::TowAfter + 1.0f;
        c.m_tow = opt.num( "--tow", 3.0f );
        c.m_towAt = c.m_pos;
        c.m_vel = { 0, 0 };
        v.m_focus = c.m_pos;
        for ( int k = 0; k < 20; ++k ) r.update( world, Dt );
    }
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

int toolShowroom( const Options &opt )
{
    if ( gpuBusy( opt ) ) return 2;
    std::string name = opt.value( "--showroom" ), file = opt.value2( "--showroom", "work/showroom.png" );
    if ( const CarDef *cd = g_data.car( name ) ) name = cd->m_model;
    if ( !g_data.model( name ) )
    {
        printf( "error: no model \"%s\"\n", name.c_str() );
        return 1;
    }
    int                w = (int)opt.num( "--w", 800 ), h = (int)opt.num( "--h", 600 );
    std::vector<float> yaws;
    std::string        list = opt.value( "--yaws", "240,60" );
    for ( size_t at = 0; at < list.size(); )
    {
        size_t comma = list.find( ',', at );
        yaws.push_back( (float)atof( list.substr( at, comma - at ).c_str() ) * Deg );
        at = comma == std::string::npos ? list.size() : comma + 1;
    }
    hiddenWindow( 320, 240 );
    Renderer r;
    r.init( w, h, 2 );
    Image all = GenImageColor( w * (int)yaws.size(), h, BLACK );
    for ( size_t k = 0; k < yaws.size(); ++k )
    {
        r.showroom( name, paintOf( (int)opt.num( "--paint", 0 ) ), yaws[k], opt.num( "--dist", 7.5f ) );
        Image img = LoadImageFromTexture( r.texture() );
        ImageFlipVertical( &img );
        ImageDraw( &all, img, { 0, 0, (float)w, (float)h }, { (float)( w * k ), 0, (float)w, (float)h }, WHITE );
        UnloadImage( img );
    }
    std::error_code ec;
    fs::create_directories( fs::path( file ).parent_path(), ec );
    ExportImage( all, file.c_str() );
    UnloadImage( all );
    r.shutdown();
    CloseWindow();
    printf( "%s -> %s\n", name.c_str(), file.c_str() );
    return 0;
}

}   // namespace grave
