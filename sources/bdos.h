/**
 * Copyright 2021 Marc SIBERT
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
 *
 * See http://www.gaby.de/cpm/manuals/archive/cpm22htm/ch5.htm
 */

#pragma once

#include <cassert>
#include <cctype>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include "console.h"

#if __cplusplus < 201703L
#error "Need C++17 compiler for using <filesystem>"
#endif
#include <filesystem>

// #define LOG 1

/**
 * CP/M File Control Block
 * The File Control Block is a 36-byte data structure (33 bytes in CP/M 1).
 * ---
 * CR = current record,   ie (file pointer % 16384)  / 128
 * EX = current extent,   ie (file pointer % 524288) / 16384
 * S2 = extent high byte, ie (file pointer / 524288). The CP/M Plus source code refers to this use of the S2 byte as 'module number'.
 */
struct __attribute__ ((packed)) FCB_t {
// Drive. 0 for default, 1-16 for A-P.
	uint8_t DR;
/**
 * Filename, 7-bit ASCII. The top bits of the filename bytes (usually referred to as F1' to F8') have the following meanings:
 *   F1'-F4' - User-defined attributes. Any program can use them in any way it likes. The filename in the disc directory has the corresponding bits set.
 *   F5'-F8' - Interface attributes. They modify the behaviour of various BDOS functions or indicate error conditions. In the directory these bits are always zero.
 */
	char filename[8];
/**
 * Filetype, 7-bit ASCII. T1' to T3' have the following meanings:
 *   T1' - Read-Only.
 *   T2' - System (hidden). System files in user 0 can be opened from other user areas.
 *   T3' - Archive. Set if the file has not been changed since it was last copied.
 */
	char filetype[3];
// Set this to 0 when opening a file and then leave it to CP/M. You can rewind a file by setting EX, RC, S2 and CR to 0.
	uint8_t EX;
// Reserved.
	uint8_t S1;
// Reserved.
	uint8_t S2;
// Set this to 0 when opening a file and then leave it to CP/M.
	uint8_t RC;
// Image of the second half of the directory entry, containing the file's allocation (which disc blocks it owns).
	uint8_t AL[16];
// Current record within extent. It is usually best to set this to 0 immediately after a file has been opened and then ignore it.
	uint8_t CR;
// Random access record number (not CP/M 1). A 16-bit value in CP/M 2 (with R2 used for overflow); an 18-bit value in CP/M 3.
	uint8_t R[3];
};

static_assert(sizeof(FCB_t) == 36, "FCB must be 36 bytes");

/**
 * Emulated BDOS. Disks A: to P: are the host directories "A" to "P" in the
 * current directory; files are host files with a 8.3 name.
 *
 * The BDOS area (BDOS_ADDR) holds, after the 6 bytes signature, a fake Disk
 * Parameter Block and allocation vector (0x120 bytes in all) for programs
 * like STAT that look at them.
 */
