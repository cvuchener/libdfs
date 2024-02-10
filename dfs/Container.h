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

#ifndef DFS_CONTAINER_H
#define DFS_CONTAINER_H

#include <dfs/Type.h>

#include <memory>
#include <variant>
#include <optional>

namespace pugi { class xml_node; }

namespace dfs {

class ErrorLog;
class Structures;

/**
 * Container types can contain any number of object of a same type.
 *
 * \ingroup types
 */
struct Container: AbstractType
{
	/**
	 * Name for debugging/logging.
	 */
	std::string debug_name;

	/**
	 * Constructs a container with an item type constructed from \p args.
	 */
	template<typename... Args>
	Container(std::string_view debug_name, Args &&...args):
		debug_name(debug_name)
	{
		type_params.emplace_back(std::forward<Args>(args)...);
	}

	/**
	 * Constructs a container from xml.
	 *
	 * \param[in] debug_name used for debugging/logging
	 * \param[in] element xml element to parse
	 * \param[in] log any error occuring while parsing this element is logged
	 * \param[in] pointer_recurse when parsing again the same xml element
	 * for a container of pointers
	 */
	Container(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log, bool pointer_recurse = false);

	const AnyType &itemType() const {
		if (type_params.empty())
			throw std::runtime_error("Missing container item type");
		return type_params.front();
	}

	/**
	 * Parse a string as an index for this container.
	 *
	 * If this container is indexed by an enum, enum value name will be
	 * converted. For any container, numbers are parsed.
	 *
	 * If the string is neither a enum value name or a number
	 * `std::nullopt` is returned.
	 */
	std::optional<int> parseIndex(std::string_view index) const;

	/**
	 * Type of the contained items.
	 */
	std::vector<AnyType> type_params;
	/**
	 * If the container is indexed by a enum, a reference to it.
	 */
	std::optional<TypeRef<Enum>> index_enum;
	/**
	 * This container of pointer may contains invalid pointers.
	 */
	bool has_bad_pointers = false;

	void resolve(Structures &structures, ErrorLog &log);
};

struct PointerType: Container
{
	PointerType();
	template <typename... Args>
	PointerType(std::string_view debug_name, Args &&... args):
		Container(debug_name, std::forward<Args>(args)...)
	{
	}
	PointerType(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log);

	bool is_array = false;
};

struct StaticArray: Container
{
	StaticArray(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log);
	/**
	 * Tag for the static string constructor.
	 *
	 * \sa Container(std::string_view, static_string_t, const pugi::xml_node)
	 */
	static inline struct static_string_t {} static_string;
	/**
	 * Constructs a static string from xml.
	 */
	StaticArray(std::string_view debug_name, static_string_t, const pugi::xml_node element);
	static inline constexpr std::size_t NoExtent = -1;
	/**
	 * Extent of the array if possible, NoExtent when not applicable.
	 */
	std::size_t extent = NoExtent;

	void resolve(Structures &structures, ErrorLog &log);
};

struct StdContainer: Container
{
	enum Type {
		StdSharedPtr,	///< `std::shared_ptr<T>`
		StdVector,	///< `std::vector<T>`
		StdDeque,	///< `std::deque<T>`
		StdSet,		///< `std::set<T>`
		StdOptional,	///< `std::optional<T>`
		StdMap,		///< `std::map<T, U>`
		StdUnorderedMap,	///< `std::unordered_map<T, U>`
		StdFuture,	///< `std::future<T>`
		StdVariant,	///< `std::variant<Ts...>`
		// Type count
		Count	///< container type count
	} container_type;

	static string_map<Type> TypeNames; ///< maps xml element names to enum values
	/**
	 * Find the type for a xml element tag name.
	 *
	 * \sa TypeNames
	 */
	static std::optional<Type> typeFromTagName(std::string_view name);
	/**
	 * \returns xml tag name corresponding to \p type or `"invalid"`
	 */
	static std::string to_string(Type type);

	static constexpr bool requiresCompleteTypes(Type type)
	{
		switch (type) {
		case StdOptional:
		case StdVariant:
			return true;
		default:
			return false;
		}
	}

	/**
	 * Constructs a container from xml.
	 *
	 * \param[in] debug_name used for debugging/logging
	 * \param[in] element xml element to parse
	 * \param[in] log any error occuring while parsing this element is logged
	 * \param[in] type of the std container
	 */
	StdContainer(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log, Type container_type);
	template <typename... Args>
	StdContainer(std::string_view debug_name, Type container_type, Args &&... args):
		Container(debug_name, std::forward<Args>(args)...),
		container_type(container_type)
	{
	}
};

struct DFContainer: Container
{
	enum Type {
		DFFlagArray,	///< `struct { uint8_t *bits; uint32_t size; }`
		DFArray,	///< `struct { T *data; unsigned short size; }`
		DFLinkedList,	///< `struct linked_list_t {
				///<     T *item;
				///<     linked_list_t<T> *prev;
				///<     linked_list_t<T> *next;
				///< }`
		// Type count
		Count	///< container type count
	} container_type;

	static string_map<Type> TypeNames; ///< maps xml element names to enum values
	/**
	 * Find the type for a xml element tag name.
	 *
	 * \sa TypeNames
	 */
	static std::optional<Type> typeFromTagName(std::string_view name);
	/**
	 * \returns xml tag name corresponding to \p type or `"invalid"`
	 */
	static std::string to_string(Type type);

	std::unique_ptr<Compound> compound;

	/**
	 * Constructs a container from xml.
	 *
	 * \param[in] debug_name used for debugging/logging
	 * \param[in] element xml element to parse
	 * \param[in] log any error occuring while parsing this element is logged
	 * \param[in] type of the DF container
	 */
	DFContainer(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log, Type container_type);
	static inline struct linked_list_t {} linked_list;
	/**
	 * Constructs a container from xml.
	 *
	 * \param[in] debug_name used for debugging/logging
	 * \param[in] element xml element to parse
	 * \param[in] log any error occuring while parsing this element is logged
	 * \param[in] type of the DF container
	 */
	DFContainer(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log, linked_list_t);

	void resolve(Structures &structures, ErrorLog &log);
};

} // namespace dfs

#endif
