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

#include "Path.h"

using namespace dfs;

std::vector<path::item> dfs::parse_path(std::string_view str)
{
	using namespace parser_details;
	std::vector<path::item> vec;
	parse_path_impl(str, [&vec](auto &&... args) {
		vec.emplace_back(std::forward<decltype(args)>(args)...);
	});
	return vec;
}

