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

#include "LinuxProcessCommon.h"

extern "C" {
#include <sys/uio.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
}

#include <fstream>

using namespace dfs;

LinuxProcessCommon::LinuxProcessCommon(int pid):
	_pid(pid)
{
}

std::error_code LinuxProcessCommon::stop()
{
	auto wait_signal = [](int pid, int sig) {
		while (true) {
			int status;
			if (-1 == waitpid(pid, &status, 0))
				return errno;
			if (!WIFSTOPPED(status))
				continue;
			if (WSTOPSIG(status) == sig)
				return 0;
			if (-1 == ptrace(PTRACE_CONT, pid, 0, WSTOPSIG(status)))
				return errno;
		}
	};

	if (-1 == ptrace(PTRACE_ATTACH, _pid, 0, 0)) {
		auto err = errno;
		return {err, std::system_category()};
	}
	if (auto err = wait_signal(_pid, SIGSTOP))
		return {err, std::system_category()};
	return {};
}

std::error_code LinuxProcessCommon::cont()
{
	if (-1 == ptrace(PTRACE_DETACH, _pid, 0, 0))
		return {errno, std::system_category()};
	return {};
}

cppcoro::task<std::error_code> LinuxProcessCommon::read(MemoryBufferRef buffer)
{
	iovec local = {buffer.data.data(), buffer.data.size()};
	iovec remote = {reinterpret_cast<void *>(buffer.address), buffer.data.size()};
	if (-1 == process_vm_readv(_pid, &local, 1, &remote, 1, 0)) {
		auto err = errno;
		co_return std::error_code{err, std::system_category()};
	}
	co_return std::error_code{};
}

cppcoro::task<std::error_code> LinuxProcessCommon::readv(std::span<const MemoryBufferRef> tasks)
{
	std::array<iovec, IOV_MAX> local;
	std::array<iovec, IOV_MAX> remote;
	auto task = tasks.begin();
	while (task != tasks.end()) {
		std::size_t count = 0;
		std::size_t bytes = 0;
		while (count < IOV_MAX && task != tasks.end()) {
			local[count].iov_base = task->data.data();
			local[count].iov_len = task->data.size();
			remote[count].iov_base = reinterpret_cast<void *>(task->address);
			remote[count].iov_len = task->data.size();
			bytes += task->data.size();
			++count;
			++task;
		}
		auto r = process_vm_readv(_pid, local.data(), count, remote.data(), count, 0);
		auto err = errno;
		if (r < 0)
			co_return std::error_code{err, std::system_category()};
		else if (std::size_t(r) != bytes)
			co_return std::error_code{EACCES, std::system_category()};
	}
	co_return std::error_code{};
}
