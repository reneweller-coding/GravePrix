/**
 * @file AppRace.cpp
 * @brief The race on screen: the steps, the camera, the sound of the engines, loops and effects, the rumble, the
 *        rivals' shouts and the announcer, the HUD with its minimap, the pause, and the results.
 * @author Rene Weller
 */
#include "App.h"

#include "Files.h"
#include "Lang.h"
#include "Rumble.h"
#include "Ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace grave
{

namespace
{

std::string clock( float t )
{
    if ( t <= 0 ) return "-:--.-";
    int m = (int)( t / 60 );
    return TextFormat( "%d:%04.1f", m, t - m * 60 );
}

/** @return a line of the announcer for an event ({name} filled in) */
std::string announcerLine( const std::string &event, const std::string &name, uint32_t pick )
{
    const CharacterDef *c = g_data.character( "announcer" );
    if ( !c ) return "";
    std::string s = c->line( event, pick );
    for ( size_t p = s.find( "{name}" ); p != std::string::npos; p = s.find( "{name}" ) ) s.replace( p, 6, name );
    return s;
}

/** @return a colour with its alpha times a */
Color faded( Color c, float a ) { return { c.r, c.g, c.b, (unsigned char)( c.a * clampf( a, 0, 1 ) ) }; }

/**
 * @brief Draws a bend as an arrow seen from above, north up like the picture: a stem the way the road comes in, an arc
 *        through the bend's turn, a head the way it goes out; fitted into a box.
 * @param[in] box    where
 * @param[in] head   the way in (radians, 0 up the screen, clockwise)
 * @param[in] turn   how far it turns (radians, > 0 clockwise)
 * @param[in] c      its colour
 * @param[in] thick  the line's width (pixels)
 */
void drawBend( Rectangle box, float head, float turn, Color c, float thick )
{
    turn = clampf( turn, -3.5f, 3.5f );
    std::vector<Vector2> pts = { { 0, 0 } };
    Vector2              p = headingDir( head );
    pts.push_back( p );
    int n = std::max( 3, (int)( std::fabs( turn ) / 0.15f ) );
    for ( int i = 0; i < n; ++i )
    {
        p = add( p, mul( headingDir( head + turn * ( i + 0.5f ) / n ), std::fabs( turn ) * 0.7f / n ) );
        pts.push_back( p );
    }
    float   out = head + turn;
    Vector2 f = headingDir( out ), r = headingRight( out );
    p = add( p, mul( f, 0.45f ) );
    pts.push_back( p );
    Vector2 tip = add( p, mul( f, 0.55f ) ), wing1 = add( p, mul( r, 0.42f ) ), wing2 = sub( p, mul( r, 0.42f ) );
    // Fitted into the box, the same scale both ways.
    Vector2 lo = tip, hi = tip;
    for ( Vector2 q : pts )
        for ( Vector2 e : { q, wing1, wing2 } )
        {
            lo = { std::min( lo.x, e.x ), std::min( lo.y, e.y ) };
            hi = { std::max( hi.x, e.x ), std::max( hi.y, e.y ) };
        }
    float   sc = std::min( ( box.width - thick * 2 ) / std::max( 0.5f, hi.x - lo.x ),
                           ( box.height - thick * 2 ) / std::max( 0.5f, hi.y - lo.y ) );
    Vector2 mid = { box.x + box.width * 0.5f, box.y + box.height * 0.5f }, c0 = mul( add( lo, hi ), 0.5f );
    auto    P = [&]( Vector2 q ) { return add( mid, mul( sub( q, c0 ), sc ) ); };
    auto    tri = [&]( Vector2 a, Vector2 b, Vector2 d, Color col ) {
        // raylib wants the corners counter-clockwise on the screen.
        if ( cross( sub( b, a ), sub( d, a ) ) > 0 ) std::swap( b, d );
        DrawTriangle( a, b, d, col );
    };
    // A dark edge first, so it reads on any ground; then the colour.
    for ( int pass = 0; pass < 2; ++pass )
    {
        float w = pass == 0 ? thick + 4.0f : thick;
        Color col = pass == 0 ? Color{ 8, 6, 8, c.a } : c;
        for ( size_t i = 0; i + 1 < pts.size(); ++i )
        {
            DrawLineEx( P( pts[i] ), P( pts[i + 1] ), w, col );
            DrawCircleV( P( pts[i + 1] ), w * 0.5f, col );
        }
        Vector2 grow = pass == 0 ? mul( f, 2.5f / sc ) : Vector2{ 0, 0 };
        Vector2 side = pass == 0 ? mul( r, 2.5f / sc ) : Vector2{ 0, 0 };
        tri( P( add( tip, grow ) ), P( add( wing1, side ) ), P( sub( wing2, side ) ), col );
    }
}

}   // namespace

// ---------------------------------------------------------------------------------------------------- start

void App::startRace( const std::string &track, bool reverse, const std::vector<CarSetup> &field, ModeE mode, int laps, int player )
{
    const TrackDef *def = g_data.track( track );
    if ( !def ) return;
    std::vector<std::string> e, w;
    if ( !m_track.build( *def, g_data.venue( def->m_venue ), reverse, false, e, w ) ) return;
    if ( mode != ModeE::DERBY ) m_track.m_laps = std::max( 1, laps );
    m_trackName = track;
    m_saidTrain = m_saidBridge = false;
    m_trackReverse = reverse;
    m_field = field;
    for ( CarSetup &s : m_field )
        if ( s.m_human ) s.m_assist = m_set.m_assist;
    // Colour vision: the rivals in the Okabe-Ito colours, which stay apart for red-green and blue-yellow eyes.
    if ( m_set.m_palette != 0 )
    {
        static const Color safe[] = { { 230, 159, 0, 255 },  { 86, 180, 233, 255 }, { 0, 158, 115, 255 }, { 240, 228, 66, 255 },
                                      { 0, 114, 178, 255 },  { 213, 94, 0, 255 },   { 204, 121, 167, 255 }, { 245, 245, 245, 255 } };
        int k = 0;
        for ( size_t i = 0; i < m_field.size(); ++i )
            if ( (int)i != player )
            {
                m_field[i].m_paint.m_c[0] = safe[k % 8];
                m_field[i].m_paint.m_c[1] = { 20, 20, 24, 255 };
                ++k;
            }
    }
    m_mode = mode;
    m_player = player;
    int difficulty = m_inCareer ? m_career.m_difficulty : m_qDiff;
    m_world.init( m_track, m_field, mode, difficulty, (uint32_t)( GetTime() * 1000 ) + 13 );
    m_hot = false;
    m_closeTime = 0.0f;
    m_lastQuip = -10.0f;
    m_view.setTrack( m_track );
    m_racing = true;
    m_paused = false;
    m_acc = 0;
    m_endTimer = 0;
    m_shake = 0;
    m_bubbles.clear();
    m_feed.clear();
    m_announce.clear();
    m_center.clear();
    m_lastBest = 0;
    m_worstPlace = 0;
    m_slipped = false;
    m_moverSeen = -100.0f;
    // The sponsor's sticker on the player's roof.
    {
        const SponsorDef *sp = m_inCareer ? g_data.sponsor( m_career.m_sponsor ) : nullptr;
        static const Color backs[] = { { 240, 236, 226, 255 }, { 240, 200, 40, 255 }, { 24, 22, 24, 255 }, { 170, 30, 26, 255 }, { 30, 90, 60, 255 } };
        static const Color inks[] = { { 20, 20, 20, 255 }, { 20, 20, 20, 255 }, { 220, 180, 80, 255 }, { 245, 240, 230, 255 }, { 240, 236, 226, 255 } };
        size_t h = sp ? std::hash<std::string>()( sp->m_key ) % 5 : 0;
        m_view.setSticker( sp ? sp->m_title.get() : "", backs[h], inks[h], player );
        m_view.setSlogan( T( "Graves & Sons – Ihr letzter Boxenstopp" ) );   // the stands' banners
    }
    // The dead rivals haunt the cemetery's tracks and the final: the last two to die, along the racing line.
    m_spiritList.clear();
    m_view.m_spirits.clear();
    m_spiritSeen = false;
    if ( m_inCareer && !m_tutorialOn && m_track.m_venue && ( m_track.m_venue->m_name == "cemetery" || track == "est_finale" ) )
    {
        std::vector<const RivalState *> dead;
        for ( const RivalState &s : m_career.m_rivals )
            if ( !s.m_alive && s.m_died > 0 && g_data.rival( s.m_key ) ) dead.push_back( &s );
        std::sort( dead.begin(), dead.end(), []( const RivalState *a, const RivalState *b ) { return a->m_died > b->m_died; } );
        for ( size_t k = 0; k < dead.size() && k < 2; ++k )
        {
            Spirit sp;
            sp.m_rival = dead[k]->m_key;
            sp.m_s = m_track.wrap( -25.0f - 14.0f * k );
            sp.m_wait = 5.0f + 8.0f * k;
            sp.m_lane = k ? 2.0f : -2.0f;
            m_spiritList.push_back( sp );
            const RivalDef  *rd = g_data.rival( sp.m_rival );
            const CarDef    *cd = g_data.car( rd->m_car );
            Renderer::Ghost  gh;
            gh.m_model = cd ? cd->m_model : "?";
            for ( int c = 0; c < 3; ++c ) gh.m_paint.m_c[c] = rd->m_colors[c];
            m_view.m_spirits.push_back( gh );
        }
        // Their pace: what the racing line's bends allow, braking ahead of them.
        int n = (int)m_track.m_s.size();
        m_spiritV.assign( (size_t)n, 30.0f );
        for ( int i = 0; i < n; ++i ) m_spiritV[i] = std::min( 31.0f, std::sqrt( 15.0f * 0.85f / std::max( m_track.m_s[i].m_lineCurv, 1e-4f ) ) );
        for ( int pass = 0; pass < 2; ++pass )
            for ( int i = n - 1; i >= 0; --i )
            {
                int j = ( i + 1 ) % n;
                m_spiritV[i] = std::min( m_spiritV[i], std::sqrt( m_spiritV[j] * m_spiritV[j] + 2.0f * 9.0f * m_track.m_step ) );
            }
    }
    // Graves' job: the debtor or the hearse found, the coffin strapped on, the wreaths laid out.
    m_jobState = 0;
    m_jobCar = -1;
    m_view.m_roofCoffin = -1;
    if ( m_inCareer && !m_tutorialOn && m_offer.m_job != JobE::NONE )
    {
        for ( size_t i = 0; i < m_world.m_cars.size(); ++i )
        {
            const CarSetup &s = m_world.m_cars[i].m_setup;
            if ( ( m_offer.m_job == JobE::DEBTOR && s.m_rival == m_offer.m_target ) || ( m_offer.m_job == JobE::ESCORT && s.m_escort ) )
                m_jobCar = (int)i;
        }
        if ( m_offer.m_job == JobE::COFFIN ) m_view.m_roofCoffin = player;
        if ( m_offer.m_job == JobE::WREATHS )
            for ( int k = 0; k < 9; ++k )
            {
                float              s = m_track.m_length * ( k + 0.5f ) / 9.0f;
                const TrackSample &ts = m_track.sampleAt( s );
                m_world.m_pickups.push_back( { "wreath", m_track.pointAt( s, ts.m_line + ( k % 3 - 1 ) * ts.m_half * 0.25f ), 0.0f, (float)k } );
            }
        if ( const CharacterDef *g = g_data.character( "graves" ) )
            bubble( g->m_name, g->line( std::string( "job_" ) + jobKey( m_offer.m_job ), (uint32_t)GetRandomValue( 0, 999 ) ), { 30, 30, 34, 255 } );
    }
    m_ghostRec.clear();
    m_ghost.clear();
    m_ghostAt = 0;
    m_view.m_ghost.m_on = false;
    if ( mode == ModeE::TRIAL && !m_tutorialOn ) loadGhost();
    const Car &me = m_world.m_cars[player];
    m_cam.m_focus = m_camCar = m_camAnchor = me.m_pos;
    m_camLead = { 0, 0 };
    m_cam.m_height = 64.0f * m_set.m_zoom;
    findBends();
    m_hudLow = false;
    m_hudFade = 1.0f;
    m_stuckTime = 0.0f;
    const VenueDef *v = m_track.m_venue;
    m_audio.music( v ? v->m_music : "race" );
    go( ScreenE::RACE );
}

// ---------------------------------------------------------------------------------------------------- the race

void App::bubble( const std::string &who, const std::string &text, Color c )
{
    if ( !m_set.m_speech || text.empty() ) return;
    for ( const Bubble &b : m_bubbles )
        if ( b.m_who == who ) return;
    if ( m_bubbles.size() >= 3 ) m_bubbles.erase( m_bubbles.begin() );
    m_bubbles.push_back( { who, text, c, 3.6f } );
}

void App::goHot()
{
    if ( m_hot ) return;
    if ( const VenueDef *v = g_data.venue( g_data.track( m_trackName ) ? g_data.track( m_trackName )->m_venue : "" ) )
        if ( m_audio.hasMusic( v->m_music + "_hot" ) )
        {
            m_audio.music( v->m_music + "_hot" );
            m_hot = true;
        }
}

bool App::quip( const std::string &event )
{
    // A short cry of Ricky's at a rival's moment; the display keeps the line with the name.
    if ( !m_set.m_speech || m_world.m_time - m_lastQuip < 4.0f ) return false;
    const CharacterDef *c = g_data.character( "announcer_voice" );
    if ( !c ) return false;
    const Says &says = english() ? c->m_sayEn : c->m_say;
    auto        it = says.find( "quip_" + event );
    if ( it == says.end() || it->second.empty() ) return false;
    size_t      n = (size_t)GetRandomValue( 0, (int)it->second.size() - 1 );
    std::string file = m_opt.m_root + "/assets/voice/" + ( english() ? "en" : "de" ) + "/ricky_quip_" + event + "_" + std::to_string( n + 1 ) + ".ogg";
    if ( !m_audio.voice( file ) ) return false;
    m_lastQuip = m_world.m_time;
    return true;
}

bool App::speak( const std::string &event )
{
    // Ricky's recorded lines (data/characters.txt "announcer_voice", assets/voice/<language>/ricky_<event>_<n>.ogg):
    // what he says is what the display shows.
    if ( !m_set.m_speech ) return false;
    const CharacterDef *c = g_data.character( "announcer_voice" );
    if ( !c ) return false;
    const Says &says = english() ? c->m_sayEn : c->m_say;
    auto        it = says.find( event );
    if ( it == says.end() || it->second.empty() ) return false;
    size_t      n = (size_t)GetRandomValue( 0, (int)it->second.size() - 1 );
    std::string file = m_opt.m_root + "/assets/voice/" + ( english() ? "en" : "de" ) + "/ricky_" + event + "_" + std::to_string( n + 1 ) + ".ogg";
    if ( !m_audio.voice( file ) ) return false;
    m_announce = it->second[n];
    m_announceTime = 5.0f;
    return true;
}

void App::announce( const std::string &event, const std::string &name )
{
    if ( !m_set.m_speech ) return;
    if ( m_tutorialOn && event == "start" ) return;   // Ricky explains the first step instead
    // The player's big moments are spoken.
    bool mine = name.empty() || name == m_world.m_cars[m_player].m_setup.m_driver;
    if ( mine && ( event == "start" || event == "final_lap" || event == "player_lead" || event == "player_wrecked" ||
                   event == "wrong_way" || ( event == "win" && !name.empty() ) || event == "lose" ) &&
         speak( event ) )
        return;
    if ( event == "wreck" || event == "lead_change" || event == "mine_hit" || event == "rocket_hit" || event == "lap_record" ||
         event == "oil_slip" )
        quip( event );
    bool important = event == "win" || event == "lose" || event == "player_wrecked" || event == "start";
    if ( m_announceTime > 1.2f && !important ) return;
    std::string s = announcerLine( event, name, (uint32_t)( m_world.m_time * 977 ) + (uint32_t)event.size() );
    if ( s.empty() ) return;
    m_announce = s;
    m_announceTime = 4.5f;
}

void App::race( float dt )
{
    if ( !m_paused )
    {
        if ( m_in.pause() )
        {
            m_paused = true;
            m_pausedAt = m_time;
            m_pauseSel = 0;
            m_audio.quiet();
            sound( SfxE::UI_OK );
        }
        m_acc += dt;
        int steps = 0;
        while ( m_acc >= Dt && steps++ < 5 && !m_paused )
        {
            m_acc -= Dt;
            raceStep();
        }
        if ( m_acc > Dt * 5 ) m_acc = 0;
        m_view.update( m_world, dt );
        for ( const Renderer::Cue &c : m_view.m_cues )
        {
            if ( c.m_kind >= 2 )   // thunder: heard however far away
            {
                float pan = clampf( ( c.m_pos.x - m_cam.m_focus.x ) / 60.0f, -0.6f, 0.6f );
                m_audio.play( c.m_kind == 2 ? SfxE::THUNDER_NEAR : SfxE::THUNDER_FAR, pan, c.m_kind == 2 ? 0.9f : 0.6f,
                              0.92f + GetRandomValue( 0, 16 ) / 100.0f );
                continue;
            }
            float d = len( sub( c.m_pos, m_cam.m_focus ) );
            if ( d > 70.0f ) continue;
            float pan = clampf( ( c.m_pos.x - m_cam.m_focus.x ) / 45.0f, -1, 1 );
            m_audio.play( c.m_kind == 0 ? SfxE::BIRDS_UP : SfxE::DOG_BARK, pan, 0.7f / ( 1.0f + d / 25.0f ),
                          0.9f + GetRandomValue( 0, 25 ) / 100.0f );
        }
        m_view.m_cues.clear();
        const Car &me = m_world.m_cars[m_player];
        raceCamera( dt );
        m_shake = std::max( 0.0f, m_shake - dt * 2.5f );
        m_cam.m_shake = std::min( 1.0f, m_shake ) * m_set.m_shake;
        m_view.m_hurt = std::max( m_view.m_hurt - dt * 1.5f, me.m_hitFlash * 0.4f + ( me.m_hp < me.m_maxHp * 0.25f && !me.m_wrecked ? 0.25f + 0.1f * std::sin( m_time * 6 ) : 0.0f ) );
        for ( Bubble &b : m_bubbles ) b.m_time -= dt;
        m_bubbles.erase( std::remove_if( m_bubbles.begin(), m_bubbles.end(), []( const Bubble &b ) { return b.m_time <= 0; } ), m_bubbles.end() );
        for ( auto &f : m_feed ) f.second -= dt;
        m_feed.erase( std::remove_if( m_feed.begin(), m_feed.end(), []( const auto &f ) { return f.second <= 0; } ), m_feed.end() );
        m_announceTime -= dt;
        m_centerTime -= dt;
        raceSound( dt );
        // Rumble.
        m_rumbleLow = std::max( 0.0f, m_rumbleLow - dt * 3 );
        m_rumbleHigh = std::max( 0.0f, m_rumbleHigh - dt * 5 );
        if ( m_set.m_rumble ) rumble::set( std::min( 1.0f, m_rumbleLow ), std::min( 1.0f, m_rumbleHigh + me.m_skid * 0.15f ) );
        if ( m_world.humansDone() )
        {
            m_endTimer += dt;
            if ( m_endTimer > 4.0f || ( m_endTimer > 1.0f && m_in.accept() ) )
            {
                endRace();
                return;
            }
        }
    }
    m_view.render( m_world, m_cam );
    const Texture2D &t = m_view.texture();
    DrawTexturePro( t, { 0, 0, (float)t.width, (float)-t.height }, { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() }, { 0, 0 }, 0, WHITE );
    raceHud();
    if ( m_paused ) racePause();
}

void App::raceCamera( float dt )
{
    // The camera leads the car the way it goes, so a bend shows in time (docs/DESIGN.md "Seeing the road"). It never
    // turns (turning makes people sick). The picture is tilted: from the focus it sees 0.65 of its height up the
    // screen and 0.36 down, so it leads further down the screen than up, and driving down shows as much road ahead
    // as driving up: 61 m at 108 km/h either way (56 m up and 34 m down before), across the screen 84 m (66 m).
    // The lead follows the velocity, damped: a spin or a crash glides instead of jerking.
    constexpr float LeadUp = 0.18f, LeadDown = 0.465f, LeadSide = 0.35f;   // shares of the height
    const Car &me = m_world.m_cars[m_player];
    float      speed = len( me.m_vel );
    float      h = ( 58.0f + speed * 0.55f ) * m_set.m_zoom;
    m_cam.m_height += ( h - m_cam.m_height ) * std::min( 1.0f, dt * 1.5f );
    // The anchor goes with the car without trailing it; a jump (back on the track) glides.
    Vector2 moved = sub( me.m_pos, m_camCar );
    m_camCar = me.m_pos;
    if ( len( moved ) < 3.0f + speed * 0.1f ) m_camAnchor = add( m_camAnchor, moved );
    m_camAnchor = add( m_camAnchor, mul( sub( me.m_pos, m_camAnchor ), std::min( 1.0f, dt * 6.0f ) ) );
    Vector2 want = { 0, 0 };
    if ( !me.m_wrecked && speed > 0.5f )
    {
        // The whole lead from 54 km/h, less while crawling (the start, a car stuck).
        Vector2 d = mul( me.m_vel, 1.0f / speed );
        float   k = clampf( ( speed - 3.0f ) / 12.0f, 0, 1 ) * m_cam.m_height;
        want = { d.x * LeadSide * k, d.y * ( d.y > 0 ? LeadDown : LeadUp ) * k };
    }
    m_camLead = add( m_camLead, mul( sub( want, m_camLead ), std::min( 1.0f, dt * 1.6f ) ) );
    m_cam.m_focus = add( m_camAnchor, m_camLead );
}

void App::raceStep()
{
    std::vector<CarInput> inputs( m_world.m_cars.size() );
    inputs[m_player] = m_in.car( 0, 1, m_set.m_stickAims ? &m_world.m_cars[m_player] : nullptr );
    // Back on the track: the key asks, the race decides (not in the countdown, not wrecked, not too soon again).
    if ( m_in.latched( ActE::RESET, 0 ) )
    {
        const Car &c = m_world.m_cars[m_player];
        if ( m_world.resetCar( m_player ) )
        {
            m_audio.play( SfxE::PICK_MYSTERY, 0, 0.7f, 0.6f );
            m_camCar = c.m_pos;   // the camera glides after it
            m_stuckTime = 0.0f;
        }
        else if ( m_world.m_phase == World::PhaseE::RACE && c.racing() )
            m_audio.play( SfxE::UI_DENY, 0, 0.5f );
    }
    int before = m_world.m_cars[m_player].m_place;
    int leader = m_world.m_leader;
    int lapBefore = m_world.m_cars[m_player].m_lap;
    m_world.step( inputs );
    if ( m_mode == ModeE::TRIAL && m_world.m_phase != World::PhaseE::COUNTDOWN && !m_tutorialOn )
    {
        // The ghost: this lap recorded, the best one played back from the lap's start.
        const Car &c = m_world.m_cars[m_player];
        if ( c.m_lap != lapBefore )
        {
            bool best = c.m_lastLap > 0 && c.m_lastLap <= c.m_bestLap + 1e-4f && lapBefore >= 0 && m_ghostRec.size() > 60;
            if ( best && ( m_ghost.empty() || c.m_lastLap < m_ghostTime ) )
            {
                if ( !m_ghost.empty() ) award( "ghost_beaten" );
                m_ghost = m_ghostRec;
                m_ghostTime = c.m_lastLap;
                saveGhost();
            }
            m_ghostRec.clear();
            m_ghostAt = 0;
        }
        if ( c.m_dist >= 0 ) m_ghostRec.push_back( { c.m_pos.x, c.m_pos.y, c.m_head } );
        if ( !m_ghost.empty() && c.m_dist >= 0 )
        {
            const Vector3 &g = m_ghost[std::min( m_ghostAt, m_ghost.size() - 1 )];
            m_view.m_ghost.m_on = m_ghostAt < m_ghost.size();
            m_view.m_ghost.m_pos = { g.x, g.y };
            m_view.m_ghost.m_head = g.z;
            m_view.m_ghost.m_model = c.m_def->m_model;
            m_view.m_ghost.m_paint = c.m_setup.m_paint;
            ++m_ghostAt;
        }
    }
    m_in.consumed();
    {
        // The reset's hint: the player stands still trying to drive (against a wall, in a wreck), or drives the
        // wrong way.
        const Car &c = m_world.m_cars[m_player];
        float      sp = len( c.m_vel );
        bool       trying = c.m_in.m_throttle > 0.3f || c.m_in.m_brake > 0.3f;
        bool       race = m_world.m_phase == World::PhaseE::RACE && c.racing();
        if ( race && ( ( sp < 1.5f && trying ) || c.m_wrongWay > 2.0f ) )
            m_stuckTime += Dt;
        else if ( sp > 4.0f || !c.racing() )
            m_stuckTime = 0.0f;
    }
    m_view.events( m_world );
    raceEvents();
    awardsEvents();
    spirits();
    if ( m_tutorialOn ) tutorial();
    if ( m_inCareer && !m_tutorialOn && m_offer.m_job != JobE::NONE && m_jobState == 0 && m_world.m_phase != World::PhaseE::COUNTDOWN ) jobWatch();
    // Places and lead changes.
    const Car &me = m_world.m_cars[m_player];
    // A close fight for a podium place (or, in a derby, three cars left): the music goes up a gear.
    if ( !m_hot && m_world.m_phase == World::PhaseE::RACE && !me.m_wrecked )
    {
        bool close = false;
        if ( m_mode == ModeE::DERBY )
        {
            int alive = 0;
            for ( const Car &c : m_world.m_cars ) alive += c.m_wrecked ? 0 : 1;
            close = alive <= 3;
        }
        else if ( m_mode == ModeE::RACE && me.m_place <= 3 && me.m_lap >= 1 )
            for ( const Car &c : m_world.m_cars )
                if ( &c != &me && !c.m_wrecked && std::fabs( c.m_dist - me.m_dist ) < 22.0f ) close = true;
        m_closeTime = close ? m_closeTime + Dt : std::max( 0.0f, m_closeTime - Dt * 0.5f );
        if ( m_closeTime > ( m_mode == ModeE::DERBY ? 0.1f : 6.0f ) ) goHot();
    }
    if ( m_world.m_phase == World::PhaseE::RACE && m_mode == ModeE::RACE )
    {
        if ( me.m_place > before && before > 0 && !me.m_wrecked )
        {
            // Someone went past.
            std::vector<int> o = m_world.order();
            int              passer = o[std::max( 0, me.m_place - 2 )];
            const Car       &p = m_world.m_cars[passer];
            if ( const RivalDef *d = g_data.rival( p.m_setup.m_rival ) )
                if ( GetRandomValue( 0, 1 ) == 0 ) bubble( d->m_name, d->line( "overtake", (uint32_t)( m_world.m_time * 31 ) ), d->m_colors[0] );
        }
        if ( m_world.m_leader != leader )
        {
            if ( m_world.m_leader == m_player )
                announce( "player_lead", me.m_setup.m_driver );
            else
                announce( "lead_change", m_world.m_cars[m_world.m_leader].m_setup.m_driver );
        }
    }
    if ( m_world.m_bestLap > 0 && m_world.m_bestLap != m_lastBest )
    {
        if ( m_lastBest > 0 && !( m_world.m_bestLapCar == m_player && speak( "player_record" ) ) )
            announce( "lap_record", m_world.m_cars[m_world.m_bestLapCar].m_setup.m_driver );
        m_lastBest = m_world.m_bestLap;
    }
}

void App::raceEvents()
{
    const Car &me = m_world.m_cars[m_player];
    auto       heard = [&]( Vector2 p, float &vol, float &pan ) {
        float d = len( sub( p, m_cam.m_focus ) );
        vol = 1.0f / ( 1.0f + d / 35.0f );
        pan = clampf( ( p.x - m_cam.m_focus.x ) / 45.0f, -1, 1 );
    };
    auto play = [&]( SfxE s, Vector2 p, float v = 1.0f, float pitch = 1.0f ) {
        float vol, pan;
        heard( p, vol, pan );
        m_audio.play( s, pan, vol * v, pitch );
    };
    for ( const Event &e : m_world.m_events )
    {
        bool mine = e.m_car == m_player;
        switch ( e.m_type )
        {
        case EvE::COUNTDOWN:
            m_center = std::to_string( e.m_kind );
            m_centerTime = 0.9f;
            m_audio.play( SfxE::COUNT_BEEP );
            break;
        case EvE::GO:
        {
            m_center = T( "LOS!" );
            m_centerTime = 1.0f;
            m_audio.play( SfxE::COUNT_GO );
            announce( "start", "" );
            // One of the rivals has something to say.
            std::vector<int> rivals;
            for ( size_t i = 0; i < m_world.m_cars.size(); ++i )
                if ( !m_world.m_cars[i].m_setup.m_rival.empty() ) rivals.push_back( (int)i );
            if ( !rivals.empty() )
            {
                const Car      &c = m_world.m_cars[rivals[GetRandomValue( 0, (int)rivals.size() - 1 )]];
                const RivalDef *d = g_data.rival( c.m_setup.m_rival );
                if ( d ) bubble( d->m_name, d->line( "start", (uint32_t)GetRandomValue( 0, 999 ) ), d->m_colors[0] );
            }
            break;
        }
        case EvE::SHOT:
        {
            const Car &c = m_world.m_cars[e.m_car];
            SfxE       s = e.m_kind == 2 ? SfxE::CANNON : c.m_gun->m_name.find( "twin" ) != std::string::npos ? SfxE::TWINMG : SfxE::MG;
            play( s, e.m_pos, mine ? 0.55f : 0.45f, 0.97f + 0.06f * ( ( e.m_car * 7 ) % 5 ) / 5.0f );
            if ( mine ) m_rumbleHigh += 0.04f;
            break;
        }
        case EvE::FLAME: play( SfxE::FLAME_START, e.m_pos, 0.4f ); break;
        case EvE::HIT:
            play( SfxE::BULLET_HIT, e.m_pos, 0.5f, 0.9f + ( e.m_car % 3 ) * 0.07f );
            if ( mine )
            {
                m_rumbleHigh += 0.12f;
                m_shake += 0.03f;
                if ( e.m_other >= 0 && GetRandomValue( 0, 40 ) == 0 )
                    if ( const RivalDef *d = g_data.rival( m_world.m_cars[e.m_other].m_setup.m_rival ) )
                        bubble( d->m_name, d->line( "hit", (uint32_t)GetRandomValue( 0, 999 ) ), d->m_colors[0] );
            }
            break;
        case EvE::RICOCHET: play( SfxE::RICOCHET, e.m_pos, 0.35f, 0.9f + GetRandomValue( 0, 20 ) / 100.0f ); break;
        case EvE::CRASH:
        {
            bool me2 = mine || e.m_other == m_player;
            play( e.m_power > 12 ? SfxE::CRASH_HEAVY : SfxE::CRASH_LIGHT, e.m_pos, clampf( e.m_power / 15.0f, 0.3f, 1.0f ) );
            if ( e.m_power > 18 ) play( SfxE::METAL_CRUNCH, e.m_pos, 0.8f );
            if ( me2 )
            {
                m_shake += e.m_power * 0.025f;
                m_rumbleLow += e.m_power * 0.04f;
            }
            break;
        }
        case EvE::WALL:
            play( SfxE::CRASH_WALL, e.m_pos, clampf( e.m_power / 15.0f, 0.3f, 1.0f ) );
            if ( mine )
            {
                m_shake += e.m_power * 0.02f;
                m_rumbleLow += e.m_power * 0.03f;
            }
            break;
        case EvE::EXPLODE:
        {
            play( e.m_power >= 6 ? SfxE::EXPLOSION_BIG : SfxE::EXPLOSION_SMALL, e.m_pos, 1.0f );
            float d = len( sub( e.m_pos, me.m_pos ) );
            if ( d < 30 && e.m_power >= 4 ) m_audio.duck( 0.5f + e.m_power * 0.05f, 0.3f );   // the blast drowns the band
            if ( d < 25 )
            {
                m_shake += 0.6f * ( 1.0f - d / 25.0f );
                m_rumbleLow += 0.8f * ( 1.0f - d / 25.0f );
            }
            break;
        }
        case EvE::WRECK:
        {
            play( SfxE::WRECK, e.m_pos, 1.0f );
            const Car  &victim = m_world.m_cars[e.m_car];
            std::string killer = e.m_other >= 0 ? m_world.m_cars[e.m_other].m_setup.m_driver : std::string( "" );
            m_feed.push_back( { killer.empty() ? victim.m_setup.m_driver + T( " ist verschrottet" )
                                               : killer + "  →  " + victim.m_setup.m_driver,
                                5.0f } );
            if ( m_feed.size() > 5 ) m_feed.erase( m_feed.begin() );
            if ( e.m_car == m_player )
            {
                announce( "player_wrecked", victim.m_setup.m_driver );
                m_center = T( "VERSCHROTTET" );
                m_centerTime = 3.5f;
                m_rumbleLow = 1.2f;
                if ( e.m_other >= 0 )
                    if ( const RivalDef *d = g_data.rival( m_world.m_cars[e.m_other].m_setup.m_rival ) )
                        bubble( d->m_name, d->line( "wreck_player", (uint32_t)GetRandomValue( 0, 999 ) ), d->m_colors[0] );
            }
            else
            {
                announce( "wreck", victim.m_setup.m_driver );
                // In the career a rival may not climb out: the band plays, the last words are said.
                if ( m_inCareer && !victim.m_setup.m_rival.empty() && m_career.dies( victim.m_setup.m_rival, e.m_other == m_player ) )
                {
                    m_audio.play( SfxE::FUNERAL_MARCH, 0, 0.8f );
                    m_audio.duck( 5.5f, 0.75f );
                    m_feed.push_back( { "\xE2\x80\xA0 " + victim.m_setup.m_driver, 6.0f } );
                    if ( const RivalDef *d = g_data.rival( victim.m_setup.m_rival ) )
                    {
                        std::string words = m_career.lastWords( victim.m_setup.m_rival );
                        if ( !words.empty() ) bubble( d->m_name + "  \xE2\x80\xA0", words, { 90, 90, 100, 255 } );
                    }
                }
                if ( e.m_other == m_player )
                {
                    if ( const RivalDef *d = g_data.rival( victim.m_setup.m_rival ) )
                        bubble( d->m_name, d->line( "wrecked_by_player", (uint32_t)GetRandomValue( 0, 999 ) ), d->m_colors[0] );
                    // Ricky on the player's deed: the third of the race, a feud settled, or a short cry now and then.
                    const RivalState *st = m_inCareer ? m_career.rival( victim.m_setup.m_rival ) : nullptr;
                    if ( m_world.m_cars[m_player].m_wrecks == 3 ) speak( "player_spree" );
                    else if ( st && st->m_grudge >= 2 ) speak( "player_revenge" );
                    else if ( m_world.m_time - m_lastQuip > 6.0f && speak( "player_wreck" ) ) m_lastQuip = m_world.m_time;
                }
                if ( m_mode == ModeE::DERBY )
                {
                    int alive = 0;
                    for ( const Car &c : m_world.m_cars ) alive += c.m_wrecked ? 0 : 1;
                    if ( alive == 2 ) announce( "derby_last", "" );
                }
            }
            break;
        }
        case EvE::PICKUP:
        {
            SfxE s = SfxE::PICK_MYSTERY;
            if ( e.m_text.rfind( "cash", 0 ) == 0 ) s = SfxE::PICK_CASH;
            else if ( e.m_text == "wrench" || e.m_text == "repair" ) s = SfxE::PICK_WRENCH;
            else if ( e.m_text == "ammo" ) s = SfxE::PICK_AMMO;
            else if ( e.m_text.rfind( "nitro", 0 ) == 0 ) s = SfxE::PICK_NITRO;
            else if ( e.m_text != "curse" ) s = SfxE::PICK_WEAPON;
            if ( mine && e.m_text == "wreath" )
            {
                m_audio.play( SfxE::PICK_MYSTERY, 0, 0.8f );
                m_feed.push_back( { tf( T( "+ Kranz (%d/%d)" ), std::min( me.m_wreaths, WreathsNeeded ), WreathsNeeded ), 2.5f } );
                if ( m_feed.size() > 5 ) m_feed.erase( m_feed.begin() );
            }
            else if ( mine )
            {
                m_audio.play( s, 0, 0.8f );
                static const std::pair<const char *, const char *> names[] = {
                    { "cash", "Bargeld" }, { "cash3", "Ein Sack voll Bargeld" }, { "wrench", "Reparatur" }, { "repair", "Volle Reparatur" },
                    { "ammo", "Munition" }, { "nitro", "Lachgas" }, { "nitro2", "Zwei Lachgas" }, { "mine", "Minen" }, { "mine3", "Drei Minen" },
                    { "rocket", "Rakete" }, { "rocket2", "Zwei Raketen" }, { "oil2", "Öl" }, { "smoke", "Nebel" }, { "spikes", "Krähenfüße" },
                    { "curse", "Grabbeigabe: Öl unterm eigenen Wagen" } };
                for ( auto &n : names )
                    if ( e.m_text == n.first )
                    {
                        m_feed.push_back( { std::string( "+ " ) + T( n.second ), 2.5f } );
                        if ( m_feed.size() > 5 ) m_feed.erase( m_feed.begin() );
                    }
            }
            break;
        }
        case EvE::MINE_DROP: play( SfxE::MINE_DROP, e.m_pos, 0.7f ); break;
        case EvE::MINE_ARM: play( SfxE::MINE_ARM, e.m_pos, 0.4f ); break;
        case EvE::ROCKET: play( SfxE::ROCKET_LAUNCH, e.m_pos, 0.9f ); break;
        case EvE::NITRO:
            play( SfxE::NITRO, e.m_pos, 0.8f );
            if ( mine ) m_rumbleLow += 0.3f;
            break;
        case EvE::OIL:
            if ( e.m_kind == 0 ) play( SfxE::OIL_DROP, e.m_pos, 0.7f );
            else if ( mine ) announce( "oil_slip", me.m_setup.m_driver );
            break;
        case EvE::SPIKES: play( SfxE::SPIKES_DROP, e.m_pos, 0.7f ); break;
        case EvE::SMOKE: play( SfxE::SMOKE_DROP, e.m_pos, 0.7f ); break;
        case EvE::TIRE_BURST:
            play( SfxE::TIRE_BURST, e.m_pos, 0.9f );
            if ( mine ) m_rumbleHigh += 0.6f;
            break;
        case EvE::BARREL: play( SfxE::BARREL, e.m_pos, 1.0f ); break;
        case EvE::LOOSE:
        {
            // Plastic, wood, tin: a knock by what it was.
            const std::string &m = e.m_text;
            bool tin = m == "bin" || m == "barrel_oil" || m == "mailbox" || m == "sign_post" || m == "hydrant";
            play( tin ? SfxE::METAL_CRUNCH : SfxE::CRASH_LIGHT, e.m_pos, tin ? 0.35f : 0.45f, tin ? 1.4f : 1.5f + GetRandomValue( 0, 30 ) / 100.0f );
            if ( mine ) m_rumbleHigh += 0.15f;
            break;
        }
        case EvE::TRAIN:
        case EvE::BRIDGE:
            if ( e.m_kind == 0 ) play( SfxE::BELL, e.m_pos, 0.9f, e.m_type == EvE::TRAIN ? 1.0f : 0.8f );
            if ( e.m_kind == 0 && len( sub( e.m_pos, me.m_pos ) ) < 250 )
            {
                bool &said = e.m_type == EvE::TRAIN ? m_saidTrain : m_saidBridge;
                if ( !said ) said = speak( e.m_type == EvE::TRAIN ? "train" : "bridge" );
            }
            else if ( e.m_type == EvE::TRAIN ) play( SfxE::HORN_TRUCK, e.m_pos, 1.0f, 0.7f );
            break;
        case EvE::ROCK:
            if ( e.m_kind == 1 )
            {
                play( SfxE::CRASH_HEAVY, e.m_pos, 1.0f, 0.6f );
                play( SfxE::EXPLOSION_SMALL, e.m_pos, 0.6f, 0.5f );
            }
            break;
        case EvE::WEATHER:
        {
            if ( e.m_kind <= 1 ) speak( e.m_kind == 0 ? "rain" : "fog" );
            static const char *words[] = { "Es fängt an zu regnen.", "Nebel zieht auf.", "Das Wetter schlägt um." };
            m_feed.push_back( { T( words[std::clamp( e.m_kind, 0, 2 )] ), 6.0f } );
            if ( m_feed.size() > 5 ) m_feed.erase( m_feed.begin() );
            break;
        }
        case EvE::HORN:
        {
            const Car &c = m_world.m_cars[e.m_car];
            play( Audio::hornOf( c.m_def->m_horn ), e.m_pos, 0.8f );
            break;
        }
        case EvE::LAP:
            if ( mine )
            {
                m_audio.play( SfxE::LAP, 0, 0.7f );
                m_center = tf( "Runde %d", e.m_kind + 1 );
                m_centerTime = 1.2f;
            }
            break;
        case EvE::FINAL_LAP:
            if ( mine )
            {
                goHot();   // the music goes up a gear for the last lap
                m_audio.play( SfxE::FINAL_LAP, 0, 0.8f );
                m_center = T( "LETZTE RUNDE" );
                m_centerTime = 1.6f;
                announce( "final_lap", me.m_setup.m_driver );
            }
            break;
        case EvE::FINISH:
            if ( mine )
            {
                m_audio.play( SfxE::FINISH, 0, 0.9f );
                m_center = e.m_kind == 1 ? T( "SIEG!" ) : tf( "ZIEL  ·  Platz %d", e.m_kind );
                m_centerTime = 4.0f;
                bool said = false;
                if ( e.m_kind == 1 && m_mode == ModeE::RACE )
                {
                    float gap = 1e9f;
                    for ( const Car &c : m_world.m_cars )
                        if ( &c != &me && !c.m_wrecked ) gap = std::min( gap, me.m_dist - c.m_dist );
                    if ( gap < 15.0f ) said = speak( "player_close" );
                    else if ( m_world.m_cars.size() >= 4 && m_worstPlace >= (int)m_world.m_cars.size() ) said = speak( "player_comeback" );
                    else if ( me.m_damage < me.m_maxHp * 0.05f ) said = speak( "player_clean" );
                }
                if ( !said ) announce( e.m_kind == 1 ? "win" : "lose", me.m_setup.m_driver );
            }
            else if ( e.m_kind == 1 )
                if ( const RivalDef *d = g_data.rival( m_world.m_cars[e.m_car].m_setup.m_rival ) )
                    bubble( d->m_name, d->line( "win", (uint32_t)GetRandomValue( 0, 999 ) ), d->m_colors[0] );
            break;
        case EvE::WRONG_WAY:
            if ( mine )
            {
                m_audio.play( SfxE::WRONG_WAY, 0, 0.7f );
                m_center = T( "FALSCHE RICHTUNG" );
                m_centerTime = 2.0f;
                announce( "wrong_way", me.m_setup.m_driver );
            }
            break;
        default: break;
        }
    }
}

void App::spirits()
{
    if ( m_spiritList.empty() || m_world.m_phase != World::PhaseE::RACE ) return;
    const Car &me = m_world.m_cars[m_player];
    for ( size_t k = 0; k < m_spiritList.size() && k < m_view.m_spirits.size(); ++k )
    {
        Spirit          &sp = m_spiritList[k];
        Renderer::Ghost &gh = m_view.m_spirits[k];
        if ( m_world.m_time < sp.m_wait ) continue;
        sp.m_s = m_track.wrap( sp.m_s + m_spiritV[(size_t)m_track.indexAt( sp.m_s )] * 0.95f * Dt );
        const TrackSample &ts = m_track.sampleAt( sp.m_s );
        float              lane = clampf( ts.m_line + sp.m_lane * std::sin( m_world.m_time * 0.4f + k ), -ts.m_half + 1.5f, ts.m_half - 1.5f );
        gh.m_on = true;
        gh.m_pos = m_track.pointAt( sp.m_s, lane );
        gh.m_head = headingOf( ts.m_dir );
        float d = len( sub( gh.m_pos, me.m_pos ) );
        if ( d < 40.0f && !m_spiritSeen )
        {
            m_spiritSeen = true;
            award( "ghost_seen" );
            speak( "ghost" );
        }
        if ( d < 26.0f && m_world.m_time - sp.m_said > 14.0f )
        {
            // Their old lines from the other side, or their last words again.
            sp.m_said = m_world.m_time;
            const RivalDef   *rd = g_data.rival( sp.m_rival );
            const RivalState *st = m_career.rival( sp.m_rival );
            static const char *events[] = { "overtake", "start", "hit", "win" };
            std::string line = GetRandomValue( 0, 2 ) == 0 && st && !st->m_words.empty()
                                   ? st->m_words
                                   : rd->line( events[GetRandomValue( 0, 3 )], (uint32_t)GetRandomValue( 0, 999 ) );
            bubble( rd->m_name + "  \xE2\x80\xA0", line, { 120, 220, 160, 255 } );
        }
    }
}

void App::jobWatch()
{
    const Car &me = m_world.m_cars[m_player];
    auto       end = [&]( int state, const char *text, const char *say ) {
        m_jobState = state;
        m_feed.push_back( { T( text ), 6.0f } );
        if ( m_feed.size() > 5 ) m_feed.erase( m_feed.begin() );
        m_audio.play( state > 0 ? SfxE::UI_CASH : SfxE::UI_DENY, 0, 0.7f );
        speak( say );
    };
    const Car *other = m_jobCar >= 0 ? &m_world.m_cars[m_jobCar] : nullptr;
    switch ( m_offer.m_job )
    {
    case JobE::COFFIN:
        if ( me.m_wrecked || me.m_damage > CoffinLimit * me.m_maxHp )
        {
            m_view.dropCoffin( m_world, m_player );
            end( -1, "Der Sarg ist vom Dach gefallen. Auftrag verpatzt.", "job_coffin_lost" );
        }
        else if ( me.m_finished )
            end( 1, "Der Sarg ist angekommen. Die Kundschaft hat nichts gemerkt.", "job_done" );
        break;
    case JobE::DEBTOR:
        if ( other && other->m_wrecked )
        {
            if ( other->m_wreckedBy == m_player ) end( 1, "Schuld beglichen. Graves lässt grüßen.", "job_debtor_done" );
            else end( -1, "Jemand anderes war schneller. Der Schuldner ist hin, das Geld auch.", "job_failed" );
        }
        else if ( me.m_finished || me.m_wrecked )
            end( -1, "Der Schuldner ist entwischt.", "job_failed" );
        break;
    case JobE::ESCORT:
        if ( other && other->m_wrecked ) end( -1, "Der Leichenwagen ist ein Wrack. Graves wird das nicht gefallen.", "job_escort_lost" );
        else if ( other && other->m_finished ) end( 1, "Der Leichenwagen ist heil im Ziel.", "job_done" );
        break;
    case JobE::WREATHS:
        if ( me.m_wreaths >= WreathsNeeded ) end( 1, "Genug Kränze für die ganze Familie.", "job_done" );
        else if ( me.m_finished || me.m_wrecked ) end( -1, "Zu wenig Kränze. Die Trauerfeier wird karg.", "job_failed" );
        break;
    default: break;
    }
}

void App::jobHud()
{
    if ( !m_inCareer || m_offer.m_job == JobE::NONE ) return;
    float       k = ui::k();
    const Car  &me = m_world.m_cars[m_player];
    const Car  *other = m_jobCar >= 0 ? &m_world.m_cars[m_jobCar] : nullptr;
    static const char *titles[] = { "", "Auftrag: der Sarg auf dem Dach", "Auftrag: der Schuldner", "Auftrag: der Leichenwagen",
                                    "Auftrag: die Kränze" };
    std::string line;
    switch ( m_offer.m_job )
    {
    case JobE::COFFIN:
        line = tf( T( "Schaden %d %% von höchstens %d %%" ), (int)( me.m_damage / std::max( 1.0f, me.m_maxHp ) * 100 ), (int)( CoffinLimit * 100 ) );
        break;
    case JobE::DEBTOR: line = other ? tf( T( "%s muss verschrottet werden. Von dir." ), other->m_setup.m_driver.c_str() ) : ""; break;
    case JobE::ESCORT:
        line = other ? tf( T( "Panzerung des Leichenwagens: %d %%" ), (int)( std::max( 0.0f, other->m_hp ) / std::max( 1.0f, other->m_maxHp ) * 100 ) ) : "";
        break;
    case JobE::WREATHS: line = tf( T( "Kränze: %d von %d" ), std::min( me.m_wreaths, WreathsNeeded ), WreathsNeeded ); break;
    default: break;
    }
    if ( m_jobState > 0 ) line = T( "Erledigt!" );
    if ( m_jobState < 0 ) line = T( "Verpatzt." );
    float     w = std::max( ui::width( T( titles[(int)m_offer.m_job] ), 26 ), ui::width( line, 24 ) ) + 40 * k;
    Rectangle r = { GetScreenWidth() * 0.5f - w * 0.5f, 20 * k, w, 82 * k };
    ui::panel( r, 0.6f );
    ui::text( T( titles[(int)m_offer.m_job] ), r.x + 20 * k, r.y + 8 * k, 26, ui::Gold );
    ui::text( line, r.x + 20 * k, r.y + 44 * k, 24, m_jobState > 0 ? ui::Good : m_jobState < 0 ? ui::Warn : ui::Ink );
}

void App::raceSound( float dt )
{
    if ( m_paused ) return;
    const VenueDef *v = m_track.m_venue;
    float skid = 0, skidLoose = 0, fire = 0;
    for ( size_t i = 0; i < m_world.m_cars.size() && i < 8; ++i )
    {
        const Car &c = m_world.m_cars[i];
        float      d = len( sub( c.m_pos, m_cam.m_focus ) );
        bool       me = (int)i == m_player;
        float      vol = me ? 0.85f : 0.75f / ( 1.0f + d / 22.0f );
        if ( c.m_wrecked ) vol = 0;
        float pan = clampf( ( c.m_pos.x - m_cam.m_focus.x ) / 45.0f, -1, 1 );
        m_audio.engine( (int)i, Audio::engineKind( c.m_def->m_engine ), c.m_rpm, c.m_load, vol, pan );
        float near = 1.0f / ( 1.0f + d / 20.0f );
        if ( surfaceLoose( c.m_surface ) )
            skidLoose = std::max( skidLoose, c.m_skid * near );
        else
            skid = std::max( skid, c.m_skid * near );
        if ( c.m_wrecked && c.m_wreckTime < 25.0f ) fire = std::max( fire, near );
    }
    const Car &me = m_world.m_cars[m_player];
    m_audio.loop( LoopE::SKID, skid * 0.7f, 0.9f + skid * 0.2f );
    m_audio.loop( LoopE::SKID_LOOSE, skidLoose * 0.6f );
    m_audio.loop( LoopE::SCRAPE, me.m_scrape * 0.8f );
    float sp = len( me.m_vel );
    m_audio.loop( LoopE::GRAVEL, me.m_offRoad && !me.m_wrecked ? clampf( sp / 25.0f, 0, 0.7f ) : 0.0f, 0.8f + sp / 80.0f );
    m_audio.loop( LoopE::FLAMER, me.m_firing && me.m_gun->m_heat > 0 ? 0.7f : 0.0f );
    m_audio.loop( LoopE::FIRE, fire * 0.6f );
    if ( v )
    {
        if ( v->m_weather == "rain" || v->m_weather == "storm" ) m_audio.loop( LoopE::RAIN, 0.45f );
        if ( v->m_weather == "snow" || v->m_weather == "dust" || v->m_weather == "storm" ) m_audio.loop( LoopE::WIND, 0.35f );
        m_audio.loop( Audio::ambienceOf( v->m_name ), 0.45f );
    }
    // What flies over the track (the renderer moves it): the helicopter's rotor, the airship's engines.
    m_audio.loop( LoopE::ROTOR, m_view.m_hum.m_rotor * 0.75f, m_view.m_hum.m_pitch );
    m_audio.loop( LoopE::AIRSHIP, m_view.m_hum.m_airship * 0.6f, m_view.m_hum.m_pitch );
    (void)dt;
}

// ---------------------------------------------------------------------------------------------------- HUD

void App::raceHud()
{
    // Names over the rivals' cars (who is who without telling the paints apart).
    if ( m_set.m_tags && m_mode != ModeE::TRIAL )
    {
        float sx = (float)GetScreenWidth() / std::max( 1, m_view.m_w ), sy = (float)GetScreenHeight() / std::max( 1, m_view.m_h );
        for ( size_t i = 0; i < m_world.m_cars.size(); ++i )
        {
            const Car &c = m_world.m_cars[i];
            if ( (int)i == m_player || c.m_wrecked ) continue;
            Vector2 p = m_view.project( { c.m_pos.x, 2.6f, c.m_pos.y } );
            p = { p.x * sx, p.y * sy };
            if ( p.x < 0 || p.y < 0 || p.x > GetScreenWidth() || p.y > GetScreenHeight() ) continue;
            std::string tag = m_mode == ModeE::DERBY ? c.m_setup.m_driver : std::to_string( c.m_place ) + "  " + c.m_setup.m_driver;
            float       w = ui::width( tag, 20 ) + 14 * ui::k();
            DrawRectangleRec( { p.x - w * 0.5f, p.y - 30 * ui::k(), w, 26 * ui::k() }, { 0, 0, 0, 150 } );
            DrawRectangleRec( { p.x - w * 0.5f, p.y - 30 * ui::k(), 4 * ui::k(), 26 * ui::k() }, c.m_setup.m_paint.m_c[0] );
            ui::text( tag, p.x, p.y - 29 * ui::k(), 20, ui::Ink, ui::AlignE::CENTER );
        }
    }
    // The debtor or the hearse: always named.
    if ( m_jobCar >= 0 && !m_world.m_cars[m_jobCar].m_wrecked && m_jobState == 0 )
    {
        float      sx = (float)GetScreenWidth() / std::max( 1, m_view.m_w ), sy = (float)GetScreenHeight() / std::max( 1, m_view.m_h );
        const Car &c = m_world.m_cars[m_jobCar];
        Vector2    p = m_view.project( { c.m_pos.x, 3.0f, c.m_pos.y } );
        p = { p.x * sx, p.y * sy };
        std::string tag = m_offer.m_job == JobE::DEBTOR ? T( "SCHULDNER" ) : T( "LEICHENWAGEN" );
        float       w = ui::width( tag, 22 ) + 16 * ui::k();
        DrawRectangleRec( { p.x - w * 0.5f, p.y - 34 * ui::k(), w, 28 * ui::k() }, { 120, 20, 16, 200 } );
        ui::text( tag, p.x, p.y - 33 * ui::k(), 22, ui::Ink, ui::AlignE::CENTER );
    }
    ui::setScale( m_set.m_hud );
    raceHudPanels();
    ui::setScale( 1.0f );
}

void App::raceHudPanels()
{
    float      k = ui::k();
    int        W = GetScreenWidth(), H = GetScreenHeight();
    const Car &me = m_world.m_cars[m_player];
    int        n = (int)m_world.m_cars.size();
    // Top left: place, lap, time.
    Rectangle top = { 24 * k, 20 * k, 420 * k, 150 * k };
    ui::panel( top, 0.6f );
    if ( m_mode == ModeE::DERBY )
    {
        int alive = 0;
        for ( const Car &c : m_world.m_cars ) alive += c.m_wrecked ? 0 : 1;
        ui::text( tf( "Noch %d", alive ), top.x + 20 * k, top.y + 10 * k, 64, ui::Ink );
        ui::text( T( "Der Letzte, der fährt, gewinnt" ), top.x + 20 * k, top.y + 90 * k, 24, ui::Dim );
    }
    else
    {
        ui::text( m_tutorialOn ? T( "Probefahrt" ) : m_mode == ModeE::TRIAL ? T( "Zeitfahren" ) : tf( "%d/%d", me.m_place, n ), top.x + 20 * k,
                  top.y + 6 * k, m_mode == ModeE::TRIAL ? 44 : 72, ui::Ink );
        int lap = std::clamp( me.m_lap + 1, 1, m_world.m_laps );
        ui::text( tf( "Runde %d/%d", lap, m_world.m_laps ), top.x + 220 * k, top.y + 16 * k, 32, ui::Gold );
        ui::text( clock( m_world.m_time - me.m_lapStart ), top.x + 220 * k, top.y + 56 * k, 30, ui::Ink );
        ui::text( T( "Beste " ) + clock( me.m_bestLap ), top.x + 20 * k, top.y + 104 * k, 26, ui::Dim );
    }
    // The field under it.
    std::vector<int> order = m_world.order();
    float            y = top.y + top.height + 10 * k;
    for ( size_t p = 0; p < order.size() && p < 8; ++p )
    {
        const Car &c = m_world.m_cars[order[p]];
        bool       isMe = order[p] == m_player;
        Color      col = c.m_wrecked ? Color{ 110, 90, 90, 255 } : isMe ? ui::Gold : ui::Ink;
        DrawRectangle( (int)( 24 * k ), (int)y, (int)( 8 * k ), (int)( 26 * k ), c.m_setup.m_paint.m_c[0] );
        std::string name = std::to_string( p + 1 ) + "  " + c.m_setup.m_driver + ( c.m_wrecked ? "  †" : "" );
        ui::text( name, 40 * k, y, 24, col );
        y += 30 * k;
    }
    // Bottom left: armour, gun, gear.
    Rectangle bl = { 24 * k, H - 200 * k, 520 * k, 176 * k };
    ui::panel( bl, 0.6f );
    float hp = me.m_maxHp > 0 ? me.m_hp / me.m_maxHp : 0;
    ui::text( T( "Panzerung" ), bl.x + 18 * k, bl.y + 12 * k, 24, ui::Dim );
    ui::bar( { bl.x + 150 * k, bl.y + 16 * k, 350 * k, 22 * k }, hp, hp < 0.25f ? ui::Warn : hp < 0.5f ? ui::Gold : ui::Good );
    bool flame = me.m_gun->m_heat > 0;
    ui::text( flame ? T( "Hitze" ) : T( "Munition" ), bl.x + 18 * k, bl.y + 48 * k, 24, ui::Dim );
    if ( flame )
        ui::bar( { bl.x + 150 * k, bl.y + 52 * k, 350 * k, 22 * k }, me.m_heat, me.m_heat > 0.8f ? ui::Warn : ui::Gold );
    else
        ui::bar( { bl.x + 150 * k, bl.y + 52 * k, 350 * k, 22 * k }, me.m_maxAmmo > 0 ? me.m_ammo / me.m_maxAmmo : 0, ui::Gold );
    static const char *rearNames[] = { "Minen", "Krähenfüße", "Öl", "Nebel" };
    int rearTotal = 0;
    for ( int r = 0; r < (int)RearE::COUNT; ++r ) rearTotal += me.m_rear[r];
    std::string rear = rearTotal > 0 ? tf( "%s %d", T( rearNames[me.m_rearSel] ), me.m_rear[me.m_rearSel] ) : T( "Heck leer" );
    ui::text( rear, bl.x + 18 * k, bl.y + 88 * k, 30, rearTotal > 0 ? ui::Ink : ui::Dim );
    ui::text( tf( "Raketen %d", me.m_rockets ), bl.x + 18 * k, bl.y + 128 * k, 28, me.m_rockets > 0 ? ui::Ink : ui::Dim );
    ui::text( tf( "Lachgas %d", me.m_nitro ), bl.x + 270 * k, bl.y + 128 * k, 28, me.m_nitroTime > 0 ? ui::Accent : me.m_nitro > 0 ? ui::Ink : ui::Dim );
    // Bottom right: the speed.
    float kmh = len( me.m_vel ) * 3.6f;
    ui::text( TextFormat( "%d", (int)kmh ), W - 140 * k, H - 150 * k, 96, ui::Ink, ui::AlignE::RIGHT );
    ui::text( "km/h", W - 40 * k, H - 90 * k, 28, ui::Dim, ui::AlignE::RIGHT );
    // Top right: the map.
    {
        float     size = 270 * k;
        Rectangle box = { W - size - 24 * k, 20 * k, size, size };
        ui::panel( box, 0.55f );
        Rectangle b = m_track.m_bounds;
        float     sc = std::min( ( size - 30 * k ) / b.width, ( size - 30 * k ) / b.height );
        float     ox = box.x + ( size - b.width * sc ) * 0.5f, oy = box.y + ( size - b.height * sc ) * 0.5f;
        auto      P = [&]( Vector2 p ) { return Vector2{ ox + ( p.x - b.x ) * sc, oy + ( p.y - b.y ) * sc }; };
        int       ns = (int)m_track.m_s.size();
        for ( int i = 0; i < ns; i += 2 )
        {
            const TrackSample &a = m_track.m_s[i], &c = m_track.m_s[( i + 2 ) % ns];
            DrawLineEx( P( a.m_pos ), P( c.m_pos ), std::max( 2.0f, a.m_half * 2 * sc ), { 150, 140, 130, 200 } );
        }
        Vector2 s0 = P( m_track.m_s[0].m_pos );
        DrawCircleV( s0, 4 * k, WHITE );
        for ( size_t i = 0; i < m_world.m_cars.size(); ++i )
        {
            const Car &c = m_world.m_cars[i];
            if ( c.m_wrecked ) continue;
            float r = (int)i == m_player ? 7 * k : 5 * k;
            if ( (int)i == m_jobCar ) DrawCircleLinesV( P( c.m_pos ), r + 5 * k, std::fmod( m_time, 0.6f ) < 0.3f ? ui::Warn : ui::Gold );
            DrawCircleV( P( c.m_pos ), r + 1.5f * k, BLACK );
            DrawCircleV( P( c.m_pos ), r, (int)i == m_player ? ui::Gold : c.m_setup.m_paint.m_c[0] );
        }
        // The feed under the map.
        float fy = box.y + size + 14 * k;
        for ( auto &f : m_feed )
        {
            ui::text( f.first, box.x + size, fy, 24, { 230, 200, 180, (unsigned char)( 255 * std::min( 1.0f, f.second ) ) }, ui::AlignE::RIGHT );
            fy += 30 * k;
        }
    }
    if ( !m_tutorialOn ) jobHud();
    tutorialHud();
    // The middle: countdown, laps, wreck.
    if ( m_centerTime > 0 && !m_center.empty() )
    {
        float a = std::min( 1.0f, m_centerTime * 2.0f );
        ui::text( m_center, W * 0.5f, H * 0.3f, 110, { 240, 60, 40, (unsigned char)( 255 * a ) }, ui::AlignE::CENTER );
    }
    // The messages in the middle (the pace notes, the shouts) keep out of the car's way: under the panels at the top,
    // or at the bottom while the car drives high on the screen (heading down the screen, the camera leads it up there).
    // They fade out and in when they change sides.
    {
        Vector2 cp = m_view.project( { me.m_pos.x, 0.0f, me.m_pos.y } );
        float   cy = cp.y / std::max( 1, m_view.m_h );
        bool    low = m_hudLow ? cy < 0.5f : cy < 0.36f;
        if ( low != m_hudLow )
        {
            m_hudFade -= GetFrameTime() * 6.0f;
            if ( m_hudFade <= 0.0f ) m_hudLow = low;
        }
        else
            m_hudFade = std::min( 1.0f, m_hudFade + GetFrameTime() * 6.0f );
    }
    float fade = clampf( m_hudFade, 0, 1 );
    float panels = m_tutorialOn ? 162 * k : m_inCareer && m_offer.m_job != JobE::NONE ? 112 * k : 20 * k;
    float bottom = H - 212 * k;   // above the panels at the bottom (the armour reaches up to H - 200)
    float notes = paceNotes( m_hudLow ? bottom : panels, m_hudLow, fade );
    // Shouts.
    float by = m_hudLow ? bottom - ( notes > 0 ? notes + 12 * k : 0.0f ) - 80 * k
                        : std::max( 190 * k, panels + notes + 12 * k );
    for ( const Bubble &b : m_bubbles )
    {
        float     a = std::min( 1.0f, b.m_time * 2.0f ) * fade;
        float     w = std::max( ui::width( b.m_text, 28 ), ui::width( b.m_who, 22 ) ) + 40 * k;
        Rectangle r = { W * 0.5f - w * 0.5f, by, w, 80 * k };
        DrawRectangleRec( r, { 10, 6, 8, (unsigned char)( 190 * a ) } );
        DrawRectangleRec( { r.x, r.y, 6 * k, r.height }, faded( b.m_color, a ) );
        ui::text( b.m_who, r.x + 20 * k, r.y + 6 * k, 22, { ui::Gold.r, ui::Gold.g, ui::Gold.b, (unsigned char)( 255 * a ) } );
        ui::text( quote( b.m_text ), r.x + 20 * k, r.y + 36 * k, 28, { 240, 236, 226, (unsigned char)( 255 * a ) } );
        by += ( m_hudLow ? -92.0f : 92.0f ) * k;
    }
    // Stuck against a wall, or the wrong way round: the key that puts the car back on the track.
    if ( m_stuckTime > 2.5f && me.racing() && m_world.m_phase == World::PhaseE::RACE )
    {
        std::string key = m_in.label( ActE::RESET, m_in.padUsed() );
        std::string s = tf( "Festgefahren? %s: zurück auf die Strecke", key.c_str() );
        float       w = ui::width( s, 30 ) + 50 * k, a = clampf( ( m_stuckTime - 2.5f ) * 3.0f, 0, 1 );
        Rectangle   r = { W * 0.5f - w * 0.5f, H * 0.68f, w, 54 * k };
        DrawRectangleRec( r, { 10, 6, 8, (unsigned char)( 200 * a ) } );
        DrawRectangleRec( { r.x, r.y, 6 * k, r.height }, faded( ui::Gold, a ) );
        ui::text( s, W * 0.5f, r.y + 10 * k, 30, faded( ui::Ink, a ), ui::AlignE::CENTER );
    }
    // The announcer.
    if ( m_announceTime > 0 && !m_announce.empty() )
    {
        float a = std::min( 1.0f, m_announceTime );
        float w = ui::width( m_announce, 28 ) + 60 * k;
        DrawRectangle( (int)( W * 0.5f - w * 0.5f ), (int)( H - 90 * k ), (int)w, (int)( 50 * k ), { 0, 0, 0, (unsigned char)( 150 * a ) } );
        ui::text( m_announce, W * 0.5f, H - 82 * k, 28, { ui::Gold.r, ui::Gold.g, ui::Gold.b, (unsigned char)( 255 * a ) }, ui::AlignE::CENTER );
    }
}

void App::findBends()
{
    // The bends for the pace notes (docs/DESIGN.md "Pace notes"): where the centre line bends one way, its turn, and
    // the racing line's tightest radius in it (the speed it allows). None in an arena.
    m_bends.clear();
    const std::vector<TrackSample> &S = m_track.m_s;
    int                             n = (int)S.size();
    if ( m_mode == ModeE::DERBY || m_track.m_arena || n < 20 ) return;
    // Which way it bends where it bends at all (a radius under 250 m), the curvature smoothed over 14 m.
    std::vector<int> side( (size_t)n, 0 );
    for ( int i = 0; i < n; ++i )
    {
        float k = 0;
        for ( int j = -3; j <= 3; ++j ) k += S[( i + j + n ) % n].m_curv;
        k /= 7.0f;
        side[i] = k > 1.0f / 250.0f ? 1 : k < -1.0f / 250.0f ? -1 : 0;
    }
    // A short straight between two arcs the same way (a bend that tightens again) belongs to the bend.
    for ( int i = 0; i < n; ++i )
    {
        if ( side[i] == 0 || side[( i + 1 ) % n] != 0 ) continue;
        int j = 1;
        while ( j <= 7 && side[( i + j ) % n] == 0 ) ++j;
        if ( j <= 7 && side[( i + j ) % n] == side[i] )
            for ( int g = 1; g < j; ++g ) side[( i + g ) % n] = side[i];
    }
    // The bends, read from a straight so that none is cut in two at the start line.
    int first = 0;
    while ( first < n && side[first] != 0 ) ++first;
    if ( first == n ) return;   // one endless bend: nothing to announce
    for ( int m = 0; m < n; )
    {
        int i0 = ( first + m ) % n;
        if ( side[i0] == 0 )
        {
            ++m;
            continue;
        }
        int count = 0;
        while ( m + count < n && side[( i0 + count ) % n] == side[i0] ) ++count;
        m += count;
        float turn = 0, curv = 0;
        for ( int j = 0; j < count; ++j ) turn += S[( i0 + j ) % n].m_curv * m_track.m_step;
        for ( int j = -6; j < count + 6; ++j ) curv = std::max( curv, S[( ( i0 + j ) % n + n ) % n].m_lineCurv );
        if ( std::fabs( turn ) < 15.0f * Deg && curv < 1.0f / 40.0f ) continue;   // a kink
        Bend b;
        b.m_s0 = S[i0].m_s;
        b.m_s1 = S[( i0 + count - 1 ) % n].m_s;
        b.m_turn = turn;
        b.m_radius = curv > 1e-4f ? 1.0f / curv : 1e4f;
        b.m_head = headingOf( S[i0].m_dir );
        b.m_grade = b.m_radius < 22.0f ? 4 : b.m_radius < 40.0f ? 3 : b.m_radius < 75.0f ? 2 : 1;
        m_bends.push_back( b );
    }
}

float App::paceNotes( float y, bool low, float alpha )
{
    // The next bend and the one after it, when it follows close, as arrows: their shape seen from above (north up,
    // like the picture), how sharp (the colour and one to four marks) and how far.
    if ( !m_set.m_paceNotes || m_bends.empty() || m_world.m_phase == World::PhaseE::DONE ) return 0.0f;
    const Car &me = m_world.m_cars[m_player];
    if ( !me.racing() || me.m_wrongWay > 0.0f ) return 0.0f;
    float look = std::max( 140.0f, len( me.m_vel ) * 4.5f );
    const Bend *next[2] = { nullptr, nullptr };
    float       dist[2] = { 1e9f, 1e9f };
    for ( const Bend &b : m_bends )
    {
        // Bends this car takes flat out are left out (its tyres' side grip on asphalt).
        if ( std::sqrt( sideGrip( me ) * b.m_radius ) >= me.m_top * 0.97f ) continue;
        float d = m_track.wrap( b.m_s0 - me.m_s );
        if ( d < dist[0] )
        {
            next[1] = next[0];
            dist[1] = dist[0];
            next[0] = &b;
            dist[0] = d;
        }
        else if ( d < dist[1] )
        {
            next[1] = &b;
            dist[1] = d;
        }
    }
    if ( !next[0] || dist[0] > look ) return 0.0f;
    // The second only when it comes right after the first (rally notes: "into").
    float into = dist[1] - m_track.wrap( next[0]->m_s1 - me.m_s );
    int   shown = next[1] && into < 60.0f && dist[1] < look + 60.0f ? 2 : 1;
    float k = ui::k(), pad = 10 * k, gap = 16 * k;
    float icon[2] = { 86 * k, 64 * k }, col[2] = { 92 * k, 80 * k };
    float w = pad * 2 + icon[0] + col[0] + ( shown > 1 ? gap + icon[1] + col[1] : 0.0f ), h = icon[0] + pad * 2;
    Rectangle r = { GetScreenWidth() * 0.5f - w * 0.5f, low ? y - h : y, w, h };
    // Fading in as it comes into view.
    float a = alpha * clampf( ( look - dist[0] ) / 25.0f + 0.35f, 0, 1 );
    DrawRectangleRec( r, { 10, 6, 8, (unsigned char)( 200 * a ) } );
    float x = r.x + pad;
    for ( int i = 0; i < shown; ++i )
    {
        const Bend &b = *next[i];
        Color       c = b.m_grade <= 1 ? ui::Good : b.m_grade == 2 ? ui::Gold
                      : b.m_grade == 3 ? mixColor( ui::Gold, ui::Warn, 0.55f ) : ui::Warn;
        float       f = a * ( i == 0 ? 1.0f : 0.8f );
        Color       off = { 70, 64, 60, (unsigned char)( 200 * f ) };
        float       s = icon[i], top = r.y + ( h - s ) * 0.5f, mid = top + s * 0.5f;
        drawBend( { x, top, s, s }, b.m_head, b.m_turn, faded( c, f ), ( i == 0 ? 7.0f : 5.5f ) * k );
        x += s + 8 * k;
        int m = std::max( 10, (int)std::lround( dist[i] / 10.0f ) * 10 );
        ui::text( TextFormat( "%d m", m ), x, mid - ( i == 0 ? 30 : 24 ) * k, i == 0 ? 32 : 26, faded( ui::Ink, f ) );
        ui::pips( x, mid + 8 * k, b.m_grade, 4, i == 0 ? 10 : 8, faded( c, f ), off );
        x += col[i] - 8 * k + gap;
    }
    return h;
}

void App::racePause()
{
    float k = ui::k();
    DrawRectangle( 0, 0, GetScreenWidth(), GetScreenHeight(), { 0, 0, 0, 150 } );
    Rectangle r = { GetScreenWidth() * 0.5f - 360 * k, GetScreenHeight() * 0.5f - 230 * k, 720 * k, 460 * k };
    ui::panel( r, 0.92f );
    ui::text( T( "Pause" ), r.x + 40 * k, r.y + 24 * k, 56, ui::Ink );
    bool                     booked = m_inCareer && !m_tutorialOn;
    std::vector<std::string> items = { T( "Weiter" ), booked ? T( "Aufgeben (letzter Platz)" ) : T( "Neu starten" ),
                                       T( "Optionen" ), booked ? T( "Aufgeben und Speichern" ) : T( "Zum Hauptmenü" ) };
    int pick = menu( items, m_pauseSel, { r.x + 50 * k, r.y + 120 * k, r.width - 80 * k, 320 * k }, 40 );
    // The key that paused is still down in this frame: it must not unpause at once.
    if ( m_time - m_pausedAt > 0.05f && ( m_in.back() || m_in.pause() ) ) pick = 0;
    if ( pick == 0 )
    {
        m_paused = false;
        m_in.consumed();   // the presses of the pause (Backspace went back) are not the race's
    }
    else if ( pick == 1 )
    {
        if ( m_tutorialOn )
        {
            m_paused = false;
            startTutorial();
        }
        else if ( m_inCareer )
        {
            Car &me = m_world.m_cars[m_player];
            me.m_dist = -1e9f;
            me.m_finished = false;
            m_paused = false;
            endRace();
        }
        else
            startRace( m_trackName, m_trackReverse, m_field, m_mode, m_track.m_laps, m_player );
    }
    else if ( pick == 2 )
    {
        m_paused = true;
        go( ScreenE::OPTIONS );
    }
    else if ( pick == 3 )
    {
        if ( m_tutorialOn )
        {
            m_paused = false;
            m_world.m_phase = World::PhaseE::DONE;
            endRace();
        }
        else if ( m_inCareer )
        {
            Car &me = m_world.m_cars[m_player];
            me.m_dist = -1e9f;
            m_paused = false;
            endRace();
        }
        else
        {
            m_racing = false;
            m_paused = false;
            m_audio.quiet();
            rumble::stop();
            m_atReady = false;
            m_audio.music( "title" );
            go( ScreenE::TITLE );
        }
    }
}

// ---------------------------------------------------------------------------------------------------- the ghost

void App::loadGhost()
{
    std::string path = userDir() + "ghosts/" + m_trackName + ( m_trackReverse ? "_r" : "" ) + ".txt";
    FILE       *f = fopen( path.c_str(), "r" );
    if ( !f ) return;
    float t = 0;
    if ( fscanf( f, "%f", &t ) == 1 ) m_ghostTime = t;
    Vector3 v;
    while ( fscanf( f, "%f %f %f", &v.x, &v.y, &v.z ) == 3 ) m_ghost.push_back( v );
    fclose( f );
}

void App::saveGhost()
{
    std::error_code ec;
    std::filesystem::create_directories( userDir() + "ghosts", ec );
    std::string path = userDir() + "ghosts/" + m_trackName + ( m_trackReverse ? "_r" : "" ) + ".txt";
    FILE       *f = beginWrite( path );
    if ( !f ) return;
    fprintf( f, "%.3f\n", m_ghostTime );
    for ( const Vector3 &v : m_ghost ) fprintf( f, "%.2f %.2f %.4f\n", v.x, v.y, v.z );
    endWrite( f, path );
}

// ---------------------------------------------------------------------------------------------------- the end

void App::endRace()
{
    m_audio.quiet();
    rumble::stop();
    const Car  &me = m_world.m_cars[m_player];
    std::string key = m_trackName + ( m_trackReverse ? ":r" : "" );
    if ( me.m_bestLap > 0 && !m_tutorialOn && ( m_rec.m_bestLap[key] <= 0 || me.m_bestLap < m_rec.m_bestLap[key] ) )
        m_rec.m_bestLap[key] = me.m_bestLap;
    awardsRace();
    if ( m_inCareer && !m_tutorialOn )
    {
        m_report = m_career.finish( m_offer, m_world, m_player );
        if ( m_report.m_won ) ++m_rec.m_careersWon;
        saveCareer();
        awardsCareer();
    }
    else
    {
        m_report = RaceReport();
        std::vector<int> o = m_world.order();
        for ( size_t p = 0; p < o.size(); ++p )
        {
            const Car &c = m_world.m_cars[o[p]];
            ResultLine l;
            l.m_name = c.m_setup.m_driver;
            l.m_car = c.m_def ? c.m_def->m_title.get() : c.m_setup.m_car;
            l.m_place = (int)p + 1;
            l.m_wrecked = c.m_wrecked;
            l.m_player = o[p] == m_player;
            l.m_time = c.m_finished ? c.m_finishTime : 0.0f;
            l.m_wrecks = c.m_wrecks;
            if ( l.m_player ) m_report.m_place = l.m_place;
            m_report.m_lines.push_back( l );
        }
        if ( m_report.m_place == 1 && !me.m_wrecked ) ( m_mode == ModeE::DERBY ? m_rec.m_derbyWins : m_rec.m_quickWins )++;
    }
    m_rec.save( userDir() + "records.txt" );
    m_racing = false;
    m_atReady = false;
    m_audio.music( m_report.m_place == 1 ? "win" : "results" );
    go( ScreenE::RESULTS );
}

void App::results()
{
    backdrop();
    float k = ui::k();
    if ( m_tutorialOn )
        header( m_tutStep >= 8 ? T( "Probefahrt bestanden" ) : T( "Probefahrt abgebrochen" ),
                m_tutStep >= 8 ? T( "Graves & Sons stellen dir einen Führerschein aus. Auf Probe, versteht sich." )
                               : T( "Kein Führerschein heute. Der Hof bleibt offen." ) );
    else
        header( m_report.m_place == 1 ? T( "Sieg!" ) : m_report.m_wrecked ? T( "Verschrottet" ) : tf( "Platz %d", m_report.m_place ),
                g_data.track( m_trackName ) ? g_data.track( m_trackName )->m_title.get() : m_trackName );
    moneyBox();
    Rectangle r = { 70 * k, 200 * k, 1100 * k, 620 * k };
    ui::panel( r );
    float y = r.y + 20 * k;
    ui::text( T( "Platz" ), r.x + 20 * k, y, 24, ui::Dim );
    ui::text( T( "Fahrer" ), r.x + 120 * k, y, 24, ui::Dim );
    ui::text( T( "Wagen" ), r.x + 500 * k, y, 24, ui::Dim );
    ui::text( T( "Zeit" ), r.x + 760 * k, y, 24, ui::Dim );
    if ( m_inCareer && !m_tutorialOn ) ui::text( T( "Preis / Punkte" ), r.x + r.width - 20 * k, y, 24, ui::Dim, ui::AlignE::RIGHT );
    y += 44 * k;
    for ( const ResultLine &l : m_report.m_lines )
    {
        Color c = l.m_player ? ui::Gold : l.m_wrecked ? Color{ 150, 120, 120, 255 } : ui::Ink;
        ui::text( std::to_string( l.m_place ), r.x + 20 * k, y, 32, c );
        ui::text( l.m_name + ( l.m_died ? T( "  † verstorben" ) : "" ), r.x + 120 * k, y, 32, l.m_died ? ui::Warn : c );
        ui::text( l.m_car, r.x + 500 * k, y, 28, c );
        ui::text( l.m_wrecked ? T( "Wrack" ) : clock( l.m_time ), r.x + 760 * k, y, 28, c );
        if ( m_inCareer && !m_tutorialOn ) ui::text( formatMoney( l.m_prize ) + ( l.m_points ? tf( "  ·  %d P", l.m_points ) : "" ), r.x + r.width - 20 * k, y, 28, c, ui::AlignE::RIGHT );
        y += 48 * k;
    }
    // The podium in the table's free lower part: the first three on Graves & Sons' coffins, a derby's runners-up as
    // the wrecks they are. Not after the test drive or a time trial (one car).
    {
        Rectangle box = { r.x + 20 * k, y + 8 * k, r.width - 40 * k, r.y + r.height - 16 * k - ( y + 8 * k ) };
        if ( !m_tutorialOn && m_world.m_cars.size() >= 2 && box.height > 110 * k )
        {
            std::vector<Renderer::PodiumCar> top;
            for ( int i : m_world.order() )
            {
                const Car &c = m_world.m_cars[i];
                if ( !c.m_def || top.size() >= 3 ) continue;
                top.push_back( { c.m_def->m_model, c.m_setup.m_paint, c.m_wrecked } );
            }
            m_podium.podium( top, GetFrameTime() );
            // The picture cut to the box's shape round its middle.
            const Texture2D &t = m_podium.texture();
            float sw = (float)t.width, sh = sw * box.height / box.width;
            if ( sh > t.height )
            {
                sh = (float)t.height;
                sw = sh * box.width / box.height;
            }
            Rectangle src = { ( t.width - sw ) * 0.5f, ( t.height - sh ) * 0.5f, sw, -sh };
            DrawTexturePro( t, src, box, { 0, 0 }, 0, WHITE );
            DrawRectangleLinesEx( box, std::max( 1.0f, 2 * k ), { 60, 54, 48, 255 } );
        }
    }
    // Last words of the departed.
    float lw = r.y + r.height + 16 * k;
    for ( const std::string &key : m_report.m_dead )
        if ( const RivalDef *d = g_data.rival( key ) )
        {
            const RivalState *st = m_career.rival( key );
            std::string       line = st ? st->m_words : "";
            if ( line.empty() ) continue;
            ui::text( "† " + d->m_name + ":  " + quote( line ), r.x, lw, 26, ui::Warn );
            lw += 34 * k;
        }
    if ( m_inCareer && !m_tutorialOn )
    {
        Rectangle m = { r.x + r.width + 30 * k, r.y, GetScreenWidth() - r.x - r.width - 100 * k, 620 * k };
        ui::panel( m );
        float my = m.y + 20 * k;
        auto  row = [&]( const char *label, int v ) {
            if ( v == 0 ) return;
            ui::text( T( label ), m.x + 20 * k, my, 28, ui::Ink );
            ui::text( formatMoney( v ), m.x + m.width - 20 * k, my, 28, ui::Gold, ui::AlignE::RIGHT );
            my += 42 * k;
        };
        row( "Preisgeld", m_report.m_prize );
        row( "Kopfgeld", m_report.m_bounty );
        row( "Aufgesammelt", m_report.m_cash );
        row( "Schnellste Runde", m_report.m_lap );
        row( "Sponsor", m_report.m_sponsor );
        row( "Aufstiegsprämie von Graves", m_report.m_promoBonus );
        row( "Auftrag von Graves", m_report.m_jobPay );
        if ( m_report.m_job != JobE::NONE && !m_report.m_jobDone )
        {
            ui::text( T( "Auftrag verpatzt: kein Lohn" ), m.x + 20 * k, my, 28, ui::Warn );
            my += 42 * k;
        }
        DrawRectangle( (int)( m.x + 20 * k ), (int)my, (int)( m.width - 40 * k ), 2, ui::Dim );
        my += 12 * k;
        ui::text( T( "Gesamt" ), m.x + 20 * k, my, 34, ui::Ink );
        ui::text( formatMoney( m_report.m_total + m_report.m_promoBonus ), m.x + m.width - 20 * k, my, 34, ui::Gold, ui::AlignE::RIGHT );
        my += 60 * k;
        if ( m_career.m_damage > 0.01f )
        {
            ui::text( tf( "Schaden: %d %%", (int)( m_career.m_damage * 100 ) ), m.x + 20 * k, my, 28, ui::Warn );
            my += 40 * k;
            ui::text( T( "Reparatur: " ) + formatMoney( m_career.repairCost() ), m.x + 20 * k, my, 28, ui::Dim );
            my += 40 * k;
        }
        if ( m_report.m_points ) ui::text( tf( "%d Ligapunkte", m_report.m_points ), m.x + 20 * k, my, 28, ui::Good );
    }
    hints( m_in.padUsed() ? T( "A weiter" ) : T( "Eingabe weiter" ) );
    if ( m_in.accept() || IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
    {
        sound( SfxE::UI_OK );
        if ( m_tutorialOn )
        {
            m_tutorialOn = false;
            m_audio.music( m_inCareer ? "garage" : "title" );
            go( m_inCareer ? ScreenE::HUB : ScreenE::TITLE );
        }
        else if ( m_inCareer )
        {
            m_audio.music( "garage" );
            ScreenE after = m_career.m_over ? ScreenE::TABLE : ScreenE::NEWS;
            if ( m_report.m_won )
            {
                m_creditsAfter = ScreenE::TABLE;
                m_creditLines.clear();
                after = ScreenE::CREDITS;
            }
            if ( !m_report.m_story.empty() )
                showStory( m_report.m_story, after );
            else
                go( after );
        }
        else if ( m_tour > 0 )
        {
            // The endless tournament: a podium place goes on, anything else ends it.
            const Car &me = m_world.m_cars[m_player];
            if ( me.m_place <= 3 && !me.m_wrecked )
            {
                ++m_tour;
                if ( m_tour - 1 >= 5 ) award( "tour_5" );
                if ( m_tour - 1 >= 10 ) award( "tour_10" );
                startTourRound();
            }
            else
            {
                m_rec.m_tourBest = std::max( m_rec.m_tourBest, m_tour - 1 );
                m_rec.save( userDir() + "records.txt" );
                m_tour = 0;
                m_audio.music( "title" );
                go( ScreenE::QUICK );
            }
        }
        else
        {
            m_audio.music( "title" );
            go( ScreenE::QUICK );
        }
    }
}

}   // namespace grave
