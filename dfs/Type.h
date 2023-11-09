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

#ifndef DFS_TYPE_H
#define DFS_TYPE_H

#include <string>
#include <map>
#include <list>
#include <variant>
#include <functional>
#include <optional>
#include <memory>
#include <utility>
#include <stdexcept>

#include <dfs/overloaded.h>

namespace dfs {

template <typename T>
using string_map = std::map<std::string, T, std::less<>>;

class Structures;

/**
 * \defgroup types Types
 *
 * \{
 */

/**
 * Base type for all types so pointer can be casted to `AbstractType *` when
 * the type does not matters.
 */
struct AbstractType {};

/**
 * Primitive types.
 *
 * Some complex types are considered primitive because they are treated like
 * opaque objects.
 */
struct PrimitiveType: AbstractType
{
	enum Type {
		// Fixed size types
		Int8,	///< `int8_t`
		UInt8,	///< `uint8_t`
		Int16,	///< `int16_t`
		UInt16,	///< `uint16_t`
		Int32,	///< `int32_t`
		UInt32,	///< `uint32_t`
		Int64,	///< `int64_t`
		UInt64,	///< `uint64_t`
		Char,	///< `char`
		// ABI-dependant types
		Bool,	///< `bool`
		Long,	///< `long`
		ULong,	///< `unsigned long`
		SizeT,	///< `size_t`
		SFloat,	///< `float`
		DFloat,	///< `double`
		PtrString,	///< `const char *`
		StdString,	///< `std::string`
		StdBitVector,	///< `std::vector<bool>`
		StdFStream,	///< `std::fstream`
		StdMap,		///< `std::map<T, U>`
		StdUnorderedMap,	///< `std::unordered_map<T, U>`
		StdMutex,	///< `std::mutex`
		StdConditionVariable,	///< `std::condition_variable`
		StdFuture,	///< `std::future<T>`
		StdFunction,	///< `std::function<void()>`
		DFFlagArray,	///< `struct { uint8_t *bits; uint32_t size; }`
		DFArray,	///< `struct { T *data; unsigned short size; }`
		// Type count
		Count ///< primitive type count
	} type;

	static string_map<Type> TypeNames; ///< maps xml element names to enum values
	/**
	 * Find the type for a xml element tag name.
	 *
	 * \sa TypeNames
	 */
	static std::optional<Type> typeFromTagName(std::string_view name);
	/**
	 * \returns xml tag name corresponding to \p type or `"invalid"`.
	 */
	static std::string to_string(Type type);

	/**
	 * Constructs from a primitive type.
	 */
	PrimitiveType(Type type): type(type) {}
	/**
	 * Constructs from a xml tag name.
	 */
	PrimitiveType(std::string_view name);
};

/**
 * Non-owning reference to a type \p T.
 */
template <typename T>
class TypeRef
{
	std::string _name;
	T *_ptr;

public:
	using element_type = T;

	template <typename String> requires std::is_constructible_v<std::string, String &&>
	TypeRef(String &&name, T *ptr = nullptr):
		_name(std::forward<String>(name)),
		_ptr(ptr)
	{
	}

	T *get() { return _ptr; }
	const T *get() const { return _ptr; }
	T &operator*() { return *_ptr; }
	const T &operator*() const { return *_ptr; }
	T *operator->() { return _ptr; }
	const T *operator->() const { return _ptr; }

	const std::string &name() const { return _name; }

	friend class Structures;
};

/**
 * Unresolved reference whose exact type is not yet known.
 */
struct UnknownTypeRef
{
	std::string name;

	UnknownTypeRef(std::string name): name(std::move(name)) {}
};


/**
 * Variant containing a owning or non-owning reference to a type (TypeRef or `std::unique_ptr`).
 */
template <typename... Ts>
class TypeVariant
{
	std::variant<UnknownTypeRef, Ts...> _ptr;

public:
	/**
	 * Constructs an UnknownTypeRef to \p name.
	 */
	TypeVariant(std::string name):
		_ptr(std::in_place_type<UnknownTypeRef>, std::move(name))
	{
	}
	/**
	 * Constructs a TypeRef to type \p T forwarding \p args.
	 */
	template <typename T, typename... Args>
	TypeVariant(std::in_place_type_t<T>, Args &&...args):
		_ptr(std::in_place_type<TypeRef<T>>, std::forward<Args>(args)...)
	{
	}
	/**
	 * Constructs a variant owning \p ptr.
	 */
	template <typename T>
	TypeVariant(std::unique_ptr<T> &&ptr):
		_ptr(std::move(ptr))
	{
	}

	/**
	 * \returns a reference to the contained type if it is convertible to \p T.
	 * \throws std::bad_variant_access if the contained type is not convertible.
	 */
	template <typename T>
	T &get() {
		return std::visit([](const auto &ptr) -> T & {
			if constexpr (requires { {*ptr} -> std::convertible_to<T &>; })
				return *ptr;
			else
				throw std::bad_variant_access();
		}, _ptr);
	 }

	/**
	 * \overload
	 */
	template <typename T>
	const T &get() const {
		return std::visit([](const auto &ptr) -> const T & {
			if constexpr (requires { {*ptr} -> std::convertible_to<const T &>; })
				return *ptr;
			else
				throw std::bad_variant_access();
		}, _ptr);
	}

