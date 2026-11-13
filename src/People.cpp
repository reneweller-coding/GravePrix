/**
 * @file People.cpp
 * @brief The skinned people: bodies, outfits, acts, CPU skinning into a mesh per figure.
 * @author Rene Weller
 */
#include "People.h"

#include <raymath.h>

#include <algorithm>
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
    auto                add = [&]( const char *name, bool female, bool hi ) -> Outfit & {
        Outfit o;
        o.m_name = name;
        o.m_female = female;
        o.m_hi = hi;
        v.push_back( o );
        return v.back();
    };
    {
        Outfit &o = add( "graves", false, true );
        o.m_skin = rgb( 226, 206, 192 );
        o.m_hair = rgb( 150, 148, 146 );
        o.m_top = o.m_legs = rgb( 20, 20, 23 );
        o.m_shoes = rgb( 10, 10, 10 );
        o.m_hands = rgb( 34, 32, 34 );   // gloves
        o.m_trim = rgb( 140, 18, 22 );
        o.m_trimKind = 1;
        o.m_coat = true;
        o.m_hat = "_p3_tophat";
        o.m_excited = PersonAct::CLAP;
    }
    {
        Outfit &o = add( "ricky", false, true );
        o.m_skin = o.m_hands = rgb( 222, 172, 136 );
        o.m_hair = rgb( 38, 24, 14 );
        o.m_top = rgb( 204, 38, 28 );
        o.m_legs = rgb( 30, 30, 36 );
        o.m_shoes = rgb( 120, 68, 30 );
        o.m_trim = rgb( 222, 180, 60 );
        o.m_trimKind = 1;
        o.m_hand = "_p3_mic";
        o.m_excited = PersonAct::WAVE;
    }
    {
        Outfit &o = add( "marshal", false, false );
        o.m_top = rgb( 230, 104, 14 );
        o.m_legs = rgb( 34, 34, 38 );
        o.m_trim = rgb( 236, 236, 226 );
        o.m_trimKind = 2;
        o.m_coat = true;
        o.m_hat = "_p3_cap";
        o.m_hand = "flag_yellow";
        o.m_excited = PersonAct::FLAG;
    }
    {
        Outfit &o = add( "mechanic", false, false );
        o.m_skin = o.m_hands = rgb( 200, 144, 112 );
        o.m_top = o.m_legs = rgb( 42, 76, 142 );
        o.m_shoes = rgb( 30, 26, 22 );
        o.m_hat = "_p3_cap_red";
        o.m_excited = PersonAct::CHEER;
    }
    {
        Outfit &o = add( "worker", false, false );
        o.m_skin = o.m_hands = rgb( 154, 106, 74 );
        o.m_top = rgb( 226, 194, 18 );
        o.m_legs = rgb( 58, 58, 66 );
        o.m_trim = rgb( 236, 236, 226 );
        o.m_trimKind = 2;
        o.m_hat = "_p3_hardhat";
        o.m_excited = PersonAct::CHEER;
    }
    {
        Outfit &o = add( "undertaker", false, false );
        o.m_skin = o.m_hands = rgb( 236, 206, 180 );
        o.m_top = o.m_legs = rgb( 20, 20, 23 );
        o.m_trim = rgb( 20, 20, 23 );
        o.m_trimKind = 1;
        o.m_coat = true;
        o.m_hat = "_p3_tophat";
        o.m_excited = PersonAct::IDLE;
    }
    {
        Outfit &o = add( "tramp", false, false );
        o.m_skin = o.m_hands = rgb( 196, 142, 110 );
        o.m_hair = rgb( 110, 100, 90 );
        o.m_top = rgb( 92, 74, 50 );
        o.m_legs = rgb( 58, 50, 38 );
        o.m_shoes = rgb( 50, 40, 30 );
        o.m_coat = true;
        o.m_hat = "_p3_beanie";
        o.m_excited = PersonAct::WAVE;
    }
    {
        Outfit &o = add( "widow", true, false );
        o.m_skin = o.m_hands = rgb( 238, 210, 190 );
        o.m_hair = rgb( 16, 14, 14 );
        o.m_top = o.m_legs = rgb( 24, 22, 27 );
        o.m_shoes = rgb( 12, 12, 12 );
        o.m_dress = true;
        o.m_longHair = true;
        o.m_hat = "_p3_widowhat";
        o.m_hand = "_p3_umbrella";
        o.m_excited = PersonAct::IDLE;
    }
    const Color teams[] = { rgb( 200, 34, 30 ), rgb( 40, 90, 200 ), rgb( 60, 160, 70 ), rgb( 230, 190, 40 ) };
    const char *names[] = { "fan_red", "fan_blue", "fan_green", "fan_gold" };
    for ( int i = 0; i < 4; ++i )
    {
        Outfit &o = add( names[i], i % 2 == 1, false );
        o.m_top = teams[i];
        o.m_legs = rgb( 48, 58, 92 );
        o.m_skin = o.m_hands = i == 2 ? rgb( 150, 100, 70 ) : rgb( 226, 186, 156 );
        o.m_hair = i == 3 ? rgb( 180, 140, 70 ) : rgb( 40, 28, 20 );
        o.m_longHair = i % 2 == 1;
        o.m_excited = PersonAct::CHEER;
    }
    return v;
}

