/**
 * @file Track.h
 * @brief A track built from its data (docs/DESIGN.md "Tracks"): the turtle path walked into a closed centre line,
 *        sampled every two metres with its width, shoulder, surface and wall; the racing line and how fast it can be
 *        taken; pickups, hazards, obstacles, surface patches; the props beside it; and the questions the race asks:
 *        where along the track a point lies, whether it is still on the drivable ground (the road with its shoulders,
 *        all parts of the track together, so crossings work), which surface it stands on.
 * @author Rene Weller
 */
#pragma once

#include "Data.h"

#include <string>
#include <vector>

namespace grave
{

/** @brief One sample of the centre line (every Spacing metres). */
struct TrackSample
{
    Vector2  m_pos = { 0, 0 };     ///< the centre
    Vector2  m_dir = { 0, -1 };    ///< the way of travel (unit)
    Vector2  m_right = { 1, 0 };   ///< to the right of it (unit)
    float    m_s = 0.0f;           ///< metres from the start line
    float    m_half = 6.5f;        ///< half the road's width
    float    m_shoulder = 3.0f;    ///< the strip beyond the road on each side
    float    m_curv = 0.0f;        ///< curvature of the centre line (1/m, > 0 bends right)
    SurfaceE m_surface = SurfaceE::ASPHALT;
    int      m_wall = 0;           ///< the wall style on the left (Track::m_wallStyles)
    int      m_wall2 = 0;          ///< the wall style on the right
    bool     m_wallL = true;       ///< a wall stands on the left here (false at crossings)
    bool     m_wallR = true;
    bool     m_bridge = false;     ///< drawn as a bridge
    float    m_line = 0.0f;        ///< the racing line: metres to the right of the centre
    float    m_lineCurv = 0.0f;    ///< the racing line's curvature (1/m, unsigned)
    float    m_lineGrip = 1.0f;    ///< the grip of a patch on the racing line (1: none)
    float    m_dist = 0.0f;        ///< the racing line's length to the next sample
};

/** @brief Something to pick up. */
struct PickupSpot
{
    std::string m_kind;   ///< cash wrench ammo nitro mine rocket mystery
    Vector2     m_pos = { 0, 0 };
    float       m_s = 0.0f;
};

/** @brief A hazard placed on the road (an oil slick, an explosive barrel, a live mine). */
struct HazardSpot
{
    std::string m_kind;
    Vector2     m_pos = { 0, 0 };
    float       m_r = 2.5f;
};

/** @brief A solid round thing on the road. */
struct ObstacleSpot
{
    Vector2     m_pos = { 0, 0 };
    float       m_r = 1.0f;
    std::string m_model;
    float       m_rot = 0.0f;   ///< degrees
};

/** @brief A patch of another surface on the road. */
struct PatchSpot
{
    float    m_s0 = 0.0f, m_s1 = 0.0f;   ///< along the track (s0 may be > s1 across the start)
    float    m_lat0 = -1e9f, m_lat1 = 1e9f;   ///< metres right of the centre
    SurfaceE m_surface = SurfaceE::ICE;
};

/** @brief A prop beside the track. */
struct PropSpot
{
    std::string m_model;
    Vector2     m_pos = { 0, 0 };
    float       m_rot = 0.0f;     ///< degrees
    float       m_scale = 1.0f;
};

/** @brief Where a point lies along the track. */
struct TrackPos
{
    int   m_i = 0;        ///< the sample before it
    float m_s = 0.0f;     ///< metres from the start line
    float m_lat = 0.0f;   ///< metres right of the centre line
};

/** @brief A built track. */
class Track
{
public:
    static constexpr float Spacing = 2.0f;   ///< metres between samples

    /**
     * @brief Builds a track.
     * @param[in]  def       the track
     * @param[in]  venue     its venue (nullptr: defaults)
     * @param[in]  reverse   driven the other way round
     * @param[in]  mirror    mirrored left to right
     * @param[out] errors    what makes it unusable (appended)
     * @param[out] warnings  what should be fixed (appended)
     * @return false when it could not be built
     */
    bool build( const TrackDef &def, const VenueDef *venue, bool reverse, bool mirror,
                std::vector<std::string> &errors, std::vector<std::string> &warnings );

