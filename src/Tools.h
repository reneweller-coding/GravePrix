/**
 * @file Tools.h
 * @brief The command line and the tools for the designers and the tests: --validate, --trackshot(s), --gallery,
 *        --shot, --sim, --simall, --careersim; and the game itself (runGame, App.cpp).
 * @author Rene Weller
 */
#pragma once

#include <string>
#include <vector>

namespace grave
{

/** @brief The command line. */
struct Options
{
    std::string              m_root;   ///< the folder of data/ and assets/
    std::string              m_data;   ///< the data folder
    std::vector<std::string> m_args;

    /** @return whether a switch is there */
    bool has( const std::string &name ) const;
    /** @return the word after a switch ("" none) */
    std::string value( const std::string &name, const std::string &def = "" ) const;
    /** @return the word after a switch as a number */
    float num( const std::string &name, float def ) const;
    /** @return the second word after a switch ("" none) */
    std::string value2( const std::string &name, const std::string &def = "" ) const;
};

/** @brief Reads and checks all data and builds every track; 1 on errors. */
int toolValidate( const Options &opt );
/** @brief --trackshot NAME FILE: a track's map. */
int toolTrackshot( const Options &opt );
/** @brief --trackshots DIR: every track's map. */
int toolTrackshots( const Options &opt );
/** @brief --gallery FILE [prefix]: every model on a sheet. */
int toolGallery( const Options &opt );
/** @brief --shot TRACK FILE: a picture of a race. */
int toolShot( const Options &opt );
/** @brief --sim TRACK / --simall: computer races. */
int toolSim( const Options &opt );
/** @brief --careersim: a career played by the computer. */
int toolCareerSim( const Options &opt );
/** @brief The game. */
int runGame( const Options &opt );

}   // namespace grave
