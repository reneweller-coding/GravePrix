/**
 * @file App.cpp
 * @brief The game's flow and its menus: the title, a new career, the stories, the quick race, the options and the
 *        controls; the race behind the menus.
 * @author Rene Weller
 */
#include "App.h"

#include "Crash.h"
#include "Files.h"
#include "IconPng.h"
#include "Lang.h"
#include "Rumble.h"
#include "Ui.h"

#include <raylib.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>

#include <rlgl.h>

namespace grave
{

namespace fs = std::filesystem;

namespace
{

bool steamDeck()
{
    const char *d = getenv( "SteamDeck" );
    return d && atoi( d ) == 1;
}

const float QualityScale[] = { 0.67f, 0.85f, 1.0f };

/** @brief The paint schemes to choose from. */
const Paint &scheme( int i )
{
    static Paint p[10];
    static bool  made = false;
    if ( !made )
    {
        const Color c[10][3] = {
            { { 150, 20, 24, 255 }, { 20, 20, 22, 255 }, { 220, 200, 160, 255 } },   // blood and coal
            { { 20, 20, 24, 255 }, { 170, 140, 60, 255 }, { 200, 200, 200, 255 } },  // undertaker black and brass
            { { 230, 226, 214, 255 }, { 30, 30, 34, 255 }, { 170, 20, 20, 255 } },   // bone white
            { { 50, 70, 40, 255 }, { 120, 100, 60, 255 }, { 30, 30, 30, 255 } },     // grave moss
            { { 90, 30, 110, 255 }, { 20, 20, 24, 255 }, { 220, 200, 120, 255 } },   // funeral purple
            { { 230, 120, 20, 255 }, { 30, 30, 30, 255 }, { 240, 240, 230, 255 } },  // pumpkin
            { { 30, 60, 120, 255 }, { 200, 200, 210, 255 }, { 220, 40, 30, 255 } },  // morgue blue
            { { 200, 180, 40, 255 }, { 20, 20, 20, 255 }, { 200, 30, 30, 255 } },    // hazard yellow
            { { 110, 110, 116, 255 }, { 60, 60, 66, 255 }, { 220, 90, 30, 255 } },   // primer grey
            { { 220, 90, 140, 255 }, { 250, 250, 250, 255 }, { 40, 40, 40, 255 } },  // wreath pink
        };
        for ( int k = 0; k < 10; ++k )
            for ( int j = 0; j < 3; ++j ) p[k].m_c[j] = c[k][j];
        made = true;
    }
    return p[( ( i % 10 ) + 10 ) % 10];
}

const char *SchemeNames[10] = { "Blut und Kohle", "Bestatterschwarz", "Knochenweiß", "Grabmoos", "Trauerviolett",
                                "Kürbis", "Leichenhallenblau", "Warngelb", "Grundierung", "Kranzrosa" };

const char *PresetNames[] = { "Fahrer", "Kurt Kurve", "Rosa Rost", "Max Mortal", "Vera Vollgas", "Hank Hinüber",
                              "Lilly Leichtsinn", "Otto Ohnebremse" };

}   // namespace

std::string App::careerFile( int slot ) { return userDir() + ( slot <= 0 ? std::string( "career.txt" ) : "career" + std::to_string( slot + 1 ) + ".txt" ); }

// ---------------------------------------------------------------------------------------------------- setup

namespace
{

/** @return the frame as it is (raylib's own capture scales by the desktop's DPI and reads past a smaller framebuffer) */
Image captureScreen()
{
    rlDrawRenderBatchActive();
    Image img = {};
    img.width = GetRenderWidth();
    img.height = GetRenderHeight();
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    img.data = rlReadScreenPixels( img.width, img.height );
    return img;
}

}   // namespace


int App::quality() const
{
    if ( m_set.m_quality >= 0 ) return std::clamp( m_set.m_quality, 0, 2 );
    return m_deck ? 0 : 2;
}

bool App::init( const Options &o )
{
    m_opt = o;
    m_set.load( userDir() + "settings.txt" );
    m_rec.load( userDir() + "records.txt" );
    for ( const std::string &b : m_set.m_binds ) m_in.load( b );
    setLanguage( m_set.m_language.empty() ? systemLanguage() : m_set.m_language );
    loadUiTexts( o.m_data );
    m_deck = steamDeck();
    int w = m_set.m_width > 0 ? m_set.m_width : m_deck ? 1280 : 1600;
    int h = m_set.m_height > 0 ? m_set.m_height : m_deck ? 800 : 900;
    if ( o.has( "--window" ) ) sscanf( o.value( "--window" ).c_str(), "%dx%d", &w, &h );
    m_test = o.has( "--apptest" );
    if ( m_test )
    {
        FILE *f = fopen( o.value( "--apptest" ).c_str(), "r" );
        char  word[128];
        while ( f && fscanf( f, "%127s", word ) == 1 )
        {
            if ( word[0] == '#' )
            {
                int c;
                while ( ( c = fgetc( f ) ) != EOF && c != '\n' ) {}
                continue;
            }
            m_testWords.push_back( word );
        }
        if ( f ) fclose( f );
        // A test starts without a career.
        std::error_code ec;
        for ( int s = 0; s < 3; ++s ) fs::remove( careerFile( s ), ec );
        if ( m_testWords.empty() )
        {
            printf( "apptest: no script %s\n", o.value( "--apptest" ).c_str() );
            return false;
        }
    }
    unsigned flags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT;
    if ( m_test ) flags = FLAG_WINDOW_HIDDEN | FLAG_WINDOW_UNFOCUSED;   // a test runs silent, unseen, never in front
    if ( m_set.m_vsync ) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags( flags );
    InitWindow( w, h, "Grave Prix" );
    if ( !IsWindowReady() ) return false;
    SetExitKey( 0 );
    Image icon = LoadImageFromMemory( ".png", IconPng, (int)sizeof( IconPng ) );
    if ( icon.data )
    {
        SetWindowIcon( icon );
        UnloadImage( icon );
    }
    SetWindowMinSize( 640, 400 );
    bool full = !m_test && ( o.has( "--fullscreen" ) ||
                             ( !o.has( "--window" ) && ( m_set.m_fullscreen || ( m_deck && m_set.m_width == 0 ) ) ) );
    if ( full ) ToggleBorderlessWindowed();
    SetTargetFPS( m_test ? 0 : m_set.m_fps );
    bool mute = o.has( "--mute" ) || m_test;
    if ( !mute ) InitAudioDevice();
    m_audio.init( !mute, o.m_root + "/assets" );
    ui::init( o.m_root + "/assets" );
    applySettings();
    m_show.init( 640, 480, 2 );
    m_podium.init( 1280, 256, 1 );
    crash::install( userDir() + "crashes/", "" );
    m_newName = PresetNames[0];
    m_qDiff = 2;
    m_audio.music( "title" );
    return true;
}

void App::applySettings()
{
    m_audio.m_musicVolume = m_set.m_music;
    m_audio.m_effectVolume = m_set.m_effects;
    ui::setTextSize( m_set.m_textSize * ( m_deck ? 1.12f : 1.0f ) );
    ui::setPalette( m_set.m_palette );
    if ( !m_test ) SetTargetFPS( m_set.m_fps );
    if ( m_racing && m_player2 >= 0 )
    {
        splitViews( true );
        m_view.setTrack( m_track );
        m_view2.setTrack( m_track );
        return;
    }
    int   q = quality();
    float s = QualityScale[q];
    m_view.shutdown();
    m_view.init( std::max( 320, (int)( GetScreenWidth() * s ) ), std::max( 200, (int)( GetScreenHeight() * s ) ), q );
    m_view.m_miniature = miniatureOn();
    if ( m_racing ) m_view.setTrack( m_track );
    else if ( m_atReady ) m_view.setTrack( m_atTrack );
}

void App::splitViews( bool on )
{
    // Two players: each renderer half the screen wide (the same scale as one would be); one: the second goes.
    if ( !on )
    {
        m_view2.shutdown();
        applySettings();
        return;
    }
    int   q = quality();
    float s = QualityScale[q];
    int   w = std::max( 320, (int)( GetScreenWidth() * 0.5f * s ) ), h = std::max( 200, (int)( GetScreenHeight() * s ) );
    for ( Renderer *r : { &m_view, &m_view2 } )
    {
        r->shutdown();
        r->init( w, h, q );
        r->m_miniature = miniatureOn();
    }
}

void App::saveSettings()
{
    m_set.m_binds = m_in.save();
    m_set.save( userDir() + "settings.txt" );
}

void App::shutdown()
{
    saveSettings();
    m_rec.save( userDir() + "records.txt" );
    if ( m_inCareer ) saveCareer();
    rumble::stop();
    m_view.shutdown();
    m_view2.shutdown();
    m_show.shutdown();
    if ( m_racePhoto.id ) UnloadTexture( m_racePhoto );
    m_podium.shutdown();
    for ( auto &p : m_photos )
        if ( p.second.id ) UnloadTexture( p.second );
    m_photos.clear();
    m_audio.shutdown();
    ui::shutdown();
    if ( IsAudioDeviceReady() ) CloseAudioDevice();
    CloseWindow();
}

void App::run()
{
    int lastW = GetScreenWidth(), lastH = GetScreenHeight();
    while ( !m_quit && !WindowShouldClose() )
    {
        float dt = m_test ? 1.0f / 30.0f : std::min( GetFrameTime(), 0.1f );
        if ( m_test ) testStep();
        m_time += dt;
        if ( GetScreenWidth() != lastW || GetScreenHeight() != lastH )
        {
            lastW = GetScreenWidth();
            lastH = GetScreenHeight();
            applySettings();
        }
        if ( IsKeyPressed( KEY_F11 ) || ( IsKeyDown( KEY_LEFT_ALT ) && IsKeyPressed( KEY_ENTER ) ) )
        {
            ToggleBorderlessWindowed();
            m_set.m_fullscreen = !m_set.m_fullscreen;
        }
        if ( IsKeyPressed( KEY_F12 ) )
        {
            std::string shot = photoDir() + "graveprix_" + std::to_string( (long long)std::time( nullptr ) ) + ".png";
            Image img = captureScreen();
            ExportImage( img, shot.c_str() );
            UnloadImage( img );
        }
        m_in.update();
        m_audio.update( dt );
        frame( dt );
    }
}

void App::testStep()
{
    static const char *names[] = { "title", "newgame", "hub", "offers", "garage", "market", "shark", "sponsor", "news", "table",
                                   "story", "race", "results", "quick", "options", "controls", "graveyard", "crypt", "slots",
                                   "credits", "school", "bench", "replay" };
    // A hard limit: a script that hangs ends by itself.
    if ( ++m_testFrames > 30 * 60 * 8 )
    {
        printf( "apptest: timed out on screen %s\n", names[(int)m_screen] );
        ++m_testFails;
        m_quit = true;
        return;
    }
    if ( m_testRace )
    {
        // A computer driver finishes the race for the player, many steps a frame.
        if ( m_screen != ScreenE::RACE )
        {
            m_testRace = false;
            return;
        }
        m_world.m_cars[m_player].m_setup.m_human = false;
        if ( m_player2 >= 0 ) m_world.m_cars[m_player2].m_setup.m_human = false;
        m_acc += Dt * 40;
        return;
    }
    if ( m_testWait > 0 )
    {
        --m_testWait;
        return;
    }
    if ( m_testAt >= m_testWords.size() )
    {
        printf( "apptest: done, %d failures\n", m_testFails );
        m_quit = true;
        return;
    }
    std::string w = m_testWords[m_testAt++];
    if ( w == "expect" && m_testAt < m_testWords.size() )
    {
        std::string want = m_testWords[m_testAt++];
        if ( want != names[(int)m_screen] )
        {
            printf( "apptest: word %zu expected screen %s, is %s\n", m_testAt, want.c_str(), names[(int)m_screen] );
            ++m_testFails;
        }
        return;
    }
    if ( w == "wait" && m_testAt < m_testWords.size() )
    {
        m_testWait = atoi( m_testWords[m_testAt++].c_str() );
        return;
    }
    if ( w == "race" )
    {
        m_testRace = true;
        return;
    }
    if ( w == "money" && m_testAt < m_testWords.size() )
    {
        m_career.m_money += atoi( m_testWords[m_testAt++].c_str() );
        return;
    }
    if ( w == "shot" && m_testAt < m_testWords.size() )
    {
        m_testShot = m_testWords[m_testAt++];
        return;
    }
    if ( w == "pad" )
    {
        m_in.forcePad( true );
        return;
    }
    if ( w == "split" )
    {
        // Two players in the next quick race without a pad (the second driven by the computer with "drive").
        m_testSplit = true;
        m_qPlayers = 2;
        return;
    }
    if ( w == "replayon" )
    {
        // The replay after the race (the tests skip it otherwise).
        m_testReplay = true;
        return;
    }
    if ( w == "school" && m_testAt < m_testWords.size() )
    {
        // school N: the driving school's exercise N (0..2), a computer driver at the wheel ("race").
        startSchool( atoi( m_testWords[m_testAt++].c_str() ) );
        return;
    }
    if ( w == "drive" )
    {
        // A computer driver takes the player's wheel at the race's own pace (pictures in the middle of a race).
        if ( m_racing ) m_world.m_cars[m_player].m_setup.m_human = false;
        if ( m_racing && m_player2 >= 0 ) m_world.m_cars[m_player2].m_setup.m_human = false;
        return;
    }
    if ( w == "reset" )
    {
        // The player's car back on the track (the reset key's World::resetCar); a failure when the race refuses.
        if ( !m_racing || !m_world.resetCar( m_player ) )
        {
            printf( "apptest: word %zu: the car was not put back on the track\n", m_testAt );
            ++m_testFails;
        }
        else
            m_camCar = m_world.m_cars[m_player].m_pos;
        return;
    }
    if ( w == "graves" )
    {
        // Three rivals of the league die, so the graveyard has stones to show.
        int n = 0;
        for ( RivalState &s : m_career.m_rivals )
        {
            const RivalDef *d = g_data.rival( s.m_key );
            if ( n >= 3 || !s.m_active || !s.m_alive || !d || d->m_champion || s.m_key == "_player" ) continue;
            s.m_alive = false;
            s.m_died = m_career.m_day;
            s.m_cause = n;
            s.m_killer = n == 0 ? "_player" : "";
            s.m_where = m_career.m_offers.empty() ? "" : m_career.m_offers[0].m_track;
            s.m_words = m_career.lastWords( s.m_key );
            ++n;
        }
        return;
    }
    if ( w == "tutorial" )
    {
        startTutorial();
        return;
    }
    if ( w == "sign" && m_testAt < m_testWords.size() )
    {
        m_career.sign( m_testWords[m_testAt++] );
        return;
    }
    if ( w == "careerrace" && m_testAt + 1 < m_testWords.size() )
    {
        // careerrace TRACK JOB: today's main race moved to that track, with a job of Graves' (none, coffin, debtor,
        // escort, wreaths).
        Offer o = m_career.m_offers.empty() ? Offer() : m_career.m_offers[std::min<size_t>( 1, m_career.m_offers.size() - 1 )];
        o.m_track = m_testWords[m_testAt++];
        std::string job = m_testWords[m_testAt++];
        o.m_job = job == "coffin" ? JobE::COFFIN : job == "debtor" ? JobE::DEBTOR : job == "escort" ? JobE::ESCORT
                : job == "wreaths" ? JobE::WREATHS : JobE::NONE;
        o.m_derby = o.m_reverse = false;
        o.m_fee = 0;
        o.m_pay = 1000;
        if ( const TrackDef *td = g_data.track( o.m_track ) ) o.m_laps = td->m_laps;
        if ( o.m_job == JobE::DEBTOR && !o.m_rivals.empty() ) o.m_target = o.m_rivals[0];
        if ( o.m_job == JobE::ESCORT && o.m_rivals.size() > 4 ) o.m_rivals.pop_back();
        m_offer = o;
        int  player = 0;
        auto field = m_career.field( o, player );
        startRace( o.m_track, false, field, ModeE::RACE, o.m_laps, player );
        return;
    }
    m_in.script( { w } );
    m_testWait = 3;
}

void App::go( ScreenE s )
{
    m_screen = s;
    m_greeting.clear();
    m_tab = 0;
}

void App::sound( SfxE s ) { m_audio.play( s, 0, 0.7f ); }

void App::frame( float dt )
{
    // Behind the menus a race of computer drivers (not while a race of ours waits behind a story or the options).
    if ( m_screen != ScreenE::RACE && m_screen != ScreenE::BENCH && m_screen != ScreenE::REPLAY && !m_racing ) attract( dt );
    BeginDrawing();
    ClearBackground( BLACK );
    switch ( m_screen )
    {
    case ScreenE::TITLE: title(); break;
    case ScreenE::NEWGAME: newGame(); break;
    case ScreenE::HUB: hub(); break;
    case ScreenE::OFFERS: offers(); break;
    case ScreenE::GARAGE: garage(); break;
    case ScreenE::MARKET: market(); break;
    case ScreenE::SHARK: shark(); break;
    case ScreenE::SPONSOR: sponsor(); break;
    case ScreenE::NEWS: newsScreen(); break;
    case ScreenE::TABLE: tableScreen(); break;
    case ScreenE::GRAVEYARD: graveyard(); break;
    case ScreenE::STORY: story(); break;
    case ScreenE::RACE: race( dt ); break;
    case ScreenE::RESULTS: results(); break;
    case ScreenE::QUICK: quick(); break;
    case ScreenE::OPTIONS: options(); break;
    case ScreenE::CONTROLS: controls(); break;
    case ScreenE::CRYPT: crypt(); break;
    case ScreenE::SLOTS: slots(); break;
    case ScreenE::CREDITS: credits(); break;
    case ScreenE::SCHOOL: school(); break;
    case ScreenE::BENCH: bench(); break;
    case ScreenE::REPLAY: replay( dt ); break;
    default: break;
    }
    toasts();
    if ( !m_testShot.empty() )
    {
        // The app test's picture of this frame (before the swap the frame is still there to read).
        std::error_code ec;
        std::filesystem::create_directories( m_opt.m_root + "/work/screens", ec );
        Image img = captureScreen();
        ExportImage( img, ( m_opt.m_root + "/work/screens/" + m_testShot + ".png" ).c_str() );
        UnloadImage( img );
        m_testShot.clear();
    }
    EndDrawing();
}

// ---------------------------------------------------------------------------------------------------- the race behind the menus

void App::attract( float dt )
{
    if ( !m_atReady || m_atWorld.m_phase == World::PhaseE::DONE || m_atTime > 150.0f )
    {
        std::vector<std::string> pool;
        for ( const std::string &t : g_data.m_trackOrder )
            if ( !g_data.m_tracks.at( t ).m_arena && t.rfind( "test_", 0 ) != 0 ) pool.push_back( t );
        if ( pool.empty() ) pool = g_data.m_trackOrder;
        if ( pool.empty() ) return;
        Rng r;
        r.seed( (uint32_t)( m_time * 1000 ) + 7 );
        const TrackDef          &def = g_data.m_tracks.at( pool[r.below( (int)pool.size() )] );
        std::vector<std::string> e, w;
        if ( !m_atTrack.build( def, g_data.venue( def.m_venue ), false, false, e, w ) ) return;
        std::vector<CarSetup> field;
        std::vector<std::string> rivals;
        for ( const std::string &k : g_data.m_rivalOrder )
            if ( g_data.car( g_data.m_rivals.at( k ).m_car ) ) rivals.push_back( k );
        for ( int k = 0; k < 6; ++k )
        {
            CarSetup s;
            if ( !rivals.empty() )
            {
                const RivalDef &d = g_data.m_rivals.at( rivals[r.below( (int)rivals.size() )] );
                s.m_car = d.m_car;
                s.m_driver = d.m_name;
                for ( int j = 0; j < 3; ++j ) s.m_paint.m_c[j] = d.m_colors[j];
                s.m_skill = d.m_skill;
                s.m_aggression = d.m_aggression;
                s.m_weapons = d.m_weapons;
            }
            else
            {
                s.m_car = g_data.m_carOrder.empty() ? "?" : g_data.m_carOrder[r.below( (int)g_data.m_carOrder.size() )];
                s.m_paint = scheme( k );
            }
            s.m_rear[(int)RearE::MINE] = 3;
            s.m_rockets = 2;
            s.m_nitro = 2;
            s.m_engine = s.m_tires = s.m_armor = s.m_gunLv = 3;
            field.push_back( s );
        }
        m_atWorld.init( m_atTrack, field, ModeE::RACE, 2, r.next() );
        m_atWorld.m_countdown = 0.6f;
        if ( !m_racing ) m_view.setTrack( m_atTrack );
        m_atReady = true;
        m_atTime = 0;
        m_atCam.m_focus = m_atWorld.m_cars[0].m_pos;
    }
    m_atTime += dt;
    static float acc = 0;
    acc += dt;
    std::vector<CarInput> none( m_atWorld.m_cars.size() );
    int steps = 0;
    while ( acc >= Dt && steps++ < 4 )
    {
        acc -= Dt;
        m_atWorld.step( none );
        m_view.events( m_atWorld );
    }
    if ( acc > Dt ) acc = 0;
    m_view.update( m_atWorld, dt );
    const Car &lead = m_atWorld.m_cars[m_atWorld.m_leader];
    Vector2    goal = add( lead.m_pos, mul( lead.m_vel, 0.5f ) );
    m_atCam.m_focus = add( m_atCam.m_focus, mul( sub( goal, m_atCam.m_focus ), std::min( 1.0f, dt * 2.0f ) ) );
    m_atCam.m_height = 62.0f;
    m_view.render( m_atWorld, m_atCam );
}

void App::backdrop()
{
    const Texture2D &t = m_view.texture();
    DrawTexturePro( t, { 0, 0, (float)t.width, (float)-t.height }, { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() },
                    { 0, 0 }, 0, { 140, 130, 130, 255 } );
    DrawRectangleGradientH( 0, 0, GetScreenWidth(), GetScreenHeight(), { 0, 0, 0, 200 }, { 0, 0, 0, 40 } );
}

// ---------------------------------------------------------------------------------------------------- helpers

void App::header( const std::string &title, const std::string &sub )
{
    float k = ui::k();
    ui::text( title, 70 * k, 46 * k, 64, ui::Ink );
    if ( !sub.empty() ) ui::text( sub, 74 * k, 118 * k, 28, ui::Gold );
    DrawRectangle( (int)( 70 * k ), (int)( 158 * k ), (int)( 420 * k ), std::max( 2, (int)( 3 * k ) ), ui::Accent );
}

void App::hints( const std::string &text )
{
    float k = ui::k();
    DrawRectangle( 0, GetScreenHeight() - (int)( 54 * k ), GetScreenWidth(), (int)( 54 * k ), { 0, 0, 0, 170 } );
    float y = GetScreenHeight() - 44 * k;
    if ( !m_in.padUsed() )
    {
        ui::text( text, 70 * k, y, 26, ui::Dim );
        return;
    }
    // With a pad: each part's leading buttons ("A", "LB/RB", "A / B") drawn as the pad's glyphs.
    const std::string dot = "   \xC2\xB7   ";
    std::string       s = T( text );
    float             x = 70 * k;
    size_t            at = 0;
    while ( at <= s.size() )
    {
        size_t      end = s.find( dot, at );
        std::string part = s.substr( at, end == std::string::npos ? std::string::npos : end - at );
        size_t      sp;
        while ( ( sp = part.find( ' ' ) ) != std::string::npos )
        {
            std::string head = part.substr( 0, sp ), rest;
            std::vector<std::string> buttons;
            for ( size_t a = 0, b; a <= head.size(); a = b + 1 )
            {
                b = head.find( '/', a );
                if ( b == std::string::npos ) b = head.size();
                buttons.push_back( head.substr( a, b - a ) );
            }
            bool all = true;
            for ( const std::string &b : buttons ) all = all && ( b == "A" || b == "B" || b == "X" || b == "Y" || b == "LB" || b == "RB" || b == "LT" || b == "RT" || b == "Start" || b == "Select" );
            if ( !all ) break;
            for ( const std::string &b : buttons ) x += ui::glyph( b, x, y - 2 * k, 26 ) + 6 * k;
            part = part.substr( sp + 1 );
            // "A / B": the slash between two buttons
            if ( part.rfind( "/ ", 0 ) == 0 ) part = part.substr( 2 );
            else break;
        }
        x += ui::text( part, x + 4 * k, y, 26, ui::Dim ) + 4 * k;
        if ( end == std::string::npos ) break;
        x += ui::text( dot, x, y, 26, ui::Dim );
        at = end + dot.size();
    }
}

int App::menu( const std::vector<std::string> &items, int &sel, Rectangle area, float size, const std::vector<bool> *enabled )
{
    int n = (int)items.size();
    if ( n == 0 ) return -1;
    sel = std::clamp( sel, 0, n - 1 );
    if ( m_in.up() )
    {
        sel = ( sel + n - 1 ) % n;
        sound( SfxE::UI_MOVE );
    }
    if ( m_in.down() )
    {
        sel = ( sel + 1 ) % n;
        sound( SfxE::UI_MOVE );
    }
    float k = ui::k(), row = size * 1.45f * k;
    int   first = std::max( 0, sel - (int)( area.height / row ) + 1 );
    for ( int i = first; i < n; ++i )
    {
        float y = area.y + ( i - first ) * row;
        if ( y + row > area.y + area.height + 1 ) break;
        bool on = !enabled || ( *enabled )[i];
        if ( i == sel ) ui::highlight( { area.x - 14 * k, y - 4 * k, area.width, row - 4 * k }, m_time );
        ui::text( items[i], area.x, y, size, !on ? Color{ 90, 84, 80, 255 } : i == sel ? ui::Ink : ui::Dim );
    }
    // The mouse.
    Vector2 mp = GetMousePosition();
    if ( CheckCollisionPointRec( mp, area ) )
    {
        int hover = first + (int)( ( mp.y - area.y ) / row );
        if ( hover >= 0 && hover < n && ( GetMouseDelta().x != 0 || GetMouseDelta().y != 0 ) ) sel = hover;
        if ( IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) && hover == sel )
        {
            if ( enabled && !( *enabled )[sel] )
            {
                sound( SfxE::UI_DENY );
                return -1;
            }
            sound( SfxE::UI_OK );
            return sel;
        }
    }
    if ( m_in.accept() )
    {
        if ( enabled && !( *enabled )[sel] )
        {
            sound( SfxE::UI_DENY );
            return -1;
        }
        sound( SfxE::UI_OK );
        return sel;
    }
    return -1;
}

