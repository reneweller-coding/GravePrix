/**
 * @file Render.cpp
 * @brief The renderer: shaders, the static scene, cars, effects, lights, shadows and the picture's last steps.
 * @author Rene Weller
 */
#include "Render.h"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace grave
{

namespace
{

constexpr int MaxLights = 24;
constexpr int MaxSpots = 8;

// ---------------------------------------------------------------------------------------------------- shaders

const char *LitVs = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
out vec2 fragUV;
out vec2 fragUV2;
void main()
{
    fragPos = ( matModel * vec4( vertexPosition, 1.0 ) ).xyz;
    fragNormal = normalize( ( matNormal * vec4( vertexNormal, 0.0 ) ).xyz );
    fragColor = vertexColor;
    fragUV = vertexTexCoord;
    fragUV2 = vertexTexCoord2;
    gl_Position = mvp * vec4( vertexPosition, 1.0 );
}
)";

// The lighting both the models and the ground use: the sun with its shadow, the sky, the lamps and fires, the
// headlights, and the fog that thickens away from where the camera looks.
const char *LightGlsl = R"(
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 ambient;
uniform vec3 camPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform vec2 focus;
uniform int lightCount;
uniform vec4 lightPos[24];
uniform vec4 lightCol[24];
uniform int spotCount;
uniform vec4 spotPos[8];
uniform vec4 spotDir[8];
uniform vec4 spotCol[8];
uniform sampler2D shadowMap;
uniform mat4 lightVP;
uniform float shadowOn;
uniform float flash;
float shadowAt( vec3 p, vec3 n )
{
    if ( shadowOn < 0.5 ) return 1.0;
    vec4 q = lightVP * vec4( p + n * 0.06, 1.0 );
    vec3 s = q.xyz / q.w * 0.5 + 0.5;
    if ( s.x <= 0.0 || s.x >= 1.0 || s.y <= 0.0 || s.y >= 1.0 || s.z >= 1.0 ) return 1.0;
    vec2 texel = 1.0 / vec2( textureSize( shadowMap, 0 ) );
    float sum = 0.0;
    for ( int x = -1; x <= 1; ++x )
        for ( int y = -1; y <= 1; ++y )
            sum += ( s.z - 0.0012 > texture( shadowMap, s.xy + vec2( x, y ) * texel ).r ) ? 0.0 : 1.0;
    return sum / 9.0;
}
vec3 lighting( vec3 p, vec3 n, vec3 albedo, float spec, float rough )
{
    vec3 v = normalize( camPos - p );
    vec3 l = -sunDir;
    float ndl = max( dot( n, l ), 0.0 );
    float sh = shadowAt( p, n );
    vec3 col = albedo * ( ambient * ( 0.6 + 0.4 * n.y ) + sunColor * ndl * sh );
    vec3 h = normalize( l + v );
    col += sunColor * pow( max( dot( n, h ), 0.0 ), rough ) * spec * sh;
    for ( int i = 0; i < lightCount; ++i )
    {
        vec3 L = lightPos[i].xyz - p;
        float d = length( L );
        float att = clamp( 1.0 - d / lightPos[i].w, 0.0, 1.0 );
        att *= att;
        vec3 Ld = L / max( d, 0.001 );
        float nl = max( dot( n, Ld ), 0.0 ) * 0.8 + 0.2;
        vec3 lc = lightCol[i].rgb * lightCol[i].a * att;
        col += albedo * lc * nl + lc * pow( max( dot( n, normalize( Ld + v ) ), 0.0 ), rough ) * spec;
    }
    for ( int i = 0; i < spotCount; ++i )
    {
        vec3 L = spotPos[i].xyz - p;
        float d = length( L );
        vec3 Ld = L / max( d, 0.001 );
        float cone = dot( -Ld, spotDir[i].xyz );
        float edge = smoothstep( spotDir[i].w, spotDir[i].w + 0.1, cone );
        float att = clamp( 1.0 - d / spotPos[i].w, 0.0, 1.0 );
        vec3 lc = spotCol[i].rgb * spotCol[i].a * edge * att;
        col += albedo * lc * max( dot( n, Ld ), 0.0 ) + lc * pow( max( dot( n, normalize( Ld + v ) ), 0.0 ), rough ) * spec;
    }
    col += albedo * flash * vec3( 0.7, 0.75, 1.0 );
    return col;
}
vec3 fogged( vec3 col, vec3 p )
{
    float d = length( p.xz - focus );
    float f = 1.0 - exp( -d * fogDensity );
    return mix( col, fogColor, clamp( f, 0.0, 0.97 ) );
}
)";

const char *LitFsBody = R"(
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
uniform vec4 tint;
uniform float emitBoost;
uniform float specular;
out vec4 finalColor;
void main()
{
    vec3 n = normalize( fragNormal );
    vec3 albedo = fragColor.rgb * tint.rgb;
    float em = fragColor.a;
    vec3 col = lighting( fragPos, n, albedo, specular, 40.0 );
    col += fragColor.rgb * em * emitBoost;
    col = mix( col, vec3( 2.2, 2.1, 2.0 ), tint.a );
    finalColor = vec4( fogged( col, fragPos ), 1.0 );
}
)";

// The ground: one shader for the road, the shoulders, the patches and the land around, its look drawn from the
// surface kind (in the colour's alpha), the distance along the track and the side position (texture coordinates).
const char *SurfaceFsBody = R"(
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
in vec2 fragUV;
in vec2 fragUV2;
uniform vec3 lineColor;
uniform vec3 groundA;
uniform vec3 groundB;
uniform float wet;
uniform float time;
out vec4 finalColor;
// A hash without sin(): stays smooth far from the origin (the sin hash turns blocky at large world coordinates).
float hash( vec2 p )
{
    vec3 p3 = fract( vec3( p.xyx ) * 0.1031 );
    p3 += dot( p3, p3.yzx + 33.33 );
    return fract( ( p3.x + p3.y ) * p3.z );
}
float noise( vec2 p )
{
    vec2 i = floor( p ), f = fract( p );
    f = f * f * ( 3.0 - 2.0 * f );
    return mix( mix( hash( i ), hash( i + vec2( 1, 0 ) ), f.x ), mix( hash( i + vec2( 0, 1 ) ), hash( i + vec2( 1, 1 ) ), f.x ), f.y );
}
float fbm( vec2 p )
{
    float a = 0.5, s = 0.0;
    for ( int i = 0; i < 4; ++i ) { s += a * noise( p ); p = p * 2.03 + 1.7; a *= 0.5; }
    return s;
}
vec2 cells( vec2 p )
{
    vec2 i = floor( p ), f = fract( p );
    float d1 = 8.0, d2 = 8.0;
    for ( int y = -1; y <= 1; ++y )
        for ( int x = -1; x <= 1; ++x )
        {
            vec2 g = vec2( x, y );
            vec2 o = vec2( hash( i + g ), hash( i + g + 17.3 ) ) * 0.8 + 0.1;
            float d = length( g + o - f );
            if ( d < d1 ) { d2 = d1; d1 = d; } else if ( d < d2 ) d2 = d;
        }
    return vec2( d1, d2 );
}
void main()
{
    int kind = int( fragColor.a * 255.0 + 0.5 );
    int role = int( fragUV2.y + 0.5 );   // 0 road, 1 shoulder, 2 land, 3 patch
    vec2 w = fragPos.xz;
    float s = fragUV.x, lat = fragUV.y, halfW = fragUV2.x;
    vec3 base = fragColor.rgb;
    float spec = 0.04, rough = 24.0, hard = 1.0;
    vec3 n = vec3( 0.0, 1.0, 0.0 );
    float big = fbm( w * 0.045 );
    float mid = fbm( w * 0.4 );
    float fine = noise( w * 6.0 );
    vec3 col = base;
    if ( role == 2 ) base = mix( groundA, groundB, smoothstep( 0.35, 0.7, big ) );
    if ( kind == 0 )        // asphalt
    {
        col = base * ( 0.78 + 0.32 * mid ) * ( 0.9 + 0.2 * hash( floor( w * 22.0 ) ) );
        float crack = smoothstep( 0.012, 0.0, abs( noise( w * 0.9 ) - 0.5 ) ) * smoothstep( 0.72, 0.8, noise( w * 0.07 + 4.0 ) );
        col *= 1.0 - 0.28 * crack;
        col *= 0.9 + 0.2 * smoothstep( 0.55, 0.75, fbm( w * 0.09 + 9.0 ) );   // patched repairs
        spec = 0.1;
    }
    else if ( kind == 1 )   // concrete: slabs
    {
        col = base * ( 0.86 + 0.2 * mid );
        float jx = smoothstep( 0.06, 0.0, abs( fract( s / 5.0 ) - 0.5 ) - 0.47 );
        float jy = role == 0 ? smoothstep( 0.06, 0.0, abs( lat ) - 0.0 ) * 0.0 : 0.0;
        vec2 slab = fract( w / 5.0 );
        float grid = max( smoothstep( 0.97, 1.0, slab.x ), smoothstep( 0.97, 1.0, slab.y ) );
        col *= 1.0 - 0.3 * max( max( jx, jy ), role == 0 ? 0.0 : grid );
        col *= 0.92 + 0.12 * hash( floor( w / 5.0 ) );
        spec = 0.06;
    }
    else if ( kind == 2 )   // cobble
    {
        vec2 c = cells( w * 2.2 );
        float gap = smoothstep( 0.0, 0.12, c.y - c.x );
        col = base * ( 0.7 + 0.45 * hash( floor( w * 2.2 + c.x ) ) ) * mix( 0.35, 1.0, gap );
        spec = 0.12 * gap;
        n = normalize( vec3( ( noise( w * 4.4 ) - 0.5 ) * 0.4 * gap, 1.0, ( noise( w * 4.4 + 3.0 ) - 0.5 ) * 0.4 * gap ) );
    }
    else if ( kind == 3 )   // dirt: ruts along the way
    {
        col = base * ( 0.72 + 0.4 * mid ) * ( 0.9 + 0.2 * fine );
        if ( role == 0 ) col *= 1.0 - 0.18 * smoothstep( 1.4, 0.2, abs( abs( lat ) - halfW * 0.45 ) );
        col *= 0.85 + 0.3 * step( 0.93, hash( floor( w * 9.0 ) ) );   // pebbles
        hard = 0.4;
    }
    else if ( kind == 4 )   // gravel
    {
        float st = hash( floor( w * 14.0 ) );
        col = base * ( 0.65 + 0.55 * st ) * ( 0.85 + 0.25 * mid );
        hard = 0.3;
    }
    else if ( kind == 5 )   // sand: ripples
    {
        float rip = sin( ( w.x * 0.8 + w.y * 0.5 ) * 3.0 + mid * 6.0 );
        col = base * ( 0.88 + 0.12 * rip ) * ( 0.9 + 0.15 * mid );
        if ( role == 0 ) col *= 1.0 - 0.12 * smoothstep( 1.4, 0.2, abs( abs( lat ) - halfW * 0.45 ) );
        n = normalize( vec3( cos( ( w.x * 0.8 + w.y * 0.5 ) * 3.0 ) * 0.08, 1.0, 0.0 ) );
        hard = 0.2;
    }
    else if ( kind == 6 )   // mud: dark, shiny where it is wet
    {
        float pud = smoothstep( 0.5, 0.65, fbm( w * 0.25 + 2.0 ) );
        col = base * ( 0.7 + 0.35 * mid ) * mix( 1.0, 0.55, pud );
        spec = 0.15 + 0.9 * pud;
        rough = mix( 16.0, 90.0, pud );
        hard = 0.2;
    }
    else if ( kind == 7 )   // snow: bright, the wheel tracks pressed and greyer
    {
        col = base * ( 0.92 + 0.1 * mid );
        if ( role == 0 )
        {
            float tr = smoothstep( 1.5, 0.3, abs( abs( lat ) - halfW * 0.42 ) ) + smoothstep( 1.0, 0.2, abs( lat ) ) * 0.4;
            col = mix( col, base * vec3( 0.72, 0.76, 0.84 ), tr * 0.6 );
        }
        spec = 0.25 * step( 0.985, hash( floor( w * 30.0 ) ) ) + 0.05;
        rough = 60.0;
        hard = 0.0;
    }
    else if ( kind == 8 )   // ice: glossy, cracked
    {
        col = base * ( 0.85 + 0.2 * mid );
        float crack = smoothstep( 0.02, 0.0, abs( cells( w * 0.7 ).y - cells( w * 0.7 ).x ) );
        col = mix( col, vec3( 0.9, 0.96, 1.0 ), crack * 0.5 );
        spec = 0.9;
        rough = 140.0;
        hard = 0.0;
    }
    else if ( kind == 9 )   // metal plates: a raised diamond pattern
    {
        vec2 q = w * 3.0;
        float d1 = abs( fract( q.x + q.y ) - 0.5 ), d2 = abs( fract( q.x - q.y ) - 0.5 );
        float bump = smoothstep( 0.1, 0.0, min( d1, d2 ) );
        vec2 plate = fract( w / 3.0 );
        float seam = max( smoothstep( 0.98, 1.0, plate.x ), smoothstep( 0.98, 1.0, plate.y ) );
        col = base * ( 0.8 + 0.25 * mid ) * ( 1.0 + 0.15 * bump ) * ( 1.0 - 0.5 * seam );
        col *= 0.85 + 0.25 * smoothstep( 0.6, 0.8, fbm( w * 0.3 + 7.0 ) ) * vec3( 1.1, 0.8, 0.6 );   // rust
        spec = 0.5;
        rough = 50.0;
    }
    else if ( kind == 10 )  // wood: planks across the way
    {
        float along = role == 3 || role == 0 ? s : w.x;
        float plank = floor( along / 0.32 );
        float gap = smoothstep( 0.0, 0.05, abs( fract( along / 0.32 ) - 0.5 ) - 0.44 );
        float grain = noise( vec2( lat * 1.5, plank * 7.0 ) * vec2( 2.0, 1.0 ) + vec2( noise( vec2( lat * 0.3, plank ) ) * 3.0 ) );
        col = base * ( 0.75 + 0.35 * hash( vec2( plank, 3.0 ) ) ) * ( 0.85 + 0.25 * grain ) * ( 1.0 - 0.6 * gap );
        spec = 0.06;
    }
    else if ( kind == 11 )  // water: dark, reflecting, rippling
    {
        vec2  q = w * 0.8 + vec2( time * 0.5, time * 0.3 );
        float gx = fbm( q + vec2( 0.15, 0.0 ) ) - fbm( q - vec2( 0.15, 0.0 ) );
        float gz = fbm( q + vec2( 0.0, 0.15 ) ) - fbm( q - vec2( 0.0, 0.15 ) );
        n = normalize( vec3( -gx * 0.8, 1.0, -gz * 0.8 ) );
        col = base * ( 0.6 + 0.2 * mid );
        spec = 1.4;
        rough = 160.0;
    }
    else if ( kind == 12 )  // grass: blades and patches
    {
        col = base * ( 0.7 + 0.45 * mid ) * ( 0.8 + 0.35 * fine );
        col = mix( col, col * vec3( 1.25, 1.1, 0.6 ), smoothstep( 0.6, 0.8, fbm( w * 0.15 + 3.0 ) ) );
        hard = 0.3;
    }
    else if ( kind == 13 )  // oil: black with a rainbow sheen
    {
        float th = fbm( w * 0.8 + time * 0.05 );
        col = vec3( 0.03 ) + 0.06 * ( 0.5 + 0.5 * cos( 6.2831 * ( th * 2.0 + vec3( 0.0, 0.33, 0.67 ) ) ) );
        spec = 1.2;
        rough = 120.0;
    }
    else
        col = base * ( 0.8 + 0.3 * mid );
    if ( role == 2 )
    {
        // The land: larger patches, a darker line along the track's edge where the wall stands.
        col *= 0.88 + 0.24 * big;
    }
    if ( role == 0 && ( kind == 0 || kind == 1 ) )
    {
        float worn = 0.65 + 0.35 * noise( w * 0.9 );
        float edge = smoothstep( 0.04, 0.0, abs( abs( lat ) - ( halfW - 0.55 ) ) - 0.11 );
        float centre = smoothstep( 0.04, 0.0, abs( lat ) - 0.09 ) * step( fract( s / 9.0 ), 0.42 );
        col = mix( col, lineColor * 0.9, max( edge, centre ) * worn );
    }
    if ( role == 1 )
    {
        float e = clamp( ( abs( lat ) - halfW ) / 0.8, 0.0, 1.0 );
        col *= 0.8 + 0.2 * e;
    }
    // Rain: hard ground darkens and shines, puddles gather in the hollows.
    float wetness = wet * hard;
    if ( wet > 0.0 && role != 2 )
    {
        float pud = smoothstep( 0.6, 0.7, fbm( w * 0.11 + 3.0 ) ) * wet;
        col *= 1.0 - 0.3 * wetness - 0.2 * pud;
        spec = max( spec, 0.35 * wetness + 1.3 * pud );
        rough = mix( rough, 150.0, max( wetness * 0.6, pud ) );
        if ( pud > 0.1 )
        {
            // Rain rings: the gradient of a smooth, slowly moving field (a gentle tilt, no grid of cells).
            vec2  q = w * 1.1 + vec2( time * 0.7, time * 0.4 );
            float e = 0.15;
            float gx = fbm( q + vec2( e, 0.0 ) ) - fbm( q - vec2( e, 0.0 ) );
            float gz = fbm( q + vec2( 0.0, e ) ) - fbm( q - vec2( 0.0, e ) );
            n = normalize( vec3( -gx * 0.6 * pud, 1.0, -gz * 0.6 * pud ) );
        }
    }
    else if ( wet > 0.0 )
        col *= 1.0 - 0.15 * wet;
    vec3 lit = lighting( fragPos, n, col, spec, rough );
    finalColor = vec4( fogged( lit, fragPos ), 1.0 );
}
)";

