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

#ifndef DFS_ABI_H
#define DFS_ABI_H

#include <array>
#include <limits>

#include <dfs/Type.h>
#include <dfs/Container.h>
#include <dfs/Process.h>

#include <cstring>

namespace dfs {

/**
 * \defgroup abi_error ABIError
 * \{
 */
/**
 * Errors when parsing data using ABI.
 *
 * \sa abi_error_category()
 */
enum class ABIError
{
	UnalignedPointer = 1,	///< A pointer is misaligned
	InvalidPointer,		///< A pointer is invalid
	InvalidLength,		///< Container length is invalid (or too big)
	InvalidCapacity,	///< Container capacity is invalid (or too big)
};

/**
 * Error category for ABIError.
 */
const std::error_category &abi_error_category();

std::error_code make_error_code(ABIError);
/// \}

/**
 * Size and alignment for type.
 *
 * ABI knows size and alignment for primitive and container types.
 * MemoryLayout can compute information for all types.
 *
 * \ingroup memory_layout
 */
struct TypeInfo {
	std::size_t size, align;
};


/**
 * Defines an ABI: compiler layout method, primitive and library types.
 *
 * \ingroup memory_layout process
 */
struct ABI
{
	/**
	 * Platform architecture.
	 */
	enum class Arch {
		X86,
		AMD64,
	} architecture;

	/**
	 * Compiler type (for class layout).
	 */
	enum class Compiler {
		GNU,
		MS,
	} compiler;

	/**
	 * Primitive type information.
	 */
	std::array<TypeInfo, PrimitiveType::Count> primitive_types;
	/**
	 * Pointer type information.
	 */
	TypeInfo pointer;
	/**
	 * Container type information if the container does not require
	 * complete type parameter.
	 */
	std::array<TypeInfo, StdContainer::Count> std_container_types;

	/**
	 * \returns primitive type information
	 */
	const TypeInfo &primitive_type(PrimitiveType::Type type) const {
		return primitive_types[type];
	}
	/**
	 * \returns container type information
	 */
	const TypeInfo &container_type(StdContainer::Type type) const {
		return std_container_types[type];
	}

	/**
	 * Type information for std container types that require complete type.
	 *
	 * Information about parameter type is given as \p item_type_info.
	 */
	TypeInfo (*container_info)(StdContainer::Type type, std::span<const TypeInfo> item_type_info);

	/**
	 * Parse an integer of type \p T from raw data \p data.
	 */
	template <typename T>
	static T get_integer(const uint8_t *data) {
		T value;
		std::memcpy(&value, data, sizeof(T));
		return value;
	}
	/**
	 * \overload
	 */
	template <typename T>
	static T get_integer(MemoryView data) { return get_integer<T>(data.data.data()); }

	/**
	 * Pointer to the function that can parse a pointer from raw data.
	 */
	uintptr_t (*read_pointer)(const uint8_t *);

	/**
	 * Parse a pointer from raw data \p data.
	 */
	uintptr_t get_pointer(const uint8_t *data) const { return read_pointer(data); }
	/**
	 * \overload
	 */
	uintptr_t get_pointer(MemoryView data) const { return read_pointer(data.data.data()); }

	struct vector_info
	{
		std::error_code err = {};	///< Error if the vector could not be read
		uintptr_t data = 0;		///< Address of the beggining of the vector data
		std::size_t size = 0;		///< Size of the vector (item count)
	};
	/**
	 * Reads a std::vector from raw data \p data whose item have type
	 * information \p item_type_info.
	 */
	cppcoro::task<vector_info> (*read_vector)(Process &process, MemoryView data, const TypeInfo &item_type_info);

	struct string_result
	{
		std::error_code err = {};	///< Error if the string could not be read
		std::string str;		///< Content of the string
	};
	/**
	 * Reads a std::string from raw data \p data.
	 */
	cppcoro::task<string_result> (*read_string)(Process &process, MemoryView data);

