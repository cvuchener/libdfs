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

#ifndef DFS_COMPOUND_H
#define DFS_COMPOUND_H

#include <vector>
#include <string>
#include <variant>
#include <optional>

#include <dfs/Type.h>

namespace pugi { class xml_node; }

namespace dfs {

class ErrorLog;
class Structures;

/**
 * Explicit padding with unknown content.
 *
 * Matches df-structures `<padding>` elements.
 *
 * \ingroup types
 */
struct Padding: AbstractType
{
	std::size_t size, align;

	Padding(std::size_t size, std::size_t align): size(size), align(align) {}
};

/**
 * Compound types (struct, class or union).
 *
 * It can be a named top-level compound or nested inside another compound,
 * container, global object, ...
 *
 * \ingroup types
 */
struct Compound: AbstractType
{
	/**
	 * Constructs an empty compound.
	 */
	Compound() = default;
	/**
	 * Constructs a generic compound from xml.
	 *
	 * \param[in] debug_name used for debugging/logging
	 * \param[in] element xml element to parse
	 * \param[in,out] log any error occuring while parsing this element is logged
	 * \param[in] vtable if the type has a vtable (from a `<class-type>` element)
	 */
	Compound(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log, bool vtable = false);
	/**
	 * Tag for the "other vectors" constructor.
	 *
	 * \sa Compound(std::string_view, const pugi::xml_node, ErrorLog &, other_vectors_t)
	 */
	static constexpr struct other_vectors_t {} other_vectors = {};
	/**
	 * Constructs a "other vectors" compound (from a `<df-other-vectors-type>` element).
	 *
	 * The compound does not have its member when first constructed. A
	 * OtherVectorsBuilder must be used to create them when corresponding
	 * Enum can be found.
	 *
	 * \param[in] debug_name used for debugging/logging
	 * \param[in] element xml element to parse
	 * \param[in,out] log any error occuring while parsing this element is logged
	 */
	Compound(std::string_view debug_name, const pugi::xml_node element, ErrorLog &log, other_vectors_t);

	struct Member;
	struct Method;

	/**
	 * Name for debugging/logging.
	 */
	std::string debug_name;
	/**
	 * If the symbol name is different from the compound name (for finding
	 * the vtable address).
	 */
	std::optional<std::string> symbol;
	/**
	 * Members.
	 */
	std::vector<Member> members;
	/**
	 * A reference to the parent if this compound inherits from one
	 */
	std::optional<TypeRef<Compound>> parent;
	/**
	 * The compound has a vtable.
	 */
	bool vtable = false;
	/**
	 * Virtual methods.
	 */
	std::vector<Method> vmethods;
	/**
	 * The compound is a `union` (otherwise a `struct` or `class`).
	 */
	bool is_union = false;

	/**
	 * Find a member by its name.
	 *
	 * The member can be nested in anonymous compound members.
	 *
	 * \returns the full path to the member. Each item is a pointer to
	 * containing compound and the index of the member. The first item
	 * compound always points to this compound. The vector is size 1 if the
	 * member is a direct member. The vector is empty if the member was not
	 * found in this compound or any nested anonymous compound.
	 */
	std::vector<std::pair<const Compound *, std::size_t>> searchMember(std::string_view name) const;

	/**
	 * Find a virtual method by name.
	 */
	template<typename String>
	const Method *method(const String &name) const;
	/**
	 * Find the index of a virtual method by name.
	 */
	template<typename String>
	int methodIndex(const String &name) const;

	template<typename T, typename String, typename... Args>
	void addMember(String &&name, Args &&...args)
	{
		if constexpr (std::same_as<T, PrimitiveType>) {
			members.emplace_back(
				std::forward<String>(name),
				std::in_place_type<T>,
				std::forward<Args>(args)...);
		}
		else {
			members.emplace_back(
				std::forward<String>(name),
				std::in_place_type<T>,
				member_debug_name(debug_name, name),
				std::forward<Args>(args)...);
		}
	}

	struct OtherVectorsBuilder
	{
		TypeRef<Enum> index_enum;
		std::string default_item_type;
		std::vector<Compound::Member> overrides;
		Compound *compound;

		OtherVectorsBuilder(const pugi::xml_node &element, Compound *compound, ErrorLog &log);

		void operator()(Structures &structures, ErrorLog &log);
	};

	void resolve(Structures &structures, ErrorLog &log);

	static std::string member_debug_name(std::string_view parent_name, std::string_view member_name);
};

/**
 * Compound member information
 */
struct Compound::Member
{
	/**
	 * Name of the member.
	 *
	 * It may be empty for anonymous members.
	 */
	std::string name;
	/**
	 * Type of the member.
	 */
	AnyType type;

	/**
	 * Constructs a member from xml.
	 */
	Member(std::string_view parent_name, std::string_view name, const pugi::xml_node element, ErrorLog &log);

	/**
	 * Constructs a member of type \p T in-place.
	 */
	template<typename T, typename... Args>
	Member(std::string_view name, std::in_place_type_t<T> in_place, Args&&... args):
		name(name),
		type(std::make_unique<T>(std::forward<Args>(args)...))
	{
	}
};

/**
 * Compound virtual method information.
 */
struct Compound::Method
{
	/**
	 * If this method is the destructor.
	 */
	bool destructor;
	/**
	 * Method name.
	 */
	std::string name;
	/**
	 * Return type if not void.
	 */
	std::optional<AnyType> return_type;
	/**
	 * Method argument names and types.
	 */
	std::vector<std::pair<std::string, AnyType>> arg_type;
};

template<typename String>
const Compound::Method *Compound::method(const String &name) const
{
	for (const auto &m: vmethods)
		if (m.name == name)
			return &m;
	return nullptr;
}

template<typename String>
int Compound::methodIndex(const String &name) const
{
	for (std::size_t i = 0; i < vmethods.size(); ++i)
		if (vmethods[i].name == name)
			return i;
	return -1;
}

} // namespace dfs

#endif
