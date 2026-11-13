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
    bool        m_player = false;   ///< the player's car: the difficulty's factors apply, even when a computer drives it
    int         m_assist = 0;       ///< the driving assist: 0 off, 1 on (grippy tyres), 2 also the brake help
    int         m_pad = 0;             ///< which player (0 or 1) for humans
    int         m_engine = 0, m_tires = 0, m_armor = 0, m_gunLv = 0;   ///< upgrade levels
    std::string m_gun;                 ///< "" the car's own
    int         m_rear[(int)RearE::COUNT] = {};   ///< mines, spikes, oil, smoke
    int         m_rockets = 0, m_nitro = 0;
    bool        m_ram = false;         ///< a ram plate
    int         m_plating = 0;         ///< armour plates
    float       m_hpShare = 1.0f;      ///< starts damaged (sabotage)
    float       m_skill = 0.6f, m_aggression = 0.5f, m_weapons = 0.5f;   ///< computer drivers
    int         m_grudge = 0;      ///< 0..3: how much this rival wants the player dead (a feud)
    float       m_novice = 0.0f;   ///< 0..1: a beginner's late, wide steering and misjudged corners (tests)
    float       m_hands = 0.0f;    ///< 0..1: a person at a keyboard (0.3 practised, 1 a beginner): late hands, all-or-nothing keys (tests)
    bool        m_flatOut = false; ///< a driver who never lifts for a bend, only steers (the brake help's test)
    float       m_pace = 1.0f;     ///< a computer driver's engine: its league's share (LeagueDef::m_pace)
    bool        m_escort = false;  ///< Graves' hearse in an escort job: the rivals go for it
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
    CarInput m_late[32];         ///< the hands: what the eyes decided, played back a reaction time later
    int      m_lateAt = 0;
    float    m_tapOwed = 0.0f;    ///< the keys: steering owed, tapped out in tenths of a second
    float    m_tapOut = 0.0f;
    int      m_tapLeft = 0;
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
    float m_slip = 0.0f;           ///< a tuned engine's nervous rear: grip lost under full throttle
    float m_dmgTaken = 1.0f, m_dmgDealt = 1.0f;   ///< the difficulty's
    // Where it is.
    Vector2 m_pos = { 0, 0 }, m_vel = { 0, 0 };
    float   m_head = 0.0f, m_yaw = 0.0f;   ///< heading (radians, 0 north, clockwise) and its rate
    // The race.
    float m_hp = 100.0f;
    bool  m_wrecked = false;
    float m_wreckTime = 0.0f;
    float   m_tow = -1.0f;          ///< Graves & Sons collect the wreck: seconds since their hook came down (< 0 not yet)
    Vector2 m_towAt = { 0, 0 };     ///< where the wreck lay when the hook took it
    bool    m_gone = false;         ///< lifted away: no longer on the track (its m_pos far off)
    int   m_hint = -1;            ///< the track sample it was near
    float m_s = 0.0f;             ///< where along the lap
    float m_dist = 0.0f;          ///< how far raced (laps x length + s, from the start line)
    int   m_lap = 0;              ///< laps completed
    bool  m_finished = false;
    float m_finishTime = 0.0f;
    int   m_place = 0;            ///< 1 = leading
    float m_lapStart = 0.0f, m_lastLap = 0.0f, m_bestLap = 0.0f;
    float m_wrongWay = 0.0f;
    float m_ghostUntil = -1.0f;   ///< put back on the track (World::resetCar): it passes through cars until then
    float m_resetAt = -100.0f;    ///< the race time it was last put back
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
    float    m_lock = 0.0f;     ///< the steering held, smoothed over a quarter of a second (0..1): what scrubs
    float    m_gripUse = 0.0f;  ///< how much of the tyres' side grip the turn asks (1: they start to slide; smoothed)
    float    m_throttle = 0.0f, m_brakeLight = 0.0f, m_reverseLight = 0.0f;
    int      m_gear = 1;
    SurfaceE m_surface = SurfaceE::ASPHALT;
    bool     m_offRoad = false;
    // Who did what.
    int   m_lastHitBy = -1;
    float m_lastHitTime = -100.0f;
    int   m_wreckedBy = -1;
    float m_moverHit = -100.0f;      ///< when a train last threw it
    int   m_partsLost = 0;         ///< bumper, door, bonnet: what fell off so far
    int   m_lastSource = -1;       ///< what did the last damage: 0 guns, 1 explosions, 2 crashes, 3 walls, 4 spikes, 5 other
    int   m_wrecks = 0;            ///< cars it wrecked
    int   m_shots = 0, m_hits = 0;
    float m_damage = 0.0f;         ///< damage taken
    int   m_cash = 0;              ///< cash picked up
    int   m_wreaths = 0;           ///< wreaths picked up (Graves' job)
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
    SPIKES, SMOKE, TIRE_BURST, LAP, FINAL_LAP, FINISH, OVERTAKE, BARREL, COUNTDOWN, GO, HORN, WRONG_WAY, SAY,
    TRAIN, BRIDGE, ROCK, WEATHER, PART, LOOSE, COUNT
};

/** @brief A small thing from the shoulders (Track::m_loose): standing, knocked flying, lying. */
struct Loose
{
    int     m_spot = 0;                    ///< its Track::m_loose
    Vector2 m_pos = { 0, 0 }, m_vel = { 0, 0 };
    float   m_rot = 0.0f, m_spin = 0.0f;   ///< its heading (radians) and how fast it turns
    float   m_fall = 0.0f;                 ///< 0 standing .. 1 lying
    Vector2 m_fallDir = { 0, 1 };          ///< where it falls to
    float   m_roll = 0.0f;                 ///< how far it rolled (radians)
    float   m_y = 0.0f, m_vy = 0.0f;       ///< thrown up
    bool    m_hit = false, m_gone = false;
};

