/**
 * @file Data.h
 * @brief Everything the game is made of, read from the text files in data/ (docs/DESIGN.md): models, cars, guns,
 *        gear, upgrades, venues, tracks, rivals, characters, leagues, difficulties, news, adverts, stories and
 *        sponsors.
 * @author Rene Weller
 */
#pragma once

#include "Parse.h"

#include <map>
#include <string>
#include <vector>

namespace grave
{

/** @brief A text in both languages. */
struct Text
{
    std::string m_de;
    std::string m_en;

    /** @return the one of the chosen language (German where English is missing) */
    const std::string &get() const;
    /** @return whether there is no text at all */
    bool empty() const { return m_de.empty() && m_en.empty(); }
};

/** @brief How a model is built: its parts. The Forge turns it into meshes. */
struct ModelDef
{
    std::string       m_name;
    float             m_scale = 1.0f;   ///< all of it larger or smaller
    bool              m_lamp = false;   ///< its light parts shine at night
    std::vector<Line> m_parts;          ///< "part <kind> key=value ..."
    std::string       m_where;          ///< file:line
};

/** @brief A car (data/cars). */
struct CarDef
{
    std::string m_name;
    Text        m_title, m_desc;
    std::string m_model;
    int         m_class = 1;
    int         m_price = 0;
    int         m_league = 5;
    float       m_mass = 1200.0f;
    float       m_power = 11.0f;
    float       m_top = 36.0f;
    float       m_grip = 1.0f;
    float       m_brake = 1.0f;
    float       m_steer = 1.0f;
    float       m_offroad = 1.0f;
    float       m_armor = 100.0f;
    float       m_length = 4.4f, m_width = 1.8f;
    std::string m_engine = "i4", m_horn = "plain", m_gun = "mg";
    Vector3     m_mountFront = { 0, 0.9f, -1.6f }, m_mountRoof = { 0, 1.4f, 0.3f }, m_mountRear = { 0, 0.6f, 2.0f };
    int         m_upEngine = 4, m_upTires = 4, m_upArmor = 4, m_upGun = 4;
    float       m_upgradeCost = 1.0f;
    float       m_regen = 0.0f, m_bounty = 1.0f, m_ramproof = 1.0f, m_ammo = 1.0f;
    int         m_scare = 0, m_nitro = 0, m_mines = 0, m_slick = 0;
    std::string m_where;
};

/** @brief A front gun (data/guns.txt). */
struct GunDef
{
    std::string m_name;
    Text        m_title, m_desc;
    int         m_price = 0;
    Range       m_damage = { 4, 8 }, m_rate = { 10, 14 }, m_ammo = { 150, 300 };
    float       m_speed = 170.0f, m_spread = 1.5f, m_range = 80.0f, m_heat = 0.0f;
    std::string m_kind = "bullet";   ///< bullet | flame | shell
    std::string m_sound = "mg";
    Color       m_color = { 255, 210, 110, 255 };
};

/** @brief What the black market sells. */
enum class GearE : int
{
    MINE, ROCKET, SPIKES, OIL, SMOKE, NITRO, RAM, PLATING, COUNT
};

/** @brief A good of the black market (data/gear.txt). */
struct GearDef
{
    std::string m_name;
    Text        m_title, m_desc;
    GearE       m_kind = GearE::MINE;
    int         m_price = 100, m_pack = 1, m_max = 9;
    float       m_damage = 40.0f, m_radius = 4.0f, m_arm = 0.6f, m_life = 60.0f;
    float       m_speed = 70.0f, m_turn = 120.0f, m_cone = 30.0f, m_slow = 2.0f;
    float       m_boost = 14.0f, m_dur = 2.0f, m_mult = 2.0f, m_self = 0.5f, m_hp = 50.0f;
    int         m_league = 5;   ///< on sale from this league on
};

/** @brief A kind of upgrade (data/upgrades.txt). */
struct UpgradeDef
{
    std::string      m_name;   ///< engine tires armor gun
    Text             m_title, m_desc;
    std::vector<int> m_cost;
    float            m_power = 0, m_top = 0, m_grip = 0, m_brake = 0, m_offroad = 0, m_hp = 0;
};

/** @brief The ground's kinds (grip and drag, docs/DESIGN.md). */
enum class SurfaceE : int
{
    ASPHALT, CONCRETE, COBBLE, DIRT, GRAVEL, SAND, MUD, SNOW, ICE, METAL, WOOD, WATER, GRASS, OIL, COUNT
};

/** @return a surface by its name (-1 unknown) */
int surfaceOf( const std::string &name );
/** @return a surface's name */
const char *surfaceName( SurfaceE s );
/** @return how well tyres hold on it (1 asphalt) */
float surfaceGrip( SurfaceE s );
/** @return how much it holds a car back (m/s² per m/s) */
float surfaceDrag( SurfaceE s );
/** @return whether it is loose (dirt, sand, snow: rally cars like it, dust flies) */
bool surfaceLoose( SurfaceE s );

/** @brief A place with its own look (data/venues.txt). */
struct VenueDef
{
    std::string m_name;
    Text        m_title, m_desc;
    int         m_league = 5;
    std::string m_time = "day", m_weather = "clear";
    float       m_sunDir = 40.0f, m_sunHeight = 45.0f, m_sunPower = 1.0f;
    Color       m_sunColor = { 255, 244, 220, 255 };
    Color       m_ambient = { 90, 100, 120, 255 };
    float       m_ambientPower = 0.6f;
    Color       m_fog = { 120, 130, 140, 255 };
    float       m_fogDensity = 0.004f;
    SurfaceE    m_ground = SurfaceE::GRASS, m_road = SurfaceE::ASPHALT, m_shoulder = SurfaceE::GRAVEL;
    Color       m_groundColor = { 60, 80, 50, 255 }, m_groundColor2 = { 80, 70, 50, 255 };
    Color       m_roadColor = { 60, 60, 66, 255 }, m_lineColor = { 210, 210, 190, 255 };
    Color       m_shoulderColor = { 90, 84, 70, 255 };
    std::string m_wall = "concrete";
    std::vector<std::pair<std::string, float>> m_props;   ///< model, weight
    float       m_density = 1.0f;
    std::string m_music, m_ambience;
    std::vector<Line> m_extra;   ///< other lines (the renderer's own values)
};

/** @brief A piece of a track's path. */
struct TrackPiece
{
    enum KindE : int { STRAIGHT, LEFT, RIGHT, POINT } m_kind = STRAIGHT;
    float       m_len = 0.0f;       ///< straight: metres
    float       m_deg = 0.0f;       ///< arcs: degrees
    float       m_r = 30.0f;        ///< arcs: radius
    Vector2     m_point = { 0, 0 }; ///< POINT: a control point (x z)
    float       m_width = -1.0f;    ///< the width at its end (-1 unchanged)
    float       m_shoulder = -1.0f;
    int         m_surface = -1;     ///< SurfaceE (-1 unchanged)
    std::string m_wall;             ///< "" unchanged
    std::string m_wallL, m_wallR;   ///< one side only ("" as m_wall)
    std::string m_where;
};

/** @brief A thing placed along a track (pickups, hazards, obstacles, patches, props, rows, lamps, bridges). */
struct TrackItem
{
    std::string m_type;     ///< pickup hazard obstacle patch prop row place lamps bridge
    std::string m_what;     ///< its kind or model
    Line        m_line;     ///< the line itself (at lat r len w side off rot every x z model)
};

/** @brief A track (data/tracks). */
struct TrackDef
{
    std::string             m_name;
    Text                    m_title;
    std::string             m_venue;
    int                     m_laps = 4;
    float                   m_width = 13.0f, m_shoulder = 3.0f;
    Vector2                 m_start = { 0, 0 };
    float                   m_startHeading = 0.0f;   ///< degrees
    std::vector<TrackPiece> m_pieces;
    std::vector<TrackItem>  m_items;
    bool                    m_closed = false;   ///< ends with "close"
    bool                    m_reverse = false, m_mirror = false, m_arena = false;
    float                   m_density = 1.0f;   ///< the venue's scattered props
    std::string             m_where;
};

/** @brief Lines said on events: event -> lines. */
using Says = std::map<std::string, std::vector<std::string>>;

/** @brief A rival driver (data/rivals.txt). */
struct RivalDef
{
    std::string m_key;
    std::string m_name;
    int         m_league = 5;
    std::string m_car;
    Color       m_colors[3] = { { 160, 40, 40, 255 }, { 30, 30, 30, 255 }, { 200, 200, 200, 255 } };
    float       m_skill = 0.5f, m_aggression = 0.5f, m_weapons = 0.5f;
    std::string m_likes = "mine";
    bool        m_champion = false, m_rookie = false;
    std::string m_portrait;
    Text        m_bio, m_obit;
    Says        m_say, m_sayEn;

