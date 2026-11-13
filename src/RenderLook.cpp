/**
 * @file RenderLook.cpp
 * @brief The picture's look beyond the plain scene (docs/DESIGN.md "The miniature look, the sky and contact
 *        shadows"): the miniature's tilt-shift blur and the venue's colour grade in the post pass; the airship of
 *        Graves & Sons and the Sensenblatt's news helicopter crossing the track, seen by their shadows and heard (they
 *        fly above the camera); lightning over the storm; and the contact shadows baked at the foot of everything
 *        that stands on the ground.
 * @author Rene Weller
 */
#include "Render.h"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace grave
{

namespace
{

// Render.cpp's particle textures (its TexE): smoke and a spark.
constexpr int TexSmoke = 1, TexSpark = 2;

// ---------------------------------------------------------------------------------------------------- shaders

// The contact shadows, first step: everything that stands on the ground drawn from above. Seen from above the side of
// a wall or a crypt is a line without area, so the vertices near the ground are pushed out along their faces: a skirt
// round the foot of each thing. r: such a foot (what meets the ground and is at least knee-high); g: the tops of tall
// things (buildings, tree crowns) for the wider shade beside and under them. Blended by the largest value.
const char *ContactVs = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 matModel;
uniform mat4 viewProj;
uniform float push;
out float height;
out float foot;
void main()
{
    vec4 w = matModel * vec4( vertexPosition, 1.0 );
    vec3 n = normalize( mat3( matModel ) * vertexNormal );
    float side = length( n.xz );
    float low = side > 0.3 ? 1.0 - smoothstep( 0.05, 0.4, w.y ) : 0.0;
    vec2 out2 = side > 0.3 ? n.xz / side * push * low : vec2( 0.0 );
    height = w.y;
    foot = low;
    gl_Position = viewProj * vec4( w.x + out2.x, w.y, w.z + out2.y, 1.0 );
}
)";

const char *ContactFs = R"(#version 330
in float height;
in float foot;
out vec4 finalColor;
void main()
{
    // r: the skirt of anything at least ankle-high, and the whole top of low things (gravestones, walls, parked cars)
    float skirt = smoothstep( 0.02, 0.2, foot ) * smoothstep( 0.03, 0.35, height );
    float top = smoothstep( 0.15, 0.6, height ) * ( 1.0 - smoothstep( 3.0, 5.0, height ) );
    float g = smoothstep( 2.5, 6.0, height ) * ( 1.0 - smoothstep( 14.0, 24.0, height ) );
    finalColor = vec4( max( skirt, top ), g, 0.0, 1.0 );
}
)";

// The contact shadows, second step (run once across, once down): a bell-shaped blur, narrow for r, wide for g, and
// the second time a gain (a lone skirt is thin: blurred, its edge would be pale).
const char *SpreadFs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 dir;
uniform vec2 sigma;   // texels: r, g
uniform vec2 gain;
out vec4 finalColor;
void main()
{
    ivec2 size = textureSize( texture0, 0 );
    ivec2 p = ivec2( gl_FragCoord.xy );
    int   n = int( ceil( 3.0 * max( sigma.x, sigma.y ) ) );
    vec2  k2 = 1.0 / ( 2.0 * sigma * sigma );
    vec2  sum = vec2( 0.0 ), wsum = vec2( 0.0 );
    for ( int k = -n; k <= n; ++k )
    {
        ivec2 q = clamp( p + ivec2( dir ) * k, ivec2( 0 ), size - 1 );
        vec2  w = exp( -float( k * k ) * k2 );
        sum += texelFetch( texture0, q, 0 ).rg * w;
        wsum += w;
    }
    finalColor = vec4( min( sum / wsum * gain, vec2( 1.0 ) ), 0.0, 1.0 );
}
)";