const char *DepthVs = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4( vertexPosition, 1.0 ); }
)";

const char *DepthFs = R"(#version 330
out vec4 finalColor;
void main() { finalColor = vec4( 1.0 ); }
)";

const char *SpriteFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float boost;
out vec4 finalColor;
void main()
{
    vec4 t = texture( texture0, fragTexCoord );
    finalColor = vec4( t.rgb * fragColor.rgb * boost, t.a * fragColor.a );
}
)";

const char *PrefilterFs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 texel;
uniform float threshold;
out vec4 finalColor;
void main()
{
    vec3 c = texture( texture0, fragTexCoord + texel * vec2( -1, -1 ) ).rgb + texture( texture0, fragTexCoord + texel * vec2( 1, -1 ) ).rgb
           + texture( texture0, fragTexCoord + texel * vec2( -1, 1 ) ).rgb + texture( texture0, fragTexCoord + texel * vec2( 1, 1 ) ).rgb;
    c *= 0.25;
    float b = max( c.r, max( c.g, c.b ) );
    float knee = 0.5;
    float soft = clamp( b - threshold + knee, 0.0, 2.0 * knee );
    soft = soft * soft / ( 4.0 * knee + 1e-4 );
    float w = max( soft, b - threshold ) / max( b, 1e-4 );
    finalColor = vec4( min( c * w, vec3( 40.0 ) ), 1.0 );
}
)";

const char *DownFs = R"(#version 330
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

const char *UpFs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 texel;
out vec4 finalColor;
void main()
{
    vec3 c = vec3( 0.0 );
    c += texture( texture0, fragTexCoord + texel * vec2( -1, 0 ) ).rgb * 2.0;
    c += texture( texture0, fragTexCoord + texel * vec2( 1, 0 ) ).rgb * 2.0;
    c += texture( texture0, fragTexCoord + texel * vec2( 0, -1 ) ).rgb * 2.0;
    c += texture( texture0, fragTexCoord + texel * vec2( 0, 1 ) ).rgb * 2.0;
    c += texture( texture0, fragTexCoord + texel * vec2( -1, -1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( 1, -1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( -1, 1 ) ).rgb;
    c += texture( texture0, fragTexCoord + texel * vec2( 1, 1 ) ).rgb;
    finalColor = vec4( c / 12.0 * 0.85, 1.0 );
}
)";

const char *FinalFs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D bloomTex;
uniform float exposure;
uniform float bloom;
uniform float hurt;
uniform float fade;
uniform float grade;
out vec4 finalColor;
vec3 aces( vec3 x )
{
    return clamp( ( x * ( 2.51 * x + 0.03 ) ) / ( x * ( 2.43 * x + 0.59 ) + 0.14 ), 0.0, 1.0 );
}
void main()
{
    vec2 uv = fragTexCoord;
    vec3 c = texture( texture0, uv ).rgb * exposure + texture( bloomTex, uv ).rgb * bloom;
    c = aces( c * 1.05 );
    // A touch of the old film: the darks a little cooler, the lights a little warmer.
    float l = dot( c, vec3( 0.3, 0.55, 0.15 ) );
    c = mix( c, c * mix( vec3( 0.92, 0.96, 1.08 ), vec3( 1.06, 1.0, 0.92 ), smoothstep( 0.2, 0.7, l ) ), grade );
    vec2 q = uv - 0.5;
    c *= 1.0 - dot( q, q ) * 0.7;
    float r = smoothstep( 0.25, 0.75, length( q ) ) * hurt;
    c = mix( c, vec3( 0.75, 0.04, 0.03 ), r * 0.6 );
    c *= 1.0 - fade;
    finalColor = vec4( c, 1.0 );
}
)";

// ---------------------------------------------------------------------------------------------------- shared

enum TexE : int
{
    T_SOFT, T_SMOKE, T_SPARK, T_FLARE, T_STREAK, T_RING, T_FLAME, T_DROP, T_COUNT
};

struct Shared
{
    int       m_users = 0;
    Shader    m_lit = {}, m_surface = {}, m_depth = {}, m_sprite = {}, m_prefilter = {}, m_down = {}, m_up = {}, m_final = {};
    Material  m_litMat = {}, m_surfMat = {}, m_depthMat = {};
    Texture2D m_tex[T_COUNT] = {};
    Forge     m_forge;
    std::map<std::string, ModelDef> m_builtin;
};
Shared g;

float noise2( float x, float y )
{
    auto h = []( int i, int j ) {
        uint32_t k = (uint32_t)i * 374761393u + (uint32_t)j * 668265263u;
        k = ( k ^ ( k >> 13 ) ) * 1274126177u;
        return ( ( k ^ ( k >> 16 ) ) & 0xffff ) / 65535.0f;
    };
    int   ix = (int)std::floor( x ), iy = (int)std::floor( y );
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * ( 3 - 2 * fx );
    fy = fy * fy * ( 3 - 2 * fy );
    return lerpf( lerpf( h( ix, iy ), h( ix + 1, iy ), fx ), lerpf( h( ix, iy + 1 ), h( ix + 1, iy + 1 ), fx ), fy );
}

Texture2D makeTex( int size, float ( *f )( float x, float y ) )
{
    Image  img = GenImageColor( size, size, BLANK );
    Color *px = (Color *)img.data;
    for ( int y = 0; y < size; ++y )
        for ( int x = 0; x < size; ++x )
        {
            float u = ( x + 0.5f ) / size * 2 - 1, v = ( y + 0.5f ) / size * 2 - 1;
            px[y * size + x] = { 255, 255, 255, (unsigned char)( clampf( f( u, v ), 0, 1 ) * 255 ) };
        }
    Texture2D t = LoadTextureFromImage( img );
    GenTextureMipmaps( &t );
    SetTextureFilter( t, TEXTURE_FILTER_TRILINEAR );
    UnloadImage( img );
    return t;
}

Shader loadLit( const char *body )
{
    std::string fs = std::string( "#version 330\n" ) + LightGlsl + body;
    Shader      s = LoadShaderFromMemory( LitVs, fs.c_str() );
    s.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation( s, "matModel" );
    s.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation( s, "matNormal" );
    return s;
}

/** @brief The models the engine needs itself (guns, gear, stand-ins for pickups). */
void builtins()
{
    const char *defs[][8] = {
        { "_gun_mg", "box w=0.26 h=0.16 d=0.55 y=0.08 color=#2a2a30", "gun r=0.05 len=0.8 y=0.1 z=-0.55 color=#18181c", nullptr },
        { "_gun_twinmg", "box w=0.5 h=0.16 d=0.55 y=0.08 color=#2a2a30", "gun r=0.05 len=0.8 x=0.14 y=0.1 z=-0.55 color=#18181c mirror=1", nullptr },
        { "_gun_cannon", "box w=0.4 h=0.26 d=0.8 y=0.13 color=#3a3a32", "gun r=0.1 len=1.2 y=0.16 z=-0.8 color=#202018", "disc r=0.14 h=0.1 y=0.16 z=-1.4 pitch=90 color=#151510", nullptr },
        { "_gun_flamer", "box w=0.34 h=0.22 d=0.6 y=0.11 color=#4a3a2a", "gun r=0.07 len=0.7 y=0.12 z=-0.5 color=#202020", "cone r=0.12 r2=0.06 len=0.2 y=0.12 z=-0.95 color=#ff8030 emit=0.6", "disc r=0.18 h=0.5 y=0.25 z=0.15 pitch=90 color=#aa3020", nullptr },
        { "_pod", "box w=0.6 h=0.28 d=0.8 y=0.14 color=#3a3f38", "disc r=0.07 h=0.04 x=0.14 y=0.2 z=-0.41 pitch=90 color=#ff5030 emit=0.5 mirror=1", "disc r=0.07 h=0.04 x=0.14 y=0.08 z=-0.41 pitch=90 color=#ff5030 emit=0.5 mirror=1", nullptr },
        { "_rack", "box w=1.1 h=0.14 d=0.32 y=0.07 color=#2a2a2a", "disc r=0.12 h=0.06 x=0.3 y=0.15 color=#4a4a3a mirror=1", "disc r=0.12 h=0.06 y=0.15 color=#4a4a3a", nullptr },
        { "_mine", "disc r=0.42 h=0.12 y=0.06 color=#3a3a34 seg=10", "disc r=0.26 h=0.08 y=0.14 color=#4a4a40 seg=8", "light r=0.07 y=0.22 color=#ff2010", nullptr },
        { "_spikes", "box w=6.4 h=0.05 d=0.35 y=0.03 color=#2a2a2a", "greeble n=40 w=6.2 d=0.3 y=0.06 size=0.1 seed=3 color=#9a9aa0", nullptr },
        { "_rocket", "hull len=0.95 wid=0.2 hgt=0.2 shape=cigar color=#c8c0b0", "cone r=0.1 len=0.25 z=-0.55 color=#c02010", "fin h=0.15 root=0.25 tip=0.1 sweep=0.1 z=0.35 thick=0.02 color=#404040", nullptr },
        { "_barrel", "disc r=0.45 h=1.0 y=0.5 seg=12 color=#b02818", "disc r=0.46 h=0.12 y=0.7 seg=12 color=#e0c020", "disc r=0.46 h=0.06 y=0.2 seg=12 color=#801810", nullptr },
        { "_pickup", "box w=1.1 h=0.6 d=0.7 y=0.3 color=#d0b040 emit=0.3", nullptr },
        { "_coffin", "box w=0.8 h=0.45 d=1.9 y=0.23 taper=0.15 color=#4a2a18", "box w=0.82 h=0.06 d=1.92 y=0.48 color=#6a3a20", "box w=0.08 h=0.02 d=0.6 y=0.52 z=-0.2 color=#d0b060 emit=0.3", "box w=0.4 h=0.02 d=0.08 y=0.52 z=-0.35 color=#d0b060 emit=0.3", nullptr },
    };
    for ( auto &d : defs )
    {
        ModelDef m;
        m.m_name = d[0];
        for ( int k = 1; k < 8 && d[k]; ++k ) m.m_parts.push_back( parseLine( d[k] ) );
        g.m_builtin[m.m_name] = m;
    }
}

