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
#include <cstdio>

/**
 * The host console (host_console.h, from the host directory of the build) provides:
 *   conctrl_init()        get the console ready, once at start
 *   conctrl_putc(c)       write a character
 *   conctrl_status()      0FFh if a key is waiting, 0 otherwise
 *   conctrl_kbhit()       non zero if a key is waiting (conctrl_status() may hide piped input)
 *   conctrl_getch()       a key if one is waiting, 0 otherwise
 *   conctrl_getch_wait()  wait for a key
 * This file turns the Kaypro / ADM-3A codes of CP/M programs into ANSI ones.
 */
#include "host_console.h"

#define ESC ((char)0x1b)

inline void conctrl_puts(const char *s) {
    while (*s) conctrl_putc(*s++);
}

// ESC then s
inline void conctrl_esc(const char *s) {
    conctrl_putc(ESC);
    conctrl_puts(s);
}

inline void conctrl_save_cursor_pos() {
    conctrl_esc("[s");
}

inline void conctrl_restore_cursor_pos() {
    conctrl_esc("[u");
}

inline void conctrl_mono_mode() {
    // ESC[=0h	40 x 25 monochrome (text)
    conctrl_esc("[=0h");
}

inline void conctrl_mono80_mode() {
    // ESC[=2h	80 x 25 monochrome (text)
    conctrl_esc("[=2h");
}

inline void conctrl_home() {
    conctrl_esc("[H");
}

inline void conctrl_cls(){
    conctrl_esc("[2J"); conctrl_esc("[H");
}

inline void conctrl_black_text_on_white() {
    conctrl_esc("[0m");
    conctrl_esc("[7m");
}

inline void conctrl_white_text_on_black() {
    conctrl_esc("[0m");
}

// row & col from 0, ANSI counts from 1
inline void conctrl_move_cursor(const uint8_t row, const uint8_t col) {
    char s[12];
    snprintf(s, sizeof(s), "[%u;%uH", unsigned(row) + 1, unsigned(col) + 1);
    conctrl_esc(s);
}

inline void conctrl_clear_to_end_of_line() {
    conctrl_esc("[0K");
}

inline void conctrl_clear_to_end_of_screen() {
    conctrl_esc("[0J");
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
        conctrl_putc(conb[i]);
    }
}

inline void console_exeb() {

    switch(conb[1]) {
// ESC 0
// disable the status line (containing the clock).
// ESC 1
// enable the status line.
    case 'A': { // ESC A - cursor up, unless at the top
        conctrl_esc("[A");
        break;
    }
    case 'B':   // ESC B n - Kaypro: set attribute n
    case 'C': { // ESC C n - Kaypro: clear attribute n
        const bool set = conb[1] == 'B';
        switch(conb[2]) {
            case '0': conctrl_esc(set ? "[7m" : "[27m"); break;     // reverse video
            case '1': conctrl_esc(set ? "[2m" : "[22m"); break;     // half intensity
            case '2': conctrl_esc(set ? "[5m" : "[25m"); break;     // blinking
            case '3': conctrl_esc(set ? "[4m" : "[24m"); break;     // underline
            case '4': conctrl_esc(set ? "[?25h" : "[?25l"); break;  // cursor on/off
            default: break;
        }
        break;
    }
    case 'D': { // ESC D - cursor left, unless at the left-hand edge
        conctrl_esc("[D");
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
        conctrl_esc("M");
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
        conctrl_esc("[L");
        break;
    }
    case 'M': { // ESC M - delete current line
        conctrl_esc("[M");
        break;
    }
    case 'N': { // ESC N - delete character at cursor position
        conctrl_esc("[P");
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
                conctrl_putc(a);
                break;
            }
            case 0x0b: { // Cursor up ....................  0Bh
                conctrl_esc("[A");
                break;
            }
            case 0x0c: { // Cursor right .................  0Ch
                conctrl_esc("[C");
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
                conctrl_putc(a);
        //    if (a < 32) std::cout << '[' << std::hex << (unsigned)a << ']';
                break;
            }
        }
    }
}
