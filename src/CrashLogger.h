#pragma once
#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
  // Guard against min/max macro pollution from windows.h bleeding into every
  // translation unit that includes this header (via PluginProcessor.h).
  // These must be defined BEFORE windows.h is pulled in.
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <dbghelp.h>
  // Belt-and-suspenders: undef in case an earlier transitive include already
  // brought in windows.h without NOMINMAX (some JUCE/SDK versions do this).
  #ifdef min
    #undef min
  #endif
  #ifdef max
    #undef max
  #endif
  #pragma comment (lib, "dbghelp.lib")
#else
  #include <signal.h>
  #include <execinfo.h>   // backtrace / backtrace_symbols (glibc / macOS)
#endif

/**
 *  CrashLogger  v1
 *  ===============
 *  Attach one instance to DysektProcessor to detect and log crashes on exit.
 *
 *  Mechanism 1 — Sentinel file
 *  ----------------------------
 *  A file named "session.lock" is written to the log directory on construction
 *  and deleted on clean destruction.  If the file already exists when the
 *  constructor runs, the previous session ended without calling the destructor
 *  (i.e. the DAW or plugin crashed), and a "PREVIOUS SESSION CRASHED" entry is
 *  appended to the log before continuing normally.
 *
 *  Mechanism 2 — Platform crash handler
 *  --------------------------------------
 *  On Windows: SetUnhandledExceptionFilter writes a .dmp minidump and a final
 *  log entry, then lets the default handler terminate the process.
 *  On POSIX (macOS / Linux): signal handlers for SIGSEGV, SIGABRT, SIGBUS,
 *  SIGILL, and SIGFPE write a final log entry (+ backtrace on platforms that
 *  support it) and re-raise the signal so the OS can generate a core dump.
 *
 *  Log location
 *  ------------
 *  Windows : %APPDATA%\DunSoft\DYSEKT-SF\dysekt_crash.log
 *  macOS   : ~/Library/Logs/DunSoft/DYSEKT-SF/dysekt_crash.log
 *  Linux   : ~/.config/DunSoft/DYSEKT-SF/dysekt_crash.log
 *
 *  Thread safety
 *  -------------
 *  The crash handler runs on whatever thread the crash occurs on.  The handler
 *  writes directly to the log file via low-level I/O to avoid heap allocation
 *  (which is unsafe after a crash).  Normal lifecycle logging uses JUCE's
 *  FileLogger and is therefore message-thread-safe.
 *
 *  Usage
 *  -----
 *  Add a member to DysektProcessor:
 *
 *      CrashLogger crashLogger;    // declare before other members
 *
 *  That's it — the constructor/destructor handle everything automatically.
 */

class CrashLogger
{
public:
    // ── Construction / destruction ────────────────────────────────────────────

    CrashLogger()
    {
        const auto logDir = getLogDirectory();
        logDir.createDirectory();

        logFile    = logDir.getChildFile ("dysekt_crash.log");
        lockFile   = logDir.getChildFile ("session.lock");
        dumpDir    = logDir;   // minidumps written to same folder

        // ── Check for previous crash ──────────────────────────────────────────
        const bool previousCrash = lockFile.existsAsFile();

        // ── Open log (append mode) ────────────────────────────────────────────
        logger = std::make_unique<juce::FileLogger> (logFile,
            "DYSEKT-SF crash log — " + juce::Time::getCurrentTime().toString (true, true));

        if (previousCrash)
        {
            logger->logMessage ("");
            logger->logMessage ("!! PREVIOUS SESSION CRASHED OR WAS FORCE-KILLED !!");
            logger->logMessage ("   The session.lock sentinel was not removed, which means");
            logger->logMessage ("   DysektProcessor::~DysektProcessor() did not run cleanly.");
            logger->logMessage ("");
        }

        // ── Write sentinel ────────────────────────────────────────────────────
        lockFile.create();
        lockFile.replaceWithText ("pid=" + juce::String (juce::Time::getCurrentTime().toMilliseconds()));

        // ── Install crash handler ─────────────────────────────────────────────
        s_logFilePath = logFile.getFullPathName();
        s_dumpDirPath = dumpDir.getFullPathName();
        installHandlers();

        log ("DysektProcessor constructed — session started");
        // Version marker for the zone-builder diagnostics added to
        // SoundFontLoader.cpp (discoverActiveNotes/finishAndPost logging for
        // the SfzPlayer2 target, full-0-127 empty-discovery fallback). If a
        // session's log is missing this line, the running binary predates
        // those changes — most likely a stale/incremental build that didn't
        // actually recompile SoundFontLoader.cpp — rebuild clean and retest.
        log ("Build marker: zonebuilder-diagnostics-v1");
    }

    ~CrashLogger()
    {
        log ("DysektProcessor destructed — session ended cleanly");
        lockFile.deleteFile();
        // Leave the log file; only the sentinel is removed.
    }

    // ── Public logging API ────────────────────────────────────────────────────

    /** Write a timestamped entry to the log.  Safe to call from any thread
     *  that isn't in a crash handler. */
    void log (const juce::String& message)
    {
        if (logger != nullptr)
            logger->logMessage (message);
    }

    /** Return the path to the current log file (useful for a "Show log" menu item). */
    juce::File getLogFile() const { return logFile; }

private:
    // ── State ─────────────────────────────────────────────────────────────────

