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
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>

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

std::string careerPath() { return userDir() + "career.txt"; }

}   // namespace

// ---------------------------------------------------------------------------------------------------- setup

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
        fs::remove( careerPath(), ec );
        if ( m_testWords.empty() )
        {
            printf( "apptest: no script %s\n", o.value( "--apptest" ).c_str() );
            return false;
        }
    }
    unsigned flags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT;
    if ( m_test ) flags = FLAG_WINDOW_HIDDEN;
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
    bool full = o.has( "--fullscreen" ) || ( !o.has( "--window" ) && ( m_set.m_fullscreen || ( m_deck && m_set.m_width == 0 ) ) );
    if ( full ) ToggleBorderlessWindowed();
    SetTargetFPS( m_test ? 0 : m_set.m_fps );
    bool mute = o.has( "--mute" ) || m_test;
    if ( !mute ) InitAudioDevice();
    m_audio.init( !mute, o.m_root + "/assets" );
    ui::init( o.m_root + "/assets" );
    applySettings();
    m_show.init( 640, 480, 2 );
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
    int   q = quality();
    float s = QualityScale[q];
    m_view.shutdown();
    m_view.init( std::max( 320, (int)( GetScreenWidth() * s ) ), std::max( 200, (int)( GetScreenHeight() * s ) ), q );
    if ( m_racing ) m_view.setTrack( m_track );
    else if ( m_atReady ) m_view.setTrack( m_atTrack );
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
    m_show.shutdown();
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
            TakeScreenshot( shot.c_str() );
        }
        m_in.update();
        m_audio.update( dt );
        frame( dt );
    }
}

