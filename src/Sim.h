/**
 * @file Sim.h
 * @brief The race itself, without a window: the cars (arcade physics on the ground plane: grip, slides, the
 *        surfaces, walls and each other), their guns and gear (bullets, flames, rockets, mines, spikes, oil, smoke,
 *        nitro), the pickups, the barrels, the laps and places, wrecks that stay on the track, and the computer
 *        drivers (Ai.cpp). It steps 60 times a second; the same seed and inputs give the same race everywhere.
 *        What happened in a step is told in m_events (the sound, the pictures and the commentator read them).
 * @author Rene Weller
 */
#pragma once

#include "Data.h"
#include "Forge.h"
#include "Track.h"

#include <string>
#include <vector>

namespace grave
{

/** @brief What a race is. */
enum class ModeE : int
{
    RACE,       ///< laps; the first across the line wins
    DERBY,      ///< an arena; the last car driving wins
    TRIAL,      ///< alone against the clock
};

/** @brief What a driver does in a step. */
struct CarInput
{
    float m_throttle = 0.0f;   ///< 0..1
    float m_brake = 0.0f;      ///< 0..1 (also reverse when slow)
    float m_steer = 0.0f;      ///< -1 left .. 1 right
    bool  m_handbrake = false;
    bool  m_fire = false;      ///< the front gun (held)
    bool  m_drop = false;      ///< the rear gear (pressed: one)
    bool  m_rocket = false;    ///< a rocket (pressed: one)
    bool  m_nitro = false;     ///< a nitro canister (pressed: one)
    bool  m_cycle = false;     ///< the next rear gear (pressed)
    bool  m_horn = false;      ///< pressed
};

/** @brief The rear gear a car can drop. */
enum class RearE : int
{
    MINE, SPIKES, OIL, SMOKE, COUNT
};

/** @brief What a car brings to a race. */
struct CarSetup
{
    std::string m_car;                 ///< CarDef name
    std::string m_driver;              ///< the name shown
    std::string m_rival;               ///< RivalDef key ("" the player or a nobody)
    Paint       m_paint;
    bool        m_human = false;
    int         m_pad = 0;             ///< which player (0 or 1) for humans
    int         m_engine = 0, m_tires = 0, m_armor = 0, m_gunLv = 0;   ///< upgrade levels
    std::string m_gun;                 ///< "" the car's own
    int         m_rear[(int)RearE::COUNT] = {};   ///< mines, spikes, oil, smoke
    int         m_rockets = 0, m_nitro = 0;
    bool        m_ram = false;         ///< a ram plate
    int         m_plating = 0;         ///< armour plates
    float       m_hpShare = 1.0f;      ///< starts damaged (sabotage)
    float       m_skill = 0.6f, m_aggression = 0.5f, m_weapons = 0.5f;   ///< computer drivers
    std::string m_likes = "mine";
};

/** @brief What a computer driver keeps between steps. */
struct AiState
{
    float m_lane = 0.0f;         ///< metres off the racing line it aims for
    float m_laneGoal = 0.0f;
    float m_stuck = 0.0f;        ///< seconds without moving
    float m_reverse = 0.0f;      ///< seconds left backing out
    float m_wobble = 0.0f;       ///< a mistake in its steering
    float m_wobbleTime = 0.0f;
    float m_fireHold = 0.0f;     ///< seconds it keeps shooting
    float m_think = 0.0f;        ///< seconds to the next decision about gear
    float m_ram = 0.0f;          ///< seconds it goes for a ram
    int   m_target = -1;         ///< the car it is after (derby, ramming)
    float m_mistake = 0.0f;      ///< a late brake
    float m_gearWait = 0.0f;     ///< seconds before it drops or fires gear again
};

/** @brief A car in the race. */
struct Car
{
    CarSetup      m_setup;
    const CarDef *m_def = nullptr;
    const GunDef *m_gun = nullptr;
    // What it can do (the car, its upgrades, the difficulty).
    float m_power = 11, m_top = 36, m_grip = 1, m_brakeF = 1, m_steerF = 1, m_offroad = 1, m_maxHp = 100;
    float m_mass = 1200, m_inertia = 2000, m_len = 4.4f, m_wid = 1.8f;
    float m_dmgTaken = 1.0f, m_dmgDealt = 1.0f;   ///< the difficulty's
    // Where it is.
    Vector2 m_pos = { 0, 0 }, m_vel = { 0, 0 };
    float   m_head = 0.0f, m_yaw = 0.0f;   ///< heading (radians, 0 north, clockwise) and its rate
    // The race.
    float m_hp = 100.0f;
    bool  m_wrecked = false;
    float m_wreckTime = 0.0f;
    int   m_hint = -1;            ///< the track sample it was near
    float m_s = 0.0f;             ///< where along the lap
    float m_dist = 0.0f;          ///< how far raced (laps x length + s, from the start line)
    int   m_lap = 0;              ///< laps completed
    bool  m_finished = false;
    float m_finishTime = 0.0f;
    int   m_place = 0;            ///< 1 = leading
    float m_lapStart = 0.0f, m_lastLap = 0.0f, m_bestLap = 0.0f;
    float m_wrongWay = 0.0f;
    // Guns and gear.
    float m_ammo = 0.0f, m_maxAmmo = 0.0f, m_heat = 0.0f;
    float m_fireTimer = 0.0f;
    bool  m_firing = false;
    int   m_rear[(int)RearE::COUNT] = {};
    int   m_rearSel = 0;
    int   m_rockets = 0, m_nitro = 0;
    float m_nitroTime = 0.0f;
    float m_oilSlip = 0.0f, m_spiked = 0.0f, m_smoked = 0.0f;
    float m_dropTimer = 0.0f, m_rocketTimer = 0.0f;
    // How it feels (sound and pictures).
    float    m_rpm = 0.0f, m_load = 0.0f, m_skid = 0.0f, m_scrape = 0.0f, m_steerVis = 0.0f, m_wheelTurn = 0.0f;
    float    m_lean = 0.0f, m_pitch = 0.0f, m_hitFlash = 0.0f;
    float    m_throttle = 0.0f, m_brakeLight = 0.0f;
    int      m_gear = 1;
    SurfaceE m_surface = SurfaceE::ASPHALT;
    bool     m_offRoad = false;
    // Who did what.
    int   m_lastHitBy = -1;
    float m_lastHitTime = -100.0f;
    int   m_wreckedBy = -1;
    int   m_wrecks = 0;            ///< cars it wrecked
    int   m_shots = 0, m_hits = 0;
    float m_damage = 0.0f;         ///< damage taken
    int   m_cash = 0;              ///< cash picked up
    AiState m_ai;
    CarInput m_in;                 ///< the last input

