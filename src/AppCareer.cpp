/**
 * @file AppCareer.cpp
 * @brief The career between the races: the paddock, the race day's offers, the garage, the black market, the loan
 *        shark, the sponsors, the tabloid and the table.
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

namespace
{

/** @return a character's line for an event (and its speaker) */
std::string greet( const std::string &who, const std::string &event )
{
    const CharacterDef *c = g_data.character( who );
    if ( !c ) return "";
    std::string l = c->line( event, (uint32_t)GetRandomValue( 0, 9999 ) );
    return l.empty() ? "" : c->m_name + ": " + quote( l );
}

const char *TierNames[3] = { "Hinterhofrennen", "Hauptrennen", "Großer Preis" };

}   // namespace

void App::saveCareer() { m_career.save( userDir() + "career.txt" ); }

void App::carCard( Rectangle r )
{
    float         k = ui::k();
    const CarDef *c = g_data.car( m_career.m_car );
    ui::panel( r );
    m_show.showroom( c ? c->m_model : "?", m_career.m_paint, m_time * 0.5f, 7.5f );
    const Texture2D &t = m_show.texture();
    float            pw = r.width - 20 * k, ph = pw * 0.75f;
    DrawTexturePro( t, { 0, 0, (float)t.width, (float)-t.height }, { r.x + 10 * k, r.y + 10 * k, pw, ph }, { 0, 0 }, 0, WHITE );
    float y = r.y + ph + 16 * k;
    ui::text( c ? c->m_title.get() : m_career.m_car, r.x + 24 * k, y, 40, ui::Ink );
    y += 52 * k;
    CarStats st = carStats( m_career.playerSetup() );
    float    hp = 1.0f - m_career.m_damage;
    ui::text( T( "Zustand" ), r.x + 24 * k, y, 24, ui::Dim );
    ui::bar( { r.x + 200 * k, y + 4 * k, r.width - 230 * k, 20 * k }, hp, hp < 0.3f ? ui::Warn : hp < 0.7f ? ui::Gold : ui::Good );
    y += 36 * k;
    ui::text( tf( "Panzerung %d  ·  Spitze %d km/h", (int)st.m_hp, (int)( st.m_top * 3.6f ) ), r.x + 24 * k, y, 24, ui::Ink );
    y += 32 * k;
    ui::text( tf( "Motor %d/%d  Reifen %d/%d  Panzer %d/%d  Kanone %d/%d", m_career.m_up[0], m_career.upgradeLevels( UpE::ENGINE ),
                  m_career.m_up[1], m_career.upgradeLevels( UpE::TIRES ), m_career.m_up[2], m_career.upgradeLevels( UpE::ARMOR ),
                  m_career.m_up[3], m_career.upgradeLevels( UpE::GUN ) ),
              r.x + 24 * k, y, 22, ui::Dim );
    y += 30 * k;
    ui::text( tf( "Minen %d  Krähenfüße %d  Öl %d  Nebel %d  Raketen %d  Lachgas %d", m_career.m_rear[0], m_career.m_rear[1],
                  m_career.m_rear[2], m_career.m_rear[3], m_career.m_rockets, m_career.m_nitro ),
              r.x + 24 * k, y, 22, ui::Dim );
}

// ---------------------------------------------------------------------------------------------------- the paddock