/** @brief A body part from a bone's name: 0 head, 1 hands, 2 torso and arms, 3 legs, 4 feet, 5 pelvis, 6 neck. */
int part( const std::string &n )
{
    if ( n == "Head" || n.rfind( "eye", 0 ) == 0 || n.rfind( "jaw", 0 ) == 0 ) return 0;
    if ( n.rfind( "neck", 0 ) == 0 ) return 6;
    if ( n.rfind( "hand", 0 ) == 0 || n.rfind( "index", 0 ) == 0 || n.rfind( "middle", 0 ) == 0 || n.rfind( "ring", 0 ) == 0 ||
         n.rfind( "pinky", 0 ) == 0 || n.rfind( "thumb", 0 ) == 0 )
        return 1;
    if ( n.rfind( "thigh", 0 ) == 0 || n.rfind( "calf", 0 ) == 0 ) return 3;
    if ( n.rfind( "foot", 0 ) == 0 || n.rfind( "ball", 0 ) == 0 ) return 4;
    if ( n == "pelvis" || n == "root" ) return 5;
    return 2;
}

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
    if ( !b.m_skin.load( file, dir + "/anims1.glb" ) ) return false;
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
    const Mesh &m = b.m_skin.m_body.meshes[0];
    if ( !m.boneIds || !m.boneWeights || !m.indices ) return false;
    int n = m.vertexCount;
    b.m_pos.assign( m.vertices, m.vertices + n * 3 );
    b.m_nrm.assign( m.normals, m.normals + n * 3 );
    b.m_ids.assign( m.boneIds, m.boneIds + n * 4 );
    b.m_wts.assign( m.boneWeights, m.boneWeights + n * 4 );
    b.m_idx.assign( m.indices, m.indices + m.triangleCount * 3 );
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
    if ( !FileExists( ( dir + "/male_lo.glb" ).c_str() ) ) dir = std::string( GRAVE_ROOT ) + "/assets/models/people3d";
#endif
    const char *files[4] = { "male_lo", "male_hi", "female_lo", "female_hi" };
    for ( int i = 0; i < 4; ++i )
        if ( !loadBody( m_body[i], dir + "/" + files[i] + ".glb", dir ) ) return false;
    m_outfits = outfits();
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
    if ( n.m_female != female )
    {
        n.m_female = female;
        n.m_longHair = female;
        if ( !female && n.m_hat == "_p3_widowhat" ) n.m_hat = "_p3_tophat";
    }
    if ( umbrella ) n.m_hand = "_p3_umbrella";
    else if ( n.m_hand == "_p3_umbrella" ) n.m_hand.clear();
    m_outfits.push_back( n );
    return (int)m_outfits.size() - 1;
}

void People::colour( const Body &b, const Outfit &o, unsigned char *out ) const
{
    const float H = b.m_headTop, k = H / 1.81f;
    const Vector3 hc = b.m_headAt;
    int n = (int)b.m_dominant.size();
    for ( int i = 0; i < n; ++i )
    {
        float x = b.m_pos[i * 3], y = b.m_pos[i * 3 + 1], z = b.m_pos[i * 3 + 2], nz = b.m_nrm[i * 3 + 2];
        int   pt = part( b.m_skin.name( b.m_dominant[i] ) );
        Color c = o.m_top;
        unsigned char glow = 0;
        switch ( pt )
        {
        case 0:
        case 6:
        {
            c = o.m_skin;
            bool top = y > H - 0.075f * k;
            bool back = z < hc.z - 0.035f * k && y > hc.y - 0.03f * k;
            bool longBack = o.m_longHair && z < hc.z - 0.01f * k && y > hc.y - 0.16f * k;
            if ( pt == 0 && ( top || back || longBack ) ) c = o.m_hair;
            if ( pt == 6 && longBack ) c = o.m_hair;
            break;
        }
        case 1: c = o.m_hands; break;
        case 4: c = o.m_shoes; break;
        default:
        {
            // The trunk and the legs: the waist by height (the bones' weights made a ragged edge at the hips).
            bool arm = std::fabs( x ) > 0.24f * k && y > 1.2f * k;
            float hem = o.m_dress ? 0.36f * k : o.m_coat ? 0.56f * k : 0.97f * k;
            c = ( arm || y > hem ) ? o.m_top : o.m_legs;
            if ( pt == 3 && !o.m_coat && !o.m_dress ) c = o.m_legs;
            break;
        }
        }
        if ( pt == 2 || pt == 6 )
        {
            if ( o.m_trimKind == 1 && nz > 0.35f && y > 1.24f * k && y < 1.52f * k && std::fabs( x ) < ( y - 1.24f * k ) * 0.55f )
                c = rgb( 236, 234, 228 );   // the shirt in the jacket's V
            if ( o.m_trimKind == 1 && nz > 0.35f && y > 1.08f * k && y < 1.47f * k && std::fabs( x ) < 0.024f * k ) c = o.m_trim;
            if ( o.m_trimKind == 2 && pt == 2 &&
                 ( ( y > 1.22f * k && y < 1.28f * k && std::fabs( x ) < 0.25f * k ) || ( std::fabs( x ) > 0.52f * k && std::fabs( x ) < 0.57f * k ) ) )
            {
                c = o.m_trim;
                glow = 50;
            }
        }
        if ( pt == 3 && o.m_trimKind == 2 && o.m_coat && y > 0.62f * k && y < 0.67f * k )
        {
            c = o.m_trim;
            glow = 50;
        }
        out[i * 4] = c.r, out[i * 4 + 1] = c.g, out[i * 4 + 2] = c.b, out[i * 4 + 3] = glow;
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
    Body         &b = m_body[( of.m_female ? 2 : 0 ) + ( of.m_hi ? 1 : 0 )];
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
