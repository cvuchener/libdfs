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

#ifndef DFS_MEMORY_LAYOUT_H
#define DFS_MEMORY_LAYOUT_H

#include <dfs/Type.h>
#include <dfs/ABI.h>
#include <dfs/Path.h>
#include <dfs/Compound.h>
#include <dfs/Enum.h>

namespace dfs {

/**
 * \defgroup memory_layout Memory layout
 *
 * TypeInfo and CompoundLayout give information about types. ABI holds the
 * information for generic types. MemoryLayout compute information for types
 * from a Structures.
 */

/**
 * Extra layout information for a Compound in addition to TypeInfo.
 *
 * \ingroup memory_layout
 */
struct CompoundLayout
{
	/**
	 * Size of the compound without the end padding.
	 */
	std::size_t unaligned_size;
	/**
	 * Offset for each member. Indices are the same as Compound::members.
	 */
	std::vector<std::size_t> member_offsets;
};

/**
 * Sizes, alignments and offsets for all types and compound members.
 *
 * \ingroup memory_layout
 */
struct MemoryLayout
{
	/**
	 * Type/member information from \p structures and \p abi.
	 */
	MemoryLayout(const Structures &structures, const ABI &abi);

	/**
	 * Mapping each type to its size and alignment.
	 */
	std::unordered_map<const AbstractType *, TypeInfo> type_info;
	/**
	 * Mapping each compound type to its layout information.
	 */
	std::unordered_map<const Compound *, CompoundLayout> compound_layout;

	/**
	 * \returns type info for \p type.
	 * \throws std::out_of_range if the type is not valid.
	 */
	inline const TypeInfo &getTypeInfo(const AnyType &type) const {
		return type_info.at(&type.get<AbstractType>());
	}
	/** \overload */
	inline const TypeInfo &getTypeInfo(const AnyTypeRef &type) const {
		return type_info.at(&type.get<AbstractType>());
	}

	/**
	 * Find the member of \p base specified by \p path and returns its offset.
	 *
	 * \returns The type and offset of the member.
	 * \throws std::invalid_argument if the path is invalid.
	 */
	template <Path T>
	std::tuple<AnyTypeRef, std::size_t> getOffset(const Compound &base, T &&path) const;

};

template <Path T>
std::tuple<AnyTypeRef, std::size_t> MemoryLayout::getOffset(const Compound &base, T &&path) const
{
	std::tuple<AnyTypeRef, std::size_t> res = {base, 0};
	auto apply_item = overloaded{
		[&](const path::identifier &id) {
			auto &[type, offset] = res;
			if (auto compound = type.get_if<Compound>()) {
				auto path = compound->searchMember(id.identifier);
				if (path.empty())
					throw std::invalid_argument("member not found");
				for (const auto &[parent, i]: path) {
					const auto &compound_info = compound_layout.at(parent);
					offset += compound_info.member_offsets[i];
					type = parent->members[i].type;
				}
			}
			else
				throw std::invalid_argument("identifier needs a compound");

		},
		[&](const path::container_of &c) {
			auto &[type, offset] = res;
			if (auto compound = type.get_if<Compound>()) {
				auto path = compound->searchMember(c.member);
				if (path.empty())
					throw std::invalid_argument("member not found");
				const auto &compound_info = compound_layout.at(compound);
				auto i = path.front().second;
				offset += compound_info.member_offsets[i];
				type = compound->members[i].type;
			}
			else
				throw std::invalid_argument("container_of needs a compound");
		},
		[&](const path::index &idx) {
			auto &[type, offset] = res;
			if (auto container = type.get_if<Container>()) {
				if (container->container_type != Container::Type::StaticArray)
					throw std::invalid_argument("index needs a static array");
				auto i = visit(overloaded{
					[](std::size_t i) { return i; },
					[container](std::string_view name) -> std::size_t {
						if (!container->index_enum)
							throw std::invalid_argument("named index on array without index enum");
						const auto &e = **container->index_enum;
						auto it = e.values.find(name);
						if (it == e.values.end())
							throw std::out_of_range("enum value not found");
						if (it->second.value < 0 || unsigned(it->second.value) >= container->extent)
							throw std::invalid_argument("invalid index value from enum");
						return it->second.value;
					}}, idx);
				auto item_info = getTypeInfo(container->item_type);
				offset += i * item_info.size;
				type = container->item_type;
			}
			else
				throw std::invalid_argument("index needs a container");
		}
	};
	for (const auto &item: path)
		visit(apply_item, item);
	return res;
}

} // namespace dfs

#endif