void App::hub()
{
    if ( !m_career.m_story.empty() )
    {
        std::string key = m_career.m_story;
        m_career.m_story = key == "intro" ? "league_" + std::to_string( m_career.m_league ) : "";
        saveCareer();
        showStory( key, ScreenE::HUB );
        return;
    }
    if ( m_career.m_over )
    {
        go( ScreenE::TABLE );
        return;
    }
    backdrop();
    float            k = ui::k();
    const LeagueDef *l = g_data.league( m_career.m_league );
    header( T( "Fahrerlager" ), ( l ? l->m_title.get() : T( "Finale" ) ) + "  ·  " + tf( "Platz %d", m_career.rank() ) + "  ·  " +
                                    tf( "Saisontag %d/%d", m_career.m_leagueDay % SeasonDays + 1, SeasonDays ) );
    moneyBox();
    bool                     sd = m_career.showdownReady();
    std::vector<std::string> items;
    if ( sd ) items.push_back( m_career.m_league == 0 ? T( "FINALE gegen Mortimer Graves" ) : T( "SHOWDOWN gegen den Ligachampion" ) );
    if ( m_career.m_league > 0 ) items.push_back( T( "Renntag: die Angebote" ) );
    for ( const char *s : { "Werkstatt", "Schwarzmarkt", "Kredithai", "Sponsor", "Das Sensenblatt", "Tabelle", "Speichern und Hauptmenü" } )
        items.push_back( T( s ) );
    int pick = menu( items, m_sel[(int)ScreenE::HUB], { 96 * k, 220 * k, 760 * k, 700 * k }, 44 );
    carCard( { GetScreenWidth() - 760 * k, 200 * k, 700 * k, 760 * k } );
    hints( m_in.padUsed() ? T( "A wählen   ·   B Hauptmenü" ) : T( "Eingabe wählen   ·   Esc Hauptmenü" ) );
    if ( m_in.back() )
    {
        saveCareer();
        m_inCareer = false;
        m_audio.music( "title" );
        go( ScreenE::TITLE );
        return;
    }
    if ( pick < 0 ) return;
    if ( !sd ) ++pick;
    if ( m_career.m_league == 0 && pick >= 1 ) ++pick;
    switch ( pick )
    {
    case 0:
    {
        if ( m_career.m_damage >= 0.95f )
        {
            m_greeting = greet( "mechanic", "broke" );
            go( ScreenE::GARAGE );
            break;
        }
        m_offer = m_career.showdown();
        int  player = 0;
        auto field = m_career.field( m_offer, player );
        if ( m_offer.m_final ) m_greeting = greet( "graves", "boss_intro" );
        std::string story = m_offer.m_final ? "" : "showdown_" + std::to_string( m_career.m_league );
        startRace( m_offer.m_track, false, field, ModeE::RACE, m_offer.m_laps, player );
        if ( !story.empty() && g_data.story( story ) )
        {
            // The story before the duel; the race waits behind it.
            m_racing = true;
            showStory( story, ScreenE::RACE );
        }
        break;
    }
    case 1: go( ScreenE::OFFERS ); break;
    case 2:
        m_greeting = greet( "mechanic", "greet" );
        go( ScreenE::GARAGE );
        break;
    case 3:
        m_greeting = greet( "dealer", "greet" );
        go( ScreenE::MARKET );
        break;
    case 4:
        m_greeting = greet( "shark", "greet" );
        go( ScreenE::SHARK );
        break;
    case 5: go( ScreenE::SPONSOR ); break;
    case 6:
        m_greeting = greet( "editor", "greet" );
        go( ScreenE::NEWS );
        break;
    case 7: go( ScreenE::TABLE ); break;
    case 8:
        saveCareer();
        m_inCareer = false;
        m_audio.music( "title" );
        go( ScreenE::TITLE );
        break;
    default: break;
    }
}

// ---------------------------------------------------------------------------------------------------- offers

