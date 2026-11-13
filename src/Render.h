/**
 * @file Render.h
 * @brief Draws a race: the ground and the road with their surfaces (procedural asphalt, cobble, dirt, snow, ice,
 *        wood, water ...), the walls in their styles, the venue's props baked into a few big meshes, the cars with
 *        turning wheels, their guns and gear, pickups, mines, slicks, bullets, rockets, smoke, fire, sparks,
 *        explosions, skid marks, rain and snow; lit by the sun with shadows, the lamps, the fires and at night the
 *        cars' headlights; then bloom and the tone curve. Several renderers may live at once (split screen, the
 *        showroom); the shaders are shared.
 * @author Rene Weller
 */
#pragma once

#include "Forge.h"
#include "Sim.h"
#include "Track.h"

#include <raylib.h>

#include <string>
#include <vector>

namespace grave
{

/** @brief Where the camera looks. */
struct View
{
    Vector2 m_focus = { 0, 0 };   ///< the ground point it looks at
    float   m_height = 80.0f;     ///< metres above it
    float   m_shake = 0.0f;       ///< 0..1
};

/** @brief A bit of light, smoke or fire. */
struct Particle
{
    Vector3 m_pos = { 0, 0, 0 }, m_vel = { 0, 0, 0 };
    float   m_age = 0.0f, m_life = 0.5f;
    float   m_size0 = 1.0f, m_size1 = 0.3f;
    Color   m_c0 = WHITE, m_c1 = { 255, 80, 20, 0 };
    int     m_tex = 0;
    bool    m_add = true;     ///< added light; else blended (smoke)
    float   m_drag = 0.0f;
    float   m_rise = 0.0f;    ///< upwards acceleration (smoke rises)
    float   m_rot = 0.0f, m_spin = 0.0f;
    float   m_boost = 1.5f;   ///< brightness for the bloom
};

/** @brief A short-lived light. */
struct GlowLight
{
    Vector3 m_pos = { 0, 0, 0 };
    Color   m_color = WHITE;
    float   m_radius = 20.0f, m_power = 2.0f;
    float   m_age = 0.0f, m_life = 0.3f;
};

/** @brief A mark on the ground (skids, scorch). */
struct Mark
{
    Vector2 m_a = { 0, 0 }, m_b = { 0, 0 };
    float   m_w = 0.25f;
    float   m_alpha = 0.5f;
    Color   m_color = { 10, 10, 10, 255 };
};

/** @brief The picture of a race. */
class Renderer
{
public:
    /**
     * @brief Makes the targets for a picture size (the shaders are shared and made once).
     * @param[in] w, h     pixels
     * @param[in] quality  0 fast (the Steam Deck), 1 balanced, 2 fine
     */
    void init( int w, int h, int quality );

    /** @brief Lets go of everything it made. */
    void shutdown();

    /** @brief Builds the static scene of a track (the ground, road, walls, props, lamps). */
    void setTrack( const Track &t );

    /** @brief Spawns the effects of the World's last step (call after every step). */
    void events( const World &w );

    /** @brief Ages the effects and leaves skid marks (once a frame). */
    void update( const World &w, float dt );

    /** @brief Draws the race into the target. */
    void render( const World &w, const View &v );

    /**
     * @brief Draws one model, lit, turning on a dark floor (the garage, the gallery).
     * @param[in] model  the model
     * @param[in] paint  its colours
     * @param[in] yaw    its turn (radians)
     * @param[in] dist   the camera's distance (metres)
     */
    void showroom( const std::string &model, const Paint &paint, float yaw, float dist );

    /** @return the finished picture */
    const Texture2D &texture() const { return m_out.texture; }

    /** @return where a world point lands in the picture (pixels of the target) */
    Vector2 project( Vector3 p ) const;

    /** @brief A sound the pictures ask for (a flock taking off, a dog barking): the race plays them and clears the list. */
    struct Cue
    {
        int     m_kind = 0;   ///< 0 birds, 1 a bark, 2 thunder close by, 3 thunder far away
        Vector2 m_pos = { 0, 0 };
    };
    std::vector<Cue> m_cues;

    int m_roofCoffin = -1;   ///< the car carrying Graves' coffin on its roof (-1 none)
    /** @brief The coffin comes off the roof and lands on the road. */
    void dropCoffin( const World &w, int car );

