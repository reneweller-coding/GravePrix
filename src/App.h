/**
 * @file App.h
 * @brief The game: the window, the menus (App.cpp), the race with its HUD, sound and results (AppRace.cpp) and the
 *        career's screens (AppCareer.cpp). Behind the menus a race of computer drivers runs on a random track.
 * @author Rene Weller
 */
#pragma once

#include "Audio.h"
#include "Career.h"
#include "Input.h"
#include "Render.h"
#include "Save.h"
#include "Sim.h"
#include "Tools.h"
#include "Track.h"

#include <map>
#include <string>
#include <vector>

namespace grave
{

/** @brief The screens. */
enum class ScreenE : int
{
    TITLE, NEWGAME, HUB, OFFERS, GARAGE, MARKET, SHARK, SPONSOR, NEWS, TABLE, STORY, RACE, RESULTS, QUICK, OPTIONS,
    CONTROLS, GRAVEYARD, CRYPT, SLOTS, CREDITS, COUNT
};

/** @brief The game. */
class App
{
public:
    /** @brief Opens the window and the sound, reads the settings. */
    bool init( const Options &o );
    /** @brief Runs until the player quits. */
    void run();
    /** @brief Closes everything. */
    void shutdown();
    /** @return the app test's result (0 passed) */
    int testResult() const { return m_testFails; }

private:
    // App.cpp: the flow and the menus.
    void frame( float dt );
    void go( ScreenE s );
    void title();
    void newGame();
    void story();
    void quick();
    void options();
    void controls();
    void showStory( const std::string &key, ScreenE after );
    void slots();     ///< the three careers: which to go on with, or where a new one goes
    void readSlots();
    void credits();   ///< the credits roll (from the title, and after Graves is beaten)
    /** @return a career slot's file (0 is the old single career.txt) */
    static std::string careerFile( int slot );
    struct SlotInfo
    {
        bool                     m_used = false;
        std::string              m_name;
        std::vector<std::string> m_lines;
    };
    SlotInfo m_slotInfo[3];
    int      m_slot = 0;            ///< the career's slot
    bool     m_slotsNew = false;    ///< the slot screen picks a place for a new career
    int      m_slotConfirm = -1;    ///< a taken slot chosen once (choose it again to overwrite)
    float    m_creditsTime = 0.0f;
    ScreenE  m_creditsAfter = ScreenE::TITLE;
    std::vector<std::pair<int, std::string>> m_creditLines;   ///< 0 the title, 1 a heading, 2 a line, 3 small, 4 a gap
    void attract( float dt );
    void backdrop();
    int  quality() const;
    void applySettings();
    void saveSettings();

    // AppRace.cpp: the race.
    void startRace( const std::string &track, bool reverse, const std::vector<CarSetup> &field, ModeE mode, int laps,
                    int player );
    void race( float dt );
    void raceStep();
    void raceEvents();
    void raceSound( float dt );
    void raceHud();
    void raceHudPanels();
    void racePause();
    void endRace();
    void results();
    void bubble( const std::string &who, const std::string &text, Color c );
    void announce( const std::string &event, const std::string &name );
    bool speak( const std::string &event );   ///< Ricky Rattle says a line of his own (false: no recording)
    bool quip( const std::string &event );    ///< a short cry of his at a rival's moment
    void goHot();                             ///< the venue's hotter take (last lap, close fights)
    void jobWatch();                          ///< Graves' job during the race: done, failed, the coffin falling
    void jobHud();
    int  m_jobState = 0;                      ///< 0 open, 1 done, -1 failed
    int  m_jobCar = -1;                       ///< the debtor or the hearse
    bool  m_hot = false;
    float m_closeTime = 0.0f, m_lastQuip = -10.0f;
    bool m_saidTrain = false, m_saidBridge = false;
    std::map<std::string, Texture2D> m_photos;   ///< the tabloid's pictures, loaded when first shown
    const Texture2D *photo( const std::string &event, uint32_t pick );