void App::offers()
{
    backdrop();
    float k = ui::k();
    header( T( "Renntag" ), tf( "Tag %d  ·  drei Angebote, ein Leben", m_career.m_day ) );
    moneyBox();
    int &sel = m_sel[(int)ScreenE::OFFERS];
    int  n = (int)m_career.m_offers.size();
    if ( n == 0 )
    {
        ui::text( T( "Heute keine Rennen." ), 96 * k, 240 * k, 40, ui::Ink );
        if ( m_in.back() || m_in.accept() ) go( ScreenE::HUB );
        return;
    }
    sel = std::clamp( sel, 0, n - 1 );
    if ( m_in.left() ) sel = ( sel + n - 1 ) % n;
    if ( m_in.right() ) sel = ( sel + 1 ) % n;
    float cw = ( GetScreenWidth() - 200 * k - ( n - 1 ) * 30 * k ) / n;
    for ( int i = 0; i < n; ++i )
    {
        const Offer    &o = m_career.m_offers[i];
        const TrackDef *td = g_data.track( o.m_track );
        const VenueDef *vd = td ? g_data.venue( td->m_venue ) : nullptr;
        Rectangle       r = { 100 * k + i * ( cw + 30 * k ), 210 * k, cw, 700 * k };
        ui::panel( r, 0.85f, i == sel ? ui::Accent : Color{ 160, 124, 70, 255 } );
        if ( i == sel ) ui::highlight( { r.x, r.y, r.width, 70 * k }, m_time );
        float y = r.y + 16 * k;
        ui::text( o.m_derby ? T( "Derby" ) : T( TierNames[o.m_tier] ), r.x + 24 * k, y, 40, i == sel ? ui::Ink : ui::Gold );
        y += 70 * k;
        ui::text( td ? td->m_title.get() : o.m_track, r.x + 24 * k, y, 34, ui::Ink );
        y += 44 * k;
        ui::text( ( vd ? vd->m_title.get() : "" ) + ( o.m_reverse ? T( "  ·  rückwärts" ) : "" ), r.x + 24 * k, y, 24, ui::Dim );
        y += 40 * k;
        if ( !o.m_derby ) ui::text( tf( "%d Runden", o.m_laps ), r.x + 24 * k, y, 28, ui::Ink );
        y += 44 * k;
        ui::text( T( "Siegprämie: " ) + formatMoney( o.m_purse ), r.x + 24 * k, y, 28, ui::Gold );
        y += 38 * k;
        ui::text( T( "Startgeld: " ) + formatMoney( o.m_fee ), r.x + 24 * k, y, 28, o.m_fee > m_career.m_money ? ui::Warn : ui::Ink );
        y += 54 * k;
        ui::text( T( "Gegner:" ), r.x + 24 * k, y, 24, ui::Dim );
        y += 34 * k;
        for ( const std::string &rk : o.m_rivals )
        {
            const RivalDef *d = g_data.rival( rk );
            if ( !d ) continue;
            DrawRectangle( (int)( r.x + 24 * k ), (int)( y + 4 * k ), (int)( 8 * k ), (int)( 22 * k ), d->m_colors[0] );
            ui::text( d->m_name + ( d->m_champion ? std::string( "  · " ) + T( "Champion" ) : std::string() ), r.x + 42 * k, y, 24, ui::Ink );
            y += 32 * k;
        }
    }
    if ( m_career.m_damage > 0.4f )
        ui::text( tf( "Achtung: dein Wagen ist zu %d %% Schrott.", (int)( m_career.m_damage * 100 ) ), 100 * k, 930 * k, 30, ui::Warn );
    hints( m_in.padUsed() ? T( "Links/Rechts wählen   ·   A antreten   ·   B zurück" ) : T( "Links/Rechts wählen   ·   Eingabe antreten   ·   Esc zurück" ) );
    if ( m_in.back() )
    {
        go( ScreenE::HUB );
        return;
    }
    if ( m_in.accept() || ( IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) ) )
    {
        const Offer &o = m_career.m_offers[sel];
        if ( m_career.m_damage >= 0.95f || !m_career.enter( o ) )
        {
            sound( SfxE::UI_DENY );
            return;
        }
        m_offer = o;
        int  player = 0;
        auto field = m_career.field( o, player );
        startRace( o.m_track, o.m_reverse, field, o.m_derby ? ModeE::DERBY : ModeE::RACE, o.m_laps, player );
    }
}

// ---------------------------------------------------------------------------------------------------- the garage

