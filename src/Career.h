/**
 * @file Career.h
 * @brief The career without a window (docs/DESIGN.md "Career"): five leagues of rivals and their tables, race days
 *        with three offers (small, main, big; now and then a derby), the showdown with a league's champion and the
 *        final race against Mortimer Graves; the money (prizes, bounties, cash on the track, fees, repairs), the
 *        garage (cars, upgrades, guns, paint), the black market (gear, sabotage), the loan shark, the sponsors;
 *        rivals who die and the rookies who take their seats; the tabloid after every race day. Kept as a text
 *        file in the player's folder.
 * @author Rene Weller
 */
#pragma once

#include "Data.h"
#include "Sim.h"

#include <string>
#include <vector>

namespace grave
{

/** @return an amount of money as the player reads it ("12.345 $", "$12,345") */
std::string formatMoney( long long m );

/** @brief The race days of a season: then the table starts afresh. */
constexpr int SeasonDays = 8;

/** @brief A rival's career. */
struct RivalState
{
    std::string m_key;
    bool        m_active = false;   ///< in a league's roster
    bool        m_alive = true;
    int         m_league = 5;
    int         m_points = 0;
    int         m_wins = 0, m_races = 0, m_wrecks = 0;
    int         m_died = 0;         ///< the day they died
};

/** @brief A race on offer. */
struct Offer
{
    std::string              m_track;
    bool                     m_reverse = false;
    int                      m_tier = 1;          ///< 0 small, 1 main, 2 big
    bool                     m_derby = false;
    bool                     m_showdown = false;  ///< against the league's champion
    bool                     m_final = false;     ///< against Graves
    std::vector<std::string> m_rivals;
    int                      m_purse = 0, m_fee = 0, m_laps = 3;
};

/** @brief One line of a race's result. */
struct ResultLine
{
    std::string m_name, m_car, m_rival;
    int         m_place = 0, m_prize = 0, m_points = 0, m_wrecks = 0;
    bool        m_wrecked = false, m_player = false, m_died = false;
    float       m_time = 0.0f;
};

/** @brief What a race brought. */
struct RaceReport
{
    std::vector<ResultLine>  m_lines;
    int                      m_place = 0;
    bool                     m_wrecked = false;
    int                      m_prize = 0, m_bounty = 0, m_cash = 0, m_lap = 0, m_sponsor = 0, m_points = 0;
    int                      m_total = 0;            ///< all the money it brought
    std::vector<std::string> m_dead;                 ///< rivals who died
    bool                     m_promoted = false;
    bool                     m_seasonOver = false;   ///< the table was reset after this race
    bool                     m_won = false;          ///< the career is won
    std::string              m_story;                ///< a story to show now ("" none)
};

/** @brief A piece of the tabloid. */
struct NewsItem
{
    std::string m_head, m_text;
    bool        m_ad = false;
};

/** @brief The upgrades. */
enum class UpE : int
{
    ENGINE, TIRES, ARMOR, GUN, COUNT
};

/** @brief The career. */
class Career
{
public:
    /** @brief A new career. */
    void start( const std::string &name, const Paint &paint, int difficulty, uint32_t seed );

    /** @brief Makes the day's offers (and the collectors' visit when a loan is overdue). */
    void newDay();

    /**
     * @brief The field of an offer: the rivals (best in the table first) and the player, at their place on the grid.
     * @param[in]  o       the offer
     * @param[out] player  the player's index
     * @return the setups
     */
    std::vector<CarSetup> field( const Offer &o, int &player ) const;

    /** @brief Pays the fee (false: too poor). */
    bool enter( const Offer &o );

    /**
     * @brief Books a finished race: money, points, the dead, the rookies, the other results, the news, the next day.
     * @param[in] o       the offer
     * @param[in] w       the finished race
     * @param[in] player  the player's index in it
     * @return what it brought
     */
    RaceReport finish( const Offer &o, const World &w, int player );

    /** @return the player's league's table: name, points, rival key ("" the player), best first */
    std::vector<std::tuple<std::string, int, std::string>> table() const;
    /** @return the player's place in the table (1 the top) */
    int rank() const;
    /** @return whether the showdown is open (first place and enough points) */
    bool showdownReady() const;
    /** @return the showdown (or in the final league the race against Graves) */
    Offer showdown() const;

    /** @return the player's car as it races */
    CarSetup playerSetup() const;
    /** @return the purse of the league's main event */
    int purse() const;

    // The garage.
    int  carPrice( const std::string &car ) const;
    int  sellValue() const;
    bool buyCar( const std::string &car );
    int  upgradeLevels( UpE u ) const;
    int  upgradeLevel( UpE u ) const;
    int  upgradeCost( UpE u ) const;   ///< -1 at the top
    bool upgrade( UpE u );
    int  repairCost() const;
    bool repair();
    int  gunPrice( const std::string &gun ) const;
    bool buyGun( const std::string &gun );

    // The black market.
    int  gearPrice( const std::string &gear ) const;
    int  gearCount( const std::string &gear ) const;
    int  gearMax( const std::string &gear ) const;
    bool buyGear( const std::string &gear );
    int  sabotagePrice() const;
    bool sabotage( const std::string &rival );

    // The loan shark.
    int  loanLimit() const;
    bool borrow( int amount );
    bool repay( int amount );

    // Sponsors.
    std::vector<std::string> sponsors() const;
    void                     sign( const std::string &sponsor );

    /** @return whether the player can go on (false: broke with a wreck and no credit) */
    bool solvent() const;

    /** @return a rival's state (nullptr none) */
    const RivalState *rival( const std::string &key ) const;

    /** @brief Writes it. */
    bool save( const std::string &path ) const;
    /** @brief Reads it. */
    bool load( const std::string &path );

    // The state.
    std::string m_name = "Fahrer";
    Paint       m_paint;
    int         m_difficulty = 2;
    int         m_league = 5;          ///< 5 lowest .. 1 top, 0 the final
    int         m_day = 1;
    int         m_leagueDay = 0;       ///< days in this league
    long long   m_money = 1500;
    std::string m_car = "rustbucket";
    int         m_carPaid = 0;         ///< what the car cost
    int         m_up[(int)UpE::COUNT] = {};
    int         m_upPaid = 0;          ///< what the upgrades cost
    std::string m_gun;                 ///< "" the car's own
    float       m_damage = 0.0f;       ///< 0 whole .. 1 a wreck
    int         m_rear[(int)RearE::COUNT] = {};
    int         m_rockets = 0, m_nitro = 0;
    bool        m_ram = false;
    int         m_plating = 0;
    long long   m_loan = 0;
    int         m_loanDue = 0;         ///< the day it must be paid back
    std::string m_sponsor;
    std::string m_sabotaged;           ///< a rival of the next race
    std::vector<RivalState> m_rivals;
    std::vector<Offer>      m_offers;
    std::vector<NewsItem>   m_news;
    std::string             m_story;   ///< a story waiting to be shown
    int  m_races = 0, m_wins = 0, m_wrecks = 0, m_wrecked = 0, m_earned = 0;
    bool m_over = false;               ///< won or bankrupt
    bool m_won = false;
    uint32_t m_seed = 1;

private:
    void news( const std::string &event, const std::string &rival, const std::string &track, int place, long long money );
    void ads( int n );
    std::string fill( const std::string &text, const std::string &rival, const std::string &track, int place,
                      long long money ) const;
    std::vector<std::string> roster( int league ) const;
    float rivalStrength( const std::string &key ) const;
    CarSetup rivalSetup( const std::string &key, int tier ) const;
    void promote();
    Rng  rng( uint32_t salt ) const;
};

}   // namespace grave
