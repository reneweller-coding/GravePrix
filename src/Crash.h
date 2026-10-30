/**
 * @file Crash.h
 * @brief When the game crashes: a report in "crashes" in the player's folder – on Windows a minidump (for a debugger)
 *        and a text, on Linux the text with the call stack – with what was played.
 * @author Rene Weller
 */
#pragma once

#include <string>

namespace grave
{
namespace crash
{

/**
 * @brief Watches for a crash from now on.
 * @param[in] dir  where the reports go (made when one is written)
 * @param[in] log  the game's log, named in the report
 */
void install( const std::string &dir, const std::string &log );

/**
 * @brief What is played now (the level, the hero), for the report.
 * @param[in] text  a line
 */
void context( const std::string &text );

/** @return the newest report from before this start ("" none), and it is marked as seen */
std::string unseen();

}   // namespace crash
}   // namespace grave