template <unsigned MEMORY_SIZE, uint16_t BDOS_ADDR>
class BDos {
public:
/**
 * Cold boot: set page zero, the BDOS signature and the disk parameters.
 */
	void init(uint8_t *const memory) {
		memory[IOBYTE] = 0x00;			// Default IOBYTE
		memory[USER_DRIVE] = 0x00;		// User 0, drive A
		vectors(memory);

	// Disk Parameter Block: 2K blocks, 4M disk, 512 directory entries
		static const uint8_t dpb[] = {
			64, 0,			// SPT - sectors per track
			4, 15, 0,		// BSH, BLM, EXM
			0xFF, 0x07,		// DSM - last block number (2047)
			0xFF, 0x01,		// DRM - last directory entry (511)
			0xFF, 0x00,		// AL0, AL1 - blocks used by the directory
			0, 0,			// CKS - not removable
			0, 0			// OFF - no system track
		};
		memcpy(memory + DPB_ADDR, dpb, sizeof(dpb));
		memset(memory + ALV_ADDR, 0, ALV_SIZE);
		memory[ALV_ADDR] = 0xFF;				// Directory blocks
	}

/**
 * Warm boot: restore the BDOS entry, close all files and reset the DMA.
 */
	void reset(uint8_t *const memory) {
		vectors(memory);
		for (auto& f : files) release(f);
		found.clear();
		dma = DEFAULT_DMA;
	}

/**
 * BDOS functions.
 * C register contains the function value.
 * @see http://www.gaby.de/cpm/manuals/archive/cpm22htm/ch5.htm
 */
	void function(Z80& cpu, uint8_t *const memory) {
		assert(memory);
		switch (Z80_C(cpu)) {
			case 0x00 : systemReset(cpu); break;
			case 0x01 : consoleInput(cpu); break;
			case 0x02 : consoleOutput(cpu); break;
			case 0x03 : readerInput(cpu); break;
			case 0x04 : punchOutput(cpu); break;
			case 0x05 : listOutput(cpu); break;
			case 0x06 : directConsoleIO(cpu); break;
			case 0x07 : getIOByte(cpu, memory); break;
			case 0x08 : setIOByte(cpu, memory); break;
			case 0x09 : printString(cpu, memory); break;
			case 0x0A : readConsoleBuffer(cpu, memory); break;
			case 0x0B : getConsoleStatus(cpu); break;
			case 0x0C : returnVersionNumber(cpu); break;
			case 0x0D : resetDiskSystem(cpu, memory); break;
			case 0x0E : selectDisk(cpu, memory); break;
			case 0x0F : openFile(cpu, memory); break;
			case 0x10 : closeFile(cpu, memory); break;
			case 0x11 : searchForFirst(cpu, memory); break;
			case 0x12 : searchForNext(cpu, memory); break;
			case 0x13 : deleteFile(cpu, memory); break;
			case 0x14 : readSequential(cpu, memory); break;
			case 0x15 : writeSequential(cpu, memory); break;
			case 0x16 : makeFile(cpu, memory); break;
			case 0x17 : renameFile(cpu, memory); break;
			case 0x18 : returnLogicVector(cpu); break;
			case 0x19 : returnCurrentDisk(cpu, memory); break;
			case 0x1A : setDMAAddress(cpu); break;
			case 0x1B : getAddrAlloc(cpu); break;
			case 0x1C : writeProtectDisk(cpu); break;
			case 0x1D : getROVector(cpu); break;
			case 0x1E : setFileAttributes(cpu, memory); break;
			case 0x1F : getAddrDiskParms(cpu); break;
			case 0x20 : setGetUserCode(cpu, memory); break;
			case 0x21 : readRandom(cpu, memory); break;
			case 0x22 : writeRandom(cpu, memory); break;
			case 0x23 : computeFileSize(cpu, memory); break;
			case 0x24 : setRandomRecord(cpu, memory); break;
			case 0x25 : resetDrive(cpu); break;
			case 0x28 : writeRandomWithZeroFill(cpu, memory); break;
			default:
	// Like CP/M 2.2, unknown functions (eg. CP/M 3 ones) return 0
#if LOG
				std::clog << "Register C: " << std::hex << std::setw(2) << std::setfill('0') << unsigned(Z80_C(cpu)) << "h";
				std::clog << ": Unknown BDOS function!" << std::endl;
#endif
				returnCode(cpu, 0);
				break;
		}
	}

protected:

/**
 * BDOS function 0 (P_TERMCPM) - System Reset
 * Supported by: CP/M 1, 2, 3; MP/M; Concurrent CP/M.
 * Entered with C=0. Does not return.
 * Quit the current program, return to command prompt. This call is hardly ever used in 8-bit CP/M since the RST 0 instruction does the same thing and saves four bytes.
 */
 	void systemReset(Z80& cpu) {
		Z80_PC(cpu) = 0x0000;
	}

/**
 * BDOS function 1 (C_READ) - Console input
 * Supported by: All versions
 * Entered with C=1. Returns A=L=character.
 * Wait for a character from the keyboard; then echo it to the screen and return it.
 */
	void consoleInput(Z80& cpu) {
		const auto c = conctrl_getch_wait();
		if (c >= ' ' || c == 0x0D || c == 0x0A || c == 0x08 || c == 0x09) console_putc(c);
		returnCode(cpu, c);
	}

/**
 * BDOS function 2 (C_WRITE) - Console output
 * Supported by: All versions
 * Entered with C=2, E=ASCII character.
 * Send the character in E to the screen. Tabs are expanded to spaces. Output can be paused with ^S and restarted with ^Q (or any key under versions prior to CP/M 3). While the output is paused, the program can be terminated with ^C.
 */
	void consoleOutput(Z80& cpu) {
 #ifdef LOG
		std::clog << "Write console ASCII " << unsigned(Z80_E(cpu)) << " (";
		switch(Z80_E(cpu)) {
			case 0x00 : std::clog << "NUL"; break;
			case 0x0a : std::clog << "LF"; break;
			case 0x0d : std::clog << "CR"; break;
			default:
				std::clog << char(Z80_E(cpu));
				break;
		}
		std::clog << ")" <<  std::endl;
#endif
		unsigned e = Z80_E(cpu);
		if (e) console_putc(e);
		returnCode(cpu, 0);
	}

/**
 * BDOS function 3 (A_READ) - Auxiliary (Reader) input
 * Supported by: All CP/M versions except MP/M and Concurrent CP/M
 * Entered with C=3. Returns A=L=ASCII character
 * Note that this call can hang if the auxiliary input never sends data.
 * No reader is emulated: always returns ^Z (end of file).
 */
	void readerInput(Z80& cpu) {
		returnCode(cpu, 0x1A);
	}

/**
 * BDOS function 4 (A_WRITE) - Auxiliary (Punch) output
 * Supported by: All versions except MP/M and Concurrent CP/M.
 * Entered with C=4, E=ASCII character.
 * If the device is permanently not ready, this call can hang.
 * No punch is emulated: the character is discarded.
 */
	void punchOutput(Z80& cpu) {
		returnCode(cpu, 0);
	}

/**
 * BDOS function 5 (L_WRITE) - Printer output
 * Supported by: All versions
 * Entered with C=2, E=ASCII character.
 * If the printer is permanently offline or busy, this call can hang.
 * No printer is emulated: the character is discarded.
 */
	void listOutput(Z80& cpu) {
		returnCode(cpu, 0);
	}

/**
 * BDOS function 6 (C_RAWIO) - Direct console I/O
 * Supported by: CP/M 1.4 and later, with variations
 * Entered with C=6, E=code. Returned values (in A) vary.
 *	E=0FFh
 *		Return a character without echoing if one is waiting; zero if none is available. In MP/M 1, this works like E=0FDh below and waits for a character.
 *	E=0FEh
 *		[CP/M3, NovaDOS, Z80DOS, DOS+] Return console input status. Zero if no character is waiting, nonzero otherwise.
 *	E=0FDh
 *		[CP/M3, DOS+] Wait until a character is ready, return it without echoing.
 *	E=0FCh
 *		[DOS+] One-character lookahead - return the next character waiting but leave it in the buffer.
 * Values of E not supported on a particular system will output the character. Under CP/M 2 and lower, direct console functions may interact undesirably with non-direct ones, since certain buffers may be bypassed. Do not mix them.
 */
	void directConsoleIO(Z80& cpu) {
		const unsigned e = Z80_E(cpu);
		switch (e) {
			case 0xFF : returnCode(cpu, conctrl_getch()); break;
			case 0xFE : returnCode(cpu, conctrl_kbhit() ? 0xFF : 0x00); break;
			case 0xFD : returnCode(cpu, conctrl_getch_wait()); break;
			default:
				console_putc(e);
				returnCode(cpu, 0x00);	// ok
				break;
		}
	}

/**
 * BDOS function 7 - Get I/O byte
 * Supported by: CP/M 2 and lookalikes. Not supported in MP/M.
 * Entered with C=7. Returns I/O byte.
 * Here's a description of how the IOBYTE works. @see https://seasip.info/Cpm/iobyte.html
 */
	void getIOByte(Z80& cpu, const uint8_t memory[]) {
#if LOG
		std::clog << "Get I/O byte " << std::hex << unsigned(memory[IOBYTE]) << "h" << std::endl;
#endif
		returnCode(cpu, memory[IOBYTE]); // A reg
	}

/**
 * BDOS function 8 - Set I/O byte
 * Supported by: CP/M 2 and lookalikes. Not supported in MP/M.
 * Entered with C=8, E=I/O byte.
 * Here's a description of how the IOBYTE works.
 */
	void setIOByte(Z80& cpu, uint8_t memory[]) {
#if LOG
		std::clog << "Set I/O byte " << std::hex << unsigned(Z80_E(cpu)) << "h" << std::endl;
#endif
		memory[IOBYTE] = Z80_E(cpu);
		returnCode(cpu, 0);
	}

/**
 * BDOS function 9 (C_WRITESTR) - Output string
 * Supported by: All versions
 * Entered with C=9, DE=address of string.
 * Display a string of ASCII characters, terminated with the $ character. Thus the string may not contain $ characters - so, for example, the VT52 cursor positioning command ESC Y y+32 x+32 will not be able to use row 4.
 * Under CP/M 3 and above, the terminating character can be changed using BDOS function 110.
 */
	void printString(Z80& cpu, const uint8_t memory[]) const {
		assert(memory);
#if LOG
		std::clog << "Output string (Buffer " << std::hex << Z80_DE(cpu) << "h)" << std::endl;
#endif
		uint16_t addr = Z80_DE(cpu);
		for (auto n = 0U; n < MEMORY_SIZE * 1024U && memory[addr] != '$'; ++n) {
			console_putc(memory[addr++]);
		}
		returnCode(cpu, 0);
	}

/**
 * BDOS function 10 (C_READSTR) - Buffered console input
 * Supported by: All versions, with variations
 * Entered with C=0Ah, DE=address or zero.
 * This function reads characters from the keyboard into a memory buffer until RETURN is pressed. The Delete key is handled correctly. In later versions, more features can be used at this point; ZPM3 includes a full line editor with recall of previous lines typed.
 * On entry, DE is the address of a buffer. If DE=0 (in CP/M-86 versions DX=0FFFFh), the DMA address is used (CP/M 3 and later) and the buffer already contains data:
 * DE=address:                 DE=0 / DX=0FFFFh:
 * buffer: DEFB    size        buffer: DEFB    size
 *         DEFB    ?                   DEFB    len
 *         DEFB    bytes               DEFB    bytes
 * The value at buffer+0 is the amount of bytes available in the buffer. Once the limit has been reached, no more can be added, although the line editor can still be used.
 * If DE=0 (in 16-bit versions, DX=0FFFFh) the next byte contains the number of bytes already in the buffer; otherwise this is ignored. On return from the function, it contains the number of bytes present in the buffer.
 * The bytes typed then follow. There is no end marker.
 * Editing: BS/DEL erase a character, ^U/^X erase the line, ^C on an empty line warm boots.
 */
	void readConsoleBuffer(Z80& cpu, uint8_t memory[]) {
		assert(memory);
#if LOG
		std::clog << "Buffered console input (Buffer " << std::hex << Z80_DE(cpu) << "h)" << std::endl;
#endif
		const uint16_t buffer = Z80_DE(cpu);
		const unsigned max = memory[buffer];
		unsigned len = 0;
		auto at = [&](unsigned i) -> uint8_t& { return memory[uint16_t(buffer + 2 + i)]; };
		auto echo = [](uint8_t c) {
			if (c < ' ') {
				console_putc('^');
				console_putc(c + '@');
			} else console_putc(c);
		};
		auto erase = [&]() {
			const auto n = at(--len) < ' ' ? 2 : 1;
			for (auto i = 0; i < n; ++i) {
				console_putc(0x08);
				console_putc(' ');
				console_putc(0x08);
			}
		};
		while (true) {
			const int c = conctrl_getch_wait();
			if (c == 0x0D || c == 0x0A) {
				console_putc(0x0D);
				break;
			}
			if (c == 0x08 || c == 0x7F) {
				if (len) erase();
			} else if (c == 0x15 || c == 0x18) {
				while (len) erase();
			} else if (c == 0x03 && len == 0) {
				Z80_PC(cpu) = 0x0000;	// Warm boot
				return;
			} else if (len < max) {
				at(len++) = c;
				echo(c);
			}
		}
		memory[uint16_t(buffer + 1)] = len;
		returnCode(cpu, 0);
	}

/**
 * BDOS function 11 (C_STAT) - Console status
 * Supported by: All versions
 * Entered with C=0Bh. Returns A=L=status
 * Returns A=0 if no characters are waiting, nonzero if a character is waiting.
 */
	void getConsoleStatus(Z80& cpu) {
#if LOG
		std::clog << "Console status" << std::endl;
#endif
		returnCode(cpu, conctrl_status());
	}

/**
 * BDOS function 12 (S_BDOSVER) - Return version number
 * Supported by: Versions 2.0 and later
 * Entered with C=0Ch. Returns B=H=system type, A=L=version number.
 * The system type is subdivided into a machine type and a CP/M type. The machine type occupies the high nibble of the byte; the CP/M type is a bitmapped field stored in the low nibble.
 *	Machine types:		CP/M types:				Version numbers:
 *	0 - 8080			Bit 0 set for MP/M		00h - Version 1 (see Lift Head above)
 *	1 - 8086			Bit 1 set for CP/Net	20h - Version 2.0
 *	2 - 68000/Z8000		(ie:network present)	21h - Version 2.1
 *						Bit 2 set in 16-bit		22h - Version 2.2
 *						multi-user OSes			25h - Version 2.5 (DOS +)
 *												28h - Version 2.8 (Personal CP/M-80)
 *						For plain CP/M,			30h - Version 3.0 (MP/M II, MP/M-86)
 *						the CP/M type is 0.		31h - Version 3.1 (CP/M Plus)
 * 												33h - Version 3.3 (Apricot PCP/M-86)
 *												41h - Version 4.1 (DOSPLUS 1)
 *												50h - Version 5.0 (DOSPLUS 2)
 * Sources differ on the value returned by DOSPLUS 2. Most sources follow the Interrupt List and say 60h; my experiments on the real thing say 50h.
 * Confusingly, CP/M-86 v1.1 returns 0022h (ie, "8080 CP/M v2.2").
 * It is interesting to note that the version numbers returned by DRDOS and Novell DOS follow this system; DRDOS 3, 5 and 6 are version 6.x, Novell DOS 7 is version 7.2 and DR-OpenDOS is version 7.3. However these systems rather unsportingly fail to provide an INT 0E0h call to get the version number; you have to use INT 21h with AX=4452h.
 */
	void returnVersionNumber(Z80& cpu) {
#if LOG
		std::clog << "Version number CP/M 2.2" << std::endl;
#endif
		returnCode(cpu, 0x0022);	// hard coded CPM 2.2
	}

/**
 * BDOS function 13 (DRV_ALLRESET) - Reset discs
 * Supported by: All versions.
 * Entered with C=0Dh. Returned values vary.
 * Resets disc drives. Logs out all discs and empties disc buffers. Sets the currently selected drive to A:. Any drives set to Read-Only in software become Read-Write; replacement BDOSses tend to leave them Read-Only.
 * In versions 1 and 2, logs in drive A: and returns 0FFh if there is a file present whose name begins with a $, otherwise 0. Replacement BDOSses may modify this behaviour.
 * In multitasking versions, returns 0 if succeeded, or 0FFh if other processes have files open on removable or read-only drives.
 */
	void resetDiskSystem(Z80& cpu, uint8_t memory[]) {
		assert(memory);
#if LOG
		std::clog << "Reset drive ; default to A" << std::endl;
#endif
		memory[USER_DRIVE] &= 0xF0;	// DRIVE: 0 (A), user unchanged
		dma = DEFAULT_DMA;
	// 0FFh if a file name begins with '$' (the CCP then looks for $$$.SUB)
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(drivePath(0), ec)) {
			if (entry.path().filename().string()[0] == '$') {
				returnCode(cpu, 0xFF);
				return;
			}
		}
		returnCode(cpu, 0);
	}

