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
    CONTROLS, GRAVEYARD, COUNT
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