    /** @return a line for an event in the chosen language ("" none) */
    std::string line( const std::string &event, uint32_t pick ) const;
};

/** @brief A person of the hub (data/characters.txt). */
struct CharacterDef
{
    std::string m_key, m_name, m_portrait;
    Text        m_role;
    Says        m_say, m_sayEn;

    /** @return a line for an event in the chosen language ("" none) */
    std::string line( const std::string &event, uint32_t pick ) const;
};

/** @brief A league (data/leagues.txt): 5 the lowest .. 1 the top, 0 the final. */
struct LeagueDef
{
    int                      m_id = 5;
    Text                     m_title, m_desc;
    std::vector<std::string> m_venues;
    int                      m_purse = 1000, m_entry = 0;
    std::vector<int>         m_points = { 10, 6, 4, 3, 2, 1 };
    int                      m_showdown = 40;
    std::string              m_intro;
};

/** @brief A difficulty (data/difficulty.txt). */
struct DifficultyDef
{
    Text  m_title, m_desc;
    float m_aiSpeed = 0.95f, m_aiSkill = 0.82f, m_aiAggression = 0.75f, m_aiWeapons = 0.75f;
    float m_damageTaken = 1.0f, m_damageDealt = 1.0f;
    float m_prize = 1.0f, m_prices = 1.0f, m_repair = 1.0f;
    float m_rubber = 0.15f, m_deaths = 0.45f, m_growth = 1.0f, m_interest = 0.2f;
};

/** @brief A piece of news (data/news). */
struct NewsDef
{
    std::string m_event, m_id;
    Text        m_head, m_text;
};

/** @brief A story (data/story). */
struct StoryDef
{
    std::string              m_key;
    Text                     m_title;
    std::vector<std::string> m_de, m_en;

