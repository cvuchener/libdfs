/*
 * Copyright 2023 Clement Vuchener
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <dfs/Structures.h>
#include <dfs/ABI.h>
#include <dfs/MemoryLayout.h>

#include <iostream>

int main(int argc, char *argv[])
{
	using namespace dfs;
	try {
		Structures structures(argv[1]);
		MemoryLayout memory_layout(structures, ABI::MSVC2015_64);
	}
	catch (std::exception &e) {
		std::cerr << "Could not load structures: " << e.what() << std::endl;
		return -1;
	}
	return 0;
}