void makeShared()
{
    g.m_lit = loadLit( LitFsBody );
    g.m_surface = loadLit( SurfaceFsBody );
    g.m_depth = LoadShaderFromMemory( DepthVs, DepthFs );
    g.m_sprite = LoadShaderFromMemory( nullptr, SpriteFs );
    g.m_prefilter = LoadShaderFromMemory( nullptr, PrefilterFs );
    g.m_down = LoadShaderFromMemory( nullptr, DownFs );
    g.m_up = LoadShaderFromMemory( nullptr, UpFs );
    g.m_final = LoadShaderFromMemory( nullptr, FinalFs );
    g.m_litMat = LoadMaterialDefault();
    g.m_litMat.shader = g.m_lit;
    g.m_surfMat = LoadMaterialDefault();
    g.m_surfMat.shader = g.m_surface;
    g.m_depthMat = LoadMaterialDefault();
    g.m_depthMat.shader = g.m_depth;
    g.m_tex[T_SOFT] = makeTex( 64, []( float x, float y ) { return std::exp( -( x * x + y * y ) * 4.5f ); } );
    g.m_tex[T_SMOKE] = makeTex( 128, []( float x, float y ) {
        float d = std::sqrt( x * x + y * y );
        float n = noise2( x * 3 + 7, y * 3 + 3 ) * 0.6f + noise2( x * 7 + 1, y * 7 + 9 ) * 0.4f;
        return clampf( ( 1.0f - d ) * 1.6f, 0, 1 ) * ( 0.45f + 0.75f * n );
    } );
    g.m_tex[T_SPARK] = makeTex( 64, []( float x, float y ) {
        float d = std::sqrt( x * x + y * y );
        return std::exp( -std::fabs( x ) * 22 ) * std::exp( -std::fabs( y ) * 2.5f ) +
               std::exp( -std::fabs( y ) * 22 ) * std::exp( -std::fabs( x ) * 2.5f ) + std::exp( -d * d * 18 );
    } );
    g.m_tex[T_FLARE] = makeTex( 128, []( float x, float y ) {
        float d = std::sqrt( x * x + y * y );
        return std::exp( -d * d * 30 ) + std::exp( -std::fabs( y ) * 40 ) * ( 1 - std::fabs( x ) ) * 0.6f;
    } );
    g.m_tex[T_STREAK] = makeTex( 64, []( float x, float y ) { return std::exp( -x * x * 1.6f - y * y * 26.0f ); } );
    g.m_tex[T_RING] = makeTex( 128, []( float x, float y ) {
        float d = std::sqrt( x * x + y * y );
        return std::exp( -( d - 0.8f ) * ( d - 0.8f ) * 160.0f );
    } );
    g.m_tex[T_FLAME] = makeTex( 64, []( float x, float y ) {
        float d = std::sqrt( x * x + y * y * 0.6f );
        return clampf( ( 1.0f - d ) * 1.4f, 0, 1 ) * ( 0.6f + 0.6f * noise2( x * 5 + 3, y * 5 ) );
    } );
    g.m_tex[T_DROP] = makeTex( 32, []( float x, float y ) { return std::exp( -x * x * 30.0f ) * clampf( 1 - std::fabs( y ), 0, 1 ); } );
    builtins();
}

RenderTexture2D hdrTarget( int w, int h, bool depth )
{
    RenderTexture2D t = {};
    t.id = rlLoadFramebuffer();
    rlEnableFramebuffer( t.id );
    t.texture.id = rlLoadTexture( nullptr, w, h, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1 );
    t.texture.width = w;
    t.texture.height = h;
    t.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    t.texture.mipmaps = 1;
    rlFramebufferAttach( t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0 );
    if ( depth )
    {
        t.depth.id = rlLoadTextureDepth( w, h, true );
        t.depth.width = w;
        t.depth.height = h;
        rlFramebufferAttach( t.id, t.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0 );
    }
    if ( !rlFramebufferComplete( t.id ) ) TraceLog( LOG_ERROR, "GFX: HDR target %dx%d incomplete", w, h );
    rlDisableFramebuffer();
    rlTextureParameters( t.texture.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR );
    rlTextureParameters( t.texture.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR );
    rlTextureParameters( t.texture.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP );
    rlTextureParameters( t.texture.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP );
    return t;
}

RenderTexture2D depthTarget( int size )
{
    RenderTexture2D t = {};
    t.id = rlLoadFramebuffer();
    t.texture.width = t.texture.height = size;
    rlEnableFramebuffer( t.id );
    t.depth.id = rlLoadTextureDepth( size, size, false );
    t.depth.width = t.depth.height = size;
    t.depth.format = 19;
    t.depth.mipmaps = 1;
    rlFramebufferAttach( t.id, t.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0 );
    if ( !rlFramebufferComplete( t.id ) ) TraceLog( LOG_ERROR, "GFX: shadow target incomplete" );
    rlDisableFramebuffer();
    return t;
}

void fullscreen( Shader s, Texture2D src, int w, int h )
{
    BeginShaderMode( s );
    DrawTexturePro( src, { 0, 0, (float)src.width, (float)-src.height }, { 0, 0, (float)w, (float)h }, { 0, 0 }, 0, WHITE );
    EndShaderMode();
}

Vector3 v3( Color c, float s = 1.0f ) { return { c.r / 255.0f * s, c.g / 255.0f * s, c.b / 255.0f * s }; }

void setVec3( Shader s, const char *name, Vector3 v ) { SetShaderValue( s, GetShaderLocation( s, name ), &v, SHADER_UNIFORM_VEC3 ); }
void setFloat( Shader s, const char *name, float v ) { SetShaderValue( s, GetShaderLocation( s, name ), &v, SHADER_UNIFORM_FLOAT ); }
void setInt( Shader s, const char *name, int v ) { SetShaderValue( s, GetShaderLocation( s, name ), &v, SHADER_UNIFORM_INT ); }
void setVec2( Shader s, const char *name, Vector2 v ) { SetShaderValue( s, GetShaderLocation( s, name ), &v, SHADER_UNIFORM_VEC2 ); }
void setVec4( Shader s, const char *name, Vector4 v ) { SetShaderValue( s, GetShaderLocation( s, name ), &v, SHADER_UNIFORM_VEC4 ); }

/** @return a model: the data's, else a built-in one */
const ModelMesh &modelOf( const std::string &name, const Paint &paint = Paint() )
{
    if ( !g_data.model( name ) )
    {
        auto it = g.m_builtin.find( name );
        if ( it != g.m_builtin.end() )
        {
            static std::map<std::string, ModelMesh> cache;
            auto c = cache.find( name );
            if ( c != cache.end() ) return c->second;
            return cache[name] = Forge::build( it->second, true, paint );
        }
    }
    return g.m_forge.get( g_data, name, paint );
}

/** @return a ground matrix: at (x, z), turned by heading (radians), raised by y */
Matrix place( Vector2 p, float head, float y = 0.0f, float scale = 1.0f )
{
    return MatrixMultiply( MatrixMultiply( MatrixScale( scale, scale, scale ), MatrixRotateY( -head ) ), MatrixTranslate( p.x, y, p.y ) );
}

/** @return the base colour of a surface kind (the road's own when it is the venue's road) */
Color surfaceColor( SurfaceE s, const VenueDef *v, int role )
{
    if ( v )
    {
        if ( role == 0 && s == v->m_road ) return v->m_roadColor;
        if ( role == 1 && s == v->m_shoulder ) return v->m_shoulderColor;
        if ( role == 2 && s == v->m_ground ) return v->m_groundColor;
    }
    switch ( s )
    {
    case SurfaceE::ASPHALT: return { 58, 58, 62, 255 };
    case SurfaceE::CONCRETE: return { 120, 118, 112, 255 };
    case SurfaceE::COBBLE: return { 96, 90, 84, 255 };
    case SurfaceE::DIRT: return { 112, 86, 60, 255 };
    case SurfaceE::GRAVEL: return { 124, 116, 104, 255 };
    case SurfaceE::SAND: return { 200, 172, 120, 255 };
    case SurfaceE::MUD: return { 70, 56, 38, 255 };
    case SurfaceE::SNOW: return { 230, 236, 244, 255 };
    case SurfaceE::ICE: return { 176, 210, 236, 255 };
    case SurfaceE::METAL: return { 110, 114, 122, 255 };
    case SurfaceE::WOOD: return { 112, 78, 50, 255 };
    case SurfaceE::WATER: return { 26, 40, 52, 255 };
    case SurfaceE::GRASS: return { 62, 92, 44, 255 };
    case SurfaceE::OIL: return { 10, 10, 12, 255 };
    default: return GRAY;
    }
}

}   // namespace

Forge &Renderer::forge() { return g.m_forge; }

// ---------------------------------------------------------------------------------------------------- setup

void Renderer::init( int w, int h, int quality )
{
    if ( g.m_users++ == 0 ) makeShared();
    m_quality = std::clamp( quality, 0, 2 );
    m_w = std::max( 64, w );
    m_h = std::max( 64, h );
    m_hdr = hdrTarget( m_w, m_h, true );
    m_out = LoadRenderTexture( m_w, m_h );
    SetTextureFilter( m_out.texture, TEXTURE_FILTER_BILINEAR );
    m_shadowSize = m_quality == 0 ? 1024 : m_quality == 1 ? 2048 : 3072;
    m_shadow = depthTarget( m_shadowSize );
    int bw = m_w / 2, bh = m_h / 2;
    for ( int i = 0; i < ( m_quality == 0 ? 4 : 6 ) && bw >= 4 && bh >= 4; ++i )
    {
        m_bloom.push_back( hdrTarget( bw, bh, false ) );
        bw /= 2;
        bh /= 2;
    }
    m_cam.up = { 0, 1, 0 };
    m_cam.fovy = 45.0f;
    m_cam.projection = CAMERA_PERSPECTIVE;
    m_rng.seed( 1234 );
}

void Renderer::shutdown()
{
    if ( m_w == 0 ) return;
    clearTrack();
    UnloadRenderTexture( m_out );
    rlUnloadFramebuffer( m_shadow.id );
    rlUnloadTexture( m_shadow.depth.id );
    rlUnloadFramebuffer( m_hdr.id );
    rlUnloadTexture( m_hdr.texture.id );
    if ( m_hdr.depth.id ) rlUnloadTexture( m_hdr.depth.id );
    for ( RenderTexture2D &t : m_bloom )
    {
        rlUnloadFramebuffer( t.id );
        rlUnloadTexture( t.texture.id );
    }
    m_bloom.clear();
    m_w = 0;
    if ( --g.m_users == 0 )
    {
        g.m_forge.clear();
        for ( Shader s : { g.m_lit, g.m_surface, g.m_depth, g.m_sprite, g.m_prefilter, g.m_down, g.m_up, g.m_final } ) UnloadShader( s );
        for ( Texture2D &t : g.m_tex ) UnloadTexture( t );
    }
}

void Renderer::clearTrack()
{
    if ( m_hasScene )
    {
        UnloadMesh( m_ground );
        UnloadMesh( m_road );
        if ( m_walls.vertexCount ) UnloadMesh( m_walls );
        for ( Chunk &c : m_chunks ) UnloadMesh( c.m_mesh );
    }
    m_chunks.clear();
    m_lamps.clear();
    m_parts.clear();
    m_glows.clear();
    m_marks.clear();
    m_markNext = 0;
    m_hasScene = false;
    m_track = nullptr;
}

void Renderer::setTrack( const Track &t )
{
    clearTrack();
    m_track = &t;
    m_venue = t.m_venue;
    const VenueDef *v = m_venue;
    m_dark = v && ( v->m_time == "night" || v->m_time == "dusk" || v->m_time == "dawn" || v->m_weather == "storm" );
    m_wet = v && ( v->m_weather == "rain" || v->m_weather == "storm" ) ? 1.0f : 0.0f;
    float az = ( v ? v->m_sunDir : 40.0f ) * Deg, el = ( v ? v->m_sunHeight : 45.0f ) * Deg;
    Vector3 toSun = { std::sin( az ) * std::cos( el ), std::sin( el ), -std::cos( az ) * std::cos( el ) };
    m_sunDir = Vector3Negate( Vector3Normalize( toSun ) );
    buildGround();
    buildRoad();
    buildWalls();
    buildProps();
    m_marks.assign( m_quality == 0 ? 3000 : 6000, Mark{ { 0, 0 }, { 0, 0 }, 0, 0, BLANK } );
    m_markNext = 0;
    m_hasScene = true;
}

void Renderer::buildGround()
{
    const Track &t = *m_track;
    MeshBuilder  mb;
    mb.m_withUv = true;
    mb.m_noise = 0;
    float     margin = 220.0f;
    Rectangle r = { t.m_bounds.x - margin, t.m_bounds.y - margin, t.m_bounds.width + 2 * margin, t.m_bounds.height + 2 * margin };
    int       nx = std::max( 4, (int)( r.width / 24.0f ) ), nz = std::max( 4, (int)( r.height / 24.0f ) );
    SurfaceE  kind = m_venue ? m_venue->m_ground : SurfaceE::GRASS;
    Color     c = surfaceColor( kind, m_venue, 2 );
    c.a = (unsigned char)kind;
    for ( int z = 0; z < nz; ++z )
        for ( int x = 0; x < nx; ++x )
        {
            float   x0 = r.x + r.width * x / nx, x1 = r.x + r.width * ( x + 1 ) / nx;
            float   z0 = r.y + r.height * z / nz, z1 = r.y + r.height * ( z + 1 ) / nz;
            Vector3 n = { 0, 1, 0 };
            Vector2 u2 = { 0, 2 };
            Vector3 a = { x0, 0, z0 }, b = { x1, 0, z0 }, cc = { x1, 0, z1 }, d = { x0, 0, z1 };
            for ( Vector3 p : { a, d, cc, a, cc, b } ) mb.vert( p, n, c, { p.x, p.z }, u2 );
        }
    m_ground = mb.mesh( true );
}

