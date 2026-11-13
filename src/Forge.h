/**
 * @file Forge.h
 * @brief Builds the cars, props and pickups from their model lines (data/models, docs/DESIGN.md "Models"): car
 *        bodies and cabins, hulls, wings, boxes, cylinders, domes, lights, greebles. Flat-shaded facets with a little
 *        brightness noise per face; a colour's alpha carries how much it glows. Wheels are kept apart (they spin and
 *        steer); "paint", "paint2" and "paint3" take the driver's colours.
 *
 *        Model space: x to the right, y up, -z is the nose. One unit is one metre; y = 0 is the ground.
 * @author Rene Weller
 */
#pragma once

#include "Data.h"

#include <raylib.h>
#include <raymath.h>

#include <map>
#include <string>
#include <vector>

namespace grave
{

/**
 * @brief How a vertex moves in the lit shader (docs/DESIGN.md "Life on the stands, wind"): the mark rides in the
 *        texture coordinates of the Forge's models and the baked prop chunks, uv.x = kind + a share, uv.y = a length,
 *        uv2 = the anchor on the ground (x, z) that the vertex belongs to (a figure, a tree, a flag's pole).
 */
enum MarkE : int
{
    MARK_NONE = 0,
    MARK_WIND = 1,        ///< bends with the wind, the top most: share = how pliant, uv.y = its height
    MARK_SEATED = 2,      ///< a seated fan's body: share = how much of the rising it takes (feet 0 .. hips 1)
    MARK_SEATED_ARM = 3,  ///< a seated fan's arm: share = metres below the shoulder (raised it mirrors)
    MARK_CALM = 4,        ///< a mourner (or anyone under an umbrella): only sways; share as MARK_SEATED
    MARK_FLAG = 5,        ///< cloth that flutters: share = how loose, uv.y = its length from the pole
    MARK_STANDING = 6,    ///< a standing fan's body: hops
    MARK_STANDING_ARM = 7 ///< a standing fan's arm (as MARK_SEATED_ARM)
};

/** @brief Gathers flat-shaded triangles into a mesh (the Forge's parts, walls, batched props). */
struct MeshBuilder
{
    std::vector<float>         m_pos, m_nrm, m_uv, m_uv2;
    std::vector<unsigned char> m_col;
    Rng                        m_rng;
    float                      m_noise = 0.06f;   ///< brightness noise per face
    bool                       m_withUv = false;  ///< keep texture coordinates (vert's uv, uv2)
    Vector2                    m_mark = { 0, 0 }, m_mark2 = { 0, 0 };   ///< the uv, uv2 tri() and quad() give
    float                      m_markShift = 0.0f;   ///< added to the crowd's position along its stand (append)