    // AppCareer.cpp: between the races.
    void hub();
    void offers();
    void garage();
    void market();
    void shark();
    void sponsor();
    void newsScreen();
    void tableScreen();
    void graveyard();
    void saveCareer();
    void carCard( Rectangle r );

    // AppAwards.cpp: the epitaphs, the crypt, the test drive.
    void award( const std::string &key );
    int  countUp( const std::string &key, int n = 1 );
    void awardsEvents();   ///< after every race step: what its events earn
    void awardsRace();     ///< at the end of a race
    void awardsCareer();   ///< after a career race was booked
    void toasts();
    void crypt();
    void startTutorial();
    void tutorial();
    void tutorialHud();
    struct Toast
    {
        std::string m_text;
        float       m_time;
    };
    std::vector<Toast> m_toasts;
    int     m_worstPlace = 0;          ///< the race's worst place once the first lap was done
    bool    m_slipped = false;         ///< the player slid on oil this race
    float   m_moverSeen = -100.0f;     ///< the last train that hit the player
    bool    m_tutorialOn = false;
    int     m_tutStep = 0, m_tutSaid = -1, m_tutCount = 0;
    float   m_tutTime = 0.0f;
    ScreenE m_tutFrom = ScreenE::TITLE;
    bool    m_hubTutorial = false;   ///< the paddock offered the test drive last frame

    // The dead rivals' ghosts (AppRace.cpp): along the racing line on the cemetery's tracks and at the final.
    struct Spirit
    {
        std::string m_rival;
        float       m_s = 0.0f, m_wait = 0.0f, m_said = -100.0f, m_lane = 0.0f;
    };
    std::vector<Spirit> m_spiritList;
    std::vector<float>  m_spiritV;   ///< the ghosts' speed at each sample of the track
    bool                m_spiritSeen = false;
    void                spirits();

    // The race camera (AppRace.cpp): it leads the car the way it goes, never turning.
    Vector2 m_camCar = { 0, 0 };      ///< the car's place in the last frame
    Vector2 m_camAnchor = { 0, 0 };   ///< the point the camera keeps with the car (it glides after a jump)
    Vector2 m_camLead = { 0, 0 };     ///< metres it looks ahead of the car (damped)
    void    raceCamera( float dt );

    // Pace notes (AppRace.cpp): the bends of the racing line ahead, as arrows in the display.
    struct Bend
    {
        float m_s0 = 0.0f, m_s1 = 0.0f;   ///< where it begins and ends along the lap
        float m_turn = 0.0f;              ///< how far it turns (radians, > 0 to the right)
        float m_radius = 0.0f;            ///< the racing line's tightest radius in it (metres)
        float m_head = 0.0f;              ///< the way of travel where it begins (radians, 0 up the screen)
        int   m_grade = 1;                ///< 1 fast .. 4 a hairpin
    };
    std::vector<Bend> m_bends;
    void              findBends();
    /**
     * @brief Draws the pace notes in the middle of the display.
     * @param[in] y      their top (low: their bottom)
     * @param[in] low    at the bottom of the screen, growing upwards
     * @param[in] alpha  0..1
     * @return the height they took (0: no bend ahead)
     */
    float paceNotes( float y, bool low, float alpha );
    bool  m_hudLow = false;     ///< the messages in the middle sit at the bottom (the car drives high on the screen)
    float m_hudFade = 1.0f;     ///< their fade while they change sides
    float m_stuckTime = 0.0f;   ///< seconds the player stood still trying to drive, or drove the wrong way (the hint)

    // Drawing helpers.
    void header( const std::string &title, const std::string &sub = "" );
    void hints( const std::string &text );
    int  menu( const std::vector<std::string> &items, int &sel, Rectangle area, float size = 40.0f,
               const std::vector<bool> *enabled = nullptr );
    void moneyBox();
    void sound( SfxE s );