/**
 * BDOS function 14 (DRV_SET) - Select disc
 * Supported by: All versions
 * Entered with C=0Eh, E=drive number. Returns L=A=0 or 0FFh.
 * The drive number passed to this routine is 0 for A:, 1 for B: up to 15 for P:.
 * Sets the currently selected drive to the drive in A; logs in the disc. Returns 0 if successful or 0FFh if error. Under MP/M II and later versions, H can contain a physical error number.
 */
 	void selectDisk(Z80& cpu, uint8_t memory[]) {
		assert(memory);
#if LOG
		std::clog << "Select disc to " << char('A' + Z80_E(cpu)) << std::endl;
#endif
		if (Z80_E(cpu) > 15) {
			std::cerr << ">> Invalid disk (A-P only)!" << std::endl;
			returnCode(cpu, 0xFF);
			return;
		}

		if (std::filesystem::is_directory(drivePath(Z80_E(cpu)))) {
			memory[USER_DRIVE] = (memory[USER_DRIVE] & 0xF0) | (Z80_E(cpu) & 0x0F);
			returnCode(cpu, 0x00);
			return;
		}
		std::cerr << ">> Path '" << char('A' + Z80_E(cpu)) << "/' do not exists!" << std::endl;
		returnCode(cpu, 0xFF);
	}

/**
 * BDOS function 15 (F_OPEN) - Open file
 * Supported by: All versions
 * Entered with C=0Fh, DE=FCB address. Returns error codes in BA and HL.
 * This function opens a file to read or read/write. The FCB is a 36-byte data structure, most of which is maintained by CP/M. Look here for details.
 * The FCB should have its DR, Fn and Tn fields filled in, and the four fields EX, S1, S2 and RC set to zero. Under CP/M 3 and later, if CR is set to 0FFh then on return CR will contain the last record byte count. Note that CR should normally be reset to zero if sequential access is to be used.
 * Under MP/M II, the file is normally opened exclusively - no other process can access it. Two bits in the FCB control the mode the file is opened in:
 *  * F5' - set to 1 for "unlocked" mode - other programs can use the file.
 *  * F6' - set to 1 to open the file in read-only mode - other programs can use the file read-only. If both F6' and F5' are set, F6' wins.
 * If the file is opened in "unlocked" mode, the file's identifier (used for record locking) will be returned at FCB+21h.
 * Under MP/M II and later versions, a password can be supplied to this function by pointing the DMA address at the password.
 * On return from this function, A is 0FFh for error, or 0-3 for success. Some versions (including CP/M 3) always return zero; others return 0-3 to indicate that an image of the directory entry is to be found at (80h+20h*A).
 * If A=0FFh, CP/M 3 returns a hardware error in H and B. It also sets some bits in the FCB:
 *  * F7' is set if the file is read-only because writing is password protected and no password was supplied;
 *  * F8' is set if the file is read-only because it is a User 0 system file opened from another user area.
 */
	void openFile(Z80& cpu, uint8_t memory[]) {
		assert(memory);
		FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
		const auto path = hostPath(pFCB, memory);

#if LOG
		std::clog << "Open file " << path << " (FCB: "
				  << std::hex << unsigned(Z80_DE(cpu)) << "h) "
				  << std::endl;
#endif

		std::error_code ec;
		OpenFile *const pFile = std::filesystem::is_regular_file(path, ec)
			? openStream(Z80_DE(cpu), pFCB, memory, path)
			: nullptr;
		if (!pFile) {
			if (auto *const p = findStream(Z80_DE(cpu))) release(*p);
			returnCode(cpu, 0xFF);
			return;
		}
		pFCB->RC = extentRecords(*pFile, fcbRecord(pFCB));
		returnCode(cpu, 0x00);
	}

