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

#include "Enum.h"

#include <charconv>
#include <format>

#include "Structures.h"

#include <pugixml.hpp>
using namespace pugi;

using namespace dfs;

Enum::Enum(std::string_view debug_name, const xml_node element, ErrorLog &log):
	PrimitiveType(element.attribute("base-type").as_string("uint32_t")),
	debug_name(debug_name)
{
	int next_value = 0;
	for (auto child: element.children()) {
		if (child.type() != node_element)
			continue;
		std::string_view tagname = child.name();
		if (tagname == "enum-item") {
			next_value = child.attribute("value").as_int(next_value);
			auto name = child.attribute("name");
			auto ret = values.emplace(name.value(), next_value++);
			if (!ret.second) {
				// some items are unnamed and will overwrite themselves
				if (name)
					log.error(child, "{}: Duplicate enum item: {}.", debug_name, name.value());
				continue;
			}
			count = next_value;
			auto &item = ret.first->second;
			for (auto attr: child.children("item-attr")) {
				item.attributes.emplace(attr.attribute("name").value(),
							std::string(attr.attribute("value").value()));
			}
		}
		else if (tagname == "enum-attr") {
			std::string_view name = child.attribute("name").value();
			auto type_name = child.attribute("type-name");
			auto default_value = child.attribute("default-value");
			auto [it, inserted] = attributes.emplace(std::piecewise_construct,
						      std::forward_as_tuple(name),
						      std::forward_as_tuple());
			if (!inserted) {
				log.error(child, "{}: Duplicate attribute name: {}.", debug_name, name);
				continue;
			}
			auto &attr = it->second;
			if (type_name)
				attr.type.emplace(type_name.value());
			if (default_value)
				attr.default_value = std::string(default_value.value());
		}
	}
}

template<typename T>
static T parseIntValue(std::string_view str)
{
	T value;
	auto first = str.data();
	auto last = first + str.size();
	auto res = std::from_chars(first, last, value);
	if (res.ec != std::errc{} || res.ptr != last)
		throw std::invalid_argument(std::format("\"{}\" is not an integer", str));
	return value;
}

static Enum::AttributeValue parse(const AnyType &type, std::string_view value)
{
	return type.visit(overloaded{
		[&](const PrimitiveType &type) -> Enum::AttributeValue {
			switch (type.type) {
			case PrimitiveType::Bool:
				if (value == "true")
					return true;
				if (value == "false")
					return false;
				throw std::invalid_argument(std::format("\"{}\" is not a boolean", value));
			case PrimitiveType::Int8:
			case PrimitiveType::UInt8:
			case PrimitiveType::Int16:
			case PrimitiveType::UInt16:
			case PrimitiveType::Int32:
			case PrimitiveType::UInt32:
			case PrimitiveType::Int64:
			case PrimitiveType::Long:
				return parseIntValue<long long>(value);
			case PrimitiveType::UInt64:
				return parseIntValue<unsigned long long>(value);
			default:
				throw std::invalid_argument("invalid type for enum attribute value");
			}
		},
		[&](const Enum &type) -> Enum::AttributeValue {
			auto it = type.values.find(value);
			if (it == type.values.end())
				throw std::invalid_argument(std::format("Unknown enum value: {}", value));
			return it;
		},
		[&](const auto &) -> Enum::AttributeValue {
			throw std::invalid_argument("invalid type for enum attribute value");
		}});
}

void Enum::resolve(Structures &structures, ErrorLog &log)
{
	for (auto &[name, attr]: attributes) {
		if (attr.type) {
			if (auto e = structures.resolve(*attr.type, log)) {
				log.error("Cannot resolve {} attribute {} type reference to {}",
						debug_name, name, e->name);
				attr.type.reset();
			}
			else if (attr.default_value) {
				try {
					attr.default_value = parse(*attr.type, std::get<std::string>(attr.default_value.value()));
				}
				catch (std::exception &e) {
					log.error("{}: Failed to parse default value for attribute {}: {}.",
							debug_name, name, e.what());
				}
			}
		}
	}
	for (auto &[name, item]: values) {
		for (auto &[attr_name, attr_value]: item.attributes) {
			auto it = attributes.find(attr_name);
			if (it == attributes.end())
				log.error("{}: Unknown enum attribute {}.", debug_name, attr_name);
			else if (it->second.type) {
				try {
					attr_value = parse(*it->second.type, std::get<std::string>(attr_value));
				}
				catch (std::exception &e) {
					log.error("{}: Failed to parse value for attribute {} of item {}: {}.",
							debug_name, attr_name, name, e.what());
				}
			}
		}
	}
}