void App::garage()
{
    backdrop();
    float k = ui::k();
    static const char *tabs[] = { "Reparatur", "Tuning", "Autohaus", "Bordwaffen", "Lack" };
    header( T( "Werkstatt" ), m_greeting );
    moneyBox();
    if ( m_in.prevTab() ) m_tab = ( m_tab + 4 ) % 5;
    if ( m_in.nextTab() ) m_tab = ( m_tab + 1 ) % 5;
    float tx = 96 * k;
    for ( int i = 0; i < 5; ++i )
    {
        float w = ui::text( T( tabs[i] ), tx, 186 * k, 32, i == m_tab ? ui::Accent : ui::Dim );
        tx += w + 40 * k;
    }
    int      &sel = m_sel[(int)ScreenE::GARAGE];
    Rectangle area = { 96 * k, 260 * k, 940 * k, 640 * k };
    std::vector<std::string> items;
    std::vector<bool>        on;
    std::vector<std::string> keys;
    std::string              showModel;
    const CarDef            *cur = g_data.car( m_career.m_car );
    if ( m_tab == 0 )
    {
        int cost = m_career.repairCost();
        items.push_back( cost > 0 ? T( "Reparieren: " ) + formatMoney( cost ) : T( "Der Wagen ist heil." ) );
        on.push_back( cost > 0 && m_career.m_money > 0 );
    }
    else if ( m_tab == 1 )
    {
        static const char *up[] = { "Motor", "Reifen", "Panzerung", "Bordkanone" };
        for ( int u = 0; u < 4; ++u )
        {
            int cost = m_career.upgradeCost( (UpE)u );
            items.push_back( tf( "%s  %d/%d", T( up[u] ), m_career.upgradeLevel( (UpE)u ), m_career.upgradeLevels( (UpE)u ) ) +
                             ( cost < 0 ? T( "   ·   ausgereizt" ) : "   ·   " + formatMoney( cost ) ) );
            on.push_back( cost >= 0 && cost <= m_career.m_money );
        }
    }
    else if ( m_tab == 2 )
    {
        items.push_back( T( "Eintauschwert deines Wagens: " ) + formatMoney( m_career.sellValue() ) );
        on.push_back( false );
        keys.push_back( "" );
        for ( const std::string &c : g_data.m_carOrder )
        {
            const CarDef &d = g_data.m_cars.at( c );
            if ( d.m_class >= 6 || d.m_league < m_career.m_league ) continue;
            int price = m_career.carPrice( c );
            items.push_back( d.m_title.get() + "   ·   " + formatMoney( price ) + ( c == m_career.m_car ? T( "   (deiner)" ) : "" ) );
            on.push_back( c != m_career.m_car && price <= m_career.m_money + m_career.sellValue() );
            keys.push_back( c );
        }
        sel = std::clamp( sel, 0, (int)items.size() - 1 );
        if ( sel == 0 && items.size() > 1 ) sel = 1;
        if ( sel < (int)keys.size() && !keys[sel].empty() ) showModel = g_data.m_cars.at( keys[sel] ).m_model;
    }
    else if ( m_tab == 3 )
    {
        std::string current = m_career.m_gun.empty() && cur ? cur->m_gun : m_career.m_gun;
        for ( const std::string &gname : g_data.m_gunOrder )
        {
            const GunDef &gd = g_data.m_guns.at( gname );
            items.push_back( gd.m_title.get() + "   ·   " + ( gname == current ? T( "eingebaut" ) : formatMoney( m_career.gunPrice( gname ) ) ) );
            on.push_back( gname != current && m_career.gunPrice( gname ) <= m_career.m_money );
            keys.push_back( gname );
        }
    }
    else
    {
        items.push_back( T( "Links/Rechts: die Farben tauschen (kostenlos)" ) );
        on.push_back( true );
    }
    int pick = menu( items, sel, area, 36, &on );
    // The description of what is selected.
    std::string desc;
    if ( m_tab == 2 && sel < (int)keys.size() && !keys[sel].empty() )
    {
        const CarDef &d = g_data.m_cars.at( keys[sel] );
        CarSetup      s;
        s.m_car = keys[sel];
        CarStats st = carStats( s );
        desc = d.m_desc.get() + "\n" + tf( "Panzerung %d   Spitze %d km/h   Kraft %.1f   Grip %.2f", (int)st.m_hp, (int)( st.m_top * 3.6f ), st.m_power, st.m_grip );
    }
    else if ( m_tab == 3 && sel < (int)keys.size() )
        desc = g_data.m_guns.at( keys[sel] ).m_desc.get();
    else if ( m_tab == 1 )
    {
        static const char *keysUp[] = { "engine", "tires", "armor", "gun" };
        if ( const UpgradeDef *u = g_data.upgrade( keysUp[std::clamp( sel, 0, 3 )] ) ) desc = u->m_desc.get();
    }
    if ( !desc.empty() )
    {
        float y = 920 * k;
        for ( const std::string &l : ui::wrap( desc, 940 * k, 26 ) )
        {
            ui::text( l, 96 * k, y, 26, ui::Ink );
            y += 32 * k;
        }
    }
    if ( showModel.empty() )
        carCard( { GetScreenWidth() - 760 * k, 200 * k, 700 * k, 760 * k } );
    else
    {
        Paint p = m_career.m_paint;
        m_show.showroom( showModel, p, m_time * 0.5f, 7.5f );
        const Texture2D &t = m_show.texture();
        Rectangle        r = { GetScreenWidth() - 760 * k, 200 * k, 700 * k, 525 * k };
        ui::panel( r );
        DrawTexturePro( t, { 0, 0, (float)t.width, (float)-t.height }, r, { 0, 0 }, 0, WHITE );
    }
    if ( m_tab == 4 )
    {
        int dir = m_in.left() ? -1 : m_in.right() ? 1 : 0;
        if ( dir )
        {
            // Turn the three colours round, or shift the main colour's hue.
            Paint &p = m_career.m_paint;
            if ( dir > 0 )
                std::rotate( p.m_c, p.m_c + 1, p.m_c + 3 );
            else
            {
                Vector3 hsv = ColorToHSV( p.m_c[0] );
                p.m_c[0] = ColorFromHSV( std::fmod( hsv.x + 30.0f, 360.0f ), std::max( 0.3f, hsv.y ), std::max( 0.25f, hsv.z ) );
            }
            sound( SfxE::UI_MOVE );
        }
    }
    hints( m_in.padUsed() ? T( "LB/RB Bereich   ·   A kaufen   ·   B zurück" ) : T( "Q/E Bereich   ·   Eingabe kaufen   ·   Esc zurück" ) );
    if ( m_in.back() )
    {
        saveCareer();
        go( ScreenE::HUB );
        return;
    }
    if ( pick < 0 ) return;
    bool ok = false;
    if ( m_tab == 0 )
    {
        ok = m_career.repair();
        if ( ok ) m_greeting = greet( "mechanic", "repair" );
    }
    else if ( m_tab == 1 )
    {
        ok = m_career.upgrade( (UpE)pick );
        if ( ok ) m_greeting = greet( "mechanic", "upgrade" );
    }
    else if ( m_tab == 2 && pick < (int)keys.size() && !keys[pick].empty() )
    {
        ok = m_career.buyCar( keys[pick] );
        if ( ok ) m_greeting = greet( "mechanic", "buy" );
    }
    else if ( m_tab == 3 && pick < (int)keys.size() )
        ok = m_career.buyGun( keys[pick] );
    if ( ok )
    {
        sound( SfxE::UI_CASH );
        saveCareer();
    }
    else if ( m_tab != 4 )
    {
        sound( SfxE::UI_DENY );
        m_greeting = greet( "mechanic", "broke" );
    }
}

