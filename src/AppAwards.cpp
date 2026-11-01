/**
 * @file AppAwards.cpp
 * @brief The epitaphs (data/awards.txt, docs/DESIGN.md "Epitaphs"): what the races, the career and the shops earn,
 *        the toasts that announce them, the crypt where they stand as gravestones; and the test drive at the
 *        undertaker's (docs/DESIGN.md "Test drive"): eight steps on Graves & Sons' yard, Ricky Rattle explaining.
 * @author Rene Weller
 */
#include "App.h"

#include "Files.h"
#include "Lang.h"
#include "Ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace grave
{

// ---------------------------------------------------------------------------------------------------- the epitaphs

void App::award( const std::string &key )
{
    if ( m_rec.m_awards.count( key ) || m_test ) return;
    const AwardDef *a = g_data.award( key );
    if ( !a ) return;
    m_rec.m_awards.insert( key );
    m_rec.save( userDir() + "records.txt" );
    m_toasts.push_back( { a->m_title.get(), 5.0f } );
    m_audio.play( SfxE::UI_STAMP, 0, 0.7f );
}

int App::countUp( const std::string &key, int n )
{
    return m_rec.m_count[key] += n;
}

void App::awardsEvents()
{
    const Car &me = m_world.m_cars[m_player];
    for ( const Event &e : m_world.m_events )
    {
        bool mine = e.m_car == m_player;
        switch ( e.m_type )
        {
        case EvE::WRECK:
            if ( e.m_other == m_player && !mine )
            {
                award( "wreck_first" );
                if ( countUp( "wrecks" ) >= 100 ) award( "wreck_100" );
            }
            if ( mine )
            {
                int n = countUp( "wrecked" );
                if ( n >= 10 ) award( "wrecked_10" );
                if ( n >= 50 ) award( "wrecked_50" );
            }
            break;
        case EvE::LOOSE:
            if ( mine )
            {
                int n = countUp( "knocked" );
                if ( n >= 50 ) award( "knock_50" );
                if ( n >= 500 ) award( "knock_500" );
                if ( e.m_text == "gnome" ) award( "knock_gnome" );
                else if ( e.m_text == "snowman" ) award( "knock_snowman" );
                else if ( e.m_text == "hydrant" ) award( "knock_hydrant" );
                else if ( e.m_text == "wood_cross" ) award( "knock_cross" );
            }
            break;
        case EvE::PICKUP:
            if ( mine && e.m_text == "curse" ) award( "curse" );
            if ( mine && e.m_text.rfind( "cash", 0 ) == 0 && countUp( "cash", e.m_text == "cash3" ? 3 : 1 ) >= 100 ) award( "cash_100" );
            break;
        case EvE::HORN:
            if ( mine && countUp( "horn" ) >= 50 ) award( "horn_50" );
            break;
        case EvE::NITRO:
            if ( mine && countUp( "nitro" ) >= 50 ) award( "nitro_50" );
            break;
        case EvE::MINE_DROP:
            if ( mine && countUp( "mines" ) >= 100 ) award( "mines_100" );
            break;
        case EvE::ROCKET:
            if ( mine && countUp( "rockets" ) >= 50 ) award( "rockets_50" );
            break;
        case EvE::WRONG_WAY:
            if ( mine && !m_tutorialOn ) award( "wrong_way" );
            break;
        case EvE::WEATHER: award( "weather" ); break;
        case EvE::OIL:
            if ( mine && e.m_kind == 1 ) m_slipped = true;
            break;
        default: break;
        }
    }
    if ( me.m_moverHit > m_moverSeen + 0.5f )
    {
        m_moverSeen = me.m_moverHit;
        award( "train_hit" );
    }
    // The worst place once the first lap is done (a comeback).
    if ( m_mode == ModeE::RACE && me.m_lap >= 1 && !me.m_finished && !me.m_wrecked ) m_worstPlace = std::max( m_worstPlace, me.m_place );
}

void App::awardsRace()
{
    const Car &me = m_world.m_cars[m_player];
    if ( m_tutorialOn ) return;
    if ( m_track.m_venue ) m_rec.m_seen.insert( "v:" + m_track.m_venue->m_name );
    if ( m_trackName.rfind( "rnd_", 0 ) != 0 ) m_rec.m_seen.insert( "t:" + m_trackName );
    int venues = 0, tracks = 0;
    for ( const std::string &s : m_rec.m_seen )
    {
        venues += s.rfind( "v:", 0 ) == 0 ? 1 : 0;
        tracks += s.rfind( "t:", 0 ) == 0 ? 1 : 0;
    }
    if ( venues >= 11 ) award( "all_venues" );
    if ( tracks >= 30 ) award( "tracks_30" );
    if ( me.m_wrecks >= 3 ) award( "wreck_3" );
    if ( m_world.m_bestLapCar == m_player && m_mode == ModeE::RACE ) award( "lap_record" );
    if ( me.m_finished && !me.m_wrecked && me.m_hp < me.m_maxHp * 0.05f ) award( "close_call" );
    if ( !m_inCareer && m_qMode == 4 && me.m_finished ) award( "daily" );
    bool won = me.m_place == 1 && !me.m_wrecked && m_mode != ModeE::TRIAL && m_world.m_cars.size() > 1 && me.m_dist > -1e8f;
    if ( !won ) return;
    int n = countUp( "wins" );
    award( "first_win" );
    if ( n >= 25 ) award( "wins_25" );
    if ( n >= 100 ) award( "wins_100" );
    if ( m_mode == ModeE::DERBY ) award( "derby_win" );
    if ( me.m_shots == 0 ) award( "no_gun_win" );
    if ( me.m_damage < me.m_maxHp * 0.05f ) award( "clean_win" );
    if ( m_slipped ) award( "oil_win" );
    if ( m_mode == ModeE::RACE && m_world.m_cars.size() >= 4 && m_worstPlace >= (int)m_world.m_cars.size() ) award( "comeback" );
    if ( m_mode == ModeE::RACE && m_world.m_finishOrder.size() >= 2 &&
         m_world.m_cars[m_world.m_finishOrder[1]].m_finishTime - me.m_finishTime < 0.3f )
        award( "photo_finish" );
    if ( m_inCareer && !m_set.m_assist ) award( "old_school" );
}

void App::awardsCareer()
{
    const RaceReport &r = m_report;
    if ( r.m_promoted )
    {
        int l = m_career.m_league;
        award( l == 4 ? "league_4" : l == 3 ? "league_3" : l == 2 ? "league_2" : l == 1 ? "league_1" : "final" );
    }
    if ( r.m_won )
    {
        award( "career_won" );
        if ( m_career.m_difficulty >= 3 ) award( "career_won_hard" );
    }
    if ( r.m_jobDone )
    {
        award( "job_first" );
        if ( m_career.m_jobsDone >= 10 ) award( "jobs_10" );
    }
    if ( r.m_job == JobE::COFFIN && !r.m_jobDone ) award( "coffin_lost" );
    int graves = 0;
    for ( const RivalState &s : m_career.m_rivals )
    {
        if ( !s.m_alive && s.m_died > 0 ) ++graves;
        if ( !s.m_alive && s.m_killer == "_player" ) award( "killer" );
        if ( s.m_grudge >= 2 ) award( "feud" );
    }
    if ( graves >= 10 ) award( "graves_10" );
    // What the tabloid reports (the old items again do no harm: an epitaph is earned once).
    for ( const NewsItem &n : m_career.m_news )
    {
        if ( n.m_event == "feud_end" ) award( "feud_end" );
        else if ( n.m_event == "sabotage_caught" ) award( "caught" );
        else if ( n.m_event == "loan_overdue" ) award( "collectors" );
    }
    if ( m_career.m_money >= 1000000 ) award( "millionaire" );
    if ( m_career.m_over && !m_career.m_won ) award( "bankrupt" );
}

void App::toasts()
{
    float k = ui::k(), y = 120 * k;
    for ( Toast &t : m_toasts )
    {
        t.m_time -= GetFrameTime();
        float       a = clampf( std::min( t.m_time, 5.0f - t.m_time ) * 3.0f, 0, 1 );
        std::string head = T( "Neue Grabinschrift" );
        float       w = std::max( ui::width( head, 22 ), ui::width( "\xE2\x80\xA0 " + t.m_text, 32 ) ) + 50 * k;
        Rectangle   r = { GetScreenWidth() * 0.5f - w * 0.5f, y, w, 86 * k };
        DrawRectangleRec( r, { 18, 16, 18, (unsigned char)( 225 * a ) } );
        DrawRectangleLinesEx( r, 2 * k, { 150, 146, 140, (unsigned char)( 255 * a ) } );
        ui::text( head, r.x + w * 0.5f, r.y + 8 * k, 22, { ui::Dim.r, ui::Dim.g, ui::Dim.b, (unsigned char)( 255 * a ) }, ui::AlignE::CENTER );
        ui::text( "\xE2\x80\xA0 " + t.m_text, r.x + w * 0.5f, r.y + 38 * k, 32, { ui::Gold.r, ui::Gold.g, ui::Gold.b, (unsigned char)( 255 * a ) },
                  ui::AlignE::CENTER );
        y += 98 * k;
    }
    m_toasts.erase( std::remove_if( m_toasts.begin(), m_toasts.end(), []( const Toast &t ) { return t.m_time <= 0; } ), m_toasts.end() );
}

// ---------------------------------------------------------------------------------------------------- the crypt

void App::crypt()
{
    backdrop();
    float k = ui::k();
    int   n = (int)g_data.m_awardOrder.size(), earned = 0;
    for ( const std::string &key : g_data.m_awardOrder ) earned += m_rec.m_awards.count( key ) ? 1 : 0;
    header( T( "Die Gruft" ), tf( T( "%d von %d Grabinschriften" ), earned, n ) );
    if ( n == 0 )
    {
        if ( m_in.back() || m_in.accept() ) go( m_inCareer ? ScreenE::HUB : ScreenE::TITLE );
        return;
    }
    const int cols = 8;
    int       rows = ( n + cols - 1 ) / cols;
    int      &sel = m_sel[(int)ScreenE::CRYPT];
    sel = std::clamp( sel, 0, n - 1 );
    if ( m_in.left() ) sel = std::max( 0, sel - 1 );
    if ( m_in.right() ) sel = std::min( n - 1, sel + 1 );
    if ( m_in.up() ) sel = std::max( 0, sel - cols );
    if ( m_in.down() ) sel = std::min( n - 1, sel + cols );
    int   first = std::clamp( sel / cols - 1, 0, std::max( 0, rows - 3 ) );
    float w = 200 * k, h = 200 * k, gap = 18 * k;
    float x0 = GetScreenWidth() * 0.5f - ( cols * w + ( cols - 1 ) * gap ) * 0.5f, y0 = 200 * k;
    for ( int i = first * cols; i < std::min( n, ( first + 3 ) * cols ); ++i )
    {
        const std::string &key = g_data.m_awardOrder[(size_t)i];
        const AwardDef    &a = g_data.m_awards.at( key );
        bool               has = m_rec.m_awards.count( key ) > 0;
        int                c = i % cols, r = i / cols - first;
        Rectangle          s = { x0 + c * ( w + gap ), y0 + r * ( h + gap ), w, h };
        Color              stone = has ? Color{ 150, 146, 140, 255 } : Color{ 54, 52, 56, 255 };
        // A headstone: a rounded top, a plinth.
        DrawRectangleRec( { s.x, s.y + s.width * 0.45f, s.width, s.height - s.width * 0.45f - 14 * k }, stone );
        DrawEllipse( (int)( s.x + s.width * 0.5f ), (int)( s.y + s.width * 0.45f ), s.width * 0.5f, s.width * 0.45f, stone );
        DrawRectangleRec( { s.x - 8 * k, s.y + s.height - 16 * k, s.width + 16 * k, 16 * k }, { 44, 38, 32, 255 } );
        if ( i == sel ) DrawRectangleLinesEx( { s.x - 6 * k, s.y - 6 * k, s.width + 12 * k, s.height + 12 * k }, 3 * k, ui::Accent );
        Color ink = has ? Color{ 36, 32, 32, 255 } : Color{ 100, 96, 100, 255 };
        ui::text( has ? "\xE2\x80\xA0" : "?", s.x + s.width * 0.5f, s.y + 24 * k, 44, ink, ui::AlignE::CENTER );
        if ( has )
        {
            float ty = s.y + 88 * k;
            for ( const std::string &l : ui::wrap( a.m_title.get(), s.width - 24 * k, 20 ) )
            {
                if ( ty > s.y + s.height - 40 * k ) break;
                ui::text( l, s.x + s.width * 0.5f, ty, 20, ink, ui::AlignE::CENTER );
                ty += 24 * k;
            }
        }
    }
    // The chosen stone's inscription and what earns it.
    const std::string &key = g_data.m_awardOrder[(size_t)sel];
    const AwardDef    &a = g_data.m_awards.at( key );
    bool               has = m_rec.m_awards.count( key ) > 0;
    Rectangle          box = { 96 * k, y0 + 3 * ( h + gap ) + 10 * k, GetScreenWidth() - 192 * k, 120 * k };
    ui::panel( box, 0.75f );
    ui::text( has ? a.m_title.get() : std::string( T( "Noch nicht verdient" ) ), box.x + 24 * k, box.y + 16 * k, 34, has ? ui::Gold : ui::Dim );
    ui::text( a.m_desc.get(), box.x + 24 * k, box.y + 64 * k, 28, ui::Ink );
    hints( m_in.padUsed() ? T( "Steuerkreuz wählen   ·   B zurück" ) : T( "Pfeile wählen   ·   Esc zurück" ) );
    if ( m_in.back() || m_in.accept() ) go( m_inCareer ? ScreenE::HUB : ScreenE::TITLE );
}

// ---------------------------------------------------------------------------------------------------- the test drive

void App::startTutorial()
{
    // Graves & Sons' yard, alone against the clock, a car with a little of everything.
    CarSetup s = m_inCareer ? m_career.playerSetup() : CarSetup();
    if ( !m_inCareer )
    {
        s.m_car = "rustbucket";
        s.m_driver = T( "Fahrschüler" );
        s.m_human = true;
        s.m_player = true;
        s.m_paint.m_c[0] = { 170, 60, 40, 255 };
    }
    s.m_hpShare = 1.0f;
    s.m_rear[(int)RearE::MINE] = std::max( s.m_rear[(int)RearE::MINE], 3 );
    s.m_rockets = std::max( s.m_rockets, 3 );
    s.m_nitro = std::max( s.m_nitro, 2 );
    m_tutorialOn = true;
    m_tutStep = 0;
    m_tutSaid = -1;
    m_tutCount = 0;
    m_tutTime = 0;
    m_tutFrom = m_screen;
    startRace( "test_undertaker", false, { s }, ModeE::TRIAL, 12, 0 );
}

void App::tutorial()
{
    const Car      &me = m_world.m_cars[m_player];
    const CarInput &in = me.m_in;
    if ( m_tutStep >= 8 || m_world.m_phase != World::PhaseE::RACE ) return;
    if ( m_tutSaid != m_tutStep && m_world.m_time > 0.8f )
    {
        m_tutSaid = m_tutStep;
        speak( "tut_" + std::to_string( m_tutStep + 1 ) );
    }
    bool done = false;
    auto happened = [&]( EvE type ) {
        for ( const Event &e : m_world.m_events )
            if ( e.m_type == type && e.m_car == m_player ) return true;
        return false;
    };
    switch ( m_tutStep )
    {
    case 0: done = me.m_dist > 150.0f; break;
    case 1:
        m_tutTime = in.m_brake > 0.5f && me.speed() > 8.0f ? m_tutTime + Dt : 0.0f;
        done = m_tutTime > 0.4f;
        break;
    case 2: done = in.m_handbrake && std::fabs( me.m_yaw ) > 0.9f && len( me.m_vel ) > 7.0f; break;
    case 3:
        for ( const Event &e : m_world.m_events )
            if ( e.m_type == EvE::BARREL && e.m_car == m_player ) ++m_tutCount;
        done = m_tutCount >= 3;
        break;
    case 4: done = happened( EvE::MINE_DROP ); break;
    case 5: done = happened( EvE::NITRO ); break;
    case 6: done = happened( EvE::ROCKET ); break;
    case 7: done = happened( EvE::LAP ) || happened( EvE::FINISH ); break;
    default: break;
    }
    if ( !done ) return;
    ++m_tutStep;
    m_tutCount = 0;
    m_tutTime = 0;
    sound( SfxE::UI_OK );
    if ( m_tutStep >= 8 )
    {
        // Passed: the licence, and the race ends.
        m_rec.m_tutorial = true;
        award( "licence" );
        m_rec.save( userDir() + "records.txt" );
        m_center = T( "BESTANDEN" );
        m_centerTime = 3.5f;
        speak( "tut_done" );
        m_world.m_phase = World::PhaseE::DONE;
        m_world.m_doneTime = 0;
    }
}

void App::tutorialHud()
{
    if ( !m_tutorialOn ) return;
    float k = ui::k();
    static const char *titles[] = { "Gas geben und lenken", "Bremsen", "Handbremse", "Schießen", "Minen", "Lachgas", "Rakete", "Ins Ziel" };
    static const char *texts[] = { "Gib Gas und folge der Straße. Mit der Fahrhilfe bleibt der Wagen in der Spur.",
                                   "Vor der Kehre: Bremse kräftig, bis der Wagen langsam genug ist.",
                                   "Zieh in der Kehre die Handbremse, dann kommt das Heck herum.",
                                   "Schieß drei Fässer am Straßenrand in die Luft.",
                                   "Leg eine Mine hinter dir ab. Und fahr nicht selbst drüber.",
                                   "Zünde auf der Geraden das Lachgas.",
                                   "Feuere eine Rakete ab. Sie sucht sich ihr Ziel selbst.",
                                   "Fahr über die Ziellinie. Graves wartet mit dem Führerschein." };
    static const ActE keys[] = { ActE::ACCEL, ActE::BRAKE, ActE::HANDBRAKE, ActE::FIRE, ActE::DROP, ActE::NITRO, ActE::ROCKET, ActE::ACCEL };
    int         s = std::min( m_tutStep, 7 );
    std::string head = tf( T( "Probefahrt  ·  Schritt %d von 8: %s" ), s + 1, T( titles[s] ) );
    std::string line = m_tutStep >= 8 ? std::string( T( "Bestanden! Willkommen im Grave Prix." ) ) : std::string( T( texts[s] ) );
    std::string key = m_tutStep >= 8 ? "" : std::string( T( "Taste: " ) ) + m_in.label( keys[s], m_in.padUsed() ) +
                                                  ( s == 0 ? "  ·  " + m_in.label( ActE::LEFT, m_in.padUsed() ) + " / " + m_in.label( ActE::RIGHT, m_in.padUsed() ) : "" );
    float     w = std::max( { ui::width( head, 28 ), ui::width( line, 26 ), ui::width( key, 24 ) } ) + 50 * k;
    Rectangle r = { GetScreenWidth() * 0.5f - w * 0.5f, 20 * k, w, 132 * k };
    ui::panel( r, 0.75f );
    ui::text( head, r.x + 24 * k, r.y + 10 * k, 28, ui::Gold );
    ui::text( line, r.x + 24 * k, r.y + 50 * k, 26, m_tutStep >= 8 ? ui::Good : ui::Ink );
    ui::text( key, r.x + 24 * k, r.y + 90 * k, 24, ui::Dim );
}

}   // namespace grave
