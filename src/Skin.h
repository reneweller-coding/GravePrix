/**
 * @file Skin.h
 * @brief Skinned figures from glTF (Quaternius' base characters, CC0): a body and its skeleton, animations from other
 *        files put onto it by bone name (both rigs share a T-pose to measure from), played with crossfades; hair
 *        and the eyes ride on the head bone.
 * @author Rene Weller
 */
#pragma once


#include <raylib.h>

#include <string>
#include <vector>

namespace grave
{

/** @brief An animation on the body's skeleton: poses in the model's space, 1000/17 frames a second (raylib's). */
struct SkinClip
{
    std::string            m_name;
    int                    m_frames = 0;
    bool                   m_loop = false;   ///< loops ("..._Loop"), else holds its last frame
    std::vector<Transform> m_poses;          ///< frame * bones + bone
};

/** @brief Where one figure is in its animations. */
struct SkinState
{
    int   m_clip = 0;             ///< playing
    int   m_prev = -1;            ///< fading out (-1 none)
    float m_time = 0.0f;          ///< seconds into the clip
    float m_prevTime = 0.0f;      ///< seconds into the one fading out
    float m_fade = 1.0f;          ///< 0..1 through the crossfade
    float m_fadeLength = 0.25f;   ///< seconds the crossfade takes
};

/** @brief A body with its animations. */
class SkinModel
{
public:
    /**
     * @brief Loads the body.
     * @param[in] body   the body's glTF
     * @param[in] tpose  a file with an "A_TPose" animation on the same rig (the reference for retargeting)
     * @return false if the body cannot be read
     */
    bool load( const std::string &body, const std::string &tpose );

    /**
     * @brief Adds animations from a file, put onto the body by bone name (the file's own "A_TPose" is its reference).
     * @param[in] file   a glTF / GLB with animations
     * @param[in] names  only these (empty: all)
     * @return how many were added
     */
    int addClips( const std::string &file, const std::vector<std::string> &names = {} );

    /**
     * @brief A clip by its name.
     * @param[in] name  the name
     * @return its index, or -1
     */
    int clip( const std::string &name ) const;

    /**
     * @brief Hair (or anything else) modelled on the body in its rest pose: it rides on the head bone.
     * @param[in] file  the glTF
     * @return its index
     */
    int addHair( const std::string &file );

    /**
     * @brief Switches to a clip with a crossfade (nothing if it already plays).
     * @param[in,out] s     the figure
     * @param[in]     clip  the clip
     * @param[in]     fade  seconds
     */
    void play( SkinState &s, int clip, float fade = 0.25f ) const;

    /**
     * @brief Moves a figure on through its clip.
     * @param[in,out] s      the figure
     * @param[in]     dt     seconds
     * @param[in]     speed  how fast the clip runs
     */
    void advance( SkinState &s, float dt, float speed = 1.0f ) const;

    /**
     * @brief The skin matrices of a pose.
     * @param[in]  s      the figure
     * @param[out] bones  one per bone (rest pose to posed, in the model's space)
     */
    void pose( const SkinState &s, std::vector<Matrix> &bones ) const;

    /**
     * @brief Where a figure is in its clips, bone by bone (the crossfade done), to change before skin() makes it.
     * @param[in]  s    the figure
     * @param[out] out  each bone's place in the model's space
     */
    void posed( const SkinState &s, std::vector<Transform> &out ) const;

    /**
     * @brief A clip's pose at a time: each bone's place in the model's space (interpolated between frames).
     * @param[in]  clip  the clip
     * @param[in]  time  seconds into it (a loop wraps, else it holds its ends)
     * @param[out] out   one per bone
     */
    void sample( int clip, float time, std::vector<Transform> &out ) const;

    /**
     * @brief The skin matrices of a pose given bone by bone.
     * @param[in]  pose   each bone's place in the model's space
     * @param[out] bones  one per bone (rest pose to posed)
     */
    void skin( const std::vector<Transform> &pose, std::vector<Matrix> &bones ) const;

    /**
     * @brief Mixes a second pose into a first, bone by bone.
     * @param[in,out] a  the pose
     * @param[in]     b  the other
     * @param[in]     w  its share 0..1
     */
    static void mix( std::vector<Transform> &a, const std::vector<Transform> &b, float w );

    /**
     * @brief Lays the upper body of a second pose (the spine and all on it: arms, head) over a first, moved onto the
     *        first's hips, so the legs walk on while the arms aim or strike.
     * @param[in,out] a       the pose (its hips and legs stay)
     * @param[in]     b       the upper body's pose
     * @param[in]     w       its share 0..1
     * @param[in]     follow  how far the upper body turns with the first's hips (0: it keeps its own facing)
     */
    void layer( std::vector<Transform> &a, const std::vector<Transform> &b, float w, float follow ) const;

    /**
     * @brief A bone by its name.
     * @param[in] name  the name
     * @return its index, or -1
     */
    int bone( const std::string &name ) const;

    /** @return a bone's place in the model's space as a matrix */
    static Matrix matrix( const Transform &t );

    /**
     * @brief Turns a bone and everything that hangs on it about a point.
     * @param[in,out] pose   the pose
     * @param[in]     bone   the bone
     * @param[in]     q      the turn
     * @param[in]     pivot  about this point (the model's space)
     */
    void turn( std::vector<Transform> &pose, int bone, Quaternion q, Vector3 pivot ) const;

    /**
     * @brief Bends an arm or a leg so that its end reaches a point.
     * @param[in,out] pose    the pose (the chain and all that hangs on it move)
     * @param[in]     upper   the upper bone (upper arm, thigh)
     * @param[in]     lower   the lower bone (forearm, shin)
     * @param[in]     end     the end (hand, foot)
     * @param[in]     target  where the end goes (the model's space)
     * @param[in]     bend    which way the joint goes (zero: it stays on the side it bends to now)
     */
    void reach( std::vector<Transform> &pose, int upper, int lower, int end, Vector3 target,
                Vector3 bend = { 0.0f, 0.0f, 0.0f } ) const;

    /** @return a bone's name */
    const std::string &name( int i ) const { return m_names[(size_t)i]; }
    /** @return the head bone's index */
    int head() const { return m_head; }
    /** @return how many bones */
    int bones() const { return (int)m_names.size(); }
    /** @return the clip's length in seconds */
    float length( int clip ) const;
    /** @return the pelvis's height in a clip's first frame (to set a sitting figure on its seat) */
    float hipHeight( int clip ) const;

    Model              m_body = {};   ///< the meshes (skinned ones carry bone ids and weights)
    std::vector<Model> m_hair;        ///< rigid on the head
    std::vector<bool>  m_skinned;     ///< per mesh of the body: skinned (else rigid on the head)

private:
    std::vector<std::string> m_names;
    std::vector<int>         m_parent;
    std::vector<Transform>   m_bind;   ///< rest pose, model space
    std::vector<Transform>   m_ref;    ///< the T-pose, model space
    std::vector<SkinClip>    m_clips;
    std::vector<bool>        m_upper;   ///< the upper body's bones (spine_01 and all on it)
    int                      m_head = 0;
    int                      m_pelvis = -1;
};

}   // namespace grave