void App::testStep()
{
    static const char *names[] = { "title", "newgame", "hub", "offers", "garage", "market", "shark", "sponsor", "news", "table",
                                   "story", "race", "results", "quick", "options", "controls" };
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
    if ( m_screen != ScreenE::RACE && !m_racing ) attract( dt );
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
    case ScreenE::STORY: story(); break;
    case ScreenE::RACE: race( dt ); break;
    case ScreenE::RESULTS: results(); break;
    case ScreenE::QUICK: quick(); break;
    case ScreenE::OPTIONS: options(); break;
    case ScreenE::CONTROLS: controls(); break;
    default: break;
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
    ui::text( text, 70 * k, GetScreenHeight() - 44 * k, 26, ui::Dim );
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
    bool                     canLoad = fs::exists( careerPath() );
    std::vector<std::string> items = { T( "Karriere fortsetzen" ), T( "Neue Karriere" ), T( "Schnelles Rennen" ), T( "Optionen" ),
                                       T( "Beenden" ) };
    std::vector<bool>        on = { canLoad, true, true, true, true };
    if ( !canLoad && m_sel[(int)ScreenE::TITLE] == 0 ) m_sel[(int)ScreenE::TITLE] = 1;
    int pick = menu( items, m_sel[(int)ScreenE::TITLE], { 96 * k, 420 * k, 640 * k, 420 * k }, 48, &on );
    hints( m_in.padUsed() ? T( "A wählen   ·   Steuerkreuz bewegen" ) : T( "Eingabe wählen   ·   Pfeile bewegen   ·   F11 Vollbild" ) );
    ui::text( "v" GRAVE_VERSION, GetScreenWidth() - 30 * k, GetScreenHeight() - 44 * k, 22, ui::Dim, ui::AlignE::RIGHT );
    switch ( pick )
    {
    case 0:
        if ( m_career.load( careerPath() ) )
        {
            m_inCareer = true;
            m_audio.music( "garage" );
            go( m_career.m_over ? ScreenE::TABLE : ScreenE::HUB );
        }
        break;
    case 1: go( ScreenE::NEWGAME ); break;
    case 2: go( ScreenE::QUICK ); break;
    case 3: go( ScreenE::OPTIONS ); break;
    case 4: m_quit = true; break;
    default: break;
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
                    char buf[4];
                    int  n = 0;
                    const char *u = CodepointToUTF8( c, &n );
                    for ( int i = 0; i < n; ++i ) buf[i] = u[i];
                    m_newName.append( buf, n );
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

void App::quick()
{
    backdrop();
    float k = ui::k();
    header( T( "Schnelles Rennen" ), T( "Ohne Karriere, ohne Schulden, ohne Beerdigung." ) );
    int &sel = m_sel[(int)ScreenE::QUICK];
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
    m_qTrack = tracks.empty() ? 0 : std::clamp( m_qTrack, 0, (int)tracks.size() - 1 );
    std::vector<std::string> cars;
    for ( const std::string &c : g_data.m_carOrder )
        if ( g_data.m_cars.at( c ).m_class < 6 || m_rec.m_careersWon > 0 ) cars.push_back( c );
    if ( cars.empty() ) cars.push_back( "?" );
    m_qCar = std::clamp( m_qCar, 0, (int)cars.size() - 1 );
    const TrackDef *td = tracks.empty() ? nullptr : g_data.track( tracks[m_qTrack] );
    const VenueDef *vd = g_data.venue( venues[m_qVenue] );
    const CarDef   *cd = g_data.car( cars[m_qCar] );
    static const char *modes[] = { "Rennen", "Derby", "Zeitfahren" };
    std::string best;
    if ( td )
    {
        auto it = m_rec.m_bestLap.find( td->m_name + ( m_qReverse ? ":r" : "" ) );
        if ( it != m_rec.m_bestLap.end() ) best = tf( "  (Rekord %.2f s)", it->second );
    }
    std::vector<std::string> items = {
        std::string( T( "Art: " ) ) + T( modes[m_qMode] ),
        std::string( T( "Schauplatz: " ) ) + ( vd ? vd->m_title.get() : venues[m_qVenue] ),
        std::string( T( "Strecke: " ) ) + ( td ? td->m_title.get() : T( "keine" ) ) + best,
        std::string( T( "Richtung: " ) ) + ( m_qReverse ? T( "rückwärts" ) : T( "vorwärts" ) ),
        std::string( T( "Auto: " ) ) + ( cd ? cd->m_title.get() : cars[m_qCar] ),
        std::string( T( "Lackierung: " ) ) + T( SchemeNames[m_qColor] ),
        T( "Gegner: " ) + std::to_string( m_qMode == 2 ? 0 : m_qCars - 1 ),
        std::string( T( "Runden: " ) ) + ( m_qMode == 1 ? std::string( "-" ) : std::to_string( m_qLaps ) ),
        T( "Schwierigkeit: " ) + g_data.difficulty( m_qDiff ).m_title.get(),
        T( "Start!" ),
    };
    int pick = menu( items, sel, { 96 * k, 210 * k, 860 * k, 700 * k }, 38 );
    int dir = m_in.left() ? -1 : m_in.right() ? 1 : 0;
    if ( dir )
    {
        sound( SfxE::UI_MOVE );
        switch ( sel )
        {
        case 0: m_qMode = ( m_qMode + dir + 3 ) % 3; m_qTrack = 0; break;
        case 1: m_qVenue = ( m_qVenue + dir + (int)venues.size() ) % (int)venues.size(); m_qTrack = 0; break;
        case 2: if ( !tracks.empty() ) m_qTrack = ( m_qTrack + dir + (int)tracks.size() ) % (int)tracks.size(); break;
        case 3: m_qReverse = !m_qReverse; break;
        case 4: m_qCar = ( m_qCar + dir + (int)cars.size() ) % (int)cars.size(); break;
        case 5: m_qColor = ( m_qColor + dir + 10 ) % 10; break;
        case 6: m_qCars = std::clamp( m_qCars + dir, 2, 8 ); break;
        case 7: m_qLaps = std::clamp( m_qLaps + dir, 1, 9 ); break;
        case 8: m_qDiff = std::clamp( m_qDiff + dir, 0, (int)g_data.m_difficulties.size() - 1 ); break;
        default: break;
        }
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
        for ( const std::string &l : ui::wrap( cd->m_desc.get(), sw, 26 ) )
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
    if ( pick == 9 && td )
    {
        std::vector<CarSetup> field;
        CarSetup me;
        me.m_car = cars[m_qCar];
        me.m_driver = m_inCareer ? m_career.m_name : T( "Du" );
        me.m_paint = scheme( m_qColor );
        me.m_human = true;
        me.m_player = true;
        me.m_engine = me.m_tires = me.m_armor = me.m_gunLv = 3;
        me.m_rear[(int)RearE::MINE] = 4;
        me.m_rear[(int)RearE::OIL] = 2;
        me.m_rockets = 3;
        me.m_nitro = 3;
        int opponents = m_qMode == 2 ? 0 : m_qCars - 1;
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
                s.m_car = cars[( m_qCar + i + 1 ) % cars.size()];
                s.m_driver = tf( "Fahrer %d", i + 2 );
                s.m_paint = scheme( m_qColor + i + 1 );
            }
            s.m_engine = s.m_tires = s.m_armor = s.m_gunLv = 3;
            s.m_rear[(int)RearE::MINE] = 3;
            s.m_rockets = 2;
            s.m_nitro = 2;
            field.push_back( s );
        }
        field.push_back( me );
        m_inCareer = false;
        ModeE mode = m_qMode == 1 ? ModeE::DERBY : m_qMode == 2 ? ModeE::TRIAL : ModeE::RACE;
        startRace( td->m_name, m_qReverse, field, mode, m_qMode == 1 ? 1 : m_qLaps, (int)field.size() - 1 );
    }
}

// ---------------------------------------------------------------------------------------------------- options

void App::options()
{
    backdrop();
    float k = ui::k();
    header( T( "Optionen" ) );
    int &sel = m_sel[(int)ScreenE::OPTIONS];
    static const char *qual[] = { "Schnell", "Ausgewogen", "Fein" };
    auto pct = []( float v ) { return std::to_string( (int)std::lround( v * 100 ) ) + " %"; };
    std::vector<std::string> items = {
        T( "Sprache: " ) + std::string( english() ? "English" : "Deutsch" ),
        T( "Musik: " ) + pct( m_set.m_music ),
        T( "Effekte: " ) + pct( m_set.m_effects ),
        std::string( T( "Vollbild: " ) ) + ( m_set.m_fullscreen ? T( "an" ) : T( "aus" ) ),
        std::string( T( "Grafik: " ) ) + T( qual[quality()] ) + ( m_set.m_quality < 0 ? T( " (automatisch)" ) : "" ),
        T( "Kamerahöhe: " ) + pct( m_set.m_zoom ),
        T( "Bildwackeln: " ) + pct( m_set.m_shake ),
        std::string( T( "Vibration: " ) ) + ( m_set.m_rumble ? T( "an" ) : T( "aus" ) ),
        std::string( T( "Sprüche im Rennen: " ) ) + ( m_set.m_speech ? T( "an" ) : T( "aus" ) ),
        T( "Schriftgröße: " ) + pct( m_set.m_textSize ),
        T( "Steuerung" ),
        T( "Zurück" ),
    };
    int pick = menu( items, sel, { 96 * k, 210 * k, 1000 * k, 800 * k }, 38 );
    int dir = m_in.left() ? -1 : m_in.right() ? 1 : 0;
    if ( dir || pick >= 0 )
    {
        if ( pick >= 0 && pick < 10 && dir == 0 ) dir = 1;
        switch ( sel )
        {
        case 0:
            setLanguage( english() ? "de" : "en" );
            m_set.m_language = language();
            loadUiTexts( m_opt.m_data );
            break;
        case 1: m_set.m_music = clampf( std::round( ( m_set.m_music + dir * 0.1f ) * 10 ) / 10, 0, 1 ); break;
        case 2: m_set.m_effects = clampf( std::round( ( m_set.m_effects + dir * 0.1f ) * 10 ) / 10, 0, 1 ); break;
        case 3:
            ToggleBorderlessWindowed();
            m_set.m_fullscreen = !m_set.m_fullscreen;
            break;
        case 4: m_set.m_quality = ( quality() + dir + 3 ) % 3; break;
        case 5: m_set.m_zoom = clampf( m_set.m_zoom + dir * 0.1f, 0.6f, 1.6f ); break;
        case 6: m_set.m_shake = clampf( m_set.m_shake + dir * 0.25f, 0, 1.5f ); break;
        case 7: m_set.m_rumble = !m_set.m_rumble; break;
        case 8: m_set.m_speech = !m_set.m_speech; break;
        case 9: m_set.m_textSize = clampf( m_set.m_textSize + dir * 0.1f, 0.8f, 1.5f ); break;
        default: break;
        }
        if ( sel <= 9 && ( sel == 4 || sel == 9 || sel == 1 || sel == 2 ) ) applySettings();
        if ( sel <= 9 ) sound( SfxE::UI_MOVE );
        saveSettings();
    }
    if ( pick == 10 ) go( ScreenE::CONTROLS );
    if ( pick == 11 || m_in.back() )
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
                                   "Rakete", "Lachgas", "Heckware wechseln", "Hupe", "Pause" };
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
