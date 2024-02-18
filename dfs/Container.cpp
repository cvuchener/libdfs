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

#include "Structures.h"
#include "Compound.h"
#include "Enum.h"

#include "overloaded.h"

#include <pugixml.hpp>
using namespace pugi;

using namespace dfs;

static AnyType makeItemType(std::string_view debug_name, const xml_node element, ErrorLog &log, bool pointer_recurse)
{
	if (auto type = element.attribute("type-name"))
		return {type.value()};
	else if (auto type = element.attribute("pointer-type"))
		if (!pointer_recurse)
			return std::make_unique<PointerType>(debug_name, element, log);
		else
			return {type.value()};
	else {
		Compound compound(debug_name, element, log);
		if (compound.members.size() == 1)
			return std::move(compound.members.at(0).type);
		else
			return std::make_unique<Compound>(std::move(compound));
	}
}

Container::Container(std::string_view debug_name, const xml_node element, ErrorLog &log, bool pointer_recurse):
	debug_name(debug_name),
	has_bad_pointers(element.attribute("has-bad-pointers").as_bool())
{
	type_params.push_back(makeItemType(debug_name, element, log, pointer_recurse));
	if (auto index_enum_attr = element.attribute("index-enum"))
		index_enum.emplace(index_enum_attr.value());
}

void Container::resolve(Structures &structures, ErrorLog &log)
{
	for (auto &type: type_params) {
		if (auto e = structures.resolve(type, log))
			log.error("Cannot resolve {} item type reference to {}", debug_name, e->name);
	}
	if (index_enum)
		if (auto e = structures.resolve(*index_enum))
			log.error("Cannot resolve {} index enum reference to {}", debug_name, e->name);
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

PointerType::PointerType():
	Container("generic_pointer")
{
}

PointerType::PointerType(std::string_view debug_name, const xml_node element, ErrorLog &log):
	Container(debug_name, element, log, true),
	is_array(element.attribute("is-array").as_bool())
{
}

StaticArray::StaticArray(std::string_view debug_name, const xml_node element, ErrorLog &log):
	Container(debug_name, element, log),
	extent(element.attribute("count").as_ullong(NoExtent))
{
}

StaticArray::StaticArray(std::string_view debug_name, static_string_t, const pugi::xml_node element):
	Container(debug_name, std::in_place_type<PrimitiveType>, "static-string"),
	extent(element.attribute("size").as_ullong(0))
{
}

void StaticArray::resolve(Structures &structures, ErrorLog &log)
{
	Container::resolve(structures, log);
	if (extent == NoExtent) {
		if (index_enum)
			extent = (*index_enum)->count;
		else
			log.error("Missing extent for static array {}", debug_name);
	}
}

string_map<StdContainer::Type> StdContainer::TypeNames = {
	{ "stl-deque", StdContainer::StdDeque },
	{ "stl-future", StdContainer::StdFuture },
	{ "stl-map", StdContainer::StdMap },
	{ "stl-optional", StdContainer::StdOptional },
	{ "stl-set", StdContainer::StdSet },
	{ "stl-shared-ptr", StdContainer::StdSharedPtr },
	{ "stl-unordered-map", StdContainer::StdUnorderedMap },
	{ "stl-variant", StdContainer::StdVariant },
	{ "stl-vector", StdContainer::StdVector },
};

std::optional<StdContainer::Type> StdContainer::typeFromTagName(std::string_view name)
{
	auto it = TypeNames.find(name);
	if (it != TypeNames.end())
		return it->second;
	else
		return std::nullopt;
}

std::string StdContainer::to_string(Type type)
{
	switch (type) {
	case StdSharedPtr: return "stl-shared-ptr";
	case StdVector: return "stl-vector";
	case StdDeque: return "stl-deque";
	case StdSet: return "stl-set";
	case StdOptional: return "stl-optional";
	case StdMap: return "stl-map";
	case StdUnorderedMap: return "stl-unordered-map";
	case StdFuture: return "stl-future";
	case StdVariant: return "stl-variant";
	default: return "invalid";
	}
}

StdContainer::StdContainer(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log, Type container_type):
	Container(debug_name, element, log, false),
	container_type(container_type)
{
}

string_map<DFContainer::Type> DFContainer::TypeNames = {
	{ "df-array", DFContainer::DFArray },
	{ "df-flagarray", DFContainer::DFFlagArray },
	{ "df-linked-list-type", DFContainer::DFLinkedList },
};

std::optional<DFContainer::Type> DFContainer::typeFromTagName(std::string_view name)
{
	auto it = TypeNames.find(name);
	if (it != TypeNames.end())
		return it->second;
	else
		return std::nullopt;
}

std::string DFContainer::to_string(Type type)
{
	switch (type) {
	case DFFlagArray: return "df-flagarray";
	case DFArray: return "df-array";
	case DFLinkedList: return "df-linked-list-type";
	default: return "invalid";
	}
}

DFContainer::DFContainer(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log, Type container_type):
	Container(debug_name, element, log, false),
	container_type(container_type),
	compound(std::make_unique<Compound>())
{
	switch (container_type) {
	case DFFlagArray:
		compound->addMember<PointerType>("bits", std::make_unique<PrimitiveType>(PrimitiveType::UInt8));
		compound->addMember<PrimitiveType>("size", PrimitiveType::UInt32);
		break;
	case DFArray:
		compound->addMember<PointerType>("data", element.attribute("type-name").value());
		compound->addMember<PrimitiveType>("size", PrimitiveType::UInt16);
		break;
	default:
		throw std::invalid_argument("Invalid DF container type");
	}
}

DFContainer::DFContainer(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log, linked_list_t):
	Container(debug_name),
	container_type(DFLinkedList),
	compound(std::make_unique<Compound>())
{
	compound->debug_name = debug_name;
	auto self_type = TypeRef<DFContainer>(element.attribute("type-name").value(), this);
	compound->addMember<PointerType>("item", element.attribute("item-type").value());
	compound->addMember<PointerType>("prev", std::in_place_type<DFContainer>, self_type);
	compound->addMember<PointerType>("next", std::in_place_type<DFContainer>, self_type);
	type_params.emplace_back(std::in_place_type<PointerType>,
			"",
			&compound->members.at(0).type.get<PointerType>());
}

void DFContainer::resolve(Structures &structures, ErrorLog &log)
{
	Container::resolve(structures, log);
	compound->resolve(structures, log);
}
