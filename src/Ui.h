/**
 * @file Ui.h
 * @brief Drawing the interface: the font (Barlow) at any size, panels, bars, power pips, wrapped text, menu
 *        lists. Sizes are given for a 1080-line picture and scaled to the window (and the chosen text size).
 * @author Rene Weller
 */
#pragma once

#include <raylib.h>

#include <string>
#include <vector>

namespace grave
{
namespace ui
{

/** @brief Loads the font (the window must be open). */
void init( const std::string &assets );

/** @brief Lets go of it. */
void shutdown();

/** @brief Sets the chosen text size (1 as made). */
void setTextSize( float s );

/** @return the scale of 1080-line sizes to the window */
float k();

/** @brief Where text is anchored. */
enum class AlignE : int
{
    LEFT, CENTER, RIGHT
};

/**
 * @brief Draws text.
 * @param[in] s      the text (UTF-8)
 * @param[in] x, y   where (pixels)
 * @param[in] size   its size at 1080 lines
 * @param[in] c      the colour
 * @param[in] align  anchored left, centre or right
 * @return its width in pixels
 */
float text( const std::string &s, float x, float y, float size, Color c, AlignE align = AlignE::LEFT );

/** @return the width of a text in pixels */
float width( const std::string &s, float size );

/** @return a text's height in pixels */
float height( float size );

/**
 * @brief Breaks text into lines that fit.
 * @param[in] s      the text
 * @param[in] w      the width in pixels
 * @param[in] size   the size
 * @return the lines
 */
std::vector<std::string> wrap( const std::string &s, float w, float size );

/** @brief A glassy panel. */
void panel( Rectangle r, float alpha = 0.82f, Color edge = { 160, 124, 70, 255 } );

/** @brief A bar filled to a share. */
void bar( Rectangle r, float share, Color c, Color back = { 20, 26, 40, 220 } );

/** @brief Power pips: n of max filled. */
void pips( float x, float y, int n, int max, float size, Color on, Color off = { 40, 50, 70, 255 } );

/** @brief A selection highlight across a row. */
void highlight( Rectangle r, float t );

/** @brief The interface's colours. */
extern Color Ink, Dim, Accent, Gold, Warn, Good;

/**
 * @brief Picks the palette for colour vision.
 * @param[in] mode  0 as made, 1 red-green (good blue, bad orange), 2 blue-yellow (gold turns pink)
 */
void setPalette( int mode );

/** @brief Scales everything drawn after it (the HUD's size; 1 as made). */
void setScale( float s );

/**
 * @brief Draws a pad button as a glyph: A, B, X, Y as coloured round buttons, LB/RB/LT/RT/Start/Select as pills.
 * @param[in] label  the button's name ("A", "LB", ...)
 * @param[in] x, y   the top left (pixels)
 * @param[in] size   the size at 1080 lines
 * @return its width in pixels (0: not a pad button, nothing drawn)
 */
float glyph( const std::string &label, float x, float y, float size );

}   // namespace ui
}   // namespace grave
