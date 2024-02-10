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

#include "ABI.h"

#include <algorithm>
#include <format>
#include <regex>
#include <system_error>

#include "Structures.h"

using namespace dfs;

static constexpr std::size_t MaxStringCapacity = 1000000;

class abi_error_category_t: public std::error_category
{
public:
	abi_error_category_t()
	{
	}

	const char *name() const noexcept override
	{
		return "abi";
	}

	std::string message(int err) const override
	{
		switch (ABIError{err}) {
		case ABIError::UnalignedPointer:
			return "Unaligned pointer";
		case ABIError::InvalidPointer:
			return "Invalid pointer";
		case ABIError::InvalidLength:
			return "Invalid length";
		case ABIError::InvalidCapacity:
			return "Invalid capacity";
		}
#ifdef __GNUC__ // GCC, Clang, ICC
		__builtin_unreachable();
#elifdef _MSC_VER // MSVC
		__assume(false);
#endif
	}
};

const std::error_category &dfs::abi_error_category()
{
	static abi_error_category_t category;
	return category;
}

std::error_code dfs::make_error_code(ABIError err)
{
	return {static_cast<int>(err), abi_error_category()};
}

const ABI &ABI::fromVersionName(std::string_view name)
{
	static std::regex version_re("v0\\.([0-9]+)\\.([0-9]+)([^ ]*) ([^ ]+) *([^ ]*)");
	std::match_results<std::string_view::const_iterator> res;
	if (!regex_search(name.begin(), name.end(), res, version_re))
		throw std::runtime_error("Failed to parse version name");
	auto major = std::stoul(res[1]);
	//auto minor = std::stoul(res[2]);
	//auto extra = res[3];
	auto platform = res[4];
	//auto dist = res[5];
	if (platform == "linux32") {
		if (major >= 50)
			return GCC_CXX11_32;
		else
			return GCC_32;
	}
	else if (platform == "linux64") {
		if (major >= 50)
			return GCC_CXX11_64;
		else
			return GCC_64;
	}
	else if (platform == "win32")
		return MSVC2015_32;
	else if (platform == "win64")
		return MSVC2015_64;
	else
		throw std::runtime_error(std::format("Unsupported abi for {}", name));
}

TypeInfo ABI::container_info_common(StdContainer::Type type, std::span<const TypeInfo> item_type_info)
{
	switch (type) {
	case StdContainer::StdOptional:
		if (item_type_info.size() != 1)
			throw std::invalid_argument("std::optional requires one type parameter");
		return {
			item_type_info.front().align + item_type_info.front().size,
			item_type_info.front().align
		};
	case StdContainer::StdVariant:
		if (item_type_info.size() >= 1) {
			using namespace std::ranges;
			auto size = max_element(item_type_info, {}, &TypeInfo::size)->size;
			auto align = max_element(item_type_info, {}, &TypeInfo::align)->align;
			return {
				align + size,
				align
			};
		}
		else
			throw std::invalid_argument("std::variant requires at least one type parameter");
	default:
		throw std::invalid_argument("container info does not depends on type parameters");
	}
}

template <ABI::Arch arch>
uintptr_t ABI::read_pointer_common(const uint8_t *data)
{
	return get_integer<typename uintptr<arch>::type>(data);
}

template uintptr_t ABI::read_pointer_common<ABI::Arch::X86>(const uint8_t *);
template uintptr_t ABI::read_pointer_common<ABI::Arch::AMD64>(const uint8_t *);