    /** @return the forward direction */
    Vector2 fwd() const { return headingDir( m_head ); }
    /** @return the right-hand direction */
    Vector2 right() const { return headingRight( m_head ); }
    /** @return the speed along its heading */
    float speed() const { return dot( m_vel, fwd() ); }
    /** @return whether it still races */
    bool racing() const { return !m_wrecked && !m_finished; }
};

/** @brief A bullet or a lick of flame. */
struct Bullet
{
    Vector2 m_pos = { 0, 0 }, m_vel = { 0, 0 };
    float   m_life = 1.0f, m_age = 0.0f, m_damage = 4.0f;
    int     m_owner = -1;
    int     m_kind = 0;   ///< 0 bullet, 1 flame, 2 shell
    Color   m_color = { 255, 210, 110, 255 };
};

/** @brief A rocket. */
struct Rocket
{
    Vector2 m_pos = { 0, 0 }, m_vel = { 0, 0 };
    float   m_head = 0.0f, m_life = 4.0f, m_damage = 50.0f, m_radius = 5.0f, m_turn = 2.0f, m_speed = 70.0f;
    int     m_owner = -1, m_target = -1;
};

/** @brief Something lying on the track: a mine, an oil slick, spikes, a smoke cloud. */
struct Hazard
{
    enum KindE : int { MINE, OIL, SPIKES, SMOKE } m_kind = MINE;
    Vector2 m_pos = { 0, 0 };
    Vector2 m_dir = { 1, 0 };      ///< spikes: across the road
    float   m_r = 2.0f;
    float   m_life = 60.0f, m_age = 0.0f, m_arm = 0.6f;
    float   m_damage = 40.0f, m_radius = 4.0f, m_slow = 2.0f;
    int     m_owner = -1;          ///< -1 placed by the track
    bool    m_gone = false;
};

/** @brief A pickup on the track. */
struct Pickup
{
    std::string m_kind;
    Vector2     m_pos = { 0, 0 };
    float       m_respawn = 0.0f;   ///< > 0: gone for so long
    float       m_spin = 0.0f;
};

/** @brief An explosive barrel. */
struct Barrel
{
    Vector2 m_pos = { 0, 0 };
    float   m_hp = 12.0f;
    bool    m_gone = false;
};

/** @brief What happened (for the sound, the pictures, the commentator, the rumble). */
enum class EvE : int
{
    SHOT, HIT, RICOCHET, FLAME, CRASH, WALL, SCRAPE, EXPLODE, WRECK, PICKUP, MINE_DROP, MINE_ARM, ROCKET, NITRO, OIL,
    SPIKES, SMOKE, TIRE_BURST, LAP, FINAL_LAP, FINISH, OVERTAKE, BARREL, COUNTDOWN, GO, HORN, WRONG_WAY, SAY, COUNT
};

/** @brief One thing that happened. */
struct Event
{
    EvE         m_type = EvE::HIT;
    int         m_car = -1;       ///< who it happened to or who did it
    int         m_other = -1;     ///< the other one
    Vector2     m_pos = { 0, 0 };
    float       m_power = 1.0f;   ///< how strong (damage, speed)
    int         m_kind = 0;       ///< what kind (gun kind, pickup, lap number)
    std::string m_text;           ///< SAY: the event of the rival's line; PICKUP: the kind
};

/** @brief The race. */
class World
{
public:
    /** @brief The race's phase. */
    enum class PhaseE : int
    {
        COUNTDOWN, RACE, DONE
    };

