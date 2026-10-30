/**
 * @file Crash.cpp
 * @brief The crash report. Written from the crash itself, so it keeps to what is ready beforehand: static buffers,
 *        C files. No raylib here (windows.h and raylib.h do not go together).
 * @author Rene Weller
 */
#include "Crash.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
  // dbghelp.h's MiniDumpWriteDump, loaded when needed.
  #include <dbghelp.h>
#else
  #include <csignal>
  #include <execinfo.h>
  #include <unistd.h>
#endif

namespace grave
{
namespace crash
{

namespace
{

char g_dir[1024] = {};      ///< where the reports go
char g_log[1024] = {};      ///< the game's log
char g_context[512] = {};   ///< what is played

/** @brief The report's text: when and what. */
void writeText( const char *path, const char *what )
{
    FILE *f = fopen( path, "w" );
    if ( !f ) return;
    fprintf( f, "GRAVE PRIX " GRAVE_VERSION " crashed: %s\n", what );
    fprintf( f, "playing: %s\n", g_context );
    fprintf( f, "the game's log: %s\n", g_log );
    fclose( f );
}

/** @brief The reports' name without its ending: crashes/crash_<time>. */
void stem( char *out, size_t size )
{
    char   stamp[32];
    time_t now = time( nullptr );
    strftime( stamp, sizeof( stamp ), "%Y%m%d_%H%M%S", localtime( &now ) );
    snprintf( out, size, "%scrash_%s", g_dir, stamp );
}

#ifdef _WIN32

typedef BOOL( WINAPI *DumpFn )( HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION );

LONG WINAPI onCrash( EXCEPTION_POINTERS *info )
{
    CreateDirectoryA( g_dir, nullptr );
    char base[1200], path[1300], what[64];
    stem( base, sizeof( base ) );
    snprintf( what, sizeof( what ), "exception 0x%08lX",
              info ? (unsigned long)info->ExceptionRecord->ExceptionCode : 0ul );
    snprintf( path, sizeof( path ), "%s.txt", base );
    writeText( path, what );
    if ( HMODULE help = LoadLibraryA( "dbghelp.dll" ) )
        if ( DumpFn dump = (DumpFn)(void *)GetProcAddress( help, "MiniDumpWriteDump" ) )
        {
            snprintf( path, sizeof( path ), "%s.dmp", base );
            HANDLE file = CreateFileA( path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
            if ( file != INVALID_HANDLE_VALUE )
            {
                MINIDUMP_EXCEPTION_INFORMATION e = { GetCurrentThreadId(), info, FALSE };
                dump( GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithIndirectlyReferencedMemory,
                      info ? &e : nullptr, nullptr, nullptr );
                CloseHandle( file );
            }
        }
    return EXCEPTION_EXECUTE_HANDLER;
}

#else

void onSignal( int signal )
{
    std::filesystem::create_directories( g_dir );
    char base[1200], path[1300], what[64];
    stem( base, sizeof( base ) );
    snprintf( what, sizeof( what ), "signal %d", signal );
    snprintf( path, sizeof( path ), "%s.txt", base );
    writeText( path, what );
    // The call stack after the text.
    if ( FILE *f = fopen( path, "a" ) )
    {
        fprintf( f, "\nthe call stack:\n" );
        fflush( f );
        void *frames[64];
        int   n = backtrace( frames, 64 );
        backtrace_symbols_fd( frames, n, fileno( f ) );
        fclose( f );
    }
    std::signal( signal, SIG_DFL );
    std::raise( signal );
}

#endif

}   // namespace

void install( const std::string &dir, const std::string &log )
{
    snprintf( g_dir, sizeof( g_dir ), "%s", dir.c_str() );
    snprintf( g_log, sizeof( g_log ), "%s", log.c_str() );
#ifdef _WIN32
    SetUnhandledExceptionFilter( onCrash );
#else
    for ( int s : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS } ) std::signal( s, onSignal );
#endif
}

void context( const std::string &text )
{
    snprintf( g_context, sizeof( g_context ), "%s", text.c_str() );
}

std::string unseen()
{
    // The newest report that has no ".seen" beside it.
    namespace fs = std::filesystem;
    std::error_code e;
    std::string     newest;
    for ( const fs::directory_entry &f : fs::directory_iterator( fs::path( g_dir ), e ) )
    {
        std::string p = f.path().string();
        if ( p.size() > 4 && p.compare( p.size() - 4, 4, ".txt" ) == 0 && !fs::exists( fs::path( p + ".seen" ), e ) &&
             p > newest )
            newest = p;
    }
    if ( !newest.empty() )
        if ( FILE *f = fopen( ( newest + ".seen" ).c_str(), "w" ) ) fclose( f );
    return newest;
}

}   // namespace crash
}   // namespace grave
