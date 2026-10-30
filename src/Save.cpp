/**
 * @file Save.cpp
 * @brief The settings and the records.
 * @author Rene Weller
 */
#include "Save.h"

#include "Files.h"

#include <fstream>
#include <sstream>

namespace grave
{

void Settings::load( const std::string &path )
{
    std::ifstream in( path );
    std::string   line;
    while ( std::getline( in, line ) )
    {
        if ( !line.empty() && line.back() == '\r' ) line.pop_back();
        std::istringstream ss( line );
        std::string        k;
        ss >> k;
        if ( k == "music" ) ss >> m_music;
        else if ( k == "effects" ) ss >> m_effects;
        else if ( k == "fullscreen" ) ss >> m_fullscreen;
        else if ( k == "window" ) ss >> m_width >> m_height;
        else if ( k == "fps" ) ss >> m_fps;
        else if ( k == "vsync" ) ss >> m_vsync;
        else if ( k == "quality" ) ss >> m_quality;
        else if ( k == "language" ) ss >> m_language;
        else if ( k == "textsize" ) ss >> m_textSize;
        else if ( k == "rumble" ) ss >> m_rumble;
        else if ( k == "shake" ) ss >> m_shake;
        else if ( k == "zoom" ) ss >> m_zoom;
        else if ( k == "speech" ) ss >> m_speech;
        else if ( k == "bind" ) m_binds.push_back( line );
    }
}

bool Settings::save( const std::string &path ) const
{
    FILE *f = beginWrite( path );
    if ( !f ) return false;
    fprintf( f, "music %.2f\neffects %.2f\nfullscreen %d\nwindow %d %d\nfps %d\nvsync %d\nquality %d\n", m_music, m_effects,
             m_fullscreen ? 1 : 0, m_width, m_height, m_fps, m_vsync ? 1 : 0, m_quality );
    if ( !m_language.empty() ) fprintf( f, "language %s\n", m_language.c_str() );
    fprintf( f, "textsize %.2f\nrumble %d\nshake %.2f\nzoom %.2f\nspeech %d\n", m_textSize, m_rumble ? 1 : 0, m_shake, m_zoom,
             m_speech ? 1 : 0 );
    for ( const std::string &b : m_binds ) fprintf( f, "%s\n", b.c_str() );
    return endWrite( f, path );
}

void Records::load( const std::string &path )
{
    std::ifstream in( path );
    std::string   line;
    while ( std::getline( in, line ) )
    {
        std::istringstream ss( line );
        std::string        k;
        ss >> k;
        if ( k == "lap" )
        {
            std::string t;
            float       s = 0;
            ss >> t >> s;
            if ( s > 0 ) m_bestLap[t] = s;
        }
        else if ( k == "wins" ) ss >> m_quickWins >> m_derbyWins >> m_careersWon;
    }
}

bool Records::save( const std::string &path ) const
{
    FILE *f = beginWrite( path );
    if ( !f ) return false;
    fprintf( f, "wins %d %d %d\n", m_quickWins, m_derbyWins, m_careersWon );
    for ( const auto &[t, s] : m_bestLap ) fprintf( f, "lap %s %.3f\n", t.c_str(), s );
    return endWrite( f, path );
}

}   // namespace grave