// ---------------------------------------------------------------------------------------------------- the black market

void App::market()
{
    backdrop();
    float k = ui::k();
    header( T( "Schwarzmarkt" ), m_greeting );
    moneyBox();
    int                     &sel = m_sel[(int)ScreenE::MARKET];
    std::vector<std::string> items, keys;
    std::vector<bool>        on;
    for ( const std::string &gname : g_data.m_gearOrder )
    {
        const GearDef &gd = g_data.m_gear.at( gname );
        if ( gd.m_league < m_career.m_league ) continue;
        int price = m_career.gearPrice( gname ), have = m_career.gearCount( gname ), max = m_career.gearMax( gname );
        items.push_back( gd.m_title.get() + tf( "   %d/%d", have, max ) + "   ·   " + formatMoney( price ) +
                         ( gd.m_pack > 1 ? tf( T( " für %d" ), gd.m_pack ) : "" ) );
        on.push_back( have < max && price <= m_career.m_money );
        keys.push_back( gname );
    }
    // Sabotage: one rival of the league, chosen with left/right.
    std::vector<std::string> roster;
    for ( const RivalState &r : m_career.m_rivals )
        if ( r.m_active && r.m_alive && r.m_league == m_career.m_league && r.m_key != "_player" ) roster.push_back( r.m_key );
    static int target = 0;
    if ( !roster.empty() )
    {
        target = std::clamp( target, 0, (int)roster.size() - 1 );
        const RivalDef *d = g_data.rival( roster[target] );
        items.push_back( m_career.m_sabotaged.empty()
                             ? std::string( T( "Sabotage: " ) ) + ( d ? d->m_name : roster[target] ) + "   ·   " + formatMoney( m_career.sabotagePrice() )
                             : std::string( T( "Sabotage bestellt: " ) ) + ( g_data.rival( m_career.m_sabotaged ) ? g_data.rival( m_career.m_sabotaged )->m_name : "" ) );
        on.push_back( m_career.m_sabotaged.empty() && m_career.sabotagePrice() <= m_career.m_money );
        keys.push_back( "_sabotage" );
    }
    int pick = menu( items, sel, { 96 * k, 220 * k, 1100 * k, 700 * k }, 36, &on );
    if ( sel < (int)keys.size() && keys[sel] == "_sabotage" )
    {
        int dir = m_in.left() ? -1 : m_in.right() ? 1 : 0;
        if ( dir && !roster.empty() ) target = ( target + dir + (int)roster.size() ) % (int)roster.size();
    }
    std::string desc = sel < (int)keys.size() && keys[sel] != "_sabotage" ? g_data.m_gear.at( keys[sel] ).m_desc.get()
                                                                           : T( "Ihr Wagen startet im nächsten Rennen halb kaputt. Wer erwischt wird, zahlt." );
    float y = 930 * k;
    for ( const std::string &l : ui::wrap( desc, 1100 * k, 26 ) )
    {
        ui::text( l, 96 * k, y, 26, ui::Ink );
        y += 32 * k;
    }
    carCard( { GetScreenWidth() - 760 * k, 200 * k, 700 * k, 760 * k } );
    hints( m_in.padUsed() ? T( "A kaufen   ·   Links/Rechts: Sabotageziel   ·   B zurück" ) : T( "Eingabe kaufen   ·   Links/Rechts: Sabotageziel   ·   Esc zurück" ) );
    if ( m_in.back() )
    {
        saveCareer();
        go( ScreenE::HUB );
        return;
    }
    if ( pick < 0 ) return;
    bool ok = keys[pick] == "_sabotage" ? m_career.sabotage( roster[target] ) : m_career.buyGear( keys[pick] );
    if ( ok )
    {
        sound( SfxE::UI_CASH );
        m_greeting = greet( "dealer", keys[pick] == "_sabotage" ? "sabotage" : "buy" );
        saveCareer();
    }
    else
    {
        sound( SfxE::UI_DENY );
        m_greeting = greet( "dealer", "broke" );
    }
}

