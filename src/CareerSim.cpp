/**
 * @file CareerSim.cpp
 * @brief --careersim: a career played by the computer without a window (the player's car driven by a computer
 *        driver of middling skill): it repairs, upgrades, buys cars and gear, picks offers, takes the showdowns, and
 *        reports per league how long it took and how the money went. The test that the economy holds together.
 * @author Rene Weller
 */
#include "Career.h"
#include "Tools.h"

#include <algorithm>
#include <cstdio>
#include <tuple>

namespace grave
{

namespace
{

/** @brief Spends money as a sensible player would. */
void shop( Career &c )
{
    long long reserve = c.purse() / 2;
    if ( c.m_damage > 0.25f ) c.repair();
    if ( c.m_loan > 0 && c.m_money > c.purse() ) c.repay( (int)( c.m_money - c.purse() ) );
    // A better car when it can be paid with money to spare.
    std::string best;
    int         bestClass = g_data.car( c.m_car ) ? g_data.car( c.m_car )->m_class : 0;
    for ( const std::string &k : g_data.m_carOrder )
    {
        const CarDef &d = g_data.m_cars.at( k );
        if ( d.m_class <= bestClass || d.m_class >= 6 || d.m_league < c.m_league ) continue;
        if ( c.carPrice( k ) + c.purse() <= c.m_money + c.sellValue() )
        {
            best = k;
            bestClass = d.m_class;
        }
    }
    // Today's used car when it is a class up and leaves half a purse.
    if ( const CarDef *u = g_data.car( c.m_used ) )
        if ( u->m_class > bestClass && c.m_usedPrice + c.purse() / 2 <= c.m_money + c.sellValue() )
        {
            c.buyUsed();
            return;
        }
    if ( !best.empty() ) c.buyCar( best );
    // An outclassed car gets no upgrades: the money goes into the next car.
    const CarDef *now = g_data.car( c.m_car );
    bool          outclassed = now && c.m_league > 0 && now->m_class < 6 - c.m_league;
    for ( int round = 0; round < 8 && !outclassed; ++round )
    {
        // Armour and gun first while they lag behind, then the cheapest.
        int cheapest = -1, cost = 1 << 30;
        for ( int u = 0; u < (int)UpE::COUNT; ++u )
        {
            int k = c.upgradeCost( (UpE)u );
            if ( k < 0 ) continue;
            bool first = ( u == (int)UpE::ARMOR || u == (int)UpE::GUN ) && c.upgradeLevel( (UpE)u ) < 2;
            int  score = first ? k / 4 : k;
            if ( score < cost )
            {
                cost = score;
                cheapest = u;
            }
        }
        if ( cheapest >= 0 ) cost = c.upgradeCost( (UpE)cheapest );
        if ( cheapest < 0 || cost + reserve > c.m_money ) break;
        c.upgrade( (UpE)cheapest );
    }
    for ( const char *g : { "mine", "nitro", "rocket", "oil" } )
        if ( !outclassed && g_data.gear( g ) && c.gearPrice( g ) + reserve * 2 < c.m_money && c.gearCount( g ) < 3 ) c.buyGear( g );
}

}   // namespace

int toolCareerSim( const Options &opt )
{
    int      difficulty = (int)opt.num( "--difficulty", 2 );
    int      maxDays = (int)opt.num( "--days", 160 );
    uint32_t seed = (uint32_t)opt.num( "--seed", 5 );
    Career   c;
    Paint    paint;
    c.start( "Bot", paint, difficulty, seed );
    if ( c.m_offers.empty() )
    {
        printf( "error: no offers on day 1 (no tracks or leagues?)\n" );
        return 1;
    }
    int  leagueStart = 1, lastLeague = c.m_league;
    int  dead = 0, wins = 0, showdowns = 0;
    long long minMoney = c.m_money;
    printf( "day league   money    debt  car            rank  pts  event\n" );
    while ( !c.m_over && c.m_day <= maxDays )
    {
        shop( c );
        if ( c.m_damage >= 0.95f )
        {
            // Too broke to repair: the loan shark.
            c.borrow( std::max( 0, std::min( c.repairCost() - (int)c.m_money, c.loanLimit() - (int)c.m_loan ) ) );
            c.repair();
        }
        Offer o;
        bool  duel = c.showdownReady();
        if ( duel )
        {
            o = c.showdown();
            ++showdowns;
        }
        else
        {
            if ( c.m_offers.empty() ) break;
            int pick = 1;
            if ( c.m_damage > 0.5f || c.m_money < c.m_offers[1].m_fee ) pick = 0;
            else if ( c.m_money > c.purse() * 3 && c.m_money >= c.m_offers[2].m_fee ) pick = 2;
            if ( c.m_offers[(size_t)pick].m_derby ) pick = pick == 0 ? 1 : 0;   // derbies are for the brave
            o = c.m_offers[(size_t)std::min( pick, (int)c.m_offers.size() - 1 )];
            if ( !c.enter( o ) ) o = c.m_offers[0];
        }
        const TrackDef *def = g_data.track( o.m_track );
        if ( !def ) break;
        Track                    t;
        std::vector<std::string> e, w;
        if ( !t.build( *def, g_data.venue( def->m_venue ), o.m_reverse, false, e, w ) ) break;
        if ( !o.m_derby ) t.m_laps = o.m_laps;
        int  player = 0;
        auto field = c.field( o, player );
        field[player].m_human = false;   // driven by a computer driver of middling skill
        field[player].m_skill = (float)opt.num( "--skill", 0.85f );
        field[player].m_novice = (float)opt.num( "--novice", 0.0f );
        field[player].m_aggression = 0.5f;
        field[player].m_weapons = 0.7f;
        World world;
        world.init( t, field, o.m_derby ? ModeE::DERBY : ModeE::RACE, difficulty, seed + (uint32_t)c.m_day * 77 );
        std::vector<CarInput> none( field.size() );
        while ( world.m_phase != World::PhaseE::DONE && world.m_time < 600 ) world.step( none );
        int        before = c.m_league;
        RaceReport r = c.finish( o, world, player );
        dead += (int)r.m_dead.size();
        if ( r.m_place == 1 ) ++wins;
        minMoney = std::min( minMoney, c.m_money );
        const CarDef *cd = g_data.car( c.m_car );
        printf( "%3d %6d %8lld %7lld  %-14s %4d %4d  %s %s place %d%s%s +%d\n", c.m_day - 1, before, c.m_money, c.m_loan,
                cd ? cd->m_name.c_str() : "?", c.rank(), [&] {
                    for ( auto &e : c.table() )
                        if ( std::get<2>( e ).empty() ) return std::get<1>( e );
                    return 0;
                }(), duel ? "SHOWDOWN" : o.m_derby ? "derby" : o.m_tier == 0 ? "small" : o.m_tier == 1 ? "main" : "big",
                o.m_track.c_str(), r.m_place, r.m_wrecked ? " (wrecked)" : "", r.m_dead.empty() ? "" : " +dead", r.m_total );
        if ( c.m_league != lastLeague )
        {
            printf( "    league %d done in %d days\n", lastLeague, c.m_day - leagueStart );
            leagueStart = c.m_day;
            lastLeague = c.m_league;
        }
    }
    printf( "%s after %d days: league %d, money %lld, debt %lld, %d wins, %d showdowns, %d rivals dead, lowest money %lld\n",
            c.m_won ? "WON" : c.m_over ? "BANKRUPT" : "STOPPED", c.m_day - 1, c.m_league, c.m_money, c.m_loan, wins, showdowns, dead, minMoney );
    return c.m_won || ( !c.m_over && opt.has( "--days" ) ) ? 0 : 1;
}

}   // namespace grave