/** @brief A rock from a rockfall. */
struct Rock
{
    Vector2 m_pos = { 0, 0 };
    float   m_r = 1.3f;
    float   m_age = 0.0f;   ///< < 0: still falling (seconds to the ground)
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

    /** @return a mover's phase in its period at a time */
    float moverPhase( const MoverSpot &m, float t ) const;
    /**
     * @brief A train's or a bridge's state.
     * @param[in]  m       the mover
     * @param[out] amount  how far along (a train's passage, a bridge's rise) 0..1
     * @param[in]  ahead   seconds from now
     * @return 0 calm, 1 the bells, 2 a train passing or a bridge up
     */
    int moverState( const MoverSpot &m, float &amount, float ahead = 0.0f ) const;
    /** @return where a passing train's front is, metres along the crossing from the road's centre */
    float trainFront( const MoverSpot &m ) const;
    /** @return whether a driver should stop before it (seconds from now) */
    bool moverBlocks( const MoverSpot &m, float ahead ) const;

    /** @return whether every human has finished or is wrecked (or the race is done) */
    bool humansDone() const;

    /**
     * @brief One step of a car's driving alone on open ground: no walls, no weapons (--handling).
     * @param[in] i   the car
     * @param[in] in  its input
     */
    void drive( int i, const CarInput &in );

    static constexpr float TowAfter = 10.0f;    ///< seconds a wreck lies before Graves & Sons' hook comes (not in a derby)
    static constexpr float TowLift = 3.5f;      ///< seconds from the hook to the wreck leaving the ground
    static constexpr float ResetWait = 5.0f;    ///< seconds between two resets of a car
    static constexpr float ResetGhost = 2.5f;   ///< seconds a reset car passes through the others

    /**
     * @brief Puts a car back on the track (the player's key when stuck or lost): on the racing line a few metres
     *        behind where it is along the lap, facing the way of travel, standing; in an arena on the freest spot.
     *        For ResetGhost seconds it passes through the other cars; the next reset waits ResetWait seconds.
     * @param[in] i  the car
     * @return false when not now (the countdown, wrecked, finished, too soon after the last one)
     */
    bool resetCar( int i );

    /**
     * @brief The speed a car may have at every sample of the lap on the racing line: what each bend's curvature
     *        allows its tyres' side grip (and the ground there, and the rain), braked down to in time for the next
     *        one (the racing line's colours on the road).
     * @param[in]  i       the car
     * @param[in]  corner  the share of the side grip to plan with
     * @param[out] plan    m/s per Track sample
     */
    void speedPlan( int i, float corner, std::vector<float> &plan ) const;

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
    std::vector<Rock>     m_rocks;               ///< from rockfalls: falling (age < 0) or lying on the road
    std::vector<Loose>    m_loose;               ///< the shoulders' small things
    std::vector<int>      m_moverCycle;          ///< each rockfall's last rock
    float                 m_wetness = 0.0f;      ///< 0..1: rain that came during the race
    float                 m_fogAdd = 0.0f;       ///< fog that came during the race (added density)
    bool                  m_weatherTurned = false;
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
    void movers();
    void looseThings();
    void knock( Loose &l, Vector2 vel, Vector2 away, int by );
    void progress( int i );
    void places();
    void damage( int victim, float amount, int by, Vector2 at );
    void explode( Vector2 at, float radius, float damage, int by );
    void wreck( int i, int by );
    void event( EvE type, int car, Vector2 pos, float power = 1.0f, int kind = 0, int other = -1,
                const std::string &text = "" );
    CarInput ai( int i );
    CarInput hands( int i, const CarInput &want );
    /**
     * @brief The speed the bends ahead allow a car: the racing line's curvature against its tyres' side grip on the
     *        ground there, with room to brake (the computer drivers' plan, and the brake help's).
     * @param[in]  i       the car
     * @param[in]  tp      where it is on the track
     * @param[in]  corner  the share of the side grip it dares to use
     * @param[out] decel   the braking it can count on where it is (m/s²)
     * @return the speed (m/s)
     */
    float cornerSpeed( int i, const TrackPos &tp, float corner, float &decel ) const;
    /**
     * @brief The brake help (assist level 2): lifts the throttle and brakes when the car is faster than the bends
     *        ahead allow. It only ever takes away; the driver's own brake works as before.
     * @param[in] i    the car
     * @param[in] own  what the driver does
     * @return what the car does
     */
    CarInput brakeHelp( int i, const CarInput &own );
};

/** @brief The race's derived numbers for a car (its class, upgrades and gear): what the garage shows too. */
struct CarStats
{
    float m_power = 0, m_top = 0, m_grip = 0, m_brake = 0, m_steer = 0, m_offroad = 0, m_hp = 0;
    float m_ammo = 0, m_damage = 0, m_rate = 0;
    float m_mass = 0, m_slip = 0;   ///< the weight (armour adds), the rear's nervousness (the engine adds)
};

/** @return a setup's numbers */
CarStats carStats( const CarSetup &s );

/** @return the most a car's tyres hold sideways on ground of grip 1 (m/s², its assist level's tyres) */
float sideGrip( const Car &c );

/** @return the turn (radians a second) a car's full lock asks of the nose at a speed */
float fullLock( const Car &c, float speed );

}   // namespace grave
