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

#ifndef DFS_LINUX_PROCESS_COMMON_H
#define DFS_LINUX_PROCESS_COMMON_H

#include <dfs/Process.h>

namespace dfs {

class LinuxProcessCommon: public Process
{
public:
	LinuxProcessCommon(int pid);
	~LinuxProcessCommon() override = default;

	[[nodiscard]] std::error_code stop() override;
	[[nodiscard]] std::error_code cont() override;

	[[nodiscard]] cppcoro::task<std::error_code> read(MemoryBufferRef buffer) override;
	[[nodiscard]] cppcoro::task<std::error_code> readv(std::span<const MemoryBufferRef> tasks) override;

protected:
	int _pid;
};

}

#endif
