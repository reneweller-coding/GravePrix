/**
 * @file Audio.h
 * @brief The sound: effects synthesised at the start (no sample files), loops whose volume and pitch the race sets
 *        every frame (tyres, scraping, gravel, flames, rain, a venue's ambience), up to eight engine voices that
 *        follow each car's revs and load, and the music, streamed from assets/music/<name>.ogg with a crossfade.
 *        Without an audio device (--mute) every call does nothing.
 * @author Rene Weller
 */
#pragma once

#include <raylib.h>

#include <string>

namespace grave
{

/** @brief The sound effects. */
enum class SfxE : int
{
    // guns and weapons
    MG, TWINMG, CANNON, FLAME_START, RICOCHET, BULLET_HIT,
    ROCKET_LAUNCH, MINE_DROP, MINE_ARM, SPIKES_DROP, OIL_DROP, SMOKE_DROP, NITRO, TIRE_BURST,
    // impacts
    CRASH_LIGHT, CRASH_HEAVY, CRASH_WALL, METAL_CRUNCH, GLASS,
    EXPLOSION_SMALL, EXPLOSION_BIG, WRECK, BARREL,
    // the race
    COUNT_BEEP, COUNT_GO, LAP, FINAL_LAP, FINISH, WRONG_WAY, BELL,
    // pickups
    PICK_CASH, PICK_WRENCH, PICK_AMMO, PICK_NITRO, PICK_WEAPON, PICK_MYSTERY,
    // horns (one per horn kind in the car data)
    HORN_PLAIN, HORN_TRUCK, HORN_DIXIE, HORN_ICE, HORN_FUNERAL, HORN_SIREN, HORN_KLAXON,
    // the menus
    UI_MOVE, UI_OK, UI_BACK, UI_BUY, UI_DENY, UI_CASH, UI_PAPER, UI_STAMP,
    COUNT
};

/** @brief The loops: the race sets their volume (0: silent) and pitch every frame. */
enum class LoopE : int
{
    SKID,           ///< tyres squealing on hard ground
    SKID_LOOSE,     ///< sliding on dirt, sand, snow
    SCRAPE,         ///< metal along a wall
    GRAVEL,         ///< rolling over gravel, off the road
    FLAMER,         ///< the flamethrower
    FIRE,           ///< burning wrecks nearby
    RAIN,           ///< rain on the car
    WIND,           ///< wind (snow, desert, storms)
    // one ambience per venue
    AMB_JUNKYARD, AMB_CEMETERY, AMB_DOCKS, AMB_DESERT, AMB_REFINERY, AMB_SWAMP, AMB_DOWNTOWN, AMB_PASS,
    AMB_FAIRGROUND, AMB_PROVING, AMB_ESTATE,
    COUNT
};

/** @brief The kinds of engine voice (the cars' "engine" value). */
enum class EngineE : int
{
    TIN,        ///< a tiny three-cylinder, buzzy
    I4,         ///< an ordinary four-cylinder
    BOXER,      ///< a rally boxer, burbling, turbo whistle
    V6,
    V8,         ///< a muscle-car V8, deep and lumpy
    V12,        ///< a supercar, smooth and screaming
    DIESEL,     ///< a van or truck, clattering
    ROTARY,     ///< a high buzzing rotary
    TURBINE,    ///< a jet-like whine (prototypes, the boss)
    ELECTRIC,   ///< a whine with no explosions
    COUNT
};

/** @brief The sound. */
class Audio
{
public:
    /**
     * @brief Synthesises the effects, loops and engine voices (the audio device must be open).
     * @param[in] on      false: stay silent (every call does nothing)
     * @param[in] assets  the assets folder (music)
     */
    void init( bool on, const std::string &assets );

    /** @brief Lets go of everything. */
    void shutdown();

    /**
     * @brief Plays an effect.
     * @param[in] s       the effect
     * @param[in] pan     -1 left .. 1 right
     * @param[in] volume  0..1
     * @param[in] pitch   1 as made
     */
    void play( SfxE s, float pan = 0.0f, float volume = 1.0f, float pitch = 1.0f );

    /**
     * @brief Sets a loop (call every frame while it should sound; 0 volume fades it out).
     * @param[in] l       the loop
     * @param[in] volume  0..1
     * @param[in] pitch   1 as made
     */
    void loop( LoopE l, float volume, float pitch = 1.0f );

    /**
     * @brief Sets an engine voice (call every frame for every audible car; volume 0 fades it out).
     * @param[in] slot    0..7
     * @param[in] kind    the engine
     * @param[in] rpm     0 idle .. 1 the limiter
     * @param[in] load    0 coasting .. 1 full throttle
     * @param[in] volume  0..1 (distance already applied)
     * @param[in] pan     -1 left .. 1 right
     */
    void engine( int slot, EngineE kind, float rpm, float load, float volume, float pan );

    /** @brief Silences every engine and loop (pause, end of a race). */
    void quiet();

    /**
     * @brief Changes the music (with a crossfade); "" fades it out.
     * @param[in] name  a file in assets/music without its ending
     */
    void music( const std::string &name );

    /** @brief Keeps the music, loops and engines running (every frame). */
    void update( float dt );

    /**
     * @brief Writes every effect, every loop (4 s) and every engine kind (a rev sweep, 6 s) as WAV.
     * @param[in] dir  the folder
     */
    static void dump( const std::string &dir );

    /** @return the engine kind of a name ("v8"; TIN for unknown names) */
    static EngineE engineKind( const std::string &name );

    /** @return the horn effect of a name ("funeral"; HORN_PLAIN for unknown names) */
    static SfxE hornOf( const std::string &name );

    /** @return the ambience loop of a venue key ("cemetery"; AMB_JUNKYARD for unknown keys) */
    static LoopE ambienceOf( const std::string &venue );

    float m_musicVolume = 0.7f;
    float m_effectVolume = 0.8f;
    bool  m_on = false;

private:
    struct Impl;
    Impl *m_impl = nullptr;
};

}   // namespace grave
