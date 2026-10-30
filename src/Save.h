/**
 * @file Save.h
 * @brief What is kept between runs: the settings and the records (best laps, quick races won), plain "key value"
 *        text files in the player's folder. The career keeps its own file (Career::save).
 * @author Rene Weller
 */
#pragma once

#include <map>
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
    std::string m_language;                  ///< de, en ("" the system's)
    float       m_textSize = 1.0f;
    bool        m_rumble = true;
    float       m_shake = 1.0f;
    float       m_zoom = 1.0f;               ///< the camera's height
    bool        m_speech = true;             ///< the rivals' and the announcer's lines
    std::vector<std::string> m_binds;        ///< Input's lines

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

    void load( const std::string &path );
    bool save( const std::string &path ) const;
};

}   // namespace grave
