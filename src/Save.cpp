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
        else if ( k == "hud" ) ss >> m_hud;
        else if ( k == "palette" ) ss >> m_palette;
        else if ( k == "tags" ) ss >> m_tags;
        else if ( k == "assist" ) ss >> m_assist;
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
    fprintf( f, "hud %.2f\npalette %d\ntags %d\nassist %d\n", m_hud, m_palette, m_tags ? 1 : 0, m_assist ? 1 : 0 );
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
        else if ( k == "tour" ) ss >> m_tourBest;
        else if ( k == "award" )
        {
            std::string a;
            if ( ss >> a ) m_awards.insert( a );
        }
        else if ( k == "count" )
        {
            std::string a;
            int         n = 0;
            if ( ss >> a >> n ) m_count[a] = n;
        }
        else if ( k == "seen" )
        {
            std::string a;
            if ( ss >> a ) m_seen.insert( a );
        }
        else if ( k == "tutorial" ) ss >> m_tutorial;
    }
}

bool Records::save( const std::string &path ) const
{
    FILE *f = beginWrite( path );
    if ( !f ) return false;
    fprintf( f, "wins %d %d %d\ntour %d\n", m_quickWins, m_derbyWins, m_careersWon, m_tourBest );
    for ( const auto &[t, s] : m_bestLap ) fprintf( f, "lap %s %.3f\n", t.c_str(), s );
    fprintf( f, "tutorial %d\n", m_tutorial ? 1 : 0 );
    for ( const std::string &a : m_awards ) fprintf( f, "award %s\n", a.c_str() );
    for ( const auto &[a, n] : m_count ) fprintf( f, "count %s %d\n", a.c_str(), n );
    for ( const std::string &a : m_seen ) fprintf( f, "seen %s\n", a.c_str() );
    return endWrite( f, path );
}

}   // namespace grave
