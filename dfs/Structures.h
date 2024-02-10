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

#ifndef DFS_STRUCTURES_H
#define DFS_STRUCTURES_H

#include <filesystem>
#include <format>
#include <ranges>

#include <dfs/Type.h>
#include <dfs/Enum.h>
#include <dfs/Bitfield.h>
#include <dfs/Compound.h>
#include <dfs/Container.h>
#include <dfs/Path.h>

namespace dfs {

struct UnresolvedReferenceError {
	std::string_view name;
};

using Logger = std::function<void(std::string_view)>;

class Structures;

class ErrorLog
{
	Logger logger;
	std::string current_file;
	bool has_errors = false;

	friend class Structures;

public:
	template <typename T> requires std::constructible_from<std::string_view, T &&>
	void error(T &&msg) {
		has_errors = true;
		logger(std::forward<T>(msg));
	}
	template <typename... Args>
	void error(std::format_string<Args...> f, Args &&...args) {
		error(std::format(f, std::forward<Args>(args)...));
	}
	template <typename... Args>
	void error(const pugi::xml_node &element, std::format_string<Args...> f, Args &&...args) {
		error(std::format(f, std::forward<Args>(args)...)
			+ format_xml_context(element));
	}
	std::string format_xml_context(const pugi::xml_node &element) const;
};

/**
 * Loads and stores data from df-structures xml.
 */
class Structures
{
	template <typename Map>
	static auto find(Map &types, std::string_view name) {
		auto it = types.find(name);
		if (it  == types.end())
			return static_cast<decltype(&it->second)>(nullptr);
		else
			return &it->second;
	}

public:
	/**
	 * Loads structures for xml in the directory \p df_structures_path.
	 *
	 * Errors will be logged using the provided \c Logger. If any error
	 * occurs the constructor will throw after having tried to load
	 * everything that it could.
	 *
	 * \throws std::runtime_error
	 */
	Structures(std::filesystem::path df_structures_path, Logger = default_logger);

	/**
	 * \returns all primitive types mapped by name.
	 */
	const string_map<PrimitiveType> &allPrimitiveTypes() const {
		return primitive_types;
	}
	/**
	 * \returns the primitive named \p name or \c nullptr if it does not
	 * exists.
	 */
	const PrimitiveType *findPrimitiveType(std::string_view name) const {
		return find(primitive_types, name);
	}
	/**
	 * \returns the type of the pointer with unknown inner type.
	 */
	const PointerType &genericPointer() const {
		return *generic_pointer;
	}
	/**
	 * \returns all top-level compound types mapped by name.
	 */
	const string_map<Compound> &allCompoundTypes() const {
		return compound_types;
	}
	/**
	 * \returns the compound type named \p name or \c nullptr if it does
	 * not exists.
	 */
	const Compound *findCompound(std::string_view name) const {
		return find(compound_types, name);
	}
	/**
	 * \returns the compound according the \p path.
	 * \throws std::invalid_argument if the path is invalid.
	 * \sa path
	 */
	template <Path T>
	const Compound *findCompound(T &&path) const;
	/**
	 * \returns all top-level enum types mapped by name.
	 */
	const string_map<Enum> &allEnumTypes() const {
		return enum_types;
	}
	/**
	 * \returns the enum type named \p name or \c nullptr if it does not
	 * exists.
	 */
	const Enum *findEnum(std::string_view name) const {
		return find(enum_types, name);
	}
	/**
	 * \returns all top-level bitfield types mapped by name.
	 */
	const string_map<Bitfield> &allBitfieldTypes() const {
		return bitfield_types;
	}
	/**
	 * \returns the enum type named \p name or \c nullptr if it does not
	 * exists.
	 */
	const Bitfield *findBitfield(std::string_view name) const {
		return find(bitfield_types, name);
	}
	/**
	 * \returns all linked list node types mapped by name.
	 */
	const string_map<DFContainer> &allLinkedListTypes() const {
		 return linked_list_types;
	}
	/**
	 * \returns all global object types mapped by the global object name.
	 */
	const string_map<AnyType> &allGlobalObjects() const {
		return global_objects;
	}
	/**
	 * \returns the type of global object named \p name or \c nullptr if
	 * does not exists.
	 */
	const AnyType *findGlobalObjectType(std::string_view name) const {
		return find(global_objects, name);
	}
	/**
	 * \returns the type of the global object or its member according to \p path.
	 * \throws std::invalid_argument if the path is invalid.
	 *
	 * \sa path
	 */
	template <Path T>
	AnyTypeRef findGlobalObjectType(T &&path) const;

