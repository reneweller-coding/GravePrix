/**
 * @file Parse.cpp
 * @brief Reading the data files: words, lines, blocks.
 * @author Rene Weller
 */
#include "Parse.h"

#include "Lang.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace grave
{

namespace fs = std::filesystem;

namespace
{

/** @brief The palette's named colours (models and patterns may use them instead of "#rrggbb"). */
const std::map<std::string, Color> &palette()
{
    static const std::map<std::string, Color> p = {
        { "white", { 240, 240, 240, 255 } },  { "black", { 14, 14, 18, 255 } },
        { "steel", { 150, 160, 172, 255 } },  { "gunmetal", { 72, 78, 88, 255 } },
        { "graphite", { 44, 46, 52, 255 } },  { "silver", { 200, 206, 214, 255 } },
        { "ivory", { 228, 222, 204, 255 } },  { "sand", { 196, 172, 128, 255 } },
        { "rust", { 150, 72, 40, 255 } },     { "crimson", { 178, 30, 44, 255 } },
        { "red", { 230, 50, 40, 255 } },      { "orange", { 255, 140, 30, 255 } },
        { "amber", { 255, 180, 40, 255 } },   { "gold", { 230, 186, 70, 255 } },
        { "yellow", { 250, 230, 70, 255 } },  { "lime", { 160, 240, 60, 255 } },
        { "green", { 60, 190, 80, 255 } },    { "olive", { 100, 110, 60, 255 } },
        { "teal", { 30, 150, 150, 255 } },    { "cyan", { 60, 220, 255, 255 } },
        { "sky", { 110, 180, 255, 255 } },    { "blue", { 50, 90, 220, 255 } },
        { "navy", { 26, 36, 90, 255 } },      { "violet", { 140, 80, 230, 255 } },
        { "magenta", { 230, 60, 200, 255 } }, { "pink", { 255, 130, 180, 255 } },
        { "bone", { 214, 204, 180, 255 } },   { "flesh", { 170, 100, 90, 255 } },
        { "moss", { 70, 100, 50, 255 } },     { "chitin", { 60, 44, 40, 255 } },
        { "plasma", { 120, 200, 255, 255 } }, { "ember", { 255, 96, 32, 255 } },
    };
    return p;
}

/** @brief Splits a line into words: "quoted text" is one word, "#" outside quotes ends the line. */
std::vector<std::string> words( const std::string &s )
{
    std::vector<std::string> out;
    std::string              cur;
    bool                     quoted = false, any = false;
    for ( size_t i = 0; i < s.size(); ++i )
    {
        char c = s[i];
        if ( quoted )
        {
            if ( c == '"' )
                quoted = false;
            else
                cur += c;
            continue;
        }
        if ( c == '"' )
        {
            quoted = true;
            any = true;
            continue;
        }
        if ( c == '#' )
        {
            // "#" starts a comment unless it begins a colour ("color=#ff0000", or "#ff0000" as a word of its own).
            bool hexNext = i + 1 < s.size() && isxdigit( (unsigned char)s[i + 1] );
            bool colour = hexNext && ( ( !cur.empty() && cur.back() == '=' ) || ( cur.empty() && !out.empty() ) );
            if ( !colour ) break;
        }
        if ( c == ' ' || c == '\t' || c == '\r' )
        {
            if ( !cur.empty() || any ) out.push_back( cur );
            cur.clear();
            any = false;
            continue;
        }
        cur += c;
        any = true;
    }
    if ( !cur.empty() || any ) out.push_back( cur );
    return out;
}

/** @brief Turns words into a line. */
Line makeLine( const std::vector<std::string> &w, size_t first, const std::string &file, int no )
{
    Line l;
    l.m_file = file;
    l.m_line = no;
    if ( first < w.size() ) l.m_key = w[first];
    for ( size_t i = first + 1; i < w.size(); ++i )
    {
        size_t eq = w[i].find( '=' );
        // "title "Text" title_en "Text"": a word ending in _en names the next word (the English beside the German).
        bool en = w[i].size() > 3 && w[i].compare( w[i].size() - 3, 3, "_en" ) == 0 && i + 1 < w.size() &&
                  w[i].find_first_not_of( "abcdefghijklmnopqrstuvwxyz_" ) == std::string::npos;
        if ( en )
        {
            l.m_kv[w[i]] = w[i + 1];
            ++i;
        }
        else if ( eq != std::string::npos && eq > 0 )
            l.m_kv[w[i].substr( 0, eq )] = w[i].substr( eq + 1 );
        else
            l.m_args.push_back( w[i] );
    }
    return l;
}

float toNum( const std::string &s, float def )
{
    if ( s.empty() ) return def;
    char *end = nullptr;
    float v = strtof( s.c_str(), &end );
    return end == s.c_str() ? def : v;
}

Range toRange( const std::string &s, float def )
{
    size_t dots = s.find( ".." );
    if ( dots == std::string::npos )
    {
        float v = toNum( s, def );
        return { v, v };
    }
    return { toNum( s.substr( 0, dots ), def ), toNum( s.substr( dots + 2 ), def ) };
}

/** @brief Checks a line's named values against the known ones. */
void checkKeysImpl( const Line &l, const std::set<std::string> &known, std::vector<std::string> &warnings )
{
    for ( const auto &kv : l.m_kv )
        if ( !known.count( kv.first ) )
            warnings.push_back( l.where() + ": unknown value \"" + kv.first + "\" on \"" + l.m_key + "\"" );
}

}   // namespace

Color parseColor( const std::string &text, Color def )
{
    if ( text.empty() ) return def;
    if ( text[0] == '#' )
    {
        std::string h = text.substr( 1 );
        auto        hex = []( const std::string &s ) { return (unsigned char)strtol( s.c_str(), nullptr, 16 ); };
        if ( h.size() == 3 )
            return { (unsigned char)( hex( h.substr( 0, 1 ) ) * 17 ), (unsigned char)( hex( h.substr( 1, 1 ) ) * 17 ),
                     (unsigned char)( hex( h.substr( 2, 1 ) ) * 17 ), 255 };
        if ( h.size() == 6 ) return { hex( h.substr( 0, 2 ) ), hex( h.substr( 2, 2 ) ), hex( h.substr( 4, 2 ) ), 255 };
        if ( h.size() == 8 )
            return { hex( h.substr( 0, 2 ) ), hex( h.substr( 2, 2 ) ), hex( h.substr( 4, 2 ) ),
                     hex( h.substr( 6, 2 ) ) };
        return def;
    }
    auto it = palette().find( text );
    return it != palette().end() ? it->second : def;
}

std::string Line::str( const std::string &k, const std::string &def ) const
{
    auto it = m_kv.find( k );
    return it != m_kv.end() ? it->second : def;
}

float Line::num( const std::string &k, float def ) const
{
    auto it = m_kv.find( k );
    return it != m_kv.end() ? toRange( it->second, def ).m_a : def;
}

Range Line::range( const std::string &k, float def ) const
{
    auto it = m_kv.find( k );
    return it != m_kv.end() ? toRange( it->second, def ) : Range{ def, def };
}

Color Line::color( const std::string &k, Color def ) const
{
    auto it = m_kv.find( k );
    return it != m_kv.end() ? parseColor( it->second, def ) : def;
}

float Line::argNum( size_t i, float def ) const
{
    return i < m_args.size() ? toNum( m_args[i], def ) : def;
}

bool readBlocks( const std::string &path, const std::vector<std::string> &types, std::vector<Block> &out,
                 std::vector<std::string> &errors )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
    {
        errors.push_back( path + ": cannot be read" );
        return false;
    }
    std::string file = fs::path( path ).filename().string();
    std::string parent = fs::path( path ).parent_path().filename().string();
    if ( !parent.empty() && parent != "data" ) file = parent + "/" + file;
    std::string line;
    int         no = 0;
    Block      *cur = nullptr;
    while ( std::getline( in, line ) )
    {
        ++no;
        if ( !line.empty() && line.back() == '\r' ) line.pop_back();
        if ( no == 1 && line.size() >= 3 && (unsigned char)line[0] == 0xEF ) line = line.substr( 3 );   // BOM
        size_t first = line.find_first_not_of( " \t" );
        if ( first == std::string::npos ) continue;
        if ( line[first] == '>' )
        {
            if ( !cur )
            {
                errors.push_back( file + ":" + std::to_string( no ) + ": prose outside a block" );
                continue;
            }
            std::string text = line.substr( first + 1 );
            if ( !text.empty() && text[0] == ' ' ) text = text.substr( 1 );
            cur->m_prose.push_back( text );
            continue;
        }
        if ( line[first] == '#' ) continue;
        std::vector<std::string> w = words( line.substr( first ) );
        if ( w.empty() ) continue;
        // A block starts at the line's beginning; the block's own lines are indented.
        if ( first == 0 && std::find( types.begin(), types.end(), w[0] ) != types.end() )
        {
            out.push_back( {} );
            cur = &out.back();
            cur->m_type = w[0];
            cur->m_name = w.size() > 1 ? w[1] : "";
            cur->m_head = makeLine( w, 0, file, no );
            continue;
        }
        if ( !cur )
        {
            errors.push_back( file + ":" + std::to_string( no ) + ": \"" + w[0] + "\" outside a block" );
            continue;
        }
        cur->m_lines.push_back( makeLine( w, 0, file, no ) );
    }
    return true;
}
void checkKeys( const Line &l, const std::set<std::string> &known, std::vector<std::string> &warnings )
{
    checkKeysImpl( l, known, warnings );
}

float toNumber( const std::string &s, float def ) { return toNum( s, def ); }

Line parseLine( const std::string &text ) { return makeLine( words( text ), 0, "code", 0 ); }

}   // namespace grave
