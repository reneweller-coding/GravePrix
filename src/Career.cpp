/**
 * @file Career.cpp
 * @brief The career: leagues, offers, money, the garage, the black market, the loan shark, the news.
 * @author Rene Weller
 */
#include "Career.h"

#include "Files.h"
#include "Lang.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <tuple>

namespace grave
{

namespace
{

const float PrizeShare[6] = { 1.0f, 0.6f, 0.4f, 0.25f, 0.15f, 0.1f };

int rearOfGear( GearE k )
{
    switch ( k )
    {
    case GearE::MINE: return (int)RearE::MINE;
    case GearE::SPIKES: return (int)RearE::SPIKES;
    case GearE::OIL: return (int)RearE::OIL;
    case GearE::SMOKE: return (int)RearE::SMOKE;
    default: return -1;
    }
}

std::string replaceAll( std::string s, const std::string &from, const std::string &to )
{
    for ( size_t p = s.find( from ); p != std::string::npos; p = s.find( from, p + to.size() ) ) s.replace( p, from.size(), to );
    return s;
}

}   // namespace

std::string formatMoney( long long m )
{
    // 12345 -> "12.345 $" (German) or "$12,345".
    std::string digits = std::to_string( std::llabs( m ) ), out;
    for ( size_t i = 0; i < digits.size(); ++i )
    {
        if ( i && ( digits.size() - i ) % 3 == 0 ) out += english() ? "," : ".";
        out += digits[i];
    }
    if ( m < 0 ) out = "-" + out;
    return english() ? "$" + out : out + " $";
}

Rng Career::rng( uint32_t salt ) const
{
    Rng r;
    r.seed( m_seed * 2654435761u + (uint32_t)m_day * 40503u + salt );
    return r;
}

void Career::start( const std::string &name, const Paint &paint, int difficulty, uint32_t seed )
{
    *this = Career();
    m_name = name.empty() ? std::string( "Fahrer" ) : name;
    m_paint = paint;
    m_difficulty = difficulty;
    m_seed = seed ? seed : 1;
    m_league = 5;
    m_money = 1500;
    m_car = "rustbucket";
    if ( !g_data.car( m_car ) && !g_data.m_carOrder.empty() ) m_car = g_data.m_carOrder.front();
    m_rear[(int)RearE::MINE] = 2;
    m_nitro = 1;
    for ( const std::string &k : g_data.m_rivalOrder )
    {
        const RivalDef &d = g_data.m_rivals.at( k );
        RivalState      r;
        r.m_key = k;
        r.m_league = d.m_league;
        r.m_active = !d.m_rookie;
        m_rivals.push_back( r );
    }
    m_story = "intro";
    newDay();
}

const RivalState *Career::rival( const std::string &key ) const
{
    for ( const RivalState &r : m_rivals )
        if ( r.m_key == key ) return &r;
    return nullptr;
}

std::vector<std::string> Career::roster( int league ) const
{
    std::vector<std::string> out;
    for ( const RivalState &r : m_rivals )
        if ( r.m_active && r.m_alive && r.m_league == league ) out.push_back( r.m_key );
    return out;
}

float Career::rivalStrength( const std::string &key ) const
{
    const RivalDef *d = g_data.rival( key );
    return d ? d->m_skill * 0.6f + d->m_aggression * 0.15f + d->m_weapons * 0.25f + ( d->m_champion ? 0.2f : 0.0f ) : 0.5f;
}

int Career::purse() const
{
    const LeagueDef *l = g_data.league( m_league );
    return l ? l->m_purse : 1000 * ( 6 - m_league );
}

void Career::newDay()
{
    m_offers.clear();
    const LeagueDef *l = g_data.league( m_league );
    Rng              r = rng( 17 );
    // The collectors.
    if ( m_loan > 0 && m_day > m_loanDue )
    {
        long long take = std::min<long long>( m_money, m_loan );
        if ( take > 0 )
        {
            m_money -= take;
            m_loan -= take;
        }
        if ( m_loan > 0 ) m_damage = std::max( m_damage, 0.6f );
        m_loanDue = m_day + 3;
        news( "loan_overdue", "", "", 0, take );
    }
    if ( m_league == 0 || !l ) return;
    std::vector<std::string> races, arenas;
    for ( const std::string &t : g_data.m_trackOrder )
    {
        const TrackDef &d = g_data.m_tracks.at( t );
        if ( t.rfind( "test_", 0 ) == 0 ) continue;
        if ( std::find( l->m_venues.begin(), l->m_venues.end(), d.m_venue ) == l->m_venues.end() ) continue;
        ( d.m_arena ? arenas : races ).push_back( t );
    }
    if ( races.empty() )
        for ( const std::string &t : g_data.m_trackOrder )
            if ( !g_data.m_tracks.at( t ).m_arena && t.rfind( "test_", 0 ) != 0 ) races.push_back( t );
    if ( races.empty() ) return;
    std::vector<std::string> own = roster( m_league );
    std::sort( own.begin(), own.end(), [&]( const std::string &a, const std::string &b ) { return rivalStrength( a ) < rivalStrength( b ); } );
    std::vector<std::string> below = roster( m_league + 1 ), above = roster( m_league - 1 );
    int derbyTier = !arenas.empty() && r.chance( 0.35f ) ? ( r.chance( 0.5f ) ? 0 : 2 ) : -1;
    std::string lastTrack;
    for ( int tier = 0; tier < 3; ++tier )
    {
        Offer o;
        o.m_tier = tier;
        o.m_derby = tier == derbyTier;
        const std::vector<std::string> &pool = o.m_derby ? arenas : races;
        do
            o.m_track = pool[r.below( (int)pool.size() )];
        while ( pool.size() > 1 && o.m_track == lastTrack );
        lastTrack = o.m_track;
        const TrackDef &td = g_data.m_tracks.at( o.m_track );
        o.m_reverse = td.m_reverse && r.chance( 0.4f );
        o.m_laps = o.m_derby ? 1 : td.m_laps;
        o.m_purse = (int)( l->m_purse * ( tier == 0 ? 0.5f : tier == 1 ? 1.0f : 2.2f ) );
        o.m_fee = tier == 0 ? 0 : tier == 1 ? l->m_entry : l->m_entry * 3;
        // The field: the weak and guests from below, the league, the strong and guests from above.
        std::vector<std::string> f;
        if ( tier == 0 )
        {
            for ( size_t k = 0; k < own.size() && f.size() < 3; ++k ) f.push_back( own[k] );
            for ( size_t k = 0; k < below.size() && f.size() < 5; ++k ) f.push_back( below[( k + m_day ) % below.size()] );
        }
        else if ( tier == 2 )
        {
            for ( size_t k = own.size(); k-- > 0 && f.size() < 3; ) f.push_back( own[k] );
            for ( size_t k = 0; k < above.size() && f.size() < 5; ++k ) f.push_back( above[( k + m_day ) % above.size()] );
        }
        for ( const std::string &k : own )
            if ( f.size() < 5 && std::find( f.begin(), f.end(), k ) == f.end() ) f.push_back( k );
        o.m_rivals = f;
        m_offers.push_back( o );
    }
}

CarSetup Career::rivalSetup( const std::string &key, int tier ) const
{
    const RivalDef *d = g_data.rival( key );
    CarSetup        s;
    s.m_rival = key;
    s.m_driver = d ? d->m_name : key;
    s.m_car = d && g_data.car( d->m_car ) ? d->m_car : m_car;
    if ( d )
        for ( int k = 0; k < 3; ++k ) s.m_paint.m_c[k] = d->m_colors[k];
    s.m_skill = d ? d->m_skill : 0.5f;
    s.m_aggression = d ? d->m_aggression : 0.5f;
    s.m_weapons = d ? d->m_weapons : 0.5f;
    s.m_likes = d ? d->m_likes : "mine";
    const RivalState    *st = rival( key );
    int                  league = st ? st->m_league : m_league;
    const DifficultyDef &dif = g_data.difficulty( m_difficulty );
    const CarDef        *cd = g_data.car( s.m_car );
    int base = std::max( 0, 5 - league );
    // They grow while the player stays in a league, but only so far: no spiral for someone who struggles.
    int level = std::clamp( (int)std::lround( ( base * 0.6f + std::min( m_leagueDay, 20 ) * 0.08f ) * dif.m_growth + ( tier - 1 ) * 0.5f ), 0, 6 );
    if ( d && d->m_champion ) level = std::min( 6, level + 1 );
    if ( league == 0 ) level = 6;
    s.m_engine = cd ? std::min( level, cd->m_upEngine ) : 0;
    s.m_tires = cd ? std::min( level, cd->m_upTires ) : 0;
    s.m_armor = cd ? std::min( level, cd->m_upArmor ) : 0;
    s.m_gunLv = cd ? std::min( level, cd->m_upGun ) : 0;
    // Gear by league and taste.
    int rich = 1 + ( 5 - std::min( 5, league ) ) / 2 + tier;
    s.m_rear[(int)RearE::MINE] = 1 + rich / 2;
    if ( s.m_likes == "mine" ) s.m_rear[(int)RearE::MINE] += 2;
    if ( s.m_likes == "oil" ) s.m_rear[(int)RearE::OIL] = 2 + rich / 2;
    if ( s.m_likes == "spikes" ) s.m_rear[(int)RearE::SPIKES] = 1 + rich / 2;
    if ( s.m_likes == "smoke" ) s.m_rear[(int)RearE::SMOKE] = 1 + rich / 2;
    if ( league <= 3 || s.m_likes == "rocket" ) s.m_rockets = 1 + rich / 2;
    s.m_nitro = s.m_likes == "nitro" ? 3 : 1 + ( league <= 3 ? 1 : 0 );
    s.m_ram = league <= 3 && s.m_aggression > 0.6f;
    s.m_plating = league <= 2 ? 1 : 0;
    if ( key == m_sabotaged ) s.m_hpShare = 0.5f;
    return s;
}

CarSetup Career::playerSetup() const
{
    CarSetup s;
    s.m_car = m_car;
    s.m_driver = m_name;
    s.m_paint = m_paint;
    s.m_human = true;
    s.m_player = true;
    s.m_engine = m_up[(int)UpE::ENGINE];
    s.m_tires = m_up[(int)UpE::TIRES];
    s.m_armor = m_up[(int)UpE::ARMOR];
    s.m_gunLv = m_up[(int)UpE::GUN];
    s.m_gun = m_gun;
    for ( int k = 0; k < (int)RearE::COUNT; ++k ) s.m_rear[k] = m_rear[k];
    s.m_rockets = m_rockets;
    s.m_nitro = m_nitro;
    s.m_ram = m_ram;
    s.m_plating = m_plating;
    s.m_hpShare = clampf( 1.0f - m_damage, 0.05f, 1.0f );
    return s;
}

std::vector<CarSetup> Career::field( const Offer &o, int &player ) const
{
    std::vector<std::string> rivals = o.m_rivals;
    // The best in the table start at the front.
    std::sort( rivals.begin(), rivals.end(), [&]( const std::string &a, const std::string &b ) {
        const RivalState *ra = rival( a ), *rb = rival( b );
        int               pa = ra ? ra->m_points : 0, pb = rb ? rb->m_points : 0;
        return pa != pb ? pa > pb : rivalStrength( a ) > rivalStrength( b );
    } );
    std::vector<CarSetup> out;
    for ( const std::string &k : rivals ) out.push_back( rivalSetup( k, o.m_tier ) );
    int pos = (int)out.size();
    if ( !o.m_showdown && !o.m_final )
    {
        int r = rank();
        pos = std::clamp( r - 1, 0, (int)out.size() );
        if ( o.m_tier == 2 ) pos = (int)out.size();
    }
    out.insert( out.begin() + pos, playerSetup() );
    player = pos;
    return out;
}

bool Career::enter( const Offer &o )
{
    if ( m_money < o.m_fee ) return false;
    m_money -= o.m_fee;
    return true;
}

std::vector<std::tuple<std::string, int, std::string>> Career::table() const
{
    std::vector<std::tuple<std::string, int, std::string>> t;
    int myPoints = 0;
    for ( const RivalState &r : m_rivals )
        if ( r.m_active && r.m_alive && r.m_league == m_league )
        {
            const RivalDef *d = g_data.rival( r.m_key );
            t.push_back( { d ? d->m_name : r.m_key, r.m_points, r.m_key } );
        }
    for ( const RivalState &r : m_rivals )
        if ( r.m_key == "_player" ) myPoints = r.m_points;
    t.push_back( { m_name, myPoints, "" } );
    std::stable_sort( t.begin(), t.end(), []( const auto &a, const auto &b ) { return std::get<1>( a ) > std::get<1>( b ); } );
    return t;
}

int Career::rank() const
{
    auto t = table();
    for ( size_t i = 0; i < t.size(); ++i )
        if ( std::get<2>( t[i] ).empty() ) return (int)i + 1;
    return (int)t.size();
}

bool Career::showdownReady() const
{
    if ( m_over ) return false;
    if ( m_league == 0 ) return true;
    const LeagueDef *l = g_data.league( m_league );
    auto             t = table();
    int              mine = 0;
    for ( auto &e : t )
        if ( std::get<2>( e ).empty() ) mine = std::get<1>( e );
    return rank() <= 2 && mine >= ( l ? l->m_showdown : 40 );
}

Offer Career::showdown() const
{
    Offer o;
    o.m_showdown = m_league > 0;
    o.m_final = m_league == 0;
    o.m_tier = 2;
    if ( m_league == 0 )
    {
        o.m_track = g_data.track( "est_finale" ) ? "est_finale" : "";
        o.m_rivals = { "mortimer_graves" };
        o.m_purse = g_data.league( 0 ) ? g_data.league( 0 )->m_purse : 150000;
    }
    else
    {
        const LeagueDef *l = g_data.league( m_league );
        for ( const RivalState &r : m_rivals )
        {
            const RivalDef *d = g_data.rival( r.m_key );
            if ( d && d->m_champion && r.m_league == m_league && r.m_alive ) o.m_rivals = { r.m_key };
        }
        // The longest race track of the league.
        float best = 0;
        for ( const std::string &t : g_data.m_trackOrder )
        {
            const TrackDef &d = g_data.m_tracks.at( t );
            if ( d.m_arena || !l || std::find( l->m_venues.begin(), l->m_venues.end(), d.m_venue ) == l->m_venues.end() ) continue;
            float len = 0;
            for ( const TrackPiece &p : d.m_pieces ) len += p.m_kind == TrackPiece::STRAIGHT ? p.m_len : p.m_r * p.m_deg * Deg;
            if ( len > best )
            {
                best = len;
                o.m_track = t;
            }
        }
        o.m_purse = ( l ? l->m_purse : 1000 ) * 4;   // enough for the next class of car
    }
    if ( o.m_track.empty() && !g_data.m_trackOrder.empty() ) o.m_track = g_data.m_trackOrder.front();
    const TrackDef *td = g_data.track( o.m_track );
    o.m_laps = td ? td->m_laps + 1 : 4;
    return o;
}

RaceReport Career::finish( const Offer &o, const World &w, int player )
{
    RaceReport           rep;
    const DifficultyDef &dif = g_data.difficulty( m_difficulty );
    const LeagueDef     *l = g_data.league( m_league );
    std::vector<int>     order = w.order();
    const Car           &me = w.m_cars[player];
    Rng                  r = rng( 99 );
    std::string          trackTitle = g_data.track( o.m_track ) ? g_data.track( o.m_track )->m_title.get() : o.m_track;
    // Points and prizes.
    for ( size_t p = 0; p < order.size(); ++p )
    {
        const Car &c = w.m_cars[order[p]];
        ResultLine line;
        line.m_name = c.m_setup.m_driver;
        line.m_car = c.m_def ? c.m_def->m_title.get() : c.m_setup.m_car;
        line.m_rival = c.m_setup.m_rival;
        line.m_place = (int)p + 1;
        line.m_wrecked = c.m_wrecked;
        line.m_player = order[p] == player;
        line.m_time = c.m_finished ? c.m_finishTime : 0.0f;
        line.m_wrecks = c.m_wrecks;
        bool counts = !( c.m_wrecked && w.m_mode == ModeE::RACE ) && c.m_dist > -1e8f;   // not wrecked, not given up
        line.m_prize = counts && p < 6 ? (int)( o.m_purse * PrizeShare[p] * ( line.m_player ? dif.m_prize : 1.0f ) ) : 0;
        if ( l && counts && p < l->m_points.size() && !o.m_showdown && !o.m_final ) line.m_points = l->m_points[p];
        rep.m_lines.push_back( line );
    }
    for ( ResultLine &line : rep.m_lines )
    {
        if ( line.m_player )
        {
            rep.m_place = line.m_place;
            rep.m_prize = line.m_prize;
            rep.m_points = line.m_points;
            continue;
        }
        RivalState *rs = nullptr;
        for ( RivalState &s : m_rivals )
            if ( s.m_key == line.m_rival ) rs = &s;
        if ( !rs ) continue;
        ++rs->m_races;
        if ( line.m_place == 1 ) ++rs->m_wins;
        rs->m_wrecks += line.m_wrecks;
        if ( rs->m_league == m_league ) rs->m_points += line.m_points;
    }
    rep.m_wrecked = me.m_wrecked;
    float bountyMul = me.m_def ? me.m_def->m_bounty : 1.0f;
    rep.m_bounty = (int)( me.m_wrecks * o.m_purse * 0.1f * bountyMul );
    rep.m_cash = (int)( me.m_cash * o.m_purse * 0.04f );
    rep.m_lap = w.m_bestLapCar == player ? (int)( o.m_purse * 0.1f ) : 0;
    if ( const SponsorDef *sp = g_data.sponsor( m_sponsor ) )
    {
        auto bonus = [&]( const char *k ) { auto it = sp->m_bonus.find( k ); return it != sp->m_bonus.end() ? it->second : 0; };
        if ( rep.m_place == 1 && !me.m_wrecked ) rep.m_sponsor += bonus( "win" );
        if ( rep.m_place <= 3 && !me.m_wrecked ) rep.m_sponsor += bonus( "podium" );
        rep.m_sponsor += bonus( "wreck" ) * me.m_wrecks;
        if ( me.m_damage < me.m_maxHp * 0.05f && !me.m_wrecked ) rep.m_sponsor += bonus( "clean" );
        if ( rep.m_lap > 0 ) rep.m_sponsor += bonus( "lap" );
        if ( me.m_shots == 0 && !me.m_wrecked ) rep.m_sponsor += bonus( "nogun" );
    }
    rep.m_total = rep.m_prize + rep.m_bounty + rep.m_cash + rep.m_lap + rep.m_sponsor;
    m_money += rep.m_total;
    m_earned += rep.m_total;
    for ( RivalState &s : m_rivals )
        if ( s.m_key == "_player" ) s.m_points += rep.m_points;
    if ( std::none_of( m_rivals.begin(), m_rivals.end(), []( const RivalState &s ) { return s.m_key == "_player"; } ) )
    {
        RivalState me2;
        me2.m_key = "_player";
        me2.m_points = rep.m_points;
        me2.m_league = m_league;
        m_rivals.push_back( me2 );
    }
    // The car comes back as it is; what was used is gone.
    m_damage = me.m_wrecked ? 1.0f : clampf( 1.0f - me.m_hp / std::max( 1.0f, me.m_maxHp ), 0, 1 );
    for ( int k = 0; k < (int)RearE::COUNT; ++k ) m_rear[k] = std::min( m_rear[k], me.m_rear[k] );
    m_rockets = std::min( m_rockets, me.m_rockets );
    m_nitro = std::min( m_nitro, me.m_nitro );
    ++m_races;
    if ( rep.m_place == 1 && !me.m_wrecked ) ++m_wins;
    m_wrecks += me.m_wrecks;
    if ( me.m_wrecked ) ++m_wrecked;
    // The dead.
    for ( size_t i = 0; i < w.m_cars.size(); ++i )
    {
        const Car &c = w.m_cars[i];
        if ( !c.m_wrecked || c.m_setup.m_rival.empty() || (int)i == player ) continue;
        const RivalDef *d = g_data.rival( c.m_setup.m_rival );
        if ( !d || d->m_champion ) continue;   // champions have more lives than a cat
        float chance = dif.m_deaths * 0.3f * ( c.m_wreckedBy == player ? 1.0f : 0.6f );
        if ( !r.chance( chance ) ) continue;
        for ( RivalState &s : m_rivals )
            if ( s.m_key == c.m_setup.m_rival && s.m_alive )
            {
                s.m_alive = false;
                s.m_died = m_day;
                rep.m_dead.push_back( s.m_key );
                for ( ResultLine &line : rep.m_lines )
                    if ( line.m_rival == s.m_key ) line.m_died = true;
                news( "rival_dead", s.m_key, trackTitle, 0, 0 );
                // A rookie takes the seat.
                RivalState *rookie = nullptr;
                for ( RivalState &k : m_rivals )
                {
                    const RivalDef *rd = g_data.rival( k.m_key );
                    if ( k.m_active || !k.m_alive || !rd || !rd->m_rookie ) continue;
                    if ( !rookie || std::abs( rd->m_league - s.m_league ) < std::abs( g_data.rival( rookie->m_key )->m_league - s.m_league ) )
                        rookie = &k;
                }
                if ( rookie )
                {
                    rookie->m_active = true;
                    rookie->m_league = s.m_league;
                    rookie->m_points = s.m_points / 2;
                    news( "rookie", rookie->m_key, trackTitle, 0, 0 );
                }
            }
    }
    // The rest of the league raced elsewhere.
    for ( RivalState &s : m_rivals )
    {
        if ( !s.m_active || !s.m_alive || s.m_league != m_league || s.m_key == "_player" ) continue;
        if ( std::find( o.m_rivals.begin(), o.m_rivals.end(), s.m_key ) != o.m_rivals.end() ) continue;
        float strength = rivalStrength( s.m_key );
        int   place = std::clamp( (int)std::round( 1 + ( 1.0f - strength ) * 4.0f + r.range( -1.5f, 1.5f ) ), 1, 6 );
        if ( l && place - 1 < (int)l->m_points.size() ) s.m_points += l->m_points[place - 1];
    }
    // The news of the day.
    std::string first = rep.m_lines.empty() ? "" : rep.m_lines[0].m_rival;
    if ( me.m_wrecked )
        news( "player_wrecked", me.m_wreckedBy >= 0 ? w.m_cars[me.m_wreckedBy].m_setup.m_rival : first, trackTitle, rep.m_place, rep.m_total );
    else if ( rep.m_place == 1 )
        news( "player_win", rep.m_lines.size() > 1 ? rep.m_lines[1].m_rival : "", trackTitle, 1, rep.m_total );
    else if ( rep.m_place <= 3 )
        news( "player_podium", first, trackTitle, rep.m_place, rep.m_total );
    else
        news( "player_lose", first, trackTitle, rep.m_place, rep.m_total );
    if ( me.m_wrecks >= 3 ) news( "player_spree", "", trackTitle, rep.m_place, rep.m_bounty );
    if ( me.m_damage < me.m_maxHp * 0.05f && !me.m_wrecked && r.chance( 0.5f ) ) news( "player_clean", "", trackTitle, rep.m_place, 0 );
    if ( !m_sabotaged.empty() )
    {
        if ( r.chance( 0.2f ) )
        {
            long long fine = purse() / 2;
            m_money -= fine;
            news( "sabotage_caught", m_sabotaged, trackTitle, 0, fine );
        }
        else
            news( "sabotage_done", m_sabotaged, trackTitle, 0, 0 );
        m_sabotaged.clear();
    }
    // Showdowns and the final.
    if ( o.m_showdown || o.m_final )
    {
        bool won = rep.m_place == 1 && !me.m_wrecked;
        if ( o.m_final )
        {
            if ( won )
            {
                rep.m_won = true;
                m_won = m_over = true;
                rep.m_story = "ending";
                news( "boss_won", "mortimer_graves", trackTitle, 1, rep.m_total );
            }
            else
                news( "showdown_lost", "mortimer_graves", trackTitle, rep.m_place, 0 );
        }
        else if ( won )
        {
            rep.m_story = "champion_" + std::to_string( m_league );
            news( "showdown_won", o.m_rivals.empty() ? "" : o.m_rivals[0], trackTitle, 1, rep.m_total );
            promote();
            rep.m_promoted = true;
        }
        else
            news( "showdown_lost", o.m_rivals.empty() ? "" : o.m_rivals[0], trackTitle, rep.m_place, 0 );
    }
    ads( 2 );
    // The loan grows overnight.
    if ( m_loan > 0 ) m_loan = (long long)std::ceil( m_loan * ( 1.0 + dif.m_interest ) );
    ++m_day;
    ++m_leagueDay;
    // A season is SeasonDays race days: then the league's table starts afresh (a bad start is not for ever).
    if ( m_leagueDay % SeasonDays == 0 && !rep.m_promoted )
    {
        for ( RivalState &s : m_rivals )
            if ( s.m_league == m_league || s.m_key == "_player" ) s.m_points = 0;
        rep.m_seasonOver = true;
    }
    newDay();
    if ( !solvent() )
    {
        m_over = true;
        if ( rep.m_story.empty() ) rep.m_story = "bankrupt";
    }
    return rep;
}

void Career::promote()
{
    m_league = std::max( 0, m_league - 1 );
    m_leagueDay = 0;
    for ( RivalState &s : m_rivals )
        if ( s.m_key == "_player" ) s.m_points = 0;
    // The league starts afresh for the newcomer.
    for ( RivalState &s : m_rivals )
        if ( s.m_league == m_league ) s.m_points = 0;
    m_story = m_league == 0 ? "boss" : "league_" + std::to_string( m_league );
    news( "league_up", "", "", 0, 0 );
}

bool Career::solvent() const
{
    if ( m_damage < 0.95f ) return true;
    return m_money >= repairCost() / 3 || m_loan < loanLimit();
}

// ---------------------------------------------------------------------------------------------------- the garage

int Career::carPrice( const std::string &car ) const
{
    const CarDef *c = g_data.car( car );
    return c ? (int)( c->m_price * g_data.difficulty( m_difficulty ).m_prices ) : 0;
}

int Career::sellValue() const
{
    float worth = m_carPaid * 0.5f + m_upPaid * 0.3f;
    return (int)( worth * ( 1.0f - 0.5f * m_damage ) );
}

bool Career::buyCar( const std::string &car )
{
    const CarDef *c = g_data.car( car );
    if ( !c || car == m_car || c->m_class >= 6 ) return false;
    int price = carPrice( car ), back = sellValue();
    if ( m_money + back < price ) return false;
    m_money += back - price;
    // The gun is bolted on: it moves to the new car with its upgrades (as far as the new car takes them).
    int gunLv = m_up[(int)UpE::GUN];
    m_car = car;
    m_carPaid = price;
    m_upPaid = 0;
    for ( int &u : m_up ) u = 0;
    m_up[(int)UpE::GUN] = std::min( gunLv, c->m_upGun );
    m_damage = 0;
    m_ram = false;
    m_plating = 0;
    news( "bought_car", "", "", 0, price );
    return true;
}

int Career::upgradeLevels( UpE u ) const
{
    const CarDef *c = g_data.car( m_car );
    if ( !c ) return 0;
    switch ( u )
    {
    case UpE::ENGINE: return c->m_upEngine;
    case UpE::TIRES: return c->m_upTires;
    case UpE::ARMOR: return c->m_upArmor;
    default: return c->m_upGun;
    }
}

int Career::upgradeLevel( UpE u ) const { return m_up[(int)u]; }

int Career::upgradeCost( UpE u ) const
{
    static const char *names[] = { "engine", "tires", "armor", "gun" };
    int lv = m_up[(int)u];
    if ( lv >= upgradeLevels( u ) ) return -1;
    const UpgradeDef *d = g_data.upgrade( names[(int)u] );
    const CarDef     *c = g_data.car( m_car );
    int               base = d && lv < (int)d->m_cost.size() ? d->m_cost[lv] : 500 * ( lv + 1 ) * ( lv + 1 );
    return (int)( base * ( c ? c->m_upgradeCost : 1.0f ) * g_data.difficulty( m_difficulty ).m_prices );
}

bool Career::upgrade( UpE u )
{
    int cost = upgradeCost( u );
    if ( cost < 0 || m_money < cost ) return false;
    m_money -= cost;
    m_upPaid += cost;
    ++m_up[(int)u];
    return true;
}

int Career::repairCost() const
{
    const CarDef *c = g_data.car( m_car );
    float         worth = 300.0f + ( c ? c->m_price : 0 ) * 0.05f + m_upPaid * 0.03f;
    return (int)std::ceil( m_damage * worth * g_data.difficulty( m_difficulty ).m_repair );
}

bool Career::repair()
{
    int cost = repairCost();
    if ( cost <= 0 ) return false;
    if ( m_money >= cost )
    {
        m_money -= cost;
        m_damage = 0;
        return true;
    }
    // As far as the money goes.
    if ( m_money <= 0 ) return false;
    m_damage = clampf( m_damage * ( 1.0f - (float)m_money / cost ), 0, 1 );
    m_money = 0;
    return true;
}

int Career::gunPrice( const std::string &gun ) const
{
    const GunDef *g = g_data.gun( gun );
    return g ? (int)( g->m_price * g_data.difficulty( m_difficulty ).m_prices ) : 0;
}

bool Career::buyGun( const std::string &gun )
{
    const GunDef *g = g_data.gun( gun );
    if ( !g ) return false;
    const CarDef *c = g_data.car( m_car );
    std::string   current = m_gun.empty() && c ? c->m_gun : m_gun;
    if ( current == gun ) return false;
    int price = gunPrice( gun );
    if ( m_money < price ) return false;
    m_money -= price;
    m_gun = gun;
    return true;
}

// ---------------------------------------------------------------------------------------------------- the black market

int Career::gearPrice( const std::string &gear ) const
{
    const GearDef *g = g_data.gear( gear );
    return g ? (int)( g->m_price * g_data.difficulty( m_difficulty ).m_prices ) : 0;
}

int Career::gearCount( const std::string &gear ) const
{
    const GearDef *g = g_data.gear( gear );
    if ( !g ) return 0;
    int rear = rearOfGear( g->m_kind );
    if ( rear >= 0 ) return m_rear[rear];
    switch ( g->m_kind )
    {
    case GearE::ROCKET: return m_rockets;
    case GearE::NITRO: return m_nitro;
    case GearE::RAM: return m_ram ? 1 : 0;
    case GearE::PLATING: return m_plating;
    default: return 0;
    }
}

int Career::gearMax( const std::string &gear ) const
{
    const GearDef *g = g_data.gear( gear );
    if ( !g ) return 0;
    if ( g->m_kind == GearE::RAM ) return 1;
    return g->m_max;
}

bool Career::buyGear( const std::string &gear )
{
    const GearDef *g = g_data.gear( gear );
    if ( !g ) return false;
    int price = gearPrice( gear );
    if ( m_money < price || gearCount( gear ) >= gearMax( gear ) ) return false;
    m_money -= price;
    int add = std::min( g->m_pack, gearMax( gear ) - gearCount( gear ) );
    int rear = rearOfGear( g->m_kind );
    if ( rear >= 0 )
        m_rear[rear] += add;
    else if ( g->m_kind == GearE::ROCKET )
        m_rockets += add;
    else if ( g->m_kind == GearE::NITRO )
        m_nitro += add;
    else if ( g->m_kind == GearE::RAM )
        m_ram = true;
    else if ( g->m_kind == GearE::PLATING )
        m_plating += 1;
    return true;
}

int Career::sabotagePrice() const { return (int)( purse() * 0.3f * g_data.difficulty( m_difficulty ).m_prices ); }

bool Career::sabotage( const std::string &rival )
{
    int price = sabotagePrice();
    if ( m_money < price || !m_sabotaged.empty() ) return false;
    m_money -= price;
    m_sabotaged = rival;
    return true;
}

// ---------------------------------------------------------------------------------------------------- the loan shark

int Career::loanLimit() const { return std::max( 1000, purse() * 3 ); }

bool Career::borrow( int amount )
{
    if ( amount <= 0 || m_loan + amount > loanLimit() ) return false;
    if ( m_loan == 0 ) m_loanDue = m_day + 6;
    m_loan += amount;
    m_money += amount;
    return true;
}

bool Career::repay( int amount )
{
    amount = (int)std::min<long long>( amount, std::min( m_loan, m_money ) );
    if ( amount <= 0 ) return false;
    m_loan -= amount;
    m_money -= amount;
    if ( m_loan == 0 )
    {
        m_loanDue = 0;
        news( "loan_paid", "", "", 0, amount );
    }
    return true;
}

std::vector<std::string> Career::sponsors() const
{
    std::vector<std::string> out;
    for ( const auto &[k, s] : g_data.m_sponsors )
        if ( s.m_league >= m_league ) out.push_back( k );
    return out;
}

void Career::sign( const std::string &sponsor ) { m_sponsor = sponsor; }

// ---------------------------------------------------------------------------------------------------- the news

std::string Career::fill( const std::string &text, const std::string &rivalKey, const std::string &track, int place,
                          long long amount ) const
{
    const RivalDef  *d = g_data.rival( rivalKey );
    const CarDef    *c = g_data.car( m_car );
    const LeagueDef *l = g_data.league( m_league );
    std::string      s = text;
    s = replaceAll( s, "{player}", m_name );
    s = replaceAll( s, "{rival}", d ? d->m_name : T( "ein Unbekannter" ) );
    s = replaceAll( s, "{track}", track );
    s = replaceAll( s, "{venue}", track );
    s = replaceAll( s, "{car}", c ? c->m_title.get() : m_car );
    s = replaceAll( s, "{place}", std::to_string( place ) );
    s = replaceAll( s, "{money}", formatMoney( amount ) );
    s = replaceAll( s, "{league}", l ? l->m_title.get() : "" );
    s = replaceAll( s, "{days}", std::to_string( m_day ) );
    return s;
}

void Career::news( const std::string &event, const std::string &rivalKey, const std::string &track, int place, long long amount )
{
    std::vector<const NewsDef *> all = g_data.news( event );
    if ( all.empty() ) return;
    Rng            r = rng( (uint32_t)( m_news.size() * 131 + event.size() * 7 ) );
    const NewsDef *n = all[r.below( (int)all.size() )];
    NewsItem       it;
    it.m_head = fill( n->m_head.get(), rivalKey, track, place, amount );
    it.m_text = fill( n->m_text.get(), rivalKey, track, place, amount );
    // An obituary line from the rival themself.
    if ( event == "rival_dead" )
        if ( const RivalDef *d = g_data.rival( rivalKey ) )
            if ( !d->m_obit.empty() ) it.m_text += " " + d->m_obit.get();
    // The news of the new day replace the old ones; this race's items come first.
    if ( m_news.size() > 12 ) m_news.erase( m_news.begin() );
    m_news.push_back( it );
}

void Career::ads( int n )
{
    if ( g_data.m_ads.empty() ) return;
    Rng r = rng( 555 );
    for ( int k = 0; k < n; ++k )
    {
        NewsItem it;
        it.m_ad = true;
        it.m_text = fill( g_data.m_ads[r.below( (int)g_data.m_ads.size() )].get(), "", "", 0, 0 );
        m_news.push_back( it );
    }
}

// ---------------------------------------------------------------------------------------------------- the file

bool Career::save( const std::string &path ) const
{
    FILE *f = beginWrite( path );
    if ( !f ) return false;
    fprintf( f, "graveprix_career 1\n" );
    fprintf( f, "name %s\n", m_name.c_str() );
    for ( int k = 0; k < 3; ++k ) fprintf( f, "paint %d %d %d %d\n", k, m_paint.m_c[k].r, m_paint.m_c[k].g, m_paint.m_c[k].b );
    fprintf( f, "difficulty %d\nleague %d\nday %d\nleagueday %d\nmoney %lld\ncar %s\ncarpaid %d\nuppaid %d\n", m_difficulty,
             m_league, m_day, m_leagueDay, m_money, m_car.c_str(), m_carPaid, m_upPaid );
    fprintf( f, "up %d %d %d %d\n", m_up[0], m_up[1], m_up[2], m_up[3] );
    fprintf( f, "gun %s\ndamage %.4f\n", m_gun.empty() ? "-" : m_gun.c_str(), m_damage );
    fprintf( f, "rear %d %d %d %d\nrockets %d\nnitro %d\nram %d\nplating %d\n", m_rear[0], m_rear[1], m_rear[2], m_rear[3],
             m_rockets, m_nitro, m_ram ? 1 : 0, m_plating );
    fprintf( f, "loan %lld %d\nsponsor %s\nsabotaged %s\n", m_loan, m_loanDue, m_sponsor.empty() ? "-" : m_sponsor.c_str(),
             m_sabotaged.empty() ? "-" : m_sabotaged.c_str() );
    fprintf( f, "stats %d %d %d %d %d\nover %d %d\nseed %u\nstory %s\n", m_races, m_wins, m_wrecks, m_wrecked, m_earned,
             m_over ? 1 : 0, m_won ? 1 : 0, m_seed, m_story.empty() ? "-" : m_story.c_str() );
    for ( const RivalState &r : m_rivals )
        fprintf( f, "rival %s %d %d %d %d %d %d %d %d\n", r.m_key.c_str(), r.m_active ? 1 : 0, r.m_alive ? 1 : 0, r.m_league,
                 r.m_points, r.m_wins, r.m_races, r.m_wrecks, r.m_died );
    for ( const NewsItem &n : m_news )
    {
        // One line each: head and text separated by a tab.
        std::string head = n.m_head, text = n.m_text;
        for ( std::string *s : { &head, &text } )
            for ( char &c : *s )
                if ( c == '\n' || c == '\t' ) c = ' ';
        fprintf( f, "news %d\t%s\t%s\n", n.m_ad ? 1 : 0, head.c_str(), text.c_str() );
    }
    return endWrite( f, path );
}

bool Career::load( const std::string &path )
{
    std::ifstream in( path );
    if ( !in ) return false;
    Career      c;
    std::string line;
    bool        ok = false;
    while ( std::getline( in, line ) )
    {
        if ( !line.empty() && line.back() == '\r' ) line.pop_back();
        std::istringstream ss( line );
        std::string        key;
        ss >> key;
        if ( key == "graveprix_career" ) ok = true;
        else if ( key == "name" )
        {
            std::getline( ss, c.m_name );
            if ( !c.m_name.empty() && c.m_name[0] == ' ' ) c.m_name.erase( 0, 1 );
        }
        else if ( key == "paint" )
        {
            int k, r, g, b;
            ss >> k >> r >> g >> b;
            if ( k >= 0 && k < 3 ) c.m_paint.m_c[k] = { (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
        }
        else if ( key == "difficulty" ) ss >> c.m_difficulty;
        else if ( key == "league" ) ss >> c.m_league;
        else if ( key == "day" ) ss >> c.m_day;
        else if ( key == "leagueday" ) ss >> c.m_leagueDay;
        else if ( key == "money" ) ss >> c.m_money;
        else if ( key == "car" ) ss >> c.m_car;
        else if ( key == "carpaid" ) ss >> c.m_carPaid;
        else if ( key == "uppaid" ) ss >> c.m_upPaid;
        else if ( key == "up" ) ss >> c.m_up[0] >> c.m_up[1] >> c.m_up[2] >> c.m_up[3];
        else if ( key == "gun" )
        {
            ss >> c.m_gun;
            if ( c.m_gun == "-" ) c.m_gun.clear();
        }
        else if ( key == "damage" ) ss >> c.m_damage;
        else if ( key == "rear" ) ss >> c.m_rear[0] >> c.m_rear[1] >> c.m_rear[2] >> c.m_rear[3];
        else if ( key == "rockets" ) ss >> c.m_rockets;
        else if ( key == "nitro" ) ss >> c.m_nitro;
        else if ( key == "ram" )
        {
            int v;
            ss >> v;
            c.m_ram = v != 0;
        }
        else if ( key == "plating" ) ss >> c.m_plating;
        else if ( key == "loan" ) ss >> c.m_loan >> c.m_loanDue;
        else if ( key == "sponsor" )
        {
            ss >> c.m_sponsor;
            if ( c.m_sponsor == "-" ) c.m_sponsor.clear();
        }
        else if ( key == "sabotaged" )
        {
            ss >> c.m_sabotaged;
            if ( c.m_sabotaged == "-" ) c.m_sabotaged.clear();
        }
        else if ( key == "stats" ) ss >> c.m_races >> c.m_wins >> c.m_wrecks >> c.m_wrecked >> c.m_earned;
        else if ( key == "over" )
        {
            int a, b;
            ss >> a >> b;
            c.m_over = a != 0;
            c.m_won = b != 0;
        }
        else if ( key == "seed" ) ss >> c.m_seed;
        else if ( key == "story" )
        {
            ss >> c.m_story;
            if ( c.m_story == "-" ) c.m_story.clear();
        }
        else if ( key == "rival" )
        {
            RivalState r;
            int        active, alive;
            ss >> r.m_key >> active >> alive >> r.m_league >> r.m_points >> r.m_wins >> r.m_races >> r.m_wrecks >> r.m_died;
            r.m_active = active != 0;
            r.m_alive = alive != 0;
            c.m_rivals.push_back( r );
        }
        else if ( key == "news" )
        {
            NewsItem    n;
            std::string rest = line.substr( 5 );
            size_t      t1 = rest.find( '\t' ), t2 = t1 == std::string::npos ? t1 : rest.find( '\t', t1 + 1 );
            if ( t2 != std::string::npos )
            {
                n.m_ad = rest[0] == '1';
                n.m_head = rest.substr( t1 + 1, t2 - t1 - 1 );
                n.m_text = rest.substr( t2 + 1 );
                c.m_news.push_back( n );
            }
        }
    }
    if ( !ok ) return false;
    // Rivals new in the data since the save.
    for ( const std::string &k : g_data.m_rivalOrder )
        if ( !c.rival( k ) )
        {
            const RivalDef &d = g_data.m_rivals.at( k );
            RivalState      r;
            r.m_key = k;
            r.m_league = d.m_league;
            r.m_active = !d.m_rookie;
            c.m_rivals.push_back( r );
        }
    *this = c;
    newDay();
    return true;
}

}   // namespace grave
