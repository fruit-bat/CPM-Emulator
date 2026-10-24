/**
 * Copyright 2024 fruit-bat
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

/**
 * Host console for the emulator: the terminal it runs in, through stdin & stdout.
 */

#include <cstdlib>
#include <csignal>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>

// Escape key for emulator commands (like telnet): Ctrl-] q leaves the emulator,
// Ctrl-] Ctrl-] sends a Ctrl-]
#define CONCTRL_CMD 0x1d

// Terminal settings saved by conctrl_init(), restored on exit
inline struct termios conctrl_saved_termios;
inline bool conctrl_raw = false;

inline void conctrl_restore() {
    if (conctrl_raw) {
        tcsetattr(STDIN_FILENO, TCSANOW, &conctrl_saved_termios);
        conctrl_raw = false;
    }
}

inline void conctrl_signal(int sig) {
    conctrl_restore();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

/**
 * Put the terminal in non-canonical mode without echo, so that keys are seen
 * as soon as they are typed (CP/M programs do their own echo).
 * Signal keys are turned off: ^C, ^Z... belong to CP/M.
 * Nothing is done when stdin is not a terminal (e.g. piped input).
 */
inline void conctrl_init() {
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &conctrl_saved_termios)) return;
    struct termios raw = conctrl_saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(ICRNL | IXON);   // RETURN gives CR, ^S/^Q reach the program
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw)) return;
    conctrl_raw = true;
    std::atexit(conctrl_restore);
    std::signal(SIGINT, conctrl_signal);
    std::signal(SIGTERM, conctrl_signal);
    std::cout << "[CP/M emulator: Ctrl-] q to quit]" << std::endl;
}

inline void conctrl_putc(const char c) {
    putchar(c);
}

inline int conctrl_kbhit(void) {
	int characters_buffered = 0;
	ioctl(STDIN_FILENO, FIONREAD, &characters_buffered);
	return characters_buffered;
}

inline bool conctrl_tty() {
    static const bool tty = isatty(STDIN_FILENO);
    return tty;
}

/**
 * Console status (BDOS 11 / BIOS CONST). Programs poll it to stop on a key
 * press, which would eat piped input: then it never reports a key.
 */
inline int conctrl_status(void) {
    return conctrl_tty() && conctrl_kbhit() ? 0xFF : 0x00;
}

inline void conctrl_quit() {
    fputs("\033[0m\033[?25h\n", stdout);
    std::exit(EXIT_SUCCESS);
}

/**
 * Wait for a key. Leaves the emulator when the input is exhausted,
 * or on Ctrl-] q at a terminal.
 */
inline int conctrl_getch_wait(void) {
    fflush(stdout);
    int c = getchar();
    if (c == CONCTRL_CMD && conctrl_tty()) {
        c = getchar();
        if (c == 'q' || c == 'Q') conctrl_quit();
    }
    switch(c) {
        case EOF: putchar('\n'); std::exit(EXIT_SUCCESS);
        case 10: return 13;
        default: return c;
    }
}

/**
 * Return a key if one is waiting, 0 otherwise.
 * Piped input is all typed ahead: wait for it, so that its end is seen.
 */
inline int conctrl_getch(void) {
	if (conctrl_tty() && !conctrl_kbhit()) return 0;
    return conctrl_getch_wait();
}