void App::moneyBox()
{
    if ( !m_inCareer ) return;
    float k = ui::k();
    float x = GetScreenWidth() - 70 * k;
    ui::text( formatMoney( m_career.m_money ), x, 50 * k, 48, m_career.m_money < 0 ? ui::Warn : ui::Gold, ui::AlignE::RIGHT );
    const LeagueDef *l = g_data.league( m_career.m_league );
    std::string      sub = tf( "Tag %d", m_career.m_day ) + "  ·  " + ( l ? l->m_title.get() : "" );
    ui::text( sub, x, 108 * k, 26, ui::Dim, ui::AlignE::RIGHT );
    if ( m_career.m_loan > 0 )
        ui::text( tf( "Schulden: %s", formatMoney( m_career.m_loan ).c_str() ), x, 140 * k, 24, ui::Warn, ui::AlignE::RIGHT );
}

// ---------------------------------------------------------------------------------------------------- title

void App::title()
{
    backdrop();
    float k = ui::k();
    float pulse = 0.85f + 0.15f * std::sin( m_time * 2.0f );
    ui::text( "GRAVE PRIX", 90 * k, 120 * k, 150, { (unsigned char)( 214 * pulse ), 40, 36, 255 } );
    ui::text( T( "Das Rennen, bei dem der Bestatter mitverdient." ), 96 * k, 290 * k, 34, ui::Ink );
    int saves = 0, only = 0;
    for ( int s = 0; s < 3; ++s )
        if ( fs::exists( careerFile( s ) ) )
        {
            ++saves;
            only = s;
        }
    bool                     canLoad = saves > 0;
    std::vector<std::string> items = { T( "Karriere fortsetzen" ), T( "Neue Karriere" ), T( "Schnelles Rennen" ), T( "Optionen" ),
                                       T( "Probefahrt beim Bestatter" ), T( "Die Gruft der Grabinschriften" ), T( "Abspann" ),
                                       T( "Beenden" ) };
    std::vector<bool>        on = { canLoad, true, true, true, true, true, true, true };
    if ( !canLoad && m_sel[(int)ScreenE::TITLE] == 0 ) m_sel[(int)ScreenE::TITLE] = 1;
    int pick = menu( items, m_sel[(int)ScreenE::TITLE], { 96 * k, 420 * k, 640 * k, 420 * k }, 48, &on );
    hints( m_in.padUsed() ? T( "A wählen   ·   Steuerkreuz bewegen" ) : T( "Eingabe wählen   ·   Pfeile bewegen   ·   F11 Vollbild" ) );
    ui::text( "v" GRAVE_VERSION, GetScreenWidth() - 30 * k, GetScreenHeight() - 44 * k, 22, ui::Dim, ui::AlignE::RIGHT );
    switch ( pick )
    {
    case 0:
        // One career: on with it; more: choose.
        if ( saves > 1 )
        {
            m_slotsNew = false;
            readSlots();
            go( ScreenE::SLOTS );
        }
        else if ( m_career.load( careerFile( only ) ) )
        {
            m_slot = only;
            m_inCareer = true;
            m_audio.music( "garage" );
            go( m_career.m_over ? ScreenE::TABLE : ScreenE::HUB );
        }
        break;
    case 1:
        // The first career goes to the first slot; with one there already, choose where.
        if ( saves == 0 )
        {
            m_slot = 0;
            go( ScreenE::NEWGAME );
        }
        else
        {
            m_slotsNew = true;
            readSlots();
            go( ScreenE::SLOTS );
        }
        break;
    case 2: go( ScreenE::QUICK ); break;
    case 3: go( ScreenE::OPTIONS ); break;
    case 4:
        m_inCareer = false;
        startTutorial();
        break;
    case 5: go( ScreenE::CRYPT ); break;
    case 6:
        m_creditsAfter = ScreenE::TITLE;
        m_creditLines.clear();
        go( ScreenE::CREDITS );
        break;
    case 7: m_quit = true; break;
    default: break;
    }
}