void Renderer::buildRoad()
{
    const Track &t = *m_track;
    MeshBuilder  mb;
    mb.m_withUv = true;
    mb.m_noise = 0;
    int     n = (int)t.m_s.size();
    Vector3 up = { 0, 1, 0 };
    auto strip = [&]( int i, float lat0, float lat1, int role, SurfaceE kind, float y ) {
        const TrackSample &a = t.m_s[i], &b = t.m_s[( i + 1 ) % n];
        float sa = a.m_s, sb = i + 1 == n ? t.m_length : b.m_s;
        float ya = y + ( a.m_bridge ? 0.05f : 0.0f ), yb = y + ( b.m_bridge ? 0.05f : 0.0f );
        auto  P = [&]( const TrackSample &q, float lat, float yy ) {
            Vector2 p = add( q.m_pos, mul( q.m_right, lat ) );
            return Vector3{ p.x, yy, p.y };
        };
        float la0 = lat0 < -50 ? -( a.m_half + a.m_shoulder + 0.8f ) : lat0 > 50 ? a.m_half + a.m_shoulder + 0.8f : lat0;
        float la1 = lat1 < -50 ? -( a.m_half + a.m_shoulder + 0.8f ) : lat1 > 50 ? a.m_half + a.m_shoulder + 0.8f : lat1;
        float lb0 = lat0 < -50 ? -( b.m_half + b.m_shoulder + 0.8f ) : lat0 > 50 ? b.m_half + b.m_shoulder + 0.8f : lat0;
        float lb1 = lat1 < -50 ? -( b.m_half + b.m_shoulder + 0.8f ) : lat1 > 50 ? b.m_half + b.m_shoulder + 0.8f : lat1;
        // Road edges follow each sample's own width.
        if ( role == 0 )
        {
            la0 = -a.m_half;
            la1 = a.m_half;
            lb0 = -b.m_half;
            lb1 = b.m_half;
        }
        else if ( role == 1 && lat0 < 0 )
        {
            la1 = -a.m_half;
            lb1 = -b.m_half;
        }
        else if ( role == 1 )
        {
            la0 = a.m_half;
            lb0 = b.m_half;
        }
        Color ca = surfaceColor( kind, m_venue, role );
        ca.a = (unsigned char)kind;
        Vector3 p00 = P( a, la0, ya ), p01 = P( a, la1, ya ), p10 = P( b, lb0, yb ), p11 = P( b, lb1, yb );
        Vector2 ua0 = { sa, la0 }, ua1 = { sa, la1 }, ub0 = { sb, lb0 }, ub1 = { sb, lb1 };
        Vector2 qa = { a.m_half, (float)role }, qb = { b.m_half, (float)role };
        mb.vert( p00, up, ca, ua0, qa );
        mb.vert( p10, up, ca, ub0, qb );
        mb.vert( p11, up, ca, ub1, qb );
        mb.vert( p00, up, ca, ua0, qa );
        mb.vert( p11, up, ca, ub1, qb );
        mb.vert( p01, up, ca, ua1, qa );
    };
    SurfaceE sh = m_venue ? m_venue->m_shoulder : SurfaceE::GRAVEL;
    for ( int i = 0; i < n; ++i )
    {
        strip( i, -100, 0, 1, sh, 0.02f );
        strip( i, 0, 100, 1, sh, 0.02f );
        strip( i, 0, 0, 0, t.m_s[i].m_surface, 0.035f );
    }
    // Patches: quads over the road.
    for ( const PatchSpot &pa : t.m_patches )
    {
        float span = pa.m_s1 >= pa.m_s0 ? pa.m_s1 - pa.m_s0 : pa.m_s1 + t.m_length - pa.m_s0;
        for ( float d = 0; d < span; d += t.m_step )
        {
            float              s0 = t.wrap( pa.m_s0 + d ), s1 = t.wrap( pa.m_s0 + std::min( span, d + t.m_step ) );
            const TrackSample &a = t.sampleAt( s0 ), &b = t.sampleAt( s1 );
            float lo0 = std::max( pa.m_lat0, -a.m_half ), hi0 = std::min( pa.m_lat1, a.m_half );
            float lo1 = std::max( pa.m_lat0, -b.m_half ), hi1 = std::min( pa.m_lat1, b.m_half );
            Vector2 A = t.pointAt( s0, lo0 ), B = t.pointAt( s0, hi0 ), C = t.pointAt( s1, hi1 ), D = t.pointAt( s1, lo1 );
            Color   c = surfaceColor( pa.m_surface, m_venue, 3 );
            c.a = (unsigned char)pa.m_surface;
            float y = 0.045f + ( a.m_bridge ? 0.05f : 0.0f );
            Vector2 qa = { a.m_half, 3 };
            float   ss0 = pa.m_s0 + d, ss1 = ss0 + t.m_step;
            mb.vert( { A.x, y, A.y }, up, c, { ss0, lo0 }, qa );
            mb.vert( { D.x, y, D.y }, up, c, { ss1, lo1 }, qa );
            mb.vert( { C.x, y, C.y }, up, c, { ss1, hi1 }, qa );
            mb.vert( { A.x, y, A.y }, up, c, { ss0, lo0 }, qa );
            mb.vert( { C.x, y, C.y }, up, c, { ss1, hi1 }, qa );
            mb.vert( { B.x, y, B.y }, up, c, { ss0, hi0 }, qa );
        }
    }
    m_road = mb.mesh( true );
}

void Renderer::buildWalls()
{
    const Track &t = *m_track;
    MeshBuilder  mb;
    mb.m_noise = 0.05f;
    mb.m_rng.seed( 77 );
    Rng rng;
    rng.seed( 4242 );
    int n = (int)t.m_s.size();
    for ( int side = 0; side < 2; ++side )
    {
        float sg = side == 0 ? -1.0f : 1.0f;
        // Runs of samples with a wall of one style.
        int start = 0;
        for ( int k = 0; k < n; ++k )
        {
            bool on = side == 0 ? t.m_s[k].m_wallL : t.m_s[k].m_wallR;
            if ( !on )
            {
                start = k + 1;
                break;
            }
        }
        std::vector<int> run;
        auto flush = [&]() {
            if ( run.size() < 2 ) return;
            int                styleIx = side == 0 ? t.m_s[run[0]].m_wall : t.m_s[run[0]].m_wall2;
            const std::string &style = t.m_wallStyles.empty() ? std::string( "concrete" ) : t.m_wallStyles[styleIx];
            // The wall line, and the way outwards.
            std::vector<Vector2> p, o;
            for ( int i : run )
            {
                const TrackSample &q = t.m_s[i];
                p.push_back( add( q.m_pos, mul( q.m_right, sg * ( q.m_half + q.m_shoulder + 0.6f ) ) ) );
                o.push_back( mul( q.m_right, sg ) );
            }
            auto extrude = [&]( const std::vector<Vector2> &prof, Color c, float vary ) {
                // prof: (outwards, height) points; quads between neighbouring points along the run.
                for ( size_t i = 0; i + 1 < p.size(); ++i )
                {
                    Color cc = vary > 0 ? scaleColor( c, 1.0f + rng.range( -vary, vary ) ) : c;
                    cc.a = c.a;
                    Vector3 mid = { ( p[i].x + p[i + 1].x ) * 0.5f, 0.4f, ( p[i].y + p[i + 1].y ) * 0.5f };
                    for ( size_t k = 0; k + 1 < prof.size(); ++k )
                    {
                        auto P = [&]( size_t a, size_t b ) {
                            Vector2 q = add( p[a], mul( o[a], prof[b].x ) );
                            return Vector3{ q.x, prof[b].y, q.y };
                        };
                        Vector3 inside = mid;
                        inside.x += ( o[i].x ) * ( prof[0].x + prof.back().x ) * 0.5f;
                        inside.z += ( o[i].y ) * ( prof[0].x + prof.back().x ) * 0.5f;
                        inside.y = 0.3f;
                        mb.quad( P( i, k ), P( i + 1, k ), P( i + 1, k + 1 ), P( i, k + 1 ), cc, inside );
                    }
                }
            };
            // Things set along the run every 'every' metres.
            auto along = [&]( float every, auto fn ) {
                float acc = 0;
                for ( size_t i = 0; i + 1 < p.size(); ++i )
                {
                    Vector2 d = sub( p[i + 1], p[i] );
                    float   l = len( d );
                    while ( acc <= l )
                    {
                        Vector2 at = add( p[i], mul( d, acc / std::max( l, 1e-4f ) ) );
                        fn( at, o[i], headingOf( d ) );
                        acc += every;
                    }
                    acc -= l;
                }
            };
            if ( style == "concrete" || style == "neon" || style == "none" )
            {
                Color c = { 168, 166, 158, 0 };
                if ( style == "none" )
                    extrude( { { -0.15f, 0 }, { -0.15f, 0.15f }, { 0.15f, 0.15f }, { 0.15f, 0 } }, { 150, 40, 36, 0 }, 0.0f );
                else
                    extrude( { { -0.3f, 0 }, { -0.3f, 0.22f }, { -0.12f, 0.42f }, { -0.1f, 0.85f }, { 0.1f, 0.85f }, { 0.12f, 0.42f }, { 0.3f, 0.22f }, { 0.3f, 0 } }, c, 0.08f );
                if ( style == "neon" )
                {
                    int k = 0;
                    along( 8.0f, [&]( Vector2 at, Vector2, float hd ) {
                        static const Color cols[] = { { 255, 40, 200, 255 }, { 40, 220, 255, 255 }, { 255, 200, 40, 255 } };
                        mb.box( { at.x, 0.92f, at.y }, { 0.05f, 0.05f, 3.6f }, hd, cols[( k++ / 3 ) % 3] );
                    } );
                }
                else if ( style == "concrete" )
                {
                    int k = 0;
                    along( 3.0f, [&]( Vector2 at, Vector2 out, float hd ) {
                        if ( ( k++ % 2 ) == 0 )
                        {
                            Vector2 q = sub( at, mul( out, 0.11f ) );
                            mb.box( { q.x, 0.6f, q.y }, { 0.02f, 0.18f, 1.4f }, hd, { 190, 40, 30, 0 } );
                        }
                    } );
                }
            }
            else if ( style == "stone" )
                extrude( { { -0.35f, 0 }, { -0.35f, 1.3f }, { -0.42f, 1.36f }, { 0.42f, 1.36f }, { 0.35f, 1.3f }, { 0.35f, 0 } }, { 112, 108, 98, 0 }, 0.12f );
            else if ( style == "hedge" )
                extrude( { { -0.6f, 0 }, { -0.7f, 1.2f }, { -0.5f, 1.9f }, { 0.5f, 1.9f }, { 0.7f, 1.2f }, { 0.6f, 0 } }, { 46, 76, 36, 0 }, 0.18f );
            else if ( style == "snowbank" )
                extrude( { { -1.1f, 0 }, { -0.8f, 0.6f }, { -0.3f, 1.1f }, { 0.4f, 1.25f }, { 1.2f, 0.7f }, { 1.8f, 0 } }, { 226, 232, 240, 0 }, 0.04f );
            else if ( style == "wood" )
            {
                extrude( { { -0.04f, 0 }, { -0.04f, 1.2f }, { 0.04f, 1.2f }, { 0.04f, 0 } }, { 104, 74, 46, 0 }, 0.2f );
                along( 2.5f, [&]( Vector2 at, Vector2 out, float hd ) {
                    Vector2 q = add( at, mul( out, 0.12f ) );
                    mb.box( { q.x, 0.7f, q.y }, { 0.08f, 0.7f, 0.08f }, hd, { 80, 56, 36, 0 } );
                } );
            }
            else if ( style == "rail" )
            {
                extrude( { { -0.05f, 0.42f }, { -0.1f, 0.55f }, { -0.05f, 0.68f }, { -0.1f, 0.78f }, { 0.02f, 0.78f }, { 0.02f, 0.42f } }, { 180, 184, 190, 0 }, 0.03f );
                along( 2.0f, [&]( Vector2 at, Vector2 out, float hd ) {
                    Vector2 q = add( at, mul( out, 0.12f ) );
                    mb.box( { q.x, 0.4f, q.y }, { 0.06f, 0.4f, 0.06f }, hd, { 120, 124, 130, 0 } );
                } );
            }
            else if ( style == "iron" )
            {
                extrude( { { -0.25f, 0 }, { -0.25f, 0.5f }, { 0.25f, 0.5f }, { 0.25f, 0 } }, { 90, 86, 80, 0 }, 0.08f );
                extrude( { { -0.03f, 1.75f }, { -0.03f, 1.82f }, { 0.03f, 1.82f }, { 0.03f, 1.75f } }, { 24, 24, 28, 0 }, 0.0f );
                along( 0.25f, [&]( Vector2 at, Vector2, float hd ) {
                    mb.box( { at.x, 1.2f, at.y }, { 0.025f, 0.7f, 0.025f }, hd, { 26, 26, 30, 0 } );
                    mb.box( { at.x, 1.95f, at.y }, { 0.04f, 0.08f, 0.04f }, hd, { 30, 30, 34, 0 }, 0.9f );
                } );
                along( 6.0f, [&]( Vector2 at, Vector2, float hd ) { mb.box( { at.x, 1.0f, at.y }, { 0.22f, 1.0f, 0.22f }, hd, { 96, 92, 86, 0 } ); } );
            }
            else if ( style == "fence" )
            {
                for ( float y : { 0.3f, 1.0f, 1.7f } )
                    extrude( { { -0.02f, y }, { -0.02f, y + 0.04f }, { 0.02f, y + 0.04f }, { 0.02f, y } }, { 140, 144, 148, 0 }, 0.0f );
                extrude( { { -0.01f, 0.3f }, { -0.01f, 1.72f }, { 0.01f, 1.72f }, { 0.01f, 0.3f } }, { 70, 74, 78, 0 }, 0.1f );
                along( 3.0f, [&]( Vector2 at, Vector2, float hd ) { mb.box( { at.x, 0.95f, at.y }, { 0.05f, 0.95f, 0.05f }, hd, { 120, 124, 128, 0 } ); } );
            }
            else if ( style == "tires" )
            {
                int k = 0;
                along( 0.78f, [&]( Vector2 at, Vector2, float hd ) {
                    int  stack = 2 + ( k % 3 == 0 ? 1 : 0 );
                    bool paint = ( k / 3 ) % 4 == 0;
                    for ( int h = 0; h < stack; ++h )
                    {
                        Color c = paint && h == stack - 1 ? Color{ 200, 40, 30, 0 } : Color{ 30, 30, 32, 0 };
                        for ( int sgm = 0; sgm < 6; ++sgm )
                        {
                            float a0 = sgm * Pi / 3, a1 = ( sgm + 1 ) * Pi / 3;
                            Vector3 c0 = { at.x, 0.13f + h * 0.26f, at.y };
                            Vector3 q0 = { at.x + std::cos( a0 ) * 0.36f, c0.y - 0.12f, at.y + std::sin( a0 ) * 0.36f };
                            Vector3 q1 = { at.x + std::cos( a1 ) * 0.36f, c0.y - 0.12f, at.y + std::sin( a1 ) * 0.36f };
                            Vector3 q2 = { q1.x, c0.y + 0.12f, q1.z }, q3 = { q0.x, c0.y + 0.12f, q0.z };
                            mb.quad( q0, q1, q2, q3, c, c0 );
                            Vector3 top = { at.x, c0.y + 0.12f, at.y };
                            mb.tri( top, q3, q2, { 18, 18, 20, 0 }, { at.x, c0.y - 1, at.y } );
                        }
                    }
                    ++k;
                    (void)hd;
                } );
            }
            else if ( style == "sandbags" )
            {
                int k = 0;
                along( 0.62f, [&]( Vector2 at, Vector2 out, float hd ) {
                    for ( int h = 0; h < 3; ++h )
                    {
                        Vector2 q = add( at, mul( out, h * 0.04f ) );
                        float   shift = ( h % 2 ) * 0.3f;
                        Vector2 dir = headingDir( hd );
                        q = add( q, mul( dir, shift ) );
                        mb.box( { q.x, 0.15f + h * 0.28f, q.y }, { 0.32f, 0.14f, 0.3f }, hd, scaleColor( { 150, 132, 96, 0 }, rng.range( 0.85f, 1.1f ) ), 0.25f );
                    }
                    ++k;
                } );
            }
            else if ( style == "containers" )
            {
                static const Color cols[] = { { 150, 50, 36, 0 }, { 40, 80, 130, 0 }, { 50, 110, 70, 0 }, { 190, 120, 40, 0 }, { 110, 110, 116, 0 } };
                along( 6.2f, [&]( Vector2 at, Vector2 out, float hd ) {
                    Vector2 q = add( at, mul( out, 1.25f ) );
                    Color   c = cols[rng.below( 5 )];
                    float   hgt = rng.chance( 0.25f ) ? 5.2f : 2.6f;
                    mb.box( { q.x, hgt * 0.5f, q.y }, { 1.2f, hgt * 0.5f, 3.0f }, hd, c );
                    for ( int r = -2; r <= 2; ++r )
                    {
                        Vector2 rr = add( q, mul( headingDir( hd ), r * 1.1f ) );
                        Vector2 face = sub( rr, mul( out, 1.22f ) );
                        mb.box( { face.x, hgt * 0.5f, face.y }, { 0.05f, hgt * 0.47f, 0.08f }, hd, scaleColor( c, 0.8f ) );
                    }
                } );
            }
            else if ( style == "pipes" )
            {
                for ( float y : { 0.5f, 1.25f } )
                {
                    std::vector<Vector2> prof;
                    for ( int k = 0; k <= 8; ++k )
                    {
                        float a = k * Pi * 2 / 8;
                        prof.push_back( { 0.35f + std::cos( a ) * 0.3f, y + std::sin( a ) * 0.3f } );
                    }
                    extrude( prof, { 130, 92, 60, 0 }, 0.12f );
                }
                along( 4.0f, [&]( Vector2 at, Vector2 out, float hd ) {
                    Vector2 q = add( at, mul( out, 0.35f ) );
                    mb.box( { q.x, 0.8f, q.y }, { 0.5f, 0.8f, 0.12f }, hd, { 60, 60, 64, 0 } );
                } );
            }
            else   // scrap: piled wrecks
            {
                static const Color cols[] = { { 120, 64, 40, 0 }, { 90, 80, 70, 0 }, { 70, 60, 54, 0 }, { 140, 90, 50, 0 }, { 60, 70, 80, 0 } };
                along( 2.2f, [&]( Vector2 at, Vector2 out, float hd ) {
                    int pile = 1 + rng.below( 3 );
                    for ( int h = 0; h < pile; ++h )
                    {
                        Vector2 q = add( at, mul( out, 0.8f + rng.range( -0.4f, 0.6f ) ) );
                        float   w = rng.range( 0.7f, 1.0f ), l = rng.range( 1.4f, 2.2f ), hh = rng.range( 0.35f, 0.6f );
                        mb.box( { q.x, 0.3f + h * 0.7f, q.y }, { w, hh, l }, hd + rng.range( -0.6f, 0.6f ), cols[rng.below( 5 )], 0.2f );
                    }
                } );
            }
            run.clear();
        };
        for ( int k = 0; k <= n; ++k )
        {
            int  i = ( start + k ) % n;
            bool on = side == 0 ? t.m_s[i].m_wallL : t.m_s[i].m_wallR;
            auto styleOf = [&]( int j ) { return side == 0 ? t.m_s[j].m_wall : t.m_s[j].m_wall2; };
            if ( k < n && on && ( run.empty() || styleOf( i ) == styleOf( run.back() ) ) )
            {
                run.push_back( i );
                continue;
            }
            if ( k < n && on )
            {
                run.push_back( i );   // the joint belongs to both styles
                flush();
                run.push_back( i );
                continue;
            }
            if ( k == n && on && !run.empty() ) run.push_back( ( start + n ) % n );
            flush();
            if ( on && k < n ) run.push_back( i );
        }
    }
    m_walls = mb.mesh( true );
}