	/**
	 * \returns a pointer to the contained type if it is convertible to \p
	 * T or \c nullptr otherwise.
	 */
	template <typename T>
	const T *get_if() const {
		return std::visit([](const auto &ptr) -> const T * {
			if constexpr (requires { {ptr.get()} -> std::convertible_to<const T *>; })
				return ptr.get();
			else
				return nullptr;
		}, _ptr);
	}

	/**
	 * Visits the contained type using \p visitor.
	 *
	 * The visitor will be passed a reference to the contained type without
	 * knowing if it is owned (a `TypeRef` or `std::unique_ptr`).
	 *
	 * \code
	 * type.visit(overloaded{
	 *     [](const dfs::Compound &compound) {
	 *         // Compound case, match both TypeRef<Compound> and std::unique_ptr<Compound>
	 *     },
	 *     [](const dfs::Container &container) {
	 *         // Container case, match both TypeRef<Container> and std::unique_ptr<Container>
	 *     },
	 *     [](const dfs::AbstractType &) {
	 *         // catch-all case, match all types not derived from Compound or Container
	 *     }
	 * });
	 * \endcode
	 *
	 * \returns the visitor return value
	 */
	template <typename Visitor>
	auto visit(Visitor &&visitor) {
		using return_type = std::invoke_result_t<Visitor, decltype(*std::declval<std::variant_alternative_t<1, decltype(_ptr)>>())>;
		return std::visit(overloaded{
			[](const UnknownTypeRef &) -> return_type { throw std::invalid_argument("visiting unresolved ref"); },
			[&visitor](const auto &ptr) { return visitor(*ptr); }
		}, _ptr);
	}

	/**
	 * \overload
	 */
	template <typename Visitor>
	auto visit(Visitor &&visitor) const {
		using return_type = std::invoke_result_t<Visitor, decltype(std::as_const(*std::declval<std::variant_alternative_t<1, decltype(_ptr)>>()))>;
		return std::visit(overloaded{
			[](const UnknownTypeRef&) -> return_type { throw std::invalid_argument("visiting unresolved ref"); },
			[&visitor](const auto &ptr) { return visitor(std::as_const(*ptr)); }
		}, _ptr);
	}

	/**
	 * \returns the contained type reference name or the empty string for
	 * unnamed owned types.
	 */
	std::string name() const {
		return std::visit(overloaded{
			[](const UnknownTypeRef &ref) { return ref.name; },
			[]<typename T>(const TypeRef<T> &ref) { return ref.name(); },
			[]<typename T>(const std::unique_ptr<T> &) { return std::string(); }
		}, _ptr);
	}

	friend class Structures;
	template <typename... Us>
	friend class TypeRefVariant;
};

/**
 * Non-owning, non-mutable reference to any type \p Ts.
 */
template <typename... Ts>
class TypeRefVariant
{
	std::variant<const Ts *...> _ptr;
public:
	/**
	 * Constructs a reference from a compatible TypeVariant.
	 *
	 * \throws std::invalid_argument if \p variant is a UnknownTypeRef.
	 */
	template <typename... Us> requires (std::convertible_to<decltype(std::declval<const Us>().get()), decltype(_ptr)> && ...)
	TypeRefVariant(const TypeVariant<Us...> &variant) {
		std::visit(overloaded{
			[](const UnknownTypeRef &) { throw std::invalid_argument("unknown type ref"); },
			[this](const auto &ref) { _ptr = ref.get(); }
		}, variant._ptr);
	}

	/**
	 * Constructs a reference to type \p T.
	 */
	template <typename T> requires (std::same_as<T, Ts> || ...)
	TypeRefVariant(const T &ref):
		_ptr(&ref)
	{
	}

	/**
	 * \returns a reference to the contained type if it is convertible to \p T.
	 * \throws std::bad_variant_access if the contained type is not convertible.
	 */
	template <typename T>
	const T &get() const {
		return std::visit([](const auto *ptr) -> const T & {
			if constexpr (requires { {*ptr} -> std::convertible_to<const T &>; })
				return *ptr;
			else
				throw std::bad_variant_access();
		}, _ptr);
	}

	/**
	 * \returns a pointer to the contained type if it is convertible to \p
	 * T or \c nullptr otherwise.
	 */
	template <typename T>
	const T *get_if() const {
		return std::visit([](const auto *ptr) -> const T * {
			if constexpr (std::convertible_to<decltype(ptr), const T *>)
				return ptr;
			else
				return nullptr;
		}, _ptr);
	}

	/**
	 * Visits the contained type using \p visitor.
	 *
	 * The visitor will be passed a reference to the contained type.
	 *
	 * \returns the visitor return value
	 */
	template <typename Visitor>
	auto visit(Visitor &&visitor) const {
		return std::visit([&visitor](const auto *ptr) { return visitor(*ptr); }, _ptr);
	}
};

struct Enum;
struct Bitfield;
struct Compound;
struct Container;
struct Padding;

/**
 * Container for any type.
 */
using AnyType = TypeVariant<
	TypeRef<PrimitiveType>, std::unique_ptr<PrimitiveType>,
	TypeRef<Enum>, std::unique_ptr<Enum>,
	TypeRef<Bitfield>, std::unique_ptr<Bitfield>,
	TypeRef<Compound>, std::unique_ptr<Compound>,
	TypeRef<Container>, std::unique_ptr<Container>,
	std::unique_ptr<Padding>
>;
/**
 * Reference for any type, compatible with AnyType.
 */
using AnyTypeRef = TypeRefVariant<
	PrimitiveType,
	Enum,
	Bitfield,
	Compound,
	Container,
	Padding
>;

/// \}

} // namespace dfs

#endif
