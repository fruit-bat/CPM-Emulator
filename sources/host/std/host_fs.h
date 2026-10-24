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
 * Host files for the BDOS: the host's own file system, through std::filesystem.
 * Paths are relative to the current directory, with '/' separators.
 */

#if __cplusplus < 201703L
#error "Need C++17 compiler for using <filesystem>"
#endif

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace hostfs {

inline std::string lastErrorText;

/**
 * Text of the last error, for messages.
 */
inline const char* lastError() {
	return lastErrorText.c_str();
}

inline bool check(const std::error_code& ec) {
	if (ec) lastErrorText = ec.message();
	return !ec;
}

inline bool isDirectory(const std::string& path) {
	std::error_code ec;
	return std::filesystem::is_directory(path, ec);
}

inline bool isFile(const std::string& path) {
	std::error_code ec;
	return std::filesystem::is_regular_file(path, ec);
}

inline bool exists(const std::string& path) {
	std::error_code ec;
	return std::filesystem::exists(path, ec);
}

inline bool fileSize(const std::string& path, uint32_t& size) {
	std::error_code ec;
	const auto s = std::filesystem::file_size(path, ec);
	if (!check(ec)) return false;
	size = uint32_t(s);
	return true;
}

inline bool resizeFile(const std::string& path, const uint32_t size) {
	std::error_code ec;
	std::filesystem::resize_file(path, size, ec);
	return check(ec);
}

inline bool removeFile(const std::string& path) {
	std::error_code ec;
	std::filesystem::remove(path, ec);
	return check(ec);
}

inline bool renameFile(const std::string& from, const std::string& to) {
	std::error_code ec;
	std::filesystem::rename(from, to, ec);
	return check(ec);
}

/**
 * Call f(name, size) for each file of a directory.
 * @return false if the directory can't be read.
 */
template <typename F>
bool listFiles(const std::string& dir, F f) {
	std::error_code ec;
	std::filesystem::directory_iterator di(dir, ec);
	if (!check(ec)) return false;
	for (const auto& entry : di) {
		if (entry.is_regular_file(ec)) {
			f(entry.path().filename().string().c_str(), uint32_t(entry.file_size(ec)));
		}
	}
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
		stream.open(path, std::ios::binary | std::ios::in | std::ios::out | (truncate ? std::ios::trunc : std::ios::openmode{}));
		if (!stream.is_open() && !truncate) {
			stream.clear();
			stream.open(path, std::ios::binary | std::ios::in);
		}
		if (!stream.is_open()) {
			lastErrorText = strerror(errno);
			stream.clear();
			return false;
		}
		this->path = path;
		return true;
	}

	void close() {
		if (stream.is_open()) stream.close();
		stream.clear();
		path.clear();
	}

/**
 * Read up to n bytes at offset.
 * @return the number of bytes read, 0 at the end of the file.
 */
	unsigned read(const uint32_t offset, uint8_t *const buf, const unsigned n) {
		stream.clear();
		stream.seekg(std::streamoff(offset));
		stream.read(reinterpret_cast<char*>(buf), n);
		const auto r = stream.gcount();
		stream.clear();
		return r > 0 ? unsigned(r) : 0;
	}

/**
 * Write n bytes at offset; writing past the end of the file fills the gap with zeroes.
 */
	bool write(const uint32_t offset, const uint8_t *const buf, const unsigned n) {
		stream.clear();
		stream.seekp(std::streamoff(offset));
		stream.write(reinterpret_cast<const char*>(buf), n);
		stream.flush();
		if (!stream) {
			lastErrorText = strerror(errno);
			stream.clear();
			return false;
		}
		return true;
	}

	bool size(uint32_t& size) const {
		return fileSize(path, size);
	}

private:
	std::string path;
	std::fstream stream;
};

}
