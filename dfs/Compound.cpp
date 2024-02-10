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

#include "Compound.h"

#include <set>
#include <ranges>
#include <format>

#include "Structures.h"
#include "Enum.h"
#include "Bitfield.h"
#include "Container.h"

#include <pugixml.hpp>
using namespace pugi;

using namespace dfs;

static const std::set<std::string, std::less<>> ignore_tags = {
	"code-helper",
	"custom-methods",
	"comment",
	"extra-include",
};

static AnyType make_type(std::string_view debug_name, const xml_node &element, ErrorLog &log)
{
	std::string_view tagname = element.name();
	auto type = element.attribute("type-name");
	if (tagname == "compound") {
		if (type)
			return {type.value()};
		else
			return std::make_unique<Compound>(debug_name, element, log);
	}
	else if (tagname == "df-linked-list") {
		return {std::in_place_type<DFContainer>, type.value()};
	}
	else if (auto container_type = StdContainer::typeFromTagName(tagname)) {
		return std::make_unique<StdContainer>(debug_name, element, log, *container_type);
	}
	else if (auto container_type = DFContainer::typeFromTagName(tagname)) {
		return std::make_unique<DFContainer>(debug_name, element, log, *container_type);
	}
	else if (tagname == "pointer") {
		return std::make_unique<PointerType>(debug_name, element, log);
	}
	else if (tagname == "static-array") {
		return std::make_unique<StaticArray>(debug_name, element, log);
	}
	else if (tagname == "static-string") {
		return std::make_unique<StaticArray>(debug_name, StaticArray::static_string, element);
	}
	else if (tagname == "padding") {
		std::size_t size = element.attribute("size").as_ullong(0);
		std::size_t align = element.attribute("align").as_ullong(1);
		return std::make_unique<Padding>(size, align);
	}
	else if (tagname == "enum" || tagname == "bitfield") {
		if (type) {
			if (auto base_type_attr = element.attribute("base-type")) {
				std::string_view base_type_name = base_type_attr.value();
				auto base_type = PrimitiveType::typeFromTagName(base_type_name);
				if (base_type)
					return std::make_unique<PrimitiveType>(*base_type);
				else {
					log.error(element, "{}: enum/bitfield base type \"{}\" is not a primitive type.", debug_name, base_type_name);
					return std::make_unique<PrimitiveType>(PrimitiveType::Int32);
				}
			}
			else if (tagname == "enum")
				return {std::in_place_type<Enum>, type.value()};
			else // tagname == "bitfield"
				return {std::in_place_type<Bitfield>, type.value()};
		}
		else {
			if (tagname == "enum")
				return std::make_unique<Enum>(debug_name, element, log);
			else // tagname == "bitfield"
				return std::make_unique<Bitfield>(debug_name, element, log);
		}
	}
	else {
		return {std::in_place_type<PrimitiveType>, tagname};
	}
}

static void make_method(std::string_view parent_name, Compound::Method &method, const xml_node &element, ErrorLog &log)
{
	using namespace std::literals;
	if (auto is_destructor = element.attribute("is-destructor"))
		method.destructor = ("true"s == is_destructor.value());
	else
		method.name = element.attribute("name").value();
	if (auto ret_type = element.attribute("ret-type"))
		method.return_type.emplace(ret_type.value());
	for (auto child: element.children()) {
		if (child.type() != node_element)
			continue;
		std::string_view tagname = child.name();
		if (tagname == "ret-type") {
			if (auto ret_type = child.attribute("type-name"))
				method.return_type.emplace(ret_type.value());
			else if (auto t = child.first_child())
				method.return_type = make_type(
						std::format("{}::{} return", parent_name, method.name),
						t, log);
			else
				log.error(child, "{}::{}: Empty ret-type element", parent_name, method.name);
		}
		else if (ignore_tags.count(tagname) == 0) {
			auto param_name = child.attribute("name").value();
			method.arg_type.emplace_back(
					param_name,
					make_type(
						std::format("{}::{} parameter {}", parent_name, method.name, param_name),
						child, log));
		}
	}
}

