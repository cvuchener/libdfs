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

#include "Win32Process.h"

#include <psapi.h>
#include <tlhelp32.h>

using namespace dfs;

struct HandleDeleter {
	using pointer = HANDLE;
	void operator()(HANDLE h){ CloseHandle(h); }
};
using unique_handle = std::unique_ptr<void, HandleDeleter>;

Win32Process::Win32Process(DWORD pid):
	_pid(pid),
	_process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid))
{
	if (!_process)
		throw std::system_error(GetLastError(), std::system_category(), "OpenProcess");

	unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, _pid));
	if (!snapshot)
		throw std::system_error(GetLastError(), std::system_category(), "CreateToolhelp32Snapshot");
	MODULEENTRY32 me32;
	me32.dwSize = sizeof(MODULEENTRY32);
	if (!Module32First(snapshot.get(), &me32))
		throw std::system_error(GetLastError(), std::system_category(), "Module32First");
	IMAGE_DOS_HEADER dos_header;
	SIZE_T r;
	if (!ReadProcessMemory(_process, me32.modBaseAddr, &dos_header, sizeof(dos_header), &r))
		throw std::system_error(GetLastError(), std::system_category(), "ReadProcessMemory");
	if (r != sizeof(dos_header))
		throw std::runtime_error("Partial DOS header");
	if (dos_header.e_magic != IMAGE_DOS_SIGNATURE)
		throw std::runtime_error("Invalid DOS header");
	IMAGE_NT_HEADERS pe_header;
	if (!ReadProcessMemory(_process, me32.modBaseAddr+dos_header.e_lfanew, &pe_header, sizeof(pe_header), &r))
		throw std::system_error(GetLastError(), std::system_category(), "ReadProcessMemory");
	if (r != sizeof(pe_header))
		throw std::runtime_error("Partial PE header");
	if (pe_header.Signature != IMAGE_NT_SIGNATURE)
		throw std::runtime_error("Invalid PE header");
	switch (pe_header.FileHeader.Machine) {
	case IMAGE_FILE_MACHINE_I386:
		_base_offset = reinterpret_cast<uintptr_t>(me32.modBaseAddr) - 0x400000ul;
		break;
	case IMAGE_FILE_MACHINE_AMD64:
		_base_offset = reinterpret_cast<uintptr_t>(me32.modBaseAddr) - 0x140000000ull;
		break;
	default:
		throw std::runtime_error("Unsupported architecture");
	}
	_timestamp.resize(4);
	for (int i = 0; i < 4; ++i)
		_timestamp[i] = static_cast<uint8_t>(pe_header.FileHeader.TimeDateStamp >> (3-i)*8);
}

Win32Process::~Win32Process()
{
	CloseHandle(_process);
}

std::span<const uint8_t> Win32Process::id() const
{
	return _timestamp;
}

intptr_t Win32Process::base_offset() const
{
	return _base_offset;
}

std::error_code Win32Process::stop()
{
	return std::make_error_code(std::errc::function_not_supported);
}

std::error_code Win32Process::cont()
{
	return std::make_error_code(std::errc::function_not_supported);
}

cppcoro::task<std::error_code> Win32Process::read(MemoryBufferRef buffer)
{
	SIZE_T r;
	if (!ReadProcessMemory(_process, reinterpret_cast<LPCVOID>(buffer.address),
				buffer.data.data(), buffer.data.size(), &r))
		co_return std::error_code{int(GetLastError()), std::system_category()};
	if (r != buffer.data.size())
		co_return std::error_code{ERROR_PARTIAL_COPY, std::system_category()};
	co_return std::error_code{};
}