    /** @brief The time trial's ghost: the best lap's car, drawn pale. */
    struct Ghost
    {
        bool        m_on = false;
        Vector2     m_pos = { 0, 0 };
        float       m_head = 0.0f;
        std::string m_model;
        Paint       m_paint;
    } m_ghost;
    std::vector<Ghost> m_spirits;   ///< the dead rivals' ghosts (the race moves them), drawn pale green

    /** @brief A point of the racing line on the road (the option): where, and its colour (green, yellow, red). */
    struct GuidePoint
    {
        Vector2 m_pos = { 0, 0 };
        Color   m_color = WHITE;
    };
    std::vector<GuidePoint> m_guide;   ///< the race sets it every frame (empty: none), drawn under the cars

    /** @brief The sponsor's sticker on a car's roof (text "" none). */
    void setSticker( const std::string &text, Color back, Color ink, int car );

    /** @brief Graves & Sons' slogan for the banners on the stands (the sponsor's name comes with the sticker). */
    void setSlogan( const std::string &text );

    /** @brief A car on the podium. */
    struct PodiumCar
    {
        std::string m_model;
        Paint       m_paint;
        bool        m_wreck = false;   ///< drawn as a smoking wreck (a derby's runners-up)
    };

    /**
     * @brief The podium after a race: the first three cars on stacked coffins, the crowd cheering behind, Graves and
     *        Ricky Rattle beside them, confetti and fireworks (the results screen; this renderer's own picture).
     * @param[in] cars  first, second, third (fewer: empty steps)
     * @param[in] dt    seconds since the last picture
     */
    void podium( const std::vector<PodiumCar> &cars, float dt );

    int   m_w = 0, m_h = 0;
    float m_hurt = 0.0f;    ///< a red edge (the player was hit)
    float m_fade = 0.0f;    ///< 0 .. 1 black
    float m_time = 0.0f;
    Camera3D m_cam = {};

    // The look (RenderLook.cpp, docs/DESIGN.md "The miniature look, the sky and contact shadows").
    bool m_peopleLineup = false;   ///< the podium shows every outfit and act in a row instead of its fans (--lineup)
    bool m_miniature = true;   ///< the tilt-shift blur at the top and bottom of the picture (the option)
    /** @brief What flies over the track, for the race's sound: loudness 0..1 of each and the Doppler pitch. */
    struct Hum
    {
        float m_rotor = 0.0f, m_airship = 0.0f, m_pitch = 1.0f;
    } m_hum;
    /**
     * @brief Sends something over the track at once (the pictures' tests).
     * @param[in] kind  0 the airship, 1 the helicopter, 2 a lightning strike in view
     */
    void skyTest( int kind );

    /** @return the shared Forge (models built once for every renderer) */
    static Forge &forge();

    /** @brief The stands, for the race's sound: how many, where each stands, how far it reaches. */
    int     standCount() const { return (int)m_stands.size(); }
    Vector2 standAt( int k ) const { return m_stands[(size_t)k].m_at; }
    float   standReach( int k ) const { return m_stands[(size_t)k].m_r; }
    /** @return the stands' joy after a finish (1 just then, fading over eight seconds) */
    float   joy() const { return m_joy; }

private:
    struct Chunk
    {
        Mesh      m_mesh = {};
        Rectangle m_box = { 0, 0, 0, 0 };
        bool      m_anim = false;   ///< its vertices carry marks (crowds, trees, flags: they move in the shader)
    };
    struct StaticLight
    {
        Vector3 m_pos;
        Color   m_color;
        float   m_radius, m_power;
    };

    void clearTrack();
    void buildGround();
    void buildRoad();
    void buildWalls();
    void buildProps();
    void buildSticker();
    std::string     m_sticker;
    Color           m_stickerBack = WHITE, m_stickerInk = BLACK;
    int             m_stickerCar = -1;
    bool            m_stickerDirty = false;
    RenderTexture2D m_stickerTex = {};
    void buildLife();
    void life( const World &w, float dt );
    void drawLife( bool shadow );
    void lights( const World &w );
    void shadowPass( const World &w );
    void drawScene( const World &w, bool shadow );
    void drawCar( const World &w, int i, bool shadow );
    void drawDecals( const World &w );
    void drawParticles();
    void drawBullets( const World &w );
    void post();
    void spawn( const Particle &p );
    void puff( Vector3 at, int n, float speed, float size, Color c0, Color c1, float life, int tex, bool add );
    void setLookUniforms();