void App::readSlots()
{
    for ( int s = 0; s < 3; ++s )
    {
        SlotInfo &i = m_slotInfo[s];
        i = SlotInfo();
        Career c;
        if ( !fs::exists( careerFile( s ) ) || !c.load( careerFile( s ) ) ) continue;
        const LeagueDef *l = g_data.league( c.m_league );
        const CarDef    *cd = g_data.car( c.m_car );
        i.m_used = true;
        i.m_name = c.m_name;
        i.m_lines = { g_data.difficulty( c.m_difficulty ).m_title.get(), l ? l->m_title.get() : std::string( T( "Finale" ) ),
                      tf( T( "Renntag %d" ), c.m_day ), formatMoney( c.m_money ), cd ? cd->m_title.get() : c.m_car };
        if ( c.m_won ) i.m_lines.push_back( T( "Graves besiegt" ) );
        else if ( c.m_over ) i.m_lines.push_back( T( "Pleite" ) );
    }
}

void App::slots()
{
    backdrop();
    float k = ui::k();
    header( m_slotsNew ? T( "Neue Karriere" ) : T( "Karriere fortsetzen" ),
            m_slotsNew ? T( "Welcher Platz? Ein belegter wird überschrieben." ) : T( "Welche Karriere?" ) );
    int &sel = m_sel[(int)ScreenE::SLOTS];
    sel = std::clamp( sel, 0, 2 );
    int before = sel;
    if ( m_in.left() ) sel = ( sel + 2 ) % 3;
    if ( m_in.right() ) sel = ( sel + 1 ) % 3;
    if ( sel != before )
    {
        m_slotConfirm = -1;
        sound( SfxE::UI_MOVE );
    }
    float cw = ( GetScreenWidth() - 200 * k - 60 * k ) / 3;
    for ( int s = 0; s < 3; ++s )
    {
        const SlotInfo &i = m_slotInfo[s];
        Rectangle       r = { 100 * k + s * ( cw + 30 * k ), 220 * k, cw, 560 * k };
        ui::panel( r, 0.85f, s == sel ? ui::Accent : Color{ 160, 124, 70, 255 } );
        if ( s == sel ) ui::highlight( { r.x, r.y, r.width, 70 * k }, m_time );
        float y = r.y + 16 * k;
        ui::text( tf( T( "Spielstand %d" ), s + 1 ), r.x + 24 * k, y, 40, s == sel ? ui::Ink : ui::Gold );
        y += 80 * k;
        if ( !i.m_used )
        {
            ui::text( T( "frei" ), r.x + 24 * k, y, 34, ui::Dim );
            continue;
        }
        ui::text( i.m_name, r.x + 24 * k, y, 38, ui::Ink );
        y += 56 * k;
        for ( const std::string &l : i.m_lines )
        {
            ui::text( l, r.x + 24 * k, y, 28, ui::Dim );
            y += 38 * k;
        }
        if ( s == m_slotConfirm )
            ui::text( T( "Nochmal wählen: überschreiben" ), r.x + 24 * k, r.y + r.height - 60 * k, 28, ui::Warn );
    }
    hints( m_in.padUsed() ? T( "Links/Rechts wählen   ·   A nehmen   ·   B zurück" ) : T( "Links/Rechts wählen   ·   Eingabe nehmen   ·   Esc zurück" ) );
    if ( m_in.back() )
    {
        m_slotConfirm = -1;
        go( ScreenE::TITLE );
        return;
    }
    if ( !m_in.accept() ) return;
    bool used = m_slotInfo[sel].m_used;
    if ( !m_slotsNew )
    {
        if ( !used || !m_career.load( careerFile( sel ) ) )
        {
            sound( SfxE::UI_DENY );
            return;
        }
        m_slot = sel;
        m_inCareer = true;
        m_audio.music( "garage" );
        go( m_career.m_over ? ScreenE::TABLE : ScreenE::HUB );
        return;
    }
    if ( used && m_slotConfirm != sel )
    {
        m_slotConfirm = sel;
        sound( SfxE::UI_DENY );
        return;
    }
    m_slotConfirm = -1;
    m_slot = sel;
    sound( SfxE::UI_OK );
    go( ScreenE::NEWGAME );
}