/**
 * BDOS function 16 (F_CLOSE) - Close file
 * Supported by: All versions
 * Entered with C=10h, DE=FCB address. Returns error codes in BA and HL.
 * This function closes a file, and writes any pending data. This function should always be used when a file has been written to.
 * On return from this function, A is 0FFh for error, or 0-3 for success. Some versions always return zero; others return 0-3 to indicate that an image of the directory entry is to be found at (80h+20h*A).
 * Under CP/M 3, if F5' is set to 1 then the pending data are written and the file is made consistent, but it remains open.
 * If A=0FFh, CP/M 3 returns a hardware error in H and B.
 */
 	void closeFile(Z80& cpu, uint8_t memory[]) {
		assert(memory);
#if LOG
		std::clog << "Close file (FCB: " << std::hex << unsigned(Z80_DE(cpu)) << "h)" << std::endl;
#endif
		const FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
		const auto path = hostPath(pFCB, memory);
		if (auto *const pFile = findStream(Z80_DE(cpu))) release(*pFile);
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec)) {
			returnCode(cpu, 0xFF);	// KO
			return;
		}
	// Closing writes RC to the directory: a lower RC in the last extent
	// truncates the file (the CCP does so to consume $$$.SUB).
		const uint32_t records = (std::filesystem::file_size(path, ec) + SECTOR_SIZE - 1) / SECTOR_SIZE;
		const uint32_t first = fcbRecord(pFCB) - fcbRecord(pFCB) % RECORDS_PER_EXTENT;
		if (!ec && records > first && records <= first + RECORDS_PER_EXTENT && pFCB->RC < records - first) {
			std::filesystem::resize_file(path, uintmax_t(first + pFCB->RC) * SECTOR_SIZE, ec);
		}
		returnCode(cpu, ec ? 0xFF : 0x00);
	}

/**
 * BDOS function 17 (F_SFIRST) - search for first
 * Supported by: All versions
 * Entered with C=11h, DE=address of FCB. Returns error codes in BA and HL.
 * Search for the first occurrence of the specified file; the filename should be stored in the supplied FCB. The filename can include ? marks, which match anything on disc. If the first byte of the FCB is ?, then any directory entry (including disc labels, date stamps etc.) will match. The EX byte is also checked; normally it should be set to zero, but if it is set to ? then all suitable extents are matched.
 * Returns A=0FFh if error (CP/M 3 returns a hardware error in H and B), or A=0-3 if successful. The value returned can be used to calculate the address of a memory image of the directory entry; it is to be found at DMA+A*32.
 * Under CP/M-86 v4, if the first byte of the FCB is '?' or bit 7 of the byte is set, subdirectories as well as files will be returned by this search.
 * The host directory is read once here; entries are then returned in name order.
 */
	void searchForFirst(Z80& cpu, uint8_t memory[]) {
		assert(memory);
		const FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
#if LOG
		std::clog << "Search for first (FCB: " << std::hex << unsigned(Z80_DE(cpu)) << "h)" << std::endl;
#endif
		char filter[11];
		fcbName(pFCB, filter);
		if (pFCB->DR == '?') memset(filter, '?', sizeof(filter));
		const bool allExtents = pFCB->DR == '?' || pFCB->EX == '?';
		const uint32_t wanted = (pFCB->S2 & 0x7F) * EXTENTS_PER_MODULE + (pFCB->EX & 0x1F);

		found.clear();
		nextFound = 0;
		const auto path = drivePath(fcbDrive(pFCB, memory));
		std::error_code ec;
		std::filesystem::directory_iterator di(path, ec);
		if (ec) {
			std::cerr << ">> Error looking for path '" << path.string() << "': " << ec.message() << "!" << std::endl;
			returnCode(cpu, 0xFF);	// KO
			return;
		}
		for (const auto& entry : di) {
			Entry e;
			if (entry.is_regular_file(ec) &&
				filenameDOS2CPM(entry.path().filename().string().c_str(), e.name) &&
				matches(filter, e.name)) {
			// One directory entry by 16K extent: all of them if EX is '?', else the one asked
				const uint32_t records = (entry.file_size(ec) + SECTOR_SIZE - 1) / SECTOR_SIZE;
				const uint32_t extents = records ? (records + RECORDS_PER_EXTENT - 1) / RECORDS_PER_EXTENT : 1;
				for (e.extent = 0; e.extent < extents; ++e.extent) {
					if (allExtents || e.extent == wanted) {
						e.records = std::min(records - e.extent * RECORDS_PER_EXTENT, RECORDS_PER_EXTENT);
						found.push_back(e);
					}
				}
			}
		}
		std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) {
			const auto c = memcmp(a.name, b.name, sizeof(a.name));
			return c ? c < 0 : a.extent < b.extent;
		});
		searchForNext(cpu, memory);
	}

/**
 * BDOS function 18 (F_SNEXT) - search for next
 * Supported by: All versions
 * Entered with C=12h, (DE=address of FCB)?. Returns error codes in BA and HL.
 * This function should only be executed immediately after function 17 or another invocation of function 18. No other disc access functions should have been used.
 * Function 18 behaves exactly as number 17, but finds the next occurrence of the specified file after the one returned last time. The FCB parameter is not documented, but Jim Lopushinsky states in LD301.DOC:
 * In none of the official Programmer's Guides for any version of CP/M does it say that an FCB is required for Search Next (function 18). However, if the FCB passed to Search First contains an unambiguous file reference (i.e. no question marks), then the Search Next function requires an FCB passed in reg DE (for CP/M-80) or DX (for CP/M-86).
 * The directory entry of the file extent is built at DMA.
 */
	void searchForNext(Z80& cpu, uint8_t memory[]) {
		assert(memory);
#if LOG
		std::clog << "Search for next (FCB: " << std::hex << unsigned(Z80_DE(cpu)) << "h)" << std::endl;
#endif
		if (nextFound >= found.size() || !dmaFits(DIR_ENTRY_SIZE)) {
			returnCode(cpu, 0xFF);	// KO
			return;
		}
		const Entry& e = found[nextFound++];
		const auto rc = e.records;

		uint8_t *const entry = memory + dma;
		memset(entry, 0, DIR_ENTRY_SIZE);
		entry[0] = memory[USER_DRIVE] >> 4;			// User number
		memcpy(entry + 1, e.name, sizeof(e.name));
		entry[12] = e.extent % EXTENTS_PER_MODULE;	// EX
		entry[14] = e.extent / EXTENTS_PER_MODULE;	// S2
		entry[15] = rc;								// RC
		for (auto b = 0U; b < (rc + RECORDS_PER_BLOCK - 1U) / RECORDS_PER_BLOCK; ++b) {
			entry[16 + 2 * b] = 1;					// Fake allocated blocks
		}
		returnCode(cpu, 0x00);	// OK
	}

/**
 * BDOS function 19 (F_DELETE) - delete file
 * Supported by: All versions
 * Entered with C=13h, DE=address of FCB. Returns error codes in BA and HL.
 * Deletes all directory entries matching the specified filename. The name can contain ? marks.
 * Returns A=0FFh if error, otherwise 0-3.
 */
 	void deleteFile(Z80& cpu, uint8_t *const memory) {
		const FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
#if LOG
		std::clog << "Delete file (FCB: " << std::hex << unsigned(Z80_DE(cpu)) << "h)" << std::endl;
#endif
		char filter[11];
		fcbName(pFCB, filter);

		const auto path = drivePath(fcbDrive(pFCB, memory));
		std::error_code ec;
		std::filesystem::directory_iterator di(path, ec);		// New directory_iterator
		if (ec) {
			std::cerr << ">> Error looking for path '" << path.string() << "': " << ec.message() << "!" << std::endl;
			returnCode(cpu, 0xFF);	// KO
			return;
		}

	// Collect first, so that the directory is not changed while scanning it
		std::vector<std::filesystem::path> victims;
		for (const auto& entry : di) {
			char name[11];
			if (entry.is_regular_file(ec) &&
				filenameDOS2CPM(entry.path().filename().string().c_str(), name) &&
				matches(filter, name)) {
				victims.push_back(entry.path());
			}
		}
		for (const auto& victim : victims) {
			releasePath(victim);
			std::filesystem::remove(victim, ec);
			if (ec) {
				std::cerr << ">> Error removing '" << victim.string() << "': " << ec.message() << "!" << std::endl;
				returnCode(cpu, 0xFF);	// KO
				return;
			}
		}
		returnCode(cpu, victims.empty() ? 0xFF : 0x00);	// one or more file removed
	}

