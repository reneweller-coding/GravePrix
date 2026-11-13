/**
 * @file Skin.cpp
 * @brief Skinned figures: loading, retargeting by bone name, sampling with crossfades, the skin matrices.
 * @author Rene Weller
 */
#include "Skin.h"

#include <raymath.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace grave
{

namespace
{

constexpr float FrameRate = 1000.0f / 17.0f;   ///< raylib samples glTF animations every 17 ms

/**
 * @brief An animation file's clips as raylib reads them, read once and kept while the game runs: every figure of every
 *        level read the two libraries again before, seconds at each level's start.
 * @param[in]  file   the file
 * @param[out] count  how many clips
 * @return the clips (nullptr if there are none)
 */
const ModelAnimation *animations( const std::string &file, int &count )
{
    static std::map<std::string, std::pair<ModelAnimation *, int>> files;
    auto                                                           it = files.find( file );
    if ( it == files.end() )
    {
        int             n = 0;
        ModelAnimation *anims = LoadModelAnimations( file.c_str(), &n );
        it = files.emplace( file, std::make_pair( anims, n ) ).first;
    }
    count = it->second.second;
    return it->second.first;
}

/** @brief The Rigify name (the first animation library's rig) of a bone of the Unreal-style rig, or "". */
std::string rigify( const std::string &n )
{
    static const std::map<std::string, std::string> trunk = { { "root", "root" },
                                                              { "pelvis", "DEF-hips" },
                                                              { "spine_01", "DEF-spine.001" },
                                                              { "spine_02", "DEF-spine.002" },
                                                              { "spine_03", "DEF-spine.003" },
                                                              { "neck_01", "DEF-neck" },
                                                              { "Head", "DEF-head" } };
    auto                                            t = trunk.find( n );
    if ( t != trunk.end() ) return t->second;
    if ( n.size() < 3 ) return "";
    std::string end = n.substr( n.size() - 2 ), side = end == "_l" ? ".L" : end == "_r" ? ".R" : "";
    if ( side.empty() ) return "";
    std::string                                     base = n.substr( 0, n.size() - 2 );
    static const std::map<std::string, std::string> limb = {
        { "clavicle", "DEF-shoulder" }, { "upperarm", "DEF-upper_arm" },
        { "lowerarm", "DEF-forearm" },  { "hand", "DEF-hand" },
        { "thigh", "DEF-thigh" },       { "calf", "DEF-shin" },
        { "foot", "DEF-foot" },         { "ball", "DEF-toe" }
    };
    auto l = limb.find( base );
    if ( l != limb.end() ) return l->second + side;
    // Fingers: index_01 is DEF-f_index.01, thumb_01 is DEF-thumb.01 (the leaf bones have no partner).
    size_t u = base.rfind( '_' );
    if ( u == std::string::npos ) return "";
    std::string finger = base.substr( 0, u ), joint = base.substr( u + 1 );
    if ( joint != "01" && joint != "02" && joint != "03" ) return "";
    if ( finger == "thumb" ) return "DEF-thumb." + joint + side;
    if ( finger == "index" || finger == "middle" || finger == "pinky" || finger == "ring" )
        return "DEF-f_" + finger + "." + joint + side;
    return "";
}

/** @brief A rig's own axes in its rest pose: across (left), up (hips to head), forward. */
Matrix basis( Vector3 hips, Vector3 head, Vector3 left, Vector3 right )
{
    Vector3 up = Vector3Normalize( Vector3Subtract( head, hips ) );
    Vector3 side = Vector3Subtract( left, right );
    side = Vector3Normalize( Vector3Subtract( side, Vector3Scale( up, Vector3DotProduct( side, up ) ) ) );
    Vector3 fwd = Vector3CrossProduct( side, up );
    Matrix  m = MatrixIdentity();
    m.m0 = side.x, m.m1 = side.y, m.m2 = side.z;
    m.m4 = up.x, m.m5 = up.y, m.m6 = up.z;
    m.m8 = fwd.x, m.m9 = fwd.y, m.m10 = fwd.z;
    return m;
}

Transform blend( const Transform &a, const Transform &b, float w )
{
    return { Vector3Lerp( a.translation, b.translation, w ), QuaternionSlerp( a.rotation, b.rotation, w ),
             Vector3Lerp( a.scale, b.scale, w ) };
}

}   // namespace

bool SkinModel::load( const std::string &body, const std::string &tpose )
{
    m_body = LoadModel( body.c_str() );
    if ( m_body.meshCount == 0 || m_body.boneCount == 0 ) return false;
    int n = m_body.boneCount;
    m_names.resize( n );
    m_parent.resize( n );
    m_bind.assign( m_body.bindPose, m_body.bindPose + n );
    for ( int i = 0; i < n; ++i )
    {
        m_names[i] = m_body.bones[i].name;
        m_parent[i] = m_body.bones[i].parent;
        if ( m_names[i] == "Head" || m_names[i] == "DEF-head" ) m_head = i;
        if ( m_names[i] == "pelvis" || m_names[i] == "DEF-hips" ) m_pelvis = i;
    }
    // The upper body: the first spine bone and every bone that hangs on it.
    int spine = std::max( bone( "spine_01" ), bone( "DEF-spine.001" ) );
    m_upper.assign( n, false );
    for ( int i = 0; i < n && spine >= 0; ++i )
        for ( int b = i; b >= 0; b = m_parent[b] )
            if ( b == spine )
            {
                m_upper[i] = true;
                break;
            }
    for ( int i = 0; i < m_body.meshCount; ++i ) m_skinned.push_back( m_body.meshes[i].boneWeights != nullptr );
    // The reference: the T-pose on this rig (else the rest pose).
    m_ref = m_bind;
    int                   count = 0;
    const ModelAnimation *anims = animations( tpose, count );
    for ( int a = 0; a < count; ++a )
    {
        if ( strcmp( anims[a].name, "A_TPose" ) != 0 ) continue;
        for ( int b = 0; b < n; ++b )
            for ( int s = 0; s < anims[a].boneCount; ++s )
                if ( m_names[b] == anims[a].bones[s].name ) m_ref[b] = anims[a].framePoses[0][s];
    }
    return true;
}

int SkinModel::addClips( const std::string &file, const std::vector<std::string> &names )
{
    int                   count = 0;
    const ModelAnimation *anims = animations( file, count );
    if ( !anims || count == 0 ) return 0;
    const ModelAnimation *refAnim = &anims[0];
    for ( int a = 0; a < count; ++a )
        if ( strcmp( anims[a].name, "A_TPose" ) == 0 ) refAnim = &anims[a];
    const Transform *sref = refAnim->framePoses[0];
    int              n = bones();

    // The file's bone for each of ours: the same name, else the Rigify name.
    std::vector<int> src( n, -1 );
    auto             find = [&]( const std::string &name )
    {
        for ( int i = 0; i < refAnim->boneCount; ++i )
            if ( name == refAnim->bones[i].name ) return i;
        return -1;
    };
    int hips = -1, head = -1, thighL = -1, thighR = -1;
    for ( int b = 0; b < n; ++b )
    {
        src[b] = find( m_names[b] );
        if ( src[b] < 0 && !rigify( m_names[b] ).empty() ) src[b] = find( rigify( m_names[b] ) );
        if ( m_names[b] == "pelvis" ) hips = b;
        if ( m_names[b] == "Head" ) head = b;
        if ( m_names[b] == "thigh_l" ) thighL = b;
        if ( m_names[b] == "thigh_r" ) thighR = b;
    }
    // The two rigs may face different ways: their rest axes, measured from hips, head and thighs, line them up.
    Quaternion align = QuaternionIdentity();
    float      scale = 1.0f;
    if ( hips >= 0 && head >= 0 && thighL >= 0 && thighR >= 0 && src[hips] >= 0 && src[head] >= 0 && src[thighL] >= 0 &&
         src[thighR] >= 0 )
    {
        Matrix t = basis( m_ref[hips].translation, m_ref[head].translation, m_ref[thighL].translation,
                          m_ref[thighR].translation );
        Matrix s = basis( sref[src[hips]].translation, sref[src[head]].translation, sref[src[thighL]].translation,
                          sref[src[thighR]].translation );
        align = QuaternionFromMatrix( MatrixMultiply( MatrixTranspose( s ), t ) );
        float hs = sref[src[hips]].translation.y, ht = m_ref[hips].translation.y;
        if ( fabsf( hs ) > 0.1f ) scale = ht / hs;
    }
    Quaternion unalign = QuaternionInvert( align );

    int added = 0;
    for ( int a = 0; a < count; ++a )
    {
        const ModelAnimation &anim = anims[a];
        bool                  wanted = names.empty();
        for ( const std::string &w : names ) wanted = wanted || w == anim.name;
        if ( !wanted || anim.frameCount <= 0 ) continue;
        SkinClip c;
        c.m_name = anim.name;
        c.m_frames = anim.frameCount;
        c.m_loop = c.m_name.find( "_Loop" ) != std::string::npos;
        c.m_poses.resize( (size_t)c.m_frames * n );
        for ( int f = 0; f < c.m_frames; ++f )
        {
            const Transform *pose = anim.framePoses[f];
            Transform       *out = &c.m_poses[(size_t)f * n];
            for ( int b = 0; b < n; ++b )
            {
                int        s = src[b], p = m_parent[b];
                Transform &t = out[b];
                t.scale = { 1.0f, 1.0f, 1.0f };
                // The turn a bone makes from the source's T-pose, made in the body's frame, from the body's T-pose.
                if ( s >= 0 )
                {
                    Quaternion turn = QuaternionMultiply( pose[s].rotation, QuaternionInvert( sref[s].rotation ) );
                    turn = QuaternionMultiply( QuaternionMultiply( align, turn ), unalign );
                    t.rotation = QuaternionMultiply( turn, m_ref[b].rotation );
                }
                else if ( p >= 0 )
                    t.rotation = QuaternionMultiply(
                        QuaternionMultiply( out[p].rotation, QuaternionInvert( m_ref[p].rotation ) ),
                        m_ref[b].rotation );
                else
                    t.rotation = m_ref[b].rotation;
                // The root and the hips move as the source's do (scaled to the body); the rest hang on their parents
                // at the body's own lengths.
                if ( p < 0 || m_parent[p] < 0 )
                {
                    Vector3 d =
                        s >= 0 ? Vector3Subtract( pose[s].translation, sref[s].translation ) : Vector3{ 0, 0, 0 };
                    t.translation = Vector3Add( m_ref[b].translation,
                                                Vector3Scale( Vector3RotateByQuaternion( d, align ), scale ) );
                }
                else
                {
                    Quaternion turn = QuaternionMultiply( out[p].rotation, QuaternionInvert( m_ref[p].rotation ) );
                    t.translation = Vector3Add(
                        out[p].translation, Vector3RotateByQuaternion(
                                                Vector3Subtract( m_ref[b].translation, m_ref[p].translation ), turn ) );
                }
            }
        }
        m_clips.push_back( std::move( c ) );
        ++added;
    }
    return added;
}

int SkinModel::clip( const std::string &name ) const
{
    for ( size_t i = 0; i < m_clips.size(); ++i )
        if ( m_clips[i].m_name == name ) return (int)i;
    return -1;
}

int SkinModel::addHair( const std::string &file )
{
    m_hair.push_back( LoadModel( file.c_str() ) );
    return (int)m_hair.size() - 1;
}

float SkinModel::length( int clip ) const
{
    return clip < 0 ? 0.0f : ( m_clips[clip].m_frames - 1 ) / FrameRate;
}

float SkinModel::hipHeight( int clip ) const
{
    if ( clip < 0 ) return 0.0f;
    for ( int b = 0; b < bones(); ++b )
        if ( m_names[b] == "pelvis" ) return m_clips[clip].m_poses[b].translation.y;
    return 0.0f;
}

void SkinModel::play( SkinState &s, int clip, float fade ) const
{
    if ( clip < 0 || clip == s.m_clip ) return;
    s.m_prev = s.m_clip;
    s.m_prevTime = s.m_time;
    s.m_clip = clip;
    s.m_time = 0.0f;
    s.m_fade = fade > 0.0f ? 0.0f : 1.0f;
    s.m_fadeLength = fade;
}

void SkinModel::advance( SkinState &s, float dt, float speed ) const
{
    s.m_time += dt * speed;
    s.m_prevTime += dt;
    if ( s.m_fade < 1.0f ) s.m_fade = fminf( 1.0f, s.m_fade + dt / fmaxf( s.m_fadeLength, 1e-3f ) );
}

void SkinModel::sample( int clip, float time, std::vector<Transform> &out ) const
{
    const SkinClip &k = m_clips[clip];
    int             n = bones();
    out.resize( n );
    float f = time * FrameRate;
    int   f0, f1;
    if ( k.m_loop && k.m_frames > 1 )
    {
        float span = (float)( k.m_frames - 1 );
        f = fmodf( f, span );
        if ( f < 0.0f ) f += span;
        f0 = (int)f;
        f1 = ( f0 + 1 ) % k.m_frames;
    }
    else
    {
        f = Clamp( f, 0.0f, (float)( k.m_frames - 1 ) );
        f0 = (int)f;
        f1 = std::min( f0 + 1, k.m_frames - 1 );
    }
    float w = f - f0;
    for ( int b = 0; b < n; ++b ) out[b] = blend( k.m_poses[(size_t)f0 * n + b], k.m_poses[(size_t)f1 * n + b], w );
}

int SkinModel::bone( const std::string &name ) const
{
    for ( size_t i = 0; i < m_names.size(); ++i )
        if ( m_names[i] == name ) return (int)i;
    return -1;
}

Matrix SkinModel::matrix( const Transform &t )
{
    return MatrixMultiply( QuaternionToMatrix( t.rotation ),
                           MatrixTranslate( t.translation.x, t.translation.y, t.translation.z ) );
}

void SkinModel::turn( std::vector<Transform> &pose, int bone, Quaternion q, Vector3 pivot ) const
{
    for ( int i = 0; i < bones(); ++i )
    {
        int b = i;
        while ( b >= 0 && b != bone ) b = m_parent[b];
        if ( b < 0 ) continue;
        pose[i].translation =
            Vector3Add( pivot, Vector3RotateByQuaternion( Vector3Subtract( pose[i].translation, pivot ), q ) );
        pose[i].rotation = QuaternionMultiply( q, pose[i].rotation );
    }
}

void SkinModel::reach( std::vector<Transform> &pose, int upper, int lower, int end, Vector3 target, Vector3 bend ) const
{
    Vector3 a = pose[upper].translation, b = pose[lower].translation, c = pose[end].translation;
    Vector3 to = Vector3Subtract( target, a );
    float   la = Vector3Distance( a, b ), lb = Vector3Distance( b, c ), len = Vector3Length( to );
    if ( la < 1e-4f || lb < 1e-4f || len < 1e-4f ) return;
    float   d = Clamp( len, fabsf( la - lb ) + 1e-3f, la + lb - 1e-3f );
    Vector3 u = Vector3Scale( to, 1.0f / len );
    // The joint where the two lengths meet, on the side of the line it is on now (or the one asked for).
    Vector3 side = Vector3Length( bend ) > 0.0f ? bend : Vector3Subtract( b, a );
    side = Vector3Subtract( side, Vector3Scale( u, Vector3DotProduct( side, u ) ) );
    if ( Vector3Length( side ) < 1e-4f ) return;
    Vector3 v = Vector3Normalize( side );
    float   cosA = Clamp( ( la * la + d * d - lb * lb ) / ( 2.0f * la * d ), -1.0f, 1.0f );
    Vector3 joint = Vector3Add(
        a, Vector3Add( Vector3Scale( u, la * cosA ), Vector3Scale( v, la * sqrtf( 1.0f - cosA * cosA ) ) ) );
    turn( pose, upper,
          QuaternionFromVector3ToVector3( Vector3Normalize( Vector3Subtract( b, a ) ),
                                          Vector3Normalize( Vector3Subtract( joint, a ) ) ),
          a );
    Vector3 nb = pose[lower].translation, nc = pose[end].translation, goal = Vector3Add( a, Vector3Scale( u, d ) );
    turn( pose, lower,
          QuaternionFromVector3ToVector3( Vector3Normalize( Vector3Subtract( nc, nb ) ),
                                          Vector3Normalize( Vector3Subtract( goal, nb ) ) ),
          nb );
}

void SkinModel::mix( std::vector<Transform> &a, const std::vector<Transform> &b, float w )
{
    if ( w <= 0.0f ) return;
    for ( size_t i = 0; i < a.size() && i < b.size(); ++i ) a[i] = w >= 1.0f ? b[i] : blend( a[i], b[i], w );
}

void SkinModel::layer( std::vector<Transform> &a, const std::vector<Transform> &b, float w, float follow ) const
{
    if ( w <= 0.0f || m_pelvis < 0 ) return;
    const Transform pa = a[m_pelvis], pb = b[m_pelvis];
    Quaternion turn = QuaternionSlerp( QuaternionIdentity(),
                                       QuaternionMultiply( pa.rotation, QuaternionInvert( pb.rotation ) ), follow );
    for ( int i = 0; i < bones(); ++i )
    {
        if ( !m_upper[i] ) continue;
        Transform t = b[i];
        t.rotation = QuaternionMultiply( turn, b[i].rotation );
        t.translation = Vector3Add(
            pa.translation, Vector3RotateByQuaternion( Vector3Subtract( b[i].translation, pb.translation ), turn ) );
        a[i] = w >= 1.0f ? t : blend( a[i], t, w );
    }
}

void SkinModel::pose( const SkinState &s, std::vector<Matrix> &bones ) const
{
    static thread_local std::vector<Transform> a;
    posed( s, a );
    skin( a, bones );
}

void SkinModel::posed( const SkinState &s, std::vector<Transform> &out ) const
{
    static thread_local std::vector<Transform> b;
    int                                        n = this->bones();
    sample( s.m_clip, s.m_time, out );
    if ( s.m_prev >= 0 && s.m_fade < 1.0f )
    {
        sample( s.m_prev, s.m_prevTime, b );
        for ( int i = 0; i < n; ++i ) out[i] = blend( b[i], out[i], s.m_fade );
    }
}

void SkinModel::skin( const std::vector<Transform> &a, std::vector<Matrix> &bones ) const
{
    // As raylib's UpdateModelAnimationBones: from the rest pose to the pose, in the model's space.
    int n = this->bones();
    bones.resize( n );
    for ( int i = 0; i < n; ++i )
    {
        const Transform &in = m_bind[i];
        Quaternion       invRotation = QuaternionInvert( in.rotation );
        Vector3          invTranslation = Vector3RotateByQuaternion( Vector3Negate( in.translation ), invRotation );
        Vector3          invScale = Vector3Divide( { 1.0f, 1.0f, 1.0f }, in.scale );
        Vector3          translation =
            Vector3Add( Vector3RotateByQuaternion( Vector3Multiply( a[i].scale, invTranslation ), a[i].rotation ),
                        a[i].translation );
        Quaternion rotation = QuaternionMultiply( a[i].rotation, invRotation );
        Vector3    scale = Vector3Multiply( a[i].scale, invScale );
        bones[i] = MatrixMultiply( MatrixMultiply( QuaternionToMatrix( rotation ),
                                                   MatrixTranslate( translation.x, translation.y, translation.z ) ),
                                   MatrixScale( scale.x, scale.y, scale.z ) );
    }
}

}   // namespace grave