void App::credits()
{
    // The roll: who made it, who raced in it (the dead of this career marked), what it was made with.
    if ( m_creditLines.empty() )
    {
        m_creditsTime = 0;
        auto add = [&]( int kind, const std::string &s ) { m_creditLines.push_back( { kind, s } ); };
        add( 0, "GRAVE PRIX" );
        add( 3, T( "Das Rennen, bei dem der Bestatter mitverdient." ) );
        add( 4, "" );
        add( 1, T( "Ein Spiel von" ) );
        add( 2, "Rene Weller" );
        add( 4, "" );
        for ( const char *key : { "graves", "announcer", "mechanic", "dealer", "shark", "editor" } )
            if ( const CharacterDef *c = g_data.character( key ) )
            {
                add( 1, c->m_role.get() );
                add( 2, c->m_name );
            }
        add( 4, "" );
        add( 1, T( "Das Fahrerfeld" ) );
        std::vector<std::string> names;
        for ( const std::string &key : g_data.m_rivalOrder )
        {
            const RivalDef   *d = g_data.rival( key );
            const RivalState *s = m_inCareer ? m_career.rival( key ) : nullptr;
            if ( d && key != "mortimer_graves" ) names.push_back( d->m_name + ( s && !s->m_alive && s->m_died > 0 ? "  \xE2\x80\xA0" : "" ) );
        }
        std::sort( names.begin(), names.end() );
        for ( const std::string &n : names ) add( 2, n );
        if ( m_inCareer )
        {
            bool any = false;
            for ( const RivalState &s : m_career.m_rivals )
                if ( !s.m_alive && s.m_died > 0 && g_data.rival( s.m_key ) )
                {
                    if ( !any )
                    {
                        add( 4, "" );
                        add( 1, T( "In memoriam" ) );
                    }
                    any = true;
                    add( 2, "\xE2\x80\xA0 " + g_data.rival( s.m_key )->m_name + tf( T( ", Renntag %d" ), s.m_died ) );
                    if ( !s.m_words.empty() ) add( 3, quote( s.m_words ) );
                }
        }
        add( 4, "" );
        add( 1, T( "Musik" ) );
        add( 2, T( "Erzeugt mit ACE-Step 1.5, auf dem eigenen Rechner" ) );
        add( 2, T( "Trauermarsch: Frédéric Chopin, 1839, gemeinfrei" ) );
        add( 1, T( "Stimme" ) );
        add( 2, T( "Ricky Rattle: Qwen3-TTS, eine erfundene Stimme" ) );
        add( 1, T( "Pressefotos" ) );
        add( 2, T( "FLUX.2 [dev], erfundene Menschen" ) );
        add( 1, T( "Klänge" ) );
        add( 2, T( "Jeder Motor und jeder Knall: im Spiel erzeugt" ) );
        add( 1, T( "Gebaut mit" ) );
        add( 2, "raylib 5.5 (zlib)" );
        add( 2, T( "Schrift: Barlow von Jeremy Tribby (SIL OFL 1.1)" ) );
        add( 2, T( "Menschen: Quaternius' Grundfiguren und Animationen (CC0)" ) );
        add( 2, T( "Programmiert mit Claude (Anthropic)" ) );
        add( 1, T( "Lizenz" ) );
        add( 2, "GNU AGPL 3.0" );
        add( 3, "github.com/reneweller-coding/GravePrix" );
        add( 4, "" );
        add( 4, "" );
        add( 3, T( "Alle Figuren, Fahrer, Firmen und Orte sind erfunden." ) );
        add( 3, T( "Ähnlichkeiten mit lebenden Personen wären Zufall." ) );
        add( 3, T( "Mit toten auch." ) );
        add( 4, "" );
        add( 4, "" );
        add( 1, T( "Graves & Sons danken für Ihre Kundschaft." ) );
        add( 3, T( "Bis bald." ) );
    }
    backdrop();
    DrawRectangle( 0, 0, GetScreenWidth(), GetScreenHeight(), { 0, 0, 0, 170 } );
    float k = ui::k(), cx = GetScreenWidth() * 0.5f;
    m_creditsTime += GetFrameTime() * ( m_in.accept() || IsKeyDown( KEY_SPACE ) ? 4.0f : 1.0f );
    float y = GetScreenHeight() + 40 * k - m_creditsTime * 60 * k;
    for ( const auto &[kind, s] : m_creditLines )
    {
        float size = kind == 0 ? 110 : kind == 1 ? 34 : kind == 2 ? 30 : 24;
        float step = ( kind == 0 ? 140 : kind == 4 ? 50 : kind == 1 ? 58 : 40 ) * k;
        if ( kind == 1 ) y += 18 * k;
        if ( y > -150 * k && y < GetScreenHeight() + 50 * k && kind != 4 )
        {
            Color c = kind == 0 ? Color{ 214, 40, 36, 255 } : kind == 1 ? ui::Gold : kind == 2 ? ui::Ink : ui::Dim;
            if ( s.find( "\xE2\x80\xA0" ) != std::string::npos && kind == 2 ) c = ui::Warn;
            ui::text( s, cx, y, size, c, ui::AlignE::CENTER );
        }
        y += step;
    }
    hints( m_in.padUsed() ? T( "A schneller   ·   B zurück" ) : T( "Leertaste schneller   ·   Esc zurück" ) );
    if ( m_in.back() || y < -100 * k )
    {
        m_creditLines.clear();
        go( m_creditsAfter );
    }
}