/**
 * BDOS function 20 (F_READ) - read next record
 * Entered with C=14h, DE=address of FCB. Returns error codes in BA and HL.
 * Supported by: All versions
 * Load a record (normally 128 bytes, but under CP/M 3 this can be a multiple of 128 bytes) at the previously specified DMA address. Values returned in A are:
 *   0 OK,
 *   1 end of file,
 *   9 invalid FCB,
 *  10 (CP/M) media changed; (MP/M) FCB checksum error,
 *  11 (MP/M) unlocked file verification error,
 *  0FFh hardware error.
 * If on return A is not 0FFh, H contains the number of 128-byte records read before the error (MP/M II and later).
 * The record read is the one given by S2, EX & CR in the FCB.
 */
 	void readSequential(Z80& cpu, uint8_t memory[]) {
		assert(memory);
		FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
#if LOG
		std::clog << "Read next record (FCB: " << std::hex << unsigned(Z80_DE(cpu)) << "h)" << std::endl;
#endif
		if (!dmaFits(SECTOR_SIZE)) {
			std::cerr << ">> Writing DMA out of memory!" << std::endl;
			returnCode(cpu, 0xFF);
			return;
		}
		OpenFile *const pFile = stream(Z80_DE(cpu), pFCB, memory);
		if (!pFile) {
			returnCode(cpu, 0x01);	// EOF - No such file
			return;
		}
		const auto record = fcbRecord(pFCB);
		const auto result = readRecord(*pFile, record, memory + dma);
		if (!result) setFcbRecord(pFCB, record + 1, *pFile);
		returnCode(cpu, result);
	}

/**
 * BDOS function 21 (F_WRITE) - write next record
 * Supported by: All versions
 * Entered with C=15h, DE=address of FCB. Returns error codes in BA and HL.
 * Write a record (normally 128 bytes) from the DMA address to the file, at the position given by S2, EX & CR in the FCB.
 * Returns A=0 if OK, 1 if directory full, 2 if disc full, 0FFh if hardware error.
 */
	void writeSequential(Z80& cpu, uint8_t memory[]) {
		assert(memory);
		FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
#if LOG
		std::clog << "Write next record (FCB: " << std::hex << unsigned(Z80_DE(cpu)) << "h)" << std::endl;
#endif
		if (!dmaFits(SECTOR_SIZE)) {
			std::cerr << ">> Reading DMA out of memory!" << std::endl;
			returnCode(cpu, 0xFF);	// KO
			return;
		}
		OpenFile *const pFile = stream(Z80_DE(cpu), pFCB, memory);
		if (!pFile) {
			returnCode(cpu, 0xFF);	// KO - No such file
			return;
		}
		const auto record = fcbRecord(pFCB);
		const auto result = writeRecord(*pFile, record, memory + dma);
		if (!result) setFcbRecord(pFCB, record + 1, *pFile);
		returnCode(cpu, result);
	}

/**
 * BDOS function 22 (F_MAKE) - create file
 * Supported by: All versions
 * Entered with C=16h, DE=address of FCB. Returns error codes in BA and HL.
 * Creates the file specified. Returns A=0FFh if the directory is full.
 * If the file exists already, then the default action is to return to the command prompt, but CP/M 3 may return a hardware error instead.
 * Under MP/M II, set F5' to open the file in "unlocked" mode.
 * Under MP/M II and later versions, set F6' to create the file with a password; the DMA address should point at a 9-byte buffer:
 *  DEFS    8   ; Password
 *  DEFB    1   ; Password mode
 * CP/M 2.2 would make a second directory entry for an existing file; here it is emptied.
 */
	void makeFile(Z80& cpu, uint8_t memory[]) {
		assert(memory);
		FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
		const auto path = hostPath(pFCB, memory);
#if LOG
		std::clog << "Make file " << path << " (FCB: " << std::hex << unsigned(Z80_DE(cpu)) << "h)" << std::endl;
#endif
		releasePath(path);
		if (!openStream(Z80_DE(cpu), pFCB, memory, path, std::ios::trunc)) {
			std::cerr << ">> Error creating file '" << path.string() << "': " << strerror(errno) << "!" << std::endl;
			returnCode(cpu, 0xFF);
			return;
		}
		pFCB->RC = 0;
		returnCode(cpu, 0x00);
	}

/**
 * BDOS function 23 (F_RENAME) - Rename file
 * Supported by: All versions
 * Entered with C=17h, DE=address of FCB. Returns error codes in BA and HL.
 * Renames the file specified to the new name, stored at FCB+16. This function cannot rename across drives so the "drive" bytes of both filenames should be identical. Returns A=0-3 if successful; A=0FFh if error. Under CP/M 3, if H is zero then the file could not be found; if it is nonzero it contains a hardware error number.
 * Under Concurrent CP/M, set F5' if an extended lock on the file should be held through the rename. Otherwise the lock will be released.
 */
 	void renameFile(Z80& cpu, uint8_t memory[]) {
		assert(memory);
		const FCB_t *const pFCB1 = fcbAt(memory, Z80_DE(cpu));
		const FCB_t *const pFCB2 = fcbAt(memory, Z80_DE(cpu) + 16);
		const auto drive = fcbDrive(pFCB1, memory);
		char name1[11], name2[11];
		fcbName(pFCB1, name1);
		fcbName(pFCB2, name2);
		const auto from = hostPath(drive, name1);
		const auto to = hostPath(drive, name2);
#if LOG
		std::clog << "Rename file " << from << " to " << to << std::endl;
#endif
		std::error_code ec;
		if (!std::filesystem::is_regular_file(from, ec) || std::filesystem::exists(to, ec)) {
			returnCode(cpu, 0xFF);
			return;
		}
		releasePath(from);
		std::filesystem::rename(from, to, ec);
		if (ec) {
			std::cerr << ">> Error renaming '" << from.string() << "': " << ec.message() << "!" << std::endl;
			returnCode(cpu, 0xFF);
			return;
		}
		returnCode(cpu, 0x00);
	}

/**
 * BDOS function 24 (DRV_LOGINVEC) - Return bitmap of logged-in drives
 * Supported by: All versions
 * Entered with C=18h. Returns bitmap in HL.
 * Bit 7 of H corresponds to P: while bit 0 of L corresponds to A:. A bit is set if the corresponding drive is logged in.
 * In DOSPLUS v2.1, the three top bits (for the floating drives) will mirror the status of the corresponding host drives). This does not happen in earlier DOSPLUS / Personal CP/M-86 systems.
 */
 	void returnLogicVector(Z80& cpu) {
#if LOG
		std::clog << "Return Logic Vector (actives disks)" << std::endl;
#endif
		uint16_t actives = 0;
		for (auto d = 0; d <= 15; ++d) {
			std::error_code ec;
			if (std::filesystem::is_directory(drivePath(d), ec)) actives |= (1 << d);
		}
		returnCode(cpu, actives);
	}

/**
 * BDOS function 25 (DRV_GET) - Return current drive
 * Supported by: All versions
 * Entered with C=19h. Returns drive in A. Returns currently selected drive. 0 => A:, 1 => B: etc.
 */
 	void returnCurrentDisk(Z80& cpu, const uint8_t memory[]) {
		assert(memory);
#if LOG
		std::clog << "Get drive (" << char('A' + (memory[USER_DRIVE] & 0x0F)) << ')' << std::endl;
#endif
		returnCode(cpu, (memory[USER_DRIVE] & 0x0F));	// ok - drive numb.
	}