void Renderer::buildProps()
{
    const Track &t = *m_track;
    const float  cell = 64.0f;
    std::map<std::pair<int, int>, MeshBuilder> cells;
    std::map<std::string, ModelMesh>           cpu;
    for ( const PropSpot &p : t.m_props )
    {
        auto it = cpu.find( p.m_model );
        if ( it == cpu.end() )
        {
            const ModelDef *d = g_data.model( p.m_model );
            ModelMesh       m;
            if ( d ) m = Forge::build( *d, false );
            it = cpu.emplace( p.m_model, m ).first;
        }
        const ModelMesh &m = it->second;
        Matrix           xf = place( p.m_pos, p.m_rot * Deg, 0.0f, p.m_scale );
        std::pair<int, int> key = { (int)std::floor( p.m_pos.x / cell ), (int)std::floor( p.m_pos.y / cell ) };
        MeshBuilder &mb = cells[key];
        mb.m_noise = 0;
        for ( const Mesh *mesh : { &m.m_base, &m.m_head, &m.m_spin } )
            if ( mesh->vertexCount ) mb.append( *mesh, xf );
        if ( m.m_lamp )
            for ( const LightSpot &l : m.m_lights )
            {
                Vector3 w = Vector3Transform( l.m_pos, xf );
                m_lamps.push_back( { w, l.m_color, 16.0f, 2.2f } );
            }
    }
    for ( auto &[key, mb] : cells )
    {
        if ( mb.count() == 0 ) continue;
        Chunk c;
        c.m_mesh = mb.mesh( true );
        c.m_box = { key.first * cell - 20, key.second * cell - 20, cell + 40, cell + 40 };
        m_chunks.push_back( c );
    }
    for ( auto &[n, m] : cpu )
        for ( Mesh *mesh : { &m.m_base, &m.m_head, &m.m_spin } )
            if ( mesh->vertexCount )
            {
                MemFree( mesh->vertices );
                MemFree( mesh->normals );
                MemFree( mesh->colors );
            }
}

// ---------------------------------------------------------------------------------------------------- effects

void Renderer::spawn( const Particle &p )
{
    size_t cap = m_quality == 0 ? 1500 : 4000;
    if ( m_parts.size() >= cap ) m_parts.erase( m_parts.begin(), m_parts.begin() + (long)( cap / 10 ) );
    m_parts.push_back( p );
}

void Renderer::puff( Vector3 at, int n, float speed, float size, Color c0, Color c1, float life, int tex, bool add )
{
    for ( int k = 0; k < n; ++k )
    {
        Particle p;
        p.m_pos = at;
        float a = m_rng.range( 0, 2 * Pi ), up = m_rng.range( 0.2f, 1.0f );
        float sp = speed * m_rng.range( 0.3f, 1.0f );
        p.m_vel = { std::cos( a ) * sp, up * sp * 0.6f, std::sin( a ) * sp };
        p.m_size0 = size * m_rng.range( 0.6f, 1.2f );
        p.m_size1 = add ? size * 0.2f : size * 2.2f;
        p.m_c0 = c0;
        p.m_c1 = c1;
        p.m_life = life * m_rng.range( 0.6f, 1.2f );
        p.m_tex = tex;
        p.m_add = add;
        p.m_drag = add ? 2.5f : 1.5f;
        p.m_rise = add ? -6.0f : 1.5f;
        p.m_rot = m_rng.range( 0, 6.28f );
        p.m_spin = m_rng.range( -2, 2 );
        spawn( p );
    }
}

void Renderer::events( const World &w )
{
    for ( const Event &e : w.m_events )
    {
        Vector3 at = { e.m_pos.x, 0.8f, e.m_pos.y };
        switch ( e.m_type )
        {
        case EvE::SHOT:
        {
            const Car &c = w.m_cars[e.m_car];
            Vector3    m = { e.m_pos.x, 1.0f, e.m_pos.y };
            Particle   p;
            p.m_pos = m;
            p.m_vel = { c.m_vel.x, 0, c.m_vel.y };
            p.m_size0 = e.m_kind == 2 ? 1.6f : 0.9f;
            p.m_size1 = 0.2f;
            p.m_c0 = { 255, 220, 140, 255 };
            p.m_c1 = { 255, 120, 30, 0 };
            p.m_life = 0.06f;
            p.m_tex = T_FLARE;
            p.m_boost = 3.0f;
            spawn( p );
            m_glows.push_back( { m, { 255, 200, 120, 255 }, 9.0f, 1.6f, 0, 0.06f } );
            break;
        }
        case EvE::FLAME:
            m_glows.push_back( { at, { 255, 130, 40, 255 }, 12.0f, 2.0f, 0, 0.12f } );
            break;
        case EvE::HIT:
            puff( at, e.m_kind == 1 ? 2 : 5, 9.0f, 0.35f, { 255, 230, 160, 255 }, { 255, 100, 20, 0 }, 0.25f, T_SPARK, true );
            break;
        case EvE::RICOCHET:
            puff( { at.x, 0.5f, at.z }, 3, 7.0f, 0.3f, { 255, 220, 150, 255 }, { 255, 90, 20, 0 }, 0.2f, T_SPARK, true );
            puff( { at.x, 0.3f, at.z }, 1, 1.0f, 0.6f, { 120, 110, 100, 120 }, { 120, 110, 100, 0 }, 0.6f, T_SMOKE, false );
            break;
        case EvE::CRASH:
        case EvE::WALL:
        {
            int n = (int)clampf( e.m_power * 0.6f, 2, 14 );
            puff( at, n, 6.0f + e.m_power * 0.3f, 0.35f, { 255, 220, 140, 255 }, { 255, 80, 10, 0 }, 0.4f, T_SPARK, true );
            if ( e.m_power > 12 ) puff( at, 3, 2.0f, 1.2f, { 90, 86, 80, 150 }, { 90, 86, 80, 0 }, 1.2f, T_SMOKE, false );
            break;
        }
        case EvE::SCRAPE:
            puff( at, 2, 6.0f, 0.3f, { 255, 220, 140, 255 }, { 255, 90, 10, 0 }, 0.3f, T_SPARK, true );
            break;
        case EvE::EXPLODE:
        {
            float r = e.m_power;
            puff( { at.x, 1.0f, at.z }, (int)( 6 + r * 2 ), r * 2.2f, r * 0.45f, { 255, 240, 200, 255 }, { 255, 70, 10, 0 }, 0.5f, T_FLAME, true );
            puff( { at.x, 1.0f, at.z }, (int)( 10 + r * 2 ), r * 4.5f, 0.35f, { 255, 230, 160, 255 }, { 255, 80, 10, 0 }, 0.8f, T_SPARK, true );
            puff( { at.x, 1.5f, at.z }, (int)( 5 + r ), r * 0.9f, r * 0.5f, { 50, 46, 42, 220 }, { 70, 66, 60, 0 }, 2.5f, T_SMOKE, false );
            Particle ring;
            ring.m_pos = { at.x, 0.3f, at.z };
            ring.m_size0 = 0.5f;
            ring.m_size1 = r * 2.5f;
            ring.m_c0 = { 255, 200, 140, 200 };
            ring.m_c1 = { 255, 120, 40, 0 };
            ring.m_life = 0.35f;
            ring.m_tex = T_RING;
            spawn( ring );
            m_glows.push_back( { { at.x, 3.0f, at.z }, { 255, 150, 60, 255 }, r * 6.0f, 4.0f, 0, 0.6f } );
            Mark mk;
            mk.m_a = mk.m_b = e.m_pos;
            mk.m_w = r * 0.9f;
            mk.m_alpha = 0.7f;
            mk.m_color = { 6, 5, 4, 255 };
            m_marks[m_markNext] = mk;
            m_markNext = ( m_markNext + 1 ) % m_marks.size();
            break;
        }
        case EvE::WRECK:
            puff( { at.x, 1.2f, at.z }, 14, 8.0f, 1.0f, { 140, 130, 120, 255 }, { 60, 60, 60, 0 }, 0.8f, T_SPARK, true );
            break;
        case EvE::PICKUP:
        {
            Color c = e.m_text.rfind( "cash", 0 ) == 0 ? Color{ 120, 255, 120, 255 } : Color{ 255, 220, 120, 255 };
            puff( { at.x, 1.0f, at.z }, 10, 6.0f, 0.4f, c, { c.r, c.g, c.b, 0 }, 0.5f, T_SPARK, true );
            Particle ring;
            ring.m_pos = { at.x, 0.4f, at.z };
            ring.m_size0 = 0.5f;
            ring.m_size1 = 4.0f;
            ring.m_c0 = c;
            ring.m_c1 = { c.r, c.g, c.b, 0 };
            ring.m_life = 0.4f;
            ring.m_tex = T_RING;
            spawn( ring );
            break;
        }
        case EvE::NITRO:
            m_glows.push_back( { at, { 80, 140, 255, 255 }, 10.0f, 2.5f, 0, 0.3f } );
            break;
        case EvE::OIL:
            if ( e.m_kind == 0 ) puff( { at.x, 0.3f, at.z }, 6, 3.0f, 0.4f, { 20, 20, 20, 220 }, { 20, 20, 20, 0 }, 0.5f, T_SOFT, false );
            break;
        case EvE::TIRE_BURST:
            puff( at, 8, 5.0f, 0.3f, { 30, 30, 30, 255 }, { 30, 30, 30, 0 }, 0.6f, T_SOFT, false );
            puff( at, 3, 2.0f, 1.0f, { 200, 200, 200, 120 }, { 200, 200, 200, 0 }, 1.0f, T_SMOKE, false );
            break;
        case EvE::ROCKET:
        case EvE::MINE_DROP:
        case EvE::SMOKE:
            puff( { at.x, 0.6f, at.z }, 3, 1.5f, 0.8f, { 180, 176, 170, 160 }, { 180, 176, 170, 0 }, 1.0f, T_SMOKE, false );
            break;
        default: break;
        }
    }
}