template <ABI::Arch arch>
cppcoro::task<ABI::vector_info> ABI::read_vector_common(Process &process, MemoryView data, const TypeInfo &item_type_info)
{
	// struct vector {
	//     T *begin;
	//     T *end;
	//     T *end_capacity;
	// };
	using Uintptr = uintptr<arch>::type;
	std::array<Uintptr, 3> ptr;
	std::memcpy(ptr.data(), data.data.data(), ptr.size() * sizeof(Uintptr));
	if (std::all_of(ptr.begin(), ptr.end(), [](auto ptr){return ptr == 0;}))
		co_return vector_info{};
	if (std::any_of(ptr.begin(), ptr.end(), [align = item_type_info.align](auto ptr){return ptr%align != 0;}))
		co_return vector_info{ABIError::UnalignedPointer};
	if (ptr[1] < ptr[0] || (ptr[1]-ptr[0])%item_type_info.size != 0)
		co_return vector_info{ABIError::InvalidLength};
	if (ptr[2] < ptr[1] || (ptr[2]-ptr[0])%item_type_info.size != 0)
		co_return vector_info{ABIError::InvalidCapacity};
	co_return vector_info{{}, ptr[0], (ptr[1]-ptr[0])/item_type_info.size};
}

template cppcoro::task<ABI::vector_info> ABI::read_vector_common<ABI::Arch::X86>(Process &, MemoryView, const TypeInfo &);
template cppcoro::task<ABI::vector_info> ABI::read_vector_common<ABI::Arch::AMD64>(Process &, MemoryView, const TypeInfo &);

template <ABI::Arch arch>
cppcoro::task<ABI::string_result> ABI::read_string_gcc_cow(Process &process, MemoryView data)
{
	// struct string {
	//     char *data;
	// };
	// struct rep_t {
	//     size_t length;
	//     size_t capacity;
	//     size_t refcount;
	// };
	// A rep_t is prepended to the string data.
	using Uintptr = uintptr<arch>::type;
	std::error_code err;
	auto addr = get_integer<Uintptr>(data);
	struct {
		Uintptr length;
		Uintptr capacity;
		Uintptr refcount;
	} rep;
	if (auto err = co_await process.read(addr-sizeof(rep), rep))
		co_return string_result{err};
	if (rep.capacity > MaxStringCapacity)
		co_return string_result{ABIError::InvalidCapacity};
	if (rep.length > rep.capacity)
		co_return string_result{ABIError::InvalidCapacity};
	std::string str(rep.length, '\0');
	if (auto err = co_await process.read({addr, {reinterpret_cast<uint8_t *>(str.data()), rep.length}}))
		co_return string_result{err};
	co_return string_result{{}, std::move(str)};
}

template cppcoro::task<ABI::string_result> ABI::read_string_gcc_cow<ABI::Arch::X86>(Process &, MemoryView);
template cppcoro::task<ABI::string_result> ABI::read_string_gcc_cow<ABI::Arch::AMD64>(Process &, MemoryView);

template <ABI::Arch arch>
cppcoro::task<ABI::string_result> ABI::read_string_gcc_sso(Process &process, MemoryView data)
{
	// struct string {
	//     char *data;
	//     size_t length;
	//     union {
	//         char local_data[16];
	//         size_t capacity;
	//     };
	// };
	using Uintptr = uintptr<arch>::type;
	std::error_code err;
	auto buffer = get_integer<Uintptr>(data);
	auto length = get_integer<Uintptr>(data.subview(sizeof(Uintptr)));
	auto local_buffer = data.subview(2*sizeof(Uintptr), 16);
	bool is_local = buffer == local_buffer.address;
	auto capacity = is_local
		? 15
		: get_integer<Uintptr>(local_buffer);
	if (capacity > MaxStringCapacity)
		co_return string_result{ABIError::InvalidCapacity};
	if (length > capacity)
		co_return string_result{ABIError::InvalidCapacity};
	string_result res;
	if (!is_local) {
		res.str.resize(length);
		res.err = co_await process.read({buffer, {reinterpret_cast<uint8_t *>(res.str.data()), length}});
	}
	else
		res.str.assign(reinterpret_cast<const char *>(local_buffer.data.data()), length);
	co_return res;
}

template cppcoro::task<ABI::string_result> ABI::read_string_gcc_sso<ABI::Arch::X86>(Process &, MemoryView);
template cppcoro::task<ABI::string_result> ABI::read_string_gcc_sso<ABI::Arch::AMD64>(Process &, MemoryView);