/**
 * BDOS function 26 (F_DMAOFF) - Set DMA address
 * Supported by: All versions
 * Entered with C=1Ah, DE=address.
 * Set the Direct Memory Access address; a pointer to where CP/M should read or write data. Initially used for the transfer of 128-byte records between memory and disc, but over the years has gained many more functions.
 */
 	void setDMAAddress(Z80& cpu) {
#if LOG
		std::clog << "Set DMA address  (" << std::hex << Z80_DE(cpu) << ')' << std::endl;
#endif
		dma = Z80_DE(cpu);
		returnCode(cpu, 0);	// OK
	}

/**
 * BDOS function 27 (DRV_ALLOCVEC) - Return address of allocation map
 * Supported by: All versions, but differs in banked versions.
 * Entered with C=1Bh. Returns address in HL (16-bit versions use ES:BX).
 * Return the address of the allocation bitmap (which blocks are used and which are free) in HL. Under banked CP/M 3 and MP/M, this will be an address in bank 0 (the system bank) and not easily accessible.
 * Under previous versions, the format of the bitmap is a sequence of bytes, with bit 7 of the byte representing the lowest-numbered block on disc, and counting starting at block 0 (the directory). A bit is set if the corresponding block is in use.
 * Under CP/M 3, the allocation vector may be of this form (single-bit) or allocate two bits to each block (double-bit). This information is stored in the SCB.
 * The emulated disks always look empty.
 */
	void getAddrAlloc(Z80& cpu) {
		returnCode(cpu, ALV_ADDR);
	}

/**
 * BDOS function 28 (DRV_SETRO) - Software write-protect current disc
 * Supported by: All versions, with differences
 * Entered with C=1Ch.
 * Temporarily set current drive to be read-only; attempts to write to it will fail. Under genuine CP/M systems, this continues until either call 13 (disc reset) or call 37 (selective disc reset) is called; in practice, this means that whenever a program returns to the command prompt, all drives are reset to read/write. Newer BDOS replacements only reset the drive when function 37 is called.
 * Under multitasking CP/Ms, this can fail (returning A=0FFh) if another process has a file open on the drive.
 * Not emulated: accepted and ignored.
 */
	void writeProtectDisk(Z80& cpu) {
		returnCode(cpu, 0);
	}

/**
 * BDOS function 29 (DRV_ROVEC) - Return bitmap of read-only drives
 * Supported by: All versions
 * Entered with C=1Dh. Returns bitmap in HL.
 * Bit 7 of H corresponds to P: while bit 0 of L corresponds to A:. A bit is set if the corresponding drive is set to read-only in software.
 * Emulated drives are never read-only.
 */
	void getROVector(Z80& cpu) {
#if LOG
		std::clog << "Return RO Vector (read-only disks)" << std::endl;
#endif
		returnCode(cpu, 0);
	}

/**
 * BDOS function 30 (F_ATTRIB) - set file attributes
 * Supported by: All versions
 * Entered with C=1Eh, DE=address of FCB. Returns error codes in BA and HL.
 * Set and reset the bits required. Standard CP/M versions allow the bits F1', F2', F3', F4', T1' (read-only), T2' (system) and T3' (archive) to be changed. Some alternative BDOSses allow F5', F6', F7' and F8' to be set, but this is not to be encouraged since setting these bits can cause CP/M 3 to behave differently.
 * Under Concurrent CP/M, if the F5' bit is not set and the file has an extended file lock, the lock will be released when the attributes are set. If F5' is set the lock stays.
 * Under CP/M 3, the Last Record Byte Count is set by storing the required value at FCB+32 (FCB+20h) and setting the F6' bit.
 * The code returned in A is 0-3 if the operation was successful, or 0FFh if there was an error. Under CP/M 3, if A is 0FFh and H is nonzero, H contains a hardware error.
 * Attributes are not stored: only checks that the file exists.
 */
	void setFileAttributes(Z80& cpu, const uint8_t memory[]) {
		std::error_code ec;
		const bool ok = std::filesystem::is_regular_file(hostPath(fcbAt(memory, Z80_DE(cpu)), memory), ec);
		returnCode(cpu, ok ? 0x00 : 0xFF);
	}

/**
 * BDOS function 31 (DRV_DPB) - get DPB address
 * Supported by: CP/M 2 and later.
 * Entered with C=1Fh. Returns address in HL.
 * Returns the address of the Disc Parameter Block for the current drive. See the formats listing for details of the DPBs under various CP/M versions.
 * All drives share the same DPB.
 */
	void getAddrDiskParms(Z80& cpu) {
		returnCode(cpu, DPB_ADDR);
	}

/**
 * BDOS function 32 (F_USERNUM) - get/set user number
 * Supported by: CP/M 2 and later.
 * Entered with C=20h, E=number. If E=0FFh, returns number in A.
 * Set current user number. E should be 0-15, or 255 to retrieve the current user number into A. Some versions can use user areas 16-31, but these should be avoided for compatibility reasons.
 * DOS+ returns the number set in A.
 */
	void setGetUserCode(Z80& cpu, uint8_t memory[]) {
		assert(memory);
		if (Z80_E(cpu) == 0xFF) {
#if LOG
			std::clog << "Get user number (" << unsigned(memory[USER_DRIVE] >> 4) << ")" << std::endl;
#endif
			returnCode(cpu, memory[USER_DRIVE] >> 4);	// OK - user numb.
		} else {
#if LOG
			std::clog << "Set user number to " << unsigned(Z80_E(cpu)) << std::endl;
#endif
			memory[USER_DRIVE] = (memory[USER_DRIVE] & 0x0F) | (Z80_E(cpu) << 4);
			returnCode(cpu, 0);	// OK
		}
	}

/**
 * BDOS function 33 (F_READRAND) - Random access read record
 * Supported by: CP/M 2 and later.
 * Entered with C=21h, DE=FCB address. Returns error codes in BA and HL.
 *
 * Read the record specified in the random record count area of the FCB, at the DMA address.
 * The pointers in the FCB will be updated so that the next record to read using the sequential I/O
 * calls will be the record just read. Error numbers returned are:
 *
 * 0 OK
 * 1 Reading unwritten data
 * 4 Reading unwritten extent (a 16k portion of file does not exist)
 * 6 Record number out of range
 * 9 Invalid FCB
 * 10 Media changed (CP/M); FCB checksum error (MP/M),
 * 11 Unlocked file verification error (MP/M), 0FFh
 *
 */
	void readRandom(Z80& cpu, uint8_t *const memory) {
		assert(memory);
		FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
		const uint32_t record = randomRecord(pFCB);
#if LOG
		std::clog << "Read random record " << std::hex << record << "h (FCB: " << std::hex << unsigned(Z80_DE(cpu)) << "h)" << std::endl;
#endif
		if (pFCB->R[2]) {
			returnCode(cpu, 0x06);	// Out of range
			return;
		}
		if (!dmaFits(SECTOR_SIZE)) {
			std::cerr << ">> Writing DMA out of memory!" << std::endl;
			returnCode(cpu, 0xFF);
			return;
		}
		OpenFile *const pFile = stream(Z80_DE(cpu), pFCB, memory);
		if (!pFile) {
			returnCode(cpu, 0x01);	// Reading unwritten data - No such file
			return;
		}
		const auto result = readRecord(*pFile, record, memory + dma);
		if (!result) setFcbRecord(pFCB, record, *pFile);
		returnCode(cpu, result);
	}

/**
 * BDOS function 34 (F_WRITERAND) - Random access write record
 * Supported by: CP/M 2 and later.
 * Entered with C=22h, DE=FCB address. Returns error codes in BA and HL.
 * Write the record specified in the random record count area of the FCB, from the DMA address.
 * The pointers in the FCB will be updated so that the next record to write using the sequential I/O
 * calls will be the record just written.
 */
	void writeRandom(Z80& cpu, uint8_t *const memory) {
		assert(memory);
		FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
		const uint32_t record = randomRecord(pFCB);
#if LOG
		std::clog << "Write random record " << std::hex << record << "h (FCB: " << std::hex << unsigned(Z80_DE(cpu)) << "h)" << std::endl;
#endif
		if (pFCB->R[2]) {
			returnCode(cpu, 0x06);	// Out of range
			return;
		}
		if (!dmaFits(SECTOR_SIZE)) {
			std::cerr << ">> Reading DMA out of memory!" << std::endl;
			returnCode(cpu, 0xFF);
			return;
		}
		OpenFile *const pFile = stream(Z80_DE(cpu), pFCB, memory);
		if (!pFile) {
			returnCode(cpu, 0xFF);	// KO - No such file
			return;
		}
		const auto result = writeRecord(*pFile, record, memory + dma);
		if (!result) setFcbRecord(pFCB, record, *pFile);
		returnCode(cpu, result);
	}

