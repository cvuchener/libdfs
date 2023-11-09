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

#ifndef DFS_COMPOUND_READER_H
#define DFS_COMPOUND_READER_H

#include <dfs/Reader.h>

#include <cppcoro/when_all.hpp>

#include <format>

namespace dfs {

/**
 * Write the vtable address in \p FieldPtr.
 *
 * \sa StructureReader StructureReaderSeq
 *
 * \ingroup readers
 */
template <auto FieldPtr>
struct VTable
{
	[[nodiscard]] bool init(ReaderFactory &factory, const Compound &compound, const CompoundLayout &layout) {
		if (!compound.vtable) {
			factory.log(std::format("compound {} does not have a vtable",
					compound.debug_name));
			return false;
		}
	}
	template <typename T>
	[[nodiscard]] cppcoro::task<bool> read(ReadSession &session, MemoryView data, T &v) const {
		std::invoke(FieldPtr, v) = session.abi().get_pointer(data);
		co_return true;
	}
};

template <auto FieldPtr, static_string FieldPath, auto... Discriminators>
struct Field;

/**
 * Write the content of member \p FieldPath in \p FieldPtr.
 *
 * The invoke results of \p Discriminators are passed to \c ItemReader::operator().
 *
 * \sa StructureReader StructureReaderSeq UnionReader
 *
 * \ingroup readers
 */
template <typename T, typename Structure, T Structure::*FieldPtr, static_string FieldPath, auto... Discriminators>
struct Field<FieldPtr, FieldPath, Discriminators...>
{
	static_assert(ReadableType<T, std::invoke_result_t<decltype(Discriminators), Structure &>...>);
	std::size_t offset;
	const Compound *parent;
	std::optional<ItemReader<T>> reader;
	static constexpr auto ptr = FieldPtr;
	static constexpr auto path = parse_path<FieldPath>();
	static_assert(!path.empty());

	[[nodiscard]] bool init(ReaderFactory &factory, const Compound &compound, const CompoundLayout &layout) {
		parent = &compound;
		try {
			auto [type, offset] = factory.layout.getOffset(compound, path);
			this->offset = offset;
			try {
				reader.emplace(factory, type);
				return true;
			}
			catch (std::exception &e) {
				factory.log(std::format("{} in {} (local: {}): {}",
						FieldPath.str(), parent->debug_name,
						typeid(Structure).name(), e.what()));
				return false;
			}
		}
		catch (std::exception &e) {
			factory.log(std::format("member \"{}\" not found in {} (local: {}): {}",
					FieldPath.str(), parent->debug_name,
					typeid(Structure).name(), e.what()));
			return false;
		}
	};

	[[nodiscard]] cppcoro::task<bool> read(ReadSession &session, MemoryView data, Structure &structure) const {
		try {
			if (reader)
				co_await (*reader)(session,
						data.subview(offset),
						std::invoke(ptr, structure),
						std::invoke(Discriminators, structure)...);
			co_return true;
		}
		catch (std::exception &e) {
			session.log(std::format("{} in {}: {}",
					FieldPath.str(), parent->debug_name, e.what()));
			co_return false;
		}
	}
};

/**
 * Reads data as the base T.
 *
 * \sa StructureReader StructureReaderSeq
 *
 * \ingroup readers
 */
template <ReadableStructure T> requires CompoundReaderWithArgs<compound_reader_type_t<T>>
struct Base
{
	compound_reader_type_t<T> *reader;

	[[nodiscard]] bool init(ReaderFactory &factory, const Compound &compound, const CompoundLayout &) {
		try {
			reader = factory.getCompoundReader<T>();
			const Compound *c = &compound;
			while (auto parent = c->parent) {
				c = parent->get();
				if (c == reader->type)
					return true;
			}
			factory.log(std::format("{} ({}) is not a base of {}",
				reader->type->debug_name,
				typeid(T).name(),
				compound.debug_name));
		}
		catch (std::exception &e) {
			factory.log(std::format("init error in base {}: {}",
					typeid(T).name(), e.what()));
		}
		reader = nullptr;
		return false;
	}

	[[nodiscard]] cppcoro::task<bool> read(ReadSession &session, MemoryView data, T &base) const {
		try {
			if (reader)
				co_await reader->read(session, data, base);
			co_return true;
		}
		catch (std::exception &e) {
			session.log(std::format("read error in base {} (local: {}): {}",
					reader->type->debug_name, typeid(base).name(), e.what()));
			co_return false;
		}
	}
};

template <typename F, typename T>
concept FieldReader = requires (F reader,
		ReaderFactory factory, const Compound compound, const CompoundLayout layout,
		ReadSession session, const MemoryView data, T &object)
{
	{ reader.init(factory, compound, layout) } -> std::convertible_to<bool>;
	{ reader.read(session, data, object) } -> std::same_as<cppcoro::task<bool>>;
};

/**
 * Common base for compound reader types for compound \p T using the DF type
 * \p TypePath.
 *
 * \p TypePath must be a string that can be parsed as a \ref path to a Compound
 * type.
 *
 * \p Fields may be VTable, Field, or Base or other types satisfying
 * FieldReader.
 *
 * \sa CompoundReaderConcept StructureReader StructureReaderSeq UnionReader
 *
 * \ingroup readers
 */
template <typename T, static_string TypePath, typename... Fields>
struct CompoundReaderBase
{
	static_assert((FieldReader<Fields, T> && ...));
	const Compound *type;
	TypeInfo info;
	std::tuple<Fields...> fields;
	static constexpr auto type_path = parse_path<TypePath>();

