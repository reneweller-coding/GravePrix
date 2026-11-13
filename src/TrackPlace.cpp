/**
 * @file TrackPlace.cpp
 * @brief A track's things: pickups, hazards, obstacles, patches, props in rows and scattered, lamps, bridges; and the
 *        designers' map.
 * @author Rene Weller
 */
#include "Forge.h"
#include "Track.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace grave
{

namespace
{

/** @return a model's reach on the ground (built once without the graphics card) */
float reachOf( const Catalog &cat, const std::string &name )
{
    static std::map<std::string, float> cache;
    auto                                it = cache.find( name );
    if ( it != cache.end() ) return it->second;
    float           r = 1.0f;
    const ModelDef *d = cat.model( name );
    if ( d )
    {
        ModelMesh m = Forge::build( *d, false );
        r = m.m_radius;
        for ( Mesh *mesh : { &m.m_base, &m.m_head, &m.m_spin } )
        {
            MemFree( mesh->vertices );
            MemFree( mesh->normals );
            MemFree( mesh->colors );
            MemFree( mesh->texcoords );
            MemFree( mesh->texcoords2 );
        }
    }
    return cache[name] = r;
}

/** @brief 3x5 digits for the map (no font needed). */
const char *Digits[10] = { "111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
                           "111100111001111", "111100111101111", "111001010010010", "111101111101111", "111101111001111" };

void drawNumber( Image &img, int x, int y, int value, int px, Color c )
{
    std::string s = std::to_string( value );
    for ( size_t k = 0; k < s.size(); ++k )
    {
        const char *g = Digits[s[k] - '0'];
        for ( int r = 0; r < 5; ++r )
            for ( int q = 0; q < 3; ++q )
                if ( g[r * 3 + q] == '1' )
                    ImageDrawRectangle( &img, x + (int)k * 4 * px + q * px, y + r * px, px, px, c );
    }
}

}   // namespace

void Track::place( const TrackDef &def, const Catalog &cat )
{
    m_weatherTo = def.m_weatherTo;
    m_weatherLap = def.m_weatherLap;
    float latSign = ( m_reverse != m_mirror ) ? -1.0f : 1.0f;
    auto  S = [&]( float s ) { return m_reverse ? wrap( m_length - s ) : wrap( s ); };
    for ( const TrackItem &it : def.m_items )
    {
        const Line &l = it.m_line;
        float       s = S( l.num( "at", 0 ) );
        const TrackSample &t = sampleAt( s );
        float       lat = latSign * l.num( "lat", 0 ) * t.m_half;
        if ( it.m_type == "pickup" )
            m_pickups.push_back( { it.m_what, pointAt( s, lat ), s } );
        else if ( it.m_type == "hazard" )
            m_hazards.push_back( { it.m_what, pointAt( s, lat ), l.num( "r", it.m_what == "barrel" ? 0.6f : 2.5f ) } );
        else if ( it.m_type == "train" || it.m_type == "drawbridge" || it.m_type == "rockfall" )
        {
            MoverSpot m;
            m.m_kind = it.m_type;
            m.m_len = l.num( "len", it.m_type == "rockfall" ? 60.0f : 24.0f );
            m.m_period = l.num( "period", it.m_type == "train" ? 42.0f : it.m_type == "drawbridge" ? 50.0f : 7.0f );
            m.m_offset = l.num( "offset", 0.0f );
            // A rockfall runs forwards from its start, in either direction of travel.
            m.m_s = it.m_type == "rockfall" && m_reverse ? wrap( s - m.m_len ) : s;
            const TrackSample &ms = sampleAt( m.m_s );
            m.m_pos = ms.m_pos;
            m.m_dir = ms.m_dir;
            m.m_right = ms.m_right;
            m.m_half = ms.m_half;
            m_movers.push_back( m );
        }
        else if ( it.m_type == "obstacle" )
            m_obstacles.push_back( { pointAt( s, lat ), l.num( "r", 1.0f ), l.str( "model", "" ), l.num( "rot", 0 ) } );
        else if ( it.m_type == "gate" )
            m_gates.push_back( { s, lat, l.num( "w", 4.5f ) } );
        else if ( it.m_type == "patch" )
        {
            int   surf = surfaceOf( it.m_what );
            float s0 = l.num( "at", 0 ), s1 = s0 + l.num( "len", 20 );
            PatchSpot p;
            p.m_surface = surf < 0 ? SurfaceE::ICE : (SurfaceE)surf;
            p.m_s0 = m_reverse ? S( s1 ) : S( s0 );
            p.m_s1 = m_reverse ? S( s0 ) : S( s1 );
            if ( l.has( "w" ) )
            {
                float w = l.num( "w", 8 ) * 0.5f;
                p.m_lat0 = lat - w;
                p.m_lat1 = lat + w;
            }
            m_patches.push_back( p );
        }
        else if ( it.m_type == "prop" || it.m_type == "row" || it.m_type == "lamps" )
        {
            std::string side = l.str( "side", it.m_type == "prop" ? "right" : "both" );
            float       off = l.num( "off", 1.5f ), rot = l.num( "rot", 0 );
            float       from = it.m_type == "lamps" ? 0.0f : l.num( "at", 0 );
            float       length = it.m_type == "prop" ? 0.0f : it.m_type == "lamps" ? m_length - 1.0f : l.num( "len", 50 );
            float       every = it.m_type == "prop" ? 1.0f : l.num( "every", it.m_type == "lamps" ? 45.0f : 4.0f );
            float       r = reachOf( cat, it.m_what );
            for ( float d = 0; d <= length; d += std::max( 0.5f, every ) )
            {
                float              ss = S( from + d );
                const TrackSample &ts = sampleAt( ss );
                for ( int sd = 0; sd < 2; ++sd )
                {
                    bool left = sd == 0;
                    if ( side == "left" && !left ) continue;
                    if ( side == "right" && left ) continue;
                    // "left"/"right" as designed: a reversed or mirrored track swaps them.
                    float sg = ( left ? -1.0f : 1.0f ) * latSign;
                    float o = ts.m_half + ts.m_shoulder + 1.2f + off + r * 0.5f;
                    Vector2 p = pointAt( ss, sg * o );
                    Vector2 push;
                    if ( outside( p, push ) < r * 0.6f + 0.8f ) continue;   // another part of the track is there
                    float face = headingOf( ts.m_dir ) / Deg + ( sg > 0 ? -90.0f : 90.0f );   // facing the road
                    m_props.push_back( { it.m_what, p, face + rot, 1.0f } );
                }
            }
        }
        else if ( it.m_type == "place" )
        {
            Vector2 p = { l.num( "x" ), l.num( "z" ) };
            if ( m_mirror ) p.x = 2 * def.m_start.x - p.x;
            m_props.push_back( { it.m_what, p, ( m_mirror ? -1.0f : 1.0f ) * l.num( "rot", 0 ), l.num( "scale", 1.0f ) } );
        }
        else if ( it.m_type == "bridge" )
        {
            float s0 = l.num( "at", 0 ), s1 = s0 + l.num( "len", 40 );
            for ( float d = s0; d <= s1; d += m_step ) m_s[indexAt( S( d ) )].m_bridge = true;
        }
    }
    for ( const PatchSpot &pa : m_patches )
    {
        float span = pa.m_s1 >= pa.m_s0 ? pa.m_s1 - pa.m_s0 : pa.m_s1 + m_length - pa.m_s0;
        for ( float d = -6.0f; d <= span; d += m_step )
        {
            TrackSample &t = m_s[indexAt( pa.m_s0 + d )];
            if ( t.m_line >= pa.m_lat0 - 1.0f && t.m_line <= pa.m_lat1 + 1.0f )
                t.m_lineGrip = std::min( t.m_lineGrip, surfaceGrip( pa.m_surface ) );
        }
    }
    if ( m_pickups.empty() )
    {
        // ~One every 120 m, alternating sides; the kinds in a fixed round.
        static const char *round[] = { "cash", "mystery", "wrench", "ammo", "cash", "nitro", "mine", "rocket", "cash", "wrench" };
        Rng rng;
        uint32_t h = 2166136261u;
        for ( char c : m_name ) h = ( h ^ (unsigned char)c ) * 16777619u;
        rng.seed( h );
        int k = 0;
        for ( float s = 70.0f; s < m_length - 30.0f; s += 120.0f, ++k )
        {
            const TrackSample &t = sampleAt( s );
            float              lat = rng.range( -0.55f, 0.55f ) * t.m_half;
            m_pickups.push_back( { round[k % 10], pointAt( s, lat ), s } );
        }
    }
}

void Track::scenes( const Catalog &cat )
{
    // The venue's composed set pieces along the road, every 70 to 140 metres, on alternating sides, facing the track.
    if ( !m_venue || m_arena ) return;
    std::vector<const SceneDef *> pool;
    float                         total = 0;
    for ( const auto &[n, s] : cat.m_scenes )
        if ( s.m_venue == m_venue->m_name && s.m_weight > 0 && n != m_venue->m_name + "_start" )
        {
            pool.push_back( &s );
            total += s.m_weight;
        }
    if ( pool.empty() ) return;
    Rng      rng;
    uint32_t h = 2166136261u;
    for ( char c : m_name ) h = ( h ^ (unsigned char)c ) * 16777619u;
    rng.seed( h ^ 0x2545f491u );
    // Cars for the scenes: the venue's league and the ones below (the wrecks of better days).
    std::vector<std::string> cars;
    for ( const std::string &k : cat.m_carOrder )
    {
        const CarDef &d = cat.m_cars.at( k );
        if ( d.m_class < 6 && d.m_league >= m_venue->m_league ) cars.push_back( k );
    }
    std::vector<std::pair<Vector2, float>> taken;
    for ( const PropSpot &p : m_props ) taken.push_back( { p.m_pos, reachOf( cat, p.m_model ) } );
    std::vector<const SceneDef *> recent;
    int side = rng.chance( 0.5f ) ? 1 : -1;
    // The start: the venue's grandstand scene beside the line, the rest after it.
    auto startScene = cat.m_scenes.find( m_venue->m_name + "_start" );
    bool first = startScene != cat.m_scenes.end(), wasFirst = false;
    for ( float s = first ? m_length - 15.0f : rng.range( 20.0f, 45.0f ); s < m_length - 30.0f || first;
          s = wasFirst ? rng.range( 25.0f, 45.0f ) : s + rng.range( 25.0f, 50.0f ) )
    {
        // A scene not used just before.
        wasFirst = first;
        const SceneDef *sc = first ? &startScene->second : nullptr;
        first = false;
        for ( int tries = 0; tries < 6 && !sc; ++tries )
        {
            float pick = rng.range( 0, total );
            for ( const SceneDef *c : pool )
            {
                if ( pick < c->m_weight )
                {
                    sc = c;
                    break;
                }
                pick -= c->m_weight;
            }
            if ( sc && pool.size() > 2 && std::find( recent.begin(), recent.end(), sc ) != recent.end() ) sc = nullptr;
        }
        if ( !sc ) continue;
        side = -side;
        bool done = false;
        for ( int flip = 0; flip < 2 && !done; ++flip, side = -side )
        {
            const TrackSample &ts = sampleAt( s );
            // Its middle a few metres behind its front row (the pieces nearest the road stand at about z -3).
            float   off = ts.m_half + ts.m_shoulder + sc->m_near + 3.5f;
            Vector2 c = pointAt( s, side * off );
            Vector2 push;
            if ( outside( c, push ) < sc->m_near + 3.0f ) continue;
            bool free = true;
            for ( const auto &tk : taken )
                if ( len( sub( tk.first, c ) ) < ( tk.second + sc->m_size ) * 0.75f ) free = false;
            if ( !free ) continue;
            // The frame: x along the road, z away from it.
            Vector2 along = ts.m_dir, away = mul( ts.m_right, (float)side );
            float   face = std::atan2( -away.x, away.y ) / Deg;   // the heading towards the road
            for ( const ScenePiece &pc : sc->m_pieces )
            {
                PropSpot p;
                p.m_pos = add( add( c, mul( along, pc.m_x ) ), mul( away, pc.m_z - 0.0f ) );
                p.m_rot = face + pc.m_rot;
                p.m_scale = pc.m_scale;
                p.m_tilt = pc.m_tilt;
                p.m_seed = (uint32_t)rng.below( 1 << 30 );
                if ( pc.m_kind == ScenePiece::MODEL )
                {
                    if ( !cat.model( pc.m_what ) ) continue;
                    p.m_model = pc.m_what;
                }
                else
                {
                    p.m_model = pc.m_what == "any" && !cars.empty() ? cars[(size_t)rng.below( (int)cars.size() )] : pc.m_what;
                    if ( !cat.car( p.m_model ) ) continue;
                    p.m_car = pc.m_kind == ScenePiece::CAR ? 1 : 2;
                }
                m_props.push_back( p );
            }
            taken.push_back( { c, sc->m_size } );
            recent.push_back( sc );
            if ( recent.size() > 3 ) recent.erase( recent.begin() );
            done = true;
        }
    }
}

void Track::scatter( const Catalog &cat )
{
    if ( !m_venue || m_venue->m_props.empty() ) return;
    float density = m_venue->m_density * ( m_def ? m_def->m_density : 1.0f );
    if ( density <= 0 ) return;
    float total = 0;
    for ( const auto &p : m_venue->m_props ) total += p.second;
    if ( total <= 0 ) return;
    Rng      rng;
    uint32_t h = 2166136261u;
    for ( char c : m_name ) h = ( h ^ (unsigned char)c ) * 16777619u;
    rng.seed( h ^ 0x51ed270bu );
    const float margin = 70.0f;
    Rectangle   area = { m_bounds.x - margin, m_bounds.y - margin, m_bounds.width + 2 * margin, m_bounds.height + 2 * margin };
    int         want = (int)( area.width * area.height * 0.0016f * density );
    want = std::min( want, 1800 );
    // Fixed props first: nothing may stand on them.
    std::vector<std::pair<Vector2, float>> taken;
    for ( const PropSpot &p : m_props ) taken.push_back( { p.m_pos, reachOf( cat, p.m_model ) } );
    std::vector<std::vector<int>> grid;
    const float cell = 12.0f;
    int gw = (int)( area.width / cell ) + 1, gh = (int)( area.height / cell ) + 1;
    grid.assign( (size_t)gw * gh, {} );
    auto cellOf = [&]( Vector2 p, int &x, int &y ) {
        x = std::clamp( (int)( ( p.x - area.x ) / cell ), 0, gw - 1 );
        y = std::clamp( (int)( ( p.y - area.y ) / cell ), 0, gh - 1 );
    };
    for ( size_t i = 0; i < taken.size(); ++i )
    {
        int x, y;
        cellOf( taken[i].first, x, y );
        grid[(size_t)y * gw + x].push_back( (int)i );
    }
    int placed = 0;
    for ( int tries = 0; tries < want * 4 && placed < want; ++tries )
    {
        float pick = rng.range( 0, total );
        const std::string *model = &m_venue->m_props.back().first;
        for ( const auto &p : m_venue->m_props )
        {
            if ( pick < p.second )
            {
                model = &p.first;
                break;
            }
            pick -= p.second;
        }
        float   r = reachOf( cat, *model );
        Vector2 p = { rng.range( area.x, area.x + area.width ), rng.range( area.y, area.y + area.height ) };
        Vector2 push;
        float   away = outside( p, push );
        if ( away < r + 2.2f ) continue;
        if ( away > 20.0f && rng.chance( std::min( 0.9f, ( away - 20.0f ) / 35.0f ) ) ) continue;   // thinner far out
        int  cx, cy;
        bool free = true;
        cellOf( p, cx, cy );
        int reachCells = (int)std::ceil( ( r + 14.0f ) / cell );
        for ( int y = std::max( 0, cy - reachCells ); y <= std::min( gh - 1, cy + reachCells ) && free; ++y )
            for ( int x = std::max( 0, cx - reachCells ); x <= std::min( gw - 1, cx + reachCells ) && free; ++x )
                for ( int i : grid[(size_t)y * gw + x] )
                    if ( len( sub( taken[i].first, p ) ) < ( taken[i].second + r ) * 0.8f )
                    {
                        free = false;
                        break;
                    }
        if ( !free ) continue;
        // Made things stand square to the road (or across it); grown things and loose junk any way.
        static const char *wild[] = { "tree", "cactus", "rock", "reeds", "stump", "tumbleweed", "fir", "bush", "crater", "pile",
                                      "skull", "ribcage", "gator", "bog", "snow_bank", "oil_puddle", "wreath" };
        bool natural = false;
        for ( const char *w : wild ) natural = natural || model->find( w ) != std::string::npos;
        float rot = rng.range( 0, 360 );
        if ( !natural )
        {
            const TrackSample &near = m_s[(size_t)locate( p, -1 ).m_i];
            rot = std::atan2( near.m_dir.x, -near.m_dir.y ) / Deg + 90.0f * rng.below( 4 ) + rng.range( -6.0f, 6.0f );
        }
        m_props.push_back( { *model, p, rot, rng.range( 0.9f, 1.1f ) } );
        taken.push_back( { p, r } );
        grid[(size_t)cy * gw + cx].push_back( (int)taken.size() - 1 );
        ++placed;
    }
}

void Track::litter( const Catalog &cat )
{
    // Small things in a band one to nine metres beyond the road's edge: about one every four metres a side.
    if ( !m_venue || m_venue->m_litter.empty() ) return;
    float total = 0;
    for ( const auto &p : m_venue->m_litter ) total += p.second;
    Rng      rng;
    uint32_t h = 2166136261u;
    for ( char c : m_name ) h = ( h ^ (unsigned char)c ) * 16777619u;
    rng.seed( h ^ 0x7f4a7c15u );
    for ( float s = 0; s < m_length; s += rng.range( 1.5f, 3.0f ) )
    {
        const TrackSample &ts = sampleAt( s );
        int     side = rng.chance( 0.5f ) ? 1 : -1;
        Vector2 p = pointAt( s, side * ( ts.m_half + ts.m_shoulder + rng.range( 1.0f, 9.0f ) ) );
        Vector2 push;
        if ( outside( p, push ) < 1.0f ) continue;
        float pick = rng.range( 0, total );
        const std::string *model = &m_venue->m_litter.back().first;
        for ( const auto &q : m_venue->m_litter )
        {
            if ( pick < q.second )
            {
                model = &q.first;
                break;
            }
            pick -= q.second;
        }
        if ( !cat.model( *model ) ) continue;
        m_props.push_back( { *model, p, rng.range( 0, 360 ), rng.range( 0.85f, 1.15f ) } );
    }
}

namespace
{

/** @return how far a model reaches from its middle on the ground and its top (from its part lines) */
std::pair<float, float> footprint( const ModelDef &m )
{
    float r = 0.2f, top = 0.3f;
    for ( const Line &p : m.m_parts )
    {
        float x = p.num( "x", 0 ), y = p.num( "y", 0 ), z = p.num( "z", 0 );
        float ext = std::max( { p.num( "w", 0 ) * 0.5f, p.num( "d", 0 ) * 0.5f, p.num( "r", 0 ), p.num( "rx", 0 ), p.num( "rz", 0 ) } );
        float up = std::max( { p.num( "h", 0 ) * 0.5f, p.num( "r", 0 ), p.num( "ry", 0 ) } );
        r = std::max( r, std::sqrt( x * x + z * z ) + ext );
        top = std::max( top, y + up );
    }
    return { r * m.m_scale, top * m.m_scale };
}

}   // namespace

void Track::loose( const Catalog &cat )
{
    // Small things on the shoulders, inside the walls, where a car that runs wide or cuts a corner knocks them over:
    // one now and then on either side, sometimes two or three together. Not at the start, the crossings, the
    // clockwork, nor beside a pickup or an obstacle.
    m_loose.clear();
    if ( !m_venue || m_venue->m_loose.empty() ) return;
    float total = 0;
    for ( const auto &p : m_venue->m_loose ) total += p.second;
    Rng      rng;
    uint32_t h = 2166136261u;
    for ( char c : m_name ) h = ( h ^ (unsigned char)c ) * 16777619u;
    rng.seed( h ^ 0x51ed270bu );
    float end = m_arena ? m_length : m_length - 45.0f;
    for ( float s = m_arena ? 0.0f : 25.0f; s < end; s += rng.range( 12.0f, 30.0f ) )
    {
        const TrackSample &ts = sampleAt( s );
        if ( ts.m_shoulder < 1.6f || ts.m_bridge ) continue;
        bool nearMover = false;
        for ( const MoverSpot &mv : m_movers )
            if ( std::fabs( delta( s, mv.m_s ) ) < mv.m_len * 0.5f + 18.0f ) nearMover = true;
        if ( nearMover ) continue;
        float pick = rng.range( 0, total );
        const std::string *model = &m_venue->m_loose.back().first;
        for ( const auto &q : m_venue->m_loose )
        {
            if ( pick < q.second )
            {
                model = &q.first;
                break;
            }
            pick -= q.second;
        }
        const ModelDef *md = cat.model( *model );
        if ( !md ) continue;
        auto [r, top] = footprint( *md );
        int  side = rng.chance( 0.5f ) ? 1 : -1;
        int  n = rng.chance( 0.3f ) ? 2 + rng.below( 2 ) : 1;
        float lat0 = ts.m_half + rng.range( 0.5f + r, std::max( 0.6f + r, ts.m_shoulder - 0.4f - r ) );
        for ( int k = 0; k < n; ++k )
        {
            float   ss = wrap( s + k * ( 2.0f * r + 0.5f ) );
            Vector2 p = pointAt( ss, side * ( lat0 + rng.range( -0.3f, 0.3f ) ) );
            Vector2 push;
            if ( outside( p, push ) > -r - 0.2f ) continue;   // inside the walls, all of it
            bool busy = false;
            for ( const PickupSpot &pk : m_pickups ) busy = busy || dist2( pk.m_pos, p ) < 16.0f;
            for ( const ObstacleSpot &o : m_obstacles ) busy = busy || dist2( o.m_pos, p ) < ( o.m_r + 2.0f ) * ( o.m_r + 2.0f );
            for ( const HazardSpot &hz : m_hazards ) busy = busy || dist2( hz.m_pos, p ) < 9.0f;
            if ( busy ) continue;
            LooseSpot l;
            l.m_model = *model;
            l.m_pos = p;
            l.m_rot = rng.range( 0, 360 );
            l.m_scale = rng.range( 0.92f, 1.08f );
            l.m_r = r * l.m_scale;
            l.m_h = top * l.m_scale;
            l.m_kind = *model == "tumbleweed" || *model == "tire_single" ? 1 : *model == "snowman" ? 2 : *model == "hydrant" ? 3 : 0;
            m_loose.push_back( l );
        }
    }
}

bool Track::map( const std::string &path, const Catalog &cat ) const
{
    const float margin = 30.0f;
    float       w = m_bounds.width + 2 * margin, h = m_bounds.height + 2 * margin;
    float       px = clampf( 1400.0f / std::max( w, h ), 0.6f, 3.0f );
    int         iw = (int)( w * px ), ih = (int)( h * px );
    Color       groundC = m_venue ? m_venue->m_groundColor : Color{ 60, 80, 50, 255 };
    Color       roadC = m_venue ? m_venue->m_roadColor : Color{ 70, 70, 76, 255 };
    Color       shoulderC = m_venue ? m_venue->m_shoulderColor : Color{ 110, 100, 80, 255 };
    roadC = scaleColor( roadC, 1.5f );
    Image img = GenImageColor( iw, ih, groundC );
    auto  P = [&]( Vector2 p ) { return Vector2{ ( p.x - m_bounds.x + margin ) * px, ( p.y - m_bounds.y + margin ) * px }; };
    int   n = (int)m_s.size();
    auto  quad = [&]( Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col ) {
        // Both windings, so the triangle routine fills it whichever way it turns.
        ImageDrawTriangle( &img, P( a ), P( b ), P( c ), col );
        ImageDrawTriangle( &img, P( a ), P( c ), P( b ), col );
        ImageDrawTriangle( &img, P( a ), P( c ), P( d ), col );
        ImageDrawTriangle( &img, P( a ), P( d ), P( c ), col );
    };
    for ( int pass = 0; pass < 2; ++pass )
        for ( int i = 0; i < n; ++i )
        {
            const TrackSample &a = m_s[i], &b = m_s[( i + 1 ) % n];
            float              ea = pass == 0 ? a.m_half + a.m_shoulder : a.m_half, eb = pass == 0 ? b.m_half + b.m_shoulder : b.m_half;
            Color              c = pass == 0 ? shoulderC : roadC;
            if ( pass == 1 && a.m_surface != ( m_venue ? m_venue->m_road : SurfaceE::ASPHALT ) )
                c = mixColor( roadC, Color{ 160, 120, 70, 255 }, 0.5f );
            if ( pass == 1 && a.m_bridge ) c = Color{ 120, 100, 80, 255 };
            quad( add( a.m_pos, mul( a.m_right, -ea ) ), add( a.m_pos, mul( a.m_right, ea ) ),
                  add( b.m_pos, mul( b.m_right, eb ) ), add( b.m_pos, mul( b.m_right, -eb ) ), c );
        }
    // Patches.
    for ( const PatchSpot &pa : m_patches )
        for ( float s = pa.m_s0; ; s += m_step )
        {
            float ss = wrap( s );
            if ( pa.m_s0 <= pa.m_s1 ? s > pa.m_s1 : ( ss > pa.m_s1 && ss < pa.m_s0 ) ) break;
            const TrackSample &t = sampleAt( ss );
            float lo = std::max( pa.m_lat0, -t.m_half ), hi = std::min( pa.m_lat1, t.m_half );
            Color c = pa.m_surface == SurfaceE::ICE ? Color{ 190, 230, 255, 255 } : pa.m_surface == SurfaceE::WATER ? Color{ 60, 110, 170, 255 }
                    : pa.m_surface == SurfaceE::OIL ? Color{ 20, 20, 24, 255 } : Color{ 150, 110, 60, 255 };
            ImageDrawLineEx( &img, P( pointAt( ss, lo ) ), P( pointAt( ss, hi ) ), (int)std::max( 2.0f, m_step * px ), c );
            if ( s > m_length * 2 ) break;
        }
    // Walls, the racing line, the start, the distances.
    for ( int i = 0; i < n; ++i )
    {
        const TrackSample &a = m_s[i], &b = m_s[( i + 1 ) % n];
        for ( int side = 0; side < 2; ++side )
        {
            bool on = side == 0 ? a.m_wallL && b.m_wallL : a.m_wallR && b.m_wallR;
            if ( !on ) continue;
            float sg = side == 0 ? -1.0f : 1.0f;
            ImageDrawLineEx( &img, P( add( a.m_pos, mul( a.m_right, sg * ( a.m_half + a.m_shoulder + 0.6f ) ) ) ),
                             P( add( b.m_pos, mul( b.m_right, sg * ( b.m_half + b.m_shoulder + 0.6f ) ) ) ),
                             std::max( 2, (int)( 1.2f * px ) ), Color{ 230, 230, 235, 255 } );
        }
        ImageDrawLineEx( &img, P( add( a.m_pos, mul( a.m_right, a.m_line ) ) ), P( add( b.m_pos, mul( b.m_right, b.m_line ) ) ), 1,
                         Color{ 255, 220, 60, 160 } );
    }
    const TrackSample &s0 = m_s[0];
    ImageDrawLineEx( &img, P( add( s0.m_pos, mul( s0.m_right, -s0.m_half ) ) ), P( add( s0.m_pos, mul( s0.m_right, s0.m_half ) ) ),
                     std::max( 3, (int)( 2 * px ) ), Color{ 255, 255, 255, 255 } );
    Vector2 arrow = add( s0.m_pos, mul( s0.m_dir, 14.0f ) );
    ImageDrawLineEx( &img, P( s0.m_pos ), P( arrow ), std::max( 2, (int)px ), Color{ 255, 60, 60, 255 } );
    ImageDrawCircleV( &img, P( arrow ), (int)( 2 * px ) + 2, Color{ 255, 60, 60, 255 } );
    for ( float s = 100.0f; s < m_length; s += 100.0f )
    {
        const TrackSample &t = sampleAt( s );
        Vector2            q = P( add( t.m_pos, mul( t.m_right, -( t.m_half + t.m_shoulder + 6.0f ) ) ) );
        drawNumber( img, (int)q.x - 6, (int)q.y - 5, (int)s, std::max( 2, (int)px ), Color{ 255, 255, 255, 255 } );
    }
    // Things.
    for ( const PropSpot &p : m_props )
    {
        float r = reachOf( cat, p.m_model ) * p.m_scale;
        ImageDrawCircleV( &img, P( p.m_pos ), std::max( 1, (int)( r * px * 0.7f ) ), Color{ 20, 20, 24, 200 } );
    }
    for ( const ObstacleSpot &o : m_obstacles ) ImageDrawCircleV( &img, P( o.m_pos ), std::max( 2, (int)( o.m_r * px ) ), Color{ 240, 240, 240, 255 } );
    for ( const HazardSpot &hz : m_hazards ) ImageDrawCircleV( &img, P( hz.m_pos ), std::max( 2, (int)( hz.m_r * px ) ), Color{ 255, 120, 0, 255 } );
    for ( const PickupSpot &pk : m_pickups )
    {
        Color c = pk.m_kind == "cash" ? Color{ 60, 220, 60, 255 } : pk.m_kind == "wrench" ? Color{ 80, 160, 255, 255 }
                : pk.m_kind == "mystery" ? Color{ 230, 80, 230, 255 } : Color{ 255, 220, 60, 255 };
        ImageDrawCircleV( &img, P( pk.m_pos ), std::max( 2, (int)( 1.5f * px ) ), c );
    }
    for ( Vector2 p : m_problems )
        for ( int k = 0; k < 3; ++k )
            ImageDrawCircleLines( &img, (int)P( p ).x, (int)P( p ).y, (int)( ( 10 + k ) * px ), Color{ 255, 0, 0, 255 } );
    bool ok = ExportImage( img, path.c_str() );
    UnloadImage( img );
    return ok;
}

}   // namespace grave