/**
 * BDOS function 35 (F_SIZE) - Compute file size
 * Supported by: CP/M 2 and later.
 * Entered with C=23h, DE=FCB address. Returns error codes in BA and HL.
 * Set the random record count bytes of the FCB to the number of 128-byte records in the file.
 */
	void computeFileSize(Z80& cpu, uint8_t *const memory) {
		assert(memory);
		FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
		std::error_code ec;
		const auto size = std::filesystem::file_size(hostPath(pFCB, memory), ec);
		if (ec) {
			returnCode(cpu, 0xFF);
			return;
		}
		setRandomRecord(pFCB, (size + SECTOR_SIZE - 1) / SECTOR_SIZE);
		returnCode(cpu, 0x00);
	}

/**
 * BDOS function 36 (F_RANDREC) - Update random access pointer
 * Supported by: CP/M 2 and later.
 * Entered with C=24h, DE=FCB address. Returns error codes in BA and HL.
 * Set the random record count bytes of the FCB to the number of the last record read/written by the sequential I/O calls.
 */
	void setRandomRecord(Z80& cpu, uint8_t *const memory)  {
		assert(memory);
		FCB_t *const pFCB = fcbAt(memory, Z80_DE(cpu));
		setRandomRecord(pFCB, fcbRecord(pFCB));
		returnCode(cpu, 0x00);	// OK
	}

/**
 * BDOS function 37 (DRV_RESET) - Selectively reset disc drives
 * Supported by: CP/M 2 and later.
 * Entered with C=25h, DE=bitmap of drives to reset. Returns A=0 if OK.
 */
	void resetDrive(Z80& cpu) {
		returnCode(cpu, 0x00);
	}

/**
 * BDOS function 40 (F_WRITEZF) - Write random with zero fill
 * Supported by: CP/M 2 and later.
 * Entered with C=28h, DE=FCB address. Returns error codes in BA and HL.
 * As function 34, but if the write is to a newly allocated disc block the remainder of the block is filled with zeroes.
 * Host files have no blocks; a write past the end of file fills the gap with zeroes.
 */
	void writeRandomWithZeroFill(Z80& cpu, uint8_t *const memory) {
		writeRandom(cpu, memory);
	}


/**
 * An host file opened through a FCB.
 */
	struct OpenFile {
		uint16_t fcb = 0;			// FCB address, 0 if free
		unsigned drive = 0;			// Drive & name the FCB had when opened
		char name[11];
		std::filesystem::path path;
		std::fstream stream;
		unsigned long used = 0;		// Last use, for recycling
	};

/**
 * A file found by searchForFirst.
 */
	struct Entry {
		char name[11];
		uint32_t extent = 0;		// Extent number (S2 * 32 + EX)
		uint8_t records = 0;		// Records in this extent (RC)
	};

/**
 * Return the stream already opened for this FCB address, if any.
 */
	OpenFile* findStream(const uint16_t aFCB) {
		for (auto& f : files) {
			if (f.fcb && f.fcb == aFCB) return &f;
		}
		return nullptr;
	}

/**
 * Open a host file for a FCB; read/write if possible, read only otherwise.
 * When all the slots are used, the least recently used file is closed: it is
 * reopened by stream() if the program uses it again.
 * @return the opened file or nullptr.
 */
	OpenFile* openStream(const uint16_t aFCB, const FCB_t *const pFCB, const uint8_t memory[],
						 const std::filesystem::path& path, const std::ios::openmode mode = {}) {
		OpenFile* pFile = findStream(aFCB);
		if (!pFile) {
			pFile = std::min_element(std::begin(files), std::end(files), [](const OpenFile& a, const OpenFile& b) {
				return a.used < b.used;
			});
		}
		release(*pFile);
		pFile->stream.open(path, std::ios::binary | std::ios::in | std::ios::out | mode);
		if (!pFile->stream.is_open() && !(mode & std::ios::trunc)) {
			pFile->stream.clear();
			pFile->stream.open(path, std::ios::binary | std::ios::in);
		}
		if (!pFile->stream.is_open()) {
			pFile->stream.clear();
			return nullptr;
		}
		pFile->fcb = aFCB;
		pFile->drive = fcbDrive(pFCB, memory);
		fcbName(pFCB, pFile->name);
		pFile->path = path;
		pFile->used = ++useCounter;
		return pFile;
	}

/**
 * Return the stream to use for I/O with a FCB, reopening the file when the
 * program uses a FCB that is not (or no longer) opened here.
 * @return the file or nullptr if it doesn't exist.
 */
	OpenFile* stream(const uint16_t aFCB, const FCB_t *const pFCB, const uint8_t memory[]) {
		char name[11];
		fcbName(pFCB, name);
		OpenFile *const pFile = findStream(aFCB);
		if (pFile && pFile->drive == fcbDrive(pFCB, memory) && !memcmp(pFile->name, name, sizeof(name))) {
			pFile->used = ++useCounter;
			return pFile;
		}
		const auto path = hostPath(pFCB, memory);
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec)) return nullptr;
		return openStream(aFCB, pFCB, memory, path);
	}

/**
 * Close a file & free its slot.
 */
	void release(OpenFile& file) {
		if (file.stream.is_open()) file.stream.close();
		file.stream.clear();
		file.fcb = 0;
		file.path.clear();
		file.used = 0;
	}

/**
 * Close any file opened on this path (before deleting or renaming it).
 */
	void releasePath(const std::filesystem::path& path) {
		for (auto& f : files) {
			if (f.fcb && f.path == path) release(f);
		}
	}

/**
 * Read a record into buf; a partial last record is padded with ^Z.
 * @return 0 if OK, 1 at end of file.
 */
	uint8_t readRecord(OpenFile& file, const uint32_t record, uint8_t *const buf) {
		auto& s = file.stream;
		s.clear();
		s.seekg(std::streamoff(record) * SECTOR_SIZE);
		s.read(reinterpret_cast<char*>(buf), SECTOR_SIZE);
		const auto n = s.gcount();
		s.clear();
		if (n <= 0) return 0x01;	// EOF - Nothing read
		if (n < SECTOR_SIZE) memset(buf + n, 0x1A, SECTOR_SIZE - n);	// padding with ^Z
		return 0x00;
	}

/**
 * Write a record from buf; writing past the end of file fills the gap with zeroes.
 * @return 0 if OK, 0FFh on error.
 */
	uint8_t writeRecord(OpenFile& file, const uint32_t record, const uint8_t *const buf) {
		auto& s = file.stream;
		s.clear();
		s.seekp(std::streamoff(record) * SECTOR_SIZE);
		s.write(reinterpret_cast<const char*>(buf), SECTOR_SIZE);
		s.flush();
		if (!s) {
			std::cerr << ">> Error writing '" << file.path.string() << "': " << strerror(errno) << "!" << std::endl;
			s.clear();
			return 0xFF;
		}
		return 0x00;
	}

/**
 * Sequential position (record number) held by S2, EX & CR.
 */
	static uint32_t fcbRecord(const FCB_t *const pFCB) {
		return (uint32_t(pFCB->S2 & 0x7F) * EXTENTS_PER_MODULE + (pFCB->EX & 0x1F)) * RECORDS_PER_EXTENT + pFCB->CR;
	}

/**
 * Set S2, EX & CR to a record number, and RC to the records of that extent.
 */
	void setFcbRecord(FCB_t *const pFCB, const uint32_t record, const OpenFile& file) const {
		pFCB->CR = record % RECORDS_PER_EXTENT;
		pFCB->EX = (record / RECORDS_PER_EXTENT) % EXTENTS_PER_MODULE;
		pFCB->S2 = record / (RECORDS_PER_EXTENT * EXTENTS_PER_MODULE);
		pFCB->RC = extentRecords(file, record);
	}

