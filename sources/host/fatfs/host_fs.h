/**
 * Copyright 2026 fruit-bat
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
 * Host files for the BDOS: a FAT file system through FatFs (e.g. on an SD card
 * of a microcontroller). The application mounts it before starting the emulator.
 * Paths are relative to the current directory of FatFs (the root unless changed),
 * with '/' separators.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include "ff.h"

namespace hostfs {

inline FRESULT lastResult = FR_OK;

/**
 * Text of the last error, for messages.
 */
inline const char* lastError() {
	static const char *const texts[] = {
		"OK", "Disk error", "Internal error", "Disk not ready", "No such file", "No such path",
		"Invalid name", "Access denied", "File exists", "Invalid object", "Write protected",
		"Invalid drive", "Not mounted", "No FAT file system", "Format aborted", "Timeout",
		"Locked", "Out of memory", "Too many open files", "Invalid parameter"
	};
	return unsigned(lastResult) < sizeof(texts) / sizeof(texts[0]) ? texts[lastResult] : "Unknown error";
}

inline bool check(const FRESULT r) {
	if (r != FR_OK) lastResult = r;
	return r == FR_OK;
}

inline bool stat(const std::string& path, FILINFO& info) {
	return check(f_stat(path.c_str(), &info));
}

inline bool isDirectory(const std::string& path) {
	FILINFO info;
	return stat(path, info) && (info.fattrib & AM_DIR);
}

inline bool isFile(const std::string& path) {
	FILINFO info;
	return stat(path, info) && !(info.fattrib & AM_DIR);
}

inline bool exists(const std::string& path) {
	FILINFO info;
	return stat(path, info);
}

inline bool fileSize(const std::string& path, uint32_t& size) {
	FILINFO info;
	if (!stat(path, info)) return false;
	size = info.fsize;
	return true;
}

inline bool resizeFile(const std::string& path, const uint32_t size) {
	static FIL fil;	// Off the stack, which can be small on a microcontroller
	if (!check(f_open(&fil, path.c_str(), FA_WRITE))) return false;
	const bool ok = check(f_lseek(&fil, size)) && check(f_truncate(&fil));
	return check(f_close(&fil)) && ok;
}

inline bool removeFile(const std::string& path) {
	return check(f_unlink(path.c_str()));
}

inline bool renameFile(const std::string& from, const std::string& to) {
	return check(f_rename(from.c_str(), to.c_str()));
}

/**
 * Call f(name, size) for each file of a directory.
 * @return false if the directory can't be read.
 */
template <typename F>
bool listFiles(const std::string& dir, F f) {
	DIR d;
	if (!check(f_opendir(&d, dir.c_str()))) return false;
	FILINFO info;
	while (check(f_readdir(&d, &info)) && info.fname[0]) {
		if (!(info.fattrib & AM_DIR)) f(info.fname, uint32_t(info.fsize));
	}
	f_closedir(&d);
	return true;
}

/**
 * An open host file.
 */
class File {
public:
	~File() {
		close();
	}

/**
 * Open a file read/write, or read only if that fails.
 * @param truncate Create the file, or empty it, instead.
 */
	bool open(const std::string& path, const bool truncate) {
		close();
		FRESULT r = f_open(&fil, path.c_str(), FA_READ | FA_WRITE | (truncate ? FA_CREATE_ALWAYS : 0));
		if (r != FR_OK && !truncate) r = f_open(&fil, path.c_str(), FA_READ);
		opened = check(r);
		return opened;
	}

	void close() {
		if (opened) f_close(&fil);
		opened = false;
	}

/**
 * Read up to n bytes at offset.
 * @return the number of bytes read, 0 at the end of the file.
 */
	unsigned read(const uint32_t offset, uint8_t *const buf, const unsigned n) {
	// Seeking past the end would extend a file opened for writing
		if (offset >= f_size(&fil) || !check(f_lseek(&fil, offset))) return 0;
		UINT r = 0;
		check(f_read(&fil, buf, n, &r));
		return r;
	}

/**
 * Write n bytes at offset; writing past the end of the file fills the gap with zeroes.
 */
	bool write(const uint32_t offset, const uint8_t *const buf, const unsigned n) {
		uint32_t end = f_size(&fil);
		if (offset > end) {
			static const uint8_t zeroes[128] = {};
			if (!check(f_lseek(&fil, end))) return false;
			while (end < offset) {
				const UINT z = std::min<uint32_t>(offset - end, sizeof(zeroes));
				UINT w = 0;
				if (!check(f_write(&fil, zeroes, z, &w))) return false;
				if (w != z) return check(FR_DENIED);	// Disk full
				end += z;
			}
		}
		UINT w = 0;
		if (!check(f_lseek(&fil, offset)) || !check(f_write(&fil, buf, n, &w))) return false;
		if (w != n) return check(FR_DENIED);	// Disk full
	// Keep the directory up to date, as sizes are read from it
		return check(f_sync(&fil));
	}

	bool size(uint32_t& size) const {
		size = f_size(&fil);
		return true;
	}

private:
	FIL fil = {};
	bool opened = false;
};

}
