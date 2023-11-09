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

#include "Bitfield.h"

#include "Structures.h"

#include <pugixml.hpp>
using namespace pugi;

using namespace dfs;

Bitfield::Bitfield(std::string_view debug_name, const xml_node element, ErrorLog &log):
	PrimitiveType(element.attribute("base-type").as_string("uint32_t")),
	debug_name(debug_name)
{
	int offset = 0;
	for (auto child: element.children()) {
		if (child.type() != node_element)
			continue;
		std::string_view tagname = child.name();
		if (tagname == "flag-bit") {
			int count = child.attribute("count").as_int(1);
			auto name = child.attribute("name");
			flags.push_back(Flag{name.value(), offset, count});
			offset += count;
		}
	}
}
