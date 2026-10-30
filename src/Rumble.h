/**
 * @file Rumble.h
 * @brief The pad's motors (raylib's desktop build has none): XInput on Windows (Xbox pads, the Steam Deck through
 *        Steam Input, Proton), force feedback through evdev on Linux. Pads without either stay still.
 * @author Rene Weller
 */
#pragma once

namespace grave
{
namespace rumble
{

/**
 * @brief Sets the motors until they are set again.
 * @param[in] low   the heavy, slow motor 0..1 (blasts, stomps)
 * @param[in] high  the light, quick one 0..1 (hits)
 */
void set( float low, float high );

/** @brief Both still (and the device let go). */
void stop();

}   // namespace rumble
}   // namespace grave
