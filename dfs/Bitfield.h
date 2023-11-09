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

#ifndef DFS_BITFIELD_H
#define DFS_BITFIELD_H

#include <dfs/Type.h>

namespace pugi { class xml_node; }

namespace dfs {

class ErrorLog;

/**
 * Bitfield types.
 *
 * \ingroup types
 */
struct Bitfield: PrimitiveType
{
	/**
	 * Constructs a bitfield from a xml element
	 */
	Bitfield(std::string_view debug_name, const pugi::xml_node element, ErrorLog &errors);

	struct Flag
	{
		std::string name;
		int offset;	///< first bit
		int count = 1;	///< bit count
	};

	std::string debug_name;
	std::vector<Flag> flags;
};

} // namespace dfs

#endif