    const Track            *m_track = nullptr;
    const VenueDef         *m_venue = nullptr;
    RenderTexture2D         m_hdr = {}, m_out = {}, m_shadow = {};
    std::vector<RenderTexture2D> m_bloom;
    int                     m_quality = 1;
    int                     m_shadowSize = 2048;
    Matrix                  m_lightVP = {};
    Vector3                 m_sunDir = { -0.4f, -0.8f, -0.3f };
    Mesh                    m_ground = {}, m_road = {}, m_walls = {};
    bool                    m_hasScene = false;
    std::vector<Chunk>      m_chunks;
    std::vector<StaticLight> m_lamps;
    std::vector<Particle>   m_parts;
    std::vector<GlowLight>  m_glows;
    std::vector<Mark>       m_marks;
    size_t                  m_markNext = 0;
    std::vector<Vector2>    m_lastWheel;   ///< per car, per rear wheel: where its last mark ended
    std::vector<float>      m_smokeTimer;
    std::vector<Vector4>    m_frameLights;   ///< xyz, radius
    std::vector<Vector4>    m_frameColors;   ///< rgb, power
    std::vector<Vector4>    m_spotPos, m_spotDir, m_spotCol;
    bool                    m_dark = false;
    float                   m_wet = 0.0f;
    float                   m_wetRace = 0.0f, m_fogRace = 0.0f;   ///< weather that came during the race
    std::vector<std::pair<std::string, Matrix>> m_turning;          ///< props whose spin group turns (wheels, carousels)
    struct Debris
    {
        std::string m_model;
        Vector3     m_pos, m_vel;
        float       m_rot = 0, m_spin = 0, m_age = 0;
        Color       m_paint = WHITE;
    };
    std::vector<Debris> m_debris;   ///< parts that came off the cars: thrown, then lying on the road
    std::vector<std::pair<Vector3, float>> m_fountains;   ///< broken hydrants: where, seconds left
    // Life by the road (docs/DESIGN.md "Life by the road"): only pictures, the race knows nothing of it.
    struct Person
    {
        std::string m_model;
        Vector2     m_pos = { 0, 0 }, m_away = { 1, 0 };   ///< where, and the way from the road
        float       m_rot = 0.0f, m_scale = 1.0f;          ///< radians
        bool        m_crowd = false;                       ///< a group (they hop back) or one (dives flat)
        float       m_t = -1.0f;                           ///< seconds into the fright (< 0 calm)
        float       m_cheer = 0.0f;                        ///< seconds of cheering left (a car passing near)
    };
    std::vector<Person> m_people;
    struct Bird
    {
        Vector3 m_pos = { 0, 0, 0 }, m_vel = { 0, 0, 0 };
        float   m_head = 0.0f, m_phase = 0.0f;
    };
    struct Flock
    {
        Vector2           m_home = { 0, 0 };
        float             m_t = -1.0f;   ///< seconds since it flew up (< 0 sitting)
        std::vector<Bird> m_birds;
    };
    std::vector<Flock> m_flocks;
    Color              m_birdTint = { 40, 38, 42, 255 };
    float              m_birdSize = 1.0f, m_birdFlap = 14.0f;
    struct Dog
    {
        Vector2 m_home = { 0, 0 }, m_pos = { 0, 0 }, m_along = { 0, 1 };   ///< its kennel, where it is, the road's way
        float   m_head = 0.0f, m_speed = 0.0f, m_phase = 0.0f, m_bark = 0.0f, m_calm = 0.0f;
        Color   m_tint = WHITE;
    };
    std::vector<Dog> m_dogs;
    // The stands (docs/DESIGN.md "Life on the stands, wind"): the crowd moves in the shader; here only what the CPU
    // must do: camera flashes at night, confetti and fireworks at a finish, the banners, the wind.
    struct Stand
    {
        Vector2              m_at = { 0, 0 };
        float                m_r = 10.0f;
        std::vector<Vector3> m_heads;   ///< the lively fans' heads
    };
    std::vector<Stand>      m_stands;
    std::vector<BannerSpot> m_banners;            ///< in the world
    RenderTexture2D         m_bannerTex = {};     ///< two rows: the sponsor (or Graves & Sons), the slogan
    bool                    m_bannerDirty = true;
    std::string             m_slogan;
    struct Shell
    {
        Vector3 m_pos = { 0, 0, 0 }, m_vel = { 0, 0, 0 };
        float   m_wait = 0.0f, m_fuse = 1.2f;   ///< seconds to the launch, then to the burst
        Color   m_color = WHITE;
    };
    std::vector<Shell> m_shells;                  ///< fireworks in the air
    float              m_joy = 0.0f;              ///< 1 after a finish, fading: every fan cheers
    float              m_windBase = 0.3f;         ///< the venue's wind (0 calm .. 1 storm)
    float              m_podiumTime = 0.0f;
    void setAnimUniforms( const World *w, float joy );
    void setAnimOn( bool shadow, bool on );
    void crowdLife( const World &w, float dt );
    void celebrate();
    void ageShells( float dt );
    void buildBanners();
    void drawBanners();
    float                   m_flash = 0.0f;      ///< lightning
    float                   m_flashTimer = 8.0f;
    View                    m_view;
    Rng                     m_rng;

