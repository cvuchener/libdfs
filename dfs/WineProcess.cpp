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

#include "WineProcess.h"

#include <fstream>
#include <algorithm>

#include "linux/proc_utils.h"

struct IMAGE_DOS_HEADER {      // DOS .EXE header
	uint16_t e_magic;                     // Magic number
	uint16_t e_cblp;                      // Bytes on last page of file
	uint16_t e_cp;                        // Pages in file
	uint16_t e_crlc;                      // Relocations
	uint16_t e_cparhdr;                   // Size of header in paragraphs
	uint16_t e_minalloc;                  // Minimum extra paragraphs needed
	uint16_t e_maxalloc;                  // Maximum extra paragraphs needed
	uint16_t e_ss;                        // Initial (relative) SS value
	uint16_t e_sp;                        // Initial SP value
	uint16_t e_csum;                      // Checksum
	uint16_t e_ip;                        // Initial IP value
	uint16_t e_cs;                        // Initial (relative) CS value
	uint16_t e_lfarlc;                    // File address of relocation table
	uint16_t e_ovno;                      // Overlay number
	uint16_t e_res[4];                    // Reserved words
	uint16_t e_oemid;                     // OEM identifier (for e_oeminfo)
	uint16_t e_oeminfo;                   // OEM information; e_oemid specific
	uint16_t e_res2[10];                  // Reserved words
	uint32_t e_lfanew;                    // File address of new exe header
};

struct IMAGE_FILE_HEADER {
	uint16_t Machine;
	uint16_t NumberOfSections;
	uint32_t TimeDateStamp;
	uint32_t PointerToSymbolTable;
	uint32_t NumberOfSymbols;
	uint16_t SizeOfOptionalHeader;
	uint16_t Characteristics;
};

struct IMAGE_NT_HEADERS {
	uint32_t                 Signature;
	IMAGE_FILE_HEADER     FileHeader;
	//IMAGE_OPTIONAL_HEADER OptionalHeader;
};

using namespace dfs;

WineProcess::WineProcess(int pid):
	LinuxProcessCommon(pid)
{
	bool found = false;
	if (auto maps = std::ifstream(proc::path(pid) / "maps")) {
		for (proc::maps_entry_t entry; maps >> entry;) {
			if (entry.pathname.ends_with("Dwarf Fortress.exe")) {
				_base_offset = entry.start_address - 0x140000000ull;

				std::ifstream df(entry.pathname);
				df.exceptions(std::ios_base::badbit | std::ios_base::failbit | std::ios_base::eofbit);
				IMAGE_DOS_HEADER dos_header;
				df.read(reinterpret_cast<char *>(&dos_header), sizeof(dos_header));
				df.seekg(dos_header.e_lfanew + offsetof(IMAGE_NT_HEADERS, FileHeader.TimeDateStamp));
				_timestamp.resize(4);
				df.read(reinterpret_cast<char *>(_timestamp.data()), _timestamp.size());
				std::ranges::reverse(_timestamp);

				found = true;
				break;
			}
		}
	}
	else
		throw std::runtime_error("Failed to open memory maps");
	if (!found)
		throw std::runtime_error("This process is not running Dwarf Fortress.exe");
}

std::span<const uint8_t> WineProcess::id() const
{
	return _timestamp;
}

intptr_t WineProcess::base_offset() const
{
	return _base_offset;
}

