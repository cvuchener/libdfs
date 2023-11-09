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

#ifndef DFS_POINTER_H
#define DFS_POINTER_H

#include <dfs/Structures.h>
#include <dfs/Type.h>
#include <dfs/MemoryLayout.h>
#include <dfs/Path.h>
#include <dfs/Process.h>

namespace dfs {

/**
 * A typed pointer.
 */
struct Pointer {
	uintptr_t address;
	AnyTypeRef type;

	/**
	 * Create a Pointer from a Path \p path to a global object or
	 * one of its member.
	 *
	 * \param[in] structures df-strucutures data
	 * \param[in] version global address from this version will be used
	 * \param[in] layout offsets and size from this memory layout will be used
	 * \param[in] path path to the global/member object
	 * \param[in] process if not null, the process base offset (\sa Process::base_offset) will be used
	 *
	 * \throws std::invalid_argument if the path is invalid
	 */
	template <Path T>
	static Pointer fromGlobal(
			const Structures &structures,
			const Structures::VersionInfo &version,
			const MemoryLayout &layout,
			T &&path,
			Process *process = nullptr);
};

template <Path T>
Pointer Pointer::fromGlobal(
		const Structures &structures,
		const Structures::VersionInfo &version,
		const MemoryLayout &layout,
		T &&path,
		Process *process)
{
	if (size(path) < 1 || !holds_alternative<path::identifier>(*begin(path)))
		throw std::invalid_argument("global path must begin with an identifier");
	const auto &global_ident = std::get<path::identifier>(*begin(path));

	auto global_address = version.global_addresses.find(global_ident.identifier);
	if (global_address == version.global_addresses.end())
		throw std::invalid_argument("global object address not found");
	auto addr = global_address->second + (process ? process->base_offset() : 0);

	auto global_type = structures.findGlobalObjectType(global_ident.identifier);
	if (!global_type)
		throw std::invalid_argument("global object type not found");

	if (size(path) > 1) {
		auto compound = global_type->template get_if<Compound>();
		if (!compound)
			throw std::invalid_argument("global type is not a compound");
		auto [type, offset] = layout.getOffset(*compound, path | std::views::drop(1));
		return {addr+offset, type};
	}
	else
		return {addr, *global_type};
}

} // namespace dfs

#endif
