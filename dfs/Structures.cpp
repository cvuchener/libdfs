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

#include "Structures.h"

#include <regex>
#include <charconv>
#include <iostream>

#include "Compound.h"
#include "Container.h"
#include "Enum.h"
#include "Bitfield.h"

#include <pugixml.hpp>
using namespace pugi;

using namespace dfs;

namespace fs = std::filesystem;

Structures::Structures(fs::path df_structures_path, Logger logger)
{
	ErrorLog log;
	log.logger = std::move(logger);

	// Create built-in primitive types
	for (auto [name, type]: PrimitiveType::TypeNames)
		primitive_types.emplace(name, type);
	generic_pointer = std::make_unique<PointerType>();

	std::regex df_types_xml("df\\..*\\.xml");

	auto add_type = [&log]<typename T, typename... Args>(
			const xml_node element,
			string_map<T> &types,
			Args &&... args)
			-> T *
	{
		std::string_view type_name = element.attribute("type-name").value();
		auto [it, inserted] = types.emplace(std::piecewise_construct,
				std::forward_as_tuple(type_name),
				std::forward_as_tuple(type_name, element, log, std::forward<Args>(args)...));
		if (!inserted) {
			log.error(element, "Duplicated type {}.", type_name);
			return nullptr;
		}
		else
			return &it->second;
	};

	std::vector<Compound::OtherVectorsBuilder> other_vectors_builders;

	// Read structures files
	for (const auto &e: fs::directory_iterator(df_structures_path)) {
		auto filename = e.path().filename().string();
		std::smatch match;
		if (!std::regex_match(filename, match, df_types_xml))
			continue;
		xml_document doc;
		auto res = doc.load_file(e.path().c_str());
		if (!res) {
			log.error("Failed to parse {}: {}.", filename, res.description());
			continue;
		}
		log.current_file = filename;

		for (auto element: doc.document_element().children()) {
			if (element.type() != node_element)
				continue;
			std::string_view tagname = element.name();
			if (tagname == "struct-type")
				add_type(element, compound_types);
			else if (tagname == "class-type")
				add_type(element, compound_types, true);
			else if (tagname == "df-linked-list-type")
				add_type(element, linked_list_types, DFContainer::linked_list);
			else if (tagname == "df-other-vectors-type") {
				auto *c = add_type(element, compound_types, Compound::other_vectors);
				other_vectors_builders.emplace_back(element, c, log);
			}
			else if (tagname == "enum-type")
				add_type(element, enum_types);
			else if (tagname == "bitfield-type")
				add_type(element, bitfield_types);
			else if (tagname == "global-object") {
				auto name = element.attribute("name").value();
				auto type_name = element.attribute("type-name");
				if (type_name)
					global_objects.emplace(name, type_name.value());
				else
					global_objects.emplace(name, std::make_unique<Compound>(name, element, log));
			}
			else {
				log.error(element, "Unknown type tag: {}.", tagname);
			}
		}
	}

	for (auto &builder: other_vectors_builders)
		builder(*this, log);

	for (auto &[name, type]: global_objects)
		resolve(type, log);
	for (auto &[name, type]: enum_types)
		type.resolve(*this, log);
	for (auto &[name, type]: compound_types)
		type.resolve(*this, log);
	for (auto &[name, type]: linked_list_types)
		type.resolve(*this, log);

	// Read symbols
	xml_document symbols;
	if (auto res = symbols.load_file((df_structures_path/"symbols.xml").c_str())) {
		log.current_file = "symbols.xml";
		for (auto symbol_table: symbols.document_element().children("symbol-table")) {
			VersionInfo &vi = versions.emplace_back();
			vi.version_name = symbol_table.attribute("name").as_string();
			for (auto element: symbol_table.children()) {
				if (element.type() != node_element)
					continue;
				std::string_view tagname = element.name();
				if (tagname == "binary-timestamp") {
					uint32_t timestamp = element.attribute("value").as_uint();
					vi.id.resize(sizeof(timestamp));
					for (std::size_t i = 0; i < sizeof(timestamp); ++i)
						vi.id[i] = uint8_t(timestamp >> (sizeof(timestamp)-1-i)*8);
				}
				else if (tagname == "md5-hash") {
					auto value = element.attribute("value").value();
					vi.id.resize(16);
					for (std::size_t i = 0; i < 16; ++i) {
						auto res = std::from_chars(&value[2*i], &value[2*(i+1)], vi.id[i], 16);
						if (res.ptr != &value[2*(i+1)] || res.ec != std::errc{}) {
							log.error(element, "invalid md5 string");
							break;
						}
					}
				}
				else if (tagname == "global-address") {
					auto [it, inserted] = vi.global_addresses.emplace(
							element.attribute("name").value(),
							element.attribute("value").as_ullong());
					if (!inserted)
						log.error(element, "Duplicate global-address for {}", it->first);
				}
				else if (tagname == "vtable-address") {
					auto [it, inserted] = vi.vtables_addresses.emplace(
							element.attribute("name").value(),
							element.attribute("value").as_ullong());
					if (!inserted)
						log.error(element, "Duplicate vtable-address for {}", it->first);
				}
				else {
					log.error(element, "Unknown element {} in symbol-table", tagname);
				}
			}
		}
	}
	else
		log.error("Failed to parse symbols.xml: {}", res.description());

	if (log.has_errors)
		throw std::runtime_error("Failed to parse structures xml");
}