// The miniature's blur: a picture made smaller with a small tent (as the bloom's).
const char *ShrinkFs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 texel;
out vec4 finalColor;
void main()
{
    vec3 c = texture( texture0, fragTexCoord ).rgb * 4.0;
    c += texture( texture0, fragTexCoord + texel * vec2( -1, -1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( 1, -1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( -1, 1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( 1, 1 ) ).rgb;
    finalColor = vec4( c / 8.0, 1.0 );
}
)";

// ---------------------------------------------------------------------------------------------------- shared

// The flyers, built from model lines like the engine's own models. Only their shadows are drawn: the airship's long
// hull with its fins and the banner it tows, the helicopter's body, tail and turning blades.
const char *AirshipParts[] = {
    "hull len=48 wid=12 hgt=12 shape=cigar color=#9a968a",
    "fin h=6 root=7 tip=3.5 sweep=2.5 thick=0.4 y=4.5 z=19 color=#3a3634",
    "fin h=6 root=7 tip=3.5 sweep=2.5 thick=0.4 y=-4.5 z=19 roll=180 color=#3a3634",
    "fin h=6 root=7 tip=3.5 sweep=2.5 thick=0.4 x=4.5 z=19 roll=90 color=#3a3634",
    "fin h=6 root=7 tip=3.5 sweep=2.5 thick=0.4 x=-4.5 z=19 roll=-90 color=#3a3634",
    "box w=3 h=2.2 d=10 y=-7 z=-3 color=#2a2624",
    "engine r=0.7 len=2.2 x=4 y=-6 z=3 color=#303030",
    "bar from=\"0,-3,24\" to=\"0,-4,33\" r=0.12 color=#202020",
    nullptr };
const char *HeliParts[] = {
    "hull len=8.5 wid=2.6 hgt=2.6 shape=tear color=#c83020",
    "box w=0.5 h=0.6 d=6 y=0.3 z=6.8 color=#c83020",
    "fin h=1.8 root=1.4 tip=0.7 sweep=0.6 thick=0.12 y=0.5 z=9.4 color=#202020",
    "box w=2.4 h=0.1 d=0.5 y=0.6 z=8.6 color=#202020",
    "bar from=\"1.1,-1.6,-2.2\" to=\"1.1,-1.6,2.2\" r=0.08 color=#202020",
    "bar from=\"-1.1,-1.6,-2.2\" to=\"-1.1,-1.6,2.2\" r=0.08 color=#202020",
    "disc r=0.25 h=0.7 y=1.4 color=#202020",
    nullptr };
const char *RotorParts[] = {
    "box w=11 h=0.06 d=0.34 color=#181818",
    "box w=0.34 h=0.06 d=11 color=#181818",
    nullptr };
const char *BannerText = "GRAVES & SONS";
constexpr float BannerCell = 0.55f;   ///< metres per pixel of the banner's letters

struct LookShared
{
    int       m_users = 0;
    Shader    m_contact = {}, m_spread = {}, m_shrink = {};
    Material  m_contactMat = {}, m_plainMat = {};
    ModelMesh m_airship, m_heli, m_rotor;
    Mesh      m_banner = {};
    float     m_bannerLen = 0.0f;
};
LookShared s_look;

ModelMesh buildFlyer( const char *name, const char *const *parts )
{
    ModelDef d;
    d.m_name = name;
    for ( int k = 0; parts[k]; ++k ) d.m_parts.push_back( parseLine( parts[k] ) );
    return Forge::build( d, true );
}

void freeFlyer( ModelMesh &m )
{
    if ( m.m_hasBase ) UnloadMesh( m.m_base );
    if ( m.m_hasHead ) UnloadMesh( m.m_head );
    if ( m.m_hasSpin ) UnloadMesh( m.m_spin );
    m = ModelMesh();
}

/** @brief The banner: its letters as flat quads (one per run of pixels), read from above with the top to the north. */
Mesh buildBanner( float &length )
{
    Image img = ImageTextEx( GetFontDefault(), BannerText, 10, 1, WHITE );
    MeshBuilder mb;
    mb.m_noise = 0;
    Color *px = (Color *)img.data;
    float  w = (float)img.width, h = (float)img.height;
    Vector3 up = { 0, 1, 0 };
    Color   ink = { 30, 30, 30, 0 };
    for ( int y = 0; px && y < img.height; ++y )
        for ( int x = 0; x < img.width; )
        {
            if ( px[y * img.width + x].a < 128 )
            {
                ++x;
                continue;
            }
            int x1 = x;
            while ( x1 < img.width && px[y * img.width + x1].a >= 128 ) ++x1;
            float x0m = ( x - w * 0.5f ) * BannerCell, x1m = ( x1 - w * 0.5f ) * BannerCell;
            float z0m = ( y - h * 0.5f ) * BannerCell, z1m = ( y + 1 - h * 0.5f ) * BannerCell;
            Vector3 a = { x0m, 0, z0m }, b = { x0m, 0, z1m }, c = { x1m, 0, z1m }, d = { x1m, 0, z0m };
            for ( Vector3 p : { a, b, c, a, c, d } ) mb.vert( p, up, ink );
            x = x1;
        }
    length = w * BannerCell;
    UnloadImage( img );
    return mb.count() ? mb.mesh( true ) : Mesh{};
}

/** @return a colour target without depth (8 bits, or 16-bit floats for the picture's blur) */
RenderTexture2D colorTarget( int w, int h, bool hdr )
{
    RenderTexture2D t = {};
    int             fmt = hdr ? PIXELFORMAT_UNCOMPRESSED_R16G16B16A16 : PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    t.id = rlLoadFramebuffer();
    rlEnableFramebuffer( t.id );
    t.texture.id = rlLoadTexture( nullptr, w, h, fmt, 1 );
    t.texture.width = w;
    t.texture.height = h;
    t.texture.format = fmt;
    t.texture.mipmaps = 1;
    rlFramebufferAttach( t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0 );
    if ( !rlFramebufferComplete( t.id ) ) TraceLog( LOG_ERROR, "GFX: look target %dx%d incomplete", w, h );
    rlDisableFramebuffer();
    rlTextureParameters( t.texture.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR );
    rlTextureParameters( t.texture.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR );
    rlTextureParameters( t.texture.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP );
    rlTextureParameters( t.texture.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP );
    return t;
}

void freeTarget( RenderTexture2D &t )
{
    if ( t.id ) rlUnloadFramebuffer( t.id );
    if ( t.texture.id ) rlUnloadTexture( t.texture.id );
    t = {};
}

/** @brief Draws a texture over the whole target through a shader (orientation kept, as Render.cpp's passes). */
void pass( Shader s, Texture2D src, int w, int h )
{
    BeginShaderMode( s );
    DrawTexturePro( src, { 0, 0, (float)src.width, (float)-src.height }, { 0, 0, (float)w, (float)h }, { 0, 0 }, 0,
                    WHITE );
    EndShaderMode();
}

/** @return a tint of a colour that keeps the brightness (its luma brought to 1) */
Vector3 tintOf( Color c )
{
    Vector3 v = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
    float   l = std::max( 0.05f, v.x * 0.3f + v.y * 0.55f + v.z * 0.15f );
    return { v.x / l, v.y / l, v.z / l };
}

/** @return a flash's brightness t seconds after it began: a stroke and two return strokes, each dying fast */
float flashCurve( float t )
{
    auto pulse = []( float t, float t0, float tau, float a ) {
        return t >= t0 ? a * std::exp( -( t - t0 ) / tau ) : 0.0f;
    };
    float f = pulse( t, 0.0f, 0.05f, 1.0f ) + pulse( t, 0.11f, 0.05f, 0.75f ) + pulse( t, 0.27f, 0.09f, 0.5f );
    return std::min( 1.2f, f );
}

}   // namespace

// ---------------------------------------------------------------------------------------------------- setup

void Renderer::lookInit()
{
    if ( s_look.m_users++ == 0 )
    {
        s_look.m_contact = LoadShaderFromMemory( ContactVs, ContactFs );
        s_look.m_contact.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation( s_look.m_contact, "matModel" );
        s_look.m_spread = LoadShaderFromMemory( nullptr, SpreadFs );
        s_look.m_shrink = LoadShaderFromMemory( nullptr, ShrinkFs );
        s_look.m_contactMat = LoadMaterialDefault();
        s_look.m_contactMat.shader = s_look.m_contact;
        s_look.m_plainMat = LoadMaterialDefault();
        s_look.m_airship = buildFlyer( "_airship", AirshipParts );
        s_look.m_heli = buildFlyer( "_heli", HeliParts );
        s_look.m_rotor = buildFlyer( "_heli_rotor", RotorParts );
        s_look.m_banner = buildBanner( s_look.m_bannerLen );
    }
    // The miniature's blur at a half and a quarter of the picture (the Deck's quality: only the half).
    m_blur[0] = colorTarget( std::max( 4, m_w / 2 ), std::max( 4, m_h / 2 ), true );
    if ( m_quality > 0 ) m_blur[1] = colorTarget( std::max( 4, m_w / 4 ), std::max( 4, m_h / 4 ), true );
}

void Renderer::lookShutdown()
{
    freeTarget( m_blur[0] );
    freeTarget( m_blur[1] );
    if ( --s_look.m_users == 0 )
    {
        for ( Shader s : { s_look.m_contact, s_look.m_spread, s_look.m_shrink } ) UnloadShader( s );
        freeFlyer( s_look.m_airship );
        freeFlyer( s_look.m_heli );
        freeFlyer( s_look.m_rotor );
        if ( s_look.m_banner.vertexCount ) UnloadMesh( s_look.m_banner );
        s_look.m_banner = {};
    }
}

void Renderer::lookTrack()
{
    // The venue's grade ("grade" in data/venues.txt; without it the old film's: darks a little cooler, lights warmer).
    m_grade = Grade();
    if ( m_venue )
        for ( const Line &l : m_venue->m_extra )
            if ( l.m_key == "grade" )
            {
                if ( l.has( "shadows" ) ) m_grade.m_lo = tintOf( l.color( "shadows" ) );
                if ( l.has( "highlights" ) ) m_grade.m_hi = tintOf( l.color( "highlights" ) );
                m_grade.m_amount = l.num( "amount", m_grade.m_amount );
                m_grade.m_sat = l.num( "sat", m_grade.m_sat );
                m_grade.m_contrast = l.num( "contrast", m_grade.m_contrast );
                m_grade.m_vignette = l.num( "vignette", m_grade.m_vignette );
            }
    m_tiltCentre = -1.0f;
    m_flyer = Flyer();
    m_flyerTimer = m_rng.range( 14.0f, 30.0f );
    m_bolt = Bolt();
    m_flash = 0.0f;
    m_flashT = -1.0f;
    m_flashTimer = m_rng.range( 4.0f, 9.0f );
    m_thunderIn = -1.0f;
    m_hum = Hum();
}

// ---------------------------------------------------------------------------------------------------- the post pass

void Renderer::lookFrame( const World &w )
{
    if ( m_skyTest >= 0 && m_track && !w.m_cars.empty() )
    {
        if ( m_skyTest == 2 )
        {
            strike( true );
            m_flashT = 0.02f;
            m_flash = m_flashPeak * flashCurve( m_flashT );
        }
        else
            spawnFlyer( w, m_skyTest, m_skyTest == 0 ? 0.5f : 0.47f );
        m_skyTest = -1;
    }
    m_tiltNow = m_miniature && m_track;
    if ( !m_tiltNow ) return;
    // The sharp band follows the player's car (the camera looks ahead of it): the human car nearest to the view.
    Vector2 at = m_view.m_focus;
    float   best = 1e9f;
    for ( const Car &c : w.m_cars )
        if ( c.m_setup.m_human && !c.m_wrecked && dist2( c.m_pos, m_view.m_focus ) < best )
        {
            best = dist2( c.m_pos, m_view.m_focus );
            at = c.m_pos;
        }
    Vector2 p = project( { at.x, 0.6f, at.y } );
    float   v = clampf( 1.0f - p.y / (float)m_h, 0.25f, 0.75f );
    m_tiltCentre = m_tiltCentre < 0 ? v : m_tiltCentre + ( v - m_tiltCentre ) * 0.15f;
}

void Renderer::miniaturePasses( bool tilt )
{
    if ( !tilt || !m_blur[0].id ) return;
    Texture2D src = m_hdr.texture;
    for ( int k = 0; k < 2 && m_blur[k].id; ++k )
    {
        BeginTextureMode( m_blur[k] );
        Vector2 texel = { 1.0f / src.width, 1.0f / src.height };
        SetShaderValue( s_look.m_shrink, GetShaderLocation( s_look.m_shrink, "texel" ), &texel, SHADER_UNIFORM_VEC2 );
        pass( s_look.m_shrink, src, m_blur[k].texture.width, m_blur[k].texture.height );
        EndTextureMode();
        src = m_blur[k].texture;
    }
}

void Renderer::setPostUniforms( Shader &f, bool tilt )
{
    auto set3 = [&]( const char *n, Vector3 v ) {
        SetShaderValue( f, GetShaderLocation( f, n ), &v, SHADER_UNIFORM_VEC3 );
    };
    bool    on = tilt && m_blur[0].id;
    // The Deck's quality: the half-size picture only, a little less blur.
    const RenderTexture2D &far = m_blur[1].id ? m_blur[1] : m_blur[0];
    float   strength = !on ? 0.0f : m_blur[1].id ? 1.0f : 0.8f;
    set3( "tilt", { strength, m_tiltCentre < 0 ? 0.45f : m_tiltCentre, 0.2f } );
    if ( on )
    {
        SetShaderValueTexture( f, GetShaderLocation( f, "blurTex" ), m_blur[0].texture );
        SetShaderValueTexture( f, GetShaderLocation( f, "blurTex2" ), far.texture );
        Vector2 texel = { 1.0f / far.texture.width, 1.0f / far.texture.height };
        SetShaderValue( f, GetShaderLocation( f, "blurTexel" ), &texel, SHADER_UNIFORM_VEC2 );
    }
    // A miniature's photograph is a little more saturated and harder.
    float   sat = m_grade.m_sat * ( on ? 1.06f : 1.0f ), con = m_grade.m_contrast * ( on ? 1.02f : 1.0f );
    Vector4 k = { m_grade.m_amount, sat, con, m_grade.m_vignette };
    set3( "gradeLo", m_grade.m_lo );
    set3( "gradeHi", m_grade.m_hi );
    SetShaderValue( f, GetShaderLocation( f, "gradeK" ), &k, SHADER_UNIFORM_VEC4 );
}

// ---------------------------------------------------------------------------------------------------- the sky

void Renderer::skyTest( int kind )
{
    // Served by the next picture (lookFrame), where the view is known: the flyer half way, the flash at its height.
    m_skyTest = kind;
}

void Renderer::strike( bool inView )
{
    m_flashTimer = m_rng.range( 6.0f, 16.0f );
    m_flashPeak = m_rng.range( 0.75f, 1.1f );
    m_flashT = 0.0f;
    if ( !inView )
    {
        m_thunderNear = false;
        m_thunderIn = m_rng.range( 1.2f, 4.0f );
        return;
    }
    // A strike in view, beside the road: a bright channel down from the clouds, a scorch, sparks.
    Vector2 focus = m_view.m_focus;
    float   a = m_rng.range( 0.0f, 2.0f * Pi ), d = m_rng.range( 16.0f, 40.0f );
    Vector2 at = { focus.x + std::cos( a ) * d, focus.y + std::sin( a ) * d * 0.6f };
    m_bolt = Bolt();
    m_bolt.m_t = 0.0f;
    m_bolt.m_at = at;
    float                top = std::min( 34.0f, m_view.m_height * 0.55f );
    std::vector<Vector3> chain = { { at.x, 0.0f, at.y } };
    int                  n = 14;
    Vector2              drift = { m_rng.range( -0.6f, 0.6f ), m_rng.range( -0.6f, 0.6f ) };
    for ( int k = 1; k <= n; ++k )
        chain.push_back( { chain.back().x + drift.x + m_rng.range( -1.4f, 1.4f ), top * k / n,
                           chain.back().z + drift.y + m_rng.range( -1.4f, 1.4f ) } );
    for ( int k = 0; k < n; ++k )
    {
        m_bolt.m_pts.push_back( chain[k] );
        m_bolt.m_pts.push_back( chain[k + 1] );
        m_bolt.m_width.push_back( 0.45f );
    }
    for ( int b = 0; b < 3; ++b )   // branches from the upper part, reaching out and down
    {
        Vector3 q = chain[n / 3 + m_rng.below( n - n / 3 )];
        float   ang = m_rng.range( 0.0f, 2.0f * Pi );
        int     steps = 4 + m_rng.below( 4 );
        for ( int k = 0; k < steps; ++k )
        {
            Vector3 r = { q.x + std::cos( ang ) * m_rng.range( 0.8f, 2.2f ), q.y - m_rng.range( 0.6f, 2.0f ),
                          q.z + std::sin( ang ) * m_rng.range( 0.8f, 2.2f ) };
            if ( r.y < 1.0f ) break;
            m_bolt.m_pts.push_back( q );
            m_bolt.m_pts.push_back( r );
            m_bolt.m_width.push_back( 0.2f );
            q = r;
            ang += m_rng.range( -0.6f, 0.6f );
        }
    }
    m_glows.push_back( { { at.x, 3.0f, at.y }, { 190, 210, 255, 255 }, 34.0f, 7.0f, 0, 0.45f } );
    if ( !m_marks.empty() )
    {
        m_marks[m_markNext] = Mark{ at, at, 1.5f, 0.6f, { 14, 12, 12, 255 } };
        m_markNext = ( m_markNext + 1 ) % m_marks.size();
    }
    puff( { at.x, 0.4f, at.y }, 16, 9.0f, 0.5f, { 220, 230, 255, 255 }, { 120, 160, 255, 0 }, 0.5f, TexSpark, true );
    puff( { at.x, 0.6f, at.y }, 4, 1.2f, 1.6f, { 60, 58, 60, 150 }, { 80, 78, 80, 0 }, 2.2f, TexSmoke, false );
    m_thunderNear = true;
    m_thunderIn = std::sqrt( d * d + 60.0f * 60.0f ) / 343.0f;   // the sound a moment later
}

namespace
{

/** @return where the helicopter's shadow wants to be at u (0..1) of its pass: from one side of the car to the other,
 *          slowest over it, a little ahead (it lags behind) */
Vector2 heliGoal( const Car &c, float u, float side, float t )
{
    float   x = 2.0f * u - 1.0f, across = 48.0f * ( 0.6f * x * x * x + 0.4f * x );
    Vector2 goal = add( add( c.m_pos, mul( c.right(), across * side ) ), mul( c.fwd(), 6.0f * std::sin( t * 0.8f ) ) );
    return add( goal, mul( c.m_vel, 0.4f ) );
}

}   // namespace

void Renderer::spawnFlyer( const World &w, int kind, float at )
{
    Flyer &f = m_flyer;
    f = Flyer();
    f.m_kind = kind;
    // The car it is about: the one the camera follows (nearest to the view).
    Vector2 focus = m_view.m_focus;
    int     car = 0;
    for ( size_t i = 0; i < w.m_cars.size(); ++i )
        if ( dist2( w.m_cars[i].m_pos, focus ) < dist2( w.m_cars[car].m_pos, focus ) ) car = (int)i;
    f.m_car = car;
    f.m_side = m_rng.chance( 0.5f ) ? 1.0f : -1.0f;
    if ( kind == 0 )
    {
        // The airship drifts east or west across the track a little ahead of the field, so the cars drive through its
        // shadow; it takes the best part of a minute. (Asked for half way: across the view.)
        Vector2 mid = focus;
        if ( at <= 0 )
        {
            TrackPos tp = m_track->locate( w.m_cars[car].m_pos, -1 );
            mid = m_track->pointAt( m_track->wrap( tp.m_s + 160.0f ), 0.0f );
        }
        f.m_from = { mid.x - 190.0f * f.m_side, mid.y + m_rng.range( -20.0f, 20.0f ) };
        f.m_to = { mid.x + 190.0f * f.m_side, mid.y + m_rng.range( -20.0f, 20.0f ) };
        f.m_life = 380.0f / 7.5f;
        f.m_t = at * f.m_life;
        f.m_ground = { lerpf( f.m_from.x, f.m_to.x, at ), lerpf( f.m_from.y, f.m_to.y, at ) };
        f.m_head = headingOf( sub( f.m_to, f.m_from ) );
    }
    else
    {
        // The helicopter films the car: its shadow comes in from one side, crosses over the car, leaves.
        f.m_life = 11.0f;
        f.m_t = at * f.m_life;
        f.m_ground = heliGoal( w.m_cars[car], at, f.m_side, f.m_t );
        f.m_head = w.m_cars[car].m_head;
    }
}

void Renderer::sky( const World &w, float dt )
{
    const VenueDef *v = m_venue;
    Vector2         focus = m_view.m_focus;
    Vector3         toSun = Vector3Negate( m_sunDir );
    // ---- lightning over the storm: a stroke and its return strokes, now and then one in view, thunder after
    if ( v && v->m_weather == "storm" && ( m_flashTimer -= dt ) <= 0 ) strike( m_rng.chance( 0.4f ) );
    if ( m_flashT >= 0 )
    {
        m_flashT += dt;
        m_flash = m_flashPeak * flashCurve( m_flashT );
        if ( m_flashT > 1.2f )
        {
            m_flashT = -1.0f;
            m_flash = 0.0f;
        }
    }
    else
        m_flash = std::max( 0.0f, m_flash - dt * 3.0f );
    if ( m_bolt.m_t >= 0 && ( m_bolt.m_t += dt ) > 0.45f ) m_bolt.m_t = -1.0f;
    if ( m_thunderIn >= 0 && ( m_thunderIn -= dt ) < 0 )
    {
        m_cues.push_back( { m_thunderNear ? 2 : 3, m_thunderNear ? m_bolt.m_at : focus } );
        m_thunderIn = -1.0f;
    }

    // ---- what flies over the track: now and then the airship (in daylight) or the news helicopter
    Flyer &f = m_flyer;
    if ( f.m_kind < 0 && m_track && !w.m_cars.empty() && w.m_time > 10.0f && ( m_flyerTimer -= dt ) <= 0 )
        spawnFlyer( w, !m_dark && toSun.y > 0.35f && m_rng.chance( 0.5f ) ? 0 : 1, 0.0f );
    m_hum = Hum();
    if ( f.m_kind < 0 ) return;
    f.m_t += dt;
    float   u = std::min( 1.0f, f.m_t / f.m_life );
    float   fade = clampf( std::min( f.m_t / 2.0f, ( f.m_life - f.m_t ) / 2.0f ), 0.0f, 1.0f );
    Vector2 was = f.m_ground;
    if ( f.m_kind == 0 )
        f.m_ground = { lerpf( f.m_from.x, f.m_to.x, u ), lerpf( f.m_from.y, f.m_to.y, u ) };
    else
    {
        const Car &c = w.m_cars[std::clamp( f.m_car, 0, (int)w.m_cars.size() - 1 )];
        Vector2    goal = heliGoal( c, u, f.m_side, f.m_t );
        f.m_ground = add( f.m_ground, mul( sub( goal, f.m_ground ), std::min( 1.0f, dt * 2.5f ) ) );
        f.m_rotor += dt * 2.0f * Pi * 4.3f;
    }
    f.m_vel = dt > 0 ? mul( sub( f.m_ground, was ), 1.0f / dt ) : Vector2{ 0, 0 };
    if ( len( f.m_vel ) > 2.0f ) f.m_head = headingOf( f.m_vel );
    // Its sound: louder as its shadow comes nearer the view, higher while it comes closer (Doppler).
    float d = len( sub( f.m_ground, focus ) ), closing = -dot( f.m_vel, norm( sub( f.m_ground, focus ) ) );
    float near = fade / ( 1.0f + d / ( f.m_kind == 0 ? 90.0f : 60.0f ) );
    ( f.m_kind == 0 ? m_hum.m_airship : m_hum.m_rotor ) = near;
    m_hum.m_pitch = clampf( 343.0f / ( 343.0f - clampf( closing, -60.0f, 60.0f ) ), 0.85f, 1.18f );
    if ( f.m_t >= f.m_life )
    {
        f = Flyer();
        m_flyerTimer = m_rng.range( 35.0f, 75.0f );
    }
}

void Renderer::skyLights( const World &w )
{
    // At night the news helicopter shines a searchlight on the car it films (from the moon's side, where it hangs).
    const Flyer &f = m_flyer;
    if ( !m_dark || f.m_kind != 1 || f.m_car < 0 || f.m_car >= (int)w.m_cars.size() || (int)m_spotPos.size() >= 8 )
        return;
    float   fade = clampf( std::min( f.m_t / 1.5f, ( f.m_life - f.m_t ) / 1.5f ), 0.0f, 1.0f );
    Vector2 c = w.m_cars[f.m_car].m_pos;
    Vector2 wob = { 3.0f * std::sin( m_time * 0.9f ), 3.0f * std::cos( m_time * 0.7f ) };
    Vector3 at = { c.x + wob.x, 0.0f, c.y + wob.y };
    Vector3 toSun = Vector3Negate( m_sunDir );
    Vector2 hz = norm( { toSun.x, toSun.z } );
    float   el = std::max( std::asin( clampf( toSun.y, 0.0f, 1.0f ) ), 55.0f * Deg );
    Vector3 back = { hz.x * std::cos( el ), std::sin( el ), hz.y * std::cos( el ) };
    Vector3 from = Vector3Add( at, Vector3Scale( back, 26.0f ) );
    Vector3 dir = Vector3Negate( back );
    m_spotPos.push_back( { from.x, from.y, from.z, 60.0f } );
    m_spotDir.push_back( { dir.x, dir.y, dir.z, std::cos( 22.0f * Deg ) } );
    m_spotCol.push_back( { 0.92f, 0.96f, 1.0f, 3.5f * fade } );   // brighter and the wet streets turn white
}

void Renderer::drawFlyer( bool shadow )
{
    // Only the shadow: the flyers are far above the camera. A directional light's shadow is the same wherever along
    // the sun's ray the thing hangs, so it is drawn low on that ray (inside the light's depth range), above the roofs.
    const Flyer &f = m_flyer;
    if ( !shadow || f.m_kind < 0 ) return;
    Vector3 toSun = Vector3Negate( m_sunDir );
    if ( toSun.y < 0.08f ) return;
    auto onRay = [&]( Vector2 g, float height ) {
        float s = std::min( height / toSun.y, 80.0f );
        return Vector3Add( { g.x, 0.0f, g.y }, Vector3Scale( toSun, s ) );
    };
    auto put = [&]( const Mesh &m, Vector3 at, float head, float roll ) {
        Matrix xf = MatrixMultiply( MatrixMultiply( MatrixRotateZ( roll ), MatrixRotateY( -head ) ),
                                    MatrixTranslate( at.x, at.y, at.z ) );
        DrawMesh( m, s_look.m_plainMat, xf );
    };
    if ( f.m_kind == 0 )
    {
        Vector3 at = onRay( f.m_ground, 34.0f );
        if ( s_look.m_airship.m_hasBase ) put( s_look.m_airship.m_base, at, f.m_head, 0.0f );
        // The banner trails behind on its line; its letters lie east to west whichever way it flies, so they read.
        if ( s_look.m_banner.vertexCount )
        {
            Vector2 back = mul( headingDir( f.m_head ), -( 33.0f + s_look.m_bannerLen * 0.5f ) );
            Vector3 b = onRay( add( f.m_ground, back ), 30.0f );
            DrawMesh( s_look.m_banner, s_look.m_plainMat, MatrixTranslate( b.x, b.y, b.z ) );
        }
    }
    else
    {
        Vector3 at = onRay( f.m_ground, 30.0f );
        if ( s_look.m_heli.m_hasBase ) put( s_look.m_heli.m_base, at, f.m_head, 0.0f );
        if ( s_look.m_rotor.m_hasBase ) put( s_look.m_rotor.m_base, Vector3Add( at, { 0, 1.8f, 0 } ), f.m_rotor, 0.0f );
    }
}

void Renderer::drawBolt()
{
    if ( m_bolt.m_t < 0 || m_bolt.m_pts.empty() ) return;
    float k = clampf( m_flash * 1.2f, 0.0f, 1.0f );
    if ( k < 0.05f ) return;
    // Seen from above the channel runs from the strike towards the middle of the picture. Three layers added up: a
    // wide glow, a brighter one, the white core (above 1, so the bloom takes it).
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode( BLEND_ADDITIVE );
    rlSetTexture( rlGetTextureIdDefault() );
    rlBegin( RL_QUADS );
    const float layers[][4] = { { 3.2f, 0.22f, 0.3f, 0.7f }, { 1.8f, 0.5f, 0.6f, 1.0f }, { 0.8f, 1.0f, 1.0f, 1.0f } };
    for ( const auto &ly : layers )
        for ( size_t s = 0; s + 1 < m_bolt.m_pts.size(); s += 2 )
        {
            Vector3 a = m_bolt.m_pts[s], b = m_bolt.m_pts[s + 1];
            Vector3 mid = Vector3Scale( Vector3Add( a, b ), 0.5f );
            Vector3 side = Vector3CrossProduct( Vector3Subtract( b, a ), Vector3Subtract( m_cam.position, mid ) );
            side = Vector3Scale( Vector3Normalize( side ), m_bolt.m_width[s / 2] * ly[0] * 0.5f );
            rlColor4f( ly[1] * k, ly[2] * k, ly[3] * k, 1.0f );
            rlVertex3f( a.x - side.x, a.y - side.y, a.z - side.z );
            rlVertex3f( a.x + side.x, a.y + side.y, a.z + side.z );
            rlVertex3f( b.x + side.x, b.y + side.y, b.z + side.z );
            rlVertex3f( b.x - side.x, b.y - side.y, b.z - side.z );
        }
    rlEnd();
    rlSetTexture( 0 );
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
}

// ---------------------------------------------------------------------------------------------------- contact shadows

void Renderer::bakeContact()
{
    const Track &t = *m_track;
    // The box: the track and the props around it (far things beyond it go without).
    float x0 = t.m_bounds.x, z0 = t.m_bounds.y;
    float x1 = t.m_bounds.x + t.m_bounds.width, z1 = t.m_bounds.y + t.m_bounds.height;
    for ( const PropSpot &p : t.m_props )
    {
        Vector2 q = { clampf( p.m_pos.x, t.m_bounds.x - 160.0f, t.m_bounds.x + t.m_bounds.width + 160.0f ),
                      clampf( p.m_pos.y, t.m_bounds.y - 160.0f, t.m_bounds.y + t.m_bounds.height + 160.0f ) };
        x0 = std::min( x0, q.x - 14.0f );
        z0 = std::min( z0, q.y - 14.0f );
        x1 = std::max( x1, q.x + 14.0f );
        z1 = std::max( z1, q.y + 14.0f );
    }
    float size = std::max( x1 - x0, z1 - z0 ) + 4.0f;
    x0 = ( x0 + x1 ) * 0.5f - size * 0.5f;
    z0 = ( z0 + z1 ) * 0.5f - size * 0.5f;
    // About a fifth of a metre a texel (a third on the Deck), at most 2048 / 3072 / 4096 texels across.
    int   cap = m_quality == 0 ? 2048 : m_quality == 1 ? 3072 : 4096;
    int   res = std::clamp( (int)std::ceil( size / ( m_quality == 0 ? 0.3f : 0.22f ) ), 256, cap );
    float texel = size / res;
    RenderTexture2D feet = colorTarget( res, res, false ), spread = colorTarget( res, res, false );
    // 1. the feet and the tall tops, seen from straight above
    Matrix vp = {};
    vp.m0 = 2.0f / size;
    vp.m12 = -2.0f * x0 / size - 1.0f;
    vp.m9 = 2.0f / size;
    vp.m13 = -2.0f * z0 / size - 1.0f;
    vp.m15 = 1.0f;
    Shader &cs = s_look.m_contact;
    SetShaderValueMatrix( cs, GetShaderLocation( cs, "viewProj" ), vp );
    float push = std::max( 0.3f, texel * 2.0f );   // at least two texels: a thinner skirt would break into dots
    SetShaderValue( cs, GetShaderLocation( cs, "push" ), &push, SHADER_UNIFORM_FLOAT );
    BeginTextureMode( feet );
    ClearBackground( BLANK );
    rlDisableBackfaceCulling();
    rlDisableDepthTest();
    rlSetBlendFactors( RL_ONE, RL_ONE, RL_MAX );
    BeginBlendMode( BLEND_CUSTOM );
    Material &mat = s_look.m_contactMat;
    if ( m_walls.vertexCount ) DrawMesh( m_walls, mat, MatrixIdentity() );
    for ( const Chunk &c : m_chunks ) DrawMesh( c.m_mesh, mat, MatrixIdentity() );
    auto model = [&]( const std::string &name, Matrix xf ) {
        if ( !g_data.model( name ) ) return;
        const ModelMesh &m = forge().get( g_data, name );
        if ( m.m_hasBase ) DrawMesh( m.m_base, mat, xf );
    };
    for ( const ObstacleSpot &o : t.m_obstacles )
        if ( !o.m_model.empty() )
            model( o.m_model,
                   MatrixMultiply( MatrixRotateY( -o.m_rot * Deg ), MatrixTranslate( o.m_pos.x, 0, o.m_pos.y ) ) );
    for ( const Person &pe : m_people )   // where they stand when calm
    {
        Matrix turn = MatrixMultiply( MatrixScale( pe.m_scale, pe.m_scale, pe.m_scale ), MatrixRotateY( -pe.m_rot ) );
        model( pe.m_model, MatrixMultiply( turn, MatrixTranslate( pe.m_pos.x, 0, pe.m_pos.y ) ) );
    }
    EndBlendMode();
    rlEnableBackfaceCulling();
    EndTextureMode();
    // 2. spread: across, then down. Beside things a narrow bell (about half a metre); beside tall ones a wide one.
    Shader &sp = s_look.m_spread;
    Vector2 sigma = { std::max( 1.5f, 0.45f / texel ), std::max( 2.5f, 1.3f / texel ) };
    SetShaderValue( sp, GetShaderLocation( sp, "sigma" ), &sigma, SHADER_UNIFORM_VEC2 );
    for ( int k = 0; k < 2; ++k )
    {
        Vector2          dir = k == 0 ? Vector2{ 1, 0 } : Vector2{ 0, 1 };
        Vector2          gain = k == 0 ? Vector2{ 1, 1 } : Vector2{ 2.0f, 1.4f };
        RenderTexture2D &from = k == 0 ? feet : spread, &to = k == 0 ? spread : feet;
        SetShaderValue( sp, GetShaderLocation( sp, "dir" ), &dir, SHADER_UNIFORM_VEC2 );
        SetShaderValue( sp, GetShaderLocation( sp, "gain" ), &gain, SHADER_UNIFORM_VEC2 );
        BeginTextureMode( to );
        ClearBackground( BLANK );
        pass( sp, from.texture, res, res );
        EndTextureMode();
    }
    freeTarget( spread );
    m_contact = feet;
    m_contactRect = { x0, z0, 1.0f / size, 1.0f };
}

void Renderer::setContactUniforms( Shader &s, float up )
{
    bool    on = m_venue && m_contact.id;
    Vector4 rect = on ? m_contactRect : Vector4{ 0, 0, 0, 0 };
    // Darker at night: the sky's light is most of what there is.
    Vector3 k = { m_dark ? 0.62f : 0.55f, m_dark ? 0.34f : 0.3f, up };
    SetShaderValue( s, GetShaderLocation( s, "contactRect" ), &rect, SHADER_UNIFORM_VEC4 );
    SetShaderValue( s, GetShaderLocation( s, "contactK" ), &k, SHADER_UNIFORM_VEC3 );
    int slot = 11;
    SetShaderValue( s, GetShaderLocation( s, "contactMap" ), &slot, SHADER_UNIFORM_INT );
    if ( on )
    {
        rlActiveTextureSlot( 11 );
        rlEnableTexture( m_contact.texture.id );
        rlActiveTextureSlot( 0 );
    }
}

}   // namespace grave