void App::newGame()
{
    backdrop();
    float k = ui::k();
    header( T( "Neue Karriere" ), T( "Unterschreib hier. Und hier. Und auf dem Organspendeausweis." ) );
    int &sel = m_sel[(int)ScreenE::NEWGAME];
    const DifficultyDef &d = g_data.difficulty( m_newDiff );
    std::vector<std::string> items = {
        T( "Name: " ) + m_newName + ( m_naming && std::fmod( m_time, 1.0f ) < 0.5f ? "_" : "" ),
        T( "Schwierigkeit: " ) + d.m_title.get(),
        std::string( T( "Lackierung: " ) ) + T( SchemeNames[m_newColor] ),
        T( "Unterschreiben" ),
    };
    if ( m_naming )
    {
        // Typing the name.
        int c;
        while ( ( c = GetCharPressed() ) != 0 )
            if ( c >= 32 && m_newName.size() < 20 )
            {
                if ( c < 128 )
                    m_newName += (char)c;
                else
                {
                    // UTF-8 for the umlauts.
                    int         n = 0;
                    const char *u = CodepointToUTF8( c, &n );
                    m_newName.append( u, (size_t)std::clamp( n, 0, 4 ) );
                }
            }
        if ( IsKeyPressed( KEY_BACKSPACE ) && !m_newName.empty() )
        {
            // Remove one UTF-8 character.
            size_t cut = m_newName.size() - 1;
            while ( cut > 0 && ( (unsigned char)m_newName[cut] & 0xC0 ) == 0x80 ) --cut;
            m_newName.erase( cut );
        }
        if ( IsKeyPressed( KEY_ENTER ) || IsKeyPressed( KEY_ESCAPE ) || IsGamepadButtonPressed( 0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN ) )
        {
            m_naming = false;
            if ( m_newName.empty() ) m_newName = PresetNames[0];
        }
    }
    int pick = m_naming ? -1 : menu( items, sel, { 96 * k, 220 * k, 900 * k, 300 * k }, 42 );
    if ( !m_naming )
    {
        int dir = m_in.left() ? -1 : m_in.right() ? 1 : 0;
        if ( dir != 0 )
        {
            sound( SfxE::UI_MOVE );
            if ( sel == 0 )
            {
                static int preset = 0;
                preset = ( preset + dir + 8 ) % 8;
                m_newName = PresetNames[preset];
            }
            else if ( sel == 1 )
                m_newDiff = std::clamp( m_newDiff + dir, 0, (int)g_data.m_difficulties.size() - 1 );
            else if ( sel == 2 )
                m_newColor = ( m_newColor + dir + 10 ) % 10;
        }
        if ( pick == 0 && !m_in.padUsed() )
        {
            m_naming = true;
            m_newName.clear();
            while ( GetCharPressed() != 0 ) {}
        }
        if ( m_in.back() )
        {
            sound( SfxE::UI_BACK );
            go( ScreenE::TITLE );
        }
    }
    // The difficulty explained, the car shown in its paint.
    Rectangle info = { 96 * k, 560 * k, 900 * k, 300 * k };
    ui::panel( info );
    float y = info.y + 20 * k;
    for ( const std::string &l : ui::wrap( d.m_desc.get(), info.width - 40 * k, 28 ) )
    {
        ui::text( l, info.x + 20 * k, y, 28, ui::Ink );
        y += 36 * k;
    }
    m_show.showroom( g_data.car( "rustbucket" ) ? g_data.car( "rustbucket" )->m_model : "?", scheme( m_newColor ), m_time * 0.6f, 7.5f );
    const Texture2D &t = m_show.texture();
    float            sw = 640 * k, sh = 480 * k;
    DrawTexturePro( t, { 0, 0, (float)t.width, (float)-t.height }, { GetScreenWidth() - sw - 60 * k, 230 * k, sw, sh }, { 0, 0 }, 0, WHITE );
    hints( m_naming ? T( "Namen tippen, Eingabe fertig" )
                    : m_in.padUsed() ? T( "Links/Rechts ändern   ·   A wählen   ·   B zurück" )
                                     : T( "Links/Rechts ändern   ·   Eingabe beim Namen: tippen   ·   Esc zurück" ) );
    if ( pick == 3 )
    {
        m_career.start( m_newName, scheme( m_newColor ), m_newDiff, (uint32_t)std::time( nullptr ) );
        m_inCareer = true;
        saveCareer();
        m_audio.music( "garage" );
        go( ScreenE::HUB );
    }
}

// ---------------------------------------------------------------------------------------------------- stories

void App::showStory( const std::string &key, ScreenE after )
{
    m_storyKey = key;
    m_storyPage = 0;
    m_afterStory = after;
    if ( !g_data.story( key ) )
    {
        go( after );
        return;
    }
    go( ScreenE::STORY );
    sound( SfxE::UI_PAPER );
}

