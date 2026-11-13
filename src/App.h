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
    CONTROLS, GRAVEYARD, CRYPT, SLOTS, CREDITS, SCHOOL, BENCH, REPLAY, COUNT
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
    int  m_optTab = 0;            ///< the options' tab: 0 picture, 1 sound, 2 display, 3 driving
    /** @return whether the miniature look is on now (on, or automatic while the frame rate holds) */
    bool miniatureOn() const { return m_set.m_miniature == 1 || ( m_set.m_miniature == 2 && !m_set.m_lookDropped ); }

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

    // The driving school (AppSchool.cpp): three exercises on the proving ground, timed, with medals.
    void  school();                ///< the screen: the exercises, their best times and medals
    void  startSchool( int k );    ///< starts exercise k (0 slalom, 1 hairpins, 2 esses)
    void  schoolStep();            ///< every race step: the gates, the penalties
    void  schoolHud();
    void  schoolFinish();          ///< at the end: the time, the medal, the record
    void  schoolResults();         ///< the results screen after an exercise
    bool  m_schoolOn = false;
    int   m_schoolEx = 0;          ///< the exercise running
    int   m_schoolGate = 0;        ///< the next slalom gate
    int   m_schoolMissed = 0, m_schoolCones = 0;
    float m_schoolTime = 0.0f;     ///< the result: the time with the penalties
    int   m_schoolMedal = 0;       ///< 0 none, 1 bronze, 2 silver, 3 gold
    bool  m_schoolBest = false;    ///< a new best time

    // The benchmark (AppSchool.cpp): half a minute on the heaviest track, with and without the miniature look.
    void  startBenchmark();
    void  bench();                 ///< the screen: the drive, then the verdict
    Track m_benchTrack;
    World m_benchWorld;
    View  m_benchCam;
    float m_benchTime = 0.0f;
    int   m_benchPhase = 0;        ///< 0 with the look, 1 without, 2 the verdict
    std::vector<float> m_benchFrames[2];
    std::string        m_benchVerdict;
    /** @brief The automatic miniature look: off for good when the frame rate does not hold in a race. */
    void  watchFrameRate( float dt );
    float m_slowFrames = 0.0f, m_frameWatch = 0.0f, m_frameCount = 0.0f;
    int   m_slowWindows = 0;       ///< windows of five seconds in a row in which the frame rate did not hold
    bool  m_schoolCareer = false;  ///< the driving school was started from the career (it is left aside meanwhile)

    // The crowd's voices (AppRace.cpp): its roar near the stands, cheers when cars pass, a gasp at a crash.
    void               crowdSound();
    void               crowdGasp( Vector2 at );   ///< a crash there: the stands near it gasp
    std::vector<float> m_standCheer;   ///< per stand: seconds until it may cheer again
    float              m_lastGasp = -10.0f;
    float              m_gripSing = 0.0f;   ///< the tyres' warning this frame (0..1)

    // The replay (AppRace.cpp): the last seconds of the race kept, the finish played again in slow motion from
    // cameras at the track.
    std::vector<World> m_replayBuf;    ///< a ring of the last steps
    size_t             m_replayHead = 0, m_replayCount = 0;
    std::vector<World> m_replayClip;   ///< what is played: the steps round the moment, in order
    float              m_replayAt = 0.0f;   ///< steps into the clip
    size_t             m_replayMoment = 0;  ///< the clip's step of the moment (the finish, the wreck)
    int                m_replayCar = -1;    ///< the car the cameras watch
    bool               startReplay();       ///< false: nothing to show
    void               replay( float dt );  ///< the screen
    View               m_replayCam;
    int                m_replayFreeze = -1;    ///< steps the ring still records after the moment (-1 not yet, 0 stopped)
    bool               m_replaySaid = false;   ///< the moment's sound played
    bool               m_testReplay = false;   ///< the app test asked for the replay ("replayon")
    bool               m_testSplit = false;    ///< the app test's two players without a pad ("split")

    // Two players on one screen (AppRace.cpp): side by side, each with a camera and a display of their own.
    int      m_player2 = -1;             ///< the second human's car (-1 alone)
    int      m_qPlayers = 1;             ///< the quick race's players
    Renderer m_view2;
    View     m_cam2;
    Vector2  m_camCar2 = { 0, 0 }, m_camAnchor2 = { 0, 0 }, m_camLead2 = { 0, 0 };
    void     splitViews( bool on );
    void     splitHud( int player, Rectangle area );
    void     miniMap( Rectangle box );   ///< the track and the cars, the feed under it (two players)
    int      m_splitWas = -1;            ///< the second player of a race whose replay runs full screen

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
    /** @brief One player's camera: it leads the car the way it goes (raceCamera for each player). */
    void    leadCamera( float dt, int player, View &cam, Vector2 &car, Vector2 &anchor, Vector2 &lead );
    /** @brief The podium's picture taken for the tabloid: cut as shown, turned into newsprint. */
    void      takePhoto( const Texture2D &t, Rectangle src );
    Texture2D m_racePhoto = {};       ///< the podium of the last race, for the tabloid (newsprint)
    int       m_racePhotoDay = -1;    ///< the career's day it was taken
    Rectangle m_photoSrc = {};        ///< the podium's part the results show (the photo's frame)
    float     m_resultsTime = 0.0f;   ///< seconds on the results screen (the photo waits for the confetti)

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
    /**
     * @brief The racing line on the road ahead of a player's car (the option, or the driving school), coloured by
     *        what its speed now would need there: green fine, yellow lift, red brake.
     * @param[in,out] view    the renderer that draws it
     * @param[in]     player  the car
     */
    void               racingLine( Renderer &view, int player );
    std::vector<float> m_plan[2];          ///< per player: World::speedPlan along the lap
    float              m_planWet[2] = { -1.0f, -1.0f };
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
