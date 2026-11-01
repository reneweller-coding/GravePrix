/**
 * @file Data.cpp
 * @brief Reading the data files into the catalog.
 * @author Rene Weller
 */
#include "Data.h"

#include "Lang.h"

#include <algorithm>
#include <filesystem>

namespace grave
{

namespace fs = std::filesystem;

Catalog g_data;

namespace
{

/** @brief The simple values of a block: every line's first word and every key=value, last one wins. */
struct Props
{
    std::map<std::string, std::string> m_v;

    explicit Props( const Block &b )
    {
        // Named values first, then "key value" pairs on a line ("length 4.1 width 1.72"), then each line's own key:
        // "engine i4" is never overwritten by an "engine=4" elsewhere in the block.
        auto ident = []( const std::string &w ) {
            return !w.empty() && w.find_first_not_of( "abcdefghijklmnopqrstuvwxyz_" ) == std::string::npos;
        };
        for ( const auto &kv : b.m_head.m_kv ) m_v[kv.first] = kv.second;
        for ( const Line &l : b.m_lines )
            for ( const auto &kv : l.m_kv ) m_v[kv.first] = kv.second;
        for ( const Line &l : b.m_lines )
            for ( size_t i = 1; i + 1 < l.m_args.size(); i += 2 )
                if ( ident( l.m_args[i] ) && !ident( l.m_args[i + 1] ) ) m_v[l.m_args[i]] = l.m_args[i + 1];
        for ( const Line &l : b.m_lines )
            if ( !l.m_args.empty() ) m_v[l.m_key] = l.m_args[0];
    }

