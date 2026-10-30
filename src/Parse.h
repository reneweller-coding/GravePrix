/**
 * @file Parse.h
 * @brief Reading the data files (docs/DESIGN.md, "Data syntax"): every file is lines of words. A line whose first
 *        word names a block ("car rustbucket") starts that block when it stands at the start of the line; the
 *        indented lines after it belong to it. "key=value" words are named values, other words are in order;
 *        "quoted text" is one word; "#" starts a comment; a line starting with ">" is a line of prose for the block.
 *        A number may be a range "a..b": the value grows from a at the first level to b at the last.
 * @author Rene Weller
 */
#pragma once

#include "Util.h"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace grave
{

/** @brief A number that may grow over a range of levels: "4" or "4..9". */
struct Range
{
    float m_a = 0.0f;   ///< at the first level
    float m_b = 0.0f;   ///< at the last

    /** @return the value at t in [0, 1] */
    float at( float t ) const { return m_a + ( m_b - m_a ) * t; }
};

/** @brief One line of a data file. */
struct Line
{
    std::string                                  m_key;    ///< its first word
    std::vector<std::string>                     m_args;   ///< the words after it without a "="
    std::unordered_map<std::string, std::string> m_kv;     ///< the "key=value" words
    std::string                                  m_file;   ///< where it stands (for messages)
    int                                          m_line = 0;

    /** @return whether a named value is there */
    bool has( const std::string &k ) const { return m_kv.count( k ) > 0; }
    /** @return a named value as text */
    std::string str( const std::string &k, const std::string &def = "" ) const;
    /** @return a named value as a number (a range gives its first value) */
    float num( const std::string &k, float def = 0.0f ) const;
    /** @return a named value as a whole number */
    int integer( const std::string &k, int def = 0 ) const { return (int)std::lround( num( k, (float)def ) ); }
    /** @return a named value as a range ("a..b"; a plain number is a range of one value) */
    Range range( const std::string &k, float def = 0.0f ) const;
    /** @return a named value as a colour */
    Color color( const std::string &k, Color def = WHITE ) const;
    /** @return the i-th word after the key ("" if missing) */
    std::string arg( size_t i, const std::string &def = "" ) const { return i < m_args.size() ? m_args[i] : def; }
    /** @return the i-th word as a number */
    float argNum( size_t i, float def = 0.0f ) const;
    /** @return "file:line" */
    std::string where() const { return m_file + ":" + std::to_string( m_line ); }
};

/** @brief A block of lines: "car rustbucket" and what follows it. */
struct Block
{
    std::string              m_type;    ///< its first word (car, model, track ...)
    std::string              m_name;    ///< its second word
    Line                     m_head;    ///< the first line itself (named values on it count)
    std::vector<Line>        m_lines;   ///< the lines after it
    std::vector<std::string> m_prose;   ///< its ">" lines
};

/**
 * @brief Reads a data file into blocks.
 * @param[in]  path    the file
 * @param[in]  types   the words that start a block
 * @param[out] out     the blocks (appended)
 * @param[out] errors  messages (appended)
 * @return false if the file could not be read
 */
bool readBlocks( const std::string &path, const std::vector<std::string> &types, std::vector<Block> &out,
                 std::vector<std::string> &errors );

/**
 * @brief Checks a line's named values against the known ones.
 * @param[in]  l         the line
 * @param[in]  known     the names it may carry
 * @param[out] warnings  a message per unknown name (appended)
 */
void checkKeys( const Line &l, const std::set<std::string> &known, std::vector<std::string> &warnings );

/**
 * @brief Reads one line of data written in code (the built-in models).
 * @param[in] text  the words, as in a data file
 * @return the line
 */
Line parseLine( const std::string &text );

/** @return a word as a number (def when it is none) */
float toNumber( const std::string &s, float def );

}   // namespace grave