// ---------------------------------------------------------------------------------------------------- the loan shark

void App::shark()
{
    backdrop();
    float k = ui::k();
    header( T( "Kredithai" ), m_greeting );
    moneyBox();
    const DifficultyDef &dif = g_data.difficulty( m_career.m_difficulty );
    int                  step = std::max( 250, m_career.purse() / 2 );
    long long            room = m_career.loanLimit() - m_career.m_loan;
    std::vector<std::string> items = {
        T( "Leihen: " ) + formatMoney( step ),
        T( "Leihen: alles, was geht (" ) + formatMoney( std::max( 0LL, room ) ) + ")",
        T( "Zurückzahlen: " ) + formatMoney( step ),
        T( "Zurückzahlen: alles" ),
    };
    std::vector<bool> on = { room >= step, room > 0, m_career.m_loan > 0 && m_career.m_money > 0, m_career.m_loan > 0 && m_career.m_money > 0 };
    int pick = menu( items, m_sel[(int)ScreenE::SHARK], { 96 * k, 220 * k, 1000 * k, 400 * k }, 38, &on );
    float y = 660 * k;
    ui::text( T( "Schulden: " ) + formatMoney( m_career.m_loan ), 96 * k, y, 34, m_career.m_loan > 0 ? ui::Warn : ui::Ink );
    y += 50 * k;
    ui::text( tf( "Zinsen: %d %% pro Renntag", (int)std::lround( dif.m_interest * 100 ) ), 96 * k, y, 30, ui::Dim );
    y += 44 * k;
    if ( m_career.m_loan > 0 )
        ui::text( tf( "Fällig an Tag %d. Danach kommen die Inkasso-Herren.", m_career.m_loanDue ), 96 * k, y, 30,
                  m_career.m_day >= m_career.m_loanDue ? ui::Warn : ui::Dim );
    hints( m_in.padUsed() ? T( "A wählen   ·   B zurück" ) : T( "Eingabe wählen   ·   Esc zurück" ) );
    if ( m_in.back() )
    {
        saveCareer();
        go( ScreenE::HUB );
        return;
    }
    bool ok = false;
    if ( pick == 0 ) ok = m_career.borrow( step );
    else if ( pick == 1 ) ok = m_career.borrow( (int)room );
    else if ( pick == 2 ) ok = m_career.repay( step );
    else if ( pick == 3 ) ok = m_career.repay( (int)m_career.m_loan );
    if ( pick >= 0 )
    {
        sound( ok ? SfxE::UI_CASH : SfxE::UI_DENY );
        m_greeting = greet( "shark", ok ? ( pick < 2 ? "borrow" : "repay" ) : "refuse" );
        if ( ok ) saveCareer();
    }
}

// ---------------------------------------------------------------------------------------------------- sponsors

