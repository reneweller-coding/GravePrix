/**
 * @file People.cpp
 * @brief The skinned people: bodies, outfits, acts, CPU skinning into a mesh per figure.
 * @author Rene Weller
 */
#include "People.h"

#include <raymath.h>

#include <algorithm>
#include <map>
#include <tuple>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace grave
{

namespace
{

Color rgb( int r, int g, int b ) { return { (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 }; }

/** @brief The outfits: the podium's two, the people by the road (after the Forge's fig_ models), fans. */
std::vector<Outfit> outfits()
{
    std::vector<Outfit> v;
    auto                add = [&]( const char *name, const char *body, bool female ) -> Outfit & {
        Outfit o;
        o.m_name = name;
        o.m_body = body;
        o.m_female = female;
        v.push_back( o );
        return v.back();
    };
    {
        // Mortimer Graves: the black suit, white shirt and tie, black gloves of the look; grey hair under the top hat.
        Outfit &o = add( "graves", "graves", false );
        o.m_hair = rgb( 150, 148, 146 );
        o.m_hat = "_p3_tophat";
        o.m_excited = PersonAct::CLAP;
    }
    {
        // Ricky: a loud red jacket over his shirt and jeans, the microphone.
        Outfit &o = add( "ricky", "ricky", false );
        o.m_top = rgb( 204, 38, 28 );
        o.m_tintTop = true;
        o.m_hair = rgb( 38, 24, 14 );
        o.m_hand = "_p3_mic";
        o.m_excited = PersonAct::WAVE;
    }
    {
        Outfit &o = add( "marshal", "overalls", false );
        o.m_legs = rgb( 236, 106, 14 );
        o.m_tintLegs = true;
        o.m_stripes = true;
        o.m_hat = "_p3_cap";
        o.m_hand = "flag_yellow";
        o.m_excited = PersonAct::FLAG;
    }
    {
        Outfit &o = add( "mechanic", "overalls", false );
        o.m_legs = rgb( 42, 76, 142 );
        o.m_tintLegs = true;
        o.m_skin = rgb( 200, 144, 112 );
        o.m_tintSkin = true;
        o.m_hat = "_p3_cap_red";
        o.m_excited = PersonAct::CHEER;
    }
    {
        Outfit &o = add( "worker", "overalls", false );
        o.m_legs = rgb( 232, 200, 20 );
        o.m_tintLegs = true;
        o.m_stripes = true;
        o.m_skin = rgb( 154, 106, 74 );
        o.m_tintSkin = true;
        o.m_hat = "_p3_hardhat";
        o.m_excited = PersonAct::CHEER;
    }
    {
        Outfit &o = add( "undertaker", "suit", false );
        o.m_other = "widow";
        o.m_hair = rgb( 70, 60, 54 );
        o.m_hat = "_p3_tophat";
        o.m_excited = PersonAct::IDLE;
    }
    {
        Outfit &o = add( "tramp", "tramp", false );
        o.m_top = rgb( 92, 74, 50 );
        o.m_legs = rgb( 58, 50, 38 );
        o.m_tintTop = o.m_tintLegs = true;
        o.m_hair = rgb( 110, 100, 90 );
        o.m_hat = "_p3_beanie";
        o.m_excited = PersonAct::WAVE;
    }
    {
        Outfit &o = add( "widow", "widow", true );
        o.m_other = "suit";
        o.m_top = o.m_legs = rgb( 24, 22, 27 );
        o.m_tintTop = o.m_tintLegs = true;
        o.m_hair = rgb( 16, 14, 14 );
        o.m_hat = "_p3_widowhat";
        o.m_hand = "_p3_umbrella";
        o.m_excited = PersonAct::IDLE;
    }
    const Color teams[] = { rgb( 200, 34, 30 ), rgb( 40, 90, 200 ), rgb( 60, 160, 70 ), rgb( 230, 190, 40 ) };
    const char *names[] = { "fan_red", "fan_blue", "fan_green", "fan_gold" };
    for ( int i = 0; i < 4; ++i )
    {
        bool    female = i % 2 == 1;
        Outfit &o = add( names[i], female ? "fan_female" : "fan_male", female );
        o.m_other = female ? "fan_male" : "fan_female";
        o.m_top = teams[i];
        o.m_tintTop = true;
        if ( i == 2 )
        {
            o.m_skin = rgb( 150, 100, 70 );
            o.m_tintSkin = true;
        }
        o.m_hair = i == 3 ? rgb( 180, 140, 70 ) : rgb( 40, 28, 20 );
        o.m_excited = PersonAct::CHEER;
    }
    return v;
}

/** @return a colour's brightness */
float lum( float r, float g, float b ) { return 0.3f * r + 0.59f * g + 0.11f * b; }

/** @brief A model's place: its +y along a direction, at a point (the Quaternius body's space). */
Matrix along( Vector3 at, Vector3 dir, Vector3 side )
{
    Vector3 y = Vector3Normalize( dir );
    Vector3 x = Vector3CrossProduct( y, side );
    if ( Vector3Length( x ) < 1e-4f ) x = Vector3CrossProduct( y, { 0, 0, 1 } );
    x = Vector3Normalize( x );
    Vector3 z = Vector3CrossProduct( x, y );
    Matrix  m = MatrixIdentity();
    m.m0 = x.x, m.m1 = x.y, m.m2 = x.z;
    m.m4 = y.x, m.m5 = y.y, m.m6 = y.z;
    m.m8 = z.x, m.m9 = z.y, m.m10 = z.z;
    m.m12 = at.x, m.m13 = at.y, m.m14 = at.z;
    return m;
}

}   // namespace

People &people()
{
    static People p;
    return p;
}

bool People::loadBody( Body &b, const std::string &file, const std::string &dir )
{
    if ( !FileExists( file.c_str() ) ) return false;
    if ( !b.m_skin.load( file, file ) ) return false;   // the look carries its own A_TPose
    b.m_skin.addClips( dir + "/anims1.glb", { "Idle_Loop", "Idle_Talking_Loop", "Death01" } );
    b.m_skin.addClips( dir + "/anims2.glb", { "Idle_FoldArms_Loop", "LayToIdle" } );
    b.m_idle = b.m_skin.clip( "Idle_Loop" );
    b.m_idle2 = b.m_skin.clip( "Idle_FoldArms_Loop" );
    b.m_talk = b.m_skin.clip( "Idle_Talking_Loop" );
    b.m_fall = b.m_skin.clip( "Death01" );
    b.m_rise = b.m_skin.clip( "LayToIdle" );
    if ( b.m_idle < 0 ) return false;
    b.m_head = b.m_skin.bone( "Head" );
    b.m_chest = b.m_skin.bone( "spine_03" );
    b.m_upperL = b.m_skin.bone( "upperarm_l" );
    b.m_upperR = b.m_skin.bone( "upperarm_r" );
    b.m_lowerL = b.m_skin.bone( "lowerarm_l" );
    b.m_lowerR = b.m_skin.bone( "lowerarm_r" );
    b.m_handL = b.m_skin.bone( "hand_l" );
    b.m_handR = b.m_skin.bone( "hand_r" );
    const char *legs[3][2] = { { "thigh_l", "thigh_r" }, { "calf_l", "calf_r" }, { "foot_l", "foot_r" } };
    for ( int k = 0; k < 2; ++k )
    {
        b.m_thigh[k] = b.m_skin.bone( legs[0][k] );
        b.m_calf[k] = b.m_skin.bone( legs[1][k] );
        b.m_foot[k] = b.m_skin.bone( legs[2][k] );
    }
    // The fingers, parents first (by their depth in the rig).
    std::vector<std::pair<int, int>> fingers;
    for ( int i = 0; i < b.m_skin.bones(); ++i )
    {
        const std::string &nm = b.m_skin.name( i );
        bool finger = false;
        for ( const char *f : { "index", "middle", "ring", "pinky", "thumb" } )
            finger = finger || nm.rfind( f, 0 ) == 0;
        if ( !finger || nm.find( "leaf" ) != std::string::npos ) continue;
        int depth = 0;
        for ( int p = b.m_skin.parent( i ); p >= 0; p = b.m_skin.parent( p ) ) ++depth;
        fingers.push_back( { depth, i } );
    }
    std::sort( fingers.begin(), fingers.end() );
    for ( const auto &f : fingers ) b.m_fingers.push_back( f.second );
    const Mesh &m = b.m_skin.m_body.meshes[0];
    if ( !m.boneIds || !m.boneWeights || !m.indices || !m.colors ) return false;
    int n = m.vertexCount;
    b.m_pos.assign( m.vertices, m.vertices + n * 3 );
    b.m_nrm.assign( m.normals, m.normals + n * 3 );
    b.m_ids.assign( m.boneIds, m.boneIds + n * 4 );
    b.m_wts.assign( m.boneWeights, m.boneWeights + n * 4 );
    b.m_idx.assign( m.indices, m.indices + m.triangleCount * 3 );
    // The colours and, in their alpha, the roles (255 kept, 200 top, 170 tape, 140 legs, 100 hair, 50 skin).
    b.m_col.assign( m.colors, m.colors + n * 4 );
    b.m_role.resize( (size_t)n );
    float rsum[6] = {}, rcnt[6] = {};
    for ( int i = 0; i < n; ++i )
    {
        int a = m.colors[i * 4 + 3];
        int r = a >= 228 ? 0 : a >= 185 ? 1 : a >= 155 ? 5 : a >= 120 ? 2 : a >= 75 ? 3 : 4;
        b.m_role[(size_t)i] = (unsigned char)r;
        rsum[r] += lum( m.colors[i * 4], m.colors[i * 4 + 1], m.colors[i * 4 + 2] );
        rcnt[r] += 1;
    }
    for ( int r = 0; r < 6; ++r ) b.m_lum[r] = rcnt[r] > 0 ? std::max( 8.0f, rsum[r] / rcnt[r] ) : 128.0f;
    b.m_dominant.resize( n );
    b.m_headTop = 0.0f;
    for ( int i = 0; i < n; ++i )
    {
        int best = 0;
        for ( int k = 1; k < 4; ++k )
            if ( b.m_wts[i * 4 + k] > b.m_wts[i * 4 + best] ) best = k;
        b.m_dominant[i] = b.m_ids[i * 4 + best];
        b.m_headTop = std::max( b.m_headTop, b.m_pos[i * 3 + 1] );
    }
    // The head's middle in the rest pose (the mesh's vertices are in that pose): where hats sit and the hair starts.
    b.m_headAt = { 0, b.m_headTop - 0.12f, 0 };
    float sx = 0, sy = 0, sz = 0, cnt = 0;
    for ( int i = 0; i < n; ++i )
        if ( b.m_dominant[i] == b.m_head )
        {
            sx += b.m_pos[i * 3], sy += b.m_pos[i * 3 + 1], sz += b.m_pos[i * 3 + 2], cnt += 1;
        }
    if ( cnt > 0 ) b.m_headAt = { sx / cnt, sy / cnt, sz / cnt };
    return true;
}

bool People::ready()
{
    if ( m_state != 0 ) return m_state > 0;
    m_state = -1;
    std::string dir = std::string( GetApplicationDirectory() ) + "assets/models/people3d";
#ifdef GRAVE_ROOT
    if ( !FileExists( ( dir + "/look_graves.glb" ).c_str() ) ) dir = std::string( GRAVE_ROOT ) + "/assets/models/people3d";
#endif
    m_outfits = outfits();
    for ( const Outfit &o : m_outfits )
        for ( const std::string &name : { o.m_body, o.m_other } )
            if ( !name.empty() && !m_body.count( name ) && !loadBody( m_body[name], dir + "/look_" + name + ".glb", dir ) )
            {
                m_body.clear();
                return false;
            }
    m_state = 1;
    return true;
}

int People::outfit( const std::string &name ) const
{
    for ( size_t i = 0; i < m_outfits.size(); ++i )
        if ( m_outfits[i].m_name == name ) return (int)i;
    return -1;
}

int People::outfitLike( int base, Color top, Color legs, bool female, bool umbrella )
{
    if ( !ready() || base < 0 || base >= (int)m_outfits.size() ) return -1;
    char key[96];
    snprintf( key, sizeof key, "%s/%02x%02x%02x/%02x%02x%02x/%d%d", m_outfits[(size_t)base].m_name.c_str(), top.r, top.g,
              top.b, legs.r, legs.g, legs.b, female ? 1 : 0, umbrella ? 1 : 0 );
    int o = outfit( key );
    if ( o >= 0 ) return o;
    Outfit n = m_outfits[(size_t)base];
    n.m_name = key;
    n.m_top = top;
    n.m_legs = legs;
    n.m_tintTop = n.m_tintLegs = true;
    if ( n.m_female != female && !n.m_other.empty() )
    {
        n.m_female = female;
        std::swap( n.m_body, n.m_other );
        if ( !female && n.m_hat == "_p3_widowhat" ) n.m_hat = "_p3_tophat";
        if ( female && n.m_hat == "_p3_tophat" ) n.m_hat = "_p3_widowhat";
    }
    // A crowd of one face otherwise: the skin and the hair by the colours.
    const Color skins[4] = { rgb( 232, 196, 170 ), rgb( 214, 168, 130 ), rgb( 160, 110, 78 ), rgb( 104, 70, 50 ) };
    const Color hairs[4] = { rgb( 40, 28, 20 ), rgb( 96, 64, 36 ), rgb( 20, 18, 18 ), rgb( 170, 132, 72 ) };
    unsigned h = top.r * 7u + top.g * 3u + legs.b * 13u + legs.r * 5u;
    n.m_skin = skins[h % 4];
    n.m_tintSkin = h % 4 != 0;
    n.m_hair = hairs[( h / 4 ) % 4];
    if ( umbrella ) n.m_hand = "_p3_umbrella";
    else if ( n.m_hand == "_p3_umbrella" ) n.m_hand.clear();
    m_outfits.push_back( n );
    return (int)m_outfits.size() - 1;
}

void People::colour( const Body &b, const Outfit &o, unsigned char *out ) const
{
    int n = (int)b.m_role.size();
    // A tint keeps the texture's folds and seams: the colour times the vertex's brightness against its role's mean.
    auto tint = [&]( int i, Color t, float mean ) {
        float l = lum( b.m_col[i * 4], b.m_col[i * 4 + 1], b.m_col[i * 4 + 2] ) / mean;
        l = std::min( 1.45f, std::max( 0.5f, l ) );
        return Color{ (unsigned char)std::min( 255.0f, t.r * l ), (unsigned char)std::min( 255.0f, t.g * l ),
                      (unsigned char)std::min( 255.0f, t.b * l ), 255 };
    };
    for ( int i = 0; i < n; ++i )
    {
        int           r = b.m_role[(size_t)i];
        Color         c = { b.m_col[i * 4], b.m_col[i * 4 + 1], b.m_col[i * 4 + 2], 255 };
        unsigned char glow = 0;
        if ( r == 5 && o.m_stripes )
        {
            c = rgb( 236, 236, 226 );   // the reflective tape lights up in the headlights
            glow = 50;
        }
        else if ( r == 5 ) r = 2;      // else it is the legs' cloth
        if ( r == 1 && o.m_tintTop ) c = tint( i, o.m_top, b.m_lum[1] );
        else if ( r == 2 && o.m_tintLegs ) c = tint( i, o.m_legs, b.m_lum[2] );
        else if ( r == 3 && o.m_tintHair ) c = tint( i, o.m_hair, b.m_lum[3] );
        else if ( r == 4 && o.m_tintSkin ) c = tint( i, o.m_skin, b.m_lum[4] );
        out[i * 4] = c.r, out[i * 4 + 1] = c.g, out[i * 4 + 2] = c.b, out[i * 4 + 3] = glow;
    }
}

void People::stand( const Body &b, std::vector<Transform> &pose ) const
{
    for ( int k = 0; k < 2; ++k )
        if ( b.m_thigh[k] < 0 || b.m_calf[k] < 0 || b.m_foot[k] < 0 ) return;
    const SkinModel &sk = b.m_skin;
    Vector3 hipL = pose[b.m_thigh[0]].translation, hipR = pose[b.m_thigh[1]].translation;
    float   hipW = Vector3Distance( hipL, hipR );
    Vector3 side = Vector3Normalize( { hipL.x - hipR.x, 0.0f, hipL.z - hipR.z } );   // towards the left leg
    Vector3 fwd = Vector3CrossProduct( side, { 0, 1, 0 } );
    Vector3 mid = Vector3Lerp( hipL, hipR, 0.5f );
    Vector3 target[2];
    Quaternion flat[2];
    float   rise = 1e9f;
    for ( int k = 0; k < 2; ++k )
    {
        Vector3 a = pose[b.m_thigh[k]].translation, kn = pose[b.m_calf[k]].translation, an = pose[b.m_foot[k]].translation;
        float   len = Vector3Distance( a, kn ) + Vector3Distance( kn, an );
        flat[k] = pose[b.m_foot[k]].rotation;
        // The ankle drawn in: 0.62 hip-widths out from the middle, most of its step forward or back kept, on the
        // ground where it was.
        Vector3 rel = Vector3Subtract( an, mid );
        float   ahead = Vector3DotProduct( rel, fwd ), out = ( k == 0 ? 1.0f : -1.0f ) * 0.62f * hipW;
        Vector3 t = Vector3Add( mid, Vector3Add( Vector3Scale( side, out ), Vector3Scale( fwd, ahead * 0.7f ) ) );
        t.y = an.y;
        target[k] = t;
        // How far the hips may rise before this leg is nearly straight (the knee at about 170 degrees).
        float h = Vector3Length( { a.x - t.x, 0.0f, a.z - t.z } ), l = 0.996f * len;
        rise = std::min( rise, std::sqrt( std::max( 0.0f, l * l - h * h ) ) - ( a.y - t.y ) );
    }
    rise = std::min( 0.12f, std::max( 0.0f, rise ) );
    for ( Transform &tr : pose ) tr.translation.y += rise;
    for ( int k = 0; k < 2; ++k )
    {
        // The knee forward and a little out; the foot as flat as the clip had it.
        sk.reach( pose, b.m_thigh[k], b.m_calf[k], b.m_foot[k], target[k],
                  Vector3Add( fwd, Vector3Scale( side, k == 0 ? 0.15f : -0.15f ) ) );
        int f = b.m_foot[k];
        sk.turn( pose, f, QuaternionMultiply( flat[k], QuaternionInvert( pose[f].rotation ) ), pose[f].translation );
    }
}

void People::relaxArm( const Body &b, std::vector<Transform> &pose, bool right ) const
{
    int up = right ? b.m_upperR : b.m_upperL, lo = right ? b.m_lowerR : b.m_lowerL, hand = right ? b.m_handR : b.m_handL;
    if ( up < 0 || lo < 0 || hand < 0 || b.m_upperL < 0 || b.m_upperR < 0 ) return;
    Vector3 l = pose[b.m_upperL].translation, r = pose[b.m_upperR].translation;
    Vector3 side = Vector3Normalize( { l.x - r.x, 0.0f, l.z - r.z } ), fwd = Vector3CrossProduct( side, { 0, 1, 0 } );
    Vector3 out = right ? Vector3Negate( side ) : side;
    Vector3 sh = pose[up].translation, el = pose[lo].translation, ha = pose[hand].translation;
    float   len = Vector3Distance( sh, el ) + Vector3Distance( el, ha );
    // Hanging beside the thigh, the elbow a little back; some of the clip's own swing kept.
    Vector3 rest = Vector3Add( sh, Vector3Add( { 0, -0.95f * len, 0 }, Vector3Add( Vector3Scale( out, 0.08f * len ), Vector3Scale( fwd, 0.05f * len ) ) ) );
    b.m_skin.reach( pose, up, lo, hand, Vector3Lerp( ha, rest, 0.75f ), Vector3Add( Vector3Negate( fwd ), Vector3Scale( out, 0.4f ) ) );
}

void People::relaxHands( const Body &b, std::vector<Transform> &pose ) const
{
    // The library's hands are fists: each finger joint half way back to the T-pose's straight one.
    const SkinModel &sk = b.m_skin;
    for ( int i : b.m_fingers )
    {
        int p = sk.parent( i );
        if ( p < 0 ) continue;
        Quaternion have = QuaternionMultiply( QuaternionInvert( pose[p].rotation ), pose[i].rotation );
        Quaternion ref = QuaternionMultiply( QuaternionInvert( sk.ref( p ).rotation ), sk.ref( i ).rotation );
        Quaternion want = QuaternionMultiply( pose[p].rotation, QuaternionSlerp( have, ref, 0.55f ) );
        sk.turn( pose, i, QuaternionMultiply( want, QuaternionInvert( pose[i].rotation ) ), pose[i].translation );
    }
}

void People::posed( Body &b, PersonAct act, float t, float seed, std::vector<Transform> &pose, PersonPose &out,
                    const Outfit &of ) const
{
    SkinModel &sk = b.m_skin;
    SkinState  s;
    s.m_clip = b.m_idle;
    s.m_time = t + seed * 17.0f;
    if ( act == PersonAct::IDLE && of.m_hand.empty() && seed > 0.62f && b.m_idle2 >= 0 ) s.m_clip = b.m_idle2;
    if ( act == PersonAct::TALK && b.m_talk >= 0 ) s.m_clip = b.m_talk;
    if ( act == PersonAct::DIVE && b.m_fall >= 0 && b.m_rise >= 0 )
    {
        // Down (the fall's clip squeezed into a second and a half), flat until 1.6 s, up again by 2.6 s.
        float fallLen = sk.length( b.m_fall ), riseLen = sk.length( b.m_rise );
        if ( t < 1.6f )
        {
            s.m_clip = b.m_fall;
            s.m_time = std::min( t / 1.5f, 1.0f ) * fallLen;
        }
        else
        {
            s.m_clip = b.m_rise;
            s.m_time = std::min( ( t - 1.6f ) / 1.0f, 1.0f ) * riseLen;
            s.m_prev = b.m_fall;
            s.m_prevTime = fallLen;
            s.m_fade = std::min( 1.0f, ( t - 1.6f ) / 0.2f );
        }
    }
    sk.posed( s, pose );
    if ( b.m_upperR < 0 || b.m_handR < 0 || b.m_chest < 0 || b.m_head < 0 ) return;
    // The library's idles stand like a fighter (the feet three hip-widths apart, the knees bent, the elbows out,
    // fists): the feet drawn in under the hips with the body raised until the knees are nearly straight, the free
    // arms hanging, the fingers half open. Folded arms stay folded.
    if ( act != PersonAct::DIVE )
    {
        stand( b, pose );
        bool folded = s.m_clip >= 0 && s.m_clip == b.m_idle2;
        if ( !folded && ( act == PersonAct::IDLE || act == PersonAct::WAVE || act == PersonAct::FLAG ) ) relaxArm( b, pose, false );
        if ( !folded && act == PersonAct::IDLE ) relaxArm( b, pose, true );
    }
    relaxHands( b, pose );
    Vector3 chest = pose[b.m_chest].translation, head = pose[b.m_head].translation;
    Vector3 shR = pose[b.m_upperR].translation, shL = pose[b.m_upperL].translation;
    float   outR = shR.x > shL.x ? 1.0f : -1.0f, outL = -outR;
    float   ph = seed * 6.28f;
    auto    armR = [&]( Vector3 target, Vector3 bend ) { sk.reach( pose, b.m_upperR, b.m_lowerR, b.m_handR, target, bend ); };
    auto    armL = [&]( Vector3 target, Vector3 bend ) { sk.reach( pose, b.m_upperL, b.m_lowerL, b.m_handL, target, bend ); };
    bool    upright = of.m_hand == "_p3_umbrella";
    switch ( act )
    {
    case PersonAct::CHEER:
    {
        float pump = 0.5f + 0.5f * std::sin( t * 9.0f + ph );
        float hop = 0.07f * std::fabs( std::sin( t * 4.5f + ph ) );
        for ( Transform &tr : pose ) tr.translation.y += hop;
        shR.y += hop, shL.y += hop;
        armR( Vector3Add( shR, { outR * 0.24f, 0.4f + 0.14f * pump, 0.1f } ), { outR, 0.0f, -0.6f } );
        armL( Vector3Add( shL, { outL * 0.24f, 0.4f + 0.14f * ( 1.0f - pump ), 0.1f } ), { outL, 0.0f, -0.6f } );
        break;
    }
    case PersonAct::CLAP:
    {
        float   f = std::fabs( std::sin( t * 8.0f + ph ) );
        Vector3 c = Vector3Add( chest, { 0, 0.04f, 0.34f } );
        armR( Vector3Add( c, { outR * ( 0.03f + 0.13f * f ), 0, 0 } ), { outR, -1.0f, -0.3f } );
        armL( Vector3Add( c, { outL * ( 0.03f + 0.13f * f ), 0, 0 } ), { outL, -1.0f, -0.3f } );
        break;
    }
    case PersonAct::WAVE:
        armR( Vector3Add( shR, { outR * 0.3f + 0.16f * std::sin( t * 7.0f + ph ), 0.48f, 0.08f } ), { outR, -0.5f, -0.6f } );
        break;
    case PersonAct::FLAG:
        armR( Vector3Add( shR, { outR * 0.12f + 0.42f * std::sin( t * 5.0f + ph ), 0.5f - 0.1f * std::fabs( std::cos( t * 5.0f + ph ) ), 0.15f } ),
              { outR, -0.3f, -0.8f } );
        break;
    case PersonAct::TALK:
        armR( Vector3Add( head, { outR * 0.05f, -0.2f, 0.2f } ), { outR, -1.0f, -0.2f } );
        break;
    default: break;
    }
    if ( upright && act != PersonAct::DIVE ) armR( Vector3Add( shR, { outR * 0.06f, -0.22f, 0.3f } ), { outR, -1.0f, 0.0f } );
    // A held thing in the right fist; the umbrella stays upright, a flag leans out along the arm.
    Vector3 hand = pose[b.m_handR].translation, elbow = pose[b.m_lowerR].translation;
    Vector3 fore = Vector3Normalize( Vector3Subtract( hand, elbow ) );
    Vector3 fist = Vector3Add( hand, Vector3Scale( fore, 0.07f ) );
    Vector3 dir = fore;
    if ( upright ) dir = { 0, 1, 0 };
    else if ( of.m_hand == "flag_yellow" ) dir = Vector3Normalize( Vector3Add( fore, { 0, 1.1f, 0 } ) );
    else if ( act == PersonAct::TALK ) dir = Vector3Normalize( Vector3Subtract( Vector3Add( head, { 0, 0.02f, 0.12f } ), fist ) );
    float grip = of.m_hand == "flag_yellow" ? 0.3f : upright ? 0.2f : 0.02f;
    Matrix hm = along( fist, dir, { 0, 0, 1 } );
    out.m_hand = MatrixMultiply( MatrixTranslate( 0, -grip, 0 ), hm );
    out.m_hat = MatrixTranslate( 0, b.m_headTop, b.m_headAt.z );
}

const Mesh *People::pose( int slot, int o, PersonAct act, float t, float seed, PersonPose &out )
{
    if ( !ready() || o < 0 || o >= (int)m_outfits.size() ) return nullptr;
    const Outfit &of = m_outfits[(size_t)o];
    Body         &b = body( of );
    Slot         &s = m_slots[slot];
    int           n = (int)b.m_dominant.size();
    if ( s.m_outfit != o )
    {
        if ( s.m_mesh.vboId ) UnloadMesh( s.m_mesh );
        s.m_mesh = {};
        Mesh &m = s.m_mesh;
        m.vertexCount = n;
        m.triangleCount = (int)b.m_idx.size() / 3;
        m.vertices = (float *)MemAlloc( n * 3 * sizeof( float ) );
        m.normals = (float *)MemAlloc( n * 3 * sizeof( float ) );
        m.texcoords = (float *)MemAlloc( n * 2 * sizeof( float ) );
        m.colors = (unsigned char *)MemAlloc( n * 4 );
        m.indices = (unsigned short *)MemAlloc( (unsigned int)b.m_idx.size() * sizeof( unsigned short ) );
        memcpy( m.indices, b.m_idx.data(), b.m_idx.size() * sizeof( unsigned short ) );
        colour( b, of, m.colors );
        s.m_outfit = o;
        s.m_act = -1;
    }
    if ( s.m_act == (int)act && s.m_t == t && s.m_seed == seed && s.m_mesh.vboId )
    {
        out = s.m_pose;
        return &s.m_mesh;
    }
    static std::vector<Transform> pose;
    static std::vector<Matrix>    mats;
    PersonPose                    pp;
    posed( b, act, t, seed, pose, pp, of );
    b.m_skin.skin( pose, mats );
    // Skinned on the CPU, turned to face -z as the Forge's people do.
    Mesh &m = s.m_mesh;
    for ( int i = 0; i < n; ++i )
    {
        const float *p = &b.m_pos[i * 3], *nr = &b.m_nrm[i * 3];
        float        px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
        for ( int k = 0; k < 4; ++k )
        {
            float w = b.m_wts[i * 4 + k];
            if ( w <= 0.0f ) continue;
            const Matrix &M = mats[b.m_ids[i * 4 + k]];
            px += w * ( M.m0 * p[0] + M.m4 * p[1] + M.m8 * p[2] + M.m12 );
            py += w * ( M.m1 * p[0] + M.m5 * p[1] + M.m9 * p[2] + M.m13 );
            pz += w * ( M.m2 * p[0] + M.m6 * p[1] + M.m10 * p[2] + M.m14 );
            nx += w * ( M.m0 * nr[0] + M.m4 * nr[1] + M.m8 * nr[2] );
            ny += w * ( M.m1 * nr[0] + M.m5 * nr[1] + M.m9 * nr[2] );
            nz += w * ( M.m2 * nr[0] + M.m6 * nr[1] + M.m10 * nr[2] );
        }
        float l = std::sqrt( nx * nx + ny * ny + nz * nz );
        if ( l > 1e-6f ) nx /= l, ny /= l, nz /= l;
        m.vertices[i * 3] = -px, m.vertices[i * 3 + 1] = py, m.vertices[i * 3 + 2] = -pz;
        m.normals[i * 3] = -nx, m.normals[i * 3 + 1] = ny, m.normals[i * 3 + 2] = -nz;
    }
    if ( !m.vboId )
        UploadMesh( &m, true );
    else
    {
        UpdateMeshBuffer( m, 0, m.vertices, n * 3 * sizeof( float ), 0 );
        UpdateMeshBuffer( m, 2, m.normals, n * 3 * sizeof( float ), 0 );
    }
    // The hat rides on the head bone, the held thing was placed in the pose; both turned as the body.
    Matrix flip = MatrixRotateY( PI );
    pp.m_hat = MatrixMultiply( MatrixMultiply( pp.m_hat, mats[b.m_head] ), flip );
    pp.m_hand = MatrixMultiply( pp.m_hand, flip );
    s.m_pose = pp;
    s.m_act = (int)act;
    s.m_t = t;
    s.m_seed = seed;
    out = pp;
    return &s.m_mesh;
}

void People::unload()
{
    for ( auto &kv : m_slots )
        if ( kv.second.m_mesh.vboId ) UnloadMesh( kv.second.m_mesh );
    m_slots.clear();
}

}   // namespace grave