template <ABI::Arch arch>
cppcoro::task<ABI::string_result> ABI::read_string_msvc2015(Process &process, MemoryView data)
{
	// struct string {
	//     union {
	//         char local_data[16];
	//         char *data;
	//     };
	//     size_t length;
	//     size_t capacity;
	// };
	using Uintptr = uintptr<arch>::type;
	auto length = get_integer<Uintptr>(data.subview(16));
	auto capacity = get_integer<Uintptr>(data.subview(16+sizeof(Uintptr)));
	if (capacity > MaxStringCapacity)
		co_return string_result{ABIError::InvalidCapacity};
	if (length > capacity)
		co_return string_result{ABIError::InvalidCapacity};
	string_result res;
	if (capacity > 15) {
		res.str.resize(length);
		res.err = co_await process.read({
				get_integer<Uintptr>(data), {
					reinterpret_cast<uint8_t *>(res.str.data()),
					length
				}});
	}
	else
		res.str.assign(reinterpret_cast<const char *>(data.data.data()), length);
	co_return res;
}

template cppcoro::task<ABI::string_result> ABI::read_string_msvc2015<ABI::Arch::X86>(Process &, MemoryView);
template cppcoro::task<ABI::string_result> ABI::read_string_msvc2015<ABI::Arch::AMD64>(Process &, MemoryView);

template <ABI::Arch arch>
static constexpr TypeInfo PointerInfo = {
	ABI::pointer_size<arch>(),
	ABI::pointer_size<arch>()
};

const ABI ABI::GCC_32 = ABI{ Arch::X86, Compiler::GNU,
			make_primitive_type_info_gcc<Arch::X86, false>(),
			PointerInfo<Arch::X86>,
			make_container_type_info_gcc<Arch::X86, false>(),
			container_info_common,
			read_pointer_common<Arch::X86>,
			read_vector_common<Arch::X86>,
			read_string_gcc_cow<Arch::X86> };
const ABI ABI::GCC_64 = ABI{ Arch::AMD64, Compiler::GNU,
			make_primitive_type_info_gcc<Arch::AMD64, false>(),
			PointerInfo<Arch::AMD64>,
			make_container_type_info_gcc<Arch::AMD64, false>(),
			container_info_common,
			read_pointer_common<Arch::AMD64>,
			read_vector_common<Arch::AMD64>,
			read_string_gcc_cow<Arch::AMD64> };
const ABI ABI::GCC_CXX11_32 = ABI{ Arch::X86, Compiler::GNU,
			make_primitive_type_info_gcc<Arch::X86, true>(),
			PointerInfo<Arch::X86>,
			make_container_type_info_gcc<Arch::X86, true>(),
			container_info_common,
			read_pointer_common<Arch::X86>,
			read_vector_common<Arch::X86>,
			read_string_gcc_sso<Arch::X86> };
const ABI ABI::GCC_CXX11_64 = ABI{ Arch::AMD64, Compiler::GNU,
			make_primitive_type_info_gcc<Arch::AMD64, true>(),
			PointerInfo<Arch::AMD64>,
			make_container_type_info_gcc<Arch::AMD64, true>(),
			container_info_common,
			read_pointer_common<Arch::AMD64>,
			read_vector_common<Arch::AMD64>,
			read_string_gcc_sso<Arch::AMD64> };
const ABI ABI::MSVC2015_32 = ABI{ Arch::X86, Compiler::MS,
			make_primitive_type_info_msvc2015<Arch::X86>(),
			PointerInfo<Arch::X86>,
			make_container_type_info_msvc2015<Arch::X86>(),
			container_info_common,
			read_pointer_common<Arch::X86>,
			read_vector_common<Arch::X86>,
			read_string_msvc2015<Arch::X86> };
const ABI ABI::MSVC2015_64 = ABI{ Arch::AMD64, Compiler::MS,
			make_primitive_type_info_msvc2015<Arch::AMD64>(),
			PointerInfo<Arch::AMD64>,
			make_container_type_info_msvc2015<Arch::AMD64>(),
			container_info_common,
			read_pointer_common<Arch::AMD64>,
			read_vector_common<Arch::AMD64>,
			read_string_msvc2015<Arch::AMD64> };