    /**
     * @brief Starts a race.
     * @param[in] track       the track (kept by reference: it must live as long as the race)
     * @param[in] cars        the field (the first ones are usually the humans)
     * @param[in] mode        race, derby or trial
     * @param[in] difficulty  0..4
     * @param[in] seed        the random seed
     */
    void init( const Track &track, const std::vector<CarSetup> &cars, ModeE mode, int difficulty, uint32_t seed );

    /**
     * @brief One step (Dt).
     * @param[in] inputs  per car: the humans' inputs (computer drivers ignore theirs)
     */
    void step( const std::vector<CarInput> &inputs );

    /** @return the cars in race order (indices) */
    std::vector<int> order() const;

    /** @return whether every human has finished or is wrecked (or the race is done) */
    bool humansDone() const;

    const Track          *m_track = nullptr;
    ModeE                 m_mode = ModeE::RACE;
    int                   m_difficulty = 2;
    PhaseE                m_phase = PhaseE::COUNTDOWN;
    float                 m_time = 0.0f;        ///< seconds since the green light
    float                 m_countdown = 3.5f;
    float                 m_doneTime = 0.0f;    ///< seconds since the phase became DONE
    int                   m_laps = 3;
    std::vector<Car>      m_cars;
    std::vector<Bullet>   m_bullets;
    std::vector<Rocket>   m_rockets;
    std::vector<Hazard>   m_hazards;
    std::vector<Pickup>   m_pickups;
    std::vector<Barrel>   m_barrels;
    std::vector<Event>    m_events;              ///< this step's
    std::vector<int>      m_finishOrder;
    int                   m_leader = 0;
    float                 m_bestLap = 0.0f;
    int                   m_bestLapCar = -1;
    uint32_t              m_seed = 1;
    Rng                   m_rng;
    bool                  m_noAi = false;       ///< computer drivers stand still (tests)
    float                 m_dmgBy[6] = {};      ///< damage dealt by: guns, explosions, crashes, walls, spikes, other
    int                   m_source = 5;         ///< what the next damage() comes from

private:
    void stepCar( int i, const CarInput &in );
    void physics( Car &c, const CarInput &in, float dt );
    void walls( Car &c, int i );
    void collide( int a, int b );
    void obstacles( Car &c, int i );
    void weapons( int i, const CarInput &in );
    void bullets();
    void rockets();
    void hazards();
    void pickups();
    void progress( int i );
    void places();
    void damage( int victim, float amount, int by, Vector2 at );
    void explode( Vector2 at, float radius, float damage, int by );
    void wreck( int i, int by );
    void event( EvE type, int car, Vector2 pos, float power = 1.0f, int kind = 0, int other = -1,
                const std::string &text = "" );
    CarInput ai( int i );
};

/** @brief The race's derived numbers for a car (its class, upgrades and gear): what the garage shows too. */
struct CarStats
{
    float m_power = 0, m_top = 0, m_grip = 0, m_brake = 0, m_steer = 0, m_offroad = 0, m_hp = 0;
    float m_ammo = 0, m_damage = 0, m_rate = 0;
};

/** @return a setup's numbers */
CarStats carStats( const CarSetup &s );

}   // namespace grave