/**
 * Number of records of the file in the extent holding this record (0-128).
 */
	uint8_t extentRecords(const OpenFile& file, const uint32_t record) const {
		std::error_code ec;
		const auto size = std::filesystem::file_size(file.path, ec);
		if (ec) return 0;
		const uint32_t records = (size + SECTOR_SIZE - 1) / SECTOR_SIZE;
		const uint32_t first = record - record % RECORDS_PER_EXTENT;
		return records <= first ? 0 : std::min(records - first, RECORDS_PER_EXTENT);
	}

	static uint32_t randomRecord(const FCB_t *const pFCB) {
		return pFCB->R[0] | (pFCB->R[1] << 8) | (pFCB->R[2] << 16);
	}

	static void setRandomRecord(FCB_t *const pFCB, const uint32_t record) {
		pFCB->R[0] = record & 0xff;
		pFCB->R[1] = (record >> 8) & 0xff;
		pFCB->R[2] = (record >> 16) & 0xff;
	}

/**
 */
	inline
	void returnCode(Z80& cpu, const uint16_t hl) const  {
		Z80_HL(cpu) = hl;
		Z80_A(cpu) = Z80_L(cpu);
		Z80_B(cpu) = Z80_H(cpu);
	}

/**
 */
	inline
	void returnCode(Z80& cpu, const uint8_t a, const uint8_t b) const  {
		Z80_L(cpu) = Z80_A(cpu) = a;
		Z80_H(cpu) = Z80_B(cpu) = b;
	}

	static FCB_t* fcbAt(uint8_t memory[], const uint16_t addr) {
		return reinterpret_cast<FCB_t*>(memory + addr);
	}

	static const FCB_t* fcbAt(const uint8_t memory[], const uint16_t addr) {
		return reinterpret_cast<const FCB_t*>(memory + addr);
	}

	bool dmaFits(const unsigned size) const {
		return dma + size <= MEMORY_SIZE * 1024U;
	}

/**
 * Drive used by a FCB: its own or the current one.
 */
	static unsigned fcbDrive(const FCB_t *const pFCB, const uint8_t memory[]) {
		return (pFCB->DR && pFCB->DR != '?') ? (pFCB->DR - 1) & 0x0F : memory[USER_DRIVE] & 0x0F;
	}

/**
 * Host directory of a drive.
 */
	static std::filesystem::path drivePath(const unsigned drive) {
		return std::string(1, char('A' + drive));
	}

/**
 * FCB name & type without attribute bits, in upper case (space padded).
 */
	static void fcbName(const FCB_t *const pFCB, char name[11]) {
		for (auto i = 0; i < 8; ++i) name[i] = toupper(pFCB->filename[i] & 0x7F);
		for (auto i = 0; i < 3; ++i) name[8 + i] = toupper(pFCB->filetype[i] & 0x7F);
	}

/**
 * Compare a CP/M name to a filter, where '?' matches any character.
 */
	static bool matches(const char filter[11], const char name[11]) {
		for (auto i = 0; i < 11; ++i) {
			if ((filter[i] != '?') && (filter[i] != name[i])) return false;
		}
		return true;
	}

/**
 * Host path of a file: the existing one whatever its case, else an upper case name.
 */
	std::filesystem::path hostPath(const unsigned drive, const char name[11]) const {
		const auto dir = drivePath(drive);
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
			char cpm[11];
			if (filenameDOS2CPM(entry.path().filename().string().c_str(), cpm) &&
				!memcmp(cpm, name, sizeof(cpm))) {
				return entry.path();
			}
		}
		char dos[13];
		filenameCPM2DOS(name, dos);
		return dir / dos;
	}

	std::filesystem::path hostPath(const FCB_t *const pFCB, const uint8_t memory[]) const {
		char name[11];
		fcbName(pFCB, name);
		return hostPath(fcbDrive(pFCB, memory), name);
	}

/**
 * Convert CP/M file name (space padding) to Dos (dot separator).
 */
	static void filenameCPM2DOS(const char cpm[11], char dos[13]) {
		auto p = dos;
		for (auto i = 0; i < 8; ++i) {
			if (cpm[i] != ' ') *p++ = cpm[i];
		}
		if (memcmp(cpm + 8, "   ", 3)) {
			*p++ = '.';
			for (auto i = 8; i < 11; ++i) {
				if (cpm[i] != ' ') *p++ = cpm[i];
			}
		}
		*p = '\0';
	}

/**
 * Convert Dos file name (dot separator) to CP/M (space padding, upper case).
 * @return false if this is not a valid 8.3 name.
 */
	static bool filenameDOS2CPM(const char dos[], char cpm[11]) {
		char f[11];
		memset(f, ' ', 11);

		const auto p = strchr(dos, '.');
		const auto l = p ? size_t(p - dos) : strlen(dos);
		if (l == 0 || l > 8) return false;	// Invalid CPM name
		for (unsigned i = 0; i < l; ++i) {
			f[i] = toupper(dos[i]);
		}
		if (p) {
			const auto l = strlen(p + 1);
			if (l > 3) return false;
			for (unsigned i = 0; i < l; ++i) {
				f[8 + i] = toupper(p[i + 1]);
			}
		}
		for (const auto c : f) {
			if (c < ' ' || c > '~' || c == '.' || c == '?' || c == '*') return false;
		}
		memcpy(cpm, f, 11);
		return true;
	}

/**
 * Page zero jump to the BDOS, and BDOS signature.
 * Programs may use memory up to BDOS+5, so the signature (tested by the CCP
 * before loading a program; it halts if wrong) is restored at each warm boot.
 */
	void vectors(uint8_t *const memory) const {
		memory[0x0005] = 0xC3;						// JUMP
		memory[0x0006] = (BDOS_ADDR + 6) & 0xFF;	// BDOS+6 (LL)
		memory[0x0007] = (BDOS_ADDR + 6) >> 8;		// BDOS+6 (HH)

		memory[BDOS_ADDR + 0] = 0x00;			// BDOS SIGNATURE
		memory[BDOS_ADDR + 1] = 0x16;			// CPM ver 22 dec.
		memory[BDOS_ADDR + 2] = 0x00;
		memory[BDOS_ADDR + 3] = 0x00;
		memory[BDOS_ADDR + 4] = 0x00;
		memory[BDOS_ADDR + 5] = 0x00;
	}

private:
/**
 * Sector size (fixed to 128 for CP/M 2.2.
 */
	static constexpr auto SECTOR_SIZE = 128U;
	static constexpr auto RECORDS_PER_EXTENT = 128U;	// 16K logical extent
	static constexpr auto EXTENTS_PER_MODULE = 32U;
	static constexpr auto RECORDS_PER_BLOCK = 16U;		// 2K blocks
	static constexpr auto DIR_ENTRY_SIZE = 32U;

/**
 * Page zero: IOBYTE, and current user (H) & drive (L)
 */
	static constexpr auto IOBYTE = 3U;
	static constexpr auto USER_DRIVE = 4U;

	static constexpr uint16_t DEFAULT_DMA = 0x0080;

/**
 * Disk Parameter Block & allocation vector, in the BDOS area.
 */
	static constexpr uint16_t DPB_ADDR = BDOS_ADDR + 0x10;
	static constexpr uint16_t ALV_ADDR = BDOS_ADDR + 0x20;
	static constexpr auto ALV_SIZE = 256U;	// (DSM + 1) / 8

	static constexpr auto MAX_FILES = 16U;

/**
 * DMA's address.
 */
	uint16_t dma = DEFAULT_DMA;

/**
 * Files found by searchForFirst, and next one to return.
 */
	std::vector<Entry> found;
	size_t nextFound = 0;

/**
 * Files opened through FCBs.
 */
	OpenFile files[MAX_FILES];
	unsigned long useCounter = 0;

};