void Renderer::update( const World &w, float dt )
{
    m_time += dt;
    for ( Particle &p : m_parts )
    {
        p.m_age += dt;
        p.m_vel = Vector3Scale( p.m_vel, std::max( 0.0f, 1.0f - p.m_drag * dt ) );
        p.m_vel.y += p.m_rise * dt;
        p.m_pos = Vector3Add( p.m_pos, Vector3Scale( p.m_vel, dt ) );
        if ( p.m_pos.y < 0.05f && p.m_add )
        {
            p.m_pos.y = 0.05f;
            p.m_vel.y = -p.m_vel.y * 0.3f;
        }
        p.m_rot += p.m_spin * dt;
    }
    m_parts.erase( std::remove_if( m_parts.begin(), m_parts.end(), []( const Particle &p ) { return p.m_age >= p.m_life; } ), m_parts.end() );
    for ( GlowLight &l : m_glows ) l.m_age += dt;
    m_glows.erase( std::remove_if( m_glows.begin(), m_glows.end(), []( const GlowLight &l ) { return l.m_age >= l.m_life; } ), m_glows.end() );
    if ( !m_track ) return;
    if ( m_lastWheel.size() != w.m_cars.size() * 2 ) m_lastWheel.assign( w.m_cars.size() * 2, { 1e9f, 1e9f } );
    if ( m_smokeTimer.size() != w.m_cars.size() ) m_smokeTimer.assign( w.m_cars.size(), 0.0f );
    for ( size_t i = 0; i < w.m_cars.size(); ++i )
    {
        const Car &c = w.m_cars[i];
        Vector2    f = c.fwd(), r = c.right();
        bool       loose = surfaceLoose( c.m_surface );
        float      sp = len( c.m_vel );
        // Skid marks and tracks in loose ground.
        for ( int k = 0; k < 2; ++k )
        {
            Vector2 wheel = add( c.m_pos, add( mul( f, -c.m_len * 0.32f ), mul( r, ( k ? 1.0f : -1.0f ) * c.m_wid * 0.4f ) ) );
            Vector2 &last = m_lastWheel[i * 2 + k];
            float    mark = c.m_wrecked ? 0.0f : std::max( c.m_skid, loose && sp > 3 ? 0.35f : 0.0f );
            if ( mark > 0.12f && last.x < 1e8f )
            {
                if ( dist2( last, wheel ) > 0.35f * 0.35f )
                {
                    Mark mk;
                    mk.m_a = last;
                    mk.m_b = wheel;
                    mk.m_w = 0.26f;
                    mk.m_alpha = clampf( mark, 0, 1 ) * ( loose ? 0.35f : 0.55f );
                    mk.m_color = loose ? ( c.m_surface == SurfaceE::SNOW ? Color{ 120, 130, 150, 255 } : Color{ 40, 30, 20, 255 } )
                                       : Color{ 8, 8, 8, 255 };
                    m_marks[m_markNext] = mk;
                    m_markNext = ( m_markNext + 1 ) % m_marks.size();
                    last = wheel;
                }
            }
            else
                last = mark > 0.12f ? wheel : Vector2{ 1e9f, 1e9f };
        }
        // Smoke from tyres, dust from loose ground, smoke and fire from damage.
        float &st = m_smokeTimer[i];
        st -= dt;
        if ( st > 0 ) continue;
        st = m_quality == 0 ? 0.07f : 0.04f;
        float hp = c.m_maxHp > 0 ? c.m_hp / c.m_maxHp : 1.0f;
        Vector3 rear = { c.m_pos.x - f.x * c.m_len * 0.4f, 0.4f, c.m_pos.y - f.y * c.m_len * 0.4f };
        if ( c.m_skid > 0.4f && !loose && sp > 5 )
            puff( rear, 1, 1.5f, 1.0f, { 200, 200, 200, 110 }, { 220, 220, 220, 0 }, 1.2f, T_SMOKE, false );
        if ( loose && sp > 8 )
        {
            Color d = c.m_surface == SurfaceE::SNOW ? Color{ 240, 244, 255, 130 } : m_venue ? m_venue->m_groundColor : Color{ 140, 120, 90, 130 };
            d.a = 120;
            puff( rear, 1, 2.0f, 1.2f, d, { d.r, d.g, d.b, 0 }, 1.0f, T_SMOKE, false );
        }
        Vector3 hood = { c.m_pos.x + f.x * c.m_len * 0.25f, 1.0f, c.m_pos.y + f.y * c.m_len * 0.25f };
        if ( c.m_wrecked )
        {
            if ( c.m_wreckTime < 25.0f )
            {
                puff( { c.m_pos.x, 1.0f, c.m_pos.y }, 1, 1.2f, 0.9f, { 255, 160, 60, 255 }, { 255, 60, 10, 0 }, 0.5f, T_FLAME, true );
                puff( { c.m_pos.x, 1.8f, c.m_pos.y }, 1, 1.0f, 1.4f, { 30, 28, 26, 200 }, { 40, 38, 36, 0 }, 2.5f, T_SMOKE, false );
            }
        }
        else if ( hp < 0.25f )
        {
            puff( hood, 1, 1.0f, 0.6f, { 255, 150, 50, 255 }, { 255, 60, 10, 0 }, 0.35f, T_FLAME, true );
            puff( hood, 1, 1.0f, 1.0f, { 30, 28, 26, 180 }, { 40, 38, 36, 0 }, 1.6f, T_SMOKE, false );
        }
        else if ( hp < 0.5f )
            puff( hood, 1, 1.0f, 0.8f, { 120, 120, 120, 140 }, { 140, 140, 140, 0 }, 1.4f, T_SMOKE, false );
        if ( c.m_nitroTime > 0 )
        {
            Particle p;
            p.m_pos = { rear.x - f.x * 0.6f, 0.5f, rear.z - f.y * 0.6f };
            p.m_vel = { -f.x * 6 + c.m_vel.x, 0, -f.y * 6 + c.m_vel.y };
            p.m_size0 = 0.8f;
            p.m_size1 = 0.2f;
            p.m_c0 = { 140, 190, 255, 255 };
            p.m_c1 = { 60, 80, 255, 0 };
            p.m_life = 0.15f;
            p.m_tex = T_FLAME;
            p.m_boost = 3.0f;
            spawn( p );
        }
        if ( c.m_scrape > 0.5f )
            puff( { c.m_pos.x, 0.6f, c.m_pos.y }, 2, 6.0f, 0.25f, { 255, 220, 140, 255 }, { 255, 90, 10, 0 }, 0.3f, T_SPARK, true );
    }
    // Flames from the flamethrowers.
    for ( const Bullet &b : w.m_bullets )
        if ( b.m_kind == 1 && m_rng.chance( 0.5f ) )
        {
            Particle p;
            p.m_pos = { b.m_pos.x, 0.9f, b.m_pos.y };
            p.m_vel = { b.m_vel.x * 0.3f, 1.0f, b.m_vel.y * 0.3f };
            p.m_size0 = 0.5f + b.m_age * 3.0f;
            p.m_size1 = 1.5f;
            p.m_c0 = { 255, 200, 90, 255 };
            p.m_c1 = { 255, 50, 10, 0 };
            p.m_life = 0.25f;
            p.m_tex = T_FLAME;
            p.m_boost = 2.5f;
            spawn( p );
        }
    // Rocket trails.
    for ( const Rocket &r : w.m_rockets )
    {
        Vector2 back = sub( r.m_pos, mul( headingDir( r.m_head ), 0.6f ) );
        puff( { back.x, 0.9f, back.y }, 1, 0.5f, 0.5f, { 200, 196, 190, 160 }, { 200, 196, 190, 0 }, 0.9f, T_SMOKE, false );
        puff( { back.x, 0.9f, back.y }, 1, 0.5f, 0.4f, { 255, 200, 100, 255 }, { 255, 80, 10, 0 }, 0.1f, T_FLARE, true );
    }
    // Smoke clouds.
    for ( const Hazard &h : w.m_hazards )
        if ( h.m_kind == Hazard::SMOKE && m_rng.chance( 0.25f ) )
        {
            float   a = m_rng.range( 0, 6.28f ), d = m_rng.range( 0, h.m_r * 0.8f );
            Vector3 p = { h.m_pos.x + std::cos( a ) * d, 0.8f, h.m_pos.y + std::sin( a ) * d };
            puff( p, 1, 0.6f, 2.5f, { 190, 190, 196, 200 }, { 200, 200, 206, 0 }, 3.0f, T_SMOKE, false );
        }
    // Weather.
    if ( m_venue )
    {
        const std::string &we = m_venue->m_weather;
        Vector2 f = m_view.m_focus;
        float   area = m_view.m_height * 0.9f;
        int     n = 0;
        if ( we == "rain" || we == "storm" ) n = m_quality == 0 ? 6 : 14;
        else if ( we == "snow" ) n = m_quality == 0 ? 3 : 6;
        else if ( we == "dust" || we == "fog" ) n = m_rng.chance( 0.3f ) ? 1 : 0;
        for ( int k = 0; k < n; ++k )
        {
            Particle p;
            p.m_pos = { f.x + m_rng.range( -area, area ), m_rng.range( 8, 20 ), f.y + m_rng.range( -area, area * 0.6f ) };
            if ( we == "rain" || we == "storm" )
            {
                p.m_vel = { 2.0f, -28.0f, 4.0f };
                p.m_size0 = p.m_size1 = 0.35f;
                p.m_c0 = p.m_c1 = { 170, 190, 220, 110 };
                p.m_life = p.m_pos.y / 28.0f;
                p.m_tex = T_DROP;
                p.m_add = true;
                p.m_boost = 0.8f;
            }
            else if ( we == "snow" )
            {
                p.m_vel = { m_rng.range( -1.5f, 1.5f ), -3.0f, m_rng.range( -1.0f, 1.0f ) };
                p.m_size0 = p.m_size1 = 0.18f;
                p.m_c0 = p.m_c1 = { 250, 252, 255, 230 };
                p.m_life = p.m_pos.y / 3.0f;
                p.m_tex = T_SOFT;
                p.m_add = false;
            }
            else
            {
                p.m_pos.y = m_rng.range( 0.5f, 3.0f );
                p.m_vel = { m_rng.range( 1.0f, 3.0f ), 0.1f, m_rng.range( -0.5f, 0.5f ) };
                p.m_size0 = 6.0f;
                p.m_size1 = 9.0f;
                Color fc = m_venue->m_fog;
                p.m_c0 = { fc.r, fc.g, fc.b, 0 };
                p.m_c1 = { fc.r, fc.g, fc.b, 0 };
                p.m_c0.a = 60;
                p.m_life = 5.0f;
                p.m_tex = T_SMOKE;
                p.m_add = false;
            }
            spawn( p );
        }
        if ( we == "storm" )
        {
            m_flashTimer -= dt;
            if ( m_flashTimer <= 0 )
            {
                m_flash = 1.0f;
                m_flashTimer = m_rng.range( 6.0f, 16.0f );
            }
        }
    }
    m_flash = std::max( 0.0f, m_flash - dt * 3.0f );
}

// ---------------------------------------------------------------------------------------------------- drawing

void Renderer::lights( const World &w )
{
    m_frameLights.clear();
    m_frameColors.clear();
    m_spotPos.clear();
    m_spotDir.clear();
    m_spotCol.clear();
    int  maxLights = m_quality == 0 ? 10 : m_quality == 1 ? 18 : MaxLights;
    auto light = [&]( Vector3 p, Color c, float r, float power ) {
        if ( (int)m_frameLights.size() >= maxLights ) return;
        m_frameLights.push_back( { p.x, p.y, p.z, r } );
        m_frameColors.push_back( { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, power } );
    };
    for ( const GlowLight &l : m_glows )
    {
        float f = 1.0f - l.m_age / l.m_life;
        light( l.m_pos, l.m_color, l.m_radius, l.m_power * f );
    }
    // Burning wrecks.
    for ( const Car &c : w.m_cars )
        if ( c.m_wrecked && c.m_wreckTime < 25.0f )
            light( { c.m_pos.x, 2.0f, c.m_pos.y }, { 255, 130, 40, 255 }, 14.0f, 1.6f + 0.5f * std::sin( m_time * 17 + c.m_pos.x ) );
    // At night the cars' own lights (beacons, light bars, candles) shine, blinking now and then.
    if ( m_dark )
        for ( const Car &c : w.m_cars )
        {
            if ( c.m_wrecked || dist2( c.m_pos, m_view.m_focus ) > 70.0f * 70.0f ) continue;
            const ModelMesh &m = g.m_forge.get( g_data, c.m_def->m_model, c.m_setup.m_paint );
            Matrix           xf = place( c.m_pos, c.m_head );
            int              n = 0;
            for ( const LightSpot &l : m.m_lights )
            {
                if ( n++ >= 2 ) break;
                float blink = 0.6f + 0.4f * std::sin( m_time * 9.0f + l.m_pos.x * 3.0f );
                light( Vector3Transform( l.m_pos, xf ), l.m_color, 7.0f, 1.3f * blink );
            }
        }
    // The lamps nearest to where the camera looks.
    if ( m_dark )
    {
        Vector2 f = m_view.m_focus;
        float   reach = m_view.m_height * 1.1f;
        std::vector<std::pair<float, int>> near;
        for ( size_t i = 0; i < m_lamps.size(); ++i )
        {
            float d = dist2( { m_lamps[i].m_pos.x, m_lamps[i].m_pos.z }, f );
            if ( d < reach * reach ) near.push_back( { d, (int)i } );
        }
        std::sort( near.begin(), near.end() );
        for ( auto &[d, i] : near ) light( m_lamps[i].m_pos, m_lamps[i].m_color, m_lamps[i].m_radius, m_lamps[i].m_power );
        // Headlights.
        for ( const Car &c : w.m_cars )
        {
            if ( c.m_wrecked || (int)m_spotPos.size() >= MaxSpots ) continue;
            Vector2 fw = c.fwd();
            Vector2 at = add( c.m_pos, mul( fw, c.m_len * 0.45f ) );
            Vector3 dir = Vector3Normalize( { fw.x, -0.22f, fw.y } );
            m_spotPos.push_back( { at.x, 1.0f, at.y, 48.0f } );
            m_spotDir.push_back( { dir.x, dir.y, dir.z, std::cos( 26.0f * Deg ) } );
            m_spotCol.push_back( { 1.0f, 0.93f, 0.8f, 3.2f } );
        }
    }
    // The pickups glow a little.
    for ( const Pickup &p : w.m_pickups )
        if ( p.m_respawn <= 0 && dist2( p.m_pos, m_view.m_focus ) < 2500 )
            light( { p.m_pos.x, 1.2f, p.m_pos.y }, p.m_kind == "cash" ? Color{ 90, 255, 120, 255 } : Color{ 255, 200, 90, 255 }, 5.0f, 0.8f );
}