void App::story()
{
    backdrop();
    const StoryDef *s = g_data.story( m_storyKey );
    if ( !s )
    {
        go( m_afterStory );
        return;
    }
    float     k = ui::k();
    Rectangle r = { GetScreenWidth() * 0.5f - 760 * k, 120 * k, 1520 * k, GetScreenHeight() - 230 * k };
    ui::panel( r, 0.9f );
    ui::text( s->m_title.get(), r.x + 40 * k, r.y + 30 * k, 52, ui::Gold );
    // Paragraphs onto pages.
    const auto &prose = s->prose();
    std::vector<std::vector<std::string>> pages( 1 );
    float avail = r.height - 170 * k, used = 0;
    for ( const std::string &p : prose )
    {
        auto  lines = ui::wrap( p, r.width - 80 * k, 32 );
        float hgt = ( lines.size() + 0.6f ) * 42 * k;
        if ( used + hgt > avail && !pages.back().empty() )
        {
            pages.push_back( {} );
            used = 0;
        }
        for ( auto &l : lines ) pages.back().push_back( l );
        pages.back().push_back( "" );
        used += hgt;
    }
    m_storyPage = std::clamp( m_storyPage, 0, (int)pages.size() - 1 );
    float y = r.y + 110 * k;
    for ( const std::string &l : pages[m_storyPage] )
    {
        ui::text( l, r.x + 40 * k, y, 32, ui::Ink );
        y += l.empty() ? 18 * k : 42 * k;
    }
    ui::text( tf( "Seite %d / %d", m_storyPage + 1, (int)pages.size() ), r.x + r.width - 40 * k, r.y + r.height - 50 * k, 24,
              ui::Dim, ui::AlignE::RIGHT );
    hints( m_in.padUsed() ? T( "A weiter   ·   B überspringen" ) : T( "Eingabe weiter   ·   Esc überspringen" ) );
    if ( m_in.accept() || IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
    {
        sound( SfxE::UI_PAPER );
        if ( ++m_storyPage >= (int)pages.size() ) go( m_afterStory );
    }
    else if ( m_in.back() )
        go( m_afterStory );
}

// ---------------------------------------------------------------------------------------------------- quick race

uint32_t App::dailySeed()
{
    std::time_t now = std::time( nullptr );
    std::tm     tm = *std::localtime( &now );
    return (uint32_t)( ( tm.tm_year + 1900 ) * 10000 + ( tm.tm_mon + 1 ) * 100 + tm.tm_mday );
}

std::vector<CarSetup> App::quickField( const VenueDef *vd, const std::string &car, int opponents, int level )
{
    std::vector<CarSetup> field;
    CarSetup              me;
    me.m_car = car;
    me.m_driver = m_inCareer ? m_career.m_name : T( "Du" );
    me.m_paint = scheme( m_qColor );
    me.m_human = true;
    me.m_player = true;
    me.m_engine = me.m_tires = me.m_armor = me.m_gunLv = 3;
    me.m_rear[(int)RearE::MINE] = 4;
    me.m_rear[(int)RearE::OIL] = 2;
    me.m_rockets = 3;
    me.m_nitro = 3;
    // Rivals of the venue's league, then any.
    std::vector<std::string> pool;
    for ( const std::string &r : g_data.m_rivalOrder )
    {
        const RivalDef &d = g_data.m_rivals.at( r );
        if ( !d.m_rookie && vd && d.m_league == vd->m_league && g_data.car( d.m_car ) ) pool.push_back( r );
    }
    for ( const std::string &r : g_data.m_rivalOrder )
        if ( std::find( pool.begin(), pool.end(), r ) == pool.end() && g_data.car( g_data.m_rivals.at( r ).m_car ) &&
             r != "mortimer_graves" )
            pool.push_back( r );
    std::vector<std::string> cars;
    for ( const std::string &c : g_data.m_carOrder )
        if ( g_data.m_cars.at( c ).m_class < 6 ) cars.push_back( c );
    for ( int i = 0; i < opponents; ++i )
    {
        CarSetup s;
        if ( i < (int)pool.size() )
        {
            const RivalDef &d = g_data.m_rivals.at( pool[i] );
            s.m_rival = pool[i];
            s.m_driver = d.m_name;
            s.m_car = d.m_car;
            for ( int j = 0; j < 3; ++j ) s.m_paint.m_c[j] = d.m_colors[j];
            s.m_skill = d.m_skill;
            s.m_aggression = d.m_aggression;
            s.m_weapons = d.m_weapons;
            s.m_likes = d.m_likes;
        }
        else
        {
            s.m_car = cars.empty() ? car : cars[(size_t)( i + 1 ) % cars.size()];
            s.m_driver = tf( "Fahrer %d", i + 2 );
            s.m_paint = scheme( m_qColor + i + 1 );
        }
        s.m_engine = s.m_tires = s.m_armor = s.m_gunLv = level;
        s.m_rear[(int)RearE::MINE] = 3;
        s.m_rockets = 2;
        s.m_nitro = 2;
        field.push_back( s );
    }
    field.push_back( me );
    return field;
}

void App::startTourRound()
{
    // Each round a random track at a random venue; the field grows meaner with the rounds.
    std::vector<std::string> venues;
    for ( const std::string &v : g_data.m_venueOrder )
        if ( v != "proving" ) venues.push_back( v );
    if ( venues.empty() ) return;
    uint32_t h = m_tourSeed * 2654435761u + (uint32_t)m_tour * 40503u;
    h ^= h >> 15;
    std::string venue = venues[h % venues.size()], track = randomTrack( venue, h % 99991u + 1 );
    if ( track.empty() ) return;
    std::vector<std::string> cars;
    for ( const std::string &c : g_data.m_carOrder )
        if ( g_data.m_cars.at( c ).m_class < 6 || m_rec.m_careersWon > 0 ) cars.push_back( c );
    std::string car = cars.empty() ? "rustbucket" : cars[(size_t)std::clamp( m_qCar, 0, (int)cars.size() - 1 )];
    auto field = quickField( g_data.venue( venue ), car, std::max( 1, m_qCars - 1 ), std::min( 6, 1 + m_tour / 2 ) );
    m_inCareer = false;
    startRace( track, ( h >> 8 ) % 2 == 1, field, ModeE::RACE, g_data.track( track )->m_laps, (int)field.size() - 1 );
    m_center = tf( T( "TURNIER  RUNDE %d" ), m_tour );
    m_centerTime = 3.0f;
}

void App::quick()
{
    backdrop();
    float k = ui::k();
    header( T( "Schnelles Rennen" ), T( "Ohne Karriere, ohne Schulden, ohne Beerdigung." ) );
    int &sel = m_sel[(int)ScreenE::QUICK];
    bool tour = m_qMode == 3, daily = m_qMode == 4;
    // The venues in order, the tracks of the chosen one (arenas for the derby).
    // Only the venues with tracks of the chosen kind (not every venue has a derby arena).
    std::vector<std::string> venues;
    for ( const std::string &v : g_data.m_venueOrder )
        for ( const std::string &t : g_data.m_trackOrder )
        {
            const TrackDef &d = g_data.m_tracks.at( t );
            if ( d.m_venue == v && d.m_arena == ( m_qMode == 1 ) && t.rfind( "test_", 0 ) != 0 )
            {
                venues.push_back( v );
                break;
            }
        }
    if ( venues.empty() ) return;
    m_qVenue = std::clamp( m_qVenue, 0, (int)venues.size() - 1 );
    std::vector<std::string> tracks;
    for ( const std::string &t : g_data.m_trackOrder )
    {
        const TrackDef &d = g_data.m_tracks.at( t );
        if ( d.m_venue == venues[m_qVenue] && d.m_arena == ( m_qMode == 1 ) && t.rfind( "test_", 0 ) != 0 ) tracks.push_back( t );
    }
    // Races and time trials: after the venue's tracks a random one.
    bool canRandom = m_qMode == 0 || m_qMode == 2;
    int  slots = (int)tracks.size() + ( canRandom ? 1 : 0 );
    m_qTrack = slots == 0 ? 0 : std::clamp( m_qTrack, 0, slots - 1 );
    bool random = canRandom && m_qTrack == (int)tracks.size();
    std::vector<std::string> cars;
    for ( const std::string &c : g_data.m_carOrder )
        if ( g_data.m_cars.at( c ).m_class < 6 || m_rec.m_careersWon > 0 ) cars.push_back( c );
    if ( cars.empty() ) cars.push_back( "?" );
    m_qCar = std::clamp( m_qCar, 0, (int)cars.size() - 1 );
    // The daily challenge: today's venue, track and car, the same for everyone.
    uint32_t    day = dailySeed();
    std::string dailyVenue = venues[( day * 2654435761u >> 7 ) % venues.size()];
    std::string dailyCar;
    {
        std::vector<std::string> plain;
        for ( const std::string &c : g_data.m_carOrder )
            if ( g_data.m_cars.at( c ).m_class < 6 ) plain.push_back( c );
        dailyCar = plain.empty() ? cars[0] : plain[( day * 40503u >> 5 ) % plain.size()];
    }
    std::string trackName = daily ? randomTrack( dailyVenue, day ) : random ? randomTrack( venues[m_qVenue], m_qSeed )
                          : tracks.empty() || tour ? "" : tracks[m_qTrack];
    const TrackDef *td = trackName.empty() ? nullptr : g_data.track( trackName );
    const VenueDef *vd = g_data.venue( daily ? dailyVenue : venues[m_qVenue] );
    const CarDef   *cd = g_data.car( daily ? dailyCar : cars[m_qCar] );
    static const char *modes[] = { "Rennen", "Derby", "Zeitfahren", "Endlos-Turnier", "Tagesherausforderung" };
    std::string best;
    if ( td )
    {
        auto it = m_rec.m_bestLap.find( td->m_name + ( m_qReverse && !daily ? ":r" : "" ) );
        if ( it != m_rec.m_bestLap.end() ) best = tf( "  (Rekord %.2f s)", it->second );
    }
    // Two players: races and derbies; the second needs a pad (the first keeps the keys or the first pad).
    bool twoModes = m_qMode == 0 || m_qMode == 1, twoPads = IsGamepadAvailable( 0 ) || m_testSplit;
    std::string trackLabel = tour    ? std::string( T( "jede Runde eine neue Zufallsstrecke" ) )
                           : daily   ? tf( T( "Tagesstrecke vom %02u.%02u." ), day % 100, day / 100 % 100 ) + best
                           : random  ? tf( T( "Zufallsstrecke %u  (Eingabe: neu würfeln)" ), m_qSeed )
                           : td      ? td->m_title.get() + best
                                     : std::string( T( "keine" ) );
    std::vector<std::string> items = {
        std::string( T( "Art: " ) ) + T( modes[m_qMode] ),
        std::string( T( "Schauplatz: " ) ) + ( tour ? std::string( T( "überall" ) ) : vd ? vd->m_title.get() : venues[m_qVenue] ),
        std::string( T( "Strecke: " ) ) + trackLabel,
        std::string( T( "Richtung: " ) ) + ( tour || daily ? std::string( "-" ) : m_qReverse ? T( "rückwärts" ) : T( "vorwärts" ) ),
        std::string( T( "Auto: " ) ) + ( cd ? cd->m_title.get() : cars[m_qCar] ) + ( daily ? T( "  (heute für alle)" ) : "" ),
        std::string( T( "Lackierung: " ) ) + T( SchemeNames[m_qColor] ),
        T( "Gegner: " ) + std::to_string( m_qMode == 2 || daily ? 0 : m_qCars - 1 ),
        std::string( T( "Runden: " ) ) + ( m_qMode == 1 || tour || daily ? std::string( "-" ) : std::to_string( m_qLaps ) ),
        T( "Schwierigkeit: " ) + g_data.difficulty( m_qDiff ).m_title.get(),
        std::string( T( "Spieler: " ) ) + ( m_qPlayers == 2 && !twoModes ? std::string( T( "1  (zu zweit: Rennen und Derby)" ) )
                                           : m_qPlayers == 2 && !twoPads ? std::string( T( "2  (Spieler 2 braucht ein Pad)" ) )
                                           : m_qPlayers == 2             ? std::string( T( "2  (geteilter Bildschirm)" ) )
                                                                         : std::string( "1" ) ),
        tour ? tf( T( "Start!   (Rekord: %d Runden)" ), m_rec.m_tourBest ) : std::string( T( "Start!" ) ),
    };
    int pick = menu( items, sel, { 96 * k, 210 * k, 860 * k, 700 * k }, 38 );
    int dir = m_in.left() ? -1 : m_in.right() ? 1 : 0;
    if ( dir )
    {
        sound( SfxE::UI_MOVE );
        switch ( sel )
        {
        case 0: m_qMode = ( m_qMode + dir + 5 ) % 5; m_qTrack = 0; break;
        case 1: m_qVenue = ( m_qVenue + dir + (int)venues.size() ) % (int)venues.size(); m_qTrack = 0; break;
        case 2: if ( slots > 0 ) m_qTrack = ( m_qTrack + dir + slots ) % slots; break;
        case 3: m_qReverse = !m_qReverse; break;
        case 4: m_qCar = ( m_qCar + dir + (int)cars.size() ) % (int)cars.size(); break;
        case 5: m_qColor = ( m_qColor + dir + 10 ) % 10; break;
        case 6: m_qCars = std::clamp( m_qCars + dir, 2, 8 ); break;
        case 7: m_qLaps = std::clamp( m_qLaps + dir, 1, 9 ); break;
        case 8: m_qDiff = std::clamp( m_qDiff + dir, 0, (int)g_data.m_difficulties.size() - 1 ); break;
        case 9: m_qPlayers = 3 - m_qPlayers; break;
        default: break;
        }
    }
    // Enter on a random track throws the dice again.
    if ( pick == 2 && random )
    {
        m_qSeed = (uint32_t)GetRandomValue( 1, 99999 );
        sound( SfxE::UI_OK );
    }
    if ( td && m_qReverse && !td->m_reverse ) m_qReverse = false;
    // The car turning on the right.
    m_show.showroom( cd ? cd->m_model : "?", scheme( m_qColor ), m_time * 0.6f, 7.5f );
    const Texture2D &t = m_show.texture();
    float            sw = 640 * k, sh = 480 * k;
    Rectangle        pic = { GetScreenWidth() - sw - 60 * k, 200 * k, sw, sh };
    DrawTexturePro( t, { 0, 0, (float)t.width, (float)-t.height }, pic, { 0, 0 }, 0, WHITE );
    if ( cd )
    {
        float y = pic.y + pic.height + 10 * k;
        std::string desc = tour    ? std::string( T( "Jede Runde eine neue Strecke irgendwo im Grave Prix. Wer unter die ersten drei kommt, fährt weiter. Wer nicht, bekommt einen Kranz." ) )
                         : daily   ? std::string( T( "Heute für alle dieselbe Strecke und dasselbe Auto. Zeitfahren gegen deinen eigenen Geist." ) )
                                   : cd->m_desc.get();
        for ( const std::string &l : ui::wrap( desc, sw, 26 ) )
        {
            ui::text( l, pic.x, y, 26, ui::Ink );
            y += 32 * k;
        }
    }
    hints( m_in.padUsed() ? T( "Links/Rechts ändern   ·   A Start   ·   B zurück" ) : T( "Links/Rechts ändern   ·   Eingabe Start   ·   Esc zurück" ) );
    if ( m_in.back() )
    {
        sound( SfxE::UI_BACK );
        go( ScreenE::TITLE );
        return;
    }
    if ( pick == 9 )
    {
        m_qPlayers = 3 - m_qPlayers;
        sound( SfxE::UI_MOVE );
    }
    if ( pick != 10 ) return;
    if ( tour )
    {
        m_tour = 1;
        m_tourSeed = (uint32_t)GetRandomValue( 1, 1 << 30 );
        startTourRound();
        return;
    }
    if ( !td ) return;
    m_tour = 0;
    int  opponents = m_qMode == 2 || daily ? 0 : m_qCars - 1;
    auto field = quickField( vd, cd ? cd->m_name : cars[m_qCar], opponents, 3 );
    m_inCareer = false;
    ModeE mode = m_qMode == 1 ? ModeE::DERBY : m_qMode == 2 || daily ? ModeE::TRIAL : ModeE::RACE;
    m_player2 = -1;
    if ( m_qPlayers == 2 && twoModes && !daily )
    {
        if ( !twoPads )
        {
            sound( SfxE::UI_DENY );
            return;
        }
        // The second player: the same car in another paint, beside the first on the grid.
        CarSetup p2 = field.back();
        p2.m_driver = T( "Spieler 2" );
        p2.m_paint = scheme( m_qColor + 5 );
        p2.m_pad = 1;
        field.insert( field.end() - 1, p2 );
        m_player2 = (int)field.size() - 2;
    }
    startRace( td->m_name, m_qReverse && !daily, field, mode, m_qMode == 1 ? 1 : daily ? 3 : m_qLaps, (int)field.size() - 1 );
}

// ---------------------------------------------------------------------------------------------------- options

void App::options()
{
    backdrop();
    float k = ui::k();
    header( T( "Optionen" ) );
    int &sel = m_sel[(int)ScreenE::OPTIONS];
    // Four tabs: the picture, the sound, the display and the driving. Each row is a text, what left/right (or
    // accept) does to it and a line of help; the last row of every tab goes back.
    static const char *tabs[] = { "Bild", "Ton", "Anzeige", "Fahren" };
    bool prev = m_in.prevTab(), next = m_in.nextTab();
    if ( prev || next )
    {
        m_optTab = ( m_optTab + ( next ? 1 : 3 ) ) % 4;
        sel = 0;
        sound( SfxE::UI_MOVE );
    }
    {
        float tx = 96 * k;
        for ( int i = 0; i < 4; ++i )
        {
            Rectangle hit = { tx - 8 * k, 180 * k, ui::width( T( tabs[i] ), 32 ) + 16 * k, 44 * k };
            if ( i != m_optTab && CheckCollisionPointRec( GetMousePosition(), hit ) && IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
            {
                m_optTab = i;
                sel = 0;
                sound( SfxE::UI_MOVE );
            }
            float w = ui::text( T( tabs[i] ), tx, 186 * k, 32, i == m_optTab ? ui::Accent : ui::Dim );
            tx += w + 40 * k;
        }
        ui::text( m_in.padUsed() ? T( "LB / RB: Reiter" ) : T( "Q / E: Reiter" ), tx + 20 * k, 192 * k, 24, ui::Dim );
    }
    static const char *qual[] = { "Schnell", "Ausgewogen", "Fein" };
    static const char *palettes[] = { "wie gemacht", "Rot-Grün-Schwäche", "Blau-Gelb-Schwäche" };
    static const char *looks[] = { "aus", "an", "automatisch" };
    static const int   rates[] = { 30, 40, 60, 0 };
    auto pct = []( float v ) { return std::to_string( (int)std::lround( v * 100 ) ) + " %"; };
    auto onOff = []( bool v ) { return std::string( v ? T( "an" ) : T( "aus" ) ); };
    struct Row
    {
        std::string                m_text;
        std::function<void( int )> m_change;          ///< dir -1 / +1; empty: a button
        std::string                m_help;
        bool                       m_apply = false;   ///< the renderer, the sound or the text size follow
        int                        m_button = 0;      ///< 1 the controls, 2 the driving school, 3 the benchmark
    };
    std::vector<Row> rows;
    if ( m_optTab == 0 )
    {
        rows.push_back( { std::string( T( "Vollbild: " ) ) + onOff( m_set.m_fullscreen ), [this]( int ) {
                             ToggleBorderlessWindowed();
                             m_set.m_fullscreen = !m_set.m_fullscreen;
                         } } );
        rows.push_back( { std::string( T( "Grafik: " ) ) + T( qual[quality()] ) + ( m_set.m_quality < 0 ? T( " (automatisch)" ) : "" ),
                          [this]( int d ) { m_set.m_quality = ( quality() + d + 3 ) % 3; },
                          T( "Schnell rechnet das Bild kleiner und lässt Schatten und Spiegelungen weg." ), true } );
        rows.push_back( { std::string( T( "Bildrate: " ) ) + ( m_set.m_fps > 0 ? std::to_string( m_set.m_fps ) : std::string( T( "unbegrenzt" ) ) ),
                          [this]( int d ) {
                              int at = 0;
                              while ( at < 3 && rates[at] != m_set.m_fps ) ++at;
                              m_set.m_fps = rates[( at + d + 4 ) % 4];
                          },
                          "", true } );
        rows.push_back( { std::string( T( "Miniatur-Look: " ) ) + T( looks[std::clamp( m_set.m_miniature, 0, 2 )] ),
                          [this]( int d ) {
                              m_set.m_miniature = ( m_set.m_miniature + d + 3 ) % 3;
                              m_set.m_lookDropped = false;
                              m_view.m_miniature = miniatureOn();
                          },
                          T( "Oben und unten unscharf, wie bei einer Modellbahn. Automatisch: aus, sobald die Bildrate nicht mehr hält." ) } );
        rows.push_back( { T( "Kamerahöhe: " ) + pct( m_set.m_zoom ), [this]( int d ) { m_set.m_zoom = clampf( m_set.m_zoom + d * 0.1f, 0.6f, 1.6f ); } } );
        rows.push_back( { T( "Bildwackeln: " ) + pct( m_set.m_shake ), [this]( int d ) { m_set.m_shake = clampf( m_set.m_shake + d * 0.25f, 0, 1.5f ); } } );
        rows.push_back( { T( "Leistungstest" ), nullptr,
                          T( "Fährt eine halbe Minute über die schwerste Strecke, misst die Bildrate mit und ohne Miniatur-Look "
                             "und stellt beides passend ein." ),
                          false, 3 } );
    }
    else if ( m_optTab == 1 )
    {
        rows.push_back( { T( "Musik: " ) + pct( m_set.m_music ),
                          [this]( int d ) { m_set.m_music = clampf( std::round( ( m_set.m_music + d * 0.1f ) * 10 ) / 10, 0, 1 ); }, "", true } );
        rows.push_back( { T( "Effekte: " ) + pct( m_set.m_effects ),
                          [this]( int d ) { m_set.m_effects = clampf( std::round( ( m_set.m_effects + d * 0.1f ) * 10 ) / 10, 0, 1 ); },
                          T( "Motoren, Waffen, das Publikum." ), true } );
        rows.push_back( { std::string( T( "Sprüche im Rennen: " ) ) + onOff( m_set.m_speech ), [this]( int ) { m_set.m_speech = !m_set.m_speech; },
                          T( "Was die Rivalen und Ricky Rattle im Rennen sagen." ) } );
    }
    else if ( m_optTab == 2 )
    {
        rows.push_back( { T( "Sprache: " ) + std::string( english() ? "English" : "Deutsch" ), [this]( int ) {
                             setLanguage( english() ? "de" : "en" );
                             m_set.m_language = language();
                             loadUiTexts( m_opt.m_data );
                         } } );
        rows.push_back( { T( "Schriftgröße: " ) + pct( m_set.m_textSize ),
                          [this]( int d ) { m_set.m_textSize = clampf( m_set.m_textSize + d * 0.1f, 0.8f, 1.5f ); }, "", true } );
        rows.push_back( { T( "Anzeige im Rennen: " ) + pct( m_set.m_hud ),
                          [this]( int d ) { m_set.m_hud = clampf( std::round( ( m_set.m_hud + d * 0.1f ) * 10 ) / 10, 0.7f, 1.4f ); } } );
        rows.push_back( { std::string( T( "Farbsicht: " ) ) + T( palettes[std::clamp( m_set.m_palette, 0, 2 )] ),
                          [this]( int d ) { m_set.m_palette = ( m_set.m_palette + d + 3 ) % 3; }, "", true } );
        rows.push_back( { std::string( T( "Namen über den Autos: " ) ) + onOff( m_set.m_tags ), [this]( int ) { m_set.m_tags = !m_set.m_tags; } } );
        rows.push_back( { std::string( T( "Kurvenpfeile: " ) ) + onOff( m_set.m_paceNotes ), [this]( int ) { m_set.m_paceNotes = !m_set.m_paceNotes; },
                          T( "Eine Tafel zeigt die nächste Kurve: ihre Form, wie weit und wie scharf (1 bis 4)." ) } );
        rows.push_back( { std::string( T( "Ideallinie: " ) ) + onOff( m_set.m_racingLine ), [this]( int ) { m_set.m_racingLine = !m_set.m_racingLine; },
                          T( "Die Linie auf der Straße: grün Vollgas, gelb vom Gas, rot bremsen." ) } );
        rows.push_back( { std::string( T( "Wiederholung im Ziel: " ) ) + onOff( m_set.m_replay ), [this]( int ) { m_set.m_replay = !m_set.m_replay; },
                          T( "Nach dem Rennen der Zieleinlauf noch einmal in Zeitlupe, aus Kameras an der Strecke. Eingabe überspringt." ) } );
    }
    else
    {
        rows.push_back( { std::string( T( "Fahrhilfe: " ) ) + ( m_set.m_assist == 0   ? T( "aus (rutschiger, für Könner)" )
                                                                : m_set.m_assist == 1 ? T( "an" )
                                                                                      : T( "an, mit Bremshilfe" ) ),
                          [this]( int d ) { m_set.m_assist = ( m_set.m_assist + d + 3 ) % 3; },
                          T( "Hält den Wagen in der Spur. Mit Bremshilfe geht er vor zu schnellen Kurven selbst vom Gas." ) } );
        rows.push_back( { T( "Spieltempo: " ) + pct( m_set.m_speed ),
                          [this]( int d ) { m_set.m_speed = clampf( std::round( ( m_set.m_speed + d * 0.05f ) * 20 ) / 20, 0.7f, 1.0f ); },
                          T( "Das ganze Rennen läuft langsamer ab: mehr Zeit für jede Kurve. Bestzeiten zählen dann nicht." ) } );
        rows.push_back( { std::string( T( "Reifen warnen: " ) ) + onOff( m_set.m_gripWarn ), [this]( int ) { m_set.m_gripWarn = !m_set.m_gripWarn; },
                          T( "Kurz bevor die Reifen rutschen, singen sie, und das Pad brummt leise." ) } );
        rows.push_back( { std::string( T( "Lenkung: " ) ) + ( m_set.m_stickAims ? T( "Stick zeigt Richtung" ) : T( "Stick dreht" ) ),
                          [this]( int ) { m_set.m_stickAims = !m_set.m_stickAims; },
                          T( "Stick zeigt Richtung: Der Wagen fährt dahin, wohin der Stick zeigt." ) } );
        rows.push_back( { std::string( T( "Vibration: " ) ) + onOff( m_set.m_rumble ), [this]( int ) { m_set.m_rumble = !m_set.m_rumble; } } );
        rows.push_back( { T( "Fahrschule" ), nullptr, T( "Drei kurze Übungen für Lenkung und Bremse, mit Medaillen." ), false, 2 } );
        rows.push_back( { T( "Steuerung" ), nullptr, T( "Tasten und Pad-Knöpfe neu belegen." ), false, 1 } );
    }
    rows.push_back( { T( "Zurück" ), nullptr } );
    std::vector<std::string> items;
    for ( const Row &r : rows ) items.push_back( r.m_text );
    int pick = menu( items, sel, { 96 * k, 260 * k, 1000 * k, 720 * k }, 38 );
    int dir = m_in.left() ? -1 : m_in.right() ? 1 : 0;
    const Row &row = rows[(size_t)std::clamp( sel, 0, (int)rows.size() - 1 )];
    bool back = m_in.back();
    if ( row.m_change && ( dir || pick == sel ) )
    {
        row.m_change( dir ? dir : 1 );
        if ( row.m_apply ) applySettings();
        sound( SfxE::UI_MOVE );
        saveSettings();
    }
    else if ( pick >= 0 && !rows[(size_t)pick].m_change )
    {
        int button = rows[(size_t)pick].m_button;
        if ( button == 1 ) go( ScreenE::CONTROLS );
        else if ( button == 2 )
        {
            saveSettings();
            go( ScreenE::SCHOOL );
            return;
        }
        else if ( button == 3 )
            startBenchmark();
        else
            back = true;
    }
    if ( !row.m_help.empty() )
    {
        Rectangle r = { 1140 * k, 260 * k, GetScreenWidth() - 1236 * k, 0 };
        if ( r.width > 240 * k )
        {
            std::vector<std::string> lines = ui::wrap( row.m_help, r.width - 40 * k, 26 );
            r.height = lines.size() * ui::height( 26 ) + 40 * k;
            ui::panel( r, 0.75f );
            float y = r.y + 20 * k;
            for ( const std::string &l : lines )
            {
                ui::text( l, r.x + 20 * k, y, 26, ui::Ink );
                y += ui::height( 26 );
            }
        }
    }
    if ( back )
    {
        saveSettings();
        go( m_racing ? ScreenE::RACE : m_inCareer ? ScreenE::HUB : ScreenE::TITLE );
    }
    hints( T( "Links/Rechts ändern   ·   F11 Vollbild   ·   F12 Bildschirmfoto" ) );
}

void App::controls()
{
    backdrop();
    float k = ui::k();
    header( T( "Steuerung" ), T( "Eingabe: Taste neu belegen   ·   Links/Rechts: Tastatur oder Pad" ) );
    int &sel = m_sel[(int)ScreenE::CONTROLS];
    if ( m_bindAct >= 0 )
    {
        int key = m_bindPad ? 0 : Input::capturedKey(), pad = m_bindPad ? m_in.capturedPad() : 0;
        if ( key || pad )
        {
            Bind &b = m_in.m_binds[m_bindAct];
            if ( m_bindPad )
                b.m_pad = pad;
            else
                b.m_key = key;
            m_bindAct = -1;
            saveSettings();
            sound( SfxE::UI_OK );
        }
        else if ( IsKeyPressed( KEY_ESCAPE ) )
            m_bindAct = -1;
    }
    static const char *names[] = { "Gas", "Bremse / Rückwärts", "Links", "Rechts", "Handbremse", "Bordkanone", "Heckware abwerfen",
                                   "Rakete", "Lachgas", "Heckware wechseln", "Hupe", "Pause",
                                   "Zurück auf die Strecke" };
    std::vector<std::string> items;
    for ( int a = 0; a < (int)ActE::COUNT; ++a )
        items.push_back( std::string( T( names[a] ) ) + ":   " + m_in.label( (ActE)a, false ) + "   |   " + m_in.label( (ActE)a, true ) );
    items.push_back( T( "Standard wiederherstellen" ) );
    items.push_back( T( "Zurück" ) );
    if ( m_bindAct < 0 )
    {
        if ( m_in.left() ) m_bindPad = false;
        if ( m_in.right() ) m_bindPad = true;
        int pick = menu( items, sel, { 96 * k, 210 * k, 1500 * k, 760 * k }, 34 );
        if ( pick >= 0 && pick < (int)ActE::COUNT ) m_bindAct = pick;
        else if ( pick == (int)ActE::COUNT )
        {
            m_in.reset();
            saveSettings();
        }
        else if ( pick == (int)ActE::COUNT + 1 || m_in.back() )
            go( ScreenE::OPTIONS );
    }
    else
        menu( items, sel, { 96 * k, 210 * k, 1500 * k, 760 * k }, 34 );
    // The pad's buttons drawn as on the pad, beside the list.
    {
        float px = GetScreenWidth() - 420 * k, py = 220 * k;
        ui::panel( { px - 20 * k, py - 16 * k, 380 * k, ( (float)ActE::COUNT * 46 + 24 ) * k }, 0.7f );
        for ( int a = 0; a < (int)ActE::COUNT; ++a )
        {
            const Bind &b = m_in.m_binds[a];
            float       x = px;
            for ( int btn : { b.m_pad, b.m_pad2 } )
                if ( btn )
                {
                    // The cross's directions as arrows on a pill.
                    std::string lab = btn == GAMEPAD_BUTTON_LEFT_FACE_UP      ? "D_UP"
                                    : btn == GAMEPAD_BUTTON_LEFT_FACE_DOWN    ? "D_DOWN"
                                    : btn == GAMEPAD_BUTTON_LEFT_FACE_LEFT    ? "D_LEFT"
                                    : btn == GAMEPAD_BUTTON_LEFT_FACE_RIGHT   ? "D_RIGHT"
                                                                              : Input::padLabel( btn );
                    float       gw = ui::glyph( lab, x, py, 30 );
                    if ( gw <= 0 ) gw = ui::text( lab, x, py, 26, ui::Ink );
                    x += gw + 8 * k;
                }
            ui::text( T( names[a] ), px + 120 * k, py + 2 * k, 24, ui::Dim );
            py += 46 * k;
        }
    }
    hints( m_bindAct >= 0 ? ( m_bindPad ? T( "Pad-Knopf drücken ..." ) : T( "Taste drücken ... (Esc bricht ab)" ) )
                          : ( m_bindPad ? T( "Spalte: Pad   ·   Lenkung, Gas und Bremse auch über Stick und Trigger" )
                                        : T( "Spalte: Tastatur" ) ) );
}

int runGame( const Options &opt )
{
    App app;
    if ( !app.init( opt ) ) return 1;
    app.run();
    app.shutdown();
    return app.testResult() ? 1 : 0;
}

}   // namespace grave
