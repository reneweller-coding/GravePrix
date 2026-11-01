/**
 * @file Rumble.cpp
 * @brief The pad's motors through XInput (Windows) or evdev force feedback (Linux). No raylib here: windows.h and
 *        raylib.h do not go together.
 * @author Rene Weller
 */
#include "Rumble.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#elif defined( __linux__ )
  #include <dirent.h>
  #include <fcntl.h>
  #include <linux/input.h>
  #include <sys/ioctl.h>
  #include <unistd.h>

  #include <cstring>
  #include <string>
#endif

namespace grave
{
namespace rumble
{

namespace
{

float  g_low = 0.0f, g_high = 0.0f;   ///< what the motors do now
double g_sent = 0.0;                  ///< when the device was last told (s)

double now()
{
    return std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

/** @return whether the change is worth telling the device (it is told often otherwise) */
bool changed( float low, float high )
{
    return fabsf( low - g_low ) > 0.02f || fabsf( high - g_high ) > 0.02f || ( low == 0.0f && g_low != 0.0f ) ||
           ( high == 0.0f && g_high != 0.0f );
}

#ifdef _WIN32

struct Vibration
{
    WORD m_left;
    WORD m_right;
};
typedef DWORD( WINAPI *SetStateFn )( DWORD, Vibration * );
typedef DWORD( WINAPI *GetStateFn )( DWORD, void * );

SetStateFn g_setState = nullptr;
GetStateFn g_getState = nullptr;
bool       g_tried = false;

/** @brief XInput, loaded when first needed (whichever version Windows or Wine has). */
bool ready()
{
    if ( g_tried ) return g_setState != nullptr;
    g_tried = true;
    for ( const char *dll : { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" } )
        if ( HMODULE m = LoadLibraryA( dll ) )
        {
            g_setState = (SetStateFn)(void *)GetProcAddress( m, "XInputSetState" );
            g_getState = (GetStateFn)(void *)GetProcAddress( m, "XInputGetState" );
            if ( g_setState && g_getState ) return true;
        }
    g_setState = nullptr;
    return false;
}

void send( float low, float high )
{
    if ( !ready() ) return;
    unsigned char state[16];
    for ( DWORD user = 0; user < 4; ++user )
        if ( g_getState( user, state ) == ERROR_SUCCESS )
        {
            Vibration v = { (WORD)( std::clamp( low, 0.0f, 1.0f ) * 65535.0f ),
                            (WORD)( std::clamp( high, 0.0f, 1.0f ) * 65535.0f ) };
            g_setState( user, &v );
            return;   // the first pad there is (the one raylib reads as pad 0)
        }
}

#elif defined( __linux__ )

int  g_fd = -1;       ///< the device with force feedback
int  g_effect = -1;   ///< the effect uploaded to it
bool g_tried = false;

/** @brief The first event device that can rumble. */
bool ready()
{
    if ( g_tried ) return g_fd >= 0;
    g_tried = true;
    DIR *dir = opendir( "/dev/input" );
    if ( !dir ) return false;
    while ( dirent *e = readdir( dir ) )
    {
        if ( strncmp( e->d_name, "event", 5 ) != 0 ) continue;
        std::string   path = std::string( "/dev/input/" ) + e->d_name;
        int           fd = open( path.c_str(), O_RDWR );
        unsigned long bits[( FF_MAX + 8 * sizeof( long ) ) / ( 8 * sizeof( long ) )] = {};
        if ( fd < 0 ) continue;
        if ( ioctl( fd, EVIOCGBIT( EV_FF, sizeof( bits ) ), bits ) >= 0 &&
             ( bits[FF_RUMBLE / ( 8 * sizeof( long ) )] >> ( FF_RUMBLE % ( 8 * sizeof( long ) ) ) ) & 1 )
        {
            g_fd = fd;
            break;
        }
        close( fd );
    }
    closedir( dir );
    return g_fd >= 0;
}

void send( float low, float high )
{
    if ( !ready() ) return;
    ff_effect effect = {};
    effect.type = FF_RUMBLE;
    effect.id = (short)g_effect;
    effect.u.rumble.strong_magnitude = (unsigned short)( std::clamp( low, 0.0f, 1.0f ) * 65535.0f );
    effect.u.rumble.weak_magnitude = (unsigned short)( std::clamp( high, 0.0f, 1.0f ) * 65535.0f );
    effect.replay.length = 1000;   // renewed while it should go on
    if ( ioctl( g_fd, EVIOCSFF, &effect ) < 0 ) return;
    g_effect = effect.id;
    input_event play = {};
    play.type = EV_FF;
    play.code = (unsigned short)g_effect;
    play.value = low > 0.0f || high > 0.0f ? 1 : 0;
    if ( write( g_fd, &play, sizeof( play ) ) < 0 ) return;
}

#else

// Elsewhere (macOS) no force feedback: the race asks, nothing shakes.
void send( float, float ) {}

#endif

}   // namespace

void set( float low, float high )
{
    // Told again now and then while it runs (Linux's effects end on their own).
    bool renew = ( low > 0.0f || high > 0.0f ) && now() - g_sent > 0.5;
    if ( !changed( low, high ) && !renew ) return;
    g_sent = now();
    g_low = low;
    g_high = high;
    send( low, high );
}

void stop()
{
    set( 0.0f, 0.0f );
}

}   // namespace rumble
}   // namespace grave