void Renderer::setLookUniforms()
{
    const VenueDef *v = m_venue;
    Color sun = v ? v->m_sunColor : Color{ 255, 244, 220, 255 };
    float sunP = v ? v->m_sunPower : 1.0f;
    Color amb = v ? v->m_ambient : Color{ 90, 100, 120, 255 };
    float ambP = v ? v->m_ambientPower : 0.6f;
    Color fog = v ? v->m_fog : Color{ 120, 130, 140, 255 };
    float fogD = v ? v->m_fogDensity : 0.004f;
    for ( Shader s : { g.m_lit, g.m_surface } )
    {
        setVec3( s, "sunDir", m_sunDir );
        setVec3( s, "sunColor", v3( sun, sunP * 1.6f ) );
        setVec3( s, "ambient", v3( amb, ambP * 1.3f ) );
        setVec3( s, "camPos", m_cam.position );
        setVec3( s, "fogColor", v3( fog ) );
        setFloat( s, "fogDensity", fogD );
        setVec2( s, "focus", m_view.m_focus );
        int lc = (int)m_frameLights.size(), sc = (int)m_spotPos.size();
        setInt( s, "lightCount", lc );
        if ( lc )
        {
            SetShaderValueV( s, GetShaderLocation( s, "lightPos" ), m_frameLights.data(), SHADER_UNIFORM_VEC4, lc );
            SetShaderValueV( s, GetShaderLocation( s, "lightCol" ), m_frameColors.data(), SHADER_UNIFORM_VEC4, lc );
        }
        setInt( s, "spotCount", sc );
        if ( sc )
        {
            SetShaderValueV( s, GetShaderLocation( s, "spotPos" ), m_spotPos.data(), SHADER_UNIFORM_VEC4, sc );
            SetShaderValueV( s, GetShaderLocation( s, "spotDir" ), m_spotDir.data(), SHADER_UNIFORM_VEC4, sc );
            SetShaderValueV( s, GetShaderLocation( s, "spotCol" ), m_spotCol.data(), SHADER_UNIFORM_VEC4, sc );
        }
        SetShaderValueMatrix( s, GetShaderLocation( s, "lightVP" ), m_lightVP );
        setFloat( s, "shadowOn", 1.0f );
        setFloat( s, "flash", m_flash );
        int slot = 10;
        SetShaderValue( s, GetShaderLocation( s, "shadowMap" ), &slot, SHADER_UNIFORM_INT );
    }
    setVec3( g.m_surface, "lineColor", v3( v ? v->m_lineColor : Color{ 210, 210, 190, 255 } ) );
    setVec3( g.m_surface, "groundA", v3( v ? v->m_groundColor : Color{ 60, 80, 50, 255 } ) );
    setVec3( g.m_surface, "groundB", v3( v ? v->m_groundColor2 : Color{ 80, 70, 50, 255 } ) );
    setFloat( g.m_surface, "wet", m_wet );
    setFloat( g.m_surface, "time", m_time );
    setVec4( g.m_lit, "tint", { 1, 1, 1, 0 } );
    setFloat( g.m_lit, "emitBoost", m_dark ? 3.0f : 2.0f );
    setFloat( g.m_lit, "specular", 0.25f );
}

void Renderer::shadowPass( const World &w )
{
    Vector3  focus = { m_view.m_focus.x, 0, m_view.m_focus.y };
    float    R = m_view.m_height * 0.95f;
    Camera3D lc = {};
    lc.position = Vector3Subtract( focus, Vector3Scale( m_sunDir, 150.0f ) );
    lc.target = focus;
    lc.up = std::fabs( m_sunDir.y ) > 0.98f ? Vector3{ 0, 0, -1 } : Vector3{ 0, 1, 0 };
    lc.fovy = 2 * R;
    lc.projection = CAMERA_ORTHOGRAPHIC;
    BeginTextureMode( m_shadow );
    rlViewport( 0, 0, m_shadowSize, m_shadowSize );
    ClearBackground( WHITE );
    BeginMode3D( lc );
    rlSetClipPlanes( 1.0, 400.0 );
    m_lightVP = MatrixMultiply( rlGetMatrixModelview(), rlGetMatrixProjection() );
    rlDisableBackfaceCulling();
    drawScene( w, true );
    EndMode3D();
    EndTextureMode();
    rlSetClipPlanes( RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR );
}

void Renderer::drawCar( const World &w, int i, bool shadow )
{
    const Car       &c = w.m_cars[i];
    const ModelMesh &m = g.m_forge.get( g_data, c.m_def->m_model, c.m_setup.m_paint );
    Matrix base = MatrixMultiply( MatrixMultiply( MatrixRotateZ( c.m_lean * 0.07f ), MatrixRotateX( c.m_pitch * 0.04f ) ),
                                  place( c.m_pos, c.m_head ) );
    Material &mat = shadow ? g.m_depthMat : g.m_litMat;
    float     hp = c.m_maxHp > 0 ? clampf( c.m_hp / c.m_maxHp, 0, 1 ) : 1.0f;
    if ( !shadow )
    {
        float k = c.m_wrecked ? 0.22f : 0.55f + 0.45f * std::sqrt( hp );
        setVec4( g.m_lit, "tint", { k, k * ( c.m_wrecked ? 0.9f : 1.0f ), k * ( c.m_wrecked ? 0.85f : 1.0f ), c.m_hitFlash * 0.35f } );
        setFloat( g.m_lit, "specular", c.m_wrecked ? 0.05f : 0.6f );
    }
    if ( m.m_hasBase ) DrawMesh( m.m_base, mat, base );
    if ( m.m_hasHead ) DrawMesh( m.m_head, mat, base );
    if ( m.m_hasSpin ) DrawMesh( m.m_spin, mat, base );
    const Mesh &wheel = g.m_forge.wheel();
    for ( const WheelSpot &ws : m.m_wheels )
    {
        float  steer = ws.m_front ? -c.m_steerVis * 0.45f : 0.0f;
        Matrix wm = MatrixMultiply( MatrixScale( ws.m_w, ws.m_r, ws.m_r ), MatrixRotateX( -c.m_wheelTurn ) );
        wm = MatrixMultiply( wm, MatrixRotateY( steer ) );
        wm = MatrixMultiply( wm, MatrixTranslate( ws.m_pos.x, ws.m_pos.y, ws.m_pos.z ) );
        if ( !shadow )
        {
            float k = c.m_wrecked ? 0.3f : 1.0f;
            setVec4( g.m_lit, "tint", { ws.m_hub.r / 255.0f * k, ws.m_hub.g / 255.0f * k, ws.m_hub.b / 255.0f * k, 0 } );
        }
        DrawMesh( wheel, mat, MatrixMultiply( wm, base ) );
    }
    // The gun on the bonnet, the rocket pod on the roof, the mine rack at the back.
    if ( !shadow ) setVec4( g.m_lit, "tint", { 1, 1, 1, 0 } );
    std::string gunModel = c.m_gun->m_kind == "flame" ? "_gun_flamer" : c.m_gun->m_kind == "shell" ? "_gun_cannon"
                         : c.m_gun->m_name.find( "twin" ) != std::string::npos ? "_gun_twinmg" : "_gun_mg";
    auto attach = [&]( const std::string &model, Vector3 at ) {
        const ModelMesh &gm = modelOf( model );
        Matrix           xf = MatrixMultiply( MatrixTranslate( at.x, at.y, at.z ), base );
        if ( gm.m_hasBase ) DrawMesh( gm.m_base, mat, xf );
    };
    attach( gunModel, c.m_def->m_mountFront );
    if ( c.m_rockets > 0 ) attach( "_pod", c.m_def->m_mountRoof );
    int rear = 0;
    for ( int r = 0; r < (int)RearE::COUNT; ++r ) rear += c.m_rear[r];
    if ( rear > 0 ) attach( "_rack", c.m_def->m_mountRear );
}

void Renderer::drawScene( const World &w, bool shadow )
{
    Material &lit = shadow ? g.m_depthMat : g.m_litMat;
    if ( !shadow )
    {
        DrawMesh( m_ground, g.m_surfMat, MatrixIdentity() );
        DrawMesh( m_road, g.m_surfMat, MatrixIdentity() );
        drawDecals( w );
    }
    if ( m_walls.vertexCount ) DrawMesh( m_walls, lit, MatrixIdentity() );
    // Props: only the chunks near the view.
    Vector2 f = m_view.m_focus;
    float   reach = m_view.m_height * 1.3f + 40.0f;
    for ( const Chunk &c : m_chunks )
    {
        float cx = clampf( f.x, c.m_box.x, c.m_box.x + c.m_box.width ), cz = clampf( f.y, c.m_box.y, c.m_box.y + c.m_box.height );
        if ( dist2( { cx, cz }, f ) > reach * reach ) continue;
        DrawMesh( c.m_mesh, lit, MatrixIdentity() );
    }
    // Obstacles, barrels, pickups, mines, spikes.
    for ( const ObstacleSpot &o : m_track->m_obstacles )
        if ( !o.m_model.empty() )
        {
            const ModelMesh &m = modelOf( o.m_model );
            if ( m.m_hasBase ) DrawMesh( m.m_base, lit, place( o.m_pos, o.m_rot * Deg ) );
        }
    for ( const Barrel &b : w.m_barrels )
        if ( !b.m_gone )
        {
            const ModelMesh &m = modelOf( g_data.model( "barrel_explosive" ) ? "barrel_explosive" : "_barrel" );
            if ( m.m_hasBase ) DrawMesh( m.m_base, lit, place( b.m_pos, 0 ) );
        }
    for ( const Pickup &p : w.m_pickups )
    {
        if ( p.m_respawn > 0 ) continue;
        std::string name = "pickup_" + p.m_kind;
        if ( !g_data.model( name ) ) name = p.m_kind == "mystery" ? "_coffin" : "_pickup";
        const ModelMesh &m = modelOf( name );
        float bob = 0.35f + 0.25f * std::sin( p.m_spin * 3.0f );
        if ( !shadow ) setVec4( g.m_lit, "tint", { 1, 1, 1, 0.12f + 0.1f * std::sin( p.m_spin * 6.0f ) } );
        if ( m.m_hasBase ) DrawMesh( m.m_base, lit, place( p.m_pos, p.m_spin * 1.6f, bob ) );
    }
    if ( !shadow ) setVec4( g.m_lit, "tint", { 1, 1, 1, 0 } );
    for ( const Hazard &h : w.m_hazards )
    {
        if ( h.m_kind == Hazard::MINE )
        {
            const ModelMesh &m = modelOf( g_data.model( "mine_live" ) ? "mine_live" : "_mine" );
            if ( !shadow )
            {
                bool  armed = h.m_age >= h.m_arm;
                float blink = armed && std::fmod( m_time + h.m_pos.x, 0.8f ) < 0.15f ? 0.6f : 0.0f;
                setVec4( g.m_lit, "tint", { 1, 1, 1, blink } );
            }
            if ( m.m_hasBase ) DrawMesh( m.m_base, lit, place( h.m_pos, h.m_pos.x ) );
        }
        else if ( h.m_kind == Hazard::SPIKES )
        {
            const ModelMesh &m = modelOf( "_spikes" );
            if ( m.m_hasBase ) DrawMesh( m.m_base, lit, place( h.m_pos, headingOf( h.m_dir ) + Pi * 0.5f ) );
        }
    }
    if ( !shadow ) setVec4( g.m_lit, "tint", { 1, 1, 1, 0 } );
    for ( const Rocket &r : w.m_rockets )
    {
        const ModelMesh &m = modelOf( "_rocket" );
        if ( m.m_hasBase ) DrawMesh( m.m_base, lit, place( r.m_pos, r.m_head, 0.9f ) );
    }
    for ( size_t i = 0; i < w.m_cars.size(); ++i ) drawCar( w, (int)i, shadow );
    if ( !shadow && m_ghost.m_on )
    {
        const ModelMesh &m = g.m_forge.get( g_data, m_ghost.m_model, m_ghost.m_paint );
        setVec4( g.m_lit, "tint", { 0.5f, 0.7f, 1.0f, 0.35f + 0.1f * std::sin( m_time * 5.0f ) } );
        if ( m.m_hasBase ) DrawMesh( m.m_base, g.m_litMat, place( m_ghost.m_pos, m_ghost.m_head, 0.05f ) );
    }
    if ( !shadow ) setVec4( g.m_lit, "tint", { 1, 1, 1, 0 } );
}

void Renderer::drawDecals( const World &w )
{
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode( BLEND_ALPHA );
    // Skid marks and scorches: flat quads a hair above the road.
    rlSetTexture( g.m_tex[T_SOFT].id );
    rlBegin( RL_QUADS );
    for ( const Mark &mk : m_marks )
    {
        if ( mk.m_alpha <= 0 ) continue;
        Color c = mk.m_color;
        c.a = (unsigned char)( mk.m_alpha * 255 );
        rlColor4ub( c.r, c.g, c.b, c.a );
        if ( mk.m_a.x == mk.m_b.x && mk.m_a.y == mk.m_b.y )
        {
            float r = mk.m_w;
            rlTexCoord2f( 0, 0 ); rlVertex3f( mk.m_a.x - r, 0.06f, mk.m_a.y - r );
            rlTexCoord2f( 0, 1 ); rlVertex3f( mk.m_a.x - r, 0.06f, mk.m_a.y + r );
            rlTexCoord2f( 1, 1 ); rlVertex3f( mk.m_a.x + r, 0.06f, mk.m_a.y + r );
            rlTexCoord2f( 1, 0 ); rlVertex3f( mk.m_a.x + r, 0.06f, mk.m_a.y - r );
            continue;
        }
        Vector2 d = norm( sub( mk.m_b, mk.m_a ) ), s = mul( perp( d ), mk.m_w * 0.5f );
        // The soft texture's middle row: a line that fades only at its sides.
        rlTexCoord2f( 0.0f, 0.5f ); rlVertex3f( mk.m_a.x - s.x, 0.055f, mk.m_a.y - s.y );
        rlTexCoord2f( 1.0f, 0.5f ); rlVertex3f( mk.m_a.x + s.x, 0.055f, mk.m_a.y + s.y );
        rlTexCoord2f( 1.0f, 0.5f ); rlVertex3f( mk.m_b.x + s.x, 0.055f, mk.m_b.y + s.y );
        rlTexCoord2f( 0.0f, 0.5f ); rlVertex3f( mk.m_b.x - s.x, 0.055f, mk.m_b.y - s.y );
    }
    // Oil slicks.
    for ( const Hazard &h : w.m_hazards )
    {
        if ( h.m_kind != Hazard::OIL ) continue;
        float r = h.m_r * std::min( 1.0f, 0.3f + h.m_age * 3.0f ) * 1.3f;
        float a = h.m_life - h.m_age < 3.0f ? ( h.m_life - h.m_age ) / 3.0f : 1.0f;
        rlColor4ub( 4, 4, 6, (unsigned char)( 235 * a ) );
        rlTexCoord2f( 0, 0 ); rlVertex3f( h.m_pos.x - r, 0.065f, h.m_pos.y - r );
        rlTexCoord2f( 0, 1 ); rlVertex3f( h.m_pos.x - r, 0.065f, h.m_pos.y + r );
        rlTexCoord2f( 1, 1 ); rlVertex3f( h.m_pos.x + r, 0.065f, h.m_pos.y + r );
        rlTexCoord2f( 1, 0 ); rlVertex3f( h.m_pos.x + r, 0.065f, h.m_pos.y - r );
    }
    rlEnd();
    rlSetTexture( 0 );
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
}

