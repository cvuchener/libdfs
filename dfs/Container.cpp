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

#include "Container.h"

#include <charconv>
#include <type_traits>

#include "Structures.h"
#include "Compound.h"
#include "Enum.h"

#include "overloaded.h"

#include <pugixml.hpp>
using namespace pugi;

using namespace dfs;

string_map<Container::Type> Container::TypeNames = {
	{ "pointer", Container::Pointer },
	{ "static-array", Container::StaticArray },
	{ "stl-deque", Container::StdDeque },
	{ "stl-optional", Container::StdOptional },
	{ "stl-set", Container::StdSet },
	{ "stl-shared-ptr", Container::StdSharedPtr },
	{ "stl-vector", Container::StdVector },
};

std::optional<Container::Type> Container::typeFromTagName(std::string_view name)
{
	auto it = TypeNames.find(name);
	if (it != TypeNames.end())
		return it->second;
	else
		return std::nullopt;
}

std::string Container::to_string(Type type)
{
	switch (type) {
	case Pointer: return "pointer";
	case StdSharedPtr: return "stl-shared-ptr";
	case StdVector: return "stl-vector";
	case StdDeque: return "stl-deque";
	case StdSet: return "stl-set";
	case StdOptional: return "stl-optional";
	case StaticArray: return "static-array";
	default: return "invalid";
	}
}

Container::Container(std::string_view debug_name, Type container_type, const xml_node element, ErrorLog &log, bool pointer_recurse):
	debug_name(debug_name),
	container_type(container_type),
	item_type([&]()->AnyType{
		if (auto type = element.attribute("type-name"))
			return {type.value()};
		else if (auto type = element.attribute("pointer-type"))
			if (!pointer_recurse)
				return std::make_unique<Container>(debug_name, Pointer, element, log, true);
			else
				return {type.value()};
		else {
			Compound compound(debug_name, element, log);
			if (compound.members.size() == 1)
				return std::move(compound.members.at(0).type);
			else
				return std::make_unique<Compound>(std::move(compound));
		}}()),
	has_bad_pointers(element.attribute("has-bad-pointers").as_bool())
{

	if (container_type == StaticArray)
		extent = element.attribute("count").as_ullong(NoExtent);
	if (auto index_enum_attr = element.attribute("index-enum"))
		index_enum.emplace(index_enum_attr.value());
}

Container::Container(std::string_view debug_name, static_string_t, const xml_node element):
	debug_name(debug_name),
	container_type(Container::StaticArray),
	item_type(std::in_place_type<PrimitiveType>, "static-string"),
	extent(element.attribute("size").as_ullong(0))
{
}

void Container::resolve(Structures &structures, ErrorLog &log)
{
	if (auto e = structures.resolve(item_type, log))
		log.error("Cannot resolve {} item type reference to {}", debug_name, e->name);
	if (index_enum)
		if (auto e = structures.resolve(*index_enum))
			log.error("Cannot resolve {} index enum reference to {}", debug_name, e->name);
	if (container_type == StaticArray && extent == NoExtent) {
		if (index_enum)
			extent = (*index_enum)->count;
		else
			log.error("Missing extent for static array {}", debug_name);
	}
}

const Compound *Container::itemCompound() const
{
	return item_type.visit(overloaded {
		[](const Compound &compound) { return &compound; },
		[](const Container &container) { return container.itemCompound(); },
		[](const auto &) -> const Compound * { return nullptr; }
	});
}

std::optional<int> Container::parseIndex(std::string_view index) const
{
	if (index_enum) {
		auto it = (*index_enum)->values.find(index);
		if (it != (*index_enum)->values.end())
			return it->second.value;
	}
	auto first = index.data();
	auto last = first + index.size();
	int i;
	auto res = std::from_chars(first, last, i);
	if (res.ec != std::errc{} || res.ptr != last)
		return std::nullopt;
	return i;
}

