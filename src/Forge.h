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

/** @brief Gathers flat-shaded triangles into a mesh (the Forge's parts, walls, batched props). */
struct MeshBuilder
{
    std::vector<float>         m_pos, m_nrm, m_uv, m_uv2;
    std::vector<unsigned char> m_col;
    Rng                        m_rng;
    float                      m_noise = 0.06f;   ///< brightness noise per face
    bool                       m_withUv = false;  ///< keep texture coordinates (vert's uv, uv2)

    /** @brief A triangle whose normal points away from 'inside'. */
    void tri( Vector3 a, Vector3 b, Vector3 c, Color col, Vector3 inside );
    /** @brief A quad (two triangles). */
    void quad( Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col, Vector3 inside );
    /** @brief One vertex as it is. */
    void vert( Vector3 p, Vector3 n, Color c, Vector2 uv = { 0, 0 }, Vector2 uv2 = { 0, 0 } );
    /** @brief A box turned by yaw (radians, clockwise from above) about its middle; taper shrinks the top. */
    void box( Vector3 center, Vector3 half, float yaw, Color col, float taper = 0.0f );
    /** @brief Another mesh's triangles, moved (its colours tinted). */
    void append( const Mesh &m, Matrix xf, Color tint = WHITE );
    /** @return the vertices gathered */
    int count() const { return (int)( m_pos.size() / 3 ); }
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
    float                  m_radius = 1.0f;         ///< how far it reaches from its middle on the ground
    float                  m_height = 1.0f;         ///< its top
    bool                   m_lamp = false;          ///< its lights shine at night
    std::vector<Vector3>   m_engines;               ///< nozzles (exhausts)
    std::vector<Color>     m_engineColors;
    std::vector<WheelSpot> m_wheels;
    std::vector<LightSpot> m_lights;
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

private:
    std::map<std::string, ModelMesh> m_cache;
    Mesh                             m_wheel = {};
    bool                             m_hasWheel = false;
};

}   // namespace grave