    /**
     * @brief Where a point lies along the track.
     * @param[in] p     the point
     * @param[in] hint  the sample it was near last time (-1: search everywhere); only a window around it is searched,
     *                  so a car at a crossing keeps to its own part
     * @return the place
     */
    TrackPos locate( Vector2 p, int hint ) const;

    /**
     * @brief How far a point lies outside the drivable ground (the road and its shoulders of all parts).
     * @param[in]  p     the point
     * @param[out] push  the way back in (unit), when outside
     * @return metres outside (<= 0: inside, by how much)
     */
    float outside( Vector2 p, Vector2 &push ) const;

    /** @return the surface at a point (the road's, a patch's, the shoulder's) */
    SurfaceE surfaceAt( Vector2 p, int hint ) const;

    /** @return the point at a distance along the track and metres to the right of the centre */
    Vector2 pointAt( float s, float lat ) const;

    /** @return the sample at a distance along the track */
    const TrackSample &sampleAt( float s ) const;

    /** @return the index of the sample at a distance along the track */
    int indexAt( float s ) const;

    /** @return a distance along the track brought into [0, length) */
    float wrap( float s ) const;

    /** @return b - a along the track, in (-length/2, length/2] */
    float delta( float a, float b ) const;

    /**
     * @brief The starting places: two columns behind the start line, or spread out in an arena.
     * @param[in] n  how many cars
     * @return per car the place and the heading (radians)
     */
    std::vector<std::pair<Vector2, float>> grid( int n ) const;

    /**
     * @brief Draws a map of the track into a picture (for the designers; no window needed).
     * @param[in] path  a .png
     * @param[in] cat   the catalog (prop sizes)
     * @return whether it was written
     */
    bool map( const std::string &path, const Catalog &cat ) const;

    std::string               m_name;
    const TrackDef           *m_def = nullptr;
    const VenueDef           *m_venue = nullptr;
    bool                      m_reverse = false, m_mirror = false, m_arena = false;
    std::vector<TrackSample>  m_s;
    float                     m_length = 0.0f;
    float                     m_step = Spacing;   ///< the samples' true spacing (the length shared out evenly)
    int                       m_laps = 4;
    std::vector<std::string>  m_wallStyles;     ///< the styles the samples name
    std::vector<PickupSpot>   m_pickups;
    std::vector<HazardSpot>   m_hazards;
    std::vector<ObstacleSpot> m_obstacles;
    std::vector<PatchSpot>    m_patches;
    std::vector<PropSpot>     m_props;
    std::vector<Vector2>      m_problems;       ///< where the checks found something (drawn on the map)
    int                       m_crossings = 0;
    Rectangle                 m_bounds = { 0, 0, 0, 0 };   ///< the drivable ground's box (x, z)

private:
    void walk( const TrackDef &def, const VenueDef *venue, bool mirror, std::vector<std::string> &warnings,
               std::vector<std::string> &errors );
    void resample();
    void racingLine();
    void buildGrid();
    void clipWalls();
    void check( std::vector<std::string> &warnings );
    void place( const TrackDef &def, const Catalog &cat );
    void scatter( const Catalog &cat );
    float segDist( int i, Vector2 p, float &t ) const;

    struct Raw
    {
        Vector2 m_pos;
        float   m_half, m_shoulder;
        int     m_surface, m_wall;
        bool    m_closing;   ///< made by close (the checks treat it apart)
        int     m_wall2 = -1;  ///< the right side's style (-1: as m_wall)
    };
    std::vector<Raw>              m_raw;
    std::vector<char>             m_closing;   ///< per sample: on the closing curve
    std::vector<std::vector<int>> m_cells;
    int                           m_gw = 0, m_gh = 0;
    float                         m_cell = 16.0f;
    Vector2                       m_origin = { 0, 0 };
    float                         m_reach = 12.0f;   ///< the largest half width + shoulder
};

}   // namespace grave
