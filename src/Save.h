/**
 * @file Save.h
 * @brief What is kept between runs: the settings and the records (best laps, quick races won), plain "key value"
 *        text files in the player's folder. The career keeps its own file (Career::save).
 * @author Rene Weller
 */
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace grave
{

/** @brief The options. */
struct Settings
{
    float       m_music = 0.7f;
    float       m_effects = 0.8f;
    bool        m_fullscreen = false;
    int         m_width = 0, m_height = 0;   ///< the window (0: by the screen)
    int         m_fps = 60;                  ///< 0: no limit
    bool        m_vsync = true;
    int         m_quality = -1;              ///< 0 fast, 1 balanced, 2 fine (-1: by the machine)
    bool        m_miniature = true;          ///< the miniature look: the picture's top and bottom blurred
    std::string m_language;                  ///< de, en ("" the system's)
    float       m_textSize = 1.0f;
    bool        m_rumble = true;
    float       m_shake = 1.0f;
    float       m_zoom = 1.0f;               ///< the camera's height
    bool        m_speech = true;             ///< the rivals' and the announcer's lines
    float       m_hud = 1.0f;                ///< the race display's size
    int         m_palette = 0;               ///< colour vision: 0 as made, 1 red-green, 2 blue-yellow
    bool        m_tags = false;              ///< names over the rivals' cars
    int         m_assist = 1;                ///< the player's driving assist: 0 off, 1 on, 2 on with the brake help
    std::vector<std::string> m_binds;        ///< Input's lines
    bool        m_paceNotes = true;          ///< the corner arrows in the race (pace notes)
    bool        m_stickAims = false;         ///< the pad's stick points the way (else it turns the wheels)

    /** @brief Reads them (missing values stay at their defaults). */
    void load( const std::string &path );
    /** @brief Writes them. */
    bool save( const std::string &path ) const;
};

/** @brief The records. */
struct Records
{
    std::map<std::string, float> m_bestLap;   ///< track (with ":r" reversed) -> seconds
    int m_quickWins = 0, m_derbyWins = 0, m_careersWon = 0;
    int m_tourBest = 0;   ///< the endless tournament: most rounds survived
    std::set<std::string>      m_awards;            ///< the epitaphs earned (data/awards.txt)
    std::map<std::string, int> m_count;             ///< what the epitaphs count over all races: wins, wrecks, mines ...
    std::set<std::string>      m_seen;              ///< venues ("v:name") and tracks ("t:name") raced on
    bool                       m_tutorial = false;  ///< the test drive passed

    void load( const std::string &path );
    bool save( const std::string &path ) const;
};

}   // namespace grave