void App::sponsor()
{
    backdrop();
    float k = ui::k();
    header( T( "Sponsoren" ), T( "Wer zahlt, bestimmt die Grabinschrift." ) );
    moneyBox();
    std::vector<std::string> keys = m_career.sponsors(), items;
    for ( const std::string &s : keys )
    {
        const SponsorDef *d = g_data.sponsor( s );
        items.push_back( ( d ? d->m_title.get() : s ) + ( s == m_career.m_sponsor ? T( "   (unter Vertrag)" ) : "" ) );
    }
    if ( items.empty() )
    {
        ui::text( T( "Niemand will dich. Noch nicht." ), 96 * k, 240 * k, 36, ui::Ink );
        if ( m_in.back() || m_in.accept() ) go( ScreenE::HUB );
        return;
    }
    int &sel = m_sel[(int)ScreenE::SPONSOR];
    int  pick = menu( items, sel, { 96 * k, 220 * k, 900 * k, 640 * k }, 36 );
    if ( const SponsorDef *d = g_data.sponsor( keys[std::clamp( sel, 0, (int)keys.size() - 1 )] ) )
    {
        Rectangle r = { 1060 * k, 220 * k, GetScreenWidth() - 1130 * k, 640 * k };
        ui::panel( r );
        float y = r.y + 20 * k;
        for ( const std::string &l : ui::wrap( d->m_desc.get(), r.width - 40 * k, 28 ) )
        {
            ui::text( l, r.x + 20 * k, y, 28, ui::Ink );
            y += 36 * k;
        }
        y += 20 * k;
        static const std::pair<const char *, const char *> conds[] = { { "win", "Sieg" }, { "podium", "Podest" }, { "wreck", "je Wrack" },
                                                                       { "clean", "ohne Kratzer" }, { "lap", "schnellste Runde" }, { "nogun", "ohne einen Schuss" } };
        for ( auto &c : conds )
        {
            auto it = d->m_bonus.find( c.first );
            if ( it == d->m_bonus.end() ) continue;
            ui::text( T( c.second ), r.x + 20 * k, y, 28, ui::Dim );
            ui::text( formatMoney( it->second ), r.x + r.width - 20 * k, y, 28, ui::Gold, ui::AlignE::RIGHT );
            y += 38 * k;
        }
    }
    hints( m_in.padUsed() ? T( "A unterschreiben   ·   B zurück" ) : T( "Eingabe unterschreiben   ·   Esc zurück" ) );
    if ( pick >= 0 )
    {
        m_career.sign( keys[pick] );
        sound( SfxE::UI_STAMP );
        saveCareer();
    }
    if ( m_in.back() ) go( ScreenE::HUB );
}

// ---------------------------------------------------------------------------------------------------- the tabloid

