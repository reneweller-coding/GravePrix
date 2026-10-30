/**
 * @file Lang.h
 * @brief The game's two languages, German and English. The interface's texts are written in German in the code
 *        and pass through T(): in English they are looked up in data/lang/en.txt ("German = English" lines; a text
 *        without a line stays German). The data carries both itself: "title"/"title_en", "desc"/"desc_en", and the
 *        stories in data/story/de and data/story/en (L() picks the one of the language, German where English is
 *        missing).
 * @author Rene Weller
 */
#pragma once

#include <string>
#include <vector>

namespace grave
{

/**
 * @brief Chooses the language.
 * @param[in] code  "de" or "en" (anything else counts as "en")
 */
void setLanguage( const std::string &code );

/** @return the chosen language: "de" or "en" */
const std::string &language();

/** @return whether the game speaks English */
bool english();

/** @return the language of the system's user interface: "de" for German, else "en" */
std::string systemLanguage();

/**
 * @brief Reads the interface's English texts.
 * @param[in] dataDir  the data folder (data/lang/en.txt in it)
 * @return how many lines were read
 */
int loadUiTexts( const std::string &dataDir );

/**
 * @brief An interface text in the chosen language.
 * @param[in] de  the German text (also the key)
 * @return it, or its English text
 */
const char *T( const char *de );

/** @copydoc T(const char *) */
std::string T( const std::string &de );

/**
 * @brief A data text in the chosen language.
 * @param[in] de  the German text
 * @param[in] en  the English text ("" none)
 * @return the English one in English where there is one, else the German one
 */
const std::string &L( const std::string &de, const std::string &en );

/**
 * @brief A formatted interface text: the German format passes through T() first, then printf fills it.
 * @param[in] fmt  the German format
 * @return the text
 */
std::string tf( const char *fmt, ... );

/** @return a text in the language's quotation marks („…“ in German, “…” in English) */
std::string quote( const std::string &s );

/** @return the German texts T() was asked for that have no English line (for the translators' check) */
std::vector<std::string> untranslated();

}   // namespace grave