	/**
	 * Initialize type information for primitive type whose size is platform indenpendant.
	 */
	static inline constexpr void init_fixed_types(std::array<TypeInfo, PrimitiveType::Count> &a, std::size_t max_align = std::numeric_limits<std::size_t>::max())
	{
		for (const auto &[type, size]: { std::make_tuple(PrimitiveType::Char, 1),
						 std::make_tuple(PrimitiveType::Int8, 1),
						 std::make_tuple(PrimitiveType::UInt8, 1),
						 std::make_tuple(PrimitiveType::Int16, 2),
						 std::make_tuple(PrimitiveType::UInt16, 2),
						 std::make_tuple(PrimitiveType::Int32, 4),
						 std::make_tuple(PrimitiveType::UInt32, 4),
						 std::make_tuple(PrimitiveType::Int64, 8),
						 std::make_tuple(PrimitiveType::UInt64, 8) }) {
			a[type].size = size;
			a[type].align = std::min(std::size_t(size), max_align);
		}
	}

	/**
	 * Integer whose size is the same as a pointer for architecture \p arch.
	 */
	template <Arch arch>
	struct uintptr;

	/**
	 * Pointer size for architecture \p arch.
	 */
	template <Arch arch>
	static constexpr size_t pointer_size() { return sizeof(typename uintptr<arch>::type); }

	template <Arch arch, bool cxx11>
	static constexpr std::array<TypeInfo, PrimitiveType::Count> make_primitive_type_info_gcc()
	{
		auto p = pointer_size<arch>();
		std::array<TypeInfo, PrimitiveType::Count> info;
		init_fixed_types(info, p);
		info[PrimitiveType::Bool] = {1, 1};
		info[PrimitiveType::Long] = {p, p};
		info[PrimitiveType::ULong] = {p, p};
		info[PrimitiveType::SizeT] = {p, p};
		info[PrimitiveType::SFloat] = {4, 4};
		info[PrimitiveType::DFloat] = {8, p};
		info[PrimitiveType::PtrString] = {p, p};
		info[PrimitiveType::StdString] = cxx11
			? TypeInfo{2*p+16, p}
			: TypeInfo{p, p};
		info[PrimitiveType::StdBitVector] = {5*p, p};
		info[PrimitiveType::StdFStream] = {61*p+40, p};
		info[PrimitiveType::StdMutex] = {4*p+8, p};
		info[PrimitiveType::StdConditionVariable] = {48, p};
		info[PrimitiveType::StdFunction] = {4*p, p};
		info[PrimitiveType::StdFsPath] = {3*p+16, p};
		return info;
	}


	template <Arch arch, bool cxx11>
	static constexpr std::array<TypeInfo, StdContainer::Count> make_container_type_info_gcc()
	{
		auto p = pointer_size<arch>();
		std::array<TypeInfo, StdContainer::Count> info;
		info[StdContainer::StdSharedPtr] = {2*p, p};
		info[StdContainer::StdWeakPtr] = {2*p, p};
		info[StdContainer::StdVector] = {3*p, p};
		info[StdContainer::StdDeque] = cxx11
			? TypeInfo{10*p, p}
			: TypeInfo{3*p, p};
		info[StdContainer::StdSet] = {6*p, p};
		info[StdContainer::StdMap] = {6*p, p};
		info[StdContainer::StdUnorderedMap] = {7*p, p};
		info[StdContainer::StdFuture] = {2*p, p};
		return info;
	}

	template <Arch arch>
	static constexpr std::array<TypeInfo, PrimitiveType::Count> make_primitive_type_info_msvc2015()
	{
		auto p = pointer_size<arch>();
		std::array<TypeInfo, PrimitiveType::Count> info;
		init_fixed_types(info);
		info[PrimitiveType::Bool] = {1, 1};
		info[PrimitiveType::Long] = {4, 4};
		info[PrimitiveType::ULong] = {4, 4};
		info[PrimitiveType::SizeT] = {p, p};
		info[PrimitiveType::SFloat] = {4, 4};
		info[PrimitiveType::DFloat] = {8, 8};
		info[PrimitiveType::PtrString] = {p, p};
		info[PrimitiveType::StdString] = {2*p+16, p};
		info[PrimitiveType::StdBitVector] = {4*p, p};
		info[PrimitiveType::StdFStream] = {22*p+104, 8};
		info[PrimitiveType::StdMutex] = {8*p+16, p};
		info[PrimitiveType::StdConditionVariable] = {8*p+8, p};
		info[PrimitiveType::StdFunction] = {6*p+16, 8};
		info[PrimitiveType::StdFsPath] = {2*p+16, p};
		return info;
	}