	/**
	 * Information about a Dwarf Fortress version.
	 */
	struct VersionInfo
	{
		std::string version_name; ///< name for this version
		std::vector<uint8_t> id; ///< timestamp or md5 checksum identifying this version
		string_map<uintptr_t> global_addresses; ///< addresses of global objects
		string_map<uintptr_t> vtables_addresses; ///< addresses of vtable for classes
	};
	/**
	 * \returns all versions supported by this \c Structures object.
	 */
	std::span<const VersionInfo> allVersions() const {
		return versions;
	}
	/**
	 * \returns the version named \p name or \c nullptr if it does not exists.
	 */
	const VersionInfo *versionByName(std::string_view name) const {
		auto it = std::ranges::find(versions, name, &VersionInfo::version_name);
		if (it == versions.end())
			return nullptr;
		else
			return &*it;
	}
	/**
	 * \returns the version with identifier (timestamp or md5) matching \p
	 * id, or \c nullptr if does not exists.
	 */
	const VersionInfo *versionById(std::span<const uint8_t> id) const {
		auto it = std::ranges::find_if(versions, [&id](const auto &version) {
				return std::ranges::equal(version.id, id);
			});
		if (it == versions.end())
			return nullptr;
		else
			return &*it;
	}

private:
	static void default_logger(std::string_view);

	// Note: references to types must not be invalidated
	string_map<PrimitiveType> primitive_types;
	std::unique_ptr<PointerType> generic_pointer; // for unknown "pointer" types
	string_map<Compound> compound_types;
	string_map<Enum> enum_types;
	string_map<Bitfield> bitfield_types;
	string_map<DFContainer> linked_list_types;
	string_map<AnyType> global_objects;

	std::vector<VersionInfo> versions;

	std::optional<UnresolvedReferenceError> resolve(TypeRef<PrimitiveType> &ref);
	std::optional<UnresolvedReferenceError> resolve(TypeRef<Compound> &ref);
	std::optional<UnresolvedReferenceError> resolve(TypeRef<Enum> &ref);
	std::optional<UnresolvedReferenceError> resolve(TypeRef<Bitfield> &ref);
	std::optional<UnresolvedReferenceError> resolve(TypeRef<DFContainer> &ref);
	std::optional<UnresolvedReferenceError> resolve(AnyType &type, ErrorLog &errors);
	friend struct PrimitiveType;
	friend struct Compound;
	friend struct Enum;
	friend struct Bitfield;
	friend struct Container;
};

/**
 * \returns the subtype of type \p type accord to \p path.
 * \throws std::invalid_argument if the path is invalid.
 * \sa path
 */
template <Path T>
AnyTypeRef findChildType(AnyTypeRef type, T &&path)
{
	auto find_item = overloaded {
		[&](const path::identifier &id) {
			while (auto container = type.get_if<Container>())
				type = container->itemType();
			if (auto compound = type.get_if<Compound>()) {
				auto res = compound->searchMember(id.identifier);
				if (res.empty())
					throw std::invalid_argument("member not found");
				auto [parent, member_index] = res.back();
				type = parent->members[member_index].type;
			}
			else {
				throw std::invalid_argument("not a compound");
			}
		},
		[&](const path::container_of &c) {
			while (auto container = type.get_if<Container>())
				type = container->itemType();
			if (auto compound = type.get_if<Compound>()) {
				auto res = compound->searchMember(c.member);
				if (res.empty())
					throw std::invalid_argument("member not found");
				auto [parent, member_index] = res.front();
				type = parent->members[member_index].type;
			}
			else {
				throw std::invalid_argument("not a compound");
			}

		},
		[&](const path::index &) {
			if (auto container = type.get_if<Container>()) {
				// TODO: check index
				type = container->itemType();
			}
			else {
				throw std::invalid_argument("not a container");
			}
		}
	};
	for (auto item: path)
		visit(find_item, item);
	return type;
}

template <Path T>
AnyTypeRef Structures::findGlobalObjectType(T &&path) const
{
	if (size(path) < 1 || !holds_alternative<path::identifier>(*begin(path)))
		throw std::invalid_argument("global path must begin with an identifier");
	auto it = global_objects.find(get<path::identifier>(*begin(path)).identifier);
	if (it == global_objects.end())
		throw std::invalid_argument("base global not found");
	if (size(path) > 1)
		return findChildType(it->second, path | std::views::drop(1));
	else
		return it->second;
}

template <Path T>
const Compound *Structures::findCompound(T &&path) const
{
	if (size(path) < 1 || !holds_alternative<path::identifier>(*begin(path)))
		throw std::invalid_argument("compound path must begin with an identifier");
	auto it = compound_types.find(get<path::identifier>(*begin(path)).identifier);
	if (it == compound_types.end())
		throw std::invalid_argument("base compound not found");
	if (size(path) > 1) {
		AnyTypeRef type = findChildType(it->second, path | std::views::drop(1));
		while (auto container = type.get_if<Container>())
			type = container->itemType();
		if (auto compound = type.get_if<Compound>())
			return compound;
		else
			throw std::invalid_argument("not a compound");
	}
	else
		return &it->second;
}

} // namespace dfs

#endif