    // The look (RenderLook.cpp): the venue's grade and the miniature's blur in the post pass, things flying over the
    // track (their shadows only: they fly above the camera), lightning, baked contact shadows.
    void lookInit();
    void lookShutdown();
    void lookTrack();
    void lookFrame( const World &w );
    void miniaturePasses( bool tilt );
    void setPostUniforms( Shader &f, bool tilt );
    void sky( const World &w, float dt );
    void strike( bool inView );
    void spawnFlyer( const World &w, int kind, float at );
    void skyLights( const World &w );
    void drawFlyer( bool shadow );
    void drawBolt();
    void drawBeam();                         ///< the helicopter's searchlight as a cone of light in the air (night)
    /** @brief A mesh lit like the cars and props (the helicopter in the picture: its colours' alpha is their glow). */
    void drawLit( const Mesh &m, Matrix xf );
    void drawTow( const World &w, bool shadow );   ///< Graves & Sons' hook and cable over a wreck; the marshals
    float m_tiltHalf = 0.2f;                 ///< the sharp band's half height (wider while a stand is in view)
    Vector3 m_beamFrom = { 0, 0, 0 }, m_beamTo = { 0, 0, 0 };   ///< the searchlight this frame (skyLights)
    float   m_beamOn = 0.0f;
    void bakeContact();
    void setContactUniforms( Shader &s, float up );
    struct Grade
    {
        Vector3 m_lo = { 0.92f, 0.96f, 1.08f }, m_hi = { 1.06f, 1.0f, 0.92f };   ///< tints of the darks and the lights
        float   m_amount = 0.6f, m_sat = 1.0f, m_contrast = 1.0f, m_vignette = 0.7f;
    } m_grade;
    RenderTexture2D m_blur[2] = {};          ///< the picture at a half and a quarter of its size (tilt-shift)
    float           m_tiltCentre = 0.45f;    ///< where the sharp band lies (texture v), following the player's car
    bool            m_tiltNow = false;       ///< this picture gets the blur (not the showroom)
    struct Flyer
    {
        int     m_kind = -1;                 ///< -1 none, 0 the airship, 1 the helicopter
        float   m_t = 0.0f, m_life = 0.0f;   ///< seconds since it came, how long it stays
        Vector2 m_ground = { 0, 0 };         ///< where its shadow falls
        Vector2 m_from = { 0, 0 }, m_to = { 0, 0 };   ///< the airship's straight way (its shadow's)
        Vector2 m_vel = { 0, 0 };
        float   m_head = 0.0f, m_rotor = 0.0f;
        int     m_car = -1;                  ///< the car the helicopter films
        float   m_side = 1.0f;               ///< which way it crosses
    } m_flyer;
    float m_flyerTimer = 18.0f;              ///< seconds until the next one
    int   m_skyTest = -1;                    ///< skyTest's wish, served by the next picture
    struct Bolt
    {
        float                m_t = -1.0f;    ///< seconds since it struck (< 0 none)
        Vector2              m_at = { 0, 0 };
        std::vector<Vector3> m_pts;          ///< the channel and its branches as segments (pairs)
        std::vector<float>   m_width;        ///< per segment
    } m_bolt;
    float m_flashT = -1.0f;                  ///< seconds into the flash sequence (< 0 none)
    float m_flashPeak = 1.0f;
    float m_thunderIn = -1.0f;               ///< seconds until the thunder (< 0 none)
    bool  m_thunderNear = false;
    RenderTexture2D m_contact = {};          ///< the baked contact shadows (r beside things, g under tall ones)
    Vector4         m_contactRect = { 0, 0, 0, 0 };   ///< x0, z0, 1 / size, on
};

}   // namespace grave