    /** @brief A triangle whose normal points away from 'inside'. */
    void tri( Vector3 a, Vector3 b, Vector3 c, Color col, Vector3 inside );
    /** @brief A quad (two triangles). */
    void quad( Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col, Vector3 inside );
    /** @brief One vertex as it is. */
    void vert( Vector3 p, Vector3 n, Color c, Vector2 uv = { 0, 0 }, Vector2 uv2 = { 0, 0 } );
    /** @brief A box turned by yaw (radians, clockwise from above) about its middle; taper shrinks the top. */
    void box( Vector3 center, Vector3 half, float yaw, Color col, float taper = 0.0f );
    /** @brief Another mesh's triangles, moved (its colours tinted; its marks moved along when m_withUv). */
    void append( const Mesh &m, Matrix xf, Color tint = WHITE );
    /** @return the vertices gathered */
    int count() const { return (int)( m_pos.size() / 3 ); }
    /** @return whether any vertex carries a mark (MarkE) */
    bool marked() const;
    /** @return the mesh (uploaded if asked) */
    Mesh mesh( bool upload );
    /** @brief Starts over. */
    void clear();
};

/** @brief The driver's three colours. */
struct Paint
{
    Color m_c[3] = { { 150, 40, 36, 255 }, { 30, 30, 34, 255 }, { 200, 200, 196, 255 } };
};

/** @brief Where a wheel sits. */
struct WheelSpot
{
    Vector3 m_pos = { 0, 0, 0 };   ///< its middle
    float   m_r = 0.32f, m_w = 0.22f;
    bool    m_front = false;
    Color   m_hub = { 170, 172, 178, 255 };
};

/** @brief A light of a prop (a lamp's bulb). */
struct LightSpot
{
    Vector3 m_pos = { 0, 0, 0 };
    Color   m_color = WHITE;
};

/** @brief A board the renderer prints an advertisement on (part banner): its corners, the text's top at 2 and 3. */
struct BannerSpot
{
    Vector3 m_c[4] = {};
    int     m_ad = 0;   ///< 0 the player's sponsor (or Graves & Sons), 1 Graves & Sons' slogan
};

/** @brief A built model. */
struct ModelMesh
{
    Mesh                   m_base = {};
    Mesh                   m_head = {};
    Mesh                   m_spin = {};
    bool                   m_hasBase = false;
    bool                   m_hasHead = false;
    bool                   m_hasSpin = false;
    Vector3                m_pivot = { 0, 0, 0 };   ///< where the head turns
    float                  m_spinSpeed = 90.0f;     ///< degrees a second
    Vector3                m_spinAxis = { 0, 1, 0 }; ///< what the spin group turns round (spin_axis=x|y|z)
    Vector3                m_spinAt = { 0, 0, 0 };   ///< and where (spin_at=x,y,z)
    float                  m_radius = 1.0f;         ///< how far it reaches from its middle on the ground
    float                  m_height = 1.0f;         ///< its top
    bool                   m_lamp = false;          ///< its lights shine at night
    std::vector<Vector3>   m_engines;               ///< nozzles (exhausts)
    std::vector<Color>     m_engineColors;
    std::vector<WheelSpot> m_wheels;
    std::vector<LightSpot> m_lights;
    std::vector<Vector3>   m_crowd;                 ///< the heads of the lively fans (camera flashes at night)
    std::vector<BannerSpot> m_banners;
};

/** @brief The workshop of meshes: builds a model when it is first asked for and keeps it. */
class Forge
{
public:
    /** @brief Lets go of every mesh. */
    void clear();

    /**
     * @brief A model, built on first use.
     * @param[in] cat    the catalog with the model lines
     * @param[in] name   the model's name (an unknown name gets a plain stand-in)
     * @param[in] paint  the colours for "paint", "paint2", "paint3"
     * @return the meshes
     */
    const ModelMesh &get( const Catalog &cat, const std::string &name, const Paint &paint = Paint() );

    /**
     * @brief Builds a model's meshes.
     * @param[in] def     the model
     * @param[in] upload  send to the graphics card
     * @param[in] paint   the colours for "paint", "paint2", "paint3"
     * @return the meshes
     */
    static ModelMesh build( const ModelDef &def, bool upload = true, const Paint &paint = Paint() );

    /** @return the wheel: radius 1, width 1 along x, the hub white (tint it) */
    const Mesh &wheel();

    /** @return a new wheel mesh (as wheel(); the caller owns it) */
    static Mesh buildWheel( bool upload );

    /**
     * @brief Where the modelled cars are (assets/cars/<model>.glb, made by tools/cars3d/make_car.py): a model with
     *        such a file takes its body from there; its wheels, exhausts and lights stay those of its parts.
     * @param[in] dir  the assets folder ("" = the Forge's parts only)
     */
    static void setAssetDir( const std::string &dir );

    /** @brief A modelled car's triangles as read (smooth normals as exported), coloured per material. */
    struct Glb
    {
        bool                     m_ok = false;
        std::vector<float>       m_pos, m_nrm;
        std::vector<signed char> m_slot;   ///< 0..2 a paint slot, -1 its own colour
        std::vector<Color>       m_col;    ///< its own colour, alpha = glow
        float                    m_radius = 0.0f, m_height = 0.0f;
    };

    /**
     * @brief Reads a modelled car (GLB: materials paint0..2, light*, anything else its base colour).
     * @param[in]  file  the .glb
     * @param[out] out   the triangles
     * @return whether it could be read
     */
    static bool readGlb( const std::string &file, Glb &out );

    /**
     * @brief A modelled car's mesh in a driver's colours.
     * @param[in] g       the car as read
     * @param[in] paint   the colours for paint0..2
     * @param[in] upload  send to the graphics card
     * @return the mesh (triangles, no indices)
     */
    static Mesh glbMesh( const Glb &g, const Paint &paint, bool upload );

private:
    /** @return the modelled car of that model, or nullptr */
    const Glb *glb( const std::string &name );

    std::map<std::string, ModelMesh> m_cache;
    std::map<std::string, Glb>       m_glb;
    Mesh                             m_wheel = {};
    bool                             m_hasWheel = false;
};

}   // namespace grave
