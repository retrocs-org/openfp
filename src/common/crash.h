/* SPDX-License-Identifier: MIT
 * If the game or the server crashes: crash-DATE.txt in dir, with the version, the rules
 * fingerprint, the signal and a backtrace of where it was (Linux, macOS; Windows: the
 * exception and its address), and then save(path) gets a chance to keep the match's
 * replay so far (crash-DATE.ofprep). Then the crash goes on as it would have. Best
 * effort: whatever a crashing program can still do. */
#ifndef OFP_CRASH_H
#define OFP_CRASH_H

void crash_install(const char *program, const char *dir, void (*save)(const char *path));

#endif