	CompoundReaderBase(const Structures &structures):
		type(structures.findCompound(type_path))
	{
		if (!type)
			throw std::runtime_error(std::format("type \"{}\" not found for {}",
						TypePath.str(), typeid(T).name()));
	}

	void setLayout(ReaderFactory &factory)
	{
		info = factory.layout.type_info.at(type);
		const auto &compound_layout = factory.layout.compound_layout.at(type);
		bool success = true;
		((get<Fields>(fields).init(factory, *type, compound_layout) || (success = false)), ...);
		if (!success)
			throw std::runtime_error(std::format("nested errors in {}",
					typeid(T).name()));
	}
};

/**
 * A CompoundReaderConcept implementation for structures.
 *
 * All \p Fields are read in sequence.
 *
 * \ingroup readers
 */
template <typename T, static_string TypeName, typename... Fields>
struct StructureReaderSeq: CompoundReaderBase<T, TypeName, Fields...>
{
	using output_type = T;

	StructureReaderSeq(const Structures &structures):
		CompoundReaderBase<T, TypeName, Fields...>(structures)
	{
		if (this->type->is_union)
			throw std::runtime_error(std::format("{} is a union (in {})",
					this->type->debug_name, typeid(T).name()));
	}

	cppcoro::task<> read(ReadSession &session, MemoryView data, T &out) const
	{
		bool success = true;
		((co_await get<Fields>(this->fields).read(session, data, out) || (success = false)), ...);
		if (!success)
			throw std::system_error(ItemReaderError::InvalidField);
	}
};

/**
 * A CompoundReaderConcept implementation for structures.
 *
 * All \p Fields are read unsequenced unlike StructureReaderSeq. This can be an
 * issue if reading one field depends on another already being initialized.
 *
 * \ingroup readers
 */
template <typename T, static_string TypeName, typename... Fields>
struct StructureReader: CompoundReaderBase<T, TypeName, Fields...>
{
	using output_type = T;

	StructureReader(const Structures &structures):
		CompoundReaderBase<T, TypeName, Fields...>(structures)
	{
		if (this->type->is_union)
			throw std::runtime_error(std::format("{} is a union (in {})",
					this->type->debug_name, typeid(T).name()));
	}

	cppcoro::task<> read(ReadSession &session, MemoryView data, T &out) const
	{
		std::vector<cppcoro::task<bool>> tasks;
		(tasks.push_back(get<Fields>(this->fields).read(session, data, out)), ...);
		auto res = co_await cppcoro::when_all(std::move(tasks));
		if (!std::ranges::all_of(res, std::identity{}))
			throw std::system_error(ItemReaderError::InvalidField);
	}
};

/**
 * A CompoundReaderConcept implementation for unions.
 *
 * Only one \p Fields is read depending on an extra argument that is the index
 * of the field being read. `std::size_t(-1)` can be used for reading none without
 * triggering an error.
 *
 * \ingroup readers
 */
template <typename T, static_string TypeName, typename... Fields> requires std::is_union_v<T>
struct UnionReader: CompoundReaderBase<T, TypeName, Fields...>
{
	using output_type = T;

	UnionReader(const Structures &structures):
		CompoundReaderBase<T, TypeName, Fields...>(structures)
	{
		if (!this->type->is_union)
			throw std::runtime_error(std::format("{} is a not union (in {})",
					this->type->debug_name, typeid(T).name()));
		if (this->type->members.size() != sizeof...(Fields))
			throw std::runtime_error(std::format("{} has invalid union size {} ({} has {})",
					this->type->debug_name, this->type->members.size(),
					typeid(T).name(), sizeof...(Fields)));
	}

	cppcoro::task<> read(ReadSession &session, MemoryView data, T &out, std::size_t discriminator) const
	{
		if (discriminator == std::size_t(-1))
			co_return;
		if (auto res = selectAlternative(
				[&]<std::size_t I>(index_constant<I>){ return I == discriminator; },
				[&]<std::size_t I>(index_constant<I>) -> cppcoro::task<bool> {
					return get<I>(this->fields).read(session, data, out);
				},
				std::index_sequence_for<Fields...>{})) {
			if (!co_await *res)
				throw std::system_error(ItemReaderError::InvalidField);
		}
		else
			throw std::system_error(ItemReaderError::InvalidDiscriminator);
	}
};

namespace details {

template <typename... Fields>
struct find_base_field;

template <typename T, typename... Others>
struct find_base_field<Base<T>, Others...>
{
	using type = T;
};

template <typename First, typename... Others> requires requires { typename find_base_field<Others...>::type; }
struct find_base_field<First, Others...>
{
	using type = typename find_base_field<Others...>::type;
};

} // namespace details

template <typename T, static_string TypeName, typename... Fields> requires requires { typename details::find_base_field<Fields...>::type; }
struct compound_reader_parent<StructureReader<T, TypeName, Fields...>>
{
	using type = typename details::find_base_field<Fields...>::type;
};

template <typename T, static_string TypeName, typename... Fields> requires requires { typename details::find_base_field<Fields...>::type; }
struct compound_reader_parent<StructureReaderSeq<T, TypeName, Fields...>>
{
	using type = typename details::find_base_field<Fields...>::type;
};

} // namespace dfs

#endif