void Renderer::drawParticles()
{
    if ( m_parts.empty() ) return;
    Matrix  view = GetCameraMatrix( m_cam );
    Vector3 right = { view.m0, view.m4, view.m8 }, up = { view.m1, view.m5, view.m9 };
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    for ( int pass = 0; pass < 2; ++pass )
    {
        bool additive = pass == 1;
        BeginBlendMode( additive ? BLEND_ADDITIVE : BLEND_ALPHA );
        BeginShaderMode( g.m_sprite );
        for ( int tex = 0; tex < T_COUNT; ++tex )
        {
            bool any = false;
            for ( const Particle &p : m_parts )
                if ( p.m_add == additive && p.m_tex == tex )
                {
                    any = true;
                    break;
                }
            if ( !any ) continue;
            float boost = additive ? 1.6f : 1.0f;
            setFloat( g.m_sprite, "boost", boost );
            rlSetTexture( g.m_tex[tex].id );
            rlBegin( RL_QUADS );
            for ( const Particle &p : m_parts )
            {
                if ( p.m_add != additive || p.m_tex != tex ) continue;
                float t = p.m_age / p.m_life;
                float s = lerpf( p.m_size0, p.m_size1, t );
                Color c = mixColor( p.m_c0, p.m_c1, t );
                float k = additive ? p.m_boost / 1.6f : 1.0f;
                rlColor4ub( (unsigned char)clampf( c.r * k, 0, 255 ), (unsigned char)clampf( c.g * k, 0, 255 ),
                            (unsigned char)clampf( c.b * k, 0, 255 ), c.a );
                Vector3 r2, u2;
                if ( tex == T_DROP )
                {
                    r2 = Vector3Scale( right, s * 0.15f );
                    u2 = Vector3Scale( Vector3Normalize( p.m_vel ), s * 4.0f );
                }
                else if ( tex == T_RING )
                {
                    r2 = { s, 0, 0 };
                    u2 = { 0, 0, s };
                }
                else
                {
                    float cs = std::cos( p.m_rot ), sn = std::sin( p.m_rot );
                    r2 = Vector3Add( Vector3Scale( right, cs * s ), Vector3Scale( up, sn * s ) );
                    u2 = Vector3Add( Vector3Scale( right, -sn * s ), Vector3Scale( up, cs * s ) );
                }
                Vector3 a = Vector3Subtract( Vector3Subtract( p.m_pos, r2 ), u2 );
                Vector3 b = Vector3Add( Vector3Subtract( p.m_pos, u2 ), r2 );
                Vector3 c2 = Vector3Add( Vector3Add( p.m_pos, r2 ), u2 );
                Vector3 d = Vector3Subtract( Vector3Add( p.m_pos, u2 ), r2 );
                rlTexCoord2f( 0, 1 ); rlVertex3f( a.x, a.y, a.z );
                rlTexCoord2f( 1, 1 ); rlVertex3f( b.x, b.y, b.z );
                rlTexCoord2f( 1, 0 ); rlVertex3f( c2.x, c2.y, c2.z );
                rlTexCoord2f( 0, 0 ); rlVertex3f( d.x, d.y, d.z );
            }
            rlEnd();
            rlDrawRenderBatchActive();
        }
        rlSetTexture( 0 );
        EndShaderMode();
        EndBlendMode();
    }
    rlEnableDepthMask();
}

void Renderer::drawBullets( const World &w )
{
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode( BLEND_ADDITIVE );
    BeginShaderMode( g.m_sprite );
    setFloat( g.m_sprite, "boost", 2.5f );
    rlSetTexture( g.m_tex[T_STREAK].id );
    rlBegin( RL_QUADS );
    for ( const Bullet &b : w.m_bullets )
    {
        if ( b.m_kind == 1 ) continue;
        Vector2 d = norm( b.m_vel ), s = mul( perp( d ), b.m_kind == 2 ? 0.25f : 0.12f );
        float   l = b.m_kind == 2 ? 1.8f : 2.4f;
        Vector2 a = sub( b.m_pos, mul( d, l ) ), e = b.m_pos;
        rlColor4ub( b.m_color.r, b.m_color.g, b.m_color.b, 255 );
        rlTexCoord2f( 0, 0 ); rlVertex3f( a.x - s.x, 0.95f, a.y - s.y );
        rlTexCoord2f( 0, 1 ); rlVertex3f( a.x + s.x, 0.95f, a.y + s.y );
        rlTexCoord2f( 1, 1 ); rlVertex3f( e.x + s.x, 0.95f, e.y + s.y );
        rlTexCoord2f( 1, 0 ); rlVertex3f( e.x - s.x, 0.95f, e.y - s.y );
    }
    // Brake lights and the headlights' glow seen from above.
    for ( const Car &c : w.m_cars )
    {
        if ( c.m_wrecked ) continue;
        Vector2 f = c.fwd(), r = c.right();
        for ( float sd : { -1.0f, 1.0f } )
        {
            if ( c.m_brakeLight > 0 )
            {
                Vector2 p = add( c.m_pos, add( mul( f, -c.m_len * 0.5f ), mul( r, sd * c.m_wid * 0.33f ) ) );
                rlColor4ub( 255, 40, 20, 255 );
                rlTexCoord2f( 0, 0 ); rlVertex3f( p.x - 0.35f, 0.8f, p.y );
                rlTexCoord2f( 0, 1 ); rlVertex3f( p.x, 0.8f, p.y + 0.35f );
                rlTexCoord2f( 1, 1 ); rlVertex3f( p.x + 0.35f, 0.8f, p.y );
                rlTexCoord2f( 1, 0 ); rlVertex3f( p.x, 0.8f, p.y - 0.35f );
            }
            if ( m_dark )
            {
                Vector2 p = add( c.m_pos, add( mul( f, c.m_len * 0.5f ), mul( r, sd * c.m_wid * 0.33f ) ) );
                rlColor4ub( 255, 240, 200, 200 );
                rlTexCoord2f( 0, 0 ); rlVertex3f( p.x - 0.4f, 0.8f, p.y );
                rlTexCoord2f( 0, 1 ); rlVertex3f( p.x, 0.8f, p.y + 0.4f );
                rlTexCoord2f( 1, 1 ); rlVertex3f( p.x + 0.4f, 0.8f, p.y );
                rlTexCoord2f( 1, 0 ); rlVertex3f( p.x, 0.8f, p.y - 0.4f );
            }
        }
    }
    rlEnd();
    rlSetTexture( 0 );
    rlDrawRenderBatchActive();
    EndShaderMode();
    EndBlendMode();
    rlEnableDepthMask();
}

void Renderer::render( const World &w, const View &v )
{
    m_view = v;
    float h = v.m_height;
    float shake = v.m_shake;
    Vector3 jitter = { ( noise2( m_time * 20, 1 ) - 0.5f ) * shake * 1.6f, 0, ( noise2( 3, m_time * 20 ) - 0.5f ) * shake * 1.6f };
    m_cam.target = Vector3Add( { v.m_focus.x, 0, v.m_focus.y - h * 0.06f }, jitter );
    m_cam.position = Vector3Add( { v.m_focus.x, h, v.m_focus.y + h * 0.36f }, jitter );
    m_cam.up = { 0, 1, 0 };
    m_cam.fovy = 45.0f;
    lights( w );
    if ( m_track ) shadowPass( w );
    BeginTextureMode( m_hdr );
    Color fog = m_venue ? m_venue->m_fog : Color{ 30, 30, 36, 255 };
    ClearBackground( fog );
    BeginMode3D( m_cam );
    rlSetClipPlanes( 0.5, 900.0 );
    setLookUniforms();
    rlActiveTextureSlot( 10 );
    rlEnableTexture( m_shadow.depth.id );
    rlActiveTextureSlot( 0 );
    rlDisableBackfaceCulling();
    if ( m_track ) drawScene( w, false );
    drawBullets( w );
    drawParticles();
    rlEnableBackfaceCulling();
    EndMode3D();
    rlSetClipPlanes( RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR );
    EndTextureMode();
    post();
}

void Renderer::showroom( const std::string &model, const Paint &paint, float yaw, float dist )
{
    m_view.m_focus = { 0, 0 };
    m_view.m_height = dist;
    m_frameLights.clear();
    m_frameColors.clear();
    m_spotPos.clear();
    m_spotDir.clear();
    m_spotCol.clear();
    m_frameLights.push_back( { -4, 3, 4, 14 } );
    m_frameColors.push_back( { 0.6f, 0.7f, 1.0f, 1.2f } );
    m_frameLights.push_back( { 4, 2, -3, 12 } );
    m_frameColors.push_back( { 1.0f, 0.6f, 0.4f, 1.0f } );
    const VenueDef *keep = m_venue;
    m_venue = nullptr;
    m_dark = false;
    m_sunDir = Vector3Normalize( { -0.4f, -0.8f, -0.45f } );
    m_cam.target = { 0, 0.5f, 0 };
    m_cam.position = { dist * 0.55f, dist * 0.55f, dist * 0.8f };
    m_cam.fovy = 35.0f;
    BeginTextureMode( m_hdr );
    ClearBackground( { 12, 12, 16, 255 } );
    BeginMode3D( m_cam );
    setLookUniforms();
    for ( Shader s : { g.m_lit, g.m_surface } )
    {
        setFloat( s, "shadowOn", 0.0f );
        setFloat( s, "fogDensity", 0.0f );
        setVec3( s, "ambient", { 0.35f, 0.36f, 0.42f } );
        setVec3( s, "sunColor", { 1.3f, 1.25f, 1.15f } );
    }
    setFloat( g.m_lit, "specular", 0.6f );
    rlDisableBackfaceCulling();
    const ModelMesh &m = modelOf( model, paint );
    Matrix           xf = MatrixRotateY( yaw );
    if ( m.m_hasBase ) DrawMesh( m.m_base, g.m_litMat, xf );
    if ( m.m_hasHead ) DrawMesh( m.m_head, g.m_litMat, xf );
    if ( m.m_hasSpin ) DrawMesh( m.m_spin, g.m_litMat, xf );
    for ( const WheelSpot &ws : m.m_wheels )
    {
        Matrix wm = MatrixMultiply( MatrixScale( ws.m_w, ws.m_r, ws.m_r ), MatrixTranslate( ws.m_pos.x, ws.m_pos.y, ws.m_pos.z ) );
        setVec4( g.m_lit, "tint", { ws.m_hub.r / 255.0f, ws.m_hub.g / 255.0f, ws.m_hub.b / 255.0f, 0 } );
        DrawMesh( g.m_forge.wheel(), g.m_litMat, MatrixMultiply( wm, xf ) );
    }
    setVec4( g.m_lit, "tint", { 1, 1, 1, 0 } );
    // A floor that fades out.
    rlDrawRenderBatchActive();
    BeginBlendMode( BLEND_ALPHA );
    rlSetTexture( g.m_tex[T_SOFT].id );
    rlBegin( RL_QUADS );
    rlColor4ub( 50, 50, 60, 255 );
    float r = std::max( 3.0f, m.m_radius * 1.6f );
    rlTexCoord2f( 0, 0 ); rlVertex3f( -r, 0.0f, -r );
    rlTexCoord2f( 0, 1 ); rlVertex3f( -r, 0.0f, r );
    rlTexCoord2f( 1, 1 ); rlVertex3f( r, 0.0f, r );
    rlTexCoord2f( 1, 0 ); rlVertex3f( r, 0.0f, -r );
    rlEnd();
    rlSetTexture( 0 );
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableBackfaceCulling();
    EndMode3D();
    EndTextureMode();
    m_venue = keep;
    post();
}

void Renderer::post()
{
    Texture2D src = m_hdr.texture;
    float     threshold = 1.0f;
    Vector2   texel = { 1.0f / src.width, 1.0f / src.height };
    BeginTextureMode( m_bloom[0] );
    setVec2( g.m_prefilter, "texel", texel );
    setFloat( g.m_prefilter, "threshold", threshold );
    fullscreen( g.m_prefilter, src, m_bloom[0].texture.width, m_bloom[0].texture.height );
    EndTextureMode();
    for ( size_t i = 1; i < m_bloom.size(); ++i )
    {
        Texture2D from = m_bloom[i - 1].texture;
        BeginTextureMode( m_bloom[i] );
        setVec2( g.m_down, "texel", { 1.0f / from.width, 1.0f / from.height } );
        fullscreen( g.m_down, from, m_bloom[i].texture.width, m_bloom[i].texture.height );
        EndTextureMode();
    }
    for ( size_t i = m_bloom.size() - 1; i > 0; --i )
    {
        Texture2D from = m_bloom[i].texture;
        BeginTextureMode( m_bloom[i - 1] );
        BeginBlendMode( BLEND_ADDITIVE );
        setVec2( g.m_up, "texel", { 1.0f / from.width, 1.0f / from.height } );
        fullscreen( g.m_up, from, m_bloom[i - 1].texture.width, m_bloom[i - 1].texture.height );
        EndBlendMode();
        EndTextureMode();
    }
    BeginTextureMode( m_out );
    Shader &f = g.m_final;
    setFloat( f, "exposure", 1.0f + m_flash * 0.8f );
    setFloat( f, "bloom", 0.5f );
    setFloat( f, "hurt", m_hurt );
    setFloat( f, "fade", m_fade );
    setFloat( f, "grade", 0.6f );
    BeginShaderMode( f );
    SetShaderValueTexture( f, GetShaderLocation( f, "bloomTex" ), m_bloom[0].texture );
    DrawTexturePro( src, { 0, 0, (float)src.width, (float)-src.height }, { 0, 0, (float)m_w, (float)m_h }, { 0, 0 }, 0, WHITE );
    EndShaderMode();
    EndTextureMode();
}

Vector2 Renderer::project( Vector3 p ) const { return GetWorldToScreenEx( p, m_cam, m_w, m_h ); }

}   // namespace grave
