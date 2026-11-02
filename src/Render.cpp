/**
 * @file Render.cpp
 * @brief The renderer: shaders, the static scene, cars, effects, lights, shadows and the picture's last steps.
 * @author Rene Weller
 */
#include "Render.h"

#include "Ui.h"

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
uniform float dent;        // 0 whole .. 1 a wreck: dented panels and soot
uniform vec4 dentFrame;    // the car's x, z and the cosine and sine of its turn (the dents stay on the car)
out vec4 finalColor;
vec3 hash33( vec3 p )
{
    p = fract( p * vec3( 0.1031, 0.1030, 0.0973 ) );
    p += dot( p, p.yxz + 33.33 );
    return fract( ( p.xxy + p.yxx ) * p.zyx );
}
void main()
{
    vec3 n = normalize( fragNormal );
    vec3 albedo = fragColor.rgb * tint.rgb;
    if ( dent > 0.0 && fragColor.a < 0.5 )
    {
        vec2 d2 = fragPos.xz - dentFrame.xy;
        vec3 lp = vec3( d2.x * dentFrame.z + d2.y * dentFrame.w, fragPos.y, -d2.x * dentFrame.w + d2.y * dentFrame.z );
        vec3 h = hash33( floor( lp * 2.6 ) + 3.1 );
        float amt = dent * step( 1.0 - dent * 0.85, h.x );
        n = normalize( n + ( h - 0.5 ) * amt * 1.3 );
        float soot = smoothstep( 0.45, 0.9, hash33( floor( lp * 1.4 ) + 7.7 ).y ) * dent;
        albedo *= 1.0 - soot * 0.65;
    }
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
uniform float landStyle;   // the venue's ground: 1 junk, 2 desert, 3 cemetery, 4 docks, 5 snow, 6 swamp, 7 refinery, 8 city, 9 fair, 10 proving, 11 estate
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
        // Its own life, by venue (landStyle): tyre tracks wandering off, specks of stone, worn patches, and what each
        // place leaves on the ground.
        int   st = int( landStyle + 0.5 );
        float speck = hash( floor( w * 3.0 ) );
        col *= 1.0 - 0.18 * step( 0.965, speck ) + 0.12 * step( speck, 0.02 );
        float ruts = abs( fbm( w * 0.021 + 17.0 ) - 0.5 );
        float rut = smoothstep( 0.005, 0.0, abs( ruts - 0.013 ) ) + smoothstep( 0.005, 0.0, abs( ruts - 0.026 ) );
        float rutsOn = smoothstep( 0.45, 0.6, noise( w * 0.012 + 3.0 ) );
        col *= 1.0 - 0.2 * rut * rutsOn * ( st == 2 || st == 4 || st == 7 || st == 8 ? 0.4 : 1.0 );
        if ( st == 1 )   // junkyard: oil pools with a sheen, bits of metal glinting
        {
            float oil = smoothstep( 0.66, 0.72, fbm( w * 0.07 + 31.0 ) );
            col = mix( col, vec3( 0.03, 0.025, 0.02 ) + 0.05 * vec3( sin( w.x * 2.0 ), sin( w.y * 2.3 + 1.0 ), sin( w.x * 1.7 + 2.0 ) ), oil * 0.9 );
            spec = max( spec, oil * 0.6 );
            rough = mix( rough, 90.0, oil );
            col += vec3( 0.25 ) * step( 0.992, hash( floor( w * 5.0 ) + 7.0 ) );
        }
        else if ( st == 2 )   // desert: cracked hardpan in the hollows
        {
            float pan = smoothstep( 0.55, 0.65, fbm( w * 0.03 + 5.0 ) );
            vec2  c = cells( w * 0.55 );
            col = mix( col, col * vec3( 1.08, 1.0, 0.9 ), pan );
            col *= 1.0 - 0.35 * pan * smoothstep( 0.06, 0.0, c.y - c.x );
        }
        else if ( st == 3 || st == 11 )   // cemetery, estate: gravel paths between the graves; the estate's lawn mown in stripes
        {
            float path = smoothstep( 0.012, 0.004, abs( fbm( w * 0.016 + 9.0 ) - 0.5 ) );
            vec3  gravel = vec3( 0.42, 0.4, 0.37 ) * ( 0.8 + 0.4 * hash( floor( w * 8.0 ) ) );
            col = mix( col, gravel, path * 0.85 );
            if ( st == 11 ) col *= 0.93 + 0.1 * step( 0.5, fract( ( w.x + w.y ) * 0.125 ) ) * ( 1.0 - path );
        }
        else if ( st == 4 || st == 7 )   // docks, refinery: concrete slabs, oil stains, faded yellow lines
        {
            vec2  g = abs( fract( w / 4.0 ) - 0.5 );
            float joint = smoothstep( 0.49, 0.5, max( g.x, g.y ) );
            float slab = hash( floor( w / 4.0 ) );
            vec3  conc = vec3( 0.36, 0.36, 0.35 ) * ( 0.85 + 0.25 * slab ) * ( 0.85 + 0.2 * mid );
            float paved = smoothstep( 0.35, 0.45, noise( w * 0.02 + 11.0 ) );
            col = mix( col, conc * ( 1.0 - 0.35 * joint ), paved );
            float stain = smoothstep( 0.7, 0.76, fbm( w * 0.11 + 13.0 ) );
            col *= 1.0 - 0.55 * stain;
            float line = smoothstep( 0.06, 0.0, abs( fract( w.x / 12.0 + 0.5 * noise( w * 0.01 ) ) - 0.5 ) - 0.47 ) * paved;
            col = mix( col, vec3( 0.55, 0.45, 0.12 ), line * 0.6 * step( 0.5, noise( w * 0.03 ) ) );
        }
        else if ( st == 5 )   // pass: snow drifts, blue in the hollows, sparkling
        {
            col *= 0.94 + 0.1 * smoothstep( 0.4, 0.6, fbm( w * 0.08 ) );
            col = mix( col, col * vec3( 0.86, 0.92, 1.05 ), smoothstep( 0.55, 0.3, mid ) * 0.6 );
            col += vec3( 0.4 ) * step( 0.996, hash( floor( w * 9.0 ) ) );
        }
        else if ( st == 6 )   // swamp: black water in the mud, shining
        {
            float pool = smoothstep( 0.6, 0.66, fbm( w * 0.05 + 21.0 ) );
            col = mix( col, vec3( 0.04, 0.06, 0.05 ), pool * 0.85 );
            spec = max( spec, pool * 0.9 );
            rough = mix( rough, 160.0, pool );
        }
        else if ( st == 8 )   // downtown: pavement tiles, kerbs, a manhole now and then
        {
            vec2  g = abs( fract( w / 1.2 ) - 0.5 );
            float joint = smoothstep( 0.47, 0.5, max( g.x, g.y ) );
            col = mix( col, vec3( 0.3, 0.3, 0.32 ) * ( 0.9 + 0.2 * hash( floor( w / 1.2 ) ) ), 0.8 );
            col *= 1.0 - 0.3 * joint;
            vec2 m = fract( w / 9.0 ) - 0.5;
            float hole = step( length( m * 9.0 ), 0.35 ) * step( 0.7, hash( floor( w / 9.0 ) ) );
            col = mix( col, vec3( 0.12, 0.12, 0.13 ), hole );
        }
        else if ( st == 9 )   // fairground: sawdust paths, trampled grass, confetti
        {
            float path = smoothstep( 0.02, 0.008, abs( fbm( w * 0.02 + 4.0 ) - 0.5 ) );
            col = mix( col, vec3( 0.55, 0.42, 0.26 ) * ( 0.85 + 0.3 * fine ), path * 0.9 );
            float h = hash( floor( w * 6.0 ) + 3.0 );
            vec3  conf = h > 0.995 ? vec3( 0.9, 0.2, 0.3 ) : h > 0.99 ? vec3( 0.2, 0.5, 0.9 ) : h > 0.985 ? vec3( 0.95, 0.85, 0.2 ) : col;
            col = conf;
        }
        else if ( st == 10 )   // proving ground: shell craters, scorched
        {
            vec2  c = cells( w * 0.06 );
            float crater = smoothstep( 0.22, 0.12, c.x ) * step( 0.6, hash( floor( w * 0.06 ) ) );
            float rim = smoothstep( 0.04, 0.0, abs( c.x - 0.24 ) ) * step( 0.6, hash( floor( w * 0.06 ) ) );
            col *= 1.0 - 0.45 * crater;
            col *= 1.0 + 0.15 * rim;
        }
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
    m_debris.clear();
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
    buildLife();
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
            else   // scrap: flattened cars stacked two to four high, as in every junkyard
            {
                static const Color cols[] = { { 132, 46, 38, 0 },  { 46, 76, 120, 0 }, { 176, 170, 156, 0 }, { 60, 92, 62, 0 },
                                              { 150, 120, 60, 0 }, { 40, 40, 44, 0 }, { 120, 70, 40, 0 },  { 96, 96, 104, 0 } };
                along( 4.3f, [&]( Vector2 at, Vector2 out, float hd ) {
                    int pile = 2 + rng.below( 3 );
                    for ( int h = 0; h < pile; ++h )
                    {
                        Vector2 q = add( at, mul( out, 1.0f + rng.range( -0.15f, 0.25f ) ) );
                        float   y = 0.26f + h * 0.5f, yaw = hd + rng.range( -0.12f, 0.12f );
                        Color   c = cols[rng.below( 8 )];
                        mb.box( { q.x, y, q.y }, { 0.88f, 0.23f, 2.0f }, yaw, c, 0.04f );                    // the body
                        mb.box( { q.x, y + 0.05f, q.y }, { 0.9f, 0.08f, 1.15f }, yaw, { 26, 30, 36, 0 } );   // its windows
                        mb.box( { q.x, y - 0.14f, q.y }, { 0.86f, 0.05f, 2.04f }, yaw, { 150, 150, 150, 0 } ); // bumpers, trim
                    }
                    // Now and then a tyre leaning against the stack.
                    if ( rng.chance( 0.35f ) )
                    {
                        Vector2 q = add( add( at, mul( out, -0.05f ) ), mul( perp( out ), rng.range( -1.2f, 1.2f ) ) );
                        mb.box( { q.x, 0.34f, q.y }, { 0.12f, 0.34f, 0.34f }, hd, { 22, 22, 24, 0 } );
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

void Renderer::setSticker( const std::string &text, Color back, Color ink, int car )
{
    m_stickerCar = text.empty() ? -1 : car;
    if ( text == m_sticker && back.r == m_stickerBack.r && ink.r == m_stickerInk.r && m_stickerTex.id ) return;
    m_sticker = text;
    m_stickerBack = back;
    m_stickerInk = ink;
    m_stickerDirty = !text.empty();
}

void Renderer::buildSticker()
{
    m_stickerDirty = false;
    if ( m_stickerTex.id ) UnloadRenderTexture( m_stickerTex );
    m_stickerTex = LoadRenderTexture( 512, 128 );
    BeginTextureMode( m_stickerTex );
    ClearBackground( m_stickerBack );
    DrawRectangleLinesEx( { 6, 6, 500, 116 }, 7, m_stickerInk );
    const Font &f = ui::font();
    float       size = 72;
    Vector2     m = MeasureTextEx( f, m_sticker.c_str(), size, 0 );
    while ( m.x > 468 && size > 18 )
    {
        size -= 3;
        m = MeasureTextEx( f, m_sticker.c_str(), size, 0 );
    }
    DrawTextEx( f, m_sticker.c_str(), { 256 - m.x * 0.5f, 64 - m.y * 0.5f }, size, 0, m_stickerInk );
    EndTextureMode();
    SetTextureFilter( m_stickerTex.texture, TEXTURE_FILTER_BILINEAR );
}

void Renderer::dropCoffin( const World &w, int car )
{
    if ( car < 0 || car >= (int)w.m_cars.size() ) return;
    const Car &c = w.m_cars[car];
    Debris     d;
    d.m_model = "_coffin";
    d.m_pos = { c.m_pos.x, 1.6f, c.m_pos.y };
    Vector2 r = c.right();
    float   side = m_rng.chance( 0.5f ) ? 1.0f : -1.0f;
    d.m_vel = { c.m_vel.x * 0.4f + r.x * side * 3.0f, 3.5f, c.m_vel.y * 0.4f + r.y * side * 3.0f };
    d.m_rot = c.m_head;
    d.m_spin = m_rng.range( -4.0f, 4.0f );
    d.m_paint = { 182, 182, 182, 255 };
    m_debris.push_back( d );
    m_roofCoffin = -1;
}

void Renderer::buildLife()
{
    // Flocks every 120 to 220 metres beyond the shoulders, the venue's birds; a dog at every kennel and at the
    // estate's gates.
    m_flocks.clear();
    m_dogs.clear();
    m_cues.clear();
    const Track &t = *m_track;
    std::string  v = m_venue ? m_venue->m_name : "";
    m_birdTint = { 40, 38, 42, 255 };
    m_birdSize = 1.0f;
    m_birdFlap = 14.0f;
    if ( v == "docks" ) m_birdTint = { 235, 235, 228, 255 };
    else if ( v == "refinery" || v == "downtown" || v == "fairground" ) m_birdTint = { 120, 124, 136, 255 };
    else if ( v == "desert" )
    {
        m_birdTint = { 74, 56, 44, 255 };
        m_birdSize = 1.8f;
        m_birdFlap = 7.0f;
    }
    else if ( v == "estate" )
    {
        m_birdTint = { 26, 22, 24, 255 };   // bats
        m_birdSize = 0.8f;
        m_birdFlap = 22.0f;
    }
    Rng rng;
    rng.seed( (uint32_t)t.m_length * 7919u + 17u );
    for ( float s = rng.range( 40.0f, 120.0f ); s < t.m_length - 20.0f; s += rng.range( 120.0f, 220.0f ) )
    {
        const TrackSample &ts = t.sampleAt( s );
        float              side = rng.chance( 0.5f ) ? 1.0f : -1.0f;
        Flock              f;
        f.m_home = t.pointAt( s, side * ( ts.m_half + ts.m_shoulder + rng.range( 2.5f, 8.0f ) ) );
        Vector2 push;
        if ( t.outside( f.m_home, push ) < 1.0f ) continue;   // not on another part of the road
        int n = 4 + rng.below( 5 );
        for ( int k = 0; k < n; ++k )
        {
            Bird b;
            b.m_pos = { f.m_home.x + rng.range( -2.5f, 2.5f ), 0.0f, f.m_home.y + rng.range( -2.5f, 2.5f ) };
            b.m_head = rng.range( 0, 6.28f );
            b.m_phase = rng.range( 0, 6.28f );
            f.m_birds.push_back( b );
        }
        m_flocks.push_back( f );
    }
    for ( const PropSpot &p : t.m_props )
    {
        bool kennel = p.m_model == "dog_kennel", gate = p.m_model == "iron_gate";
        if ( ( !kennel && !gate ) || m_dogs.size() >= 4 ) continue;
        TrackPos           tp = t.locate( p.m_pos, -1 );
        const TrackSample &ts = t.m_s[tp.m_i];
        Dog                d;
        Vector2            toRoad = mul( ts.m_right, tp.m_lat >= 0 ? -1.0f : 1.0f );
        d.m_home = add( p.m_pos, mul( toRoad, 2.5f ) );
        d.m_pos = d.m_home;
        d.m_along = ts.m_dir;
        d.m_head = headingOf( toRoad );
        d.m_tint = gate ? Color{ 52, 44, 44, 255 } : Color{ 255, 255, 255, 255 };   // the estate keeps a black one
        m_dogs.push_back( d );
    }
}

void Renderer::life( const World &w, float dt )
{
    if ( !m_track || dt <= 0 ) return;
    dt = std::min( dt, 0.05f );
    auto nearest = [&]( Vector2 p, float &speed, Vector2 &from ) {
        float best = 1e9f;
        for ( const Car &c : w.m_cars )
        {
            if ( c.m_wrecked ) continue;
            float d = len( sub( c.m_pos, p ) );
            if ( d < best )
            {
                best = d;
                speed = len( c.m_vel );
                from = c.m_pos;
            }
        }
        return best;
    };
    // People: a car close and fast, and they throw themselves aside (or the crowd steps back); then up again.
    for ( Person &p : m_people )
    {
        if ( p.m_t >= 0 )
        {
            p.m_t += dt;
            if ( p.m_t > ( p.m_crowd ? 1.4f : 2.6f ) ) p.m_t = -1.0f;
            continue;
        }
        float   sp = 0;
        Vector2 from = { 0, 0 };
        if ( nearest( p.m_pos, sp, from ) < ( p.m_crowd ? 11.0f : 13.0f ) && sp > 10.0f ) p.m_t = m_rng.range( 0.0f, 0.12f );
    }
    // Birds: up when a car comes, away and out of sight, back after a while when it is quiet.
    for ( Flock &f : m_flocks )
    {
        float   sp = 0;
        Vector2 from = { 0, 0 };
        float   d = nearest( f.m_home, sp, from );
        if ( f.m_t < 0 )
        {
            for ( Bird &b : f.m_birds )
            {
                b.m_phase += dt * 3.0f;   // pecking
                if ( m_rng.chance( dt * 0.3f ) ) b.m_head += m_rng.range( -1.5f, 1.5f );
            }
            if ( d < 22.0f && sp > 4.0f )
            {
                f.m_t = 0;
                Vector2 away = norm( sub( f.m_home, from ) );
                for ( Bird &b : f.m_birds )
                {
                    float sideways = m_rng.range( -0.6f, 0.6f );
                    Vector2 dir = norm( add( away, mul( perp( away ), sideways ) ) );
                    float   v = m_rng.range( 6.0f, 10.0f );
                    b.m_vel = { dir.x * v, m_rng.range( 3.0f, 5.5f ), dir.y * v };
                    b.m_head = headingOf( dir );
                }
                m_cues.push_back( { 0, f.m_home } );
            }
            continue;
        }
        f.m_t += dt;
        for ( Bird &b : f.m_birds )
        {
            b.m_vel.y = std::min( 6.0f, b.m_vel.y + dt * 1.2f );
            float turn = std::sin( f.m_t * 0.7f + b.m_phase ) * 0.4f * dt;
            float c = std::cos( turn ), s = std::sin( turn );
            b.m_vel = { b.m_vel.x * c - b.m_vel.z * s, b.m_vel.y, b.m_vel.x * s + b.m_vel.z * c };
            b.m_pos = Vector3Add( b.m_pos, Vector3Scale( b.m_vel, dt ) );
            b.m_head = headingOf( { b.m_vel.x, b.m_vel.z } );
            b.m_phase += dt * m_birdFlap;
        }
        if ( f.m_t > 40.0f && d > 50.0f )
        {
            // Back on the ground, as if they had never left.
            f.m_t = -1.0f;
            for ( Bird &b : f.m_birds ) b.m_pos = { f.m_home.x + m_rng.range( -2.5f, 2.5f ), 0.0f, f.m_home.y + m_rng.range( -2.5f, 2.5f ) };
        }
    }
    // Dogs: after the cars along the road, as far as the chain or the fence lets them, barking; then home.
    for ( Dog &dg : m_dogs )
    {
        float   sp = 0;
        Vector2 from = { 0, 0 };
        float   d = nearest( dg.m_home, sp, from );
        Vector2 goal = dg.m_home;
        if ( d < 40.0f && sp > 6.0f )
        {
            float along = clampf( dot( sub( from, dg.m_home ), dg.m_along ), -26.0f, 26.0f );
            goal = add( dg.m_home, mul( dg.m_along, along ) );
            dg.m_calm = 0;
            dg.m_bark -= dt;
            if ( dg.m_bark <= 0 )
            {
                dg.m_bark = m_rng.range( 0.7f, 1.4f );
                m_cues.push_back( { 1, dg.m_pos } );
            }
        }
        else
            dg.m_calm += dt;
        Vector2 to = sub( goal, dg.m_pos );
        float   dist = len( to );
        float   want = dist < 0.4f ? 0.0f : std::min( dg.m_calm > 1.0f ? 4.0f : 11.0f, dist * 3.0f );
        dg.m_speed += clampf( want - dg.m_speed, -20.0f * dt, 14.0f * dt );
        if ( dist > 0.05f )
        {
            dg.m_pos = add( dg.m_pos, mul( to, std::min( 1.0f, dg.m_speed * dt / dist ) ) );
            if ( dg.m_speed > 0.5f ) dg.m_head = headingOf( to );
        }
        dg.m_phase += dt * ( 2.0f + dg.m_speed * 1.6f );
    }
}

void Renderer::drawLife( bool shadow )
{
    Material &lit = shadow ? g.m_depthMat : g.m_litMat;
    Vector2   f = m_view.m_focus;
    float     near = m_view.m_height * 1.3f + 30.0f;
    for ( const Person &p : m_people )
    {
        if ( dist2( p.m_pos, f ) > near * near ) continue;
        const ModelMesh &m = modelOf( p.m_model );
        if ( !m.m_hasBase ) continue;
        float tilt = 0, shift = 0, hop = 0;
        if ( p.m_t >= 0 )
        {
            if ( p.m_crowd )
            {
                shift = std::sin( std::min( 1.0f, p.m_t / 1.4f ) * Pi ) * 1.1f;
                hop = std::fabs( std::sin( p.m_t * 7.0f ) ) * 0.35f * ( 1.0f - p.m_t / 1.4f );
            }
            else
            {
                // Down in a quarter second, a second and a half flat on the ground, then up again.
                float down = p.m_t < 0.25f ? p.m_t / 0.25f : p.m_t < 1.8f ? 1.0f : std::max( 0.0f, 1.0f - ( p.m_t - 1.8f ) / 0.8f );
                tilt = down * 1.45f;
                shift = std::min( 1.0f, p.m_t / 0.25f ) * 1.4f;
                hop = p.m_t < 0.25f ? std::sin( p.m_t / 0.25f * Pi ) * 0.4f : 0.0f;
            }
        }
        Vector3 axis = { p.m_away.y, 0, -p.m_away.x };
        Matrix  xf = MatrixMultiply( MatrixMultiply( MatrixScale( p.m_scale, p.m_scale, p.m_scale ), MatrixRotateY( -p.m_rot ) ),
                                     MatrixRotate( axis, tilt ) );
        Vector2 at = add( p.m_pos, mul( p.m_away, shift ) );
        xf = MatrixMultiply( xf, MatrixTranslate( at.x, hop + std::sin( tilt ) * 0.25f, at.y ) );
        DrawMesh( m.m_base, lit, xf );
        if ( m.m_hasHead ) DrawMesh( m.m_head, lit, xf );
    }
    const ModelMesh &body = modelOf( "bird_body" ), &wing = modelOf( "bird_wing" );
    if ( !shadow ) setVec4( g.m_lit, "tint", { m_birdTint.r / 255.0f, m_birdTint.g / 255.0f, m_birdTint.b / 255.0f, 0 } );
    for ( const Flock &fl : m_flocks )
    {
        if ( dist2( fl.m_home, f ) > ( near + 60.0f ) * ( near + 60.0f ) ) continue;
        if ( fl.m_t > 14.0f ) continue;   // out of sight
        for ( const Bird &b : fl.m_birds )
        {
            Matrix at = MatrixMultiply( MatrixMultiply( MatrixScale( m_birdSize, m_birdSize, m_birdSize ), MatrixRotateY( -b.m_head ) ),
                                        MatrixTranslate( b.m_pos.x, b.m_pos.y + ( fl.m_t < 0 ? std::max( 0.0f, std::sin( b.m_phase ) ) * 0.04f : 0.0f ),
                                                         b.m_pos.z ) );
            if ( body.m_hasBase ) DrawMesh( body.m_base, lit, at );
            float flap = fl.m_t < 0 ? -0.15f : std::sin( b.m_phase ) * 0.9f;
            Matrix right = MatrixMultiply( MatrixMultiply( MatrixRotateZ( flap ), MatrixTranslate( 0.07f, 0.2f, 0 ) ), at );
            Matrix left = MatrixMultiply( MatrixMultiply( MatrixMultiply( MatrixRotateZ( flap ), MatrixRotateY( Pi ) ), MatrixTranslate( -0.07f, 0.2f, 0 ) ), at );
            if ( wing.m_hasBase )
            {
                DrawMesh( wing.m_base, lit, right );
                DrawMesh( wing.m_base, lit, left );
            }
        }
    }
    const ModelMesh &dogBody = modelOf( "dog_body" ), &leg = modelOf( "dog_leg" );
    for ( const Dog &d : m_dogs )
    {
        if ( dist2( d.m_pos, f ) > near * near ) continue;
        if ( !shadow ) setVec4( g.m_lit, "tint", { d.m_tint.r / 255.0f, d.m_tint.g / 255.0f, d.m_tint.b / 255.0f, 0 } );
        float  bob = std::fabs( std::sin( d.m_phase ) ) * 0.06f * std::min( 1.0f, d.m_speed / 4.0f );
        Matrix at = MatrixMultiply( MatrixRotateY( -d.m_head ), MatrixTranslate( d.m_pos.x, bob, d.m_pos.y ) );
        if ( dogBody.m_hasBase ) DrawMesh( dogBody.m_base, lit, at );
        const float sc = 1.35f;
        const Vector3 hips[4] = { { 0.1f, 0.42f, -0.28f }, { -0.1f, 0.42f, -0.28f }, { 0.1f, 0.42f, 0.28f }, { -0.1f, 0.42f, 0.28f } };
        for ( int k = 0; k < 4; ++k )
        {
            float  swing = std::sin( d.m_phase + ( k == 0 || k == 3 ? 0.0f : Pi ) ) * 0.6f * std::min( 1.0f, d.m_speed / 3.0f );
            Matrix lm = MatrixMultiply( MatrixRotateX( swing ), MatrixTranslate( hips[k].x * sc, hips[k].y * sc, hips[k].z * sc ) );
            if ( leg.m_hasBase ) DrawMesh( leg.m_base, lit, MatrixMultiply( lm, at ) );
        }
    }
    if ( !shadow ) setVec4( g.m_lit, "tint", { 1, 1, 1, 0 } );
}

void Renderer::buildProps()
{
    m_turning.clear();
    const Track &t = *m_track;
    const float  cell = 64.0f;
    std::map<std::pair<int, int>, MeshBuilder> cells;
    std::map<std::string, ModelMesh>           cpu;
    m_people.clear();
    for ( const PropSpot &p : t.m_props )
    {
        // People near the road are drawn apart: they throw themselves aside when a car comes.
        if ( !p.m_car && ( p.m_model.rfind( "fig_", 0 ) == 0 || p.m_model.rfind( "crowd_", 0 ) == 0 ) )
        {
            TrackPos           tp = t.locate( p.m_pos, -1 );
            const TrackSample &ts = t.m_s[tp.m_i];
            if ( std::fabs( tp.m_lat ) - ts.m_half < 22.0f && m_people.size() < 160 )
            {
                Person pe;
                pe.m_model = p.m_model;
                pe.m_pos = p.m_pos;
                pe.m_rot = p.m_rot * Deg;
                pe.m_scale = p.m_scale;
                pe.m_away = mul( ts.m_right, tp.m_lat >= 0 ? 1.0f : -1.0f );
                pe.m_crowd = p.m_model.rfind( "crowd_", 0 ) == 0;
                m_people.push_back( pe );
                continue;
            }
        }
        // Cars by the road: a car's own model in a paint of its own; wrecks rusted, sunk on their rims, sometimes
        // on their side.
        std::string meshKey = p.m_model;
        Paint       paint;
        if ( p.m_car )
        {
            const CarDef *cd = g_data.car( p.m_model );
            if ( !cd ) continue;
            static const Color parked[] = { { 150, 40, 36, 255 }, { 40, 70, 120, 255 }, { 200, 196, 184, 255 }, { 30, 32, 36, 255 },
                                            { 60, 96, 60, 255 },  { 170, 140, 70, 255 }, { 110, 110, 116, 255 }, { 120, 60, 110, 255 } };
            static const Color rust[] = { { 112, 58, 34, 255 }, { 92, 70, 52, 255 }, { 70, 60, 54, 255 }, { 128, 82, 44, 255 },
                                          { 84, 54, 40, 255 }, { 66, 72, 70, 255 } };
            uint32_t r = p.m_seed;
            if ( p.m_car == 1 )
            {
                paint.m_c[0] = parked[r % 8];
                paint.m_c[1] = parked[( r / 8 ) % 8];
            }
            else
            {
                paint.m_c[0] = rust[r % 6];
                paint.m_c[1] = rust[( r / 6 ) % 6];
                paint.m_c[2] = { 60, 54, 50, 255 };
            }
            meshKey = cd->m_model + "#" + std::to_string( p.m_car ) + "_" + std::to_string( r % 48 );
            if ( !cpu.count( meshKey ) )
            {
                const ModelDef *d = g_data.model( cd->m_model );
                ModelMesh       m;
                if ( d ) m = Forge::build( *d, false, paint );
                cpu.emplace( meshKey, m );
            }
        }
        auto it = cpu.find( meshKey );
        if ( it == cpu.end() )
        {
            const ModelDef *d = g_data.model( p.m_model );
            ModelMesh       m;
            if ( d ) m = Forge::build( *d, false );
            it = cpu.emplace( p.m_model, m ).first;
        }
        const ModelMesh &m = it->second;
        float  sink = p.m_car == 2 ? -0.22f : 0.0f;
        Matrix xf = place( p.m_pos, p.m_rot * Deg, sink, p.m_scale );
        if ( p.m_tilt != 0.0f || p.m_car == 2 )
        {
            float roll = p.m_tilt * Deg + ( p.m_car == 2 ? ( ( p.m_seed >> 8 ) % 9 - 4 ) * 0.025f : 0.0f );
            float pitch = p.m_car == 2 ? ( ( p.m_seed >> 12 ) % 7 - 3 ) * 0.03f : 0.0f;
            xf = MatrixMultiply( MatrixMultiply( MatrixRotateZ( roll ), MatrixRotateX( pitch ) ), xf );
        }
        std::pair<int, int> key = { (int)std::floor( p.m_pos.x / cell ), (int)std::floor( p.m_pos.y / cell ) };
        MeshBuilder &mb = cells[key];
        mb.m_noise = 0;
        for ( const Mesh *mesh : { &m.m_base, &m.m_head, &m.m_spin } )
            if ( mesh->vertexCount && !( mesh == &m.m_spin && !p.m_car ) ) mb.append( *mesh, xf );
        if ( m.m_hasSpin && !p.m_car ) m_turning.push_back( { p.m_model, xf } );   // drawn turning every frame
        // A parked car's tyres (a wreck sits on its rims, mostly out of sight).
        if ( p.m_car == 1 )
            for ( const WheelSpot &ws : m.m_wheels )
            {
                Vector3 c = Vector3Transform( ws.m_pos, xf );
                mb.box( c, { ws.m_w * 0.5f, ws.m_r, ws.m_r * 0.9f }, p.m_rot * Deg, { 22, 22, 24, 0 } );
            }
        if ( m.m_lamp && !p.m_car )
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
            // Chips of paint from both cars and a spray of glass.
            if ( e.m_power > 10 && e.m_car >= 0 && e.m_car < (int)w.m_cars.size() )
            {
                int chips = (int)clampf( e.m_power * 0.25f, 2, 8 );
                for ( int who : { e.m_car, e.m_other } )
                    if ( who >= 0 && who < (int)w.m_cars.size() )
                    {
                        Color p = w.m_cars[who].m_setup.m_paint.m_c[0];
                        puff( { at.x, 0.9f, at.z }, chips, 7.0f, 0.22f, p, { p.r, p.g, p.b, 0 }, 0.9f, T_SOFT, false );
                    }
                puff( { at.x, 1.0f, at.z }, chips, 5.0f, 0.12f, { 220, 240, 255, 220 }, { 220, 240, 255, 0 }, 0.6f, T_SPARK, true );
            }
            break;
        }
        case EvE::SCRAPE:
            puff( at, 2, 6.0f, 0.3f, { 255, 220, 140, 255 }, { 255, 90, 10, 0 }, 0.3f, T_SPARK, true );
            break;
        case EvE::PART:
            if ( e.m_car >= 0 && e.m_car < (int)w.m_cars.size() )
            {
                // A bumper, a door, a bonnet: thrown off sideways, then lying where it fell.
                const Car &c = w.m_cars[e.m_car];
                static const char *parts[] = { "hubcap", "car_door", "car_door" };
                Debris d;
                d.m_model = parts[std::clamp( e.m_kind, 0, 2 )];
                float side = m_rng.chance( 0.5f ) ? 1.0f : -1.0f;
                Vector2 r = c.right();
                d.m_pos = { c.m_pos.x, 0.8f, c.m_pos.y };
                d.m_vel = { c.m_vel.x * 0.5f + r.x * side * 5.0f, 4.0f, c.m_vel.y * 0.5f + r.y * side * 5.0f };
                d.m_rot = m_rng.range( 0, 6.28f );
                d.m_spin = m_rng.range( -8.0f, 8.0f );
                d.m_paint = c.m_setup.m_paint.m_c[0];
                if ( m_debris.size() > 60 ) m_debris.erase( m_debris.begin() );
                m_debris.push_back( d );
                puff( { c.m_pos.x, 0.9f, c.m_pos.y }, 6, 6.0f, 0.3f, { 255, 220, 140, 255 }, { 255, 80, 10, 0 }, 0.4f, T_SPARK, true );
            }
            break;
        case EvE::LOOSE:
        {
            // Snow, water, dust or splinters, by what it was.
            if ( e.m_kind == 2 )
                puff( { at.x, 0.9f, at.z }, 18, 4.0f, 0.7f, { 240, 246, 255, 235 }, { 240, 246, 255, 0 }, 1.2f, T_SOFT, false );
            else if ( e.m_kind == 3 )
            {
                if ( m_fountains.size() > 8 ) m_fountains.erase( m_fountains.begin() );
                m_fountains.push_back( { { at.x, 0.4f, at.z }, 14.0f } );
            }
            else
                puff( { at.x, 0.4f, at.z }, 4, 2.5f, 0.6f, { 120, 110, 95, 160 }, { 120, 110, 95, 0 }, 0.8f, T_SMOKE, false );
            break;
        }
        case EvE::ROCK:
            if ( e.m_kind == 1 )
                puff( { at.x, 0.8f, at.z }, 16, 5.0f, 2.2f, { 150, 140, 125, 230 }, { 170, 160, 145, 0 }, 2.2f, T_SMOKE, false );
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
            if ( e.m_car >= 0 && e.m_car < (int)w.m_cars.size() )
            {
                Color p = w.m_cars[e.m_car].m_setup.m_paint.m_c[0];
                puff( { at.x, 1.2f, at.z }, 10, 9.0f, 0.35f, p, { p.r, p.g, p.b, 0 }, 1.4f, T_SOFT, false );   // the bonnet and doors
            }
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
    for ( Debris &d : m_debris )
    {
        d.m_age += dt;
        if ( d.m_pos.y > 0.05f || d.m_vel.y > 0 )
        {
            d.m_vel.y -= 18.0f * dt;
            d.m_pos = Vector3Add( d.m_pos, Vector3Scale( d.m_vel, dt ) );
            d.m_rot += d.m_spin * dt;
            if ( d.m_pos.y <= 0.05f )
            {
                d.m_pos.y = 0.05f;
                d.m_vel = { 0, 0, 0 };
            }
        }
    }
    m_wetRace = w.m_wetness;
    m_fogRace = w.m_fogAdd;
    life( w, dt );
    // The ghosts leave a pale mist behind.
    for ( const Ghost &s : m_spirits )
        if ( s.m_on && m_rng.chance( std::min( 1.0f, dt * 24.0f ) ) )
        {
            Particle p;
            p.m_pos = { s.m_pos.x + m_rng.range( -1.0f, 1.0f ), 0.6f, s.m_pos.y + m_rng.range( -1.0f, 1.0f ) };
            p.m_vel = { 0, 1.2f, 0 };
            p.m_size0 = 0.9f;
            p.m_size1 = 1.8f;
            p.m_c0 = { 120, 255, 170, 110 };
            p.m_c1 = { 120, 255, 170, 0 };
            p.m_life = 1.0f;
            p.m_tex = T_SOFT;
            p.m_add = true;
            spawn( p );
        }
    // Broken hydrants spout for a while.
    for ( auto &[at, left] : m_fountains )
    {
        left -= dt;
        if ( left <= 0 ) continue;
        int n = (int)( dt * 60.0f * std::min( 1.0f, left / 3.0f ) ) + 1;
        for ( int k = 0; k < n; ++k )
        {
            Particle p;
            p.m_pos = at;
            p.m_vel = { m_rng.range( -0.8f, 0.8f ), m_rng.range( 7.0f, 10.0f ), m_rng.range( -0.8f, 0.8f ) };
            p.m_rise = -14.0f;
            p.m_size0 = 0.35f;
            p.m_size1 = 0.8f;
            p.m_c0 = { 200, 225, 255, 200 };
            p.m_c1 = { 200, 225, 255, 0 };
            p.m_life = 1.3f;
            p.m_tex = T_SOFT;
            p.m_add = false;
            spawn( p );
        }
    }
    m_fountains.erase( std::remove_if( m_fountains.begin(), m_fountains.end(), []( const auto &f ) { return f.second <= 0; } ),
                       m_fountains.end() );
    // The bells of crossings and bridges, and where a rock is about to land: blinking lights.
    bool blink = std::fmod( w.m_time, 0.9f ) < 0.45f;
    if ( m_track )
        for ( const MoverSpot &mv : m_track->m_movers )
        {
            float amount = 0.0f;
            int   st = mv.m_kind == "rockfall" ? 0 : w.moverState( mv, amount );
            if ( st == 0 ) continue;
            Color c = mv.m_kind == "train" ? Color{ 255, 40, 30, 255 } : Color{ 255, 160, 30, 255 };
            for ( int side = -1; side <= 1; side += 2 )
            {
                Vector2 p = add( add( mv.m_pos, mul( mv.m_right, side * ( mv.m_half + 1.2f ) ) ),
                                 mul( mv.m_dir, -side * ( mv.m_kind == "train" ? 4.0f : mv.m_len * 0.5f + 3.0f ) ) );
                if ( blink == ( side < 0 ) ) m_glows.push_back( { { p.x, 2.8f, p.y }, c, 9.0f, 3.0f, 0, dt * 1.5f } );
            }
        }
    for ( const Rock &r : w.m_rocks )
        if ( r.m_age < 0 && blink ) m_glows.push_back( { { r.m_pos.x, 0.5f, r.m_pos.y }, { 255, 120, 30, 255 }, 5.0f, 2.0f, 0, dt * 1.5f } );
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
        std::string we = m_venue->m_weather;
        if ( m_wetRace > 0.02f && we != "storm" ) we = "rain";
        Vector2 f = m_view.m_focus;
        float   area = m_view.m_height * 0.9f;
        int     n = 0;
        if ( we == "rain" || we == "storm" )
            n = (int)std::ceil( ( m_quality == 0 ? 6 : 14 ) * ( m_venue->m_weather == "rain" || m_venue->m_weather == "storm" ? 1.0f : m_wetRace ) );
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
        setFloat( s, "fogDensity", fogD + m_fogRace );
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
    setFloat( g.m_surface, "wet", std::max( m_wet, m_wetRace ) );
    setFloat( g.m_surface, "time", m_time );
    {
        static const char *styles[] = { "", "junkyard", "desert", "cemetery", "docks", "pass", "swamp", "refinery", "downtown",
                                         "fairground", "proving", "estate" };
        float style = 0;
        for ( int k = 1; k < 12; ++k )
            if ( v && v->m_name == styles[k] ) style = (float)k;
        setFloat( g.m_surface, "landStyle", style );
    }
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
    rlSetClipPlanes( 1.0, 400.0 );   // before BeginMode3D: it builds the projection from them
    BeginMode3D( lc );
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
    Matrix base = MatrixMultiply( MatrixMultiply( MatrixRotateZ( c.m_lean * 0.07f + ( c.m_wrecked ? 0.12f : 0.0f ) ),
                                                  MatrixRotateX( c.m_pitch * 0.04f + ( c.m_wrecked ? 0.05f : 0.0f ) ) ),
                                  place( c.m_pos, c.m_head, c.m_wrecked ? -0.08f : 0.0f ) );
    Material &mat = shadow ? g.m_depthMat : g.m_litMat;
    float     hp = c.m_maxHp > 0 ? clampf( c.m_hp / c.m_maxHp, 0, 1 ) : 1.0f;
    if ( !shadow )
    {
        float k = c.m_wrecked ? 0.22f : 0.55f + 0.45f * std::sqrt( hp );
        setVec4( g.m_lit, "tint", { k, k * ( c.m_wrecked ? 0.9f : 1.0f ), k * ( c.m_wrecked ? 0.85f : 1.0f ), c.m_hitFlash * 0.35f } );
        setFloat( g.m_lit, "specular", c.m_wrecked ? 0.05f : 0.6f * ( 0.4f + 0.6f * hp ) );
        setFloat( g.m_lit, "dent", c.m_wrecked ? 1.0f : clampf( ( 1.0f - hp ) * 1.15f, 0, 1 ) );
        setVec4( g.m_lit, "dentFrame", { c.m_pos.x, c.m_pos.y, std::cos( c.m_head ), std::sin( c.m_head ) } );
    }
    if ( m.m_hasBase ) DrawMesh( m.m_base, mat, base );
    if ( m.m_hasHead ) DrawMesh( m.m_head, mat, base );
    if ( m.m_hasSpin ) DrawMesh( m.m_spin, mat, base );
    if ( !shadow ) setFloat( g.m_lit, "dent", 0.0f );
    const Mesh &wheel = g.m_forge.wheel();
    bool lost = false;   // a wreck has lost its front left wheel
    for ( const WheelSpot &ws : m.m_wheels )
    {
        if ( c.m_wrecked && !lost && ws.m_front && ws.m_pos.x < 0 )
        {
            lost = true;
            continue;
        }
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
    if ( i == m_stickerCar && !shadow && m_stickerTex.id && !c.m_wrecked )
    {
        // The sponsor on the roof (on the bonnet when a coffin or a rocket pod takes the roof): a textured quad just above
        // it, the text across the car, its top to the nose.
        rlDrawRenderBatchActive();
        bool    bonnet = i == m_roofCoffin || c.m_rockets > 0;
        Vector3 at = bonnet ? c.m_def->m_mountFront : c.m_def->m_mountRoof;
        float   hw = c.m_wid * 0.4f, hl = bonnet ? 0.3f : 0.38f, y = at.y + 0.04f, z = at.z + ( bonnet ? 0.8f : 0.0f );
        Vector3 q[4] = { { -hw, y, z + hl }, { hw, y, z + hl }, { hw, y, z - hl }, { -hw, y, z - hl } };
        for ( Vector3 &v : q ) v = Vector3Transform( v, base );
        float lum = m_dark ? 0.55f : 0.95f;
        rlDisableBackfaceCulling();
        rlSetTexture( m_stickerTex.texture.id );
        rlBegin( RL_QUADS );
        rlColor4f( lum, lum, lum, 1.0f );
        rlTexCoord2f( 0, 0 );
        rlVertex3f( q[0].x, q[0].y, q[0].z );
        rlTexCoord2f( 1, 0 );
        rlVertex3f( q[1].x, q[1].y, q[1].z );
        rlTexCoord2f( 1, 1 );
        rlVertex3f( q[2].x, q[2].y, q[2].z );
        rlTexCoord2f( 0, 1 );
        rlVertex3f( q[3].x, q[3].y, q[3].z );
        rlEnd();
        rlSetTexture( 0 );
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
    }
    if ( i == m_roofCoffin ) attach( "_coffin", { c.m_def->m_mountRoof.x, c.m_def->m_mountRoof.y + 0.05f, c.m_def->m_mountRoof.z + 0.3f } );
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
    // Parts that came off the cars: tinted in their car's paint.
    for ( const Debris &d : m_debris )
    {
        const ModelMesh &dm = modelOf( d.m_model );
        if ( !shadow ) setVec4( g.m_lit, "tint", { d.m_paint.r / 255.0f * 1.4f, d.m_paint.g / 255.0f * 1.4f, d.m_paint.b / 255.0f * 1.4f, 0 } );
        if ( dm.m_hasBase ) DrawMesh( dm.m_base, lit, place( { d.m_pos.x, d.m_pos.z }, d.m_rot, d.m_pos.y - 0.04f ) );
    }
    if ( !shadow ) setVec4( g.m_lit, "tint", { 1, 1, 1, 0 } );
    // Obstacles, barrels, pickups, mines, spikes.
    for ( const ObstacleSpot &o : m_track->m_obstacles )
        if ( !o.m_model.empty() )
        {
            const ModelMesh &m = modelOf( o.m_model );
            if ( m.m_hasBase ) DrawMesh( m.m_base, lit, place( o.m_pos, o.m_rot * Deg ) );
        }
    drawLife( shadow );
    // The shoulders' small things: standing, tipping over, rolling, lying where they came to rest.
    {
        float near = m_view.m_height * 1.3f + 30.0f;
        for ( const Loose &l : w.m_loose )
        {
            if ( l.m_gone || dist2( l.m_pos, f ) > near * near ) continue;
            const LooseSpot &sp = m_track->m_loose[l.m_spot];
            const ModelMesh &m = modelOf( sp.m_model );
            if ( !m.m_hasBase ) continue;
            Vector3 axis = { l.m_fallDir.y, 0, -l.m_fallDir.x };   // up turns towards where it falls
            float   tilt = sp.m_kind == 1 ? l.m_roll : l.m_fall * 1.5708f;
            float   lift = sp.m_kind == 1 ? 0.0f : std::sin( l.m_fall * 1.5708f ) * std::min( sp.m_r, sp.m_h ) * 0.55f;
            Matrix  xf = MatrixMultiply( MatrixMultiply( MatrixScale( sp.m_scale, sp.m_scale, sp.m_scale ), MatrixRotateY( -l.m_rot ) ),
                                         MatrixRotate( axis, tilt ) );
            if ( sp.m_kind == 1 )   // a ball rolls round its middle
                xf = MatrixMultiply( MatrixMultiply( MatrixMultiply( MatrixTranslate( 0, -sp.m_h * 0.5f, 0 ), MatrixRotateY( -l.m_rot ) ),
                                                     MatrixRotate( axis, tilt ) ),
                                     MatrixMultiply( MatrixScale( sp.m_scale, sp.m_scale, sp.m_scale ), MatrixTranslate( 0, sp.m_h * 0.5f, 0 ) ) );
            DrawMesh( m.m_base, lit, MatrixMultiply( xf, MatrixTranslate( l.m_pos.x, lift + l.m_y, l.m_pos.y ) ) );
        }
    }
    // What turns: Ferris wheels, carousels, radar dishes.
    for ( const auto &[name, xf] : m_turning )
    {
        const ModelMesh &tm = modelOf( name );
        if ( !tm.m_hasSpin ) continue;
        Matrix spin = MatrixMultiply( MatrixMultiply( MatrixTranslate( -tm.m_spinAt.x, -tm.m_spinAt.y, -tm.m_spinAt.z ),
                                                      MatrixRotate( tm.m_spinAxis, m_time * tm.m_spinSpeed * Deg ) ),
                                      MatrixTranslate( tm.m_spinAt.x, tm.m_spinAt.y, tm.m_spinAt.z ) );
        DrawMesh( tm.m_spin, lit, MatrixMultiply( spin, xf ) );
    }
    // The track's clockwork: rails and posts, a passing train, drawbridge leaves, rocks.
    for ( const MoverSpot &mv : m_track->m_movers )
    {
        float head = std::atan2( mv.m_dir.x, -mv.m_dir.y ), back = std::atan2( -mv.m_dir.x, mv.m_dir.y );
        float amount = 0.0f;
        int   st = mv.m_kind == "rockfall" ? 0 : w.moverState( mv, amount );
        if ( mv.m_kind == "train" )
        {
            const ModelMesh &rail = modelOf( "rail_piece" );
            for ( float v = -( mv.m_half + 30.0f ); v <= mv.m_half + 30.0f; v += 10.0f )
                if ( rail.m_hasBase ) DrawMesh( rail.m_base, lit, place( add( mv.m_pos, mul( mv.m_right, v ) ), head ) );
            const ModelMesh &post = modelOf( "crossing_post" );
            if ( post.m_hasBase )
            {
                DrawMesh( post.m_base, lit, place( add( add( mv.m_pos, mul( mv.m_right, mv.m_half + 1.2f ) ), mul( mv.m_dir, -4.0f ) ), back ) );
                DrawMesh( post.m_base, lit, place( add( add( mv.m_pos, mul( mv.m_right, -mv.m_half - 1.2f ) ), mul( mv.m_dir, 4.0f ) ), head ) );
            }
            if ( st == 2 )
            {
                float            across = std::atan2( mv.m_right.x, -mv.m_right.y ), front = w.trainFront( mv );
                const ModelMesh &loco = modelOf( "train_loco" ), &wagon = modelOf( "train_wagon" );
                if ( loco.m_hasBase ) DrawMesh( loco.m_base, lit, place( add( mv.m_pos, mul( mv.m_right, front - 7.5f ) ), across ) );
                for ( int k = 0; k < 3; ++k )
                    if ( wagon.m_hasBase )
                        DrawMesh( wagon.m_base, lit, place( add( mv.m_pos, mul( mv.m_right, front - 22.5f - 14.5f * k ) ), across ) );
            }
        }
        else if ( mv.m_kind == "drawbridge" )
        {
            const ModelMesh &leaf = modelOf( "bridge_leaf" ), &gap = modelOf( "bridge_gap" );
            float            sx = ( 2.0f * mv.m_half + 2.0f ) / 20.0f, sz = mv.m_len / 24.0f, ang = ( st == 2 ? amount : 0.0f ) * 70.0f * Deg;
            if ( ang > 0.01f && gap.m_hasBase && !shadow )
                DrawMesh( gap.m_base, lit, MatrixMultiply( MatrixScale( ( 2.0f * mv.m_half + 4.0f ) / 24.0f, 1, sz ), place( mv.m_pos, head ) ) );
            for ( int side = -1; side <= 1; side += 2 )
            {
                Vector2 hinge = add( mv.m_pos, mul( mv.m_dir, side * mv.m_len * 0.5f ) );
                Matrix  m = MatrixMultiply( MatrixMultiply( MatrixScale( sx, 1, sz ), MatrixRotateX( ang ) ),
                                            place( hinge, side < 0 ? head : back, 0.02f ) );
                if ( leaf.m_hasBase ) DrawMesh( leaf.m_base, lit, m );
            }
        }
    }
    {
        std::string rockModel = g_data.model( "rock_snow" ) && m_venue && m_venue->m_name == "pass" ? "rock_snow"
                                : g_data.model( "rock_red" ) ? "rock_red" : "_barrel";
        const ModelMesh &rm = modelOf( rockModel );
        for ( const Rock &r : w.m_rocks )
        {
            float y = r.m_age < 0 ? 30.0f * ( r.m_age / -1.8f ) * ( r.m_age / -1.8f ) : 0.0f;
            if ( rm.m_hasBase ) DrawMesh( rm.m_base, lit, place( r.m_pos, r.m_r * 2.3f, y, r.m_r / 1.3f ) );
        }
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
    if ( !shadow )
        for ( const Ghost &s : m_spirits )
        {
            if ( !s.m_on ) continue;
            const ModelMesh &m = g.m_forge.get( g_data, s.m_model, s.m_paint );
            // Pale and green, a faint glow that flickers (more glow and the night's bloom turns it into a white block).
            setVec4( g.m_lit, "tint", { 0.35f, 0.85f, 0.55f, 0.14f + 0.06f * std::sin( m_time * 7.0f + s.m_pos.x ) } );
            if ( m.m_hasBase ) DrawMesh( m.m_base, g.m_litMat, place( s.m_pos, s.m_head, 0.2f + 0.1f * std::sin( m_time * 2.0f ) ) );
        }
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
    // Brake lights (white backing up) and the headlights' glow seen from above.
    for ( const Car &c : w.m_cars )
    {
        if ( c.m_wrecked ) continue;
        Vector2 f = c.fwd(), r = c.right();
        for ( float sd : { -1.0f, 1.0f } )
        {
            if ( c.m_brakeLight > 0 || c.m_reverseLight > 0 )
            {
                Vector2 p = add( c.m_pos, add( mul( f, -c.m_len * 0.5f ), mul( r, sd * c.m_wid * 0.33f ) ) );
                if ( c.m_reverseLight > 0 ) rlColor4ub( 255, 250, 235, 255 );
                else rlColor4ub( 255, 40, 20, 255 );
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
    if ( m_stickerDirty ) buildSticker();
    if ( m_track ) shadowPass( w );
    BeginTextureMode( m_hdr );
    Color fog = m_venue ? m_venue->m_fog : Color{ 30, 30, 36, 255 };
    ClearBackground( fog );
    // The near plane well away from the eye: raylib's 0.01 m left ~3 cm depth steps at 70 m, and the road's layers
    // (road, patches, decals, a centimetre apart) flickered through each other.
    rlSetClipPlanes( std::max( 1.0, h * 0.25 ), 1200.0 );
    BeginMode3D( m_cam );
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
    rlSetClipPlanes( 0.3, 200.0 );
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
