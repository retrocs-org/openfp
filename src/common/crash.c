/* SPDX-License-Identifier: MIT */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L                          /* (fileno) */
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "common/crash.h"
#include "common/net.h"
#include "common/rules.h"
#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#include <unistd.h>
#include <fcntl.h>
#define OFP_BACKTRACE 1
#endif
#ifdef _WIN32
#include <windows.h>
#endif

static char crash_dir[900], crash_program[32], crash_head[256];
static void (*crash_save)(const char *path);
static volatile sig_atomic_t crashing;

static void write_report(const char *what)
{
    char stamp[32], path[1000];
    time_t now = time(NULL);
    FILE *f;
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    snprintf(path, sizeof path, "%scrash-%s.txt", crash_dir, stamp);
    if ((f = fopen(path, "w")) != NULL) {
        fprintf(f, "%s\n%s\n", crash_head, what);
        fflush(f);
#ifdef OFP_BACKTRACE
        {
            void *frames[64];
            int n = backtrace(frames, 64);
            fprintf(f, "where (newest first):\n");
            fflush(f);
            backtrace_symbols_fd(frames, n, fileno(f));
        }
#endif
        fclose(f);
        fprintf(stderr, "%s crashed (%s): a report is in %s\n", crash_program, what, path);
    }
    if (crash_save) {                                    /* the match so far */
        snprintf(path, sizeof path, "%scrash-%s.ofprep", crash_dir, stamp);
        crash_save(path);
    }
}

#ifdef _WIN32
static LONG WINAPI on_exception(EXCEPTION_POINTERS *e)
{
    char what[128];
    if (crashing) return EXCEPTION_CONTINUE_SEARCH;
    crashing = 1;
    snprintf(what, sizeof what, "exception %08lx at %p", (unsigned long)e->ExceptionRecord->ExceptionCode,
             e->ExceptionRecord->ExceptionAddress);
    write_report(what);
    return EXCEPTION_CONTINUE_SEARCH;                    /* (and Windows does what it does) */
}
#else
static void on_signal(int sig)
{
    const char *what = sig == SIGSEGV ? "signal SIGSEGV (a bad memory access)" : sig == SIGABRT ? "signal SIGABRT (an abort)"
                     : sig == SIGFPE ? "signal SIGFPE (arithmetic)" : sig == SIGILL ? "signal SIGILL (an illegal instruction)"
#ifdef SIGBUS
                     : sig == SIGBUS ? "signal SIGBUS (a bad memory access)"
#endif
                     : "a signal";
    if (!crashing) { crashing = 1; write_report(what); }
    signal(sig, SIG_DFL);                                /* then as it would have gone */
    raise(sig);
}
#endif

void crash_install(const char *program, const char *dir, void (*save)(const char *path))
{
    snprintf(crash_program, sizeof crash_program, "%s", program);
    snprintf(crash_dir, sizeof crash_dir, "%s", dir ? dir : "");
    crash_save = save;
    /* (worked out now: a crashing program shouldn't have to) */
    snprintf(crash_head, sizeof crash_head, "%s %s, network protocol %d, rules %08x", program, OFP_VERSION, NET_PROTOCOL,
             (unsigned)net_rules());
#ifdef _WIN32
    SetUnhandledExceptionFilter(on_exception);
#else
    signal(SIGSEGV, on_signal);
    signal(SIGABRT, on_signal);
    signal(SIGFPE, on_signal);
    signal(SIGILL, on_signal);
#ifdef SIGBUS
    signal(SIGBUS, on_signal);
#endif
#endif
}
