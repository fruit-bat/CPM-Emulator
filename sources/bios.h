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
 */

#pragma once

// #include "Z80.h"

// #include <dirent.h>
// #include <sys/stat.h>
// #include <cstring>
// #include <iostream>
// #include <iomanip>
// #include <filesystem>
#include "console.h"

// #define LOG 1
/**
 * @see https://www.seasip.info/Cpm/bios.html#const
 * 	JMP	BOOT	;-3: Cold start routine
	JMP	WBOOT	; 0: Warm boot - reload command processor
	JMP	CONST	; 3: Console status
	JMP	CONIN	; 6: Console input
	JMP	CONOUT	; 9: Console output
	JMP	LIST	;12: Printer output
	JMP	PUNCH	;15: Paper tape punch output
	JMP	READER	;18: Paper tape reader input
	JMP	HOME	;21: Move disc head to track 0
	JMP	SELDSK	;24: Select disc drive
	JMP	SETTRK	;27: Set track number
	JMP	SETSEC	;30: Set sector number
	JMP	SETDMA	;33: Set DMA address
	JMP	READ	;36: Read a sector
	JMP	WRITE	;39: Write a sector
	JMP	LISTST	;42: Status of list device
	JMP	SECTRAN	;45: Sector translation for skewing
 */
template <unsigned MEMORY_SIZE, uint16_t BIOS_ADDR>
class BIOS {
public:
	BIOS() {
		// TODO send to console
		std::cout << "CP/M 2.2 Emulator " << MEMORY_SIZE << "kb" << std::endl;
		std::cout << "Copyright (c) 2021 by M. Sibert" << std::endl;
		std::cout << std::endl;
	}
	
	void init(uint8_t *const memory) {
	// WBOOT (BIOS entry)
		memory[0x0000] = 0xC3;						// JUMP TO BIOS
		memory[0x0001] = (BIOS_ADDR + 3) & 0xFF;	// Function 1
		memory[0x0002] = (BIOS_ADDR + 3) >> 8;

		for (auto i = 0; i <= 16; ++i) {
			memory[BIOS_ADDR + (i * 3) + 0] = 0xC3;	 // JMP
			memory[BIOS_ADDR + (i * 3) + 1] = (BIOS_ADDR + (i * 3)) & 0xFF;
			memory[BIOS_ADDR + (i * 3) + 2] = (BIOS_ADDR + (i * 3)) >> 8;
		}
	}

/**
 * BIOS functions.
 * PC register contains the local address.
 * BOOT & WBOOT are handled by the Computer (warm boot).
 */	
	void function(Z80& cpu, [[maybe_unused]] uint8_t *const memory) {
		assert(memory);
		switch (Z80_PC(cpu)) {
			case CONST_ADDR: {
				Z80_A(cpu) = conctrl_status();
				break;
			}
			case CONIN_ADDR : {
				Z80_A(cpu) = conctrl_getch_wait();
				break;
			}
			case CONOUT_ADDR : {
				console_putc(Z80_C(cpu));
				break;
			}
			case LIST_ADDR :		// No printer: discard
			case PUNCH_ADDR : {		// No punch: discard
				break;
			}
			case READER_ADDR : {	// No reader: always end of file
				Z80_A(cpu) = 0x1A;
				break;
			}
			case HOME_ADDR :
			case SETTRK_ADDR :
			case SETSEC_ADDR :
			case SETDMA_ADDR : {	// Disks are emulated by the BDOS
				break;
			}
			case SELDSK_ADDR : {	// No physical disk: HL=0 means error
				Z80_HL(cpu) = 0x0000;
				break;
			}
			case READ_ADDR :
			case WRITE_ADDR : {		// No physical disk: A=1 means error
				Z80_A(cpu) = 0x01;
				break;
			}
			case LISTST_ADDR : {	// Printer always ready
				Z80_A(cpu) = 0xFF;
				break;
			}
			case SECTRAN_ADDR : {	// No skew
				Z80_HL(cpu) = Z80_BC(cpu);
				break;
			}
				
			default:
				std::cerr << "Function " << (Z80_PC(cpu) - BIOS_ADDR) / 3;
				std::cerr << " : Unknown BIOS function!" << std::endl;
				
				throw std::runtime_error("Un-emulated BIOS function");
				break;
		}
	}

	enum {
		BOOT_ADDR 	= BIOS_ADDR + 3 * 0,
		WBOOT_ADDR 	= BIOS_ADDR + 3 * 1,
		CONST_ADDR 	= BIOS_ADDR + 3 * 2,
		CONIN_ADDR 	= BIOS_ADDR + 3 * 3,
		CONOUT_ADDR = BIOS_ADDR + 3 * 4,
		LIST_ADDR 	= BIOS_ADDR + 3 * 5,
		PUNCH_ADDR 	= BIOS_ADDR + 3 * 6,
		READER_ADDR = BIOS_ADDR + 3 * 7,
		HOME_ADDR 	= BIOS_ADDR + 3 * 8,
		SELDSK_ADDR = BIOS_ADDR + 3 * 9,
		SETTRK_ADDR = BIOS_ADDR + 3 * 10,
		SETSEC_ADDR = BIOS_ADDR + 3 * 11,
		SETDMA_ADDR = BIOS_ADDR + 3 * 12,
		READ_ADDR 	= BIOS_ADDR + 3 * 13,
		WRITE_ADDR 	= BIOS_ADDR + 3 * 14,
		LISTST_ADDR = BIOS_ADDR + 3 * 15,
		SECTRAN_ADDR = BIOS_ADDR + 3 * 16
	};
};