void App::newsScreen()
{
    float k = ui::k();
    int   W = GetScreenWidth(), H = GetScreenHeight();
    // Old paper.
    DrawRectangleGradientV( 0, 0, W, H, { 222, 212, 188, 255 }, { 196, 184, 156, 255 } );
    Color ink = { 30, 24, 20, 255 }, red = { 150, 20, 18, 255 };
    ui::text( T( "Das Sensenblatt" ), W * 0.5f, 30 * k, 96, ink, ui::AlignE::CENTER );
    ui::text( tf( "Tag %d  ·  Unabhängig von allem außer dem Bestattungsgewerbe  ·  Preis: ein Stoßgebet", m_career.m_day ), W * 0.5f, 140 * k, 24, ink,
              ui::AlignE::CENTER );
    DrawRectangle( (int)( 60 * k ), (int)( 180 * k ), W - (int)( 120 * k ), (int)( 4 * k ), ink );
    // The newest first; the ads in their own column.
    std::vector<const NewsItem *> heads, ads;
    for ( auto it = m_career.m_news.rbegin(); it != m_career.m_news.rend(); ++it ) ( it->m_ad ? ads : heads ).push_back( &*it );
    float colW = ( W - 200 * k ) * 0.68f;
    float y = 210 * k;
    for ( size_t i = 0; i < heads.size() && y < H - 160 * k; ++i )
    {
        float size = i == 0 ? 52 : 34;
        for ( const std::string &l : ui::wrap( heads[i]->m_head, colW, size ) )
        {
            ui::text( l, 80 * k, y, size, i == 0 ? red : ink );
            y += size * 1.15f * k;
        }
        for ( const std::string &l : ui::wrap( heads[i]->m_text, colW, 26 ) )
        {
            ui::text( l, 80 * k, y, 26, ink );
            y += 31 * k;
        }
        y += 24 * k;
    }
    float ax = 80 * k + colW + 40 * k, aw = W - ax - 80 * k, ay = 210 * k;
    ui::text( T( "Kleinanzeigen" ), ax, ay, 34, red );
    ay += 50 * k;
    for ( const NewsItem *a : ads )
    {
        if ( ay > H - 200 * k ) break;
        auto  lines = ui::wrap( a->m_text, aw - 30 * k, 24 );
        float h = lines.size() * 29 * k + 24 * k;
        DrawRectangleLinesEx( { ax, ay, aw, h }, 2, ink );
        float ly = ay + 12 * k;
        for ( const std::string &l : lines )
        {
            ui::text( l, ax + 15 * k, ly, 24, ink );
            ly += 29 * k;
        }
        ay += h + 18 * k;
    }
    if ( !m_greeting.empty() ) ui::text( m_greeting, 80 * k, H - 110 * k, 24, { 90, 60, 40, 255 } );
    hints( m_in.padUsed() ? T( "A weiter" ) : T( "Eingabe weiter" ) );
    if ( m_in.accept() || m_in.back() || IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
    {
        sound( SfxE::UI_PAPER );
        go( ScreenE::HUB );
    }
}

void App::tableScreen()
{
    backdrop();
    float            k = ui::k();
    const LeagueDef *l = g_data.league( m_career.m_league );
    header( m_career.m_over ? ( m_career.m_won ? T( "Du hast den Grave Prix gewonnen" ) : T( "Pleite und begraben" ) ) : T( "Tabelle" ),
            l ? l->m_title.get() : T( "Finale" ) );
    moneyBox();
    Rectangle r = { 96 * k, 210 * k, 1000 * k, 700 * k };
    ui::panel( r );
    float y = r.y + 20 * k;
    auto  t = m_career.table();
    for ( size_t i = 0; i < t.size(); ++i )
    {
        bool me = std::get<2>( t[i] ).empty();
        ui::text( std::to_string( i + 1 ), r.x + 20 * k, y, 34, me ? ui::Gold : ui::Ink );
        ui::text( std::get<0>( t[i] ), r.x + 90 * k, y, 34, me ? ui::Gold : ui::Ink );
        ui::text( tf( "%d P", std::get<1>( t[i] ) ), r.x + r.width - 20 * k, y, 34, me ? ui::Gold : ui::Ink, ui::AlignE::RIGHT );
        y += 48 * k;
    }
    if ( l )
        ui::text( tf( "Showdown ab %d Punkten unter den ersten zwei  ·  Saisontag %d/%d, danach beginnt die Tabelle von vorn", l->m_showdown,
                      m_career.m_leagueDay % SeasonDays + 1, SeasonDays ),
                  r.x + 20 * k, r.y + r.height - 50 * k, 24, ui::Dim );
    // The dead of this career.
    Rectangle d = { 1140 * k, 210 * k, GetScreenWidth() - 1210 * k, 700 * k };
    ui::panel( d );
    ui::text( T( "Auf dem Friedhof" ), d.x + 20 * k, d.y + 16 * k, 32, ui::Accent );
    float dy = d.y + 70 * k;
    for ( const RivalState &rs : m_career.m_rivals )
        if ( !rs.m_alive )
        {
            const RivalDef *rd = g_data.rival( rs.m_key );
            ui::text( "† " + ( rd ? rd->m_name : rs.m_key ) + tf( "  (Tag %d)", rs.m_died ), d.x + 20 * k, dy, 26, ui::Dim );
            dy += 34 * k;
            if ( dy > d.y + d.height - 40 * k ) break;
        }
    ui::text( tf( "Rennen %d  ·  Siege %d  ·  Wracks verursacht %d  ·  selbst verschrottet %d  ·  verdient %s", m_career.m_races, m_career.m_wins,
                  m_career.m_wrecks, m_career.m_wrecked, formatMoney( m_career.m_earned ).c_str() ),
              96 * k, 930 * k, 26, ui::Ink );
    hints( m_in.padUsed() ? T( "A / B zurück" ) : T( "Eingabe / Esc zurück" ) );
    if ( m_in.accept() || m_in.back() )
    {
        if ( m_career.m_over )
        {
            m_inCareer = false;
            m_audio.music( "title" );
            go( ScreenE::TITLE );
        }
        else
            go( ScreenE::HUB );
    }
}

}   // namespace grave