    bool        has( const std::string &k ) const { return m_v.count( k ) > 0; }
    std::string str( const std::string &k, const std::string &def = "" ) const
    {
        auto it = m_v.find( k );
        return it != m_v.end() ? it->second : def;
    }
    float num( const std::string &k, float def ) const
    {
        auto it = m_v.find( k );
        return it != m_v.end() ? toNumber( it->second, def ) : def;
    }
    int   integer( const std::string &k, int def ) const { return (int)std::lround( num( k, (float)def ) ); }
    Range range( const std::string &k, Range def ) const
    {
        auto it = m_v.find( k );
        if ( it == m_v.end() ) return def;
        const std::string &s = it->second;
        size_t             dots = s.find( ".." );
        if ( dots == std::string::npos )
        {
            float v = toNumber( s, def.m_a );
            return { v, v };
        }
        return { toNumber( s.substr( 0, dots ), def.m_a ), toNumber( s.substr( dots + 2 ), def.m_b ) };
    }
    Text text( const std::string &k ) const { return { str( k ), str( k + "_en" ) }; }
    Color color( const std::string &k, Color def ) const { return parseColor( str( k ), def ); }
};

/** @return a line by its key (nullptr none) */
const Line *lineOf( const Block &b, const std::string &key )
{
    for ( const Line &l : b.m_lines )
        if ( l.m_key == key ) return &l;
    return nullptr;
}

/** @brief Reads "say <event> line line ..." and "say_en ..." lines. */
void readSays( const Block &b, Says &de, Says &en )
{
    for ( const Line &l : b.m_lines )
    {
        if ( ( l.m_key != "say" && l.m_key != "say_en" ) || l.m_args.size() < 2 ) continue;
        Says &to = l.m_key == "say" ? de : en;
        for ( size_t i = 1; i < l.m_args.size(); ++i ) to[l.m_args[0]].push_back( l.m_args[i] );
    }
}

/** @return a line of a says map */
std::string pickLine( const Says &de, const Says &en, const std::string &event, uint32_t pick )
{
    const Says &s = english() && en.count( event ) ? en : de;
    auto        it = s.find( event );
    if ( it == s.end() || it->second.empty() ) return "";
    return it->second[pick % it->second.size()];
}

const std::vector<std::string> BlockTypes = { "model",  "car",       "gun",   "gear",       "upgrade", "venue",
                                              "track",  "rival",     "character", "league", "difficulty",
                                              "news",   "ad",        "story", "story_en",   "sponsor", "scene", "award" };

}   // namespace

const std::string &Text::get() const { return L( m_de, m_en ); }

std::string RivalDef::line( const std::string &event, uint32_t pick ) const
{
    return pickLine( m_say, m_sayEn, event, pick );
}

std::string CharacterDef::line( const std::string &event, uint32_t pick ) const
{
    return pickLine( m_say, m_sayEn, event, pick );
}

const std::vector<std::string> &StoryDef::prose() const { return english() && !m_en.empty() ? m_en : m_de; }

namespace
{

struct SurfaceInfo
{
    const char *m_name;
    float       m_grip, m_drag;
    bool        m_loose;
};

const SurfaceInfo Surfaces[(int)SurfaceE::COUNT] = {
    { "asphalt", 1.0f, 0.0f, false }, { "concrete", 0.97f, 0.0f, false }, { "cobble", 0.9f, 0.02f, false },
    { "dirt", 0.8f, 0.05f, true },    { "gravel", 0.72f, 0.08f, true },   { "sand", 0.65f, 0.14f, true },
    { "mud", 0.55f, 0.2f, true },     { "snow", 0.6f, 0.06f, true },      { "ice", 0.35f, 0.01f, false },
    { "metal", 0.85f, 0.0f, false },  { "wood", 0.9f, 0.01f, false },     { "water", 0.7f, 0.12f, false },
    { "grass", 0.72f, 0.08f, true },  { "oil", 0.12f, 0.0f, false },
};

}   // namespace

int surfaceOf( const std::string &name )
{
    for ( int i = 0; i < (int)SurfaceE::COUNT; ++i )
        if ( name == Surfaces[i].m_name ) return i;
    return -1;
}

const char *surfaceName( SurfaceE s ) { return Surfaces[(int)s].m_name; }
float       surfaceGrip( SurfaceE s ) { return Surfaces[(int)s].m_grip; }
float       surfaceDrag( SurfaceE s ) { return Surfaces[(int)s].m_drag; }
bool        surfaceLoose( SurfaceE s ) { return Surfaces[(int)s].m_loose; }

namespace
{

GearE gearKind( const std::string &s, bool &ok )
{
    static const char *names[] = { "mine", "rocket", "spikes", "oil", "smoke", "nitro", "ram", "plating" };
    for ( int i = 0; i < (int)GearE::COUNT; ++i )
        if ( s == names[i] )
        {
            ok = true;
            return (GearE)i;
        }
    ok = false;
    return GearE::MINE;
}

Vector3 vec3( const Line *l, Vector3 def )
{
    if ( !l || l->m_args.size() < 3 ) return def;
    return { l->argNum( 0 ), l->argNum( 1 ), l->argNum( 2 ) };
}

void readCar( const Block &b, CarDef &c )
{
    Props p( b );
    c.m_name = b.m_name;
    c.m_title = p.text( "title" );
    if ( c.m_title.empty() ) c.m_title.m_de = b.m_name;
    c.m_desc = p.text( "desc" );
    c.m_model = p.str( "model", "car_" + b.m_name );
    c.m_class = p.integer( "class", 1 );
    c.m_price = p.integer( "price", 0 );
    c.m_league = p.integer( "league", 5 );
    c.m_mass = p.num( "mass", c.m_mass );
    c.m_power = p.num( "power", c.m_power );
    c.m_top = p.num( "top", c.m_top );
    c.m_grip = p.num( "grip", c.m_grip );
    c.m_brake = p.num( "brake", c.m_brake );
    c.m_steer = p.num( "steer", c.m_steer );
    c.m_offroad = p.num( "offroad", c.m_offroad );
    c.m_armor = p.num( "armor", c.m_armor );
    c.m_length = p.num( "length", c.m_length );
    c.m_width = p.num( "width", c.m_width );
    c.m_engine = p.str( "engine", c.m_engine );
    c.m_horn = p.str( "horn", c.m_horn );
    c.m_gun = p.str( "gun", c.m_gun );
    c.m_mountFront = vec3( lineOf( b, "mount_front" ), c.m_mountFront );
    c.m_mountRoof = vec3( lineOf( b, "mount_roof" ), c.m_mountRoof );
    c.m_mountRear = vec3( lineOf( b, "mount_rear" ), c.m_mountRear );
    if ( const Line *u = lineOf( b, "upgrades" ) )
    {
        c.m_upEngine = std::clamp( u->integer( "engine", 4 ), 0, 6 );
        c.m_upTires = std::clamp( u->integer( "tires", 4 ), 0, 6 );
        c.m_upArmor = std::clamp( u->integer( "armor", 4 ), 0, 6 );
        c.m_upGun = std::clamp( u->integer( "gun", 4 ), 0, 6 );
    }
    c.m_upgradeCost = p.num( "upgrade_cost", 1.0f );
    if ( const Line *pk = lineOf( b, "perk" ) )
    {
        c.m_regen = pk->num( "regen", 0 );
        c.m_bounty = pk->num( "bounty", 1 );
        c.m_ramproof = pk->num( "ramproof", 1 );
        c.m_ammo = pk->num( "ammo", 1 );
        c.m_scare = pk->integer( "scare", 0 );
        c.m_nitro = pk->integer( "nitro", 0 );
        c.m_mines = pk->integer( "mines", 0 );
        c.m_slick = pk->integer( "slick", 0 );
    }
    c.m_where = b.m_head.where();
}

void readVenue( const Block &b, VenueDef &v, std::vector<std::string> &warnings )
{
    Props p( b );
    v.m_name = b.m_name;
    v.m_title = p.text( "title" );
    v.m_desc = p.text( "desc" );
    v.m_league = p.integer( "league", 5 );
    for ( const Line &l : b.m_lines )
    {
        const std::string &k = l.m_key;
        if ( k == "time" ) v.m_time = l.arg( 0, v.m_time );
        else if ( k == "weather" ) v.m_weather = l.arg( 0, v.m_weather );
        else if ( k == "sun" )
        {
            v.m_sunDir = l.num( "dir", v.m_sunDir );
            v.m_sunHeight = l.num( "height", v.m_sunHeight );
            v.m_sunColor = l.color( "color", v.m_sunColor );
            v.m_sunPower = l.num( "power", v.m_sunPower );
        }
        else if ( k == "ambient" )
        {
            v.m_ambient = parseColor( l.arg( 0 ), v.m_ambient );
            v.m_ambientPower = l.num( "power", v.m_ambientPower );
        }
        else if ( k == "fog" )
        {
            v.m_fog = parseColor( l.arg( 0 ), v.m_fog );
            v.m_fogDensity = l.num( "density", v.m_fogDensity );
        }
        else if ( k == "ground" || k == "road" || k == "shoulder" )
        {
            int s = surfaceOf( l.arg( 0 ) );
            if ( s < 0 )
            {
                warnings.push_back( l.where() + ": unknown surface \"" + l.arg( 0 ) + "\"" );
                s = 0;
            }
            if ( k == "ground" )
            {
                v.m_ground = (SurfaceE)s;
                v.m_groundColor = l.color( "color", v.m_groundColor );
                v.m_groundColor2 = l.color( "color2", v.m_groundColor2 );
            }
            else if ( k == "road" )
            {
                v.m_road = (SurfaceE)s;
                v.m_roadColor = l.color( "color", v.m_roadColor );
                v.m_lineColor = l.color( "line", v.m_lineColor );
            }
            else
            {
                v.m_shoulder = (SurfaceE)s;
                v.m_shoulderColor = l.color( "color", v.m_shoulderColor );
            }
        }
        else if ( k == "wall" ) v.m_wall = l.arg( 0, v.m_wall );
        else if ( k == "props" || k == "litter" || k == "loose" )
        {
            auto &list = k == "props" ? v.m_props : k == "litter" ? v.m_litter : v.m_loose;
            for ( const std::string &w : l.m_args )
            {
                size_t star = w.find( '*' );
                if ( star == std::string::npos )
                    list.push_back( { w, 1.0f } );
                else
                    list.push_back( { w.substr( 0, star ), toNumber( w.substr( star + 1 ), 1.0f ) } );
            }
        }
        else if ( k == "density" ) v.m_density = l.argNum( 0, 1.0f );
        else if ( k == "music" ) v.m_music = l.arg( 0 );
        else if ( k == "ambience" ) v.m_ambience = l.arg( 0 );
        else if ( k != "title" && k != "title_en" && k != "desc" && k != "desc_en" && k != "league" )
            v.m_extra.push_back( l );
    }
    if ( v.m_music.empty() ) v.m_music = v.m_name;
    if ( v.m_ambience.empty() ) v.m_ambience = v.m_name;
}

void readScene( const Block &b, SceneDef &s, std::vector<std::string> &warnings )
{
    Props p( b );
    s.m_name = b.m_name;
    s.m_venue = p.str( "venue" );
    s.m_weight = p.num( "weight", 1.0f );
    s.m_size = p.num( "size", 12.0f );
    s.m_near = p.num( "near", 4.0f );
    for ( const Line &l : b.m_lines )
    {
        const std::string &k = l.m_key;
        if ( k != "put" && k != "car" && k != "wreck" ) continue;
        ScenePiece sp;
        sp.m_kind = k == "put" ? ScenePiece::MODEL : k == "car" ? ScenePiece::CAR : ScenePiece::WRECK;
        sp.m_what = l.arg( 0 );
        sp.m_x = l.num( "x", 0 );
        sp.m_z = l.num( "z", 0 );
        sp.m_rot = l.num( "rot", 0 );
        sp.m_scale = l.num( "scale", 1.0f );
        sp.m_tilt = l.num( "tilt", 0 );
        if ( sp.m_what.empty() ) warnings.push_back( l.where() + ": a scene piece without a name" );
        s.m_pieces.push_back( sp );
    }
}

void readTrack( const Block &b, TrackDef &t, std::vector<std::string> &errors )
{
    t.m_name = b.m_name;
    t.m_where = b.m_head.where();
    float width = -1, shoulder = -1;
    for ( const Line &l : b.m_lines )
    {
        const std::string &k = l.m_key;
        auto               piece = [&]( TrackPiece::KindE kind ) {
            TrackPiece p;
            p.m_kind = kind;
            if ( kind == TrackPiece::STRAIGHT )
                p.m_len = l.argNum( 0, 0 );
            else if ( kind == TrackPiece::POINT )
                p.m_point = { l.argNum( 0 ), l.argNum( 1 ) };
            else
            {
                p.m_deg = l.argNum( 0, 0 );
                p.m_r = l.num( "r", 30.0f );
            }
            p.m_width = l.num( "w", -1.0f );
            p.m_shoulder = l.num( "shoulder", -1.0f );
            if ( l.has( "surface" ) )
            {
                p.m_surface = surfaceOf( l.str( "surface" ) );
                if ( p.m_surface < 0 ) errors.push_back( l.where() + ": unknown surface \"" + l.str( "surface" ) + "\"" );
            }
            p.m_wall = l.str( "wall" );
            p.m_wallL = l.str( "wall_left" );
            p.m_wallR = l.str( "wall_right" );
            p.m_where = l.where();
            if ( ( kind == TrackPiece::STRAIGHT && p.m_len <= 0 ) ||
                 ( ( kind == TrackPiece::LEFT || kind == TrackPiece::RIGHT ) && ( p.m_deg <= 0 || p.m_r <= 0 ) ) )
                errors.push_back( l.where() + ": a piece needs a length, or degrees and a radius" );
            t.m_pieces.push_back( p );
        };
        if ( k == "title" )
        {
            t.m_title.m_de = l.arg( 0 );
            if ( l.has( "title_en" ) ) t.m_title.m_en = l.str( "title_en" );
        }
        else if ( k == "title_en" ) t.m_title.m_en = l.arg( 0 );
        else if ( k == "venue" ) t.m_venue = l.arg( 0 );
        else if ( k == "laps" ) t.m_laps = (int)l.argNum( 0, 4 );
        else if ( k == "width" ) width = l.argNum( 0, 13 );
        else if ( k == "shoulder" ) shoulder = l.argNum( 0, 3 );
        else if ( k == "start" )
        {
            t.m_start = { l.argNum( 0 ), l.argNum( 1 ) };
            t.m_startHeading = l.argNum( 2 );
        }
        else if ( k == "straight" ) piece( TrackPiece::STRAIGHT );
        else if ( k == "left" ) piece( TrackPiece::LEFT );
        else if ( k == "right" ) piece( TrackPiece::RIGHT );
        else if ( k == "p" ) piece( TrackPiece::POINT );
        else if ( k == "close" ) t.m_closed = true;
        else if ( k == "reverse" ) t.m_reverse = l.argNum( 0, 1 ) != 0;
        else if ( k == "mirror" ) t.m_mirror = l.argNum( 0, 1 ) != 0;
        else if ( k == "arena" ) t.m_arena = l.argNum( 0, 1 ) != 0;
        else if ( k == "density" ) t.m_density = l.argNum( 0, 1 );
        else if ( k == "pickup" || k == "hazard" || k == "obstacle" || k == "patch" || k == "prop" || k == "row" ||
                  k == "place" || k == "lamps" || k == "bridge" || k == "train" || k == "drawbridge" || k == "rockfall" )
            t.m_items.push_back( { k, l.arg( 0 ), l } );
        else if ( k == "weather" )
        {
            t.m_weatherTo = l.arg( 0 );
            t.m_weatherLap = (int)l.num( "lap", 2 );
        }
        else if ( k == "desc" || k == "desc_en" )
            ;
        else
            errors.push_back( l.where() + ": unknown track line \"" + k + "\"" );
    }
    if ( width > 0 ) t.m_width = width;
    if ( shoulder >= 0 ) t.m_shoulder = shoulder;
    if ( t.m_title.empty() ) t.m_title.m_de = t.m_name;
}

void readRival( const Block &b, RivalDef &r )
{
    Props p( b );
    r.m_key = b.m_name;
    r.m_name = p.str( "name", b.m_name );
    r.m_league = p.integer( "league", 5 );
    r.m_car = p.str( "car" );
    if ( const Line *c = lineOf( b, "colors" ) )
        for ( size_t i = 0; i < 3 && i < c->m_args.size(); ++i ) r.m_colors[i] = parseColor( c->m_args[i], r.m_colors[i] );
    r.m_skill = clampf( p.num( "skill", 0.5f ), 0, 1 );
    r.m_aggression = clampf( p.num( "aggression", 0.5f ), 0, 1 );
    r.m_weapons = clampf( p.num( "weapons", 0.5f ), 0, 1 );
    r.m_likes = p.str( "likes", "mine" );
    r.m_champion = p.integer( "champion", 0 ) != 0;
    r.m_boss = p.str( "boss" );
    r.m_rookie = p.integer( "rookie", 0 ) != 0;
    r.m_portrait = p.str( "portrait" );
    r.m_bio = p.text( "bio" );
    r.m_obit = p.text( "obit" );
    readSays( b, r.m_say, r.m_sayEn );
}

}   // namespace

bool Catalog::load( const std::string &dir )
{
    m_dir = dir;
    std::vector<std::string> files;
    std::error_code          ec;
    for ( auto it = fs::recursive_directory_iterator( dir, ec ); !ec && it != fs::recursive_directory_iterator(); ++it )
        if ( it->is_regular_file() && it->path().extension() == ".txt" )
        {
            std::string rel = fs::relative( it->path(), dir ).generic_string();
            if ( rel.rfind( "lang/", 0 ) == 0 ) continue;   // the interface's translations (Lang)
            files.push_back( it->path().string() );
        }
    if ( files.empty() ) m_errors.push_back( dir + ": no data files" );
    std::sort( files.begin(), files.end() );
    std::vector<Block> blocks;
    for ( const std::string &f : files ) readBlocks( f, BlockTypes, blocks, m_errors );

    auto dup = [&]( const Block &b, bool exists ) {
        if ( exists ) m_errors.push_back( b.m_head.where() + ": " + b.m_type + " \"" + b.m_name + "\" defined twice" );
    };
    for ( const Block &b : blocks )
    {
        const std::string &t = b.m_type;
        Props              p( b );
        if ( t == "model" )
        {
            dup( b, m_models.count( b.m_name ) > 0 );
            ModelDef &m = m_models[b.m_name];
            m = {};
            m.m_name = b.m_name;
            m.m_scale = b.m_head.num( "scale", 1.0f );
            m.m_lamp = b.m_head.integer( "lamp", 0 ) != 0;
            m.m_where = b.m_head.where();
            for ( const Line &l : b.m_lines )
            {
                if ( l.m_key != "part" || l.m_args.empty() )
                {
                    m_warnings.push_back( l.where() + ": a model has only \"part\" lines" );
                    continue;
                }
                Line part = l;
                part.m_key = l.m_args[0];
                part.m_args.erase( part.m_args.begin() );
                m.m_parts.push_back( part );
            }
        }
        else if ( t == "car" )
        {
            dup( b, m_cars.count( b.m_name ) > 0 );
            readCar( b, m_cars[b.m_name] );
        }
        else if ( t == "gun" )
        {
            dup( b, m_guns.count( b.m_name ) > 0 );
            GunDef &g = m_guns[b.m_name];
            g.m_name = b.m_name;
            g.m_title = p.text( "title" );
            g.m_desc = p.text( "desc" );
            g.m_price = p.integer( "price", 0 );
            g.m_damage = p.range( "damage", g.m_damage );
            g.m_rate = p.range( "rate", g.m_rate );
            g.m_ammo = p.range( "ammo", g.m_ammo );
            g.m_speed = p.num( "speed", g.m_speed );
            g.m_spread = p.num( "spread", g.m_spread );
            g.m_range = p.num( "range", g.m_range );
            g.m_heat = p.num( "heat", g.m_heat );
            g.m_kind = p.str( "kind", g.m_kind );
            g.m_sound = p.str( "sound", g.m_sound );
            g.m_color = p.color( "color", g.m_color );
            m_gunOrder.push_back( b.m_name );
        }
        else if ( t == "gear" )
        {
            dup( b, m_gear.count( b.m_name ) > 0 );
            GearDef &g = m_gear[b.m_name];
            g.m_name = b.m_name;
            g.m_title = p.text( "title" );
            g.m_desc = p.text( "desc" );
            bool ok = false;
            g.m_kind = gearKind( p.str( "kind", b.m_name ), ok );
            if ( !ok ) m_errors.push_back( b.m_head.where() + ": unknown gear kind \"" + p.str( "kind" ) + "\"" );
            g.m_price = p.integer( "price", g.m_price );
            g.m_pack = p.integer( "pack", g.m_pack );
            g.m_max = p.integer( "max", g.m_max );
            g.m_damage = p.num( "damage", g.m_damage );
            g.m_radius = p.num( "radius", g.m_radius );
            g.m_arm = p.num( "arm", g.m_arm );
            g.m_life = p.num( "life", g.m_life );
            g.m_speed = p.num( "speed", g.m_speed );
            g.m_turn = p.num( "turn", g.m_turn );
            g.m_cone = p.num( "cone", g.m_cone );
            g.m_slow = p.num( "slow", g.m_slow );
            g.m_boost = p.num( "boost", g.m_boost );
            g.m_dur = p.num( "dur", g.m_dur );
            g.m_mult = p.num( "mult", g.m_mult );
            g.m_self = p.num( "self", g.m_self );
            g.m_hp = p.num( "hp", g.m_hp );
            g.m_league = p.integer( "league", 5 );
            m_gearOrder.push_back( b.m_name );
        }
        else if ( t == "upgrade" )
        {
            UpgradeDef &u = m_upgrades[b.m_name];
            u.m_name = b.m_name;
            u.m_title = p.text( "title" );
            u.m_desc = p.text( "desc" );
            if ( const Line *c = lineOf( b, "cost" ) )
                for ( size_t i = 0; i < c->m_args.size(); ++i ) u.m_cost.push_back( (int)c->argNum( i ) );
            u.m_power = p.num( "power", 0 );
            u.m_top = p.num( "top", 0 );
            u.m_grip = p.num( "grip", 0 );
            u.m_brake = p.num( "brake", 0 );
            u.m_offroad = p.num( "offroad", 0 );
            u.m_hp = p.num( "hp", 0 );
            u.m_slip = p.num( "slip", 0 );
            u.m_drag = p.num( "drag", 0 );
            u.m_mass = p.num( "mass", 0 );
        }
        else if ( t == "venue" )
        {
            dup( b, m_venues.count( b.m_name ) > 0 );
            readVenue( b, m_venues[b.m_name], m_warnings );
            m_venueOrder.push_back( b.m_name );
        }
        else if ( t == "scene" )
        {
            dup( b, m_scenes.count( b.m_name ) > 0 );
            readScene( b, m_scenes[b.m_name], m_warnings );
        }
        else if ( t == "track" )
        {
            dup( b, m_tracks.count( b.m_name ) > 0 );
            readTrack( b, m_tracks[b.m_name], m_errors );
            m_trackOrder.push_back( b.m_name );
        }
        else if ( t == "rival" )
        {
            dup( b, m_rivals.count( b.m_name ) > 0 );
            readRival( b, m_rivals[b.m_name] );
            m_rivalOrder.push_back( b.m_name );
        }
        else if ( t == "character" )
        {
            CharacterDef &c = m_characters[b.m_name];
            c.m_key = b.m_name;
            c.m_name = p.str( "name", b.m_name );
            c.m_portrait = p.str( "portrait" );
            c.m_role = p.text( "role" );
            readSays( b, c.m_say, c.m_sayEn );
        }
        else if ( t == "league" )
        {
            int        id = (int)toNumber( b.m_name, 5 );
            LeagueDef &l = m_leagues[id];
            l.m_id = id;
            l.m_title = p.text( "title" );
            l.m_desc = p.text( "desc" );
            if ( const Line *v = lineOf( b, "venues" ) ) l.m_venues = v->m_args;
            l.m_purse = p.integer( "purse", l.m_purse );
            l.m_entry = p.integer( "entry", l.m_entry );
            if ( const Line *pts = lineOf( b, "points" ) )
            {
                l.m_points.clear();
                for ( size_t i = 0; i < pts->m_args.size(); ++i ) l.m_points.push_back( (int)pts->argNum( i ) );
            }
            l.m_showdown = p.integer( "showdown", l.m_showdown );
            l.m_intro = p.str( "intro" );
            l.m_pace = p.num( "pace", 1.0f );
            l.m_skill = p.num( "skill", 1.0f );
        }
        else if ( t == "difficulty" )
        {
            size_t i = (size_t)std::max( 0, (int)toNumber( b.m_name, 0 ) );
            if ( m_difficulties.size() <= i ) m_difficulties.resize( i + 1 );
            DifficultyDef &d = m_difficulties[i];
            d.m_title = p.text( "title" );
            d.m_desc = p.text( "desc" );
            d.m_aiSpeed = p.num( "ai_speed", d.m_aiSpeed );
            d.m_aiSkill = p.num( "ai_skill", d.m_aiSkill );
            d.m_aiAggression = p.num( "ai_aggression", d.m_aiAggression );
            d.m_aiWeapons = p.num( "ai_weapons", d.m_aiWeapons );
            d.m_damageTaken = p.num( "damage_taken", d.m_damageTaken );
            d.m_damageDealt = p.num( "damage_dealt", d.m_damageDealt );
            d.m_prize = p.num( "prize", d.m_prize );
            d.m_prices = p.num( "prices", d.m_prices );
            d.m_repair = p.num( "repair", d.m_repair );
            d.m_rubber = p.num( "rubber", d.m_rubber );
            d.m_deaths = p.num( "deaths", d.m_deaths );
            d.m_growth = p.num( "growth", d.m_growth );
            d.m_interest = p.num( "interest", d.m_interest );
        }
        else if ( t == "news" )
        {
            NewsDef n;
            n.m_event = b.m_name;
            n.m_id = b.m_head.arg( 1 );
            n.m_head = p.text( "head" );
            n.m_text = p.text( "text" );
            m_news.push_back( n );
        }
        else if ( t == "ad" )
            m_ads.push_back( p.text( "text" ) );
        else if ( t == "story" || t == "story_en" )
        {
            StoryDef &s = m_stories[b.m_name];
            s.m_key = b.m_name;
            if ( t == "story" )
            {
                s.m_de = b.m_prose;
                if ( p.has( "title" ) ) s.m_title.m_de = p.str( "title" );
                if ( p.has( "title_en" ) ) s.m_title.m_en = p.str( "title_en" );
            }
            else
            {
                s.m_en = b.m_prose;
                if ( p.has( "title" ) && s.m_title.m_en.empty() ) s.m_title.m_en = p.str( "title" );
            }
        }
        else if ( t == "award" )
        {
            dup( b, m_awards.count( b.m_name ) > 0 );
            AwardDef &a = m_awards[b.m_name];
            a.m_key = b.m_name;
            a.m_title = p.text( "title" );
            a.m_desc = p.text( "desc" );
            m_awardOrder.push_back( b.m_name );
        }
        else if ( t == "sponsor" )
        {
            SponsorDef &s = m_sponsors[b.m_name];
            s.m_key = b.m_name;
            s.m_title = p.text( "title" );
            s.m_desc = p.text( "desc" );
            s.m_league = p.integer( "league", 5 );
            if ( const Line *bo = lineOf( b, "bonus" ) )
                for ( const auto &kv : bo->m_kv ) s.m_bonus[kv.first] = (int)toNumber( kv.second, 0 );
        }
    }
    // The cars by class, then by price.
    for ( const auto &[n, c] : m_cars ) m_carOrder.push_back( n );
    std::sort( m_carOrder.begin(), m_carOrder.end(), [&]( const std::string &a, const std::string &b ) {
        const CarDef &x = m_cars[a], &y = m_cars[b];
        return x.m_class != y.m_class ? x.m_class < y.m_class : x.m_price != y.m_price ? x.m_price < y.m_price : a < b;
    } );
    if ( m_difficulties.empty() ) m_difficulties.resize( 5 );
    return m_errors.empty();
}

TrackDef *Catalog::addTrack( const Block &b, std::vector<std::string> &errors )
{
    size_t before = errors.size();
    readTrack( b, m_tracks[b.m_name], errors );
    if ( errors.size() > before )
    {
        m_tracks.erase( b.m_name );
        return nullptr;
    }
    return &m_tracks[b.m_name];
}

void Catalog::check()
{
    for ( const auto &[n, c] : m_cars )
    {
        if ( !m_models.count( c.m_model ) ) m_warnings.push_back( c.m_where + ": car " + n + ": no model " + c.m_model );
        if ( !m_guns.empty() && !m_guns.count( c.m_gun ) ) m_errors.push_back( c.m_where + ": car " + n + ": no gun " + c.m_gun );
        if ( c.m_title.m_en.empty() && c.m_desc.m_en.empty() && !c.m_desc.m_de.empty() )
            m_warnings.push_back( c.m_where + ": car " + n + " has no English description" );
    }
    for ( const auto &[n, t] : m_tracks )
    {
        if ( !m_venues.empty() && !m_venues.count( t.m_venue ) )
            m_errors.push_back( t.m_where + ": track " + n + ": no venue \"" + t.m_venue + "\"" );
        if ( t.m_title.m_en.empty() ) m_warnings.push_back( t.m_where + ": track " + n + " has no English title" );
        for ( const TrackItem &it : t.m_items )
        {
            const Line &l = it.m_line;
            if ( it.m_type == "prop" || it.m_type == "row" || it.m_type == "place" || it.m_type == "lamps" )
                if ( !m_models.count( it.m_what ) ) m_warnings.push_back( l.where() + ": no model \"" + it.m_what + "\"" );
            if ( it.m_type == "obstacle" && l.has( "model" ) && !m_models.count( l.str( "model" ) ) )
                m_warnings.push_back( l.where() + ": no model \"" + l.str( "model" ) + "\"" );
        }
    }
    for ( const auto &[n, v] : m_venues )
    {
        for ( const auto &pr : v.m_litter )
            if ( !m_models.count( pr.first ) ) m_warnings.push_back( "venue " + n + ": no litter model \"" + pr.first + "\"" );
        for ( const auto &pr : v.m_loose )
            if ( !m_models.count( pr.first ) ) m_warnings.push_back( "venue " + n + ": no loose model \"" + pr.first + "\"" );
        for ( const auto &pr : v.m_props )
            if ( !m_models.count( pr.first ) ) m_warnings.push_back( "venue " + n + ": no prop model \"" + pr.first + "\"" );
    }
    for ( const auto &[n, r] : m_rivals )
    {
        if ( !m_cars.empty() && !m_cars.count( r.m_car ) )
            m_warnings.push_back( "rival " + n + ": no car \"" + r.m_car + "\"" );
        if ( !r.m_boss.empty() && !m_cars.count( r.m_boss ) )
            m_warnings.push_back( "rival " + n + ": no boss car \"" + r.m_boss + "\"" );
    }
    for ( const auto &[n, s] : m_scenes )
    {
        if ( !m_venues.count( s.m_venue ) ) m_warnings.push_back( "scene " + n + ": no venue \"" + s.m_venue + "\"" );
        for ( const ScenePiece &p : s.m_pieces )
            if ( p.m_kind == ScenePiece::MODEL ? !m_models.count( p.m_what ) : p.m_what != "any" && !m_cars.count( p.m_what ) )
                m_warnings.push_back( "scene " + n + ": no " + ( p.m_kind == ScenePiece::MODEL ? "model" : "car" ) + " \"" + p.m_what + "\"" );
    }
    for ( const auto &[id, l] : m_leagues )
        for ( const std::string &v : l.m_venues )
            if ( !m_venues.empty() && !m_venues.count( v ) )
                m_warnings.push_back( "league " + std::to_string( id ) + ": no venue \"" + v + "\"" );
}

template <typename M> static const typename M::mapped_type *findIn( const M &m, const typename M::key_type &n )
{
    auto it = m.find( n );
    return it != m.end() ? &it->second : nullptr;
}

const ModelDef     *Catalog::model( const std::string &n ) const { return findIn( m_models, n ); }
const CarDef       *Catalog::car( const std::string &n ) const { return findIn( m_cars, n ); }
const GunDef       *Catalog::gun( const std::string &n ) const { return findIn( m_guns, n ); }
const GearDef      *Catalog::gear( const std::string &n ) const { return findIn( m_gear, n ); }
const UpgradeDef   *Catalog::upgrade( const std::string &n ) const { return findIn( m_upgrades, n ); }
const VenueDef     *Catalog::venue( const std::string &n ) const { return findIn( m_venues, n ); }
const TrackDef     *Catalog::track( const std::string &n ) const { return findIn( m_tracks, n ); }
const RivalDef     *Catalog::rival( const std::string &n ) const { return findIn( m_rivals, n ); }
const CharacterDef *Catalog::character( const std::string &n ) const { return findIn( m_characters, n ); }
const LeagueDef    *Catalog::league( int id ) const { return findIn( m_leagues, id ); }
const StoryDef     *Catalog::story( const std::string &n ) const { return findIn( m_stories, n ); }
const SponsorDef   *Catalog::sponsor( const std::string &n ) const { return findIn( m_sponsors, n ); }
const AwardDef     *Catalog::award( const std::string &n ) const { return findIn( m_awards, n ); }

const DifficultyDef &Catalog::difficulty( int i ) const
{
    static const DifficultyDef none;
    if ( m_difficulties.empty() ) return none;
    return m_difficulties[(size_t)std::clamp( i, 0, (int)m_difficulties.size() - 1 )];
}

std::vector<const NewsDef *> Catalog::news( const std::string &event ) const
{
    std::vector<const NewsDef *> out;
    for ( const NewsDef &n : m_news )
        if ( n.m_event == event ) out.push_back( &n );
    return out;
}

}   // namespace grave