Compound::Compound(std::string_view debug_name, const xml_node element, ErrorLog &log, bool vtable):
	debug_name(debug_name),
	vtable(vtable)
{
	if (auto parent_attr = element.attribute("inherits-from"))
		parent.emplace(parent_attr.value());

	is_union = element.attribute("is-union").as_bool(false);

	if (auto original_name = element.attribute("original-name"))
		symbol = original_name.value();

	for (auto child: element.children()) {
		if (child.type() != node_element)
			continue;
		std::string_view tagname = child.name();
		auto name = child.attribute("name").value();
		if (tagname == "virtual-methods") {
			if (!vtable) {
				log.error(child, "{}: Adding virtual methods without a vtable", debug_name);
				continue;
			}
			for (auto method: child.children("vmethod")) {
				make_method(debug_name, vmethods.emplace_back(), method, log);
			}
		}
		else if (ignore_tags.count(tagname) == 0) {
			members.emplace_back(debug_name, name, child, log);
		}
	}
}

Compound::Compound(std::string_view debug_name, const xml_node element, ErrorLog &log, other_vectors_t):
	debug_name(debug_name)
{
}

std::vector<std::pair<const Compound *, std::size_t>> Compound::searchMember(std::string_view name) const
{
	std::vector<std::pair<const Compound *, std::size_t>> stack = {{this, -1}};
	while (!stack.empty()) {
		auto &[compound, i] = stack.back();
		++i;
		if (i >= compound->members.size()) {
			stack.pop_back();
			continue;
		}
		const auto &member = compound->members[i];
		if (member.name.empty()) {
			if (auto anon_compound = member.type.get_if<Compound>())
				stack.emplace_back(anon_compound, -1);
		}
		else if (member.name == name)
			break;
	}
	return stack;
}

Compound::OtherVectorsBuilder::OtherVectorsBuilder(const pugi::xml_node &element, Compound *compound, ErrorLog &log):
	index_enum(element.attribute("index-enum").value()),
	default_item_type(element.attribute("item-type").value()),
	compound(compound)
{
	for (auto child: element.children("stl-vector")) {
		auto name = child.attribute("name").value();
		overrides.emplace_back(compound->debug_name, name, child, log);
	}
}

void Compound::OtherVectorsBuilder::operator()(Structures &structures, ErrorLog &log)
{
	structures.resolve(index_enum);

	// Sort enum values
	std::vector<std::string_view> names;
	names.reserve(index_enum->count);
	for (const auto &[name, item]: index_enum->values) {
		if (item.value < 0)
			continue;
		if (unsigned(item.value) >= names.size())
			names.resize(item.value+1);
		names[item.value] = name;
	}

	// Add members in index order
	for (auto name: names) {
		if (name.empty()) {
			log.error("missing name for member {} in other-vectors compound {}",
					compound->members.size(), compound->debug_name);
		}
		auto it = std::ranges::find(overrides, name, &Compound::Member::name);
		if (it == overrides.end())
			compound->addMember<StdContainer>(
					name,
					StdContainer::StdVector,
					std::make_unique<PointerType>(
						member_debug_name(compound->debug_name, name),
						default_item_type));
		else
			compound->members.push_back(std::move(*it));
	}
}

Compound::Member::Member(std::string_view parent_name, std::string_view name, const xml_node element, ErrorLog &log):
	name(name),
	type(make_type(member_debug_name(parent_name, name), element, log))
{
}

void Compound::resolve(Structures &structures, ErrorLog &log)
{
	if (parent) {
		if (auto e = structures.resolve(*parent))
			log.error("Cannot resolve {} parent reference to {}",
					debug_name, e->name);
	}
	for (auto &[name, type]: members) {
		if (auto e = structures.resolve(type, log))
			log.error("Cannot resolve {} member {} reference to {}",
					debug_name, name, e->name);
	}
	for (auto &method: vmethods) {
		if (method.return_type) {
			if (auto e = structures.resolve(*method.return_type, log))
				log.error("Cannot resolve {}::{} return type reference to {}",
						debug_name, method.name, e->name);
		}
		for (auto &[name, type]: method.arg_type) {
			if (auto e = structures.resolve(type, log))
				log.error("Cannot resolve {}::{} parameter {} reference to {}",
						debug_name, method.name, name, e->name);
		}
	}
}

std::string Compound::member_debug_name(std::string_view parent_name, std::string_view member_name)
{
	return std::format("{}.{}", parent_name, member_name);
}