std::optional<UnresolvedReferenceError> Structures::resolve(TypeRef<PrimitiveType> &ref)
{
	if (!(ref._ptr = find(primitive_types, ref._name)))
		return UnresolvedReferenceError{ref._name};
	return std::nullopt;
}

std::optional<UnresolvedReferenceError> Structures::resolve(TypeRef<Compound> &ref)
{
	if (!(ref._ptr = find(compound_types, ref._name)))
		return UnresolvedReferenceError{ref._name};
	return std::nullopt;
}

std::optional<UnresolvedReferenceError> Structures::resolve(TypeRef<Enum> &ref)
{
	if (!(ref._ptr = find(enum_types, ref._name)))
		return UnresolvedReferenceError{ref._name};
	return std::nullopt;
}

std::optional<UnresolvedReferenceError> Structures::resolve(TypeRef<Bitfield> &ref)
{
	if (!(ref._ptr = find(bitfield_types, ref._name)))
		return UnresolvedReferenceError{ref._name};
	return std::nullopt;
}

std::optional<UnresolvedReferenceError> Structures::resolve(TypeRef<DFContainer> &ref)
{
	if (!(ref._ptr = find(linked_list_types, ref._name)))
		return UnresolvedReferenceError{ref._name};
	return std::nullopt;
}

template <typename T>
concept needs_resolving = requires (T &t, Structures &s, ErrorLog &l) { t.resolve(s, l); };

std::optional<UnresolvedReferenceError> Structures::resolve(AnyType &type, ErrorLog &log)
{
	static_assert(needs_resolving<Compound>);
	static_assert(needs_resolving<Container>);
	static_assert(needs_resolving<Enum>);
	return visit(overloaded{
		[&](UnknownTypeRef &ref) -> std::optional<UnresolvedReferenceError> {
			auto &&name = std::move(ref.name);
			if (auto ptr = find(primitive_types, name))
				type._ptr = TypeRef<PrimitiveType>{std::move(name), ptr};
			else if (auto ptr = find(compound_types, name))
				type._ptr = TypeRef<Compound>{std::move(name), ptr};
			else if (auto ptr = find(enum_types, name))
				type._ptr = TypeRef<Enum>{std::move(name), ptr};
			else if (auto ptr = find(bitfield_types, name))
				type._ptr = TypeRef<Bitfield>{std::move(name), ptr};
			else if (auto ptr = find(linked_list_types, name))
				type._ptr = TypeRef<DFContainer>{std::move(name), ptr};
			else if (name == "pointer")
				type._ptr = TypeRef<PointerType>{std::move(name), generic_pointer.get()};
			else
				return UnresolvedReferenceError{ref.name};
			return std::nullopt;
		},
		[this]<typename T>(TypeRef<T> &ref) -> std::optional<UnresolvedReferenceError> {
			if constexpr (requires {resolve(ref);})
				return resolve(ref);
			else
				return UnresolvedReferenceError{ref._name};
		},
		[this, &log]<typename T>(std::unique_ptr<T> &ptr) -> std::optional<UnresolvedReferenceError> {
			if constexpr (needs_resolving<T>)
				ptr->resolve(*this, log);
			return std::nullopt;
		},
	}, type._ptr);
}

std::string ErrorLog::format_xml_context(const xml_node &element) const
{
	return std::format(" (in {}:{})", current_file, element.offset_debug());
}

void Structures::default_logger(std::string_view message)
{
	std::cerr << message << std::endl;
}
