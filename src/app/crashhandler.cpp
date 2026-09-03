/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "app/crashhandler.h"

#ifdef _WIN32
void installCrashHandler() {}   // Windows story arrives with the port (SEH/breakpad)
#else

#include <csignal>
#include <initializer_list>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

// Everything in the handler must be async-signal-safe: write(), backtrace's
// *_fd variant, no malloc, no Qt, no iostream, no locale.
void writeStr(int fd, const char *s) { (void)!write(fd, s, strlen(s)); }

void writeNum(int fd, unsigned long v, int base) {
    char buf[24]; int i = 23; buf[i--] = 0;
    if (v == 0) buf[i--] = '0';
    while (v && i >= 0) {
        unsigned d = v % base;
        buf[i--] = d < 10 ? char('0' + d) : char('a' + d - 10);
        v /= base;
    }
    writeStr(fd, &buf[i + 1]);
}

struct sigaction gPrev[32];

void handler(int sig, siginfo_t *info, void *) {
    // File name built without snprintf's locale machinery.
    char path[64];
    strcpy(path, "crash-");
    int n = int(strlen(path));
    unsigned long t = (unsigned long)time(nullptr);
    char tail[32]; int i = 31; tail[i--] = 0;
    if (t == 0) tail[i--] = '0';
    while (t && i >= 0) { tail[i--] = char('0' + t % 10); t /= 10; }
    strcpy(path + n, &tail[i + 1]);
    strcat(path, ".log");

    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        writeStr(fd, "Jahshaka crash\nsignal: ");
        writeNum(fd, (unsigned long)sig, 10);
        writeStr(fd, " (");
        switch (sig) {
        case SIGSEGV: writeStr(fd, "SIGSEGV"); break;
        case SIGABRT: writeStr(fd, "SIGABRT"); break;
        case SIGBUS:  writeStr(fd, "SIGBUS");  break;
        case SIGFPE:  writeStr(fd, "SIGFPE");  break;
        case SIGILL:  writeStr(fd, "SIGILL");  break;
        default:      writeStr(fd, "?");       break;
        }
        writeStr(fd, ")\nfault address: 0x");
        writeNum(fd, (unsigned long)(info ? (unsigned long)info->si_addr : 0), 16);
        writeStr(fd, "\n\nbacktrace (decode: addr2line -Cfe ./Jahshaka <addr>, or\n"
                     "gdb ./Jahshaka -ex 'info symbol <addr>'):\n");
        void *frames[64];
        int count = backtrace(frames, 64);
        backtrace_symbols_fd(frames, count, fd);
        writeStr(fd, "\nsee jahshaka-ogre.log in the same directory for the session tail\n");
        close(fd);

        // Mirror a one-liner to stderr so terminal runs see it immediately.
        writeStr(2, "\n*** Jahshaka crashed — backtrace written to ");
        writeStr(2, path);
        writeStr(2, " ***\n");
    }

    // Restore and re-raise: the default action (core dump / abort) and any
    // chained handler (future breakpad) still run.
    if (sig >= 0 && sig < 32) sigaction(sig, &gPrev[sig], nullptr);
    raise(sig);
}

}  // namespace

void installCrashHandler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int sig : { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL })
        sigaction(sig, &sa, &gPrev[sig]);
}
#endif