    /** @return the paragraphs of the chosen language */
    const std::vector<std::string> &prose() const;
};

/** @brief A sponsor (data/sponsors.txt). */
struct SponsorDef
{
    std::string                m_key;
    Text                       m_title, m_desc;
    int                        m_league = 5;
    std::map<std::string, int> m_bonus;   ///< win podium wreck clean lap nogun -> money
};

/** @brief Everything read. */
class Catalog
{
public:
    /**
     * @brief Reads every data file below a folder.
     * @param[in] dir  the data folder
     * @return false if there were errors
     */
    bool load( const std::string &dir );

    /** @brief Checks the references between the data (adds warnings and errors). */
    void check();

    const ModelDef     *model( const std::string &n ) const;
    const CarDef       *car( const std::string &n ) const;
    const GunDef       *gun( const std::string &n ) const;
    const GearDef      *gear( const std::string &n ) const;
    const UpgradeDef   *upgrade( const std::string &n ) const;
    const VenueDef     *venue( const std::string &n ) const;
    const TrackDef     *track( const std::string &n ) const;
    const RivalDef     *rival( const std::string &n ) const;
    const CharacterDef *character( const std::string &n ) const;
    const LeagueDef    *league( int id ) const;
    const StoryDef     *story( const std::string &n ) const;
    const SponsorDef   *sponsor( const std::string &n ) const;
    /** @return a difficulty (held to the ones there are) */
    const DifficultyDef &difficulty( int i ) const;

    /** @return the news of an event */
    std::vector<const NewsDef *> news( const std::string &event ) const;

    std::string m_dir;
    std::map<std::string, ModelDef>     m_models;
    std::map<std::string, CarDef>       m_cars;
    std::map<std::string, GunDef>       m_guns;
    std::map<std::string, GearDef>      m_gear;
    std::map<std::string, UpgradeDef>   m_upgrades;
    std::map<std::string, VenueDef>     m_venues;
    std::map<std::string, TrackDef>     m_tracks;
    std::map<std::string, RivalDef>     m_rivals;
    std::map<std::string, CharacterDef> m_characters;
    std::map<int, LeagueDef>            m_leagues;
    std::map<std::string, StoryDef>     m_stories;
    std::map<std::string, SponsorDef>   m_sponsors;
    std::vector<DifficultyDef>          m_difficulties;
    std::vector<NewsDef>                m_news;
    std::vector<Text>                   m_ads;
    std::vector<std::string>            m_carOrder;     ///< cars by class, then price
    std::vector<std::string>            m_trackOrder;   ///< tracks in file order
    std::vector<std::string>            m_venueOrder;   ///< venues in file order
    std::vector<std::string>            m_rivalOrder;   ///< rivals in file order
    std::vector<std::string>            m_gearOrder;
    std::vector<std::string>            m_gunOrder;

    std::vector<std::string> m_errors;
    std::vector<std::string> m_warnings;
};

/** @brief The game's data, read once at the start. */
extern Catalog g_data;

}   // namespace grave
