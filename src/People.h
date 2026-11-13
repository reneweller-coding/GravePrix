/**
 * @file People.h
 * @brief The skinned people (Quaternius' CC0 base bodies and animations, see tools/make_people3d.py): a light body for
 *        the roadside and the marshals, a fuller one for the podium, dressed by vertex colours bone by bone; what they
 *        do (idle, cheer, clap, wave, a flag, a microphone, a dive aside) comes from the animation library's clips with
 *        the arms set by IK on top. Skinned on the CPU into a mesh per figure, so the lit and the shadow shaders, the
 *        contact shadows and the fog treat them like every other model.
 * @author Rene Weller
 */
#pragma once

#include "Skin.h"

#include <raylib.h>

#include <map>
#include <string>
#include <vector>

namespace grave
{

/** @brief What a figure does. */
enum class PersonAct
{
    IDLE,    ///< standing, the weight shifting
    CHEER,   ///< both arms up, hopping
    CLAP,    ///< clapping in front of the chest
    WAVE,    ///< the right hand waving above the head
    FLAG,    ///< a flag swung wide over the head
    TALK,    ///< the right hand at the mouth (a microphone), the left one talking
    DIVE,    ///< thrown aside and down, then up again (the time from the start)
};

/** @brief What a figure wears: colours by body part, a hat, something in the right hand. */
struct Outfit
{
    std::string m_name;
    bool        m_female = false, m_hi = false;   ///< the body; the fuller one (podium)
    Color       m_skin = { 224, 184, 154, 255 }, m_hair = { 40, 30, 24, 255 }, m_top = { 80, 80, 90, 255 },
          m_legs = { 40, 40, 46, 255 }, m_shoes = { 20, 18, 18, 255 }, m_hands = { 224, 184, 154, 255 },
          m_trim = { 230, 230, 224, 255 };
    int         m_trimKind = 0;      ///< 0 none, 1 a shirt collar and a tie in the trim's colour, 2 reflective stripes
    bool        m_coat = false;      ///< the top reaches the knees
    bool        m_dress = false;     ///< the top reaches the shins (a skirt)
    bool        m_longHair = false;  ///< the hair down to the neck
    std::string m_hat;               ///< a model on the head ("" none)
    std::string m_hand;              ///< a model in the right hand ("" none)
    PersonAct   m_excited = PersonAct::CHEER;   ///< what it does when a car passes
};

/** @brief Where the hat and the thing in the hand go (the figure's model space, facing -z like the Forge's people). */
struct PersonPose
{
    Matrix m_hat = {};    ///< the hat's model (its origin on the crown of the head)
    Matrix m_hand = {};   ///< the held model (its origin in the fist, +y along it)
};

/** @brief The bodies, the outfits, a skinned mesh per figure. */
class People
{
public:
    /** @return whether the bodies are there (loads them on the first call; false if the files are missing) */
    bool ready();

    /** @return an outfit's index by its name, or -1 */
    int outfit( const std::string &name ) const;
    /**
     * @brief An outfit like another in other colours (made on the first call, kept).
     * @param[in] base    the outfit it is made from
     * @param[in] top     the coat's or the shirt's colour
     * @param[in] legs    the trousers' colour
     * @param[in] female    the female body
     * @param[in] umbrella  an umbrella in the hand (else nothing)
     * @return its index, or -1
     */
    int outfitLike( int base, Color top, Color legs, bool female, bool umbrella );

    /** @return an outfit */
    const Outfit &outfitAt( int i ) const { return m_outfits[(size_t)i]; }

    /**
     * @brief Poses and skins one figure.
     * @param[in]  slot    the figure's own mesh (any number the caller keeps for it)
     * @param[in]  o       the outfit
     * @param[in]  act     what it does
     * @param[in]  t       seconds (into the act for a dive, else a running clock)
     * @param[in]  seed    0..1, shifts the figure's phases
     * @param[out] out     where its hat and the thing in its hand go
     * @return the mesh in the figure's model space (feet at 0, facing -z), nullptr if there are no bodies
     */
    const Mesh *pose( int slot, int o, PersonAct act, float t, float seed, PersonPose &out );

    /** @brief The Forge's old figures instead (--oldpeople, for before/after pictures). */
    void disable() { m_state = -1; }

    /** @brief Frees the meshes (the GL context goes). */
    void unload();

private:
    struct Body
    {
        SkinModel                  m_skin;
        std::vector<float>         m_pos, m_nrm, m_wts;   ///< rest pose, 3 / 3 / 4 per vertex
        std::vector<unsigned char> m_ids;                 ///< 4 per vertex
        std::vector<unsigned short> m_idx;
        std::vector<int>           m_dominant;            ///< the bone each vertex follows most
        float                      m_headTop = 1.8f;
        Vector3                    m_headAt = { 0, 1.65f, 0 };
        int m_idle = -1, m_idle2 = -1, m_talk = -1, m_fall = -1, m_rise = -1;
        int m_head = -1, m_chest = -1, m_upperL = -1, m_upperR = -1, m_lowerL = -1, m_lowerR = -1, m_handL = -1,
            m_handR = -1;
    };
    struct Slot
    {
        Mesh  m_mesh = {};
        int   m_outfit = -1;
        int   m_act = -1;
        float m_t = -1e9f, m_seed = -1.0f;
        PersonPose m_pose;
    };

    bool loadBody( Body &b, const std::string &file, const std::string &dir );
    void colour( const Body &b, const Outfit &o, unsigned char *out ) const;
    void posed( Body &b, PersonAct act, float t, float seed, std::vector<Transform> &pose, PersonPose &out,
                const Outfit &of ) const;

    int                  m_state = 0;   ///< 0 not tried, 1 loaded, -1 missing
    Body                 m_body[4];     ///< male lo, male hi, female lo, female hi
    std::vector<Outfit>  m_outfits;
    std::map<int, Slot>  m_slots;
};

/** @return the people (one for the game) */
People &people();

}   // namespace grave
