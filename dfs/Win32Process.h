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

#ifndef DFS_WIN32_PROCESS_H
#define DFS_WIN32_PROCESS_H

#include <dfs/Process.h>

#include <vector>

extern "C" {
#include <windows.h>
}

namespace dfs {

class Win32Process: public Process
{
public:
	Win32Process(DWORD pid);
	~Win32Process() override;

	std::span<const uint8_t> id() const override;
	intptr_t base_offset() const override;

	std::error_code stop() override;
	std::error_code cont() override;

	[[nodiscard]] cppcoro::task<std::error_code> read(MemoryBufferRef buffer) override;

private:
	DWORD _pid;
	HANDLE _process;
	intptr_t _base_offset;
	std::vector<uint8_t> _timestamp;
};

} // namespace dfs

#endif
