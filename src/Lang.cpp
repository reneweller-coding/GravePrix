/**
 * @file Lang.cpp
 * @brief German and English.
 * @author Rene Weller
 */
#include "Lang.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <unordered_map>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

namespace grave
{

namespace
{

std::string                                  g_lang = "de";
std::unordered_map<std::string, std::string> g_en;
std::set<std::string>                        g_missing;

std::string trim( const std::string &s )
{
    size_t a = s.find_first_not_of( " \t\r" ), b = s.find_last_not_of( " \t\r" );
    return a == std::string::npos ? std::string() : s.substr( a, b - a + 1 );
}

/** @brief Reads "\n" in a line as a line break. */
std::string unescape( const std::string &s )
{
    std::string out;
    for ( size_t i = 0; i < s.size(); ++i )
    {
        if ( s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n' )
        {
            out += '\n';
            ++i;
        }
        else
            out += s[i];
    }
    return out;
}

}   // namespace

void setLanguage( const std::string &code ) { g_lang = code == "de" ? "de" : "en"; }

const std::string &language() { return g_lang; }

bool english() { return g_lang == "en"; }

std::string systemLanguage()
{
#ifdef _WIN32
    LANGID id = GetUserDefaultUILanguage();
    return PRIMARYLANGID( id ) == LANG_GERMAN ? "de" : "en";
#else
    for ( const char *var : { "LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE" } )
        if ( const char *v = getenv( var ) )
            if ( *v ) return std::string( v ).rfind( "de", 0 ) == 0 ? "de" : "en";
    return "en";
#endif
}

int loadUiTexts( const std::string &dataDir )
{
    g_en.clear();
    std::ifstream in( dataDir + "/lang/en.txt", std::ios::binary );
    std::string   line;
    int           n = 0;
    while ( std::getline( in, line ) )
    {
        if ( !line.empty() && line.back() == '\r' ) line.pop_back();
        if ( line.size() >= 3 && (unsigned char)line[0] == 0xEF ) line = line.substr( 3 );
        if ( line.empty() || line[0] == '#' ) continue;
        size_t eq = line.find( " = " );
        if ( eq == std::string::npos ) continue;
        std::string de = unescape( trim( line.substr( 0, eq ) ) ), en = unescape( trim( line.substr( eq + 3 ) ) );
        if ( !de.empty() && !en.empty() )
        {
            g_en[de] = en;
            ++n;
        }
    }
    return n;
}

const char *T( const char *de )
{
    if ( g_lang == "de" || !de || !*de ) return de;
    auto it = g_en.find( de );
    if ( it != g_en.end() ) return it->second.c_str();
    // "Name: " is translated as "Name:" (the file's keys are trimmed), its spaces kept round the English.
    std::string key = de, core = trim( key );
    if ( !core.empty() && core != key )
    {
        auto c = g_en.find( core );
        if ( c != g_en.end() )
        {
            size_t      a = key.find( core );
            std::string full = key.substr( 0, a ) + c->second + key.substr( a + core.size() );
            return g_en.emplace( key, full ).first->second.c_str();
        }
    }
    g_missing.insert( de );
    return de;
}

std::string T( const std::string &de ) { return T( de.c_str() ); }

const std::string &L( const std::string &de, const std::string &en )
{
    return g_lang != "de" && !en.empty() ? en : de;
}

std::string tf( const char *fmt, ... )
{
    char    buf[1024];
    va_list args;
    va_start( args, fmt );
    vsnprintf( buf, sizeof( buf ), T( fmt ), args );
    va_end( args );
    return buf;
}

std::string quote( const std::string &s )
{
    return english() ? "“" + s + "”" : "„" + s + "“";
}

std::vector<std::string> untranslated() { return std::vector<std::string>( g_missing.begin(), g_missing.end() ); }

}   // namespace grave
