/**
 * @file AppSchool.cpp
 * @brief The driving school (three exercises on the proving ground with medals, docs/DESIGN.md "The driving
 *        school"), the benchmark in the options (the heaviest track with and without the miniature look, the settings
 *        that hold the frame rate) and the automatic miniature look's watch on the frame rate during a race.
 * @author Rene Weller
 */
#include "App.h"

#include "Files.h"
#include "Lang.h"
#include "Ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace grave
{

namespace
{

/** @brief The exercises: their tracks, titles and what to do. */
struct Exercise
{
    const char *m_track, *m_title, *m_task;
};
const Exercise Exercises[3] = {
    { "test_school_slalom", "Slalom",
      "Fahr durch jedes Tor aus zwei Hütchen. Ein verpasstes Tor kostet 2 Sekunden, ein umgefahrenes Hütchen eine." },
    { "test_school_hairpin", "Haarnadeln",
      "Bremse, solange die Linie rot ist, lenk am Ende der Bremsung ein und gib am Scheitel wieder Gas." },
    { "test_school_esses", "Kurvenkombination",
      "Lenk früh und sanft und folge der Linie. Wer zu spät einlenkt, verliert die nächste Kurve." } };

const char *MedalNames[4] = { "keine Medaille", "Bronze", "Silber", "Gold" };
const Color MedalColors[4] = { { 150, 140, 130, 255 }, { 205, 127, 50, 255 }, { 200, 205, 215, 255 }, { 240, 200, 60, 255 } };

/** @return the frames a second of a list of frame times, and the slowest 5 % of them */
void fpsOf( const std::vector<float> &frames, float &avg, float &low )
{
    avg = low = 0.0f;
    if ( frames.empty() ) return;
    float sum = std::accumulate( frames.begin(), frames.end(), 0.0f );
    avg = frames.size() / std::max( 1e-4f, sum );
    std::vector<float> s = frames;
    std::sort( s.begin(), s.end() );
    size_t n = std::max<size_t>( 1, s.size() / 20 );
    float  slow = 0.0f;
    for ( size_t k = s.size() - n; k < s.size(); ++k ) slow += s[k];
    low = n / std::max( 1e-4f, slow );
}

}   // namespace

// ---------------------------------------------------------------------------------------------------- the school

void App::school()
{
    backdrop();
    float k = ui::k();
    header( T( "Fahrschule" ), T( "Drei Übungen auf dem Sperrgebiet. Die Ideallinie ist an, die Uhr läuft." ) );
    int                     &sel = m_sel[(int)ScreenE::SCHOOL];
    std::vector<std::string> items;
    for ( int e = 0; e < 3; ++e )
    {
        const TrackDef *td = g_data.track( Exercises[e].m_track );
        auto            best = m_rec.m_bestLap.find( Exercises[e].m_track );
        int             medal = 0;
        auto            mc = m_rec.m_count.find( std::string( "school_" ) + Exercises[e].m_track );
        if ( mc != m_rec.m_count.end() ) medal = std::clamp( mc->second, 0, 3 );
        std::string line = tf( "%d.  %s", e + 1, T( Exercises[e].m_title ) );
        if ( best != m_rec.m_bestLap.end() ) line += tf( T( "   ·   Bestzeit %.1f s" ), best->second );
        if ( medal > 0 ) line += std::string( "   ·   " ) + T( MedalNames[medal] );
        if ( !td ) line += "  (?)";
        items.push_back( line );
    }
    items.push_back( T( "Zurück" ) );
    int pick = menu( items, sel, { 96 * k, 220 * k, 1100 * k, 500 * k }, 38 );
    // The chosen exercise: its task and the medals' times.
    int e = std::clamp( sel, 0, 2 );
    if ( sel < 3 )
    {
        const TrackDef *td = g_data.track( Exercises[e].m_track );
        Rectangle       r = { 96 * k, 520 * k, 1100 * k, 0 };
        auto            lines = ui::wrap( T( Exercises[e].m_task ), r.width - 40 * k, 28 );
        r.height = lines.size() * ui::height( 28 ) + 110 * k;
        ui::panel( r, 0.75f );
        float y = r.y + 18 * k;
        for ( const std::string &l : lines )
        {
            ui::text( l, r.x + 20 * k, y, 28, ui::Ink );
            y += ui::height( 28 );
        }
        y += 14 * k;
        float x = r.x + 20 * k;
        for ( int m = 3; m >= 1 && td; --m )
        {
            DrawCircleV( { x + 12 * k, y + 16 * k }, 12 * k, MedalColors[m] );
            x += 32 * k;
            x += ui::text( tf( "%s %g s", T( MedalNames[m] ), td->m_medals[m - 1] ), x, y, 26, ui::Dim ) + 40 * k;
        }
    }
    hints( m_in.padUsed() ? T( "A starten   ·   B zurück" ) : T( "Eingabe starten   ·   Esc zurück" ) );
    if ( pick >= 0 && pick < 3 && g_data.track( Exercises[pick].m_track ) )
    {
        sound( SfxE::UI_OK );
        startSchool( pick );
        return;
    }
    if ( pick == 3 || m_in.back() )
    {
        sound( SfxE::UI_BACK );
        go( ScreenE::OPTIONS );
    }
}

void App::startSchool( int k )
{
    const Exercise &ex = Exercises[std::clamp( k, 0, 2 )];
    if ( !g_data.track( ex.m_track ) ) return;
    // The school's own car: a Rustbucket with a little tuning, no weapons. The career waits meanwhile.
    CarSetup s;
    s.m_car = "rustbucket";
    s.m_driver = T( "Fahrschüler" );
    s.m_human = true;
    s.m_player = true;
    s.m_engine = s.m_tires = s.m_armor = 2;
    s.m_paint.m_c[0] = { 230, 190, 40, 255 };
    s.m_paint.m_c[1] = { 30, 30, 34, 255 };
    s.m_paint.m_c[2] = { 230, 230, 220, 255 };
    if ( !m_schoolOn ) m_schoolCareer = m_inCareer;   // a restart from the pause keeps what was set aside
    m_inCareer = false;
    m_schoolOn = true;
    m_schoolEx = std::clamp( k, 0, 2 );
    m_schoolGate = 0;
    m_schoolMissed = m_schoolCones = 0;
    m_schoolTime = 0.0f;
    m_schoolMedal = 0;
    m_schoolBest = false;
    m_tour = 0;
    startRace( ex.m_track, false, { s }, ModeE::TRIAL, 1, 0 );
}

void App::schoolStep()
{
    if ( !m_schoolOn || m_world.m_phase != World::PhaseE::RACE ) return;
    const Car &me = m_world.m_cars[m_player];
    // The gates: passed when the car's distance reaches them; between the cones or not.
    while ( m_schoolGate < (int)m_track.m_gates.size() && me.m_dist >= m_track.m_gates[m_schoolGate].m_s )
    {
        const GateSpot &g = m_track.m_gates[m_schoolGate++];
        TrackPos        tp = m_track.locate( me.m_pos, me.m_hint );
        if ( std::fabs( tp.m_lat - g.m_lat ) > g.m_w * 0.5f )
        {
            ++m_schoolMissed;
            m_audio.play( SfxE::UI_DENY, 0, 0.6f );
            m_feed.push_back( { T( "Tor verpasst: +2 s" ), 2.5f } );
        }
        else
            m_audio.play( SfxE::UI_MOVE, 0, 0.5f, 1.3f );
    }
    for ( const Event &e : m_world.m_events )
        if ( e.m_type == EvE::LOOSE && e.m_car == m_player && e.m_text == "cone" )
        {
            ++m_schoolCones;
            m_feed.push_back( { T( "Hütchen umgefahren: +1 s" ), 2.5f } );
        }
    if ( m_feed.size() > 5 ) m_feed.erase( m_feed.begin(), m_feed.begin() + ( m_feed.size() - 5 ) );
}

void App::schoolHud()
{
    if ( !m_schoolOn ) return;
    float           k = ui::k();
    const Exercise &ex = Exercises[m_schoolEx];
    const TrackDef *td = g_data.track( ex.m_track );
    std::string     head = tf( T( "Fahrschule  ·  %s" ), T( ex.m_title ) );
    float           penalty = m_schoolMissed * 2.0f + m_schoolCones * 1.0f;
    std::string     line = m_track.m_gates.empty()
                               ? std::string( T( ex.m_task ) )
                               : tf( T( "Tore %d von %d  ·  verpasst %d  ·  Hütchen %d  ·  Strafzeit +%.0f s" ), m_schoolGate,
                                     (int)m_track.m_gates.size(), m_schoolMissed, m_schoolCones, penalty );
    std::string medals = td ? tf( T( "Gold %g s  ·  Silber %g s  ·  Bronze %g s" ), td->m_medals[2], td->m_medals[1], td->m_medals[0] ) : "";
    float       w = std::max( { ui::width( head, 28 ), ui::width( line, 24 ), ui::width( medals, 24 ) } ) + 50 * k;
    w = std::min( w, GetScreenWidth() - 760 * k );
    Rectangle r = { GetScreenWidth() * 0.5f - w * 0.5f, 20 * k, w, 124 * k };
    ui::panel( r, 0.75f );
    ui::text( head, r.x + 24 * k, r.y + 10 * k, 28, ui::Gold );
    ui::text( line, r.x + 24 * k, r.y + 50 * k, 24, ui::Ink );
    ui::text( medals, r.x + 24 * k, r.y + 86 * k, 24, ui::Dim );
}

void App::schoolFinish()
{
    const Car      &me = m_world.m_cars[m_player];
    const Exercise &ex = Exercises[m_schoolEx];
    const TrackDef *td = g_data.track( ex.m_track );
    m_schoolMedal = 0;
    m_schoolBest = false;
    m_schoolTime = 0.0f;
    if ( !me.m_finished ) return;
    // Gates not reached count as missed (a car that cut across the infield).
    m_schoolMissed += std::max( 0, (int)m_track.m_gates.size() - m_schoolGate );
    m_schoolGate = (int)m_track.m_gates.size();
    m_schoolTime = me.m_finishTime + m_schoolMissed * 2.0f + m_schoolCones * 1.0f;
    for ( int m = 3; m >= 1 && td; --m )
        if ( m_schoolTime <= td->m_medals[m - 1] )
        {
            m_schoolMedal = m;
            break;
        }
    // Records only at the full game speed (the school runs at it anyway).
    float &best = m_rec.m_bestLap[ex.m_track];
    if ( best <= 0 || m_schoolTime < best )
    {
        best = m_schoolTime;
        m_schoolBest = true;
    }
    int &medal = m_rec.m_count[std::string( "school_" ) + ex.m_track];
    medal = std::max( medal, m_schoolMedal );
    int golds = 0;
    for ( const Exercise &e : Exercises ) golds += m_rec.m_count[std::string( "school_" ) + e.m_track] >= 3 ? 1 : 0;
    if ( golds >= 3 ) award( "school_gold" );
}

void App::schoolResults()
{
    float           k = ui::k();
    const Exercise &ex = Exercises[m_schoolEx];
    const TrackDef *td = g_data.track( ex.m_track );
    const Car      &me = m_world.m_cars[m_player];
    header( me.m_finished ? tf( T( "Fahrschule: %s" ), T( ex.m_title ) ) : std::string( T( "Übung abgebrochen" ) ),
            me.m_finished ? T( "Die Uhr ist stehen geblieben." ) : T( "Ohne Ziel keine Zeit." ) );
    Rectangle r = { 96 * k, 210 * k, 900 * k, 470 * k };
    ui::panel( r );
    float y = r.y + 24 * k;
    auto  row = [&]( const std::string &label, const std::string &value, Color c ) {
        ui::text( label, r.x + 30 * k, y, 32, ui::Ink );
        ui::text( value, r.x + r.width - 30 * k, y, 32, c, ui::AlignE::RIGHT );
        y += 50 * k;
    };
    if ( me.m_finished )
    {
        row( T( "Fahrzeit" ), tf( "%.2f s", me.m_finishTime ), ui::Ink );
        if ( !m_track.m_gates.empty() )
        {
            row( tf( T( "Tore verpasst: %d" ), m_schoolMissed ), tf( "+%d s", m_schoolMissed * 2 ), m_schoolMissed ? ui::Warn : ui::Dim );
            row( tf( T( "Hütchen umgefahren: %d" ), m_schoolCones ), tf( "+%d s", m_schoolCones ), m_schoolCones ? ui::Warn : ui::Dim );
        }
        DrawRectangle( (int)( r.x + 30 * k ), (int)y, (int)( r.width - 60 * k ), 2, ui::Dim );
        y += 14 * k;
        row( T( "Gesamt" ), tf( "%.2f s", m_schoolTime ), ui::Gold );
        if ( m_schoolBest ) row( T( "Neue Bestzeit!" ), "", ui::Good );
    }
    // The medal.
    {
        Vector2 c = { r.x + r.width + 170 * k, r.y + 150 * k };
        int     m = std::clamp( m_schoolMedal, 0, 3 );
        if ( m > 0 )
        {
            DrawCircleV( c, 92 * k, { 20, 16, 14, 220 } );
            DrawCircleV( c, 80 * k, MedalColors[m] );
            DrawCircleLinesV( c, 64 * k, { 60, 40, 20, 160 } );
        }
        ui::text( T( MedalNames[m] ), c.x, c.y + 110 * k, 36, m > 0 ? MedalColors[m] : ui::Dim, ui::AlignE::CENTER );
        if ( td )
            for ( int q = 3; q >= 1; --q )
                ui::text( tf( "%s %g s", T( MedalNames[q] ), td->m_medals[q - 1] ), c.x, c.y + ( 160 + ( 3 - q ) * 34 ) * k, 24,
                          m >= q ? MedalColors[q] : ui::Dim, ui::AlignE::CENTER );
    }
    hints( m_in.padUsed() ? T( "A weiter" ) : T( "Eingabe weiter" ) );
    if ( m_in.accept() || m_in.back() || IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
    {
        sound( SfxE::UI_OK );
        m_schoolOn = false;
        m_inCareer = m_schoolCareer;
        m_audio.music( m_inCareer ? "garage" : "title" );
        go( ScreenE::SCHOOL );
    }
}

// ---------------------------------------------------------------------------------------------------- the benchmark

void App::startBenchmark()
{
    // The estate in its storm: rain, lightning, the stands, the most there is to draw. Six computer drivers.
    const TrackDef *def = g_data.track( "est_finale" );
    if ( !def )
        for ( const std::string &t : g_data.m_trackOrder )
            if ( !g_data.m_tracks.at( t ).m_arena && t.rfind( "test_", 0 ) != 0 ) def = &g_data.m_tracks.at( t );
    if ( !def ) return;
    std::vector<std::string> e, w;
    if ( !m_benchTrack.build( *def, g_data.venue( def->m_venue ), false, false, e, w ) ) return;
    std::vector<CarSetup> field;
    for ( int k = 0; k < 6 && k < (int)g_data.m_rivalOrder.size(); ++k )
    {
        const RivalDef &d = g_data.m_rivals.at( g_data.m_rivalOrder[(size_t)k * 3 % g_data.m_rivalOrder.size()] );
        CarSetup        s;
        s.m_car = g_data.car( d.m_car ) ? d.m_car : g_data.m_carOrder.front();
        s.m_driver = d.m_name;
        for ( int j = 0; j < 3; ++j ) s.m_paint.m_c[j] = d.m_colors[j];
        s.m_skill = d.m_skill;
        s.m_aggression = d.m_aggression;
        s.m_weapons = d.m_weapons;
        s.m_rear[(int)RearE::MINE] = 3;
        s.m_rockets = 2;
        s.m_engine = s.m_tires = s.m_armor = s.m_gunLv = 3;
        field.push_back( s );
    }
    if ( field.empty() ) return;
    m_benchWorld.init( m_benchTrack, field, ModeE::RACE, 2, 4242 );
    m_benchWorld.m_countdown = 0.6f;
    m_view.setTrack( m_benchTrack );
    m_view.m_miniature = true;
    m_benchCam.m_focus = m_benchWorld.m_cars[0].m_pos;
    m_benchCam.m_height = 64.0f;
    m_benchTime = 0.0f;
    m_benchPhase = 0;
    m_benchFrames[0].clear();
    m_benchFrames[1].clear();
    m_benchVerdict.clear();
    if ( !m_test ) SetTargetFPS( 0 );   // as fast as it goes: the headroom shows
    go( ScreenE::BENCH );
}

void App::bench()
{
    float k = ui::k();
    float dt = m_test ? 1.0f / 30.0f : std::min( GetFrameTime(), 0.1f );   // the app test: its own clock
    constexpr float Warm = 2.0f, Measure = 8.0f;
    if ( m_benchPhase < 2 )
    {
        m_benchTime += dt;
        static float acc = 0;
        acc += dt;
        std::vector<CarInput> none( m_benchWorld.m_cars.size() );
        for ( int steps = 0; acc >= Dt && steps < 4; ++steps )
        {
            acc -= Dt;
            m_benchWorld.step( none );
            m_view.events( m_benchWorld );
        }
        if ( acc > Dt ) acc = 0;
        m_view.update( m_benchWorld, dt );
        const Car &lead = m_benchWorld.m_cars[m_benchWorld.m_leader];
        Vector2    goal = add( lead.m_pos, mul( lead.m_vel, 0.6f ) );
        m_benchCam.m_focus = add( m_benchCam.m_focus, mul( sub( goal, m_benchCam.m_focus ), std::min( 1.0f, dt * 2.0f ) ) );
        m_benchCam.m_height = 64.0f;
        if ( m_benchTime > Warm ) m_benchFrames[m_benchPhase].push_back( GetFrameTime() );
        if ( m_benchTime > Warm + Measure )
        {
            ++m_benchPhase;
            m_benchTime = 0.0f;
            m_view.m_miniature = false;
        }
    }
    m_view.render( m_benchWorld, m_benchCam );
    const Texture2D &t = m_view.texture();
    DrawTexturePro( t, { 0, 0, (float)t.width, (float)-t.height }, { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() }, { 0, 0 }, 0, WHITE );
    float target = m_set.m_fps > 0 ? (float)m_set.m_fps : 60.0f;
    if ( m_benchPhase < 2 )
    {
        Rectangle r = { 40 * k, 40 * k, 760 * k, 120 * k };
        ui::panel( r, 0.7f );
        ui::text( m_benchPhase == 0 ? T( "Leistungstest  ·  mit Miniatur-Look" ) : T( "Leistungstest  ·  ohne Miniatur-Look" ), r.x + 20 * k, r.y + 12 * k, 30, ui::Gold );
        float share = clampf( ( m_benchPhase * ( Warm + Measure ) + m_benchTime ) / ( 2 * ( Warm + Measure ) ), 0, 1 );
        ui::bar( { r.x + 20 * k, r.y + 62 * k, r.width - 40 * k, 20 * k }, share, ui::Gold );
        ui::text( tf( T( "%.0f Bilder pro Sekunde" ), 1.0f / std::max( 1e-3f, GetFrameTime() ) ), r.x + 20 * k, r.y + 88 * k, 22, ui::Dim );
        if ( m_in.back() )
        {
            applySettings();
            m_atReady = false;
            go( ScreenE::OPTIONS );
        }
        return;
    }
    // The verdict: the look stays where the frame rate holds; without it, the look goes; neither, the picture's
    // quality one step down as well.
    float withAvg, withLow, withoutAvg, withoutLow;
    fpsOf( m_benchFrames[0], withAvg, withLow );
    fpsOf( m_benchFrames[1], withoutAvg, withoutLow );
    if ( m_benchVerdict.empty() )
    {
        bool withHolds = withAvg >= target * 0.97f && withLow >= target * 0.8f;
        bool withoutHolds = withoutAvg >= target * 0.97f && withoutLow >= target * 0.8f;
        if ( withHolds )
        {
            m_set.m_lookDropped = false;
            if ( m_set.m_miniature == 0 ) m_set.m_miniature = 2;
            m_benchVerdict = T( "Die Bildrate hält auch mit dem Miniatur-Look. Er bleibt an." );
        }
        else if ( withoutHolds )
        {
            m_set.m_miniature = 2;
            m_set.m_lookDropped = true;
            m_benchVerdict = T( "Ohne den Miniatur-Look hält die Bildrate. Er ist jetzt aus (Einstellung: automatisch)." );
        }
        else
        {
            m_set.m_miniature = 2;
            m_set.m_lookDropped = true;
            if ( quality() > 0 )
            {
                m_set.m_quality = quality() - 1;
                m_benchVerdict = T( "Auch ohne den Miniatur-Look zu langsam: Er ist aus, und die Grafik steht eine Stufe tiefer." );
            }
            else
                m_benchVerdict = T( "Auch ohne den Miniatur-Look zu langsam. Er ist aus; die Grafik steht schon auf Schnell." );
        }
        saveSettings();
    }
    Rectangle r = { GetScreenWidth() * 0.5f - 520 * k, GetScreenHeight() * 0.5f - 200 * k, 1040 * k, 400 * k };
    ui::panel( r, 0.85f );
    ui::text( T( "Leistungstest" ), r.x + 30 * k, r.y + 20 * k, 40, ui::Gold );
    ui::text( tf( T( "Mit Miniatur-Look:  %.0f Bilder pro Sekunde, die langsamsten 5 %%: %.0f" ), withAvg, withLow ), r.x + 30 * k,
              r.y + 90 * k, 28, ui::Ink );
    ui::text( tf( T( "Ohne Miniatur-Look:  %.0f Bilder pro Sekunde, die langsamsten 5 %%: %.0f" ), withoutAvg, withoutLow ),
              r.x + 30 * k, r.y + 134 * k, 28, ui::Ink );
    ui::text( tf( T( "Ziel: %.0f Bilder pro Sekunde" ), target ), r.x + 30 * k, r.y + 178 * k, 26, ui::Dim );
    float y = r.y + 230 * k;
    for ( const std::string &l : ui::wrap( m_benchVerdict, r.width - 60 * k, 28 ) )
    {
        ui::text( l, r.x + 30 * k, y, 28, ui::Good );
        y += ui::height( 28 );
    }
    hints( m_in.padUsed() ? T( "A weiter" ) : T( "Eingabe weiter" ) );
    if ( m_in.accept() || m_in.back() || IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
    {
        sound( SfxE::UI_OK );
        applySettings();   // the frame limit, the quality, the look
        m_atReady = false;
        go( ScreenE::OPTIONS );
    }
}

void App::watchFrameRate( float dt )
{
    // The automatic miniature look (Settings::m_miniature 2): windows of five seconds; when in two of them in a row a
    // quarter of the frames came late, the look goes for good (the options bring it back).
    if ( m_set.m_miniature != 2 || m_set.m_lookDropped || m_test || m_world.m_time < 6.0f || m_player2 >= 0 ) return;
    float target = m_set.m_fps > 0 ? (float)m_set.m_fps : 60.0f;
    m_frameWatch += dt;
    m_frameCount += 1.0f;
    if ( dt > 1.3f / target ) m_slowFrames += 1.0f;
    if ( m_frameWatch < 5.0f ) return;
    bool slow = m_slowFrames > m_frameCount * 0.25f;
    m_slowWindows = slow ? m_slowWindows + 1 : 0;
    m_frameWatch = m_frameCount = m_slowFrames = 0.0f;
    if ( m_slowWindows < 2 ) return;
    m_set.m_lookDropped = true;
    m_view.m_miniature = false;
    saveSettings();
    m_feed.push_back( { T( "Miniatur-Look aus: die Bildrate hielt nicht" ), 5.0f } );
    if ( m_feed.size() > 5 ) m_feed.erase( m_feed.begin() );
}

}   // namespace grave
