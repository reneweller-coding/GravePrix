/**
 * @file Input.h
 * @brief Keys and pads: the drivers' actions (each with two keys and two pad buttons, rebound in the options), the
 *        pad's stick (it turns the wheels, or points the way) and triggers for steering and pedals, the menus' fixed
 *        keys with repeat, and which pad belongs
 *        to which driver (one driver: the keys and every pad; two: the first pad or the keys, and the second pad).
 *        Presses are kept until a step of the race takes them, so a quick tap is never lost between steps.
 * @author Rene Weller
 */
#pragma once

#include "Sim.h"

#include <string>
#include <vector>

namespace grave
{

/** @brief What can be bound. */
enum class ActE : int
{
    ACCEL, BRAKE, LEFT, RIGHT, HANDBRAKE, FIRE, DROP, ROCKET, NITRO, CYCLE, HORN, PAUSE, RESET, COUNT
};

/** @brief One action's keys and pad buttons (0: none). */
struct Bind
{
    int m_key = 0, m_key2 = 0;
    int m_pad = 0, m_pad2 = 0;
};

/** @brief The input. */
class Input
{
public:
    Input();

    /** @brief Once a frame before anything reads it. */
    void update();

    /**
     * @brief A driver's input for the next step.
     * @param[in] i        0 or 1
     * @param[in] players  how many drive
     * @param[in] aim      the car the pad's stick points the way for (Settings::m_stickAims; nullptr: the stick turns
     *                     the wheels, left and right)
     * @return the input (the presses since the last step included)
     */
    CarInput car( int i, int players, const Car *aim = nullptr );

    /**
     * @brief The stick points the way: the steering that turns a car's nose to a direction on the screen, a controller
     *        on the angle between them, damped by the turn the car already has (so it settles without swinging), the
     *        other way round while the car backs up.
     * @param[in]     want  the way (radians, 0 up the screen, clockwise)
     * @param[in]     car   the car
     * @param[in,out] side  the way it turned last (-1 left, 1 right): a stick held straight back keeps turning that way
     * @return the steering (-1 left .. 1 right)
     */
    static float aimSteer( float want, const Car &car, int &side );

    /** @return whether an action was pressed since the last step (the presses a step takes: drop, rocket, ... reset) */
    bool latched( ActE a, int i ) const { return m_latch[i & 1][(int)a]; }

    /** @brief Forgets the kept presses (a step took them). */
    void consumed();

    /** @return the menu moves (with key repeat) */
    bool up();
    bool down();
    bool left();
    bool right();
    /** @return accept (Enter, Space, A) */
    bool accept();
    /** @return back (Esc, Backspace, B) */
    bool back();
    /** @return the pause (Esc, P, Start) */
    bool pause();
    /** @return the tabs (Q/E, LB/RB) */
    bool prevTab();
    bool nextTab();

    /** @return a label for an action's binding ("Leertaste / J", "A") */
    std::string label( ActE a, bool pad ) const;

    /** @return a key pressed this frame (0 none), for rebinding */
    static int capturedKey();
    /** @return a pad button pressed this frame (0 none) */
    int capturedPad() const;

    /** @return the settings lines of the bindings */
    std::vector<std::string> save() const;
    /** @brief Reads one settings line ("bind <action> key key2 pad pad2"). */
    void load( const std::string &line );
    /** @brief Back to the defaults. */
    void reset();

    /** @return an action's name (German, through T()) */
    static const char *name( ActE a );
    /** @return a key's label */
    static std::string keyLabel( int key );
    /** @return a pad button's label */
    static std::string padLabel( int button );

    /** @return whether a pad was used last (the menus show its buttons) */
    bool padUsed() const { return m_padUsed; }
    /** @brief Makes the menus show the pad's buttons (the app test's "pad"). */
    void forcePad( bool on ) { m_padUsed = on; }

    /**
     * @brief Plays a script instead of the keys (the app test): one word a frame, each followed by idle frames.
     * @param[in] words  accept back up down left right pause prevtab nexttab, or anything else to wait a frame
     */
    void script( const std::vector<std::string> &words );

    /** @return whether a script plays */
    bool scripted() const { return m_scripted; }

    /** @return the scripted word of this frame ("" none) */
    const std::string &word() const { return m_word; }

    Bind m_binds[(int)ActE::COUNT];

private:
    bool held( ActE a, int mask, bool keys ) const;
    bool pressedAct( ActE a, int mask, bool keys ) const;
    void route( int i, int &mask, bool &keys ) const;
    bool repeat( bool down, int slot );
    bool padButtonDown( int pad, int b ) const;
    bool padButtonPressed( int pad, int b ) const;
    bool take( const char *w );

    float m_rep[4] = { 0, 0, 0, 0 };
    bool  m_prev[4] = { false, false, false, false };
    bool  m_latch[2][(int)ActE::COUNT] = {};
    int   m_aimSide[2] = { 0, 0 };   ///< aimSteer's side per driver
    bool  m_padUsed = false;
    bool  m_trigPrev[4][2] = {};
    bool  m_trigNow[4][2] = {};
    int   m_players = 1;
    bool  m_scripted = false;
    std::vector<std::string> m_script;
    size_t m_scriptAt = 0;
    int    m_idle = 0;
    std::string m_word;
};

}   // namespace grave
