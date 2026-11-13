/**
 * @file Audio.cpp
 * @brief The sound of GRAVE PRIX, all of it made by the program: effects synthesised at the start (filtered noise,
 *        tones, FM brass, an organ, modal impacts, a small reverb), loops made seamless (tyres, scraping, rain, wind,
 *        and for every venue a bed with events that fall at random), engine voices synthesised live on the audio
 *        thread (firing pulses through exhaust resonances, rasp, intake, turbo, clatter, whine) and the music,
 *        streamed and crossfaded.
 * @author Rene Weller
 */
#include "Audio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace grave
{

namespace
{

constexpr int    Rate = 44100;                 ///< samples a second, everywhere
constexpr float  Pi = 3.14159265358979f;       ///< pi
constexpr float  TwoPi = 2.0f * Pi;            ///< two pi
constexpr double TwoPiD = 6.283185307179586;   ///< two pi, double
constexpr int    EngineSlots = 8;              ///< engine voices at once
constexpr int    SoundVoices = 6;              ///< overlapping plays of one effect
constexpr int    Chunk = 256;                  ///< the mixer works in pieces of this many frames
constexpr int    SfxCount = (int)SfxE::COUNT;
constexpr int    LoopCount = (int)LoopE::COUNT;
constexpr int    EngineCount = (int)EngineE::COUNT;
constexpr int    FirstAmbience = (int)LoopE::AMB_JUNKYARD;

const char *const SfxNames[] = {
    "mg",           "twinmg",      "cannon",      "flame_start",  "ricochet",   "bullet_hit",      "rocket_launch",
    "mine_drop",    "mine_arm",    "spikes_drop", "oil_drop",     "smoke_drop", "nitro",           "tire_burst",
    "crash_light",  "crash_heavy", "crash_wall",  "metal_crunch", "glass",      "explosion_small", "explosion_big",
    "wreck",        "barrel",      "count_beep",  "count_go",     "lap",        "final_lap",       "finish",
    "wrong_way",    "bell",        "funeral_march", "cheer", "gasp", "applause", "birds_up",   "dog_bark",    "thunder_near", "thunder_far", "pick_cash",   "pick_wrench",  "pick_ammo",  "pick_nitro",      "pick_weapon",
    "pick_mystery", "horn_plain",  "horn_truck",  "horn_dixie",   "horn_ice",   "horn_funeral",    "horn_siren",
    "horn_klaxon",  "ui_move",     "ui_ok",       "ui_back",      "ui_buy",     "ui_deny",         "ui_cash",
    "ui_paper",     "ui_stamp"
};
static_assert( sizeof( SfxNames ) / sizeof( SfxNames[0] ) == SfxCount, "one name per effect" );

const char *const LoopNames[] = { "skid",      "skid_loose",     "scrape",       "gravel",       "flamer",
                                  "fire",      "rain",           "wind",         "rotor",        "airship",      "tyre_sing", "crowd", "amb_junkyard", "amb_cemetery",
                                  "amb_docks", "amb_desert",     "amb_refinery", "amb_swamp",    "amb_downtown",
                                  "amb_pass",  "amb_fairground", "amb_proving",  "amb_estate" };
static_assert( sizeof( LoopNames ) / sizeof( LoopNames[0] ) == LoopCount, "one name per loop" );

const char *const EngineNames[] = {
    "tin", "i4", "boxer", "v6", "v8", "v12", "diesel", "rotary", "turbine", "electric"
};
static_assert( sizeof( EngineNames ) / sizeof( EngineNames[0] ) == EngineCount, "one name per engine" );

const char *const HornNames[] = { "plain", "truck", "dixie", "ice", "funeral", "siren", "klaxon" };
const char *const VenueNames[] = { "junkyard", "cemetery", "docks",      "desert",  "refinery", "swamp",
                                   "downtown", "pass",     "fairground", "proving", "estate" };
static_assert( sizeof( VenueNames ) / sizeof( VenueNames[0] ) == LoopCount - FirstAmbience, "one per ambience" );

// ---------------------------------------------------------------------------------------------------------------
// small things
// ---------------------------------------------------------------------------------------------------------------

/** @return v held between a and b */
inline float clampf( float v, float a, float b )
{
    return v < a ? a : v > b ? b : v;
}

/** @return 0 below 0, 1 above 1, a smooth S between */
inline float smooth01( float t )
{
    t = clampf( t, 0.0f, 1.0f );
    return t * t * ( 3.0f - 2.0f * t );
}

/** @return the frequency of a MIDI note */
inline float midiHz( float m )
{
    return 440.0f * std::pow( 2.0f, ( m - 69.0f ) / 12.0f );
}

/** @return the factor per sample of a one-pole smoother that settles in 'seconds' (time constant) */
inline float coef( float seconds )
{
    return 1.0f - std::exp( -1.0f / ( seconds * Rate ) );
}

/** @brief A small, fast random source (xorshift32); every sound has its own seed, so it is made the same each time. */
struct Rand
{
    uint32_t m_s = 0x9e3779b9u;   ///< the state (never 0)

    /** @brief Starts over. @param[in] s  any number */
    void seed( uint32_t s )
    {
        m_s = s * 2654435761u + 0x6d2b79f5u;
        if ( !m_s ) m_s = 1;
    }

    /** @return the next 32 bits */
    uint32_t next()
    {
        uint32_t x = m_s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return m_s = x;
    }

    /** @return a number in [0, 1) */
    float unit() { return ( next() >> 8 ) * ( 1.0f / 16777216.0f ); }

    /** @return a number in [-1, 1) */
    float bi() { return unit() * 2.0f - 1.0f; }

    /** @return a number in [a, b) */
    float range( float a, float b ) { return a + ( b - a ) * unit(); }

    /** @return a whole number in [0, n) */
    int below( int n ) { return n > 0 ? (int)( next() % (uint32_t)n ) : 0; }

    /** @return true with the chance p */
    bool chance( float p ) { return unit() < p; }
};

/**
 * @brief raylib's pan law (the one its sounds get), scaled so that the middle is 1 in both ears: raylib applies the
 *        law once more to the mixer's stream (0.6875 in the middle), so a voice panned here sounds as loud as an
 *        effect played with the same pan.
 * @param[in]  pan  -1 left .. 1 right
 * @param[out] l    the left gain
 * @param[out] r    the right gain
 */
inline void panGains( float pan, float &l, float &r )
{
    float p = clampf( 0.5f - 0.5f * pan, 0.0f, 1.0f ), q = 1.0f - p;
    l = 0.5f * p * ( 3.0f - p * p ) * ( 1.0f / 0.6875f );
    r = 0.5f * q * ( 3.0f - q * q ) * ( 1.0f / 0.6875f );
}

/** @return a 4-point Hermite interpolation between x0 and x1 at t (0..1) */
inline float hermite( float xm, float x0, float x1, float x2, float t )
{
    float c = ( x1 - xm ) * 0.5f, v = x0 - x1, w = c + v, a = w + v + ( x2 - x0 ) * 0.5f, b = w + a;
    return ( ( a * t - b ) * t + c ) * t + x0;
}

/** @return x unchanged below 0.75, then bent smoothly towards 1 (the mixer never clips) */
inline float softCeil( float x )
{
    float a = std::fabs( x );
    if ( a <= 0.75f ) return x;
    float y = 0.75f + 0.25f * std::tanh( ( a - 0.75f ) * 4.0f );
    return x < 0 ? -y : y;
}

/** @brief A state-variable filter (topology-preserving transform): stable while its cutoff moves. */
struct Svf
{
    float m_ic1 = 0, m_ic2 = 0, m_a1 = 0, m_a2 = 0, m_a3 = 0, m_k = 1;
    float lp = 0;    ///< the lowpass output
    float bp = 0;    ///< the bandpass output (peak gain q)
    float bpn = 0;   ///< the bandpass output with a peak gain of 1
    float hp = 0;    ///< the highpass output

    /** @brief Sets the cutoff (Hz) and the resonance q (0.5 none .. 30 ringing). */
    void set( float hz, float q )
    {
        hz = clampf( hz, 8.0f, Rate * 0.45f );
        float g = std::tan( Pi * hz / Rate );
        m_k = 1.0f / std::max( 0.05f, q );
        m_a1 = 1.0f / ( 1.0f + g * ( g + m_k ) );
        m_a2 = g * m_a1;
        m_a3 = g * m_a2;
    }

    /** @brief Filters one sample (read the outputs afterwards). */
    void run( float x )
    {
        float v3 = x - m_ic2;
        float v1 = m_a1 * m_ic1 + m_a2 * v3;
        float v2 = m_ic2 + m_a2 * m_ic1 + m_a3 * v3;
        m_ic1 = 2.0f * v1 - m_ic1;
        m_ic2 = 2.0f * v2 - m_ic2;
        lp = v2;
        bp = v1;
        bpn = m_k * v1;
        hp = x - m_k * v1 - v2;
    }
};

/** @brief The filter types. */
enum class Flt
{
    LP,
    BP,
    HP
};

/** @return the output of a filter of that type */
inline float pick( const Svf &f, Flt t )
{
    return t == Flt::LP ? f.lp : t == Flt::BP ? f.bpn : f.hp;
}

/** @return the gain that gives white noise through that filter about the loudness of unfiltered noise */
inline float noiseNorm( Flt t, float hz, float q )
{
    float bw = t == Flt::LP ? hz : t == Flt::BP ? 1.57f * hz / std::max( 0.1f, q ) : 0.5f * Rate - hz;
    return std::sqrt( 0.5f * Rate / clampf( bw, 20.0f, 0.5f * Rate ) );
}

/** @brief The waveforms. */
enum class Wv
{
    SINE,
    TRI,
    SAW,
    SQUARE
};

/** @return the band-limiting correction of a step in a naive oscillator (polyBLEP) */
inline float polyBlep( float t, float dt )
{
    if ( t < dt )
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if ( t > 1.0f - dt )
    {
        t = ( t - 1.0f ) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/** @return one sample of a waveform at phase p (0..1) that advances dt a sample; pw the pulse width */
inline float wave( Wv w, float p, float dt, float pw )
{
    switch ( w )
    {
    case Wv::SINE: return std::sin( TwoPi * p );
    case Wv::TRI: return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
    case Wv::SAW: return 2.0f * p - 1.0f - polyBlep( p, dt );
    case Wv::SQUARE:
    {
        float q = p + 1.0f - pw;
        q -= std::floor( q );
        return ( p < pw ? 1.0f : -1.0f ) + polyBlep( p, dt ) - polyBlep( q, dt );
    }
    }
    return 0.0f;
}

/** @brief A small reverb after Freeverb (eight combs and four allpasses a side): one input, two outputs. */
class Reverb
{
public:
    /**
     * @param[in] size   1 a mid-sized hall; the delay lines scale with it
     * @param[in] decay  0..1, how long it rings
     * @param[in] damp   0..0.95, how quickly the highs die
     */
    Reverb( float size, float decay, float damp )
    {
        static const int combs[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int alls[4] = { 556, 441, 341, 225 };
        for ( int c = 0; c < 2; ++c )
        {
            for ( int k = 0; k < 8; ++k )
                m_comb[c][k].assign( std::max( 16, (int)( ( combs[k] + 23 * c ) * size ) ), 0.0f );
            for ( int k = 0; k < 4; ++k )
                m_all[c][k].assign( std::max( 8, (int)( ( alls[k] + 23 * c ) * size ) ), 0.0f );
        }
        m_fb = 0.7f + 0.28f * clampf( decay, 0.0f, 1.0f );
        m_damp = clampf( damp, 0.0f, 0.95f );
    }

    /** @brief One sample in, two out. */
    void run( float x, float &l, float &r )
    {
        float in = x * 0.015f, o[2];
        for ( int c = 0; c < 2; ++c )
        {
            float acc = 0;
            for ( int k = 0; k < 8; ++k )
            {
                std::vector<float> &b = m_comb[c][k];
                size_t             &i = m_ci[c][k];
                float               y = b[i];
                m_store[c][k] = y * ( 1.0f - m_damp ) + m_store[c][k] * m_damp;
                b[i] = in + m_store[c][k] * m_fb;
                if ( ++i >= b.size() ) i = 0;
                acc += y;
            }
            for ( int k = 0; k < 4; ++k )
            {
                std::vector<float> &b = m_all[c][k];
                size_t             &i = m_ai[c][k];
                float               bo = b[i], y = bo - acc;
                b[i] = acc + bo * 0.5f;
                if ( ++i >= b.size() ) i = 0;
                acc = y;
            }
            o[c] = acc * 3.0f;
        }
        l = o[0];
        r = o[1];
    }

private:
    std::vector<float> m_comb[2][8], m_all[2][4];
    size_t             m_ci[2][8] = {}, m_ai[2][4] = {};
    float              m_store[2][8] = {};
    float              m_fb = 0.84f, m_damp = 0.3f;
};

/** @brief A biquad (direct form I), double precision, for measuring. */
struct Biquad
{
    double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    /** @return the next output */
    double run( double x )
    {
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

/** @return the loudest 400 ms of a mono sound: K-weighted momentary loudness (BS.1770) in LUFS */
float maxMomentary( const std::vector<float> &x )
{
    // the K-weighting as two biquads: a +4 dB shelf above ~1.5 kHz, a highpass at 38 Hz
    double a = std::pow( 10.0, 4.0 / 40.0 ), w = TwoPiD * 1500.0 / Rate, al = std::sin( w ) / ( 2 * 0.7071 );
    double c = std::cos( w ), sa = 2 * std::sqrt( a ) * al, n0 = ( a + 1 ) - ( a - 1 ) * c + sa;
    Biquad shelf{ a * ( ( a + 1 ) + ( a - 1 ) * c + sa ) / n0, -2 * a * ( ( a - 1 ) + ( a + 1 ) * c ) / n0,
                  a * ( ( a + 1 ) + ( a - 1 ) * c - sa ) / n0, 2 * ( ( a - 1 ) - ( a + 1 ) * c ) / n0,
                  ( ( a + 1 ) - ( a - 1 ) * c - sa ) / n0 };
    w = TwoPiD * 38.0 / Rate;
    al = std::sin( w ) / ( 2 * 0.5 );
    c = std::cos( w );
    double              h0 = 1 + al;
    Biquad              high{ ( 1 + c ) / 2 / h0, -( 1 + c ) / h0, ( 1 + c ) / 2 / h0, -2 * c / h0, ( 1 - al ) / h0 };
    size_t              block = (size_t)( 0.4 * Rate ), hop = (size_t)( 0.1 * Rate ), n = std::max( x.size(), block );
    std::vector<double> cum( n + 1, 0.0 );
    for ( size_t i = 0; i < n; ++i )
    {
        double y = high.run( shelf.run( i < x.size() ? x[i] : 0.0 ) );
        cum[i + 1] = cum[i] + y * y;
    }
    double best = 1e-12;
    for ( size_t s0 = 0; s0 + block <= n; s0 += hop ) best = std::max( best, ( cum[s0 + block] - cum[s0] ) / block );
    return (float)( -0.691 + 10.0 * std::log10( best ) );
}

// ---------------------------------------------------------------------------------------------------------------
// the effect workshop
// ---------------------------------------------------------------------------------------------------------------

/** @brief An envelope: a smooth attack, a hold, then an exponential decay. */
struct Env
{
    float a = 0.002f;   ///< attack (s)
    float h = 0.0f;     ///< hold (s)
    float d = 0.1f;     ///< decay time constant (s)

    /** @return the level t seconds after the start */
    float operator()( float t ) const
    {
        if ( t < a ) return smooth01( t / a );
        t -= a;
        if ( t < h ) return 1.0f;
        return std::exp( -( t - h ) / d );
    }

    /** @return when it has died away (-80 dB) */
    float end() const { return a + h + d * 9.2f; }
};

/** @brief A mode of a ringing body: its frequency, decay time constant and level. */
struct Mode
{
    float f, d, a;
};

/** @brief A sound effect being made (mono, 44.1 kHz). */
struct Fx
{
    std::vector<float> m_s;     ///< the samples
    Rand               m_rng;   ///< its noise

    /** @brief An empty sound of that length. */
    Fx( float seconds, uint32_t seed ) : m_s( (size_t)( seconds * Rate ) + 1, 0.0f ) { m_rng.seed( seed ); }

    /** @return the length in samples */
    size_t n() const { return m_s.size(); }

    /** @return the sample at t seconds (clamped to the end) */
    size_t at( float t ) const { return std::min( n(), (size_t)std::max( 0.0f, t * Rate ) ); }

    /** @return a short fade before 'end' so that nothing clicks */
    static float fade( size_t i, size_t end ) { return std::min( 1.0f, ( end - i ) / ( 0.004f * Rate ) ); }

    /**
     * @brief A pitched voice whose frequency follows freq(t).
     * @param[in] rough  0 clean .. 1 a rasp (its level shaken by noise, as a buzzing reed or a stick-slip)
     */
    void voice( float start, float len, const std::function<float( float )> &freq, float amp, Env e, Wv w,
                float rough = 0.0f, float pw = 0.5f )
    {
        size_t i0 = at( start ), i1 = at( start + len );
        double ph = 0;
        float  rn = 0, rk = coef( 1.0f / ( TwoPi * 70.0f ) );
        for ( size_t i = i0; i < i1; ++i )
        {
            float t = ( i - i0 ) / (float)Rate, f = freq( t ), dt = f / Rate;
            ph += dt;
            ph -= std::floor( ph );
            float v = wave( w, (float)ph, dt, pw );
            if ( rough > 0 )
            {
                rn += ( m_rng.bi() - rn ) * rk;
                v *= std::max( 0.0f, 1.0f + rough * 4.0f * rn );
            }
            m_s[i] += v * e( t ) * amp * fade( i, i1 );
        }
    }

    /** @brief A tone gliding (evenly in pitch) from f0 to f1 within 'glide' seconds, then held. */
    void tone( float start, float len, float f0, float f1, float glide, float amp, Env e, Wv w = Wv::SINE,
               float pw = 0.5f )
    {
        voice(
            start, len, [=]( float t )
            { return glide > 0 ? f0 * std::pow( f1 / f0, std::min( 1.0f, t / glide ) ) : f1; }, amp, e, w, 0.0f, pw );
    }

    /** @brief A thump: a sine whose pitch falls from f0 towards f1 (time constant tau). */
    void kick( float start, float f0, float f1, float tau, float amp, Env e )
    {
        size_t i0 = at( start ), i1 = at( start + e.end() );
        double ph = 0;
        for ( size_t i = i0; i < i1; ++i )
        {
            float t = ( i - i0 ) / (float)Rate, f = f1 + ( f0 - f1 ) * std::exp( -t / tau );
            ph += f / Rate;
            m_s[i] += std::sin( (float)( TwoPiD * ph ) ) * e( t ) * amp * fade( i, i1 );
        }
    }

    /** @brief Noise through a filter whose cutoff glides from c0 to c1 within 'sweep' seconds. */
    void noise( float start, float len, float amp, Env e, Flt type, float c0, float c1, float sweep, float q = 0.707f )
    {
        size_t i0 = at( start ), i1 = at( start + std::min( len, e.end() ) );
        Svf    f;
        float  norm = 1;
        for ( size_t i = i0; i < i1; ++i )
        {
            float t = ( i - i0 ) / (float)Rate;
            if ( ( ( i - i0 ) & 15 ) == 0 )
            {
                float c = c0 * std::pow( c1 / c0, std::min( 1.0f, t / std::max( 1e-4f, sweep ) ) );
                f.set( c, q );
                norm = noiseNorm( type, c, q );
            }
            f.run( m_rng.bi() );
            m_s[i] += pick( f, type ) * norm * e( t ) * amp * fade( i, i1 );
        }
    }

    /** @brief A struck body: decaying sines (they start at zero phase: the strike itself is the click). */
    void modal( float start, float amp, const std::vector<Mode> &modes )
    {
        size_t i0 = at( start );
        for ( const Mode &m : modes )
        {
            if ( m.f >= Rate * 0.45f ) continue;
            size_t i1 = std::min( n(), i0 + (size_t)( m.d * 9.2f * Rate ) );
            double w = TwoPiD * m.f / Rate;
            float  k = std::exp( -1.0f / ( m.d * Rate ) ), g = amp * m.a;
            for ( size_t i = i0; i < i1; ++i )
            {
                m_s[i] += (float)std::sin( w * ( i - i0 ) ) * g * fade( i, i1 );
                g *= k;
            }
        }
    }

    /** @brief A struck body (list form). */
    void modal( float start, float amp, std::initializer_list<Mode> modes )
    {
        modal( start, amp, std::vector<Mode>( modes ) );
    }

    /**
     * @brief Random short resonant ticks (crackle, debris, gravel): rate0 a second at the start, rate1 at the end;
     *        each a decaying sine between lo and hi Hz; 'fall' lets their level sink over the span.
     */
    void grains( float start, float len, float rate0, float rate1, float amp, float lo, float hi, float d0, float d1,
                 float fall = 0.0f )
    {
        float t = 0;
        while ( true )
        {
            float rate = std::max( 0.5f, rate0 + ( rate1 - rate0 ) * t / len );
            t += -std::log( 1.0f - m_rng.unit() * 0.9999f ) / rate;
            if ( t >= len ) break;
            float  f = lo * std::pow( hi / lo, m_rng.unit() ), d = m_rng.range( d0, d1 );
            float  a = amp * m_rng.range( 0.2f, 1.0f ) * std::exp( -fall * t / len * 3.0f );
            float  ph = m_rng.unit() * TwoPi;
            size_t i0 = at( start + t ), i1 = std::min( n(), i0 + (size_t)( d * 7.0f * Rate ) );
            float  k = std::exp( -1.0f / ( d * Rate ) ), w = TwoPi * f / Rate;
            for ( size_t i = i0; i < i1; ++i )
            {
                m_s[i] += std::sin( ph ) * a;
                ph += w;
                a *= k;
            }
        }
    }

    /** @brief Adds another sound at 'start'. */
    void mix( const std::vector<float> &o, float start, float gain )
    {
        size_t i0 = at( start );
        for ( size_t k = 0; k < o.size() && i0 + k < n(); ++k ) m_s[i0 + k] += o[k] * gain;
    }

    /** @brief A note on a brass instrument: FM (ratio 1) whose brightness swells with the attack. */
    void brass( float start, float len, float f, float amp, float bright )
    {
        size_t i0 = at( start ), i1 = at( start + len + 0.12f );
        double ph = 0;
        for ( size_t i = i0; i < i1; ++i )
        {
            float t = ( i - i0 ) / (float)Rate;
            float env = smooth01( t / 0.035f ) * ( t > len ? std::exp( -( t - len ) / 0.035f ) : 1.0f );
            float idx = bright * ( 0.9f + 1.5f * env + 0.9f * std::exp( -t / 0.05f ) );
            float vib = 1.0f + 0.005f * std::sin( TwoPi * 5.3f * t ) * smooth01( ( t - 0.2f ) / 0.3f );
            ph += f * vib / Rate;
            float p = (float)( TwoPiD * ( ph - std::floor( ph ) ) );
            m_s[i] += std::sin( p + idx * std::sin( p ) ) * env * amp * fade( i, i1 );
        }
    }

    /** @brief A note on a pipe organ (drawbars: 16', 8', 4', 2 2/3', 2', 1 1/3', 1'). */
    void organ( float start, float len, float midi, float amp )
    {
        static const float bars[][2] = { { 0.5f, 0.45f }, { 1, 1 },     { 2, 0.55f }, { 3, 0.3f },
                                         { 4, 0.25f },    { 6, 0.08f }, { 8, 0.06f } };
        float              f = midiHz( midi );
        size_t             i0 = at( start ), i1 = at( start + len + 0.15f );
        for ( size_t i = i0; i < i1; ++i )
        {
            double t = ( i - i0 ) / (double)Rate;
            float  env = smooth01( (float)t / 0.025f ) * ( t > len ? std::exp( -( (float)t - len ) / 0.05f ) : 1.0f );
            float  v = 0;
            for ( const auto &b : bars )
                if ( f * b[0] < 12000 ) v += b[1] * (float)std::sin( TwoPiD * f * b[0] * t );
            m_s[i] += v * env * amp * 0.35f * fade( i, i1 );
        }
        noise( start, 0.05f, amp * 0.04f, { 0.004f, 0.0f, 0.015f }, Flt::BP, f * 4, f * 4, 1, 2 );   // the chiff
    }

    /** @brief A large bell (hum, prime, minor third, fifth, nominal and the shimmer above), slowly beating. */
    void bell( float start, float f, float amp, float decay )
    {
        static const float parts[][3] = { { 0.5f, 3.2f, 0.5f },    { 1.0f, 2.4f, 0.75f }, { 1.19f, 1.9f, 0.55f },
                                          { 1.5f, 1.4f, 0.3f },    { 2.0f, 1.2f, 0.7f },  { 2.52f, 0.7f, 0.3f },
                                          { 2.66f, 0.55f, 0.25f }, { 3.01f, 0.4f, 0.2f }, { 4.07f, 0.25f, 0.12f },
                                          { 5.38f, 0.15f, 0.08f } };
        std::vector<Mode>  modes;
        for ( const auto &p : parts )
        {
            modes.push_back( { f * p[0] * 0.9993f, p[1] * decay, p[2] * 0.5f } );
            modes.push_back( { f * p[0] * 1.0007f, p[1] * decay, p[2] * 0.5f } );
        }
        modal( start, amp, modes );
        noise( start, 0.03f, amp * 0.3f, { 0.0003f, 0.0f, 0.006f }, Flt::BP, f * 3, f * 3, 1, 1.5f );   // the clapper
    }

    /** @brief A small bright chime (a struck bar). */
    void chime( float start, float f, float amp )
    {
        modal( start, amp,
               { { f, 0.35f, 1 },
                 { f * 2.76f, 0.12f, 0.35f },
                 { f * 5.4f, 0.05f, 0.15f },
                 { f * 8.93f, 0.025f, 0.08f } } );
    }

    /** @brief The bell of a cash register. */
    void ching( float start, float f, float amp )
    {
        modal( start, amp,
               { { f, 0.5f, 1 }, { f * 2.32f, 0.25f, 0.5f }, { f * 4.25f, 0.12f, 0.3f }, { f * 6.63f, 0.06f, 0.2f } } );
    }

    /** @brief The whole sound through a filter. */
    void filter( Flt type, float hz, float q = 0.707f )
    {
        Svf f;
        f.set( hz, q );
        for ( float &v : m_s )
        {
            f.run( v );
            v = pick( f, type );
        }
    }

    /** @brief The whole sound through resonances (frequency, q) beside 'dry' of itself. */
    void formants( std::initializer_list<std::pair<float, float>> fs, float dry )
    {
        std::vector<Svf> f;
        for ( const auto &p : fs )
        {
            f.emplace_back();
            f.back().set( p.first, p.second );
        }
        for ( float &v : m_s )
        {
            float y = v * dry;
            for ( Svf &s : f )
            {
                s.run( v );
                y += s.bpn;
            }
            v = y;
        }
    }

    /** @brief Brought to a peak of 1, then bent by tanh (drive 1 gentle .. 4 hard). */
    void saturate( float drive )
    {
        float m = 1e-9f;
        for ( float v : m_s ) m = std::max( m, std::fabs( v ) );
        float k = std::tanh( drive );
        for ( float &v : m_s ) v = std::tanh( drive * v / m ) / k;
    }

    /** @brief A tremolo over the whole sound. */
    void trem( float hz, float depth )
    {
        for ( size_t i = 0; i < n(); ++i ) m_s[i] *= 1.0f - depth * ( 0.5f + 0.5f * std::sin( TwoPi * hz * i / Rate ) );
    }

    /** @brief A room: 'wet' of the reverb beside 'dry' of the sound. */
    void reverb( float wet, float size, float decay, float damp, float dry = 1.0f )
    {
        Reverb rv( size, decay, damp );
        for ( float &v : m_s )
        {
            float l, r;
            rv.run( v, l, r );
            v = v * dry + wet * 0.5f * ( l + r );
        }
    }

    /** @brief Done: no DC, peak at peakDb (dBFS), short fades at both ends. */
    std::vector<float> finishPeak( float peakDb )
    {
        // DC out (a one-pole highpass at ~10 Hz)
        float x1 = 0, y1 = 0;
        for ( float &v : m_s )
        {
            float y = v - x1 + 0.9986f * y1;
            x1 = v;
            y1 = y;
            v = y;
        }
        float m = 1e-9f;
        for ( float v : m_s ) m = std::max( m, std::fabs( v ) );
        float  g = std::pow( 10.0f, peakDb / 20.0f ) / m;
        size_t fin = std::min( n(), (size_t)( 0.008f * Rate ) ), fon = std::min( n(), (size_t)( 0.0003f * Rate ) );
        for ( size_t i = 0; i < n(); ++i )
        {
            float f = 1;
            if ( i < fon ) f = ( i + 1.0f ) / ( fon + 1.0f );
            if ( n() - i <= fin ) f *= ( n() - i - 1 ) / (float)fin;
            m_s[i] *= g * f;
        }
        return std::move( m_s );
    }

    /**
     * @brief Done: no DC, its loudest 400 ms at 'loud' (K-weighted momentary loudness, LUFS), the peak never above
     *        ceilDb (dBFS). When the peak would go higher it is driven into a soft clipper (at most 10 dB): punch.
     */
    std::vector<float> finish( float loud, float ceilDb = -3.0f )
    {
        m_s = finishPeak( ceilDb );
        float m0 = maxMomentary( m_s );
        if ( m0 >= loud )
        {
            float g = std::pow( 10.0f, ( loud - m0 ) / 20.0f );
            for ( float &v : m_s ) v *= g;
            return std::move( m_s );
        }
        float              ceil = std::pow( 10.0f, ceilDb / 20.0f ), lo = 0, hi = 10;
        std::vector<float> y( m_s.size() );
        auto               drive = [&]( float dB )
        {
            float g = std::pow( 10.0f, dB / 20.0f ), k = 0.6f * ceil;
            for ( size_t i = 0; i < y.size(); ++i )
            {
                float x = m_s[i] * g, a = std::fabs( x );
                if ( a > k ) a = k + ( ceil - k ) * std::tanh( ( a - k ) / ( ceil - k ) );
                y[i] = x < 0 ? -a : a;
            }
            return maxMomentary( y );
        };
        for ( int it = 0; it < 12; ++it )
        {
            float mid = 0.5f * ( lo + hi );
            ( drive( mid ) < loud ? lo : hi ) = mid;
        }
        drive( hi );
        return y;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// the effects
// ---------------------------------------------------------------------------------------------------------------

/** @brief One shot of the chain gun: crack, blast, punch and the bolt. */
void gunShot( Fx &s, float t0, float p )
{
    s.noise( t0, 0.03f, 1.0f, { 0.0002f, 0.0f, 0.004f }, Flt::HP, 2500 * p, 2500 * p, 1 );
    s.noise( t0, 0.12f, 0.9f, { 0.0005f, 0.004f, 0.025f }, Flt::BP, 1600 * p, 500 * p, 0.05f, 0.9f );
    s.kick( t0, 260 * p, 75 * p, 0.012f, 1.0f, { 0.0008f, 0.002f, 0.035f } );
    s.modal( t0 + 0.006f, 0.12f, { { 2650 * p, 0.018f, 1 }, { 4150 * p, 0.012f, 0.7f } } );
}

/** @brief An explosion (big 0 a car-sized blast .. 1 a fuel tanker). */
void explosion( Fx &s, float t0, float big )
{
    s.noise( t0, 0.03f, 1.0f, { 0.0002f, 0.0f, 0.004f + 0.004f * big }, Flt::HP, 2000, 2000, 1 );   // the crack
    s.kick( t0, 110 - 30 * big, 30 - 6 * big, 0.04f + 0.03f * big, 1.5f,
            { 0.001f, 0.02f, 0.22f + 0.25f * big } );   // the punch
    s.noise( t0, 0.6f + 1.2f * big, 1.1f, { 0.001f, 0.03f, 0.25f + 0.35f * big }, Flt::LP, 5000, 120,
             0.4f + 0.5f * big );   // the blast
    s.noise( t0 + 0.01f, 1.2f + 1.8f * big, 0.6f, { 0.03f, 0.05f, 0.5f + 0.9f * big }, Flt::LP, 320, 140,
             1.0f + big );   // the rumble
    s.noise( t0 + 0.02f, 0.9f + 0.5f * big, 0.35f, { 0.08f, 0.05f, 0.25f + 0.2f * big }, Flt::BP, 500, 900, 0.5f,
             0.8f );                                                                            // the fireball
    s.grains( t0 + 0.03f, 0.8f + 0.9f * big, 140, 6, 0.35f, 700, 5000, 0.002f, 0.01f, 1.0f );   // debris
}

/** @brief Metal folding up: buzzy creaks and crackle through panel resonances. */
void crunch( Fx &s, float t0, float len, float amp )
{
    Fx    c( len + 0.1f, s.m_rng.next() );
    float t = 0;
    while ( t < len - 0.05f )
    {
        float d = c.m_rng.range( 0.03f, 0.09f ), f = c.m_rng.range( 90, 260 );
        c.voice(
            t, d, [f, d]( float x ) { return f * ( 1.0f - 0.3f * x / d ); },
            c.m_rng.range( 0.4f, 1.0f ) * std::exp( -t / len ), { 0.002f, d * 0.5f, d * 0.4f }, Wv::SAW, 0.6f );
        c.grains( t, d, 600, 200, 0.3f, 800, 5000, 0.001f, 0.004f );
        t += d * c.m_rng.range( 0.6f, 1.4f );
    }
    c.formants( { { 1100, 2.5f }, { 2300, 3.5f }, { 3700, 4.0f } }, 0.3f );
    float m = 1e-9f;
    for ( float v : c.m_s ) m = std::max( m, std::fabs( v ) );
    for ( float &v : c.m_s ) v /= m;
    s.mix( c.m_s, t0, amp );
}

/** @brief Glass bursting: a crack and a shower of shards. */
void shatter( Fx &s, float t0, float amp )
{
    s.noise( t0, 0.02f, amp, { 0.0001f, 0.0f, 0.005f }, Flt::HP, 2000, 2000, 1 );
    for ( int k = 0; k < 70; ++k )
    {
        float t = 0.9f * std::pow( s.m_rng.unit(), 1.8f ), f = 2500 * std::pow( 4.0f, s.m_rng.unit() );
        float a = s.m_rng.range( 0.15f, 1.0f ) * std::exp( -t * 2.5f );
        s.modal( t0 + t, 0.35f * amp * a,
                 { { f, s.m_rng.range( 0.01f, 0.06f ), 1 }, { f * s.m_rng.range( 1.3f, 1.7f ), 0.015f, 0.5f } } );
    }
    s.noise( t0, 0.6f, 0.3f * amp, { 0.001f, 0.02f, 0.12f }, Flt::HP, 5000, 5000, 1 );
}

std::vector<float> thunder( uint32_t seed, float near );   // with the ambiences, below

/** @brief One voice in a crowd: a shout ("hey", "oh", "ja") whose pitch slides, through a vowel's formants. */
std::vector<float> crowdShout( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    float d = r.range( 0.22f, 0.55f ), f0 = r.range( 160, 330 ), f1 = f0 * r.range( 0.75f, 1.3f );
    Fx    s( d + 0.4f, seed );
    s.voice( 0, d, [=]( float t ) { return f0 + ( f1 - f0 ) * t / d; }, 0.6f, { 0.025f, d * 0.4f, d * 0.25f }, Wv::SAW, 0.35f );
    s.noise( 0, d, 0.12f, { 0.02f, d * 0.4f, d * 0.2f }, Flt::BP, 1800, 1800, 1, 1.5f );
    static const float vowels[][2] = { { 730, 1090 }, { 570, 840 }, { 300, 870 }, { 530, 1840 }, { 660, 1720 } };
    int                v = r.below( 5 );
    s.formants( { { vowels[v][0], 4.0f }, { vowels[v][1], 5.0f }, { 2500, 6.0f } }, 0.05f );
    s.reverb( 0.35f, 0.6f, 0.6f, 0.5f, 0.85f );
    return s.finishPeak( 0 );
}

/** @brief Someone whistling through their fingers: one or two shrill notes. */
std::vector<float> fingerWhistle( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    float f = r.range( 2300, 3100 );
    Fx    s( 1.3f, seed );
    s.voice( 0, 0.42f, [=]( float t ) { return f * ( 0.88f + 0.12f * std::min( 1.0f, t / 0.07f ) ) * ( 1.0f + 0.008f * std::sin( 38 * t ) ); },
             0.5f, { 0.015f, 0.3f, 0.05f }, Wv::SINE, 0.12f );
    if ( r.unit() < 0.6f )
        s.voice( 0.5f, 0.55f, [=]( float t ) { return f * 1.12f * ( 1.0f - 0.3f * t ); }, 0.5f, { 0.015f, 0.35f, 0.08f }, Wv::SINE, 0.12f );
    s.noise( 0, 1.0f, 0.03f, { 0.02f, 0.8f, 0.1f }, Flt::BP, f, f, 1, 4 );
    return s.finishPeak( 0 );
}

/** @brief Many voices at once: noise through the vowels' formants, the level following the envelope. */
void crowdRoar( Fx &s, float t0, float len, float amp, Env e, float bright )
{
    s.noise( t0, len, amp, e, Flt::BP, 520, 560, len, 2.0f );
    s.noise( t0, len, amp * 0.7f, e, Flt::BP, 1100 * bright, 1250 * bright, len, 2.4f );
    s.noise( t0, len, amp * 0.35f, e, Flt::BP, 2500 * bright, 2700 * bright, len, 3.0f );
    s.noise( t0, len, amp * 0.4f, e, Flt::LP, 280, 280, 1 );
}

/** @brief Hands clapping: short bright bursts at random, 'rate' a second at their thickest. */
void claps( Fx &s, float t0, float len, float rate, float amp )
{
    float t = 0;
    while ( true )
    {
        float k = std::sin( Pi * clampf( t / len, 0, 1 ) );   // swelling, then dying away
        t += -std::log( 1.0f - s.m_rng.unit() * 0.9999f ) / std::max( 2.0f, rate * ( 0.25f + 0.75f * k ) );
        if ( t >= len ) break;
        float f = s.m_rng.range( 1100, 2600 );
        s.noise( t0 + t, 0.02f, amp * s.m_rng.range( 0.4f, 1.0f ) * ( 0.3f + 0.7f * k ), { 0.0004f, 0.0f, 0.008f }, Flt::BP, f, f, 1, 1.3f );
    }
}

/** @brief Makes one effect. */
std::vector<float> makeSfx( SfxE e )
{
    switch ( e )
    {
    // --- guns and weapons ---
    case SfxE::MG:
    {
        Fx s( 0.16f, 11 );
        gunShot( s, 0, 1.0f );
        s.saturate( 2.2f );
        return s.finish( -23 );
    }
    case SfxE::TWINMG:
    {
        Fx s( 0.2f, 12 );
        gunShot( s, 0, 1.05f );
        gunShot( s, 0.032f, 0.93f );
        s.saturate( 2.0f );
        return s.finish( -22 );
    }
    case SfxE::CANNON:
    {
        Fx s( 1.0f, 13 );
        s.noise( 0, 0.03f, 1.0f, { 0.0002f, 0.0f, 0.006f }, Flt::HP, 3000, 3000, 1 );
        s.kick( 0, 150, 38, 0.03f, 1.4f, { 0.001f, 0.01f, 0.16f } );
        s.noise( 0, 0.6f, 1.0f, { 0.001f, 0.01f, 0.12f }, Flt::LP, 4000, 250, 0.25f );
        s.noise( 0.01f, 0.9f, 0.45f, { 0.01f, 0.0f, 0.3f }, Flt::LP, 700, 200, 0.6f );
        s.modal( 0.002f, 0.12f, { { 520, 0.25f, 1 }, { 1310, 0.15f, 0.6f }, { 2240, 0.08f, 0.4f } } );
        s.saturate( 1.8f );
        s.reverb( 0.12f, 0.8f, 0.6f, 0.4f );
        return s.finish( -15 );
    }
    case SfxE::FLAME_START:
    {
        Fx s( 0.9f, 14 );
        s.noise( 0, 0.01f, 0.5f, { 0.0002f, 0.0f, 0.002f }, Flt::HP, 4000, 4000, 1 );   // the igniter clicks
        s.noise( 0.05f, 0.01f, 0.5f, { 0.0002f, 0.0f, 0.002f }, Flt::HP, 4500, 4500, 1 );
        s.noise( 0.02f, 0.2f, 0.25f, { 0.02f, 0.03f, 0.05f }, Flt::HP, 3000, 3000, 1 );           // gas
        s.noise( 0.09f, 0.8f, 1.0f, { 0.06f, 0.05f, 0.25f }, Flt::LP, 250, 2200, 0.15f, 1.2f );   // whoomp
        s.kick( 0.09f, 90, 45, 0.05f, 0.7f, { 0.03f, 0.02f, 0.12f } );
        s.grains( 0.12f, 0.6f, 60, 10, 0.25f, 800, 4000, 0.002f, 0.006f );
        s.saturate( 1.5f );
        return s.finish( -18 );
    }
    case SfxE::RICOCHET:
    {
        Fx s( 0.55f, 15 );
        s.noise( 0, 0.01f, 0.7f, { 0.0002f, 0.0f, 0.002f }, Flt::HP, 3500, 3500, 1 );
        auto f = []( float t )
        { return 3900 * std::pow( 0.38f, std::min( 1.0f, t / 0.42f ) ) * ( 1 + 0.012f * std::sin( TwoPi * 37 * t ) ); };
        s.voice( 0.003f, 0.5f, f, 0.6f, { 0.002f, 0.02f, 0.13f }, Wv::SINE );
        s.voice( 0.003f, 0.5f, [f]( float t ) { return 1.51f * f( t ); }, 0.12f, { 0.002f, 0.01f, 0.08f }, Wv::TRI );
        return s.finish( -21 );
    }
    case SfxE::BULLET_HIT:
    {
        Fx s( 0.18f, 16 );
        s.noise( 0, 0.02f, 0.8f, { 0.0002f, 0.0f, 0.003f }, Flt::HP, 2500, 2500, 1 );
        s.modal( 0, 0.5f,
                 { { 1780, 0.04f, 1 }, { 3120, 0.03f, 0.8f }, { 4710, 0.018f, 0.6f }, { 6230, 0.012f, 0.4f } } );
        s.kick( 0, 380, 160, 0.01f, 0.5f, { 0.0005f, 0.0f, 0.02f } );
        return s.finish( -26 );
    }
    case SfxE::ROCKET_LAUNCH:
    {
        Fx s( 1.1f, 17 );
        s.kick( 0, 200, 60, 0.02f, 0.9f, { 0.001f, 0.0f, 0.05f } );
        s.noise( 0, 0.02f, 0.8f, { 0.0003f, 0.0f, 0.006f }, Flt::HP, 2500, 2500, 1 );
        s.noise( 0.01f, 1.0f, 1.0f, { 0.06f, 0.15f, 0.35f }, Flt::BP, 350, 2600, 0.6f, 1.3f );
        s.noise( 0.01f, 1.0f, 0.6f, { 0.04f, 0.1f, 0.3f }, Flt::LP, 500, 300, 0.5f );
        s.grains( 0.03f, 0.7f, 120, 30, 0.3f, 1000, 6000, 0.001f, 0.004f );
        s.saturate( 1.6f );
        return s.finish( -16 );
    }
    case SfxE::MINE_DROP:
    {
        Fx   s( 0.5f, 18 );
        auto clunk = [&s]( float t, float a )
        {
            s.kick( t, 170, 90, 0.015f, a, { 0.0005f, 0.0f, 0.05f } );
            s.modal( t, 0.4f * a,
                     { { 430, 0.12f, 1 }, { 990, 0.08f, 0.7f }, { 1740, 0.05f, 0.5f }, { 2810, 0.03f, 0.3f } } );
            s.noise( t, 0.03f, 0.4f * a, { 0.0003f, 0.0f, 0.008f }, Flt::BP, 1500, 1500, 1, 0.8f );
        };
        clunk( 0, 1.0f );
        clunk( 0.15f, 0.35f );
        clunk( 0.24f, 0.12f );
        return s.finish( -21 );
    }
    case SfxE::MINE_ARM:
    {
        Fx s( 0.32f, 19 );
        s.noise( 0, 0.01f, 0.3f, { 0.0002f, 0.0f, 0.002f }, Flt::HP, 5000, 5000, 1 );
        s.tone( 0.01f, 0.07f, 2093, 2093, 0, 0.5f, { 0.002f, 0.05f, 0.02f }, Wv::SQUARE );
        s.tone( 0.15f, 0.09f, 2637, 2637, 0, 0.5f, { 0.002f, 0.07f, 0.02f }, Wv::SQUARE );
        s.filter( Flt::LP, 6000 );
        return s.finish( -23 );
    }
    case SfxE::SPIKES_DROP:
    {
        Fx s( 0.7f, 20 );
        s.kick( 0, 220, 110, 0.01f, 0.4f, { 0.0005f, 0.0f, 0.03f } );
        for ( int k = 0; k < 16; ++k )
        {
            float t = 0.45f * std::pow( s.m_rng.unit(), 1.4f ), a = s.m_rng.range( 0.3f, 1.0f ) * ( 1.0f - t / 0.6f );
            float f = s.m_rng.range( 2200, 5200 );
            s.modal(
                t, 0.4f * a,
                { { f, s.m_rng.range( 0.03f, 0.07f ), 1 }, { f * 1.47f, 0.025f, 0.5f }, { f * 2.31f, 0.015f, 0.3f } } );
        }
        return s.finish( -20 );
    }
    case SfxE::OIL_DROP:
    {
        Fx s( 0.7f, 21 );
        for ( int k = 0; k < 3; ++k )   // glug, glug, glug
            s.voice(
                k * 0.1f + s.m_rng.range( 0.0f, 0.02f ), 0.07f, []( float x ) { return 170 + 3500 * x; }, 0.6f,
                { 0.003f, 0.02f, 0.025f }, Wv::SINE );
        s.noise( 0.28f, 0.4f, 0.8f, { 0.003f, 0.01f, 0.08f }, Flt::LP, 1400, 400, 0.15f );   // splat
        s.kick( 0.28f, 120, 60, 0.02f, 0.5f, { 0.002f, 0.0f, 0.05f } );
        s.grains( 0.3f, 0.25f, 60, 10, 0.15f, 600, 2000, 0.004f, 0.01f );
        return s.finish( -21 );
    }
    case SfxE::SMOKE_DROP:
    {
        Fx s( 1.4f, 22 );
        s.kick( 0, 140, 70, 0.015f, 0.6f, { 0.0005f, 0.0f, 0.04f } );
        s.noise( 0, 0.02f, 0.5f, { 0.0002f, 0.0f, 0.004f }, Flt::HP, 3000, 3000, 1 );
        s.noise( 0.03f, 1.35f, 0.8f, { 0.03f, 0.15f, 0.45f }, Flt::BP, 3800, 2400, 1.0f, 0.6f );
        s.noise( 0.03f, 1.35f, 0.4f, { 0.05f, 0.1f, 0.4f }, Flt::LP, 1500, 800, 1.0f );
        return s.finish( -20 );
    }
    case SfxE::NITRO:
    {
        Fx s( 1.5f, 23 );
        s.noise( 0, 0.15f, 0.6f, { 0.003f, 0.02f, 0.05f }, Flt::HP, 4000, 4000, 1 );             // the valve
        s.noise( 0.03f, 1.45f, 1.0f, { 0.12f, 0.25f, 0.4f }, Flt::BP, 300, 2200, 0.9f, 1.4f );   // the whoosh
        s.noise( 0.03f, 1.45f, 0.8f, { 0.15f, 0.3f, 0.45f }, Flt::LP, 160, 700, 0.8f, 1.1f );    // the roar
        s.voice(
            0.05f, 1.3f, []( float t ) { return 520 + 900 * std::min( 1.0f, t / 0.9f ); }, 0.12f, { 0.15f, 0.4f, 0.3f },
            Wv::SAW );
        s.kick( 0.02f, 90, 45, 0.08f, 0.6f, { 0.03f, 0.1f, 0.25f } );
        s.saturate( 1.8f );
        return s.finish( -16 );
    }
    case SfxE::TIRE_BURST:
    {
        Fx s( 0.9f, 24 );
        s.noise( 0, 0.02f, 1.0f, { 0.0001f, 0.0f, 0.004f }, Flt::HP, 1500, 1500, 1 );
        s.noise( 0, 0.08f, 0.9f, { 0.0002f, 0.0f, 0.02f }, Flt::LP, 6000, 1500, 0.05f );
        s.kick( 0, 220, 55, 0.012f, 1.2f, { 0.0005f, 0.0f, 0.06f } );
        s.noise( 0.01f, 0.7f, 0.35f, { 0.005f, 0.05f, 0.18f }, Flt::BP, 2600, 1800, 0.6f, 0.8f );   // the air
        for ( int k = 0; k < 6; ++k )   // the rubber flapping round
        {
            float t = 0.08f + 0.085f * k * ( 1 + 0.15f * k ), a = 1.0f - k / 7.0f;
            s.noise( t, 0.04f, 0.35f * a, { 0.002f, 0.0f, 0.015f }, Flt::LP, 700, 400, 0.04f );
            s.kick( t, 110, 70, 0.01f, 0.3f * a, { 0.001f, 0.0f, 0.02f } );
        }
        s.saturate( 1.5f );
        return s.finish( -16 );
    }

    // --- impacts ---
    case SfxE::CRASH_LIGHT:
    {
        Fx s( 0.5f, 25 );
        s.kick( 0, 130, 60, 0.015f, 1.0f, { 0.0005f, 0.0f, 0.06f } );
        s.noise( 0, 0.2f, 0.8f, { 0.0003f, 0.005f, 0.04f }, Flt::LP, 2500, 500, 0.08f );
        s.modal( 0.001f, 0.35f,
                 { { 340, 0.1f, 1 }, { 760, 0.07f, 0.8f }, { 1420, 0.05f, 0.6f }, { 2600, 0.03f, 0.4f } } );
        s.grains( 0, 0.06f, 400, 100, 0.35f, 1500, 6000, 0.001f, 0.004f );
        s.saturate( 1.4f );
        return s.finish( -20 );
    }
    case SfxE::CRASH_HEAVY:
    {
        Fx s( 1.2f, 26 );
        s.kick( 0, 95, 34, 0.025f, 1.4f, { 0.0008f, 0.01f, 0.14f } );
        s.noise( 0, 0.5f, 1.0f, { 0.0004f, 0.01f, 0.12f }, Flt::LP, 3500, 300, 0.25f );
        s.grains( 0, 0.3f, 500, 80, 0.6f, 600, 4500, 0.002f, 0.008f, 1.5f );
        s.modal( 0.002f, 0.35f,
                 { { 212, 0.35f, 1 },
                   { 534, 0.28f, 0.8f },
                   { 873, 0.22f, 0.7f },
                   { 1338, 0.16f, 0.5f },
                   { 2214, 0.09f, 0.35f } } );
        crunch( s, 0.02f, 0.3f, 0.35f );
        s.grains( 0.15f, 0.8f, 25, 4, 0.25f, 2000, 7000, 0.01f, 0.04f );   // bits falling off
        s.saturate( 1.6f );
        s.reverb( 0.1f, 0.7f, 0.5f, 0.4f );
        return s.finish( -14.5f );
    }
    case SfxE::CRASH_WALL:
    {
        Fx s( 1.1f, 27 );
        s.kick( 0, 80, 32, 0.02f, 1.5f, { 0.0008f, 0.01f, 0.13f } );
        s.noise( 0, 0.4f, 1.0f, { 0.0004f, 0.01f, 0.09f }, Flt::LP, 2500, 220, 0.2f );
        s.grains( 0, 0.25f, 700, 100, 0.6f, 300, 2200, 0.003f, 0.01f, 1.5f );   // concrete crumbling
        s.grains( 0.08f, 0.9f, 35, 3, 0.25f, 900, 3500, 0.005f, 0.02f );        // pebbles
        s.modal( 0.002f, 0.15f, { { 260, 0.2f, 1 }, { 690, 0.12f, 0.6f }, { 1190, 0.08f, 0.4f } } );
        s.saturate( 1.7f );
        s.reverb( 0.1f, 0.7f, 0.5f, 0.5f );
        return s.finish( -14.5f );
    }
    case SfxE::METAL_CRUNCH:
    {
        Fx s( 0.9f, 28 );
        crunch( s, 0, 0.75f, 1.0f );
        s.kick( 0, 110, 50, 0.02f, 0.8f, { 0.001f, 0.0f, 0.08f } );
        s.modal( 0.01f, 0.2f, { { 410, 0.2f, 1 }, { 1080, 0.12f, 0.7f }, { 1830, 0.08f, 0.5f } } );
        s.saturate( 2.0f );
        return s.finish( -17 );
    }
    case SfxE::GLASS:
    {
        Fx s( 1.3f, 29 );
        shatter( s, 0, 1.0f );
        s.kick( 0, 300, 150, 0.01f, 0.3f, { 0.0005f, 0.0f, 0.02f } );
        s.filter( Flt::HP, 900 );
        return s.finish( -19 );
    }
    case SfxE::EXPLOSION_SMALL:
    {
        Fx s( 1.8f, 30 );
        explosion( s, 0, 0 );
        s.saturate( 2.0f );
        s.reverb( 0.15f, 0.9f, 0.6f, 0.5f );
        return s.finish( -12.5f );
    }
    case SfxE::EXPLOSION_BIG:
    {
        Fx s( 3.4f, 31 );
        explosion( s, 0, 1 );
        s.kick( 0.07f, 70, 25, 0.06f, 1.0f, { 0.002f, 0.02f, 0.4f } );
        for ( float t : { 0.45f, 0.8f, 1.2f } )   // things going up after it
        {
            s.noise( t, 0.25f, 0.3f, { 0.001f, 0.0f, 0.05f }, Flt::LP, 2500, 600, 0.1f );
            s.kick( t, 90, 45, 0.02f, 0.25f, { 0.001f, 0.0f, 0.08f } );
        }
        s.saturate( 2.3f );
        s.reverb( 0.2f, 1.1f, 0.7f, 0.5f );
        return s.finish( -11 );
    }
    case SfxE::WRECK:
    {
        Fx s( 3.4f, 32 );
        explosion( s, 0, 0.35f );
        crunch( s, 0.0f, 0.5f, 0.45f );
        shatter( s, 0.04f, 0.35f );
        // a hubcap rolls away and wobbles to rest: contacts ever faster, ever softer, then a last clank
        float t = 1.0f, ph = 0;
        while ( t < 2.9f )
        {
            float u = ( t - 1.0f ) / 1.9f, rate = 5.0f + 30.0f * u * u;
            ph += rate / 400.0f;
            if ( ph >= 1.0f )
            {
                ph -= 1.0f;
                float a = 0.12f * ( 1.0f - 0.6f * u );
                s.modal( t, a, { { 1930, 0.05f, 1 }, { 3110, 0.035f, 0.6f }, { 4720, 0.02f, 0.4f } } );
            }
            t += 1.0f / 400.0f;
        }
        s.modal( 2.95f, 0.15f, { { 1930, 0.12f, 1 }, { 3110, 0.08f, 0.6f }, { 4720, 0.05f, 0.4f } } );
        s.saturate( 1.6f );
        s.reverb( 0.12f, 0.9f, 0.6f, 0.5f );
        return s.finish( -11.5f );
    }
    case SfxE::BARREL:
    {
        Fx s( 2.2f, 33 );
        s.modal( 0, 0.6f,
                 { { 165, 0.6f, 1 },
                   { 415, 0.45f, 0.8f },
                   { 770, 0.3f, 0.6f },
                   { 1160, 0.2f, 0.4f },
                   { 1720, 0.12f, 0.3f } } );
        explosion( s, 0.035f, 0.45f );
        s.saturate( 2.0f );
        s.reverb( 0.15f, 0.9f, 0.6f, 0.5f );
        return s.finish( -12 );
    }

    // --- the race ---
    case SfxE::COUNT_BEEP:
    {
        Fx s( 0.3f, 40 );
        s.tone( 0, 0.2f, 880, 880, 0, 0.6f, { 0.003f, 0.17f, 0.03f }, Wv::SQUARE );
        s.tone( 0, 0.2f, 880, 880, 0, 0.4f, { 0.003f, 0.17f, 0.03f }, Wv::SINE );
        s.filter( Flt::LP, 3500 );
        return s.finish( -17 );
    }
    case SfxE::COUNT_GO:
    {
        Fx s( 0.85f, 41 );
        for ( float f : { 1760.0f, 880.0f, 1318.5f } )
            s.tone( 0, 0.65f, f, f, 0, f > 1700 ? 0.5f : 0.35f, { 0.003f, 0.35f, 0.15f }, Wv::SQUARE );
        s.filter( Flt::LP, 4500 );
        return s.finish( -15 );
    }
    case SfxE::LAP:
    {
        Fx s( 0.6f, 42 );
        s.chime( 0, 1046.5f, 0.6f );
        s.chime( 0.09f, 1568, 0.7f );
        return s.finish( -19 );
    }
    case SfxE::FINAL_LAP:
    {
        Fx s( 1.5f, 43 );
        for ( int k = 0; k < 3; ++k ) s.chime( k * 0.14f, 1568, 0.5f );
        for ( float m : { 50.0f, 57.0f, 62.0f, 65.0f } )
            s.brass( 0.45f, 0.75f, midiHz( m ), 0.22f, 1.2f );   // a grim brass stab
        return s.finish( -17 );
    }
    case SfxE::FINISH:
    {
        Fx          s( 2.2f, 44 );
        const float notes[][3] = { { 67, 0.0f, 0.1f },  { 72, 0.1f, 0.1f },  { 76, 0.2f, 0.1f },
                                   { 79, 0.3f, 0.13f }, { 76, 0.45f, 0.1f }, { 79, 0.56f, 1.0f } };
        for ( const auto &n : notes ) s.brass( n[1], n[2], midiHz( n[0] ), 0.4f, 1.3f );
        for ( float m : { 60.0f, 64.0f, 67.0f } ) s.brass( 0.56f, 1.0f, midiHz( m ), 0.18f, 1.0f );
        s.noise( 0.56f, 1.4f, 0.25f, { 0.002f, 0.02f, 0.5f }, Flt::HP, 6000, 6000, 1 );   // the cymbal
        s.kick( 0.56f, 120, 60, 0.03f, 0.5f, { 0.001f, 0.0f, 0.15f } );
        s.reverb( 0.12f, 0.8f, 0.5f, 0.5f );
        return s.finish( -15 );
    }
    case SfxE::WRONG_WAY:
    {
        Fx s( 0.85f, 45 );
        for ( int k = 0; k < 2; ++k )
        {
            s.tone( k * 0.36f, 0.26f, 233, 233, 0, 0.5f, { 0.005f, 0.22f, 0.03f }, Wv::SQUARE );
            s.tone( k * 0.36f, 0.26f, 247, 247, 0, 0.5f, { 0.005f, 0.22f, 0.03f }, Wv::SAW );
        }
        s.filter( Flt::LP, 2200, 0.9f );
        s.saturate( 1.5f );
        return s.finish( -19 );
    }
    case SfxE::BELL:
    {
        Fx s( 4.5f, 46 );
        s.bell( 0, 294, 1.0f, 1.0f );
        s.reverb( 0.2f, 1.0f, 0.75f, 0.4f );
        return s.finish( -17 );
    }

    // --- pickups ---
    case SfxE::PICK_CASH:
    {
        Fx s( 0.8f, 50 );
        s.noise( 0, 0.02f, 0.5f, { 0.0002f, 0.0f, 0.004f }, Flt::BP, 2500, 2500, 1, 1.5f );   // the key
        s.kick( 0.05f, 160, 90, 0.015f, 0.5f, { 0.001f, 0.0f, 0.04f } );                      // the drawer
        s.modal( 0.05f, 0.2f, { { 720, 0.06f, 1 }, { 1630, 0.04f, 0.5f } } );
        s.ching( 0.08f, 2637, 0.6f );
        s.ching( 0.12f, 3520, 0.4f );
        return s.finish( -19 );
    }
    case SfxE::PICK_WRENCH:
    {
        Fx s( 0.55f, 51 );
        for ( int k = 0; k < 5; ++k )   // the ratchet
        {
            s.modal( k * 0.028f, 0.35f, { { 3200, 0.012f, 1 }, { 5100, 0.008f, 0.6f } } );
            s.noise( k * 0.028f, 0.005f, 0.3f, { 0.0001f, 0.0f, 0.002f }, Flt::HP, 3000, 3000, 1 );
        }
        s.chime( 0.16f, 880, 0.4f );
        s.chime( 0.24f, 1318.5f, 0.45f );
        return s.finish( -19 );
    }
    case SfxE::PICK_AMMO:
    {
        Fx   s( 0.45f, 52 );
        auto clack = [&s]( float t, float p )
        {
            s.noise( t, 0.03f, 0.7f, { 0.0003f, 0.0f, 0.01f }, Flt::BP, 1900 * p, 1900 * p, 1, 1.2f );
            s.modal( t, 0.4f, { { 1150 * p, 0.04f, 1 }, { 2630 * p, 0.03f, 0.6f }, { 4100 * p, 0.015f, 0.4f } } );
            s.kick( t, 200 * p, 110 * p, 0.01f, 0.4f, { 0.0005f, 0.0f, 0.03f } );
        };
        clack( 0, 1.1f );
        clack( 0.13f, 0.85f );
        return s.finish( -20 );
    }
    case SfxE::PICK_NITRO:
    {
        Fx s( 0.6f, 53 );
        s.noise( 0, 0.12f, 0.4f, { 0.002f, 0.02f, 0.04f }, Flt::BP, 4500, 4500, 1, 1 );
        s.voice(
            0.02f, 0.35f, []( float t ) { return 420 * std::pow( 4.0f, std::min( 1.0f, t / 0.25f ) ); }, 0.5f,
            { 0.01f, 0.15f, 0.08f }, Wv::SQUARE, 0, 0.3f );
        s.chime( 0.22f, 2093, 0.3f );
        s.chime( 0.3f, 3136, 0.3f );
        s.filter( Flt::LP, 7000 );
        return s.finish( -19 );
    }
    case SfxE::PICK_WEAPON:
    {
        Fx s( 0.75f, 54 );
        s.noise( 0, 0.01f, 0.5f, { 0.0001f, 0.0f, 0.003f }, Flt::HP, 3500, 3500, 1 );
        s.modal( 0, 0.25f, { { 2600, 0.02f, 1 }, { 4300, 0.012f, 0.6f } } );
        s.noise( 0.09f, 0.04f, 0.7f, { 0.0003f, 0.0f, 0.012f }, Flt::BP, 1300, 1300, 1, 1 );
        s.modal( 0.09f, 0.45f, { { 880, 0.06f, 1 }, { 2110, 0.04f, 0.6f }, { 3320, 0.02f, 0.4f } } );
        s.kick( 0.2f, 130, 60, 0.02f, 1.0f, { 0.0008f, 0.0f, 0.08f } );
        s.modal( 0.2f, 0.4f, { { 450, 0.12f, 1 }, { 1130, 0.07f, 0.5f } } );
        s.brass( 0.22f, 0.3f, midiHz( 38 ), 0.25f, 1.0f );   // a low note of menace
        return s.finish( -18 );
    }
    case SfxE::PICK_MYSTERY:
    {
        Fx          s( 1.0f, 55 );
        const float m[] = { 69, 72, 76, 80, 84 };
        for ( int k = 0; k < 5; ++k )
        {
            float f = midiHz( m[k] );
            auto  fq = [f]( float t ) { return f * ( 1 + 0.015f * std::sin( TwoPi * 6.5f * t ) ); };
            s.voice( k * 0.11f, 0.4f, fq, 0.35f, { 0.02f, 0.05f, 0.18f }, Wv::SINE );
            s.voice(
                k * 0.11f, 0.4f, [fq]( float t ) { return 2 * fq( t ); }, 0.08f, { 0.02f, 0.05f, 0.12f }, Wv::TRI );
        }
        s.reverb( 0.25f, 0.9f, 0.7f, 0.4f );
        return s.finish( -19 );
    }

    // --- horns ---
    case SfxE::HORN_PLAIN:
    {
        // an ordinary electric horn: two buzzing diaphragms a major third apart
        Fx s( 0.65f, 70 );
        for ( float f : { 415.3f, 523.3f } )
            s.voice(
                0, 0.55f, [f]( float t ) { return f * ( 1 - 0.01f * std::exp( -t / 0.02f ) ); }, 0.5f,
                { 0.008f, 0.5f, 0.03f }, Wv::SQUARE, 0.04f, 0.18f );
        s.formants( { { 1850, 2.5f }, { 3100, 3.0f }, { 780, 2.0f } }, 0.25f );
        s.saturate( 2.5f );
        s.filter( Flt::HP, 250 );
        return s.finish( -16 );
    }
    case SfxE::HORN_TRUCK:
    {
        // three air trumpets, a dark chord, the pressure building at the start
        Fx s( 1.35f, 71 );
        for ( float f : { 233.1f, 277.2f, 349.2f } )
            s.voice(
                0, 1.2f, [f]( float t ) { return f * ( 1 - 0.035f * std::exp( -t / 0.05f ) ); }, 0.4f,
                { 0.04f, 1.0f, 0.06f }, Wv::SAW, 0.03f );
        s.noise( 0, 1.2f, 0.06f, { 0.03f, 1.0f, 0.06f }, Flt::HP, 3000, 3000, 1 );
        s.formants( { { 650, 1.5f }, { 1350, 2.0f }, { 2450, 3.0f } }, 0.35f );
        s.saturate( 1.8f );
        return s.finish( -15 );
    }
    case SfxE::HORN_DIXIE:
    {
        // a musical air horn: five trumpets play a jaunty little tune (our own)
        Fx          s( 2.6f, 72 );
        const float mel[][2] = { { 67, 1 }, { 71, 1 }, { 74, 1 }, { 79, 2 }, { 76, 1 }, { 79, 1 }, { 74, 3 },
                                 { 0, 1 },  { 71, 1 }, { 74, 1 }, { 76, 1 }, { 74, 1 }, { 71, 1 }, { 67, 3 } };
        float       beat = 0.115f, t = 0;
        for ( const auto &n : mel )
        {
            if ( n[0] > 0 )
            {
                float f = midiHz( n[0] ), d = n[1] * beat * 0.88f;
                s.voice(
                    t, d, [f]( float x ) { return f * ( 1 - 0.02f * std::exp( -x / 0.015f ) ); }, 0.5f,
                    { 0.012f, d - 0.03f, 0.02f }, Wv::SAW, 0.02f );
            }
            t += n[1] * beat;
        }
        s.formants( { { 1150, 2.0f }, { 2500, 2.5f }, { 3600, 3.0f } }, 0.35f );
        s.saturate( 1.9f );
        return s.finish( -16 );
    }
    case SfxE::HORN_ICE:
    {
        // an ice-cream van: a worn music box through a horn speaker, its tape warbling (our own tune)
        Fx          s( 2.6f, 73 );
        const float mel[] = { 72, 76, 79, 84, 79, 76, 77, 81, 84, 81, 77, 74, 72 };
        float       beat = 0.13f;
        for ( int k = 0; k < 13; ++k )
        {
            float t = k * beat, f = midiHz( mel[k] );
            auto  wob = [f, t]( float x )
            {
                float a = t + x;
                return f * ( 1 + 0.006f * std::sin( TwoPi * 0.8f * a + 1 ) + 0.003f * std::sin( TwoPi * 6 * a ) );
            };
            float d = k == 12 ? 0.9f : 0.6f;
            s.voice( t, d, wob, 0.45f, { 0.004f, 0.0f, k == 12 ? 0.45f : 0.28f }, Wv::SINE );
            s.voice( t, 0.3f, [wob]( float x ) { return 2 * wob( x ); }, 0.15f, { 0.002f, 0.0f, 0.09f }, Wv::SINE );
            s.voice( t, d * 0.7f, wob, 0.18f, { 0.004f, 0.02f, 0.15f }, Wv::SQUARE );
            s.voice(
                t, 0.08f, [wob]( float x ) { return 3.01f * wob( x ); }, 0.08f, { 0.001f, 0.0f, 0.02f }, Wv::SINE );
        }
        s.filter( Flt::HP, 450, 0.8f );
        s.filter( Flt::LP, 4200, 1.2f );
        s.saturate( 1.6f );
        return s.finish( -17 );
    }
    case SfxE::HORN_FUNERAL:
    {
        // a hearse's horn: two bars of a mournful march on organ and brass (our own)
        Fx          s( 3.9f, 74 );
        float       beat = 0.36f;
        const float mel[][2] = { { 69, 1 }, { 67, 0.5f }, { 65, 0.5f }, { 64, 1 },
                                 { 65, 1 }, { 62, 1 },    { 61, 1 },    { 62, 2 } };
        float       t = 0;
        for ( const auto &n : mel )
        {
            s.brass( t, n[1] * beat * 0.92f, midiHz( n[0] ), 0.3f, 0.9f );
            s.organ( t, n[1] * beat * 0.95f, n[0], 0.12f );
            t += n[1] * beat;
        }
        const struct
        {
            float at, len;
            float notes[4];
        } chords[] = { { 0, 1, { 50, 53, 57, 0 } }, { 1, 1, { 43, 50, 55, 58 } }, { 2, 1, { 45, 49, 52, 55 } },
                       { 3, 1, { 50, 53, 57, 0 } }, { 4, 1, { 46, 50, 53, 0 } },  { 5, 1, { 45, 49, 52, 0 } },
                       { 6, 2, { 38, 50, 53, 57 } } };
        for ( const auto &c : chords )
            for ( float m : c.notes )
                if ( m > 0 ) s.organ( c.at * beat, c.len * beat * 0.97f, m, 0.15f );
        s.trem( 5.5f, 0.12f );
        s.filter( Flt::HP, 90 );
        s.reverb( 0.18f, 0.9f, 0.7f, 0.5f );
        return s.finish( -16 );
    }
    case SfxE::HORN_SIREN:
    {
        // a police siren: a wail up, three yelps, a fall
        Fx   s( 1.6f, 75 );
        auto f = []( float t )
        {
            if ( t < 0.45f ) return 520 + 730 * smooth01( t / 0.45f );
            if ( t < 1.11f )
            {
                float u = std::fmod( t - 0.45f, 0.22f ) / 0.22f;
                return 1250 - 600 * ( 1 - std::fabs( 2 * u - 1 ) );
            }
            return 1250 - 770 * smooth01( ( t - 1.11f ) / 0.42f );
        };
        s.voice( 0, 1.55f, f, 0.5f, { 0.02f, 1.3f, 0.12f }, Wv::SQUARE );
        s.voice( 0, 1.55f, f, 0.4f, { 0.02f, 1.3f, 0.12f }, Wv::SINE );
        s.formants( { { 1600, 1.2f }, { 3000, 2.0f } }, 0.5f );
        s.saturate( 2.0f );
        s.filter( Flt::HP, 350 );
        return s.finish( -16 );
    }
    case SfxE::HORN_KLAXON:
    {
        // a-OO-ga: a ratchet hammering a diaphragm, spinning up and down
        Fx   s( 1.0f, 76 );
        auto f = []( float t )
        {
            if ( t < 0.06f ) return 150 + 50 * t / 0.06f;
            if ( t < 0.38f ) return 200 + 140 * smooth01( ( t - 0.06f ) / 0.32f );
            if ( t < 0.68f ) return 340 + 6 * std::sin( TwoPi * 7 * t );
            return 340 - 70 * smooth01( ( t - 0.68f ) / 0.25f );
        };
        s.voice( 0, 0.93f, f, 0.6f, { 0.01f, 0.75f, 0.08f }, Wv::SQUARE, 0.35f, 0.12f );
        s.formants( { { 900, 2.0f }, { 2200, 3.0f }, { 3400, 4.0f } }, 0.3f );
        s.saturate( 3.0f );
        return s.finish( -16 );
    }

    // --- the menus ---
    case SfxE::UI_MOVE:
    {
        Fx s( 0.045f, 60 );
        s.tone( 0, 0.03f, 2400, 2200, 0.03f, 0.5f, { 0.0005f, 0.0f, 0.008f }, Wv::TRI );
        s.noise( 0, 0.005f, 0.2f, { 0.0001f, 0.0f, 0.001f }, Flt::HP, 5000, 5000, 1 );
        return s.finish( -36 );
    }
    case SfxE::UI_OK:
    {
        Fx s( 0.16f, 61 );
        s.tone( 0, 0.05f, 1318.5f, 1318.5f, 0, 0.5f, { 0.001f, 0.02f, 0.02f }, Wv::TRI );
        s.tone( 0.05f, 0.1f, 1760, 1760, 0, 0.5f, { 0.001f, 0.03f, 0.04f }, Wv::TRI );
        return s.finish( -28 );
    }
    case SfxE::UI_BACK:
    {
        Fx s( 0.16f, 62 );
        s.tone( 0, 0.05f, 1318.5f, 1318.5f, 0, 0.5f, { 0.001f, 0.02f, 0.02f }, Wv::TRI );
        s.tone( 0.05f, 0.1f, 987.8f, 987.8f, 0, 0.5f, { 0.001f, 0.03f, 0.04f }, Wv::TRI );
        return s.finish( -28 );
    }
    case SfxE::UI_BUY:
    {
        Fx s( 0.45f, 63 );
        s.noise( 0, 0.01f, 0.4f, { 0.0002f, 0.0f, 0.003f }, Flt::BP, 2500, 2500, 1, 1.5f );
        s.ching( 0.02f, 2637, 0.6f );
        s.ching( 0.06f, 3520, 0.4f );
        return s.finish( -24 );
    }
    case SfxE::UI_DENY:
    {
        Fx s( 0.25f, 64 );
        s.tone( 0, 0.2f, 150, 150, 0, 0.5f, { 0.003f, 0.12f, 0.03f }, Wv::SQUARE );
        s.tone( 0, 0.2f, 158, 158, 0, 0.5f, { 0.003f, 0.12f, 0.03f }, Wv::SAW );
        s.filter( Flt::LP, 1500 );
        return s.finish( -27 );
    }
    case SfxE::UI_CASH:
    {
        Fx s( 0.55f, 65 );
        for ( int k = 0; k < 9; ++k )
        {
            float t = 0.35f * std::pow( s.m_rng.unit(), 1.3f ), f = s.m_rng.range( 3500, 6500 ),
                  d = s.m_rng.range( 0.04f, 0.12f );
            s.modal( t, s.m_rng.range( 0.4f, 1.0f ),
                     { { f, d, 1 }, { f * 1.83f, d * 0.6f, 0.5f }, { f * 2.9f, d * 0.4f, 0.3f } } );
        }
        return s.finish( -25 );
    }
    case SfxE::UI_PAPER:
    {
        Fx s( 0.4f, 66 );
        s.noise( 0, 0.35f, 0.5f, { 0.04f, 0.05f, 0.1f }, Flt::BP, 2600, 2600, 1, 0.8f );
        s.grains( 0, 0.3f, 300, 100, 0.4f, 1200, 6000, 0.001f, 0.003f );
        return s.finish( -28 );
    }
    case SfxE::BIRDS_UP:
    {
        // A flock taking off: a flurry of wing beats and two hoarse calls.
        Fx       s( 1.5f, 93 );
        uint32_t r = 12345;
        for ( int k = 0; k < 34; ++k )
        {
            r = r * 1664525u + 1013904223u;
            float t = k * 0.032f + ( r >> 24 ) / 255.0f * 0.03f;
            s.noise( t, 0.04f, 0.5f * ( 1.0f - k / 40.0f ), { 0.003f, 0.0f, 0.012f }, Flt::BP, 1400 + ( r >> 20 ) % 900, 900, 0.03f, 1.4f );
        }
        s.tone( 0.15f, 0.22f, 980, 700, 0.18f, 0.35f, { 0.01f, 0.06f, 0.06f }, Wv::SAW );
        s.tone( 0.55f, 0.2f, 900, 640, 0.16f, 0.28f, { 0.01f, 0.05f, 0.06f }, Wv::SAW );
        s.filter( Flt::BP, 1500, 0.7f );
        return s.finish( -24 );
    }
    case SfxE::DOG_BARK:
    {
        // A yard dog: two hoarse barks.
        Fx s( 0.75f, 94 );
        for ( int k = 0; k < 2; ++k )
        {
            float t = k * 0.27f;
            s.tone( t, 0.17f, 560, 290, 0.12f, 0.65f, { 0.006f, 0.035f, 0.05f }, Wv::SAW );
            s.tone( t, 0.17f, 1120, 580, 0.12f, 0.2f, { 0.006f, 0.035f, 0.04f }, Wv::SQUARE );
            s.noise( t, 0.15f, 0.4f, { 0.004f, 0.03f, 0.05f }, Flt::BP, 1000, 650, 0.1f, 1.2f );
        }
        s.filter( Flt::BP, 850, 0.8f );
        s.saturate( 2.2f );
        return s.finish( -20 );
    }
    case SfxE::THUNDER_NEAR:
    {
        // A strike in view: the crack at once, the roll after it, a little of the valley's echo.
        Fx s( 8.0f, 95 );
        s.mix( thunder( 7101, 1.0f ), 0.0f, 1.0f );
        s.reverb( 0.25f, 0.8f, 0.6f, 0.4f );
        return s.finish( -14, -2 );
    }
    case SfxE::THUNDER_FAR:
    {
        // Lightning in the clouds: only the low roll, from far off.
        Fx s( 7.0f, 96 );
        s.mix( thunder( 7202, 0.2f ), 0.0f, 1.0f );
        s.filter( Flt::LP, 700 );
        return s.finish( -20 );
    }
    case SfxE::FUNERAL_MARCH:
    {
        // A rival has died: the first bars of Chopin's funeral march (1837, free for all) on brass and organ over a
        // muffled drum, slow and a little out of tune, as a village band would play it.
        Fx          s( 6.2f, 91 );
        float       beat = 0.62f;
        const float mel[][2] = { { 58, 1 },    { 58, 0.75f }, { 58, 0.25f }, { 58, 2 },
                                 { 61, 1 },    { 60, 0.75f }, { 60, 0.25f }, { 58, 0.75f },
                                 { 58, 0.25f }, { 57, 0.75f }, { 58, 1.5f } };
        float       t = 0;
        for ( const auto &n : mel )
        {
            s.brass( t, n[1] * beat * 0.9f, midiHz( n[0] ) * 1.003f, 0.32f, 0.7f );
            s.brass( t, n[1] * beat * 0.9f, midiHz( n[0] - 12 ) * 0.997f, 0.18f, 0.5f );
            s.organ( t, n[1] * beat * 0.96f, n[0], 0.08f );
            t += n[1] * beat;
        }
        // The low chords, B flat minor and its fifth, on every beat; the drum on one and three.
        for ( int b = 0; b < 9; ++b )
        {
            s.organ( b * beat, beat * 0.9f, b % 2 ? 41 : 34, 0.12f );
            s.organ( b * beat, beat * 0.9f, 49, 0.06f );
            if ( b % 2 == 0 ) s.kick( b * beat, 70, 45, 0.25f, 0.5f, { 0.004f, 0.0f, 0.5f } );
        }
        s.trem( 4.5f, 0.1f );
        s.filter( Flt::HP, 60 );
        return s.finish( -20 );
    }
    case SfxE::CHEER:
    {
        // The stand jumps up: the roar swells, shouts and whistles come out of it, a few claps.
        Fx s( 3.6f, 201 );
        crowdRoar( s, 0, 3.4f, 1.0f, { 0.25f, 1.0f, 0.5f }, 1.15f );
        for ( int k = 0; k < 9; ++k ) s.mix( crowdShout( 2010 + k ), s.m_rng.range( 0.05f, 1.6f ), s.m_rng.range( 0.15f, 0.35f ) );
        for ( int k = 0; k < 3; ++k ) s.mix( fingerWhistle( 2030 + k ), s.m_rng.range( 0.1f, 1.4f ), 0.18f );
        claps( s, 0.4f, 2.8f, 40, 0.25f );
        s.reverb( 0.3f, 0.8f, 0.6f, 0.5f );
        return s.finish( -20 );
    }
    case SfxE::GASP:
    {
        // A crash before the stand: a long "oooh" of many voices, falling.
        Fx s( 2.2f, 202 );
        for ( int k = 0; k < 12; ++k )
        {
            float f0 = s.m_rng.range( 130, 300 ), st = s.m_rng.range( 0.0f, 0.15f );
            s.voice( st, 1.5f, [=]( float t ) { return f0 * ( 1.06f - 0.12f * t ); }, 0.12f, { 0.18f, 0.35f, 0.35f }, Wv::SAW, 0.4f );
        }
        s.noise( 0, 1.8f, 0.25f, { 0.2f, 0.3f, 0.4f }, Flt::BP, 450, 380, 1.5f, 2.0f );
        s.formants( { { 360, 4.0f }, { 760, 5.0f }, { 2400, 6.0f } }, 0.05f );
        s.reverb( 0.35f, 0.8f, 0.6f, 0.5f );
        return s.finish( -21 );
    }
    case SfxE::APPLAUSE:
    {
        // The finish: applause swelling and dying away, cheering under it.
        Fx s( 5.0f, 203 );
        claps( s, 0, 4.8f, 220, 0.5f );
        crowdRoar( s, 0, 4.5f, 0.35f, { 0.3f, 2.0f, 0.6f }, 1.1f );
        for ( int k = 0; k < 5; ++k ) s.mix( crowdShout( 2050 + k ), s.m_rng.range( 0.2f, 2.5f ), 0.2f );
        s.mix( fingerWhistle( 2060 ), 0.6f, 0.15f );
        s.reverb( 0.25f, 0.7f, 0.5f, 0.5f );
        return s.finish( -21 );
    }
    case SfxE::UI_STAMP:
    {
        Fx s( 0.3f, 67 );
        s.kick( 0, 120, 60, 0.012f, 1.0f, { 0.0005f, 0.0f, 0.04f } );
        s.noise( 0, 0.05f, 0.6f, { 0.0003f, 0.0f, 0.015f }, Flt::LP, 2500, 800, 0.03f );
        s.noise( 0, 0.02f, 0.4f, { 0.0001f, 0.0f, 0.004f }, Flt::HP, 2500, 2500, 1 );
        return s.finish( -28 );
    }
    default: break;
    }
    return std::vector<float>( 100, 0.0f );
}

// ---------------------------------------------------------------------------------------------------------------
// loops: a bed of noise made seamless by a crossfade, and periodic layers that wrap round exactly
// ---------------------------------------------------------------------------------------------------------------

/** @brief A layer of filtered noise in a loop. */
struct Layer
{
    float                     amp = 0.1f;       ///< its level
    Flt                       type = Flt::LP;   ///< its filter
    float                     hz = 1000;        ///< the cutoff
    float                     q = 0.707f;       ///< the resonance
    const std::vector<float> *amod = nullptr;   ///< a slow level movement (-1..1, periodic)
    float                     adepth = 0;       ///< how deep it moves
    const std::vector<float> *fmod = nullptr;   ///< a slow cutoff movement (-1..1, periodic)
    float                     fratio = 1;       ///< the cutoff moves between hz / fratio and hz * fratio
    float                     width = 1;        ///< 0 the same in both ears, 1 independent
    float                     pulse = 0;        ///< >0: chopped this many times a second (insects)
    float                     sharp = 1;        ///< how short the chops are
};

/** @brief A loop being made. */
class Bed
{
public:
    /** @brief A loop of 'seconds', its noise layers crossfaded over 'fade' seconds at the seam. */
    Bed( float seconds, float fade, bool stereo, uint32_t seed )
        : m_n( (size_t)( seconds * Rate ) ), m_f( (size_t)( fade * Rate ) ), m_stereo( stereo )
    {
        m_rng.seed( seed );
        m_xl.assign( len(), 0.0f );
        m_pl.assign( m_n, 0.0f );
        if ( m_stereo )
        {
            m_xr.assign( len(), 0.0f );
            m_pr.assign( m_n, 0.0f );
        }
    }

    size_t n() const { return m_n; }
    size_t len() const { return m_n + m_f; }
    float seconds() const { return m_n / (float)Rate; }

    /** @return the frequency nearest to hz that fits a whole number of times into the loop */
    float snap( float hz ) const { return std::max( 1.0f, std::round( hz * m_n / Rate ) ) * Rate / (float)m_n; }

    /** @return a slow random movement in -1..1 that repeats with the loop (sines of c0..c1 cycles in it) */
    std::vector<float> walk( int c0, int c1, int terms )
    {
        std::vector<double> acc( len(), 0.0 );
        for ( int k = 0; k < terms; ++k )
        {
            int    c = c0 + m_rng.below( c1 - c0 + 1 );
            double a = m_rng.range( 0.4f, 1.0f ) / std::sqrt( (double)c ), p = m_rng.unit() * TwoPiD,
                   w = TwoPiD * c / m_n;
            double cr = std::cos( w ), ci = std::sin( w ), zr = std::cos( p ), zi = std::sin( p );
            for ( size_t i = 0; i < len(); ++i )
            {
                acc[i] += a * zi;
                double r = zr * cr - zi * ci;
                zi = zr * ci + zi * cr;
                zr = r;
            }
        }
        double m = 1e-9;
        for ( double v : acc ) m = std::max( m, std::fabs( v ) );
        std::vector<float> w( len() );
        for ( size_t i = 0; i < len(); ++i ) w[i] = (float)( acc[i] / m );
        return w;
    }

    /** @brief Adds a layer of filtered noise. */
    void noise( const Layer &L )
    {
        Svf    fl, fr;
        float  norm = 1;
        double pw = L.pulse > 0 ? snap( L.pulse ) / Rate : 0.0;
        for ( size_t i = 0; i < len(); ++i )
        {
            if ( ( i & 15 ) == 0 )
            {
                float hz = L.hz * ( L.fmod ? std::pow( L.fratio, ( *L.fmod )[i] ) : 1.0f );
                fl.set( hz, L.q );
                fr.set( hz, L.q );
                norm = noiseNorm( L.type, hz, L.q );
            }
            float g = L.amp * norm * ( L.amod ? std::max( 0.0f, 1.0f + L.adepth * ( *L.amod )[i] ) : 1.0f );
            if ( pw > 0 )
            {
                double ph = pw * (double)( i % m_n );
                g *= std::pow( 0.5f + 0.5f * (float)std::sin( TwoPiD * ( ph - std::floor( ph ) ) ), L.sharp );
            }
            float a = m_rng.bi();
            fl.run( a );
            m_xl[i] += pick( fl, L.type ) * g;
            if ( m_stereo )
            {
                fr.run( L.width * m_rng.bi() + ( 1.0f - L.width ) * a );
                m_xr[i] += pick( fr, L.type ) * g;
            }
        }
    }

    /** @brief A steady tone (hum) that repeats with the loop, its level following 'mod'. */
    void hum( float hz, float amp, const std::vector<float> *mod = nullptr, float depth = 0 )
    {
        double f = snap( hz ) / Rate, ph = 0;
        for ( size_t i = 0; i < m_n; ++i )
        {
            float v =
                (float)std::sin( TwoPiD * ph ) * amp * ( mod ? std::max( 0.0f, 1.0f + depth * ( *mod )[i] ) : 1.0f );
            m_pl[i] += v;
            if ( m_stereo ) m_pr[i] += v;
            ph += f;
            if ( ph >= 1.0 ) ph -= 1.0;
        }
    }

    /** @brief Puts a sound at t (it wraps round the seam), panned. */
    void place( const std::vector<float> &clip, float t, float pan, float gain )
    {
        placeMoving( clip, t, pan, pan, gain );
    }

    /** @brief Puts a sound at t that moves from pan0 to pan1 (a car passing). */
    void placeMoving( const std::vector<float> &clip, float t, float pan0, float pan1, float gain )
    {
        size_t s0 = (size_t)( std::max( 0.0f, t ) * Rate ) % m_n;
        for ( size_t k = 0; k < clip.size(); ++k )
        {
            size_t i = ( s0 + k ) % m_n;
            if ( m_stereo )
            {
                float l, r;
                panGains( pan0 + ( pan1 - pan0 ) * k / (float)clip.size(), l, r );
                m_pl[i] += clip[k] * gain * l;
                m_pr[i] += clip[k] * gain * r;
            }
            else
                m_pl[i] += clip[k] * gain;
        }
    }

    /** @brief Random short resonant ticks all through the loop (drops, crackle, gravel), 'spread' wide. */
    void grains( float rate, float amp, float lo, float hi, float d0, float d1, float spread )
    {
        float t = 0, T = seconds();
        while ( true )
        {
            t += -std::log( 1.0f - m_rng.unit() * 0.9999f ) / rate;
            if ( t >= T ) break;
            float f = lo * std::pow( hi / lo, m_rng.unit() ), d = m_rng.range( d0, d1 ),
                  a = amp * m_rng.range( 0.15f, 1.0f );
            float ph = m_rng.unit() * TwoPi, w = TwoPi * f / Rate, k = std::exp( -1.0f / ( d * Rate ) ), l = 1, r = 1;
            if ( m_stereo ) panGains( spread * m_rng.bi(), l, r );
            size_t s0 = (size_t)( t * Rate ), len = (size_t)( d * 7.0f * Rate );
            for ( size_t j = 0; j < len; ++j )
            {
                size_t i = ( s0 + j ) % m_n;
                float  v = std::sin( ph ) * a;
                m_pl[i] += v * l;
                if ( m_stereo ) m_pr[i] += v * r;
                ph += w;
                a *= k;
            }
        }
    }

    /** @brief Done: the seam crossfaded, no DC, the level at rmsDb (but the peak no higher than peakDb). */
    std::vector<float> finish( float rmsDb, float peakDb )
    {
        int                ch = m_stereo ? 2 : 1;
        std::vector<float> out( m_n * ch );
        for ( size_t i = 0; i < m_n; ++i )
        {
            float a = 1, b = 0;
            if ( i < m_f )
            {
                float u = ( i + 0.5f ) / m_f;
                a = std::sin( 0.5f * Pi * u );
                b = std::cos( 0.5f * Pi * u );
            }
            out[i * ch] = m_xl[i] * a + ( i < m_f ? m_xl[m_n + i] * b : 0.0f ) + m_pl[i];
            if ( m_stereo ) out[i * ch + 1] = m_xr[i] * a + ( i < m_f ? m_xr[m_n + i] * b : 0.0f ) + m_pr[i];
        }
        for ( int c = 0; c < ch; ++c )
        {
            double mean = 0;
            for ( size_t i = 0; i < m_n; ++i ) mean += out[i * ch + c];
            mean /= m_n;
            for ( size_t i = 0; i < m_n; ++i ) out[i * ch + c] -= (float)mean;
        }
        double sq = 0;
        float  pk = 1e-9f;
        for ( float v : out )
        {
            sq += v * v;
            pk = std::max( pk, std::fabs( v ) );
        }
        float rms = (float)std::sqrt( sq / out.size() ) + 1e-9f;
        float g = std::min( std::pow( 10.0f, rmsDb / 20.0f ) / rms, std::pow( 10.0f, peakDb / 20.0f ) / pk );
        for ( float &v : out ) v *= g;
        return out;
    }

    Rand m_rng;   ///< its noise and its chances

private:
    size_t             m_n, m_f;
    bool               m_stereo;
    std::vector<float> m_xl, m_xr;   ///< the crossfaded layers (n + f)
    std::vector<float> m_pl, m_pr;   ///< the periodic layers (n)
};

/** @brief A sound far away: darker, in a large space, its tail kept. */
std::vector<float> distant( std::vector<float> x, float lp, float wet, float size, float peakDb, float tail = 2.0f )
{
    Fx s( x.size() / (float)Rate + tail, 1 );
    std::copy( x.begin(), x.end(), s.m_s.begin() );
    s.filter( Flt::LP, lp );
    s.reverb( wet, size, 0.85f, 0.5f, 1.0f - 0.5f * wet );
    return s.finishPeak( peakDb );
}

// --- the creatures and machines of the venues (each normalised to 0 dBFS; the venue sets their level) ---

/** @brief A crow: two to four caws. */
std::vector<float> crowCall( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    int   caws = 2 + r.below( 3 );
    Fx    s( caws * 0.5f + 0.4f, seed );
    float t = 0.05f;
    for ( int k = 0; k < caws; ++k )
    {
        float d = r.range( 0.2f, 0.3f ), f = r.range( 470, 590 );
        s.voice(
            t, d, [f, d]( float x ) { return f * ( 1.1f - 0.3f * x / d ); }, 1.0f, { 0.015f, d * 0.4f, d * 0.25f },
            Wv::SAW, 0.5f );
        t += d + r.range( 0.12f, 0.22f );
    }
    s.formants( { { 1250, 3.0f }, { 2150, 4.0f }, { 3300, 5.0f } }, 0.1f );
    s.saturate( 2.5f );
    return s.finishPeak( 0 );
}

/** @brief An owl: a long hoot, a pause, then a quavering hoo-hoo-hooo. */
std::vector<float> owlCall( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    float f = r.range( 370, 430 );
    Fx    s( 3.0f, seed );
    auto  hoot = [&s, f]( float t0, float d, float vib, float a )
    {
        auto fq = [f, d, vib]( float x )
        {
            return f * ( 0.93f + 0.07f * smooth01( x / 0.07f ) ) * ( 1 + vib * std::sin( TwoPi * 11 * x ) ) *
                   ( 1 - 0.04f * smooth01( ( x - d + 0.15f ) / 0.15f ) );
        };
        Env e{ 0.04f, std::max( 0.0f, d - 0.14f ), 0.05f };
        s.voice( t0, d, fq, a, e, Wv::SINE );
        s.voice( t0, d, [fq]( float x ) { return 2 * fq( x ); }, a * 0.12f, e, Wv::SINE );
        s.noise( t0, d, a * 0.05f, e, Flt::BP, f * 2, f * 2, 1, 2 );
    };
    hoot( 0.05f, 0.6f, 0, 1 );
    hoot( 1.45f, 0.14f, 0, 0.6f );
    hoot( 1.66f, 0.16f, 0, 0.7f );
    hoot( 1.9f, 0.85f, 0.015f, 0.9f );
    return s.finishPeak( 0 );
}

/** @brief A gull: a long cry and a few laughing ones. */
std::vector<float> gullCry( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    int   n = 2 + r.below( 4 );
    Fx    s( n * 0.42f + 0.6f, seed );
    float t = 0.05f, base = r.range( 1250, 1500 );
    for ( int k = 0; k < n; ++k )
    {
        float d = k == 0 ? 0.45f : r.range( 0.16f, 0.26f ), f0 = base * r.range( 0.95f, 1.05f );
        s.voice(
            t, d,
            [f0, d]( float x )
            {
                float u = x / d;
                return f0 * ( 1 + 0.5f * std::sin( Pi * std::min( 1.0f, u * 1.6f ) ) - 0.3f * u );
            },
            1.0f, { 0.02f, d * 0.4f, d * 0.25f }, Wv::SAW, 0.25f );
        t += d + r.range( 0.06f, 0.14f );
    }
    s.formants( { { 2200, 2.5f }, { 3500, 3.0f } }, 0.2f );
    s.filter( Flt::HP, 600 );
    s.saturate( 1.6f );
    return s.finishPeak( 0 );
}

/** @brief A foghorn (a diaphone: "bee-ohh"). */
std::vector<float> foghorn( uint32_t seed )
{
    Fx   s( 3.2f, seed );
    auto fq = []( float x ) { return 78.0f * ( x < 2.2f ? 1.0f : 1.0f - 0.18f * smooth01( ( x - 2.2f ) / 0.35f ) ); };
    s.voice( 0.05f, 2.7f, fq, 0.7f, { 0.18f, 2.2f, 0.25f }, Wv::SAW );
    s.voice(
        0.05f, 2.7f, [fq]( float x ) { return fq( x ) * 2.003f; }, 0.35f, { 0.2f, 2.2f, 0.25f }, Wv::SQUARE, 0, 0.3f );
    s.formants( { { 380, 2.0f }, { 760, 3.0f } }, 0.5f );
    s.filter( Flt::LP, 1100 );
    s.saturate( 1.5f );
    return s.finishPeak( 0 );
}

/** @brief A church bell, struck 'strikes' times. */
std::vector<float> churchBell( uint32_t seed, int strikes, float f )
{
    Fx s( strikes * 2.2f + 3.5f, seed );
    for ( int k = 0; k < strikes; ++k ) s.bell( 0.05f + k * 2.2f, f, 1.0f, 1.3f );
    return s.finishPeak( 0 );
}

/** @brief A buoy's bell, rocked by the waves. */
std::vector<float> buoyBell( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    Fx    s( 4.5f, seed );
    float t = 0.05f;
    for ( int k = 0; k < 3; ++k )
    {
        s.bell( t, r.range( 560, 600 ), r.range( 0.5f, 1.0f ), 0.5f );
        t += r.range( 0.6f, 1.3f );
    }
    return s.finishPeak( 0 );
}

/** @brief A car crusher: the hydraulics, then the press comes down. */
std::vector<float> crusher( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    Fx s( 3.5f, seed );
    s.voice(
        0, 1.2f, []( float x ) { return 140 + 160 * smooth01( x / 1.1f ); }, 0.25f, { 0.3f, 0.6f, 0.2f }, Wv::SAW,
        0.2f );
    s.noise( 0, 1.2f, 0.2f, { 0.3f, 0.6f, 0.2f }, Flt::BP, 2500, 3500, 1.1f, 1 );
    float t = 1.15f;
    s.kick( t, 90, 35, 0.03f, 1.2f, { 0.001f, 0.02f, 0.2f } );
    s.modal( t, 0.6f,
             { { 96, 0.9f, 1 },
               { 233, 0.7f, 0.8f },
               { 412, 0.5f, 0.7f },
               { 641, 0.4f, 0.5f },
               { 978, 0.3f, 0.4f },
               { 1495, 0.2f, 0.3f } } );
    s.grains( t, 0.9f, 300, 20, 0.4f, 400, 3500, 0.003f, 0.012f, 1.2f );
    crunch( s, t + 0.05f, 0.6f, 0.4f );
    if ( r.chance( 0.6f ) )
    {
        s.kick( t + 0.65f, 80, 35, 0.03f, 0.6f, { 0.001f, 0.02f, 0.15f } );
        s.modal( t + 0.65f, 0.3f, { { 118, 0.6f, 1 }, { 297, 0.5f, 0.7f }, { 530, 0.35f, 0.5f } } );
    }
    return s.finishPeak( 0 );
}

/** @brief Air let out of a valve (pneumatics, steam). */
std::vector<float> hiss( uint32_t seed, float len, float hz )
{
    Fx s( len + 0.8f, seed );
    s.noise( 0, len + 0.7f, 1.0f, { 0.08f, len, 0.2f }, Flt::HP, hz, hz, 1 );
    s.noise( 0, len + 0.7f, 0.6f, { 0.1f, len, 0.25f }, Flt::BP, hz * 2.4f, hz * 2.0f, len, 1 );
    return s.finishPeak( 0 );
}

/** @brief A pile of scrap settling: rattling, a clank or two. */
std::vector<float> metalSettle( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    Fx s( 2.5f, seed );
    s.grains( 0, 1.5f, 60, 10, 0.5f, 600, 4000, 0.004f, 0.03f, 1.0f );
    for ( int k = 0; k < 2; ++k )
    {
        float t = r.range( 0.0f, 1.0f ), f = r.range( 250, 500 );
        s.modal( t, 0.5f, { { f, 0.4f, 1 }, { f * 2.43f, 0.25f, 0.6f }, { f * 4.1f, 0.15f, 0.4f } } );
    }
    return s.finishPeak( 0 );
}

/** @brief A rusty creak (a stick-slip through resonances); low < 1 for heavier gates and doors. */
std::vector<float> creak( uint32_t seed, float low )
{
    Rand r;
    r.seed( seed );
    float d = r.range( 0.6f, 1.4f );
    Fx    s( d + 0.3f, seed );
    float f0 = r.range( 180, 320 ) * low, f1 = f0 * r.range( 0.7f, 1.4f );
    s.voice(
        0.02f, d, [f0, f1, d]( float x ) { return f0 * std::pow( f1 / f0, x / d ); }, 1.0f,
        { 0.08f, d * 0.5f, d * 0.2f }, Wv::SAW, 0.7f );
    s.formants( { { 850 * low, 6.0f }, { 1650 * low, 8.0f }, { 2900 * low, 10.0f } }, 0.05f );
    s.saturate( 2.0f );
    return s.finishPeak( 0 );
}

/** @brief A chain swinging against a post. */
std::vector<float> chainClink( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    Fx    s( 1.4f, seed );
    float t = 0.02f;
    int   n = 5 + r.below( 4 );
    for ( int k = 0; k < n; ++k )
    {
        float f = r.range( 2000, 4200 );
        s.modal( t, r.range( 0.4f, 1.0f ),
                 { { f, 0.04f, 1 }, { f * 1.6f, 0.025f, 0.5f }, { f * 2.7f, 0.015f, 0.3f } } );
        t += r.range( 0.05f, 0.15f );
    }
    return s.finishPeak( 0 );
}

/** @brief A big pipe struck once or twice. */
std::vector<float> pipeClank( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    Fx    s( 2.5f, seed );
    float f = r.range( 260, 380 );
    s.kick( 0, 140, 70, 0.01f, 0.5f, { 0.0005f, 0.0f, 0.03f } );
    s.modal( 0, 1.0f,
             { { f, 0.5f, 1 }, { f * 2.55f, 0.35f, 0.7f }, { f * 4.7f, 0.25f, 0.5f }, { f * 7.6f, 0.15f, 0.3f } } );
    if ( r.chance( 0.5f ) )
        s.modal( 0.4f, 0.5f, { { f, 0.4f, 1 }, { f * 2.55f, 0.3f, 0.7f }, { f * 4.7f, 0.2f, 0.5f } } );
    return s.finishPeak( 0 );
}

/** @brief The flare stack roaring up for a moment. */
std::vector<float> flareSurge( uint32_t seed )
{
    Fx s( 4.0f, seed );
    s.noise( 0, 3.8f, 1.0f, { 0.8f, 0.6f, 0.7f }, Flt::LP, 300, 900, 1.4f, 0.8f );
    s.noise( 0, 3.8f, 0.6f, { 0.6f, 0.8f, 0.7f }, Flt::BP, 140, 180, 1.4f, 1.0f );
    s.grains( 0.4f, 2.5f, 20, 10, 0.2f, 600, 3000, 0.002f, 0.006f );
    return s.finishPeak( 0 );
}

/** @brief An alarm far off: two-tone beeps. */
std::vector<float> alarm( uint32_t seed )
{
    Fx s( 3.2f, seed );
    for ( int k = 0; k < 6; ++k )
        s.tone( k * 0.45f, 0.3f, k % 2 ? 520.0f : 650.0f, k % 2 ? 520.0f : 650.0f, 0, 0.5f, { 0.01f, 0.25f, 0.03f },
                Wv::SQUARE );
    s.formants( { { 900, 1.5f } }, 0.5f );
    return s.finishPeak( 0 );
}

/** @brief A hawk's scream, high in the sky. */
std::vector<float> hawkCry( uint32_t seed )
{
    Fx s( 1.5f, seed );
    s.voice(
        0.05f, 1.1f, []( float x ) { return 2900 - 800 * smooth01( x / 1.1f ) + 120 * std::sin( TwoPi * 13 * x ); },
        1.0f, { 0.05f, 0.5f, 0.25f }, Wv::SAW, 0.3f );
    s.formants( { { 3000, 2.0f } }, 0.3f );
    s.filter( Flt::LP, 6000 );
    return s.finishPeak( 0 );
}

/** @brief A rattlesnake, rattling. */
std::vector<float> rattle( uint32_t seed )
{
    Fx s( 2.0f, seed );
    s.noise( 0, 1.8f, 1.0f, { 0.15f, 1.0f, 0.3f }, Flt::HP, 4000, 4000, 1 );
    s.trem( 52, 0.9f );
    return s.finishPeak( 0 );
}

/** @brief A gust of wind (with a whistle when 'whistle' > 0). */
std::vector<float> gust( uint32_t seed, float whistle )
{
    Rand r;
    r.seed( seed );
    float d = r.range( 2.5f, 4.0f );
    Fx    s( d + 0.5f, seed );
    s.noise( 0, d, 1.0f, { d * 0.4f, 0.0f, d * 0.25f }, Flt::LP, 250, 900, d * 0.4f );
    s.noise( 0, d, 0.5f, { d * 0.45f, 0.0f, d * 0.2f }, Flt::BP, 400, 1200, d * 0.45f, 1.2f );
    if ( whistle > 0 )
    {
        float f = r.range( 700, 1100 );
        s.noise( 0, d, whistle, { d * 0.45f, 0.1f, d * 0.2f }, Flt::BP, f, f * r.range( 1.2f, 1.6f ), d * 0.5f, 18 );
    }
    return s.finishPeak( 0 );
}

/** @brief An avalanche far off: a long low rumble with snow hissing. */
std::vector<float> avalanche( uint32_t seed )
{
    Fx s( 6.0f, seed );
    s.noise( 0, 5.8f, 1.0f, { 1.2f, 1.5f, 1.2f }, Flt::LP, 160, 90, 4 );
    s.noise( 0.5f, 5.0f, 0.3f, { 1.5f, 1.0f, 1.0f }, Flt::BP, 2500, 1500, 4, 0.6f );
    s.grains( 0.3f, 4.0f, 40, 10, 0.3f, 150, 600, 0.01f, 0.04f );
    return s.finishPeak( 0 );
}

/** @brief A bullfrog: two to four pulsing "rumm"s. */
std::vector<float> bullfrog( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    int   n = 2 + r.below( 3 );
    Fx    s( n * 0.95f + 0.6f, seed );
    float f = r.range( 105, 140 );
    for ( int k = 0; k < n; ++k )
    {
        float t = 0.05f + k * r.range( 0.75f, 0.95f ), d = r.range( 0.45f, 0.7f );
        s.voice(
            t, d, [f, d]( float x ) { return f * ( 1.05f - 0.1f * x / d ); }, 1.0f, { 0.04f, d * 0.5f, d * 0.2f },
            Wv::SQUARE, 0.3f, 0.3f );
    }
    s.trem( 26, 0.7f );
    s.formants( { { 280, 3.0f }, { 650, 4.0f } }, 0.2f );
    s.filter( Flt::LP, 1500 );
    return s.finishPeak( 0 );
}

/** @brief Something heavy dropping into the water. */
std::vector<float> splash( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    Fx s( 1.6f, seed );
    s.kick( 0, 110, 60, 0.03f, 0.6f, { 0.002f, 0.0f, 0.08f } );
    s.noise( 0, 0.8f, 1.0f, { 0.005f, 0.03f, 0.18f }, Flt::LP, 3000, 700, 0.3f );
    for ( int k = 0; k < 6; ++k )
    {
        float t = r.range( 0.1f, 1.2f ), f = r.range( 300, 900 );
        s.voice(
            t, 0.06f, [f]( float x ) { return f * ( 1 + 12 * x ); }, r.range( 0.1f, 0.3f ), { 0.002f, 0.01f, 0.02f },
            Wv::SINE );
    }
    return s.finishPeak( 0 );
}

/** @brief A mosquito flying past an ear. */
std::vector<float> mosquito( uint32_t seed )
{
    Fx s( 3.0f, seed );
    s.voice(
        0, 2.9f, []( float x )
        { return 540 * ( 1.03f - 0.06f * smooth01( x / 2.9f ) ) * ( 1 + 0.006f * std::sin( TwoPi * 9 * x ) ); }, 1.0f,
        { 1.3f, 0.0f, 0.4f }, Wv::SAW );
    s.filter( Flt::HP, 300 );
    return s.finishPeak( 0 );
}

/** @brief A siren far off, wailing. */
std::vector<float> sirenFar( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    float p = r.range( 2.2f, 3.0f ), lo = r.range( 620, 720 );
    Fx    s( 8.5f, seed );
    auto  f = [p, lo]( float t ) { return lo + 420 * ( 0.5f - 0.5f * std::cos( TwoPi * t / p ) ); };
    s.voice( 0, 8.0f, f, 0.6f, { 1.5f, 4.5f, 1.0f }, Wv::SQUARE );
    s.voice( 0, 8.0f, f, 0.5f, { 1.5f, 4.5f, 1.0f }, Wv::SINE );
    s.formants( { { 1400, 1.5f } }, 0.5f );
    return s.finishPeak( 0 );
}

/** @brief A horn honked far off, twice. */
std::vector<float> honkFar( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    Fx    s( 1.4f, seed );
    float p = r.range( 0.85f, 1.15f );
    for ( int k = 0; k < 2; ++k )
        for ( float f : { 392.0f * p, 494.0f * p } )
            s.voice(
                k * 0.45f, k ? 0.5f : 0.25f, [f]( float ) { return f; }, 0.5f, { 0.01f, 0.2f, 0.04f }, Wv::SQUARE,
                0.05f, 0.2f );
    s.formants( { { 1800, 2.5f }, { 900, 2.0f } }, 0.3f );
    return s.finishPeak( 0 );
}

/** @brief A car hissing past on a wet road. */
std::vector<float> carPass( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    float d = r.range( 3.0f, 4.5f ), mid = d * 0.5f;
    Fx    s( d, seed );
    float w = r.range( 0.5f, 0.8f );
    auto  env = [mid, w]( float t )
    {
        float u = ( t - mid ) / w;
        return std::exp( -u * u );
    };
    Svf a, b;
    a.set( 1400, 0.6f );
    b.set( 300, 0.7f );
    double ph = 0;
    float  f0 = r.range( 80, 120 );
    for ( size_t i = 0; i < s.n(); ++i )
    {
        float t = i / (float)Rate, e = env( t );
        a.run( s.m_rng.bi() );
        b.run( s.m_rng.bi() );
        ph += f0 * ( t < mid ? 1.06f : 0.94f + 0.12f * std::exp( -( t - mid ) / 0.15f ) ) / Rate;
        float eng = wave( Wv::SAW, (float)( ph - std::floor( ph ) ), f0 / Rate, 0.5f );
        s.m_s[i] = e * ( a.bpn * 1.2f + b.lp * 2.0f + eng * 0.12f );
    }
    s.filter( Flt::LP, 3000 );
    return s.finishPeak( 0 );
}

/** @brief A steam calliope far off, its pipes wheezing out of tune (our own waltz); part 1 runs out of steam. */
std::vector<float> calliope( uint32_t seed, int part )
{
    Rand r;
    r.seed( seed );
    static const int tunes[2][24] = {
        { 67, 72, 76, 79, 0, 76, 77, 74, 71, 72, 0, 0, 69, 72, 77, 81, 0, 77, 79, 76, 72, 74, 0, 0 },
        { 76, 74, 72, 71, 0, 67, 69, 71, 72, 76, 0, 74, 72, 71, 69, 67, 0, 64, 65, 67, 71, 72, 0, 0 }
    };
    static const int bass[2][8] = { { 48, 43, 43, 48, 41, 41, 43, 43 }, { 48, 43, 41, 48, 41, 48, 43, 48 } };
    float            beat = 0.33f;
    Fx               s( 24 * beat * 1.5f + 1.5f, seed );
    float            t = 0.05f;
    auto             whistle = [&s]( float t0, float d, float f, float sag, float a )
    {
        auto fq = [f, sag, d]( float x ) { return f * ( 1 - sag * x / d ); };
        Env  e{ 0.02f, d * 0.7f, 0.05f };
        s.voice( t0, d, fq, a, e, Wv::SINE );
        s.voice( t0, d, [fq]( float x ) { return 2 * fq( x ); }, a * 0.25f, e, Wv::SINE );
        s.voice( t0, d, [fq]( float x ) { return 3 * fq( x ); }, a * 0.1f, e, Wv::SINE );
        s.noise( t0, d, a * 0.06f, e, Flt::BP, f * 2, f * 2, 1, 3 );
    };
    for ( int i = 0; i < 24; ++i )
    {
        float tired = part == 1 && i > 17 ? ( i - 17 ) : 0.0f;
        float b = beat * r.range( 0.93f, 1.08f ) * ( 1 + tired * 0.09f );
        int   m = tunes[part][i];
        if ( m > 0 && !r.chance( 0.07f ) )
        {
            int len = 1;
            while ( i + len < 24 && tunes[part][i + len] == 0 && len < 3 ) ++len;
            float d = b * len * 0.92f, f = midiHz( (float)m ) * std::pow( 2.0f, r.range( -25.0f, 25.0f ) / 1200.0f );
            whistle( t, d, f, 0.03f * tired, 0.5f );
        }
        if ( i % 3 == 0 )
            whistle( t, b * 0.8f,
                     midiHz( (float)bass[part][i / 3] ) * std::pow( 2.0f, r.range( -20.0f, 20.0f ) / 1200.0f ),
                     0.02f * tired, 0.35f );
        else
            for ( int c : { 4, 7 } )
                whistle( t, b * 0.5f, midiHz( (float)( bass[part][i / 3] + 12 + c ) ), 0.02f * tired, 0.12f );
        t += b;
    }
    return s.finishPeak( 0 );
}

/** @brief The bell of a high striker, rung by nobody. */
std::vector<float> strikerBell( uint32_t seed )
{
    Fx s( 2.0f, seed );
    s.kick( 0, 160, 80, 0.01f, 0.4f, { 0.0005f, 0.0f, 0.04f } );
    s.modal( 0.35f, 1.0f, { { 1320, 0.6f, 1 }, { 3640, 0.3f, 0.5f }, { 6100, 0.15f, 0.3f } } );
    return s.finishPeak( 0 );
}

/** @brief A gun far away on the range, and its echo off the hills. */
std::vector<float> artillery( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    Fx s( 5.0f, seed );
    s.kick( 0, 70, 28, 0.06f, 1.2f, { 0.003f, 0.03f, 0.45f } );
    s.noise( 0, 2.5f, 1.0f, { 0.004f, 0.05f, 0.45f }, Flt::LP, 900, 120, 1.0f );
    s.noise( 0.02f, 4.0f, 0.6f, { 0.05f, 0.2f, 1.2f }, Flt::LP, 160, 80, 2 );
    float e = r.range( 0.6f, 1.2f );
    s.kick( e, 60, 30, 0.08f, 0.45f, { 0.05f, 0.05f, 0.5f } );
    s.noise( e, 2.5f, 0.4f, { 0.05f, 0.1f, 0.6f }, Flt::LP, 400, 100, 1.5f );
    return s.finishPeak( 0 );
}

/** @brief A machine gun far off: a burst. */
std::vector<float> mgFar( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    int n = 6 + r.below( 9 );
    Fx  s( n * 0.085f + 0.5f, seed );
    for ( int k = 0; k < n; ++k )
    {
        float t = k * 0.085f + r.range( 0.0f, 0.006f );
        s.kick( t, 140, 70, 0.01f, 1.0f, { 0.0005f, 0.0f, 0.025f } );
        s.noise( t, 0.06f, 0.8f, { 0.0005f, 0.0f, 0.02f }, Flt::LP, 1200, 600, 0.03f );
    }
    return s.finishPeak( 0 );
}

/** @brief An air-raid siren far off: up, held, down. */
std::vector<float> airRaid( uint32_t seed )
{
    Fx   s( 9.0f, seed );
    auto f = []( float t )
    {
        return t < 3      ? 150 + 370 * smooth01( t / 3 )
               : t < 5.5f ? 520 + 4 * std::sin( TwoPi * 0.7f * t )
                          : 520 - 320 * smooth01( ( t - 5.5f ) / 3 );
    };
    s.voice( 0, 8.8f, f, 0.6f, { 1.0f, 5.0f, 1.2f }, Wv::SAW );
    s.voice( 0, 8.8f, f, 0.4f, { 1.0f, 5.0f, 1.2f }, Wv::SQUARE );
    s.formants( { { 900, 1.5f } }, 0.4f );
    return s.finishPeak( 0 );
}

/** @brief Thunder (near 1 a strike close by: a crack first; 0 far away: only the rolling). */
std::vector<float> thunder( uint32_t seed, float near )
{
    Rand r;
    r.seed( seed );
    float len = 5.5f + 2.0f * near;
    Fx    s( len + 0.5f, seed );
    if ( near > 0.5f )
    {
        s.noise( 0, 0.05f, 1.0f, { 0.0005f, 0.0f, 0.01f }, Flt::HP, 1500, 1500, 1 );
        s.grains( 0, 0.4f, 900, 50, 0.8f, 800, 6000, 0.001f, 0.004f, 2 );
        s.kick( 0.01f, 80, 30, 0.05f, 1.0f, { 0.002f, 0.02f, 0.3f } );
    }
    int   bursts = 4 + r.below( 4 );
    float t = near > 0.5f ? 0.05f : r.range( 0.0f, 0.3f );
    for ( int k = 0; k < bursts; ++k )
    {
        float d = r.range( 0.6f, 1.8f );
        s.noise( t, d * 2.5f, r.range( 0.5f, 1.0f ) * ( k == 0 ? 1.0f : 0.8f ), { d * 0.3f, d * 0.2f, d * 0.7f },
                 Flt::LP, 400 * ( 0.5f + near ), 90, d * 2, 0.8f );
        t += r.range( 0.3f, 1.1f );
    }
    s.noise( 0.1f, len, 0.5f, { 0.6f, 1.0f, len * 0.3f }, Flt::LP, 120, 60, len );
    return s.finishPeak( 0 );
}

/** @brief A tiny clip: a cricket's chirp (three pulses). */
std::vector<float> cricketChirp( float f, int pulses )
{
    Fx s( pulses * 0.022f + 0.02f, 9 );
    for ( int k = 0; k < pulses; ++k )
        s.tone( k * 0.022f, 0.016f, f, f, 0, 1.0f, { 0.002f, 0.008f, 0.003f }, Wv::SINE );
    return s.finishPeak( 0 );
}

/** @brief A tiny clip: a tree frog's peep. */
std::vector<float> peep( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    float f = r.range( 2600, 3300 );
    Fx    s( 0.1f, seed );
    s.tone( 0, 0.08f, f * 0.85f, f, 0.06f, 1.0f, { 0.005f, 0.04f, 0.015f }, Wv::SINE );
    return s.finishPeak( 0 );
}

/** @brief A tiny clip: water sloshing against a quay. */
std::vector<float> slosh( uint32_t seed )
{
    Rand r;
    r.seed( seed );
    float d = r.range( 0.4f, 1.1f );
    Fx    s( d + 0.4f, seed );
    s.noise( 0, d + 0.3f, 1.0f, { d * 0.4f, 0.0f, d * 0.3f }, Flt::LP, r.range( 350, 700 ), r.range( 250, 500 ), d );
    s.noise( d * 0.3f, d, 0.3f, { 0.05f, 0.0f, d * 0.2f }, Flt::BP, 1500, 1200, d, 1 );
    return s.finishPeak( 0 );
}

/** @brief A tiny clip: a canvas flap. */
std::vector<float> flap( uint32_t seed )
{
    Fx s( 0.3f, seed );
    s.noise( 0, 0.25f, 1.0f, { 0.01f, 0.0f, 0.05f }, Flt::LP, 600, 250, 0.1f );
    s.kick( 0, 90, 60, 0.02f, 0.4f, { 0.002f, 0.0f, 0.04f } );
    return s.finishPeak( 0 );
}

/** @brief A loop and, for the ambiences, the events that fall into it at random. */
struct LoopClip
{
    std::vector<float>              data;          ///< the samples (interleaved when stereo)
    int                             ch = 1;        ///< 1 or 2
    size_t                          frames = 0;    ///< its length
    std::vector<std::vector<float>> events;        ///< mono clips played now and then (ambiences)
    float                           gapMin = 6;    ///< seconds between two events, at least
    float                           gapMax = 14;   ///< .. at most
};

/** @brief Makes one of the short loops (tyres, scraping, rain, ...). */
LoopClip makeShortLoop( LoopE l )
{
    LoopClip c;
    bool     stereo = l == LoopE::RAIN || l == LoopE::WIND || l == LoopE::CROWD;
    Bed      b( l == LoopE::CROWD ? 10.0f : 4.0f, l == LoopE::CROWD ? 1.0f : 0.3f, stereo, 100 + (int)l );
    switch ( l )
    {
    case LoopE::SKID:
    {
        // two tyres squealing: tones whose pitch jitters (stick and slip), roughened by narrow noise
        auto               jit = b.walk( 30, 140, 6 ), flut = b.walk( 15, 70, 5 ), slow = b.walk( 1, 6, 3 ),
                           jit2 = b.walk( 40, 160, 6 );
        std::vector<float> tone( b.n(), 0.0f );
        for ( int k = 0; k < 2; ++k )
        {
            double                    f = b.snap( k == 0 ? 1180.0f : 1530.0f ), ph = 0;
            float                     dep = k == 0 ? 0.025f : 0.03f, a = k == 0 ? 0.5f : 0.25f;
            const std::vector<float> &j = k == 0 ? jit : jit2;
            for ( size_t i = 0; i < b.n(); ++i )
            {
                ph += f * ( 1.0 + dep * j[i] ) / Rate;
                float p = (float)( TwoPiD * ( ph - std::floor( ph ) ) );
                float lvl =
                    a * std::max( 0.0f, k == 0 ? 0.75f + 0.25f * flut[i] + 0.15f * slow[i] : 0.6f - 0.4f * flut[i] );
                tone[i] += ( std::sin( p ) + 0.35f * std::sin( 2 * p ) + 0.12f * std::sin( 3 * p ) ) * lvl;
            }
        }
        b.place( tone, 0, 0, 1.0f );
        b.noise( { .amp = 0.25f, .type = Flt::BP, .hz = 1180, .q = 6, .amod = &flut, .adepth = 0.4f } );
        b.noise( { .amp = 0.1f, .type = Flt::BP, .hz = 3000, .q = 1 } );
        c.data = b.finish( -15, -8 );
        break;
    }
    case LoopE::SKID_LOOSE:
    {
        auto m = b.walk( 8, 40, 5 );
        b.noise( { .amp = 0.5f, .type = Flt::BP, .hz = 700, .q = 0.6f, .amod = &m, .adepth = 0.3f } );
        b.noise( { .amp = 0.4f, .type = Flt::LP, .hz = 250, .amod = &m, .adepth = 0.4f } );
        b.grains( 500, 0.25f, 300, 2500, 0.002f, 0.008f, 0 );   // dirt and pebbles
        b.grains( 40, 0.35f, 800, 3000, 0.004f, 0.015f, 0 );
        c.data = b.finish( -17, -9 );
        break;
    }
    case LoopE::SCRAPE:
    {
        auto        m = b.walk( 20, 90, 6 ), s = b.walk( 2, 8, 3 ), j = b.walk( 50, 200, 6 );
        const float modes[][2] = { { 1210, 25 }, { 2340, 30 }, { 3170, 35 }, { 4420, 40 }, { 6050, 30 } };
        for ( const auto &md : modes )
            b.noise( { .amp = 0.25f, .type = Flt::BP, .hz = md[0], .q = md[1], .amod = &m, .adepth = 0.6f } );
        b.noise( { .amp = 0.3f, .type = Flt::HP, .hz = 3000, .amod = &m, .adepth = 0.5f } );
        b.noise( { .amp = 0.35f, .type = Flt::LP, .hz = 180, .amod = &s, .adepth = 0.3f } );
        std::vector<float> sq( b.n(), 0.0f );
        double             f = b.snap( 2350 ), ph = 0;
        for ( size_t i = 0; i < b.n(); ++i )
        {
            ph += f * ( 1.0 + 0.04 * j[i] ) / Rate;
            sq[i] =
                (float)std::sin( TwoPiD * ( ph - std::floor( ph ) ) ) * 0.15f * std::max( 0.0f, 0.4f + 0.6f * m[i] );
        }
        b.place( sq, 0, 0, 1.0f );
        b.grains( 120, 0.3f, 3000, 9000, 0.0015f, 0.005f, 0 );   // sparks
        c.data = b.finish( -16, -8 );
        break;
    }
    case LoopE::GRAVEL:
    {
        auto m = b.walk( 4, 20, 4 );
        b.grains( 1400, 0.22f, 350, 3500, 0.0015f, 0.006f, 0 );
        b.grains( 10, 0.35f, 2500, 5000, 0.01f, 0.03f, 0 );   // a stone pinging off the sills
        b.noise( { .amp = 0.35f, .type = Flt::LP, .hz = 220, .amod = &m, .adepth = 0.3f } );
        b.noise( { .amp = 0.15f, .type = Flt::BP, .hz = 1200, .q = 0.7f, .amod = &m, .adepth = 0.3f } );
        c.data = b.finish( -17, -9 );
        break;
    }
    case LoopE::FLAMER:
    {
        auto t1 = b.walk( 8, 40, 6 ), t2 = b.walk( 20, 80, 5 );
        b.noise( { .amp = 0.8f,
                   .type = Flt::LP,
                   .hz = 700,
                   .q = 0.8f,
                   .amod = &t1,
                   .adepth = 0.45f,
                   .fmod = &t2,
                   .fratio = 1.5f } );
        b.noise( { .amp = 0.5f, .type = Flt::BP, .hz = 250, .q = 0.7f, .amod = &t1, .adepth = 0.3f } );
        b.noise( { .amp = 0.15f, .type = Flt::HP, .hz = 3500, .amod = &t2, .adepth = 0.5f } );
        b.grains( 30, 0.2f, 800, 3000, 0.002f, 0.006f, 0 );
        c.data = b.finish( -15, -8 );
        break;
    }
    case LoopE::FIRE:
    {
        auto m = b.walk( 3, 15, 5 );
        b.noise( { .amp = 0.6f, .type = Flt::LP, .hz = 400, .amod = &m, .adepth = 0.4f } );
        b.noise( { .amp = 0.1f, .type = Flt::HP, .hz = 4000, .amod = &m, .adepth = 0.5f } );
        b.grains( 35, 0.6f, 700, 5000, 0.001f, 0.004f, 0 );   // crackles
        b.grains( 3, 0.9f, 200, 900, 0.008f, 0.02f, 0 );      // pops
        c.data = b.finish( -19, -10 );
        break;
    }
    case LoopE::RAIN:
    {
        auto m = b.walk( 2, 10, 3 );
        b.noise( { .amp = 0.5f, .type = Flt::HP, .hz = 3500, .amod = &m, .adepth = 0.1f } );
        b.noise( { .amp = 0.25f, .type = Flt::BP, .hz = 8000, .q = 0.8f } );
        b.noise( { .amp = 0.15f, .type = Flt::LP, .hz = 800 } );
        b.grains( 250, 0.25f, 2500, 9000, 0.0008f, 0.003f, 0.9f );   // drops
        b.grains( 25, 0.35f, 1100, 2600, 0.004f, 0.012f, 0.6f );     // drops drumming on the roof
        c.data = b.finish( -21, -12 );
        break;
    }
    case LoopE::WIND:
    {
        auto g = b.walk( 1, 4, 3 ), w = b.walk( 2, 9, 4 );
        b.noise( { .amp = 0.7f, .type = Flt::LP, .hz = 280, .amod = &g, .adepth = 0.5f, .width = 0.8f } );
        b.noise( { .amp = 0.25f,
                   .type = Flt::BP,
                   .hz = 850,
                   .q = 9,
                   .amod = &g,
                   .adepth = 0.8f,
                   .fmod = &w,
                   .fratio = 1.4f,
                   .width = 0.6f } );
        b.noise( { .amp = 0.15f, .type = Flt::BP, .hz = 1500, .q = 4, .amod = &w, .adepth = 0.6f } );
        c.data = b.finish( -21, -10 );
        break;
    }
    case LoopE::ROTOR:
    {
        // The news helicopter: four blades slapping the air 17 times a second, the turbine's whine, the tail rotor.
        auto g = b.walk( 1, 5, 3 );
        b.noise( { .amp = 0.9f, .type = Flt::LP, .hz = 520, .amod = &g, .adepth = 0.15f, .pulse = 17.2f, .sharp = 6 } );
        b.noise( { .amp = 0.7f, .type = Flt::LP, .hz = 110, .pulse = 17.2f, .sharp = 3 } );
        b.noise( { .amp = 0.12f, .type = Flt::BP, .hz = 900, .q = 2, .pulse = 61.0f, .sharp = 2 } );
        b.hum( 1180, 0.025f, &g, 0.3f );
        b.hum( 2360, 0.01f );
        c.data = b.finish( -18, -8 );
        break;
    }
    case LoopE::TYRE_SING:
    {
        // Tyres near their limit: a thin, wavering whine, the squeal's first breath.
        auto               jit = b.walk( 20, 90, 5 ), slow = b.walk( 1, 5, 3 );
        std::vector<float> tone( b.n(), 0.0f );
        double             f = b.snap( 1640.0f ), ph = 0;
        for ( size_t i = 0; i < b.n(); ++i )
        {
            ph += f * ( 1.0 + 0.012 * jit[i] ) / Rate;
            float p = (float)( TwoPiD * ( ph - std::floor( ph ) ) );
            tone[i] = ( std::sin( p ) + 0.2f * std::sin( 2 * p ) ) * 0.4f * std::max( 0.0f, 0.8f + 0.2f * slow[i] );
        }
        b.place( tone, 0, 0, 1.0f );
        b.noise( { .amp = 0.18f, .type = Flt::BP, .hz = 1640, .q = 12, .amod = &slow, .adepth = 0.4f } );
        b.noise( { .amp = 0.06f, .type = Flt::BP, .hz = 2600, .q = 3 } );
        c.data = b.finish( -20, -10 );
        break;
    }
    case LoopE::CROWD:
    {
        // A stand full of people: many voices as a roar through the vowels' formants, ebbing and swelling, single
        // shouts and whistles rising out of it, a clap here and there.
        auto g1 = b.walk( 1, 4, 3 ), g2 = b.walk( 2, 8, 4 ), g3 = b.walk( 3, 12, 4 );
        b.noise( { .amp = 0.5f, .type = Flt::BP, .hz = 520, .q = 2.2f, .amod = &g1, .adepth = 0.3f, .width = 0.9f } );
        b.noise( { .amp = 0.35f,
                   .type = Flt::BP,
                   .hz = 1150,
                   .q = 2.5f,
                   .amod = &g2,
                   .adepth = 0.35f,
                   .fmod = &g3,
                   .fratio = 1.15f,
                   .width = 0.9f } );
        b.noise( { .amp = 0.18f, .type = Flt::BP, .hz = 2500, .q = 3.0f, .amod = &g3, .adepth = 0.4f, .width = 0.9f } );
        b.noise( { .amp = 0.25f, .type = Flt::LP, .hz = 260, .amod = &g1, .adepth = 0.2f } );
        Rand &r = b.m_rng;
        for ( int k = 0; k < 22; ++k ) b.place( crowdShout( 900 + k ), r.range( 0.0f, b.seconds() ), r.bi() * 0.8f, r.range( 0.05f, 0.14f ) );
        for ( int k = 0; k < 3; ++k ) b.place( fingerWhistle( 950 + k ), r.range( 0.0f, b.seconds() ), r.bi() * 0.7f, 0.06f );
        b.grains( 6, 0.15f, 1200, 2400, 0.002f, 0.006f, 0.8f );
        c.data = b.finish( -21, -10 );
        break;
    }
    case LoopE::AIRSHIP:
    {
        // The airship's two propellers, a little apart in speed so they beat, over the engines' low drone.
        auto g = b.walk( 1, 4, 3 );
        b.noise( { .amp = 0.6f, .type = Flt::LP, .hz = 260, .amod = &g, .adepth = 0.2f, .pulse = 8.6f, .sharp = 3 } );
        b.noise( { .amp = 0.5f, .type = Flt::LP, .hz = 240, .pulse = 9.1f, .sharp = 3 } );
        b.noise( { .amp = 0.25f, .type = Flt::LP, .hz = 420, .amod = &g, .adepth = 0.3f } );
        b.hum( 46, 0.2f, &g, 0.2f );
        b.hum( 92, 0.12f );
        b.hum( 138, 0.05f );
        c.data = b.finish( -20, -9 );
        break;
    }
    default: break;
    }
    c.ch = stereo ? 2 : 1;
    c.frames = c.data.size() / c.ch;
    return c;
}

/** @brief Makes a venue's ambience: a 20 s bed and the events that fall into it now and then. */
LoopClip makeAmbience( LoopE l )
{
    LoopClip c;
    int      v = (int)l - FirstAmbience;
    Bed      b( 20.0f, 1.5f, true, 300 + v );
    Rand    &r = b.m_rng;
    auto     g1 = b.walk( 1, 5, 3 ), g2 = b.walk( 2, 9, 4 ), g3 = b.walk( 3, 14, 5 );
    auto     ev = [&c]( std::vector<float> clip, float lp, float wet, float size, float peakDb )
    { c.events.push_back( distant( std::move( clip ), lp, wet, size, peakDb ) ); };
    uint32_t s = 1000 + v * 100;
    switch ( l )
    {
    case LoopE::AMB_JUNKYARD:
    {
        b.noise( { .amp = 0.7f,
                   .type = Flt::LP,
                   .hz = 90,
                   .amod = &g1,
                   .adepth = 0.3f,
                   .width = 0.7f } );   // the yard's drone
        b.noise( { .amp = 0.35f,
                   .type = Flt::BP,
                   .hz = 500,
                   .q = 0.6f,
                   .amod = &g2,
                   .adepth = 0.6f,
                   .fmod = &g3,
                   .fratio = 1.5f } );
        b.hum( 60, 0.004f, &g2, 0.5f );   // machinery somewhere
        b.hum( 120, 0.003f, &g1, 0.6f );
        b.grains( 0.8f, 0.05f, 1500, 4500, 0.02f, 0.08f, 0.9f );   // metal ticking as it cools
        for ( int k = 0; k < 3; ++k ) ev( crusher( s + k ), 2500, 0.45f, 1.0f, -11 );
        for ( int k = 0; k < 3; ++k ) ev( crowCall( s + 10 + k ), 5000, 0.3f, 0.8f, -15 );
        ev( metalSettle( s + 20 ), 3500, 0.35f, 0.9f, -16 );
        ev( hiss( s + 21, 0.8f, 2500 ), 4000, 0.4f, 0.9f, -19 );
        c.gapMin = 4;
        c.gapMax = 11;
        break;
    }
    case LoopE::AMB_CEMETERY:
    {
        b.noise(
            { .amp = 0.35f, .type = Flt::LP, .hz = 350, .amod = &g1, .adepth = 0.7f, .width = 0.8f } );   // soft wind
        b.noise( { .amp = 0.12f,
                   .type = Flt::BP,
                   .hz = 900,
                   .q = 3,
                   .amod = &g1,
                   .adepth = 0.8f,
                   .fmod = &g3,
                   .fratio = 1.5f } );                                                           // in the cypresses
        b.noise( { .amp = 0.08f, .type = Flt::HP, .hz = 3000, .amod = &g2, .adepth = 0.8f } );   // leaves
        auto chirp = cricketChirp( 4600, 3 );
        for ( float t = 0.2f; t < 19.8f; t += r.range( 0.4f, 0.55f ) )
            b.place( chirp, t, -0.5f, 0.02f * r.range( 0.7f, 1.0f ) );
        auto chirp2 = cricketChirp( 4100, 4 );
        for ( float t = 0.5f; t < 19.5f; t += r.range( 0.7f, 1.1f ) )
            b.place( chirp2, t, 0.7f, 0.01f * r.range( 0.6f, 1.0f ) );
        ev( owlCall( s ), 4000, 0.3f, 1.0f, -15 );
        ev( owlCall( s + 1 ), 4000, 0.3f, 1.0f, -16 );
        ev( churchBell( s + 2, 1, 233 ), 3000, 0.5f, 1.2f, -14 );
        ev( churchBell( s + 3, 3, 220 ), 3000, 0.5f, 1.2f, -15 );
        ev( crowCall( s + 4 ), 4500, 0.35f, 1.0f, -17 );
        ev( creak( s + 5, 0.6f ), 4000, 0.3f, 0.8f, -18 );
        c.gapMin = 6;
        c.gapMax = 14;
        break;
    }
    case LoopE::AMB_DOCKS:
    {
        b.noise( { .amp = 0.4f,
                   .type = Flt::LP,
                   .hz = 70,
                   .amod = &g1,
                   .adepth = 0.3f,
                   .width = 0.6f } );                                                           // the harbour hum
        b.noise( { .amp = 0.1f, .type = Flt::HP, .hz = 5000, .amod = &g2, .adepth = 0.2f } );   // drizzle
        for ( float t = 0.0f; t < 20.0f; t += r.range( 0.6f, 1.6f ) )
            b.place( slosh( r.next() ), t, r.range( -0.8f, 0.8f ), r.range( 0.06f, 0.14f ) );
        b.grains( 1.2f, 0.03f, 500, 1500, 0.01f, 0.03f, 0.8f );   // plips
        for ( int k = 0; k < 3; ++k ) ev( gullCry( s + k ), 6000, 0.3f, 0.9f, -14 );
        ev( foghorn( s + 10 ), 900, 0.55f, 1.4f, -11 );
        ev( foghorn( s + 11 ), 700, 0.6f, 1.4f, -13 );
        ev( buoyBell( s + 12 ), 3500, 0.45f, 1.1f, -17 );
        ev( creak( s + 13, 0.5f ), 3000, 0.3f, 0.8f, -19 );   // a hawser
        c.gapMin = 4;
        c.gapMax = 10;
        break;
    }
    case LoopE::AMB_DESERT:
    {
        b.noise(
            { .amp = 0.3f, .type = Flt::LP, .hz = 1500, .amod = &g1, .adepth = 0.5f, .width = 0.9f } );   // hot wind
        b.noise( { .amp = 0.3f, .type = Flt::BP, .hz = 300, .q = 0.7f, .amod = &g1, .adepth = 0.5f } );
        b.noise( { .amp = 0.06f, .type = Flt::HP, .hz = 5000, .amod = &g1, .adepth = 0.8f } );   // sand
        b.noise( { .amp = 0.09f,
                   .type = Flt::BP,
                   .hz = 5200,
                   .q = 5,
                   .amod = &g2,
                   .adepth = 0.9f,
                   .width = 0.3f,
                   .pulse = 190,
                   .sharp = 4 } );   // cicadas
        b.noise( { .amp = 0.06f,
                   .type = Flt::BP,
                   .hz = 6100,
                   .q = 6,
                   .amod = &g3,
                   .adepth = 0.9f,
                   .width = 0.3f,
                   .pulse = 230,
                   .sharp = 5 } );
        ev( hawkCry( s ), 5000, 0.4f, 1.2f, -17 );
        ev( rattle( s + 1 ), 7000, 0.2f, 0.7f, -20 );
        ev( gust( s + 2, 0 ), 3000, 0.2f, 0.8f, -15 );
        ev( gust( s + 3, 0 ), 3000, 0.2f, 0.8f, -15 );
        c.gapMin = 7;
        c.gapMax = 16;
        break;
    }
    case LoopE::AMB_REFINERY:
    {
        b.hum( 100, 0.02f, &g2, 0.2f );   // transformers
        b.hum( 200, 0.01f, &g3, 0.3f );
        b.hum( 300, 0.006f, &g1, 0.4f );
        b.noise(
            { .amp = 0.6f, .type = Flt::LP, .hz = 350, .amod = &g3, .adepth = 0.35f, .width = 0.7f } );   // the flare
        b.noise( { .amp = 0.4f, .type = Flt::BP, .hz = 120, .q = 1, .amod = &g2, .adepth = 0.3f } );
        b.noise( { .amp = 0.05f, .type = Flt::HP, .hz = 6000 } );
        std::vector<float> thump;
        {
            Fx f( 0.3f, 7 );
            f.kick( 0, 70, 45, 0.02f, 1.0f, { 0.005f, 0.0f, 0.06f } );
            thump = f.finishPeak( 0 );
        }
        for ( int k = 0; k < 22; ++k ) b.place( thump, k * 20.0f / 22, 0.3f, 0.04f );   // a pump
        ev( hiss( s, 1.8f, 2500 ), 6000, 0.35f, 1.0f, -15 );
        ev( hiss( s + 1, 1.0f, 3200 ), 6000, 0.35f, 1.0f, -16 );
        ev( pipeClank( s + 2 ), 4000, 0.45f, 1.1f, -15 );
        ev( pipeClank( s + 3 ), 4000, 0.45f, 1.1f, -16 );
        ev( flareSurge( s + 4 ), 2500, 0.3f, 1.0f, -13 );
        ev( alarm( s + 5 ), 2500, 0.55f, 1.3f, -19 );
        c.gapMin = 5;
        c.gapMax = 12;
        break;
    }
    case LoopE::AMB_SWAMP:
    {
        b.noise(
            { .amp = 0.25f, .type = Flt::LP, .hz = 250, .amod = &g1, .adepth = 0.3f, .width = 0.8f } );   // heavy air
        b.noise( { .amp = 0.04f,
                   .type = Flt::HP,
                   .hz = 7000,
                   .amod = &g2,
                   .adepth = 0.6f,
                   .width = 0.5f,
                   .pulse = 42,
                   .sharp = 3 } );   // katydids
        for ( float f : { 4300.0f, 4700.0f, 5100.0f } )
        {
            auto  chirp = cricketChirp( f, 3 );
            float pan = r.range( -0.9f, 0.9f ), gap = r.range( 0.35f, 0.6f );
            for ( float t = r.range( 0.0f, 0.3f ); t < 19.8f; t += gap * r.range( 0.9f, 1.1f ) )
                b.place( chirp, t, pan, 0.015f );
        }
        for ( int k = 0; k < 6; ++k )   // tree frogs
        {
            auto p = peep( r.next() );
            for ( int j = 0; j < 10; ++j )
                b.place( p, r.range( 0.0f, 20.0f ), r.range( -1.0f, 1.0f ), r.range( 0.02f, 0.05f ) );
        }
        b.grains( 1.0f, 0.03f, 300, 900, 0.02f, 0.05f, 0.9f );   // bubbles
        b.hum( 550, 0.002f, &g3, 0.9f );                         // a mosquito somewhere
        for ( int k = 0; k < 3; ++k ) ev( bullfrog( s + k ), 3000, 0.3f, 0.9f, -13 );
        ev( splash( s + 10 ), 4000, 0.3f, 0.9f, -16 );
        ev( mosquito( s + 11 ), 8000, 0.05f, 0.5f, -22 );
        c.gapMin = 3;
        c.gapMax = 8;
        break;
    }
    case LoopE::AMB_DOWNTOWN:
    {
        b.noise( { .amp = 0.6f, .type = Flt::LP, .hz = 180, .amod = &g2, .adepth = 0.4f, .width = 0.7f } );   // traffic
        b.noise( { .amp = 0.3f, .type = Flt::HP, .hz = 3500 } );                                              // rain
        b.noise( { .amp = 0.12f, .type = Flt::BP, .hz = 9000, .q = 0.8f } );
        b.grains( 6, 0.06f, 1500, 5000, 0.004f, 0.015f, 0.9f );   // drops off the awnings
        b.hum( 120, 0.0015f, &g1, 0.8f );                         // neon
        for ( int k = 0; k < 3; ++k )
        {
            float p = r.chance( 0.5f ) ? 1.0f : -1.0f;
            b.placeMoving( carPass( r.next() ), 2.0f + k * 6.5f + r.range( -1.0f, 1.0f ), -0.9f * p, 0.9f * p, 0.12f );
        }
        ev( sirenFar( s ), 2500, 0.55f, 1.4f, -16 );
        ev( sirenFar( s + 1 ), 2000, 0.6f, 1.4f, -18 );
        ev( honkFar( s + 2 ), 3000, 0.45f, 1.1f, -18 );
        ev( carPass( s + 3 ), 5000, 0.2f, 0.8f, -15 );
        ev( carPass( s + 4 ), 5000, 0.2f, 0.8f, -16 );
        c.gapMin = 4;
        c.gapMax = 10;
        break;
    }
    case LoopE::AMB_PASS:
    {
        b.noise(
            { .amp = 0.6f, .type = Flt::LP, .hz = 250, .amod = &g1, .adepth = 0.7f, .width = 0.8f } );   // cold wind
        b.noise( { .amp = 0.18f,
                   .type = Flt::BP,
                   .hz = 900,
                   .q = 12,
                   .amod = &g1,
                   .adepth = 0.9f,
                   .fmod = &g3,
                   .fratio = 1.6f,
                   .width = 0.6f } );
        b.noise( { .amp = 0.08f,
                   .type = Flt::BP,
                   .hz = 1500,
                   .q = 18,
                   .amod = &g2,
                   .adepth = 0.9f,
                   .fmod = &g1,
                   .fratio = 1.3f,
                   .width = 0.6f } );
        b.noise( { .amp = 0.05f, .type = Flt::HP, .hz = 6000, .amod = &g1, .adepth = 0.6f } );   // snow
        for ( int k = 0; k < 3; ++k ) ev( gust( s + k, 0.4f ), 4000, 0.25f, 0.9f, -13 );
        ev( creak( s + 10, 0.8f ), 4000, 0.3f, 0.8f, -19 );   // the avalanche fence
        ev( avalanche( s + 11 ), 1500, 0.5f, 1.4f, -15 );
        c.gapMin = 6;
        c.gapMax = 12;
        break;
    }
    case LoopE::AMB_FAIRGROUND:
    {
        b.noise( { .amp = 0.3f, .type = Flt::LP, .hz = 400, .amod = &g1, .adepth = 0.6f, .width = 0.8f } );   // wind
        for ( int k = 0; k < 2; ++k )   // canvas flapping
        {
            auto  f = flap( r.next() );
            float pan = r.range( -0.7f, 0.7f );
            for ( float t = r.range( 0.0f, 2.0f ); t < 19.5f; t += r.range( 0.25f, 2.5f ) )
                b.place( f, t, pan, r.range( 0.02f, 0.06f ) );
        }
        b.hum( 100, 0.002f, &g3, 1.0f );   // a broken bulb buzzing
        b.place( distant( creak( s + 50, 1.0f ), 3000, 0.4f, 1.0f, 0 ), 7.0f, -0.6f, 0.05f );
        ev( calliope( s, 0 ), 2200, 0.55f, 1.3f, -13 );
        ev( calliope( s + 1, 1 ), 2200, 0.55f, 1.3f, -13 );
        for ( int k = 0; k < 3; ++k ) ev( creak( s + 10 + k, 1.0f ), 4000, 0.35f, 0.9f, -17 );
        ev( chainClink( s + 20 ), 6000, 0.3f, 0.8f, -19 );
        ev( strikerBell( s + 21 ), 5000, 0.45f, 1.1f, -18 );
        c.gapMin = 5;
        c.gapMax = 12;
        break;
    }
    case LoopE::AMB_PROVING:
    {
        b.noise(
            { .amp = 0.35f, .type = Flt::LP, .hz = 700, .amod = &g1, .adepth = 0.5f, .width = 0.9f } );   // dry wind
        b.noise( { .amp = 0.2f, .type = Flt::BP, .hz = 250, .q = 0.8f, .amod = &g2, .adepth = 0.4f } );
        b.noise( { .amp = 0.04f, .type = Flt::HP, .hz = 5000, .amod = &g1, .adepth = 0.7f } );   // dust
        b.noise( { .amp = 0.3f, .type = Flt::LP, .hz = 60, .amod = &g3, .adepth = 0.4f } );      // the range far off
        for ( int k = 0; k < 3; ++k ) ev( artillery( s + k ), 1200, 0.6f, 1.5f, -11 );
        ev( mgFar( s + 10 ), 1500, 0.5f, 1.3f, -17 );
        ev( mgFar( s + 11 ), 1500, 0.5f, 1.3f, -18 );
        ev( airRaid( s + 12 ), 1800, 0.6f, 1.5f, -18 );
        c.gapMin = 4;
        c.gapMax = 10;
        break;
    }
    case LoopE::AMB_ESTATE:
    {
        b.noise(
            { .amp = 0.6f, .type = Flt::LP, .hz = 300, .amod = &g1, .adepth = 0.8f, .width = 0.8f } );   // the storm
        b.noise( { .amp = 0.15f,
                   .type = Flt::BP,
                   .hz = 700,
                   .q = 8,
                   .amod = &g1,
                   .adepth = 0.9f,
                   .fmod = &g3,
                   .fratio = 1.5f,
                   .width = 0.6f } );
        b.noise( { .amp = 0.4f, .type = Flt::HP, .hz = 2500, .amod = &g2, .adepth = 0.2f } );   // heavy rain
        b.noise( { .amp = 0.2f, .type = Flt::BP, .hz = 7000, .q = 0.8f } );
        b.noise( { .amp = 0.15f, .type = Flt::LP, .hz = 900 } );
        b.grains( 8, 0.05f, 1200, 5000, 0.004f, 0.012f, 0.9f );   // drops off the gutters
        ev( thunder( s, 1.0f ), 6000, 0.35f, 1.3f, -9 );
        ev( thunder( s + 1, 0.4f ), 2500, 0.45f, 1.4f, -12 );
        ev( thunder( s + 2, 0.0f ), 1500, 0.5f, 1.5f, -14 );
        ev( creak( s + 3, 0.5f ), 3500, 0.35f, 0.9f, -18 );          // the gate
        ev( churchBell( s + 4, 2, 196 ), 2500, 0.55f, 1.3f, -16 );   // the private chapel
        ev( crowCall( s + 5 ), 4000, 0.35f, 1.0f, -18 );
        c.gapMin = 5;
        c.gapMax = 12;
        break;
    }
    default: break;
    }
    c.data = b.finish( -28, -14 );
    c.ch = 2;
    c.frames = c.data.size() / 2;
    return c;
}

/** @brief Makes a loop. */
LoopClip makeLoop( LoopE l )
{
    return (int)l >= FirstAmbience ? makeAmbience( l ) : makeShortLoop( l );
}

// ---------------------------------------------------------------------------------------------------------------
// the engines
// ---------------------------------------------------------------------------------------------------------------

/** @brief How one kind of engine is built. */
struct EngineSpec
{
    int   cyl = 4;                        ///< firings per cycle
    float firing[12] = {};                ///< where each fires in the cycle (0..1)
    int   bank[12] = {};                  ///< the exhaust it blows into (0, 1)
    float idle = 850, redline = 7000;     ///< revolutions a minute
    float cycleRevs = 2;                  ///< crank turns a cycle (2 four-strokes; 3 the rotary: three faces a rotor)
    float width = 0.11f;                  ///< the exhaust pulse as a share of the cycle
    float riseMs = 0.6f;                  ///< its front
    float var = 0.05f;                    ///< fixed differences between the cylinders (lumpiness at idle)
    float jitter = 0.05f;                 ///< random differences from firing to firing
    float miss = 0;                       ///< chance of a weak firing at idle
    float pipe[2] = { 1.8f, 1.8f };       ///< exhaust lengths per bank (m)
    float pipeFb = 0.45f;                 ///< how strongly the pipe's end reflects
    float lowCut = 600, highCut = 4000;   ///< the muffler: closed (idle, coasting) .. open (limiter, full throttle)
    float formant = 400;                  ///< a body resonance (Hz)
    float formantGain = 0.3f;             ///< .. and how much of it
    float rasp = 0.25f, raspHz = 2000;    ///< combustion noise riding on the pulses
    float intake = 0.3f;                  ///< induction noise with the throttle open
    float howl = 0, howlHz = 900;         ///< the intake resonance rung by the pulses (V12 howl)
    float drive = 1.5f;                   ///< saturation
    float whine = 0.04f;                  ///< gear whine
    float turbo = 0;                      ///< turbo whistle and blow-off
    float flutter = 0;                    ///< the blow-off flutters (compressor surge)
    float clatter = 0;                    ///< diesel knock
    float crackle = 0;                    ///< pops on the overrun and at the limiter
    float tick = 0;                       ///< valve tick
    float tin = 0;                        ///< a tin-can rattle
    float gain = 1;                       ///< the level of the voice
};

/** @brief The engines. */
EngineSpec makeSpec( EngineE k )
{
    EngineSpec s;
    auto       even = [&s]( int n )
    {
        s.cyl = n;
        for ( int c = 0; c < n; ++c ) s.firing[c] = c / (float)n;
    };
    switch ( k )
    {
    case EngineE::TIN:   // a tiny three-cylinder: buzzy, uneven, rattling
        even( 3 );
        s.idle = 950, s.redline = 6800, s.width = 0.13f, s.riseMs = 0.5f, s.var = 0.14f, s.jitter = 0.1f,
        s.miss = 0.01f;
        s.pipe[0] = s.pipe[1] = 1.3f, s.lowCut = 700, s.highCut = 3800, s.formant = 1100, s.formantGain = 0.4f;
        s.rasp = 0.35f, s.raspHz = 2600, s.intake = 0.25f, s.drive = 1.8f, s.whine = 0.05f, s.tick = 0.25f,
        s.tin = 0.5f;
        s.gain = 0.82f;
        break;
    case EngineE::I4:   // an ordinary four
        even( 4 );
        s.idle = 850, s.redline = 7000, s.width = 0.11f, s.riseMs = 0.5f, s.var = 0.05f, s.jitter = 0.06f;
        s.pipe[0] = s.pipe[1] = 1.8f, s.lowCut = 600, s.highCut = 4200, s.formant = 480, s.formantGain = 0.3f;
        s.rasp = 0.25f, s.raspHz = 2200, s.intake = 0.35f, s.drive = 1.5f, s.whine = 0.04f, s.crackle = 0.25f;
        s.gain = 0.68f;
        break;
    case EngineE::BOXER:   // a rally boxer: unequal headers (the burble), turbo, anti-lag bangs
    {
        s.cyl = 4;
        const float f[4] = { 0.0f, 0.285f, 0.5f, 0.785f };
        for ( int c = 0; c < 4; ++c ) s.firing[c] = f[c], s.bank[c] = c & 1;
        s.idle = 850, s.redline = 7200, s.width = 0.12f, s.riseMs = 0.7f, s.var = 0.08f, s.jitter = 0.08f;
        s.pipe[0] = 1.7f, s.pipe[1] = 2.1f, s.pipeFb = 0.5f, s.lowCut = 450, s.highCut = 3200, s.formant = 320;
        s.formantGain = 0.35f, s.rasp = 0.3f, s.raspHz = 1800, s.intake = 0.3f, s.drive = 2.0f, s.whine = 0.04f;
        s.turbo = 1.0f, s.flutter = 1.0f, s.crackle = 0.6f, s.gain = 0.72f;
        break;
    }
    case EngineE::V6:
        even( 6 );
        for ( int c = 0; c < 6; ++c ) s.bank[c] = c & 1;
        s.idle = 800, s.redline = 7200, s.width = 0.1f, s.riseMs = 0.5f, s.var = 0.07f, s.jitter = 0.06f;
        s.pipe[0] = 1.9f, s.pipe[1] = 2.0f, s.pipeFb = 0.5f, s.lowCut = 550, s.highCut = 4000, s.formant = 420;
        s.rasp = 0.3f, s.raspHz = 2000, s.intake = 0.4f, s.drive = 1.7f, s.whine = 0.04f, s.crackle = 0.35f,
        s.gain = 0.63f;
        break;
    case EngineE::V8:   // a cross-plane V8: each bank's pulses uneven, a lumpy cam
    {
        even( 8 );
        const int banks[8] = { 0, 1, 0, 0, 1, 1, 1, 0 };   // firing order 1-8-4-3-6-5-7-2, cylinders 1-4 left
        for ( int c = 0; c < 8; ++c ) s.bank[c] = banks[c];
        s.idle = 750, s.redline = 6600, s.width = 0.11f, s.riseMs = 0.9f, s.var = 0.2f, s.jitter = 0.14f,
        s.miss = 0.03f;
        s.pipe[0] = 2.1f, s.pipe[1] = 2.5f, s.pipeFb = 0.55f, s.lowCut = 380, s.highCut = 2600, s.formant = 260;
        s.formantGain = 0.35f, s.rasp = 0.35f, s.raspHz = 1600, s.intake = 0.3f, s.drive = 2.4f, s.whine = 0.03f;
        s.crackle = 0.5f, s.gain = 0.56f;
        break;
    }
    case EngineE::V12:   // smooth and screaming, the intake howling
        even( 12 );
        for ( int c = 0; c < 12; ++c ) s.bank[c] = c & 1;
        s.idle = 1000, s.redline = 9200, s.width = 0.09f, s.riseMs = 0.35f, s.var = 0.03f, s.jitter = 0.03f;
        s.pipe[0] = 1.6f, s.pipe[1] = 1.65f, s.lowCut = 900, s.highCut = 6500, s.formant = 700, s.formantGain = 0.3f;
        s.rasp = 0.25f, s.raspHz = 2800, s.intake = 0.4f, s.howl = 0.6f, s.howlHz = 1100, s.drive = 1.6f,
        s.whine = 0.05f;
        s.crackle = 0.2f, s.gain = 0.46f;
        break;
    case EngineE::DIESEL:   // a truck's straight six: low revs, clatter, a gentle turbo
        even( 6 );
        s.idle = 650, s.redline = 4300, s.width = 0.12f, s.riseMs = 1.0f, s.var = 0.06f, s.jitter = 0.05f;
        s.pipe[0] = s.pipe[1] = 2.8f, s.pipeFb = 0.5f, s.lowCut = 350, s.highCut = 1800, s.formant = 220,
        s.formantGain = 0.35f;
        s.rasp = 0.2f, s.raspHz = 1400, s.intake = 0.2f, s.drive = 1.6f, s.whine = 0.03f, s.turbo = 0.45f,
        s.clatter = 1.0f;
        s.gain = 0.62f;
        break;
    case EngineE::ROTARY:   // two rotors, three faces each: sharp, buzzing, bridge-port brap at idle
        even( 6 );
        s.cycleRevs = 3, s.idle = 1000, s.redline = 9000, s.width = 0.16f, s.riseMs = 0.35f, s.var = 0.1f,
        s.jitter = 0.12f;
        s.miss = 0.02f, s.pipe[0] = s.pipe[1] = 1.5f, s.pipeFb = 0.55f, s.lowCut = 800, s.highCut = 5000,
        s.formant = 650;
        s.formantGain = 0.3f, s.rasp = 0.5f, s.raspHz = 2400, s.intake = 0.2f, s.drive = 3.0f, s.whine = 0.04f;
        s.crackle = 0.7f, s.gain = 0.72f;
        break;
    default: break;   // the turbine and the electric are made otherwise
    }
    if ( k == EngineE::TURBINE ) s.gain = 1.36f;
    if ( k == EngineE::ELECTRIC ) s.gain = 1.15f;
    return s;
}

/** @return the spec of an engine kind */
const EngineSpec &specOf( EngineE k )
{
    static const std::vector<EngineSpec> table = []
    {
        std::vector<EngineSpec> t;
        for ( int i = 0; i < EngineCount; ++i ) t.push_back( makeSpec( (EngineE)i ) );
        return t;
    }();
    return table[std::clamp( (int)k, 0, EngineCount - 1 )];
}

const float KRpm = coef( 0.035f );                             ///< how quickly the revs follow the game
const float KLoad = coef( 0.05f );                             ///< .. the throttle
const float KCut = coef( 0.004f );                             ///< the limiter's cut
const float KSpool = coef( 0.7f );                             ///< a turbo spooling up
const float KUnspool = coef( 0.25f );                          ///< .. and down
const float KSpoolT = coef( 0.5f );                            ///< a turbine spooling up
const float KUnspoolT = coef( 0.9f );                          ///< .. and down
const float KGlide = coef( 0.012f );                           ///< the inverter's steps
const float KKnock = std::exp( -1.0f / ( 0.0025f * Rate ) );   ///< a diesel knock dying away
const float KTick = std::exp( -1.0f / ( 0.0012f * Rate ) );    ///< a valve tick dying away

constexpr int PipeLen = 1024;   ///< exhaust delay lines (up to 3.9 m)

/** @brief One engine voice: made sample by sample, its revs and throttle following the game smoothly. */
class EngineVoice
{
public:
    /** @brief Starts afresh as that kind (each seed a slightly different engine). */
    void start( EngineE kind, uint32_t seed, float rpm, float load )
    {
        m_kind = kind;
        m_s = &specOf( kind );
        m_rng.seed( seed );
        m_rpm = clampf( rpm, 0, 1 );
        m_load = clampf( load, 0, 1 );
        m_detune = 1.0f + 0.025f * m_rng.bi();
        m_phase = m_rng.unit();
        for ( int c = 0; c < 12; ++c )
        {
            m_cylVar[c] = m_rng.bi();
            float lp = (float)( m_phase - m_s->firing[c] );
            m_local[c] = lp < 0 ? lp + 1.0f : lp;
            m_amp[c] = 0.5f;
        }
        for ( int b = 0; b < 2; ++b )
        {
            std::fill( m_pipe[b], m_pipe[b] + PipeLen, 0.0f );
            m_pipeLp[b] = 0;
            m_pipeLen[b] = std::clamp( (int)( 2.0f * m_s->pipe[b] * ( 1.0f + 0.04f * m_rng.bi() ) / 343.0f * Rate ), 8,
                                       PipeLen - 1 );
        }
        m_pipeI = 0;
        m_muffler = m_formant = m_rasp = m_intake = m_howl = m_knockA = m_knockB = m_tickF = m_turboF = m_bovF =
            m_tinA = m_tinB = Svf();
        m_formant.set( m_s->formant, 1.4f );
        m_rasp.set( m_s->raspHz, 0.9f );
        m_howl.set( m_s->howlHz, 3.0f );
        m_knockA.set( 2900, 4.0f );
        m_knockB.set( 4700, 5.0f );
        m_tickF.set( 5200, 6.0f );
        m_tinA.set( 1350, 7.0f );
        m_tinB.set( 2950, 9.0f );
        m_turboF.set( 3200, 1.5f );
        m_bovF.set( 3500, 0.8f );
        m_cut = m_limPh = 0;
        m_boost = m_turboPh = m_bov = 0;
        m_bovT = m_popT = 1e9f;
        m_lastLoad = m_load;
        m_knock = m_tick = 0;
        m_gearPh = 0;
        m_dcX = m_dcY = 0;
        m_ctl = 0;
        m_spool = m_rpm;
        m_fsw = 1047;
        for ( double &p : m_ph ) p = m_rng.unit();
    }

    /** @return the next sample, the revs (0 idle .. 1 limiter) and throttle (0..1) heading for these */
    float tick( float rpmGoal, float loadGoal )
    {
        if ( !m_s ) return 0.0f;
        rpmGoal = clampf( rpmGoal, 0.0f, 1.0f );
        loadGoal = clampf( loadGoal, 0.0f, 1.0f );
        m_rpm += ( rpmGoal - m_rpm ) * KRpm;
        m_load += ( loadGoal - m_load ) * KLoad;
        float y = m_kind == EngineE::TURBINE    ? turbine()
                  : m_kind == EngineE::ELECTRIC ? electric()
                                                : piston( loadGoal );
        // DC out (~25 Hz): the pulses push air one way
        float o = y - m_dcX + 0.9965f * m_dcY;
        m_dcX = y;
        m_dcY = o;
        return o * m_s->gain;
    }

private:
    /** @brief A piston (or rotary) engine. */
    float piston( float loadGoal )
    {
        const EngineSpec &s = *m_s;

        // the rev limiter: held at the limit with the throttle open, the ignition cuts in and out
        bool limit = m_rpm > 0.975f && m_load > 0.5f;
        m_limPh += 12.5f / Rate;
        if ( m_limPh >= 1.0f ) m_limPh -= 1.0f;
        m_cut += ( ( limit && m_limPh < 0.45f ? 1.0f : 0.0f ) - m_cut ) * KCut;
        float rpm = clampf( m_rpm - 0.035f * m_cut, 0.0f, 1.0f );

        float revs = ( s.idle + rpm * ( s.redline - s.idle ) ) * m_detune;
        float cycHz = revs / ( 60.0f * s.cycleRevs );
        m_phase += cycHz / Rate;
        if ( m_phase >= 1.0 ) m_phase -= 1.0;

        // the exhaust pulses: one per firing, its front a fixed time, its body a share of the cycle
        float rise = clampf( s.riseMs * 0.001f * cycHz / s.width, 0.04f, 0.45f );
        float fuel = ( 0.3f + 0.7f * m_load ) * ( 1.0f - 0.94f * m_cut );
        float var = s.var * ( 1.0f - 0.5f * rpm ), jit = s.jitter * ( 1.0f - 0.75f * rpm );
        float ex[2] = { 0, 0 }, env = 0;
        int   fired = -1;
        for ( int c = 0; c < s.cyl; ++c )
        {
            float lp = (float)( m_phase - s.firing[c] );
            if ( lp < 0 ) lp += 1.0f;
            if ( lp < m_local[c] )
            {
                float a = fuel * ( 1.0f + var * m_cylVar[c] ) * ( 1.0f + jit * m_rng.bi() );
                if ( m_rng.unit() < s.miss * ( 1.0f - rpm ) ) a *= 0.2f;
                m_amp[c] = std::max( 0.0f, a );
                fired = s.bank[c];
            }
            m_local[c] = lp;
            if ( lp < s.width )
            {
                float u = lp / s.width, v;
                if ( u < rise )
                {
                    float x = std::sin( 0.5f * Pi * u / rise );
                    v = x * x;
                }
                else
                {
                    float w = ( u - rise ) / ( 1.0f - rise ), o = 1.0f - w;
                    v = o * o * std::exp( -2.5f * w );
                }
                ex[s.bank[c]] += m_amp[c] * v;
                env += m_amp[c] * v;
            }
        }
        if ( fired >= 0 )
        {
            // overrun: lifting off at high revs, unburnt fuel bangs in the hot exhaust (and at the limiter)
            float over = rpm > 0.3f && m_load < 0.2f ? ( rpm - 0.3f ) * ( 1.0f - 5.0f * m_load ) : 0.0f;
            float chance = s.crackle * ( 0.09f * over + 0.12f * m_cut );
            if ( m_popT > 0.06f * Rate && m_rng.unit() < chance )
            {
                m_popT = 0;
                m_popA = m_rng.range( 0.5f, 1.4f );
                m_popF = m_rng.range( 90, 220 );
                m_popBank = fired;
            }
            m_knock = m_rng.range( 0.7f, 1.0f );
            m_tick = 1.0f;
        }
        if ( m_popT < 0.06f * Rate )
        {
            float t = m_popT / Rate;
            float p = m_popA * ( m_rng.bi() * std::exp( -t / 0.0018f ) * 1.6f +
                                 std::sin( TwoPi * m_popF * t ) * std::exp( -t / 0.012f ) );
            ex[m_popBank] += p;
            env += std::fabs( p ) * 0.5f;
        }
        m_popT += 1.0f;

        // the exhaust pipes: a comb each (the reflection from the open end), then the muffler
        float exh = 0;
        for ( int b = 0; b < 2; ++b )
        {
            float d = m_pipe[b][( m_pipeI - m_pipeLen[b] ) & ( PipeLen - 1 )];
            m_pipeLp[b] += ( d - m_pipeLp[b] ) * 0.5f;
            float y = ex[b] - s.pipeFb * m_pipeLp[b];
            m_pipe[b][m_pipeI & ( PipeLen - 1 )] = y;
            exh += y;
        }
        ++m_pipeI;

        if ( ( m_ctl++ & 15 ) == 0 )
        {
            float open = clampf( 0.35f * rpm + 0.65f * m_load, 0.0f, 1.0f );
            m_muffler.set( s.lowCut * std::pow( s.highCut / s.lowCut, open ), 0.8f );
            m_intake.set( 300.0f + 1700.0f * rpm, 1.6f );
        }
        m_muffler.run( exh );
        m_formant.run( exh );
        m_rasp.run( m_rng.bi() * std::sqrt( env ) );
        float body = m_muffler.lp + s.formantGain * m_formant.bpn + s.rasp * ( 0.35f + 0.65f * m_load ) * m_rasp.bpn;
        float drive = s.drive * ( 0.55f + 0.45f * m_load );
        float y = std::tanh( body * drive ) / drive;

        // the intake: noise with the throttle open, and the pulses ringing its resonance
        m_intake.run( m_rng.bi() );
        y +=
            s.intake * m_load * ( 0.15f + 0.85f * rpm ) * ( 0.5f + 0.5f * std::min( 1.0f, env ) ) * m_intake.bpn * 0.5f;
        if ( s.howl > 0 )
        {
            m_howl.run( env );
            y += s.howl * m_load * m_howl.bpn;
        }

        // the machinery: gear whine, diesel knock, valve tick, a tin-can rattle, the turbo
        m_gearPh += revs / 60.0f * 11.7f / Rate;
        if ( m_gearPh >= 1.0 ) m_gearPh -= 1.0;
        y += s.whine * rpm * rpm * ( 0.3f + 0.7f * m_load ) * std::sin( (float)( TwoPiD * m_gearPh ) );
        if ( s.clatter > 0 )
        {
            m_knock *= KKnock;
            float k = m_rng.bi() * m_knock;
            m_knockA.run( k );
            m_knockB.run( k );
            y += s.clatter * 0.25f * ( 0.55f + 0.45f * ( 1.0f - rpm ) ) * ( 0.6f + 0.4f * m_load ) *
                 ( m_knockA.bpn + 0.6f * m_knockB.bpn );
        }
        if ( s.tick > 0 )
        {
            m_tick *= KTick;
            m_tickF.run( m_rng.bi() * m_tick );
            y += s.tick * 0.2f * m_tickF.bpn * ( 0.5f + 0.5f * ( 1.0f - rpm ) );
        }
        if ( s.tin > 0 )
        {
            m_tinA.run( exh );
            m_tinB.run( exh );
            y += s.tin * 0.15f * ( m_tinA.bpn + 0.7f * m_tinB.bpn );
        }
        if ( s.turbo > 0 ) y += s.turbo * turboSound( rpm, loadGoal );
        // a little more of it at idle than physics would give: the player must hear the car waiting
        return y * ( 1.0f + 0.6f * ( 1.0f - rpm ) * ( 1.0f - m_load ) );
    }

    /** @return the turbo: a whistle rising with boost, a whoosh, and the blow-off valve when the throttle shuts */
    float turboSound( float rpm, float loadGoal )
    {
        float goal = m_load * smooth01( ( rpm - 0.15f ) / 0.5f );
        m_boost += ( goal - m_boost ) * ( goal > m_boost ? KSpool : KUnspool );
        if ( loadGoal < 0.25f && m_lastLoad >= 0.25f && m_boost > 0.3f )
        {
            m_bov = m_boost;
            m_bovT = 0;
        }
        m_lastLoad = loadGoal;
        m_turboPh += ( 1800.0f + 5600.0f * m_boost ) / Rate;
        if ( m_turboPh >= 1.0f ) m_turboPh -= 1.0f;
        float y = std::sin( TwoPi * m_turboPh ) * m_boost * m_boost * 0.035f;
        m_turboF.run( m_rng.bi() );
        y += m_turboF.bpn * m_boost * 0.03f;
        if ( m_bovT < 0.5f * Rate )
        {
            float t = m_bovT / Rate;
            m_bovF.run( m_rng.bi() );
            float fl = m_s->flutter > 0 ? 0.55f + 0.45f * std::sin( TwoPi * 17.0f * t ) : 1.0f;
            y += m_bov * 0.2f * m_bovF.hp * std::exp( -t / 0.11f ) * fl;
        }
        m_bovT += 1.0f;
        return y;
    }

    /** @brief A turbine: compressor whine, the shaft's hum, combustion roar, a low rumble, jet hiss. */
    float turbine()
    {
        m_spool += ( m_rpm - m_spool ) * ( m_rpm > m_spool ? KSpoolT : KUnspoolT );
        float        sp = m_spool, fc = ( 1900.0f + 5200.0f * sp ) * m_detune, ft = ( 70.0f + 310.0f * sp ) * m_detune;
        const double inc[4] = { fc, fc * 1.006, fc * 0.5, ft };
        for ( int k = 0; k < 4; ++k )
        {
            m_ph[k] += inc[k] / Rate;
            if ( m_ph[k] >= 1.0 ) m_ph[k] -= 1.0;
        }
        auto  sn = []( double p ) { return (float)std::sin( TwoPiD * p ); };
        float whine =
            ( sn( m_ph[0] ) + 0.7f * sn( m_ph[1] ) ) * 0.06f * ( 0.4f + 0.6f * sp ) + sn( m_ph[2] ) * 0.04f * sp;
        float shaft =
            ( sn( m_ph[3] ) + 0.5f * sn( 2 * m_ph[3] ) + 0.25f * sn( 3 * m_ph[3] ) ) * 0.12f * ( 0.5f + 0.5f * m_load );
        if ( ( m_ctl++ & 15 ) == 0 )
        {
            m_muffler.set( 250.0f + 2600.0f * m_load * ( 0.3f + 0.7f * sp ), 0.7f );
            m_formant.set( 90.0f + 60.0f * sp, 1.2f );
            m_intake.set( 5000, 0.7f );
        }
        float n = m_rng.bi();
        m_muffler.run( n );
        m_formant.run( m_rng.bi() );
        m_intake.run( m_rng.bi() );
        float roar = m_muffler.lp * noiseNorm( Flt::LP, 250.0f + 2600.0f * m_load * ( 0.3f + 0.7f * sp ), 0.7f ) *
                     0.1f * ( 0.12f + 0.55f * m_load ) * ( 0.35f + 0.65f * sp );
        float rumble = m_formant.bpn * 0.6f * ( 0.3f + 0.7f * m_load );
        float jet = m_intake.hp * 0.05f * sp * m_load;
        float y = whine + shaft + roar + rumble + jet;
        return std::tanh( y * 1.3f ) / 1.3f;
    }

    /** @brief An electric motor: whine, the inverter stepping up through a few notes, a soft hum at a standstill. */
    float electric()
    {
        float m = m_rpm, fe = ( 12.0f + 520.0f * m ) * m_detune;
        float goal = m < 0.04f   ? 1047.0f
                     : m < 0.08f ? 1397.0f
                     : m < 0.12f ? 1760.0f
                     : m < 0.17f ? 2349.0f
                     : m < 0.23f ? 2794.0f
                                 : 3520.0f;
        m_fsw += ( goal - m_fsw ) * KGlide;
        const double inc[7] = {
            fe, m_fsw, m_fsw + 2 * fe, std::max( 50.0f, m_fsw - 2 * fe ), fe * 3.71f, 110.0, 165.4
        };
        for ( int k = 0; k < 7; ++k )
        {
            m_ph[k] += inc[k] / Rate;
            if ( m_ph[k] >= 1.0 ) m_ph[k] -= 1.0;
        }
        auto  sn = []( double p ) { return (float)std::sin( TwoPiD * p ); };
        float p0 = (float)m_ph[0];
        float whine = ( 0.15f * sn( p0 ) + 0.08f * sn( 2.0 * p0 ) + 0.3f * sn( 6.0 * p0 ) + 0.1f * sn( 12.0 * p0 ) ) *
                      ( 0.4f + 0.6f * m_load ) * smooth01( m * 8.0f ) * 0.3f;   // coasting: the regeneration whines too
        float inv = ( sn( m_ph[1] ) + 0.3f * sn( m_ph[2] ) + 0.3f * sn( m_ph[3] ) ) * 0.05f * ( 0.2f + 0.8f * m_load ) *
                    ( 1.0f - smooth01( ( m - 0.2f ) / 0.12f ) ) * ( 0.3f + 0.7f * smooth01( m * 30.0f ) );
        float gear = sn( m_ph[4] ) * 0.05f * m * ( 0.3f + 0.7f * m_load );
        float avas = ( sn( m_ph[5] ) + 0.5f * sn( m_ph[6] ) ) * 0.05f *
                     ( 1.0f - smooth01( m * 4.0f ) );   // heard by those it runs over
        if ( ( m_ctl++ & 15 ) == 0 ) m_muffler.set( 1500, 0.7f );
        m_muffler.run( m_rng.bi() );
        float hissy = m_muffler.lp * 0.04f * m * m_load;
        return whine + inv + gear + avas + hissy;
    }

    const EngineSpec *m_s = nullptr;
    EngineE           m_kind = EngineE::TIN;
    Rand              m_rng;
    float             m_rpm = 0, m_load = 0, m_detune = 1;
    double            m_phase = 0;
    float             m_local[12] = {}, m_amp[12] = {}, m_cylVar[12] = {};
    float             m_pipe[2][PipeLen] = {};
    float             m_pipeLp[2] = {};
    int               m_pipeLen[2] = { 100, 100 };
    unsigned          m_pipeI = 0;
    Svf   m_muffler, m_formant, m_rasp, m_intake, m_howl, m_knockA, m_knockB, m_tickF, m_turboF, m_bovF, m_tinA, m_tinB;
    float m_cut = 0, m_limPh = 0;
    float m_boost = 0, m_turboPh = 0, m_bov = 0, m_bovT = 1e9f, m_lastLoad = 0;
    float m_popA = 0, m_popT = 1e9f, m_popF = 150;
    int   m_popBank = 0;
    float m_knock = 0, m_tick = 0;
    double   m_gearPh = 0;
    float    m_dcX = 0, m_dcY = 0;
    unsigned m_ctl = 0;
    float    m_spool = 0, m_fsw = 1047;
    double   m_ph[8] = {};
};

/** @brief The rev sweep of the dump: idle 1 s, full throttle to the limiter (held a moment), off-throttle down. */
std::vector<float> engineSweep( EngineE k )
{
    EngineVoice v;
    v.start( k, 7, 0, 0 );
    size_t             n = 6 * Rate;
    std::vector<float> out( n );
    float              rpm = 0, load = 0;
    for ( size_t i = 0; i < n; ++i )
    {
        if ( i % 735 == 0 )   // the game sets them once a frame
        {
            float t = i / (float)Rate;
            if ( t < 1 )
                rpm = 0, load = 0;
            else if ( t < 4 )
                rpm = std::min( 1.0f, ( t - 1 ) / 2.6f ), load = 1;
            else
                rpm = std::max( 0.0f, 1 - ( t - 4 ) / 2 ), load = 0;
        }
        out[i] = v.tick( rpm, load ) * std::min( 1.0f, i / ( 0.005f * Rate ) );
    }
    return out;
}

/** @brief Writes 16-bit PCM WAV. */
bool writeWav( const std::string &path, const std::vector<float> &data, int ch )
{
    FILE *f = std::fopen( path.c_str(), "wb" );
    if ( !f ) return false;
    auto     u32 = [f]( uint32_t v ) { std::fwrite( &v, 4, 1, f ); };
    auto     u16 = [f]( uint16_t v ) { std::fwrite( &v, 2, 1, f ); };
    uint32_t bytes = (uint32_t)( data.size() * 2 );
    std::fwrite( "RIFF", 1, 4, f );
    u32( 36 + bytes );
    std::fwrite( "WAVEfmt ", 1, 8, f );
    u32( 16 );
    u16( 1 );
    u16( (uint16_t)ch );
    u32( Rate );
    u32( Rate * ch * 2 );
    u16( (uint16_t)( ch * 2 ) );
    u16( 16 );
    std::fwrite( "data", 1, 4, f );
    u32( bytes );
    std::vector<int16_t> pcm( data.size() );
    for ( size_t i = 0; i < data.size(); ++i )
        pcm[i] = (int16_t)std::lround( clampf( data[i], -1.0f, 1.0f ) * 32767.0f );
    std::fwrite( pcm.data(), 2, pcm.size(), f );
    std::fclose( f );
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// the mixer (the audio thread): engines, loops and the ambience's events
// ---------------------------------------------------------------------------------------------------------------

/** @brief An engine slot: the game writes the atomics, the audio thread does the rest. */
struct EngineSlot
{
    std::atomic<int>   kind{ 0 };
    std::atomic<float> rpm{ 0 }, load{ 0 }, vol{ 0 }, pan{ 0 };
    int                stamp = -100;   ///< the frame of the last engine() call (game thread)
    EngineVoice        voice;
    bool               active = false;
    int                cur = -1;
    float              gain = 0, panS = 0;
};

/** @brief A loop slot. */
struct LoopSlot
{
    std::atomic<float> vol{ 0 }, pitch{ 1 };
    int                stamp = -100;     ///< the frame of the last loop() call (game thread)
    bool               wanted = false;   ///< asked for at least once (ambiences are made when first wanted)
    double             pos = 0;
    float              gain = 0, pit = 1;
    bool               active = false;
    double             next = 0;    ///< samples till the next event
    int                last = -1;   ///< the last event (not twice in a row)

    /** @brief An event playing. */
    struct Ev
    {
        int    clip = -1;
        double pos = 0;
        float  gain = 0, pan = 0, rate = 1;
    } ev[4];
};

/** @brief The mixer's state: the engines and loops the audio thread plays. */
struct Mixer
{
    EngineSlot              engines[EngineSlots];
    LoopSlot                loops[LoopCount];
    std::atomic<LoopClip *> clips[LoopCount] = {};
    std::atomic<float>      master{ 0.8f };
    float                   masterS = 0.8f;
    Rand                    rng;   ///< the audio thread's chances
    int                     frame = 0;

    ~Mixer()
    {
        for ( auto &c : clips ) delete c.load();
    }

    /** @brief Mixes the engines and loops into 'frames' stereo frames (the audio thread). */
    void mix( float *out, unsigned frames )
    {
        for ( unsigned off = 0; off < frames; off += Chunk )
            mixChunk( out + 2 * off, std::min<unsigned>( Chunk, frames - off ) );
    }

    void mixChunk( float *out, unsigned frames )
    {
        std::memset( out, 0, sizeof( float ) * 2 * frames );
        const float kGain = coef( 0.03f ), kPan = coef( 0.05f ), kPitch = coef( 0.03f );

        for ( int s = 0; s < EngineSlots; ++s )
        {
            EngineSlot &e = engines[s];
            float       vol = e.vol.load( std::memory_order_relaxed ), pan = e.pan.load( std::memory_order_relaxed );
            float       rpm = e.rpm.load( std::memory_order_relaxed ), load = e.load.load( std::memory_order_relaxed );
            int         kind = std::clamp( e.kind.load( std::memory_order_relaxed ), 0, EngineCount - 1 );
            if ( !e.active )
            {
                if ( vol <= 0.0005f ) continue;
                e.voice.start( (EngineE)kind, 1000u + s * 7919u + kind * 131u, rpm, load );
                e.active = true;
                e.cur = kind;
                e.gain = 0;
                e.panS = pan;
            }
            float goal = kind == e.cur ? vol : 0.0f;   // another kind: fade out, start afresh
            for ( unsigned i = 0; i < frames; ++i )
            {
                e.gain += ( goal - e.gain ) * kGain;
                e.panS += ( pan - e.panS ) * kPan;
                float v = e.voice.tick( rpm, load ) * e.gain, l, r;
                panGains( e.panS, l, r );
                out[2 * i] += v * l;
                out[2 * i + 1] += v * r;
            }
            if ( goal <= 0.0f && e.gain < 0.0003f ) e.active = false;
        }

        float gains[Chunk];
        for ( int l = 0; l < LoopCount; ++l )
        {
            LoopSlot &L = loops[l];
            LoopClip *c = clips[l].load( std::memory_order_acquire );
            float     vol = L.vol.load( std::memory_order_relaxed ), pit = L.pitch.load( std::memory_order_relaxed );
            if ( !c || c->frames < 4 ) continue;
            if ( !L.active )
            {
                if ( vol <= 0.0005f ) continue;
                L.active = true;
                L.pos = rng.unit() * c->frames;   // a loop never starts at the same place
                L.gain = 0;
                L.pit = pit;
                L.next = Rate * rng.range( 1.0f, 4.0f );
                for ( auto &ev : L.ev ) ev.clip = -1;
            }
            const size_t N = c->frames;
            const float *d = c->data.data();
            for ( unsigned i = 0; i < frames; ++i )
            {
                L.gain += ( vol - L.gain ) * kGain;
                L.pit += ( pit - L.pit ) * kPitch;
                gains[i] = L.gain;
                size_t i0 = (size_t)L.pos, im = ( i0 + N - 1 ) % N, i1 = ( i0 + 1 ) % N, i2 = ( i0 + 2 ) % N;
                float  t = (float)( L.pos - i0 );
                if ( c->ch == 1 )
                {
                    float v = hermite( d[im], d[i0], d[i1], d[i2], t ) * L.gain;
                    out[2 * i] += v;
                    out[2 * i + 1] += v;
                }
                else
                    for ( int k = 0; k < 2; ++k )
                        out[2 * i + k] +=
                            hermite( d[2 * im + k], d[2 * i0 + k], d[2 * i1 + k], d[2 * i2 + k], t ) * L.gain;
                L.pos += L.pit;
                if ( L.pos >= N ) L.pos -= N;
            }
            if ( !c->events.empty() )
            {
                L.next -= frames * L.pit;
                if ( L.next <= 0 )
                {
                    for ( auto &ev : L.ev )
                        if ( ev.clip < 0 )
                        {
                            int k = rng.below( (int)c->events.size() );
                            if ( k == L.last && c->events.size() > 1 )
                                k = ( k + 1 + rng.below( (int)c->events.size() - 1 ) ) % c->events.size();
                            ev = { k, 0.0, rng.range( 0.55f, 1.0f ), rng.range( -0.75f, 0.75f ),
                                   rng.range( 0.96f, 1.04f ) };
                            L.last = k;
                            break;
                        }
                    L.next = Rate * rng.range( c->gapMin, c->gapMax );
                }
                for ( auto &ev : L.ev )
                {
                    if ( ev.clip < 0 ) continue;
                    const std::vector<float> &clip = c->events[ev.clip];
                    float                     gl, gr;
                    panGains( ev.pan, gl, gr );
                    for ( unsigned i = 0; i < frames; ++i )
                    {
                        size_t k = (size_t)ev.pos;
                        if ( k + 1 >= clip.size() )
                        {
                            ev.clip = -1;
                            break;
                        }
                        float t = (float)( ev.pos - k ),
                              v = ( clip[k] + ( clip[k + 1] - clip[k] ) * t ) * ev.gain * gains[i];
                        out[2 * i] += v * gl;
                        out[2 * i + 1] += v * gr;
                        ev.pos += ev.rate * L.pit;
                    }
                }
            }
            if ( vol <= 0.0f && L.gain < 0.0003f ) L.active = false;
        }

        // the effect volume, and a soft ceiling so that a pile-up never clips
        float goal = master.load( std::memory_order_relaxed );
        for ( unsigned i = 0; i < frames; ++i )
        {
            masterS += ( goal - masterS ) * kGain;
            out[2 * i] = softCeil( out[2 * i] * masterS );
            out[2 * i + 1] = softCeil( out[2 * i + 1] * masterS );
        }
    }
};

}   // namespace

/** @brief Everything behind the interface: the effects, the mixer, the music. */
struct Audio::Impl : Mixer
{
    std::string assets;
    Sound       sounds[SfxCount][SoundVoices] = {};
    int         next[SfxCount] = {};
    double      lastPlay[SfxCount] = {};
    bool        soundsOk = false;

    AudioStream stream = {};
    bool        streamOn = false;

    std::thread       builder;   ///< makes an ambience the first time it is wanted
    std::atomic<bool> building{ false };

    Music       music[2] = {};
    bool        has[2] = {};
    float       mgain[2] = {};
    int         cur = 0;
    std::string musicName;
    float                    duckTime = 0.0f, duckDepth = 0.0f;   ///< the music lowered for a moment
    Sound                    voiceSnd = {};                        ///< the announcer's line playing
    bool                     hasVoice = false;
    std::vector<std::string> takes;   ///< the playing piece's takes (<name>, <name>_2, ...)
    int                      take = 0;

    /** @brief Starts takes[take] in the free slot; the update crossfades to it. */
    void playTake()
    {
        int slot = 1 - cur;
        if ( has[slot] )
        {
            StopMusicStream( music[slot] );
            UnloadMusicStream( music[slot] );
            has[slot] = false;
        }
        if ( !takes.empty() )
        {
            music[slot] = LoadMusicStream( takes[(size_t)take].c_str() );
            if ( IsMusicValid( music[slot] ) )
            {
                has[slot] = true;
                music[slot].looping = takes.size() == 1;   // several takes follow one another instead
                mgain[slot] = 0.0f;
                SetMusicVolume( music[slot], 0.0f );
                PlayMusicStream( music[slot] );
            }
        }
        cur = slot;
    }

    ~Impl()
    {
        if ( builder.joinable() ) builder.join();
    }
};

namespace
{

std::atomic<Mixer *> g_mixer{ nullptr };   ///< raylib's stream callback has no user pointer

void mixCallback( void *buffer, unsigned int frames )
{
    Mixer *m = g_mixer.load( std::memory_order_acquire );
    if ( !m )
    {
        std::memset( buffer, 0, sizeof( float ) * 2 * frames );
        return;
    }
    m->mix( (float *)buffer, frames );
}

}   // namespace

void Audio::init( bool on, const std::string &assets )
{
    m_on = on && IsAudioDeviceReady();
    if ( !m_on || m_impl ) return;
    m_impl = new Impl;
    Impl &m = *m_impl;
    m.assets = assets;
    m.rng.seed( (uint32_t)( GetTime() * 1000.0 ) + 17u );
    for ( int k = 0; k < EngineCount; ++k ) specOf( (EngineE)k );

    // the effects and the short loops, made on a few threads (the ambiences when first wanted)
    std::vector<std::vector<float>> made( SfxCount );
    std::vector<LoopClip *>         loops( FirstAmbience, nullptr );
    std::atomic<int>                job{ 0 };
    auto                            work = [&]
    {
        for ( int j; ( j = job++ ) < SfxCount + FirstAmbience; )
            if ( j < SfxCount )
                made[j] = makeSfx( (SfxE)j );
            else
                loops[j - SfxCount] = new LoopClip( makeLoop( (LoopE)( j - SfxCount ) ) );
    };
    std::vector<std::thread> workers;
    int                      n = std::clamp( (int)std::thread::hardware_concurrency() - 1, 1, 4 );
    for ( int k = 1; k < n; ++k ) workers.emplace_back( work );
    work();
    for ( std::thread &t : workers ) t.join();
    for ( int l = 0; l < FirstAmbience; ++l ) m.clips[l].store( loops[l], std::memory_order_release );

    // raylib takes float samples and converts them for the device
    for ( int e = 0; e < SfxCount; ++e )
    {
        const std::vector<float> &s = made[e];
        Wave                      w = {};
        w.frameCount = (unsigned)s.size();
        w.sampleRate = Rate;
        w.sampleSize = 32;
        w.channels = 1;
        w.data = (void *)s.data();
        m.sounds[e][0] = LoadSoundFromWave( w );
        for ( int v = 1; v < SoundVoices; ++v ) m.sounds[e][v] = LoadSoundAlias( m.sounds[e][0] );
    }
    m.soundsOk = true;

    // the mixer: one float stereo stream fed from the audio thread
    m.stream = LoadAudioStream( Rate, 32, 2 );
    if ( IsAudioStreamValid( m.stream ) )
    {
        g_mixer.store( m_impl, std::memory_order_release );
        SetAudioStreamCallback( m.stream, mixCallback );
        PlayAudioStream( m.stream );
        m.streamOn = true;
    }
}

void Audio::shutdown()
{
    if ( !m_impl ) return;
    Impl &m = *m_impl;
    if ( m.streamOn )
    {
        StopAudioStream( m.stream );   // takes raylib's lock: the callback is not running afterwards
        g_mixer.store( nullptr, std::memory_order_release );
        UnloadAudioStream( m.stream );
        m.streamOn = false;
    }
    if ( m.hasVoice )
    {
        StopSound( m.voiceSnd );
        UnloadSound( m.voiceSnd );
        m.hasVoice = false;
    }
    for ( int i = 0; i < 2; ++i )
        if ( m.has[i] )
        {
            StopMusicStream( m.music[i] );
            UnloadMusicStream( m.music[i] );
            m.has[i] = false;
        }
    if ( m.soundsOk )
        for ( int e = 0; e < SfxCount; ++e )
        {
            for ( int v = 1; v < SoundVoices; ++v ) UnloadSoundAlias( m.sounds[e][v] );
            UnloadSound( m.sounds[e][0] );
        }
    delete m_impl;
    m_impl = nullptr;
    m_on = false;
}

void Audio::play( SfxE s, float pan, float volume, float pitch )
{
    if ( !m_on || !m_impl ) return;
    int e = (int)s;
    if ( e < 0 || e >= SfxCount ) return;
    Impl  &m = *m_impl;
    double now = GetTime();
    // the same effect at most every 25 ms: a hail of hits only gets louder, not better
    if ( now - m.lastPlay[e] < 0.025 ) return;
    m.lastPlay[e] = now;
    Sound &snd = m.sounds[e][m.next[e]];
    m.next[e] = ( m.next[e] + 1 ) % SoundVoices;
    SetSoundVolume( snd, clampf( volume * m_effectVolume, 0.0f, 1.0f ) );
    SetSoundPan( snd, clampf( 0.5f - 0.5f * pan, 0.0f, 1.0f ) );   // raylib: 1 is the left
    SetSoundPitch( snd, clampf( pitch, 0.1f, 4.0f ) );
    PlaySound( snd );
}

void Audio::loop( LoopE l, float volume, float pitch )
{
    if ( !m_on || !m_impl ) return;
    int i = (int)l;
    if ( i < 0 || i >= LoopCount ) return;
    LoopSlot &L = m_impl->loops[i];
    L.vol.store( clampf( volume, 0.0f, 1.0f ), std::memory_order_relaxed );
    L.pitch.store( clampf( pitch, 0.25f, 4.0f ), std::memory_order_relaxed );
    L.stamp = m_impl->frame;
    L.wanted = true;
}

void Audio::engine( int slot, EngineE kind, float rpm, float load, float volume, float pan )
{
    if ( !m_on || !m_impl || slot < 0 || slot >= EngineSlots ) return;
    EngineSlot &e = m_impl->engines[slot];
    e.kind.store( std::clamp( (int)kind, 0, EngineCount - 1 ), std::memory_order_relaxed );
    e.rpm.store( clampf( rpm, 0.0f, 1.0f ), std::memory_order_relaxed );
    e.load.store( clampf( load, 0.0f, 1.0f ), std::memory_order_relaxed );
    e.vol.store( clampf( volume, 0.0f, 1.0f ), std::memory_order_relaxed );
    e.pan.store( clampf( pan, -1.0f, 1.0f ), std::memory_order_relaxed );
    e.stamp = m_impl->frame;
}

void Audio::quiet()
{
    if ( !m_on || !m_impl ) return;
    for ( EngineSlot &e : m_impl->engines )
    {
        e.vol.store( 0.0f, std::memory_order_relaxed );
        e.stamp = -100;
    }
    for ( LoopSlot &L : m_impl->loops )
    {
        L.vol.store( 0.0f, std::memory_order_relaxed );
        L.stamp = -100;
    }
}

void Audio::music( const std::string &name )
{
    if ( !m_on || !m_impl ) return;
    Impl &m = *m_impl;
    if ( name == m.musicName ) return;
    m.musicName = name;
    m.takes.clear();
    if ( !name.empty() )
    {
        // a piece may come in several takes (<name>, <name>_2, ...): one of them by chance, then the others in turn
        for ( int k = 1; k <= 9; ++k )
        {
            std::string base = m.assets + "/music/" + name + ( k == 1 ? "" : "_" + std::to_string( k ) );
            for ( const char *ext : { ".ogg", ".mp3", ".wav", ".flac" } )
            {
                std::error_code ec;
                if ( std::filesystem::exists( base + ext, ec ) )
                {
                    m.takes.push_back( base + ext );
                    break;
                }
            }
        }
    }
    m.take = m.takes.empty() ? 0 : GetRandomValue( 0, (int)m.takes.size() - 1 );
    m.playTake();
}

bool Audio::hasMusic( const std::string &name ) const
{
    if ( !m_impl ) return false;
    std::error_code ec;
    for ( const char *ext : { ".ogg", ".mp3", ".wav", ".flac" } )
        if ( std::filesystem::exists( m_impl->assets + "/music/" + name + ext, ec ) ) return true;
    return false;
}

bool Audio::voice( const std::string &file )
{
    if ( !m_on || !m_impl ) return false;
    Impl &m = *m_impl;
    std::error_code ec;
    if ( !std::filesystem::exists( file, ec ) ) return false;
    if ( m.hasVoice )
    {
        StopSound( m.voiceSnd );
        UnloadSound( m.voiceSnd );
        m.hasVoice = false;
    }
    m.voiceSnd = LoadSound( file.c_str() );
    if ( !IsSoundValid( m.voiceSnd ) ) return false;
    m.hasVoice = true;
    SetSoundVolume( m.voiceSnd, std::min( 1.0f, m_effectVolume * 1.15f ) );
    PlaySound( m.voiceSnd );
    duck( (float)m.voiceSnd.frameCount / std::max( 1u, m.voiceSnd.stream.sampleRate ) + 0.3f, 0.45f );
    return true;
}

void Audio::duck( float seconds, float depth )
{
    if ( !m_on || !m_impl ) return;
    m_impl->duckTime = std::max( m_impl->duckTime, seconds );
    m_impl->duckDepth = std::max( m_impl->duckTime > seconds ? m_impl->duckDepth : 0.0f, depth );
}

void Audio::update( float dt )
{
    if ( !m_on || !m_impl ) return;
    Impl &m = *m_impl;
    m.duckTime = std::max( 0.0f, m.duckTime - dt );
    if ( m.duckTime <= 0 ) m.duckDepth = 0;
    float duckGain = 1.0f - m.duckDepth * std::min( 1.0f, m.duckTime / 0.4f );
    m.master.store( clampf( m_effectVolume, 0.0f, 1.0f ), std::memory_order_relaxed );

    // engines and loops not set in this frame or the last fade out
    for ( EngineSlot &e : m.engines )
        if ( m.frame - e.stamp > 1 ) e.vol.store( 0.0f, std::memory_order_relaxed );
    for ( LoopSlot &L : m.loops )
        if ( m.frame - L.stamp > 1 ) L.vol.store( 0.0f, std::memory_order_relaxed );
    ++m.frame;

    // an ambience is made (on its own thread) the first time it is wanted
    if ( !m.building.load() )
    {
        if ( m.builder.joinable() ) m.builder.join();
        for ( int l = FirstAmbience; l < LoopCount; ++l )
            if ( m.loops[l].wanted && !m.clips[l].load() )
            {
                m.building = true;
                m.builder = std::thread(
                    [&m, l]
                    {
                        m.clips[l].store( new LoopClip( makeLoop( (LoopE)l ) ), std::memory_order_release );
                        m.building = false;
                    } );
                break;
            }
    }

    // several takes: the next one crossfades in as the playing one runs out
    if ( m.takes.size() > 1 && m.has[m.cur] &&
         GetMusicTimeLength( m.music[m.cur] ) - GetMusicTimePlayed( m.music[m.cur] ) < 1.5f )
    {
        m.take = ( m.take + 1 ) % (int)m.takes.size();
        m.playTake();
    }

    // the music, crossfaded over 1.5 s
    for ( int i = 0; i < 2; ++i )
    {
        if ( !m.has[i] ) continue;
        float goal = i == m.cur ? 1.0f : 0.0f;
        m.mgain[i] += clampf( goal - m.mgain[i], -dt / 1.5f, dt / 1.5f );
        SetMusicVolume( m.music[i], m.mgain[i] * m_musicVolume * duckGain );
        UpdateMusicStream( m.music[i] );
        if ( i != m.cur && m.mgain[i] <= 0.0f )
        {
            StopMusicStream( m.music[i] );
            UnloadMusicStream( m.music[i] );
            m.has[i] = false;
        }
    }
}

void Audio::dump( const std::string &dir )
{
    std::error_code ec;
    std::filesystem::create_directories( dir, ec );
    using Clock = std::chrono::steady_clock;
    double made[4] = {};   // seconds spent making: effects, short loops, ambiences, engines
    auto   took = []( Clock::time_point t0 ) { return std::chrono::duration<double>( Clock::now() - t0 ).count(); };
    for ( int e = 0; e < SfxCount; ++e )
    {
        auto               t0 = Clock::now();
        std::vector<float> s = makeSfx( (SfxE)e );
        made[0] += took( t0 );
        writeWav( dir + "/" + SfxNames[e] + ".wav", s, 1 );
    }
    for ( int l = 0; l < LoopCount; ++l )
    {
        auto     t0 = Clock::now();
        LoopClip c = makeLoop( (LoopE)l );
        made[l < FirstAmbience ? 1 : 2] += took( t0 );
        if ( !c.events.empty() )
        {
            // an ambience as heard: its bed and a few of its events, folded round so the file loops as well
            Rand r;
            r.seed( 77 + l );
            float t = 1.5f;
            for ( size_t k = 0; k < c.events.size() && t < 18.0f; ++k )
            {
                const std::vector<float> &ev = c.events[k];
                float                     gl, gr, g = 0.8f;
                panGains( r.range( -0.7f, 0.7f ), gl, gr );
                size_t s0 = (size_t)( t * Rate );
                for ( size_t j = 0; j < ev.size(); ++j )
                {
                    size_t i = ( s0 + j ) % c.frames;
                    c.data[2 * i] += ev[j] * g * gl;
                    c.data[2 * i + 1] += ev[j] * g * gr;
                }
                t += r.range( 4.0f, 6.0f );
            }
        }
        writeWav( dir + "/loop_" + LoopNames[l] + ".wav", c.data, c.ch );
    }
    for ( int k = 0; k < EngineCount; ++k )
    {
        auto               t0 = Clock::now();
        std::vector<float> s = engineSweep( (EngineE)k );
        made[3] += took( t0 );
        writeWav( dir + "/engine_" + EngineNames[k] + ".wav", s, 1 );
    }
    std::printf(
        "made in: effects %.2f s, short loops %.2f s (both at init), ambiences %.2f s (each when first wanted), "
        "engines %.2f s for %d x 6 s (%.1f%% of one core per voice)\n",
        made[0], made[1], made[2], made[3], EngineCount, 100.0 * made[3] / ( EngineCount * 6.0 ) );
}

EngineE Audio::engineKind( const std::string &name )
{
    for ( int k = 0; k < EngineCount; ++k )
        if ( name == EngineNames[k] ) return (EngineE)k;
    return EngineE::TIN;
}

SfxE Audio::hornOf( const std::string &name )
{
    for ( int k = 0; k < 7; ++k )
        if ( name == HornNames[k] ) return (SfxE)( (int)SfxE::HORN_PLAIN + k );
    return SfxE::HORN_PLAIN;
}

LoopE Audio::ambienceOf( const std::string &venue )
{
    for ( int k = 0; k < LoopCount - FirstAmbience; ++k )
        if ( venue == VenueNames[k] ) return (LoopE)( FirstAmbience + k );
    return LoopE::AMB_JUNKYARD;
}

}   // namespace grave