    std::unique_ptr<juce::FileLogger> logger;
    juce::File logFile;
    juce::File lockFile;
    juce::File dumpDir;

    // Shared with crash handlers (static so no heap access needed in handler).
    static juce::String s_logFilePath;
    static juce::String s_dumpDirPath;

    // ── Log directory ─────────────────────────────────────────────────────────

    static juce::File getLogDirectory()
    {
#if JUCE_WINDOWS
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("DunSoft/DYSEKT-SF");
#elif JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Logs/DunSoft/DYSEKT-SF");
#else
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile (".config/DunSoft/DYSEKT-SF");
#endif
    }

    // ── Handler installation ──────────────────────────────────────────────────

    static void installHandlers()
    {
#if JUCE_WINDOWS
        SetUnhandledExceptionFilter (windowsExceptionFilter);
#else
        struct sigaction sa {};
        sa.sa_handler = posixSignalHandler;
        sigemptyset (&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;   // restore default after first signal

        sigaction (SIGSEGV, &sa, nullptr);
        sigaction (SIGABRT, &sa, nullptr);
        sigaction (SIGBUS,  &sa, nullptr);
        sigaction (SIGILL,  &sa, nullptr);
        sigaction (SIGFPE,  &sa, nullptr);
#endif
    }

    // ── Crash-safe file write (no heap allocation) ────────────────────────────

    /** Appends a C-string to the log file using low-level I/O.
     *  Safe to call from a signal / exception handler. */
    static void crashWrite (const char* msg)
    {
#if JUCE_WINDOWS
        HANDLE h = CreateFileW (s_logFilePath.toWideCharPointer(),
                                GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE)
        {
            SetFilePointer (h, 0, nullptr, FILE_END);
            DWORD written;
            WriteFile (h, msg, (DWORD) strlen (msg), &written, nullptr);
            CloseHandle (h);
        }
#else
        // POSIX — open(2) / write(2) are async-signal-safe
        int fd = open (s_logFilePath.toRawUTF8(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0)
        {
            write (fd, msg, strlen (msg));
            close (fd);
        }
#endif
    }

    // ── Platform crash handlers ───────────────────────────────────────────────

#if JUCE_WINDOWS

    static LONG WINAPI windowsExceptionFilter (EXCEPTION_POINTERS* info)
    {
        crashWrite ("\n!! CRASH — unhandled exception !!\n");

        // Write a minidump alongside the log
        juce::String dumpPath = s_dumpDirPath + "\\dysekt_crash.dmp";
        HANDLE hDump = CreateFileW (dumpPath.toWideCharPointer(),
                                    GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDump != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei {};
            mei.ThreadId          = GetCurrentThreadId();
            mei.ExceptionPointers = info;
            mei.ClientPointers    = FALSE;

            MiniDumpWriteDump (GetCurrentProcess(),
                               GetCurrentProcessId(),
                               hDump,
                               MiniDumpWithDataSegs,
                               &mei, nullptr, nullptr);
            CloseHandle (hDump);
            crashWrite ("   Minidump written to: ");
            crashWrite (dumpPath.toRawUTF8());
            crashWrite ("\n");
        }

        crashWrite ("   Exception code: 0x");
        // Write the exception code as hex without sprintf (async-signal-safe workaround)
        char buf[32];
        DWORD code = info ? info->ExceptionRecord->ExceptionCode : 0;
        snprintf (buf, sizeof (buf), "%08lX\n", (unsigned long) code);
        crashWrite (buf);

        return EXCEPTION_CONTINUE_SEARCH;   // let default handler terminate
    }

#else   // POSIX

    static void posixSignalHandler (int sig)
    {
        crashWrite ("\n!! CRASH — signal received: ");
        switch (sig)
        {
            case SIGSEGV: crashWrite ("SIGSEGV (segmentation fault)\n"); break;
            case SIGABRT: crashWrite ("SIGABRT (abort)\n");              break;
            case SIGBUS:  crashWrite ("SIGBUS  (bus error)\n");          break;
            case SIGILL:  crashWrite ("SIGILL  (illegal instruction)\n"); break;
            case SIGFPE:  crashWrite ("SIGFPE  (floating-point exception)\n"); break;
            default:
            {
                char buf[32];
                snprintf (buf, sizeof (buf), "%d\n", sig);
                crashWrite (buf);
            }
        }

#if defined(__GLIBC__) || defined(__APPLE__)
        // Best-effort backtrace — may not resolve symbols in a stripped build.
        void* frames[32];
        const int n = backtrace (frames, 32);
        char** syms = backtrace_symbols (frames, n);
        crashWrite ("   Stack trace:\n");
        for (int i = 0; i < n; ++i)
        {
            crashWrite ("     ");
            crashWrite (syms ? syms[i] : "?");
            crashWrite ("\n");
        }
        // Note: free(syms) is intentionally omitted — we are about to crash.
#endif

        // Re-raise so the OS generates a core dump and the DAW sees the real signal.
        raise (sig);
    }

#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrashLogger)
};

// Static member definitions (one translation unit will see these via the header;
// acceptable for a header-only utility class in a single-plugin project).
inline juce::String CrashLogger::s_logFilePath;
inline juce::String CrashLogger::s_dumpDirPath;
