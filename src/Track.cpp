/**
 * @file Track.cpp
 * @brief Building a track: the path, the samples, the racing line, the walls, the things on and beside it, the
 *        checks and the map.
 * @author Rene Weller
 */
#include "Track.h"

#include "Forge.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace grave
{

namespace
{

/** @return a style's index in the table (added when new) */
int styleIndex( std::vector<std::string> &styles, const std::string &s )
{
    for ( size_t i = 0; i < styles.size(); ++i )
        if ( styles[i] == s ) return (int)i;
    styles.push_back( s );
    return (int)styles.size() - 1;
}

/** @return the closest point's parameter on segment a-b to p */
float closestT( Vector2 a, Vector2 b, Vector2 p )
{
    Vector2 ab = sub( b, a );
    float   l2 = dot( ab, ab );
    return l2 > 1e-9f ? clampf( dot( sub( p, a ), ab ) / l2, 0.0f, 1.0f ) : 0.0f;
}

/** @return "s=123 m" */
std::string at( float s )
{
    char b[32];
    snprintf( b, sizeof b, "s=%.0f m", s );
    return b;
}

}   // namespace

bool Track::build( const TrackDef &def, const VenueDef *venue, bool reverse, bool mirror,
                   std::vector<std::string> &errors, std::vector<std::string> &warnings )
{
    *this = Track();
    m_name = def.m_name;
    m_def = &def;
    m_venue = venue;
    m_reverse = reverse;
    m_mirror = mirror;
    m_arena = def.m_arena;
    m_laps = std::clamp( def.m_laps, 1, 20 );
    size_t errorsBefore = errors.size();
    walk( def, venue, mirror, warnings, errors );
    if ( errors.size() > errorsBefore ) return false;
    if ( m_raw.size() < 40 )
    {
        errors.push_back( def.m_where + ": track " + def.m_name + " is too short" );
        return false;
    }
    resample();
    if ( reverse )
    {
        // The same start line, driven the other way: the samples run backwards from sample 0.
        std::vector<TrackSample> r( m_s.size() );
        std::vector<char>        c( m_s.size() );
        for ( size_t i = 0; i < m_s.size(); ++i )
        {
            size_t j = ( m_s.size() - i ) % m_s.size();
            r[i] = m_s[j];
            c[i] = m_closing[j];
        }
        m_s.swap( r );
        m_closing.swap( c );
    }
    size_t n = m_s.size();
    for ( size_t i = 0; i < n; ++i )
    {
        TrackSample &t = m_s[i];
        t.m_s = i * m_step;
        t.m_dir = norm( sub( m_s[( i + 1 ) % n].m_pos, m_s[( i + n - 1 ) % n].m_pos ) );
        t.m_right = perp( t.m_dir );
    }
    for ( size_t i = 0; i < n; ++i )
    {
        Vector2 d0 = m_s[( i + n - 1 ) % n].m_dir, d1 = m_s[( i + 1 ) % n].m_dir;
        m_s[i].m_curv = std::asin( clampf( cross( d0, d1 ), -1, 1 ) ) / ( 2 * m_step );
    }
    for ( const TrackSample &t : m_s ) m_reach = std::max( m_reach, t.m_half + t.m_shoulder );
    buildGrid();
    clipWalls();
    racingLine();
    check( warnings );
    place( def, g_data );
    scenes( g_data );
    scatter( g_data );
    litter( g_data );
    loose( g_data );
    // The slalom's gates: a cone on either side, knocked flying when hit.
    for ( const GateSpot &g : m_gates )
        for ( int side = -1; side <= 1; side += 2 )
        {
            LooseSpot l;
            l.m_model = "cone";
            l.m_pos = pointAt( g.m_s, g.m_lat + side * g.m_w * 0.5f );
            l.m_r = 0.3f;
            l.m_h = 0.75f;
            m_loose.push_back( l );
        }
    return true;
}

void Track::walk( const TrackDef &def, const VenueDef *venue, bool mirror, std::vector<std::string> &warnings,
                  std::vector<std::string> &errors )
{
    SurfaceE road = venue ? venue->m_road : SurfaceE::ASPHALT;
    int      wall = styleIndex( m_wallStyles, venue ? venue->m_wall : std::string( "concrete" ) );
    float    half = def.m_width * 0.5f, sh = def.m_shoulder;
    Vector2  pos = def.m_start;
    float    h = def.m_startHeading * Deg;
    m_raw.push_back( { pos, half, sh, (int)road, wall, false } );
    bool points = std::any_of( def.m_pieces.begin(), def.m_pieces.end(),
                               []( const TrackPiece &p ) { return p.m_kind == TrackPiece::POINT; } );
    if ( def.m_pieces.empty() )
    {
        errors.push_back( def.m_where + ": track " + def.m_name + " has no path" );
        return;
    }
    if ( points )
    {
        // Control points: a closed Catmull-Rom curve through the start and the points.
        std::vector<Vector2>    cp = { def.m_start };
        std::vector<const TrackPiece *> pc = { nullptr };
        for ( const TrackPiece &p : def.m_pieces )
            if ( p.m_kind == TrackPiece::POINT )
            {
                cp.push_back( p.m_point );
                pc.push_back( &p );
            }
        size_t k = cp.size();
        for ( size_t i = 0; i < k; ++i )
        {
            Vector2 p0 = cp[( i + k - 1 ) % k], p1 = cp[i], p2 = cp[( i + 1 ) % k], p3 = cp[( i + 2 ) % k];
            float   seglen = len( sub( p2, p1 ) );
            int     steps = std::max( 2, (int)std::ceil( seglen ) );
            const TrackPiece *to = pc[( i + 1 ) % k];
            float half1 = to && to->m_width > 0 ? to->m_width * 0.5f : half;
            float sh1 = to && to->m_shoulder >= 0 ? to->m_shoulder : sh;
            int   surf = to && to->m_surface >= 0 ? to->m_surface : (int)road;
            int   w = to && !to->m_wall.empty() ? styleIndex( m_wallStyles, to->m_wall ) : wall;
            int   wl = to && !to->m_wallL.empty() ? styleIndex( m_wallStyles, to->m_wallL ) : w;
            int   wr = to && !to->m_wallR.empty() ? styleIndex( m_wallStyles, to->m_wallR ) : w;
            for ( int sIdx = 1; sIdx <= steps; ++sIdx )
            {
                if ( i == k - 1 && sIdx == steps ) break;   // back at the start
                float   t = sIdx / (float)steps, t2 = t * t, t3 = t2 * t;
                Vector2 q = { 0.5f * ( 2 * p1.x + ( -p0.x + p2.x ) * t + ( 2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x ) * t2 +
                                       ( -p0.x + 3 * p1.x - 3 * p2.x + p3.x ) * t3 ),
                              0.5f * ( 2 * p1.y + ( -p0.y + p2.y ) * t + ( 2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y ) * t2 +
                                       ( -p0.y + 3 * p1.y - 3 * p2.y + p3.y ) * t3 ) };
                m_raw.push_back( { q, lerpf( half, half1, t ), lerpf( sh, sh1, t ), surf, wl, false, wr } );
            }
            half = half1;
            sh = sh1;
        }
    }
    else
    {
        for ( const TrackPiece &p : def.m_pieces )
        {
            if ( p.m_kind == TrackPiece::POINT ) continue;
            bool  arc = p.m_kind != TrackPiece::STRAIGHT;
            float length = arc ? p.m_r * p.m_deg * Deg : p.m_len;
            int   steps = std::max( 1, (int)std::ceil( length ) );
            float step = length / steps, sign = p.m_kind == TrackPiece::RIGHT ? 1.0f : -1.0f;
            float half1 = p.m_width > 0 ? p.m_width * 0.5f : half;
            float sh1 = p.m_shoulder >= 0 ? p.m_shoulder : sh;
            int   surf = p.m_surface >= 0 ? p.m_surface : (int)road;
            int   w = p.m_wall.empty() ? wall : styleIndex( m_wallStyles, p.m_wall );
            int   wl = p.m_wallL.empty() ? w : styleIndex( m_wallStyles, p.m_wallL );
            int   wr = p.m_wallR.empty() ? w : styleIndex( m_wallStyles, p.m_wallR );
            for ( int k = 1; k <= steps; ++k )
            {
                float t = k / (float)steps;
                if ( arc )
                {
                    float dh = sign * step / p.m_r;
                    pos = add( pos, mul( headingDir( h + dh * 0.5f ), step ) );
                    h += dh;
                }
                else
                    pos = add( pos, mul( headingDir( h ), step ) );
                m_raw.push_back( { pos, lerpf( half, half1, t ), lerpf( sh, sh1, t ), surf, wl, false, wr } );
            }
            half = half1;
            sh = sh1;
        }
        if ( !def.m_closed )
        {
            errors.push_back( def.m_where + ": track " + def.m_name + " has no \"close\"" );
            return;
        }
        // Back to the start along a cubic: leaving the end on its heading, arriving on the start's.
        Vector2 p0 = pos, p1 = def.m_start;
        float   gap = len( sub( p1, p0 ) );
        float   turn = std::fabs( wrapRad( h - def.m_startHeading * Deg ) ) / Deg;
        if ( gap > 0.6f )
        {
            Vector2 t0 = mul( headingDir( h ), gap ), t1 = mul( headingDir( def.m_startHeading * Deg ), gap );
            float   half0 = half, sh0 = sh;
            Vector2 last = p0;
            float   acc = 0;
            const int fine = std::max( 64, (int)( gap * 8 ) );
            for ( int i = 1; i < fine; ++i )
            {
                float   u = i / (float)fine, u2 = u * u, u3 = u2 * u;
                float   h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
                Vector2 q = add( add( mul( p0, h00 ), mul( t0, h10 ) ), add( mul( p1, h01 ), mul( t1, h11 ) ) );
                acc += len( sub( q, last ) );
                last = q;
                if ( acc >= 1.0f )
                {
                    acc = 0;
                    m_raw.push_back( { q, lerpf( half0, def.m_width * 0.5f, u ), lerpf( sh0, def.m_shoulder, u ),
                                       (int)road, wall, true } );
                }
            }
            if ( gap > 160.0f )
            {
                char b[160];
                snprintf( b, sizeof b, ": track %s: the path ends %.0f m from the start (close draws a long curve)",
                          def.m_name.c_str(), gap );
                warnings.push_back( def.m_where + b );
            }
        }
        else
        {
            if ( turn > 8.0f )
            {
                char b[160];
                snprintf( b, sizeof b, ": track %s: the path ends at the start but %.0f degrees off its heading",
                          def.m_name.c_str(), turn );
                warnings.push_back( def.m_where + b );
            }
            if ( m_raw.size() > 1 ) m_raw.pop_back();   // it is the start again
        }
    }
    if ( mirror )
        for ( Raw &r : m_raw ) r.m_pos.x = 2 * def.m_start.x - r.m_pos.x;
}

void Track::resample()
{
    size_t             k = m_raw.size();
    std::vector<float> cum( k + 1, 0.0f );
    for ( size_t i = 0; i < k; ++i ) cum[i + 1] = cum[i] + len( sub( m_raw[( i + 1 ) % k].m_pos, m_raw[i].m_pos ) );
    float total = cum[k];
    int   n = std::max( 8, (int)std::lround( total / Spacing ) );
    m_step = total / n;
    m_length = total;
    m_s.assign( n, {} );
    m_closing.assign( n, 0 );
    size_t j = 0;
    for ( int i = 0; i < n; ++i )
    {
        float s = i * m_step;
        while ( j + 1 < k && cum[j + 1] <= s ) ++j;
        const Raw &a = m_raw[j], &b = m_raw[( j + 1 ) % k];
        float      segl = cum[j + 1] - cum[j];
        float      t = segl > 1e-6f ? ( s - cum[j] ) / segl : 0.0f;
        TrackSample &o = m_s[i];
        o.m_pos = { lerpf( a.m_pos.x, b.m_pos.x, t ), lerpf( a.m_pos.y, b.m_pos.y, t ) };
        o.m_half = lerpf( a.m_half, b.m_half, t );
        o.m_shoulder = lerpf( a.m_shoulder, b.m_shoulder, t );
        const Raw &near = t < 0.5f ? a : b;
        o.m_surface = (SurfaceE)near.m_surface;
        o.m_wall = near.m_wall;
        o.m_wall2 = near.m_wall2 >= 0 ? near.m_wall2 : near.m_wall;
        m_closing[i] = near.m_closing ? 1 : 0;
    }
}

float Track::segDist( int i, Vector2 p, float &t ) const
{
    const TrackSample &a = m_s[i], &b = m_s[( i + 1 ) % m_s.size()];
    t = closestT( a.m_pos, b.m_pos, p );
    Vector2 c = { lerpf( a.m_pos.x, b.m_pos.x, t ), lerpf( a.m_pos.y, b.m_pos.y, t ) };
    return len( sub( p, c ) );
}

void Track::buildGrid()
{
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for ( const TrackSample &t : m_s )
    {
        minX = std::min( minX, t.m_pos.x );
        minY = std::min( minY, t.m_pos.y );
        maxX = std::max( maxX, t.m_pos.x );
        maxY = std::max( maxY, t.m_pos.y );
    }
    float m = m_reach + 4.0f;
    m_bounds = { minX - m_reach, minY - m_reach, maxX - minX + 2 * m_reach, maxY - minY + 2 * m_reach };
    m_origin = { minX - m, minY - m };
    m_gw = (int)std::ceil( ( maxX - minX + 2 * m ) / m_cell ) + 1;
    m_gh = (int)std::ceil( ( maxY - minY + 2 * m ) / m_cell ) + 1;
    m_cells.assign( (size_t)m_gw * m_gh, {} );
    size_t n = m_s.size();
    for ( size_t i = 0; i < n; ++i )
    {
        const TrackSample &a = m_s[i], &b = m_s[( i + 1 ) % n];
        float r = std::max( a.m_half + a.m_shoulder, b.m_half + b.m_shoulder ) + 3.0f;
        int   x0 = (int)( ( std::min( a.m_pos.x, b.m_pos.x ) - r - m_origin.x ) / m_cell );
        int   x1 = (int)( ( std::max( a.m_pos.x, b.m_pos.x ) + r - m_origin.x ) / m_cell );
        int   y0 = (int)( ( std::min( a.m_pos.y, b.m_pos.y ) - r - m_origin.y ) / m_cell );
        int   y1 = (int)( ( std::max( a.m_pos.y, b.m_pos.y ) + r - m_origin.y ) / m_cell );
        for ( int y = std::max( 0, y0 ); y <= std::min( m_gh - 1, y1 ); ++y )
            for ( int x = std::max( 0, x0 ); x <= std::min( m_gw - 1, x1 ); ++x ) m_cells[(size_t)y * m_gw + x].push_back( (int)i );
    }
}

float Track::outside( Vector2 p, Vector2 &push ) const
{
    int   cx = (int)std::floor( ( p.x - m_origin.x ) / m_cell ), cy = (int)std::floor( ( p.y - m_origin.y ) / m_cell );
    float best = 1e9f;
    Vector2 bestC = p;
    if ( cx >= 0 && cy >= 0 && cx < m_gw && cy < m_gh )
    {
        size_t n = m_s.size();
        for ( int i : m_cells[(size_t)cy * m_gw + cx] )
        {
            float t;
            float d = segDist( i, p, t );
            const TrackSample &a = m_s[i], &b = m_s[( i + 1 ) % n];
            float lim = lerpf( a.m_half + a.m_shoulder, b.m_half + b.m_shoulder, t );
            if ( d - lim < best )
            {
                best = d - lim;
                bestC = { lerpf( a.m_pos.x, b.m_pos.x, t ), lerpf( a.m_pos.y, b.m_pos.y, t ) };
            }
        }
    }
    if ( best > 1e8f )
    {
        // Far from everything: back towards the nearest sample.
        float bd = 1e18f;
        for ( const TrackSample &t : m_s )
        {
            float d = dist2( t.m_pos, p );
            if ( d < bd )
            {
                bd = d;
                bestC = t.m_pos;
                best = std::sqrt( d ) - t.m_half - t.m_shoulder;
            }
        }
    }
    push = norm( sub( bestC, p ) );
    return best;
}

TrackPos Track::locate( Vector2 p, int hint ) const
{
    int n = (int)m_s.size();
    int from = 0, to = n - 1;
    if ( hint >= 0 )
    {
        from = hint - 30;
        to = hint + 30;
    }
    float best = 1e18f;
    int   bi = 0;
    float bt = 0;
    for ( int k = from; k <= to; ++k )
    {
        int   i = ( ( k % n ) + n ) % n;
        float t;
        float d = segDist( i, p, t );
        if ( d < best )
        {
            best = d;
            bi = i;
            bt = t;
        }
    }
    const TrackSample &a = m_s[bi], &b = m_s[( bi + 1 ) % n];
    Vector2 c = { lerpf( a.m_pos.x, b.m_pos.x, bt ), lerpf( a.m_pos.y, b.m_pos.y, bt ) };
    TrackPos r;
    r.m_i = bi;
    r.m_s = wrap( a.m_s + bt * m_step );
    r.m_lat = dot( sub( p, c ), a.m_right );
    return r;
}

SurfaceE Track::surfaceAt( Vector2 p, int hint ) const
{
    TrackPos           w = locate( p, hint );
    const TrackSample &t = m_s[w.m_i];
    if ( std::fabs( w.m_lat ) > t.m_half + t.m_shoulder + 0.5f ) return m_venue ? m_venue->m_ground : SurfaceE::GRASS;
    if ( std::fabs( w.m_lat ) > t.m_half ) return m_venue ? m_venue->m_shoulder : SurfaceE::GRAVEL;
    for ( const PatchSpot &pa : m_patches )
    {
        bool in = pa.m_s0 <= pa.m_s1 ? ( w.m_s >= pa.m_s0 && w.m_s <= pa.m_s1 ) : ( w.m_s >= pa.m_s0 || w.m_s <= pa.m_s1 );
        if ( in && w.m_lat >= pa.m_lat0 && w.m_lat <= pa.m_lat1 ) return pa.m_surface;
    }
    return t.m_surface;
}

float Track::wrap( float s ) const
{
    if ( m_length <= 0 ) return 0;
    s = std::fmod( s, m_length );
    return s < 0 ? s + m_length : s;
}

float Track::delta( float a, float b ) const
{
    float d = wrap( b - a );
    return d > m_length * 0.5f ? d - m_length : d;
}

int Track::indexAt( float s ) const
{
    int n = (int)m_s.size();
    return std::clamp( (int)( wrap( s ) / m_step ), 0, n - 1 );
}

const TrackSample &Track::sampleAt( float s ) const { return m_s[indexAt( s )]; }

Vector2 Track::pointAt( float s, float lat ) const
{
    s = wrap( s );
    int                n = (int)m_s.size();
    int                i = std::clamp( (int)( s / m_step ), 0, n - 1 );
    float              t = clampf( ( s - i * m_step ) / m_step, 0, 1 );
    const TrackSample &a = m_s[i], &b = m_s[( i + 1 ) % n];
    Vector2            c = { lerpf( a.m_pos.x, b.m_pos.x, t ), lerpf( a.m_pos.y, b.m_pos.y, t ) };
    Vector2            r = norm( { lerpf( a.m_right.x, b.m_right.x, t ), lerpf( a.m_right.y, b.m_right.y, t ) } );
    return add( c, mul( r, lat ) );
}

void Track::clipWalls()
{
    int n = (int)m_s.size();
    int win = (int)std::ceil( ( 2 * m_reach + 12.0f ) / m_step );
    for ( int i = 0; i < n; ++i )
    {
        TrackSample &t = m_s[i];
        for ( int side = 0; side < 2; ++side )
        {
            float   sg = side == 0 ? -1.0f : 1.0f;
            Vector2 w = add( t.m_pos, mul( t.m_right, sg * ( t.m_half + t.m_shoulder + 0.6f ) ) );
            int     cx = (int)std::floor( ( w.x - m_origin.x ) / m_cell ), cy = (int)std::floor( ( w.y - m_origin.y ) / m_cell );
            if ( cx < 0 || cy < 0 || cx >= m_gw || cy >= m_gh ) continue;
            for ( int j : m_cells[(size_t)cy * m_gw + cx] )
            {
                int d = std::abs( j - i );
                d = std::min( d, n - d );
                if ( d <= win ) continue;
                float tt;
                float dd = segDist( j, w, tt );
                const TrackSample &a = m_s[j], &b = m_s[( j + 1 ) % n];
                if ( dd < lerpf( a.m_half + a.m_shoulder, b.m_half + b.m_shoulder, tt ) + 0.3f )
                {
                    ( side == 0 ? t.m_wallL : t.m_wallR ) = false;
                    break;
                }
            }
        }
    }
}

void Track::racingLine()
{
    int                n = (int)m_s.size();
    std::vector<float> off( n, 0.0f ), lim( n );
    for ( int i = 0; i < n; ++i ) lim[i] = std::max( 0.0f, m_s[i].m_half - 2.3f );
    auto P = [&]( int i ) {
        i = ( ( i % n ) + n ) % n;
        return add( m_s[i].m_pos, mul( m_s[i].m_right, off[i] ) );
    };
    // Pull every point towards the middle of its neighbours (a straighter, rounder line), first with far
    // neighbours (the big shape), then with near ones (smooth).
    for ( int span : { 12, 6, 3 } )
        for ( int it = 0; it < 120; ++it )
            for ( int i = 0; i < n; ++i )
            {
                Vector2 mid = mul( add( P( i - span ), P( i + span ) ), 0.5f );
                float   target = dot( sub( mid, m_s[i].m_pos ), m_s[i].m_right );
                off[i] = clampf( off[i] + ( target - off[i] ) * 0.6f, -lim[i], lim[i] );
            }
    for ( int i = 0; i < n; ++i ) m_s[i].m_line = off[i];
    std::vector<float> k( n );
    for ( int i = 0; i < n; ++i )
    {
        Vector2 a = P( i - 3 ), b = P( i ), c = P( i + 3 );
        float   ab = len( sub( b, a ) ), bc = len( sub( c, b ) ), ca = len( sub( a, c ) );
        float   den = ab * bc * ca;
        k[i] = den > 1e-6f ? std::fabs( 2.0f * cross( sub( b, a ), sub( c, b ) ) / den ) : 0.0f;
    }
    for ( int i = 0; i < n; ++i )
    {
        m_s[i].m_lineCurv = ( k[( i + n - 1 ) % n] + 2 * k[i] + k[( i + 1 ) % n] ) * 0.25f;
        m_s[i].m_dist = len( sub( P( i + 1 ), P( i ) ) );
    }
}

void Track::check( std::vector<std::string> &warnings )
{
    int         n = (int)m_s.size();
    std::string who = m_def ? m_def->m_where + ": track " + m_name : m_name;
    if ( !m_arena && ( m_length < 450.0f || m_length > 2600.0f ) )
    {
        char b[96];
        snprintf( b, sizeof b, ": length %.0f m (450..2600)", m_length );
        warnings.push_back( who + b );
    }
    // Corners tighter than the road is wide fold the inner wall over itself.
    int last = -100;
    for ( int i = 0; i < n; ++i )
    {
        float r = std::fabs( m_s[i].m_curv ) > 1e-5f ? 1.0f / std::fabs( m_s[i].m_curv ) : 1e9f;
        float need = m_closing[i] ? m_s[i].m_half + m_s[i].m_shoulder + 3.0f : m_s[i].m_half + m_s[i].m_shoulder * 0.5f;
        if ( r < need && i - last > 15 )
        {
            char b[200];
            snprintf( b, sizeof b, ": %s corner radius %.1f m is tighter than the road allows (%.1f)%s", at( m_s[i].m_s ).c_str(),
                      r, need, m_closing[i] ? " -- on the closing curve: end the path nearer the start, on its heading" : "" );
            warnings.push_back( who + b );
            m_problems.push_back( m_s[i].m_pos );
            last = i;
        }
    }
    // Parts of the track that come too close without crossing.
    int win = (int)std::ceil( ( 2 * m_reach + 12.0f ) / m_step );
    last = -100;
    std::vector<Vector2> crossings;
    for ( int i = 0; i < n; ++i )
    {
        const TrackSample &a = m_s[i];
        int cx = (int)std::floor( ( a.m_pos.x - m_origin.x ) / m_cell ), cy = (int)std::floor( ( a.m_pos.y - m_origin.y ) / m_cell );
        for ( int dy = -2; dy <= 2; ++dy )
            for ( int dx = -2; dx <= 2; ++dx )
            {
                int x = cx + dx, y = cy + dy;
                if ( x < 0 || y < 0 || x >= m_gw || y >= m_gh ) continue;
                for ( int j : m_cells[(size_t)y * m_gw + x] )
                {
                    int d = std::abs( j - i );
                    d = std::min( d, n - d );
                    if ( d <= win || j < i ) continue;
                    const TrackSample &b = m_s[j];
                    float dist = len( sub( a.m_pos, b.m_pos ) );
                    float need = a.m_half + a.m_shoulder + b.m_half + b.m_shoulder + 6.0f;
                    if ( dist >= need ) continue;
                    float angle = std::acos( clampf( std::fabs( dot( a.m_dir, b.m_dir ) ), 0, 1 ) ) / Deg;
                    if ( angle > 35.0f )
                    {
                        bool known = false;
                        for ( Vector2 c : crossings )
                            if ( len( sub( c, a.m_pos ) ) < need * 2 ) known = true;
                        if ( !known ) crossings.push_back( a.m_pos );
                        continue;
                    }
                    if ( i - last > 15 )
                    {
                        char b2[200];
                        snprintf( b2, sizeof b2, ": %s runs %.1f m from %s (needs %.1f m apart, or cross at > 35 degrees)",
                                  at( a.m_s ).c_str(), dist, at( b.m_s ).c_str(), need );
                        warnings.push_back( who + b2 );
                        m_problems.push_back( a.m_pos );
                        last = i;
                    }
                }
            }
    }
    m_crossings = (int)crossings.size();
    if ( !m_s.empty() && std::fabs( m_s[0].m_half - m_s[n - 1].m_half ) > 0.6f )
    {
        char b[160];
        snprintf( b, sizeof b, ": the road is %.1f m wide at the start but %.1f m at the end of the path (a step at the line)",
                  m_s[0].m_half * 2, m_s[n - 1].m_half * 2 );
        warnings.push_back( who + b );
    }
}

std::vector<std::pair<Vector2, float>> Track::grid( int n ) const
{
    std::vector<std::pair<Vector2, float>> out;
    for ( int k = 0; k < n; ++k )
    {
        float s, lat;
        if ( m_arena )
        {
            s = m_length * k / std::max( 1, n );
            lat = ( k % 2 ? 0.3f : -0.3f ) * m_s[indexAt( s )].m_half;
        }
        else
        {
            int row = k / 2, col = k % 2;
            s = -7.0f - row * 9.0f - col * 4.5f;
            lat = ( col == 0 ? -0.42f : 0.42f ) * m_s[indexAt( s )].m_half;
        }
        const TrackSample &t = m_s[indexAt( s )];
        out.push_back( { pointAt( s, lat ), headingOf( t.m_dir ) } );
    }
    return out;
}

}   // namespace grave