    // The app test: a script of menu words; "expect <screen>" checks, "race" lets a computer driver finish the race.
    bool                     m_test = false;
    std::vector<std::string> m_testWords;
    size_t                   m_testAt = 0;
    int                      m_testWait = 0;
    int                      m_testFrames = 0;
    int                      m_testFails = 0;
    bool                     m_testRace = false;
    std::string              m_testShot;   ///< "shot NAME": this frame goes to work/screens/NAME.png
    void                     testStep();

    Options  m_opt;
    Settings m_set;
    Records  m_rec;
    Input    m_in;
    Audio    m_audio;
    Renderer m_view, m_show;
    Renderer m_podium;   ///< the results screen's podium
    bool     m_deck = false;
    bool     m_quit = false;
    float    m_time = 0.0f;
    ScreenE  m_screen = ScreenE::TITLE;
    int      m_sel[(int)ScreenE::COUNT] = {};
    int      m_tab = 0;

    // The career.
    Career      m_career;
    bool        m_inCareer = false;
    std::string m_greeting;      ///< the line of the person behind the counter
    float       m_greetTime = 0.0f;

    // The race.
    Track             m_track;
    World             m_world;
    ModeE             m_mode = ModeE::RACE;
    int               m_player = 0;
    Offer             m_offer;
    bool              m_racing = false;
    bool              m_paused = false;
    int               m_pauseSel = 0;
    float             m_pausedAt = 0.0f;
    float             m_acc = 0.0f;
    View              m_cam;
    float             m_shake = 0.0f;
    float             m_endTimer = 0.0f;
    RaceReport        m_report;
    std::string       m_trackName;
    bool              m_trackReverse = false;
    std::vector<CarSetup> m_field;
    struct Bubble
    {
        std::string m_who, m_text;
        Color       m_color;
        float       m_time;
    };
    std::vector<Bubble> m_bubbles;
    std::string         m_announce;
    float               m_announceTime = 0.0f;
    std::vector<std::pair<std::string, float>> m_feed;
    std::string m_center;           ///< a big word in the middle (3, 2, 1, GO, LAST LAP)
    float       m_centerTime = 0.0f;
    float       m_lastBest = 0.0f;
    float       m_rumbleLow = 0.0f, m_rumbleHigh = 0.0f;
    std::vector<Vector3> m_ghostRec, m_ghost;   ///< x, z, heading per step: this lap, the best lap
    size_t               m_ghostAt = 0;
    float                m_ghostTime = 0.0f;
    void                 loadGhost();
    void                 saveGhost();

    // The race behind the menus.
    Track m_atTrack;
    World m_atWorld;
    bool  m_atReady = false;
    float m_atTime = 0.0f;
    View  m_atCam;

    // Quick race and new game.
    int  m_qVenue = 0, m_qTrack = 0, m_qCar = 0, m_qLaps = 3, m_qCars = 6, m_qDiff = 2, m_qColor = 0;
    bool m_qReverse = false;
    int  m_qMode = 0;   ///< 0 race, 1 derby, 2 trial, 3 endless tournament, 4 daily challenge
    uint32_t m_qSeed = 1;           ///< the random track's number
    int      m_tour = 0;            ///< the endless tournament's round (0: none running)
    uint32_t m_tourSeed = 1;
    void     startTourRound();
    std::vector<CarSetup> quickField( const VenueDef *vd, const std::string &car, int opponents, int level );
    static uint32_t dailySeed();
    std::string m_newName;
    int         m_newDiff = 2, m_newColor = 0;
    bool        m_naming = false;

    // Stories.
    std::string m_storyKey;
    int         m_storyPage = 0;
    ScreenE     m_afterStory = ScreenE::HUB;
    int         m_bindAct = -1;     ///< the action being rebound (-1 none)
    bool        m_bindPad = false;
};

}   // namespace grave
