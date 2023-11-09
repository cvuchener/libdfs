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

#ifndef DFS_ENUM_H
#define DFS_ENUM_H

#include <dfs/Type.h>

#include <optional>
#include <variant>

namespace pugi { class xml_node; }

namespace dfs {

class ErrorLog;
class Structures;

/**
 * Enumerated types.
 *
 * \ingroup types
 */
struct Enum: PrimitiveType
{
	/**
	 * Constructs an enum from a xml element.
	 */
	Enum(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log);

	struct Item;
	using EnumValueIterator = string_map<Item>::const_iterator;
	using AttributeValue = std::variant<std::string, bool, long long, unsigned long long, EnumValueIterator>;

	struct Item
	{
		/**
		 * Integral value for this item.
		 */
		int value;
		/**
		 * Attributes that are not defaulted.
		 */
		string_map<AttributeValue> attributes;

		Item(int value): value(value) {}
	};
	struct Attribute
	{
		/**
		 * Type of this attribute.
		 *
		 * It can be an integral primitive type or another enum.
		 *
		 * If not set the attribute value is a unparsed string.
		 */
		std::optional<AnyType> type;
		/**
		 * Default value if the item does not have one.
		 */
		std::optional<AttributeValue> default_value;
	};

	/**
	 * Name for debugging/logging.
	 */
	std::string debug_name;
	/**
	 * Attributes by name.
	 */
	string_map<Attribute> attributes;
	/**
	 * Values by name.
	 */
	string_map<Item> values;
	/**
	 * The last value plus one.
	 */
	int count = 0;

	void resolve(Structures &structures, ErrorLog &log);
};

} // namespace dfs

#endif
