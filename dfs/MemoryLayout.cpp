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

#include "MemoryLayout.h"

#include "Structures.h"
#include "Compound.h"
#include "Container.h"

#include <set>
#include <cassert>

using namespace dfs;

static std::size_t computeAlign(std::size_t offset, std::size_t align)
{
	return ((offset + align-1)/align)*align;
}

struct compute_info_t {
	std::unordered_map<const AbstractType *, TypeInfo> &type_info;
	std::unordered_map<const Compound *, CompoundLayout> &compound_layout;
	const ABI &abi;
	std::set<const Compound *> in_progress;
	std::set<std::variant<const PrimitiveType *, const Enum *, const Bitfield *, const Compound *, const PointerType *, const StaticArray *, const StdContainer *, const DFContainer *>> unvisited;

	void do_later(const AnyType &type) {
		type.visit([this](const auto &type){
			if constexpr (requires { unvisited.insert(&type); })
				unvisited.insert(&type);
		});
	}

	template <typename... Args>
	const TypeInfo &add_info(const AbstractType *type, Args &&...args) {
		auto [it, inserted] = type_info.emplace(std::piecewise_construct,
				std::forward_as_tuple(type),
				std::forward_as_tuple(std::forward<Args>(args)...));
		assert(inserted);
		return it->second;
	}

	struct return_type {
		const TypeInfo info;
		const CompoundLayout *layout = nullptr;
	};

	return_type get_info(const AnyType &type) {
		return type.visit([this](const auto &type){return get_info(type);});
	}
	template <typename T>
	return_type get_info(const T &type) {
		auto it = type_info.find(&type);
		if (it != type_info.end()) {
			return_type ret = {it->second};
			if constexpr (std::same_as<T, Compound>)
				ret.layout = &compound_layout.at(&type);
			return ret;
		}
		else
			return (*this)(type);
	}


	return_type operator()(const Padding &padding) {
		return {add_info(&padding, padding.size, padding.align)};
	};
	template <std::derived_from<PrimitiveType> T>
	return_type operator()(const T &type) {
		unvisited.erase(&type);
		return {add_info(&type, abi.primitive_type(type.type))};
	}
	return_type operator()(const Compound &compound) {
		auto [in_progress_it, inserted] = in_progress.emplace(&compound);
		if (!inserted)
			throw std::runtime_error("Cyclic dependency");

		auto &layout = compound_layout[&compound];

		std::size_t offset = 0;
		std::size_t align = 1;
		std::size_t union_size = 0;

		if (compound.parent) {
			auto parent = get_info(**compound.parent);
			if (abi.compiler == ABI::Compiler::GNU)
				offset = parent.layout->unaligned_size; // assume non-POD type
			else
				offset = parent.info.size;
			align = parent.info.align;
		}
		else if (compound.vtable) {
			const auto &pointer = abi.pointer;
			offset = pointer.size;
			align = pointer.align;
		}

		for (auto &[name, member_type]: compound.members) {
			auto member_info = get_info(member_type).info;
			auto member_offset = computeAlign(offset, member_info.align);
			layout.member_offsets.push_back(member_offset);
			if (!compound.is_union)
				offset = member_offset + member_info.size;
			else
				union_size = std::max(union_size, member_info.size);
			align = std::max(align, member_info.align);
		}
		layout.unaligned_size = (compound.is_union ? union_size : offset);
		unvisited.erase(&compound);
		in_progress.erase(in_progress_it);
		return { add_info(&compound, computeAlign(layout.unaligned_size, align), align), &layout };
	}
	return_type operator()(const PointerType &pointer) {
		for (const auto &type: pointer.type_params) {
			if (type.visit([this](const auto &type){ return !type_info.contains(&type); }))
				do_later(type);
		}
		unvisited.erase(&pointer);
		return {add_info(&pointer, abi.pointer)};
	}
	return_type operator()(const StaticArray &array) {
		auto item_info = get_info(array.itemType()).info;
		unvisited.erase(&array);
		return {add_info(&array, array.extent * item_info.size, item_info.align)};
	}
	return_type operator()(const StdContainer &container) {
		if (StdContainer::requiresCompleteTypes(container.container_type)) {
			std::vector<TypeInfo> item_info;
			item_info.reserve(container.type_params.size());
			for (const auto &type: container.type_params)
				item_info.push_back(get_info(type).info);
			unvisited.erase(&container);
			return {add_info(&container, abi.container_info(container.container_type, item_info))};
		}
		else {
			for (const auto &type: container.type_params) {
				if (type.visit([this](const auto &type){ return !type_info.contains(&type); }))
					do_later(type);
			}
			unvisited.erase(&container);
			return {add_info(&container, abi.container_type(container.container_type))};
		}
	}
	return_type operator()(const DFContainer &container) {
		for (const auto &type: container.type_params) {
			if (type.visit([this](const auto &type){ return !type_info.contains(&type); }))
				do_later(type);
		}
		auto compound_info = get_info(*container.compound).info;
		unvisited.erase(&container);
		return {add_info(&container, compound_info)};
	}
};


MemoryLayout::MemoryLayout(const Structures &structures, const ABI &abi)
{
	compute_info_t compute_info{type_info, compound_layout, abi};
	// Add named types
	compute_info.unvisited.insert(&structures.genericPointer());
	for (const auto &[name, type]: structures.allPrimitiveTypes())
		compute_info.unvisited.insert(&type);
	for (const auto &[name, type]: structures.allEnumTypes())
		compute_info.unvisited.insert(&type);
	for (const auto &[name, type]: structures.allBitfieldTypes())
		compute_info.unvisited.insert(&type);
	for (const auto &[name, type]: structures.allCompoundTypes())
		compute_info.unvisited.insert(&type);
	for (const auto &[name, type]: structures.allLinkedListTypes())
		compute_info.unvisited.insert(&type);
	// Add global object types if not already inserted (named)
	for (const auto &[name, type]: structures.allGlobalObjects())
		compute_info.do_later(type);
	// Compute type info
	while (!compute_info.unvisited.empty()) {
		auto ptr = *compute_info.unvisited.begin();
		visit([&](const auto *ptr){compute_info(*ptr);}, ptr);
	}
}
