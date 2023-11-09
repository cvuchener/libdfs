/*
 * Copyright 2023 Clement Vuchener
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef PROC_UTILS_H
#define PROC_UTILS_H

#include <filesystem>
#include <charconv>
#include <format>
#include <regex>
#include <cassert>

namespace proc {

static inline std::filesystem::path path(int pid)
{
	return std::format("/proc/{}", pid);
}

struct maps_entry_t
{
	uintptr_t start_address, end_address;
	std::string perms;
	uintptr_t offset;
	unsigned int dev_major, dev_minor;
	unsigned int inode;
	std::string pathname;
};

static inline std::istream &operator>>(std::istream &in, maps_entry_t &entry)
{
	static const std::regex procmaps_re(
			"([0-9a-f]+)-([0-9a-f]+)"   // address
			" ([-r][-w][-x][sp])"       // perms
			" ([0-9a-f]+)"              // offset
			" ([0-9a-f]+):([0-9a-f]+)"  // dev
			" ([0-9]+)"                 // inode
			"(?:\\s*(.+))");            // pathname
	std::string line;
	std::smatch res;
	static const auto to_int = [](const std::ssub_match &submatch, std::integral auto &var, int base = 10) {
		const auto str = std::string_view(submatch.first, submatch.second);
		[[maybe_unused]] auto r = std::from_chars(str.data(), str.data()+str.size(), var, base);
		assert(r.ec == std::errc{} && r.ptr == str.data()+str.size());
	};
	while (true) {
		auto &r = getline(in, line);
		if (!r)
			return r;
		if (regex_match(line, res, procmaps_re)) {
			to_int(res[1], entry.start_address, 16);
			to_int(res[2], entry.end_address, 16);
			entry.perms = res[3];
			to_int(res[4], entry.offset, 16);
			to_int(res[5], entry.dev_major, 16);
			to_int(res[6], entry.dev_minor, 16);
			to_int(res[7], entry.inode);
			entry.pathname = res[8];
			return r;
		}
	}
}

}

#endif