	template <Arch arch>
	static constexpr std::array<TypeInfo, StdContainer::Count> make_container_type_info_msvc2015()
	{
		auto p = pointer_size<arch>();
		std::array<TypeInfo, StdContainer::Count> info;
		info[StdContainer::StdSharedPtr] = {2*p, p};
		info[StdContainer::StdWeakPtr] = {2*p, p};
		info[StdContainer::StdVector] = {3*p, p};
		info[StdContainer::StdDeque] = {5*p, p};
		info[StdContainer::StdSet] = {2*p, p};
		info[StdContainer::StdMap] = {2*p, p};
		info[StdContainer::StdUnorderedMap] = {8*p, p};
		info[StdContainer::StdFuture] = {2*p, p};
		return info;
	}

	static TypeInfo container_info_common(StdContainer::Type, std::span<const TypeInfo>);

	template <Arch arch>
	static uintptr_t read_pointer_common(const uint8_t *);

	template <Arch arch>
	static cppcoro::task<vector_info> read_vector_common(Process &process, MemoryView data, const TypeInfo &item_type_info);

	template <Arch arch>
	static cppcoro::task<string_result> read_string_gcc_cow(Process &process, MemoryView data);

	template <Arch arch>
	static cppcoro::task<string_result> read_string_gcc_sso(Process &process, MemoryView data);

	template <Arch arch>
	static cppcoro::task<string_result> read_string_msvc2015(Process &process, MemoryView data);

	static const ABI
		GCC_32,		///< pre-C++11 ABI for GCC x86
		GCC_64,		///< pre-C++11 ABI for GCC amd64
		GCC_CXX11_32,	///< C++11 ABI for GCC x86
		GCC_CXX11_64,	///< C++11 ABI for GCC amd64
		MSVC2015_32,	///< MSVC2015 (v140) ABI for x86
		MSVC2015_64;	///< MSVC2015 (v140) ABI for amd64

	/**
	 * Guess the ABI from the given Dwarf Fortress version name (from
	 * Structures::VersionInfo).
	 */
	static const ABI &fromVersionName(std::string_view name);
};

template <>
struct ABI::uintptr<ABI::Arch::X86> {
	using type = uint32_t;
};

template <>
struct ABI::uintptr<ABI::Arch::AMD64> {
	using type = uint64_t;
};

extern template uintptr_t ABI::read_pointer_common<ABI::Arch::X86>(const uint8_t *);
extern template uintptr_t ABI::read_pointer_common<ABI::Arch::AMD64>(const uint8_t *);
extern template cppcoro::task<ABI::vector_info> ABI::read_vector_common<ABI::Arch::X86>(Process &, MemoryView, const TypeInfo &);
extern template cppcoro::task<ABI::vector_info> ABI::read_vector_common<ABI::Arch::AMD64>(Process &, MemoryView, const TypeInfo &);
extern template cppcoro::task<ABI::string_result> ABI::read_string_gcc_cow<ABI::Arch::X86>(Process &, MemoryView);
extern template cppcoro::task<ABI::string_result> ABI::read_string_gcc_cow<ABI::Arch::AMD64>(Process &, MemoryView);
extern template cppcoro::task<ABI::string_result> ABI::read_string_gcc_sso<ABI::Arch::X86>(Process &, MemoryView);
extern template cppcoro::task<ABI::string_result> ABI::read_string_gcc_sso<ABI::Arch::AMD64>(Process &, MemoryView);
extern template cppcoro::task<ABI::string_result> ABI::read_string_msvc2015<ABI::Arch::X86>(Process &, MemoryView);
extern template cppcoro::task<ABI::string_result> ABI::read_string_msvc2015<ABI::Arch::AMD64>(Process &, MemoryView);

} // namespace dfs

///< \ingroup ABIError
template<>
struct std::is_error_code_enum<dfs::ABIError>: true_type {};

#endif
