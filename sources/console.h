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

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>

inline void conctrl_save_cursor_pos();
inline void conctrl_restore_cursor_pos();
inline void conctrl_mono_mode();
inline void conctrl_mono80_mode();
inline void conctrl_home();
inline void conctrl_cls();
inline void conctrl_clear_to_end_of_line();
inline void conctrl_clear_to_end_of_screen();
inline void conctrl_black_text_on_white();
inline void conctrl_white_text_on_black();
inline void conctrl_move_cursor(const uint8_t x, const uint8_t y);
inline int conctrl_kbhit();
inline int conctrl_getch();
inline int conctrl_getch_wait();

#define ESC ((char)0x1b)

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
 * Nothing is done when stdin is not a terminal (e.g. piped input).
 */
inline void conctrl_init() {
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &conctrl_saved_termios)) return;
    struct termios raw = conctrl_saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(ICRNL | IXON);   // RETURN gives CR, ^S/^Q reach the program
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw)) return;
    conctrl_raw = true;
    std::atexit(conctrl_restore);
    std::signal(SIGINT, conctrl_signal);
    std::signal(SIGTERM, conctrl_signal);
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

/**
 * Wait for a key. Leaves the emulator when the input is exhausted.
 */
inline int conctrl_getch_wait(void) {
    std::cout.flush();
    const int c = getchar();
    switch(c) {
        case EOF: std::cout << std::endl; std::exit(EXIT_SUCCESS);
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

inline void conctrl_save_cursor_pos() {
    std::cout << ESC << "[s";
}

inline void conctrl_restore_cursor_pos() {
    std::cout << ESC << "[u";
}

inline void conctrl_mono_mode() {
    // ESC[=0h	40 x 25 monochrome (text)
    std::cout << ESC << "[=0h";
}

inline void conctrl_mono80_mode() {
    // ESC[=2h	80 x 25 monochrome (text)
    std::cout << ESC << "[=2h";
}

inline void conctrl_home() {
    std::cout << ESC << "[H";
}

inline void conctrl_cls(){
    std::cout << ESC << "[2J" << ESC << "[H";
}

inline void conctrl_black_text_on_white() {
    std::cout << ESC << "[0m";
    std::cout << ESC << "[7m";
}

inline void conctrl_white_text_on_black() {
    std::cout << ESC << "[0m";
}

// row & col from 0, ANSI counts from 1
inline void conctrl_move_cursor(const uint8_t row, const uint8_t col) {
    std::cout << ESC << "[" << (unsigned)row + 1 << ';' <<  (unsigned)col + 1 << 'H';
}

inline void conctrl_clear_to_end_of_line() {
    std::cout << ESC << "[0K";
}

inline void conctrl_clear_to_end_of_screen() {
    std::cout << ESC << "[0J";
}

// See https://www.seasip.info/Cpm/cpm86esc.html	
//     https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
//     https://mdfs.net/Archive/info-cpm/1985/01/19/053100.htm
//     https://www.cse.psu.edu/~kxc104/class/cmpen472/16f/hw/hw8/vt100ansi.htm#:~:text=Cursor%20Home%20%5B%7B,upper%20left%20of%20the%20screen.
//
// Buffer console output so we can execute control sequences
// TODO need a way to reset these
inline uint8_t conb[4];
inline unsigned coni = 0;

inline void console_reset() {
    coni = 0; 
}

inline unsigned console_el() {
    switch(conb[1]) {
        case 'Y':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
        case 'l':
        case '=':
            return 4;
        case 'a': 
        case 'b':
        case 'c':
        case 'B':
        case 'C':
            return 3;
        default: return 2;
    }
}

// Unknown sequence: pass it through untouched
inline void console_output_buf() {
    for(unsigned i = 0; i < coni; ++i) {
        std::cout << (char)conb[i];
    }
}

inline void console_exeb() {

    switch(conb[1]) {
// ESC 0
// disable the status line (containing the clock).
// ESC 1
// enable the status line.
    case 'A': { // ESC A - cursor up, unless at the top
        std::cout << ESC << "[A";
        break;
    }
    case 'B':   // ESC B n - Kaypro: set attribute n
    case 'C': { // ESC C n - Kaypro: clear attribute n
        const bool set = conb[1] == 'B';
        switch(conb[2]) {
            case '0': std::cout << ESC << (set ? "[7m" : "[27m"); break;     // reverse video
            case '1': std::cout << ESC << (set ? "[2m" : "[22m"); break;     // half intensity
            case '2': std::cout << ESC << (set ? "[5m" : "[25m"); break;     // blinking
            case '3': std::cout << ESC << (set ? "[4m" : "[24m"); break;     // underline
            case '4': std::cout << ESC << (set ? "[?25h" : "[?25l"); break;  // cursor on/off
            default: break;
        }
        break;
    }
    case 'D': { // ESC D - cursor left, unless at the left-hand edge
        std::cout << ESC << "[D";
        break;
    }
    case 'E': { // ESC E - clear screen and home cursor
        conctrl_cls();
        break;
    }
    case 'H': { // ESC H - home cursor
        conctrl_home();
        break;
    }
    case 'I': { // ESC I - cursor up, scroll down if at top
        std::cout << ESC << 'M';
        break;
    }
    case 'J': { // ESC J - delete to end of screen
        conctrl_clear_to_end_of_screen();
        break;
    }
    case 'K': { // ESC K - delete to end of line
        conctrl_clear_to_end_of_line();
        break;
    }
    case 'L': { // ESC L - insert a line
        std::cout << ESC << "[L";
        break;
    }
    case 'M': { // ESC M - delete current line
        std::cout << ESC << "[M";
        break;
    }
    case 'N': { // ESC N - delete character at cursor position
        std::cout << ESC << "[P";
        break;
    }
    case 'Y': case '=': { // ESC Y r c - Move cursor to row r-32, column c-32
        conctrl_move_cursor(
            conb[2] - 32,
            conb[3] - 32
        );
        break;
    }
// ESC a m
// Select screen mode m
// ESC b c
// Set foreground colour c
// ESC c c
// Set background colour c
// ESC d h l
// Redirect CONIN
// ESC e h l
// Redirect CONOUT
// ESC f h l
// Redirect AUXIN
// ESC g h l
// Redirect AUXOUT
// ESC h h l
// Redirect LST
// All redirections combine h and l to form a device number:

//                  0, 1 = CRT
//                  2,3  = COM1-2
//                  4,5,6= LPT1-3
//                  7    = CRT
//                  8    = light pen
//                  9,10 = CRT
	case 'j': { // ESC j - Save cursor position
        conctrl_save_cursor_pos();
        break;
    }
    case 'k': { // ESC k - Restore cursor position
        conctrl_restore_cursor_pos();
        break;
    }
// ESC l
// Erase all characters on the current line
// ESC m
// Enable cursor
// ESC n
// Disable cursor
// ESC o
// Erase characters up to the cursor position


    case 'p': { // ESC p - Select black text on white background
        conctrl_black_text_on_white();
        break;
    }
    case 'q': { // ESC q - Select white text on black background
        conctrl_white_text_on_black();
        break;
    }
// ESC r
// Select bright text
// ESC s
// Select flashing text
// ESC t
// Cancel flashing text
// ESC u
// Cancel bright text
// ESC v
// Allow wrapping at end of line
// ESC w
// Do not wrap at end of line
    case 'x': { // ESC x - Mode MONO
        conctrl_mono_mode();
        break;
    }
    case 'y': { // ESC y - Mode CO80
        conctrl_mono80_mode();
        break;
    }


// ESC / bh bl
// Call INT 10h, function 0Bh. ie:
// ESC / 00h n - set border
// ESC / 01h n - set CGA palette

        default: {
            console_output_buf();
            break;
        }
    }

    coni = 0;
}

inline void console_chkb() {
    if (coni >= 2 && conb[0] == 0x1b) {
        unsigned el = console_el();
        if (coni >= el) {
            console_exeb();
        }
    }
}

inline void console_pushc(const uint8_t c) {
    if (coni < sizeof(conb)) {
        conb[coni++] = c;
    }
    console_chkb();
}

inline void console_putc(const uint8_t c) {
    const uint8_t a = c & 127; // remove parity?
    if (a == 0x1b || coni > 0) {
        console_pushc(a);
    }
    else {
//         Cursor left (bs) .............  08h     08
//         Cursor right .................  0Ch     12
//         Cursor down (lf) .............  0Ah     10
//         Cursor up ....................  0Bh     12
//         Home cursor ..................  1Eh     30
//         Clear screen & home cursor ...  1Ah     26
//         Carriage return ..............  0Dh     13 
//         Clear EOL (Ctl-X) ............  18h     24
//         Clear EOS (Ctl-W) ............  17h     23
        switch(a) {
            case 0x08:
            case 0x0a: {
                putchar(a);
                break;
            }
            case 0x0b: { // Cursor up ....................  0Bh
                std::cout << ESC << "[A";
                break;
            }
            case 0x0c: { // Cursor right .................  0Ch
                std::cout << ESC << "[C";
                break;
            }
            case 0x17: { // Clear EOS (Ctl-W) ............  17h     23
                conctrl_clear_to_end_of_screen();
                break;
            }
            case 0x18: { // Clear EOL (Ctl-X) ............  18h     24
                conctrl_clear_to_end_of_line();
                break;
            }            
            case 0x1a: { // Clear screen & home cursor ...  1Ah     26
                conctrl_cls();
                break;
            }
            case 0x1e: { // Home cursor ..................  1Eh     30
                conctrl_home();
                break;
            }
            default: {
                putchar(a);
        //    if (a < 32) std::cout << '[' << std::hex << (unsigned)a << ']';
                break;
            }
        }
    }
}
