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

#ifndef DFS_POLYMORPHIC_READER_H
#define DFS_POLYMORPHIC_READER_H

#include <dfs/Reader.h>

#include <format>

namespace dfs {

struct fallback_nullptr {};	///< fallback to null pointer
struct fallback_base {};	///< fallback to base type
struct no_fallback {};	///< no fallback, throws an error

/**
 * A PolymorphicReaderConcept implementation.
 *
 * \p Base must be the base of the polymorphic type family, \p Ts are all other
 * derived types that may be read.
 *
 * If a unknown type is read, \c PolymorphicReader will fall back to the base
 * type if it is not abstract, or a null pointer if it is, and log a warning.
 * The \p Base type may define a nested alias type `fallback` to \ref
 * fallback_nullptr, \ref fallback_base, or \ref no_fallback to suppress the
 * warning.
 *
 * \ingroup readers
 */
template <typename Base, std::derived_from<Base>... Ts> requires (ReadableStructure<Base> && ... && ReadableStructure<Ts>)
struct PolymorphicReader
{
	using output_type = Base;

	std::tuple<compound_reader_type_t<Base> *, compound_reader_type_t<Ts> *...> readers;
	std::array<uintptr_t, 1+sizeof...(Ts)> vtables;

	void setLayout(ReaderFactory &factory)
	{
		try {
			readers = std::make_tuple(factory.getCompoundReader<Base>(),
					factory.getCompoundReader<Ts>()...);
		}
		catch (std::exception &) {
			[&]<std::size_t... Index>(std::index_sequence<Index...>) {
				((get<Index>(readers) = nullptr), ...);
				((vtables[Index] = -1), ...);
			}(std::index_sequence_for<Base, Ts...>{});
			throw;
		}
		auto get_vtable = [&](auto compound_reader) {
			static_assert(compound_reader->type_path.size() == 1);
			static_assert(holds_alternative<path::identifier>(compound_reader->type_path.front()));
			std::string_view symbol = get<path::identifier>(compound_reader->type_path.front()).identifier;
			if (compound_reader->type->symbol)
				symbol = *compound_reader->type->symbol;
			auto it = factory.version.vtables_addresses.find(symbol);
			if (it == factory.version.vtables_addresses.end()) {
				using Output = std::decay_t<decltype(*compound_reader)>::output_type;
				if constexpr (!std::is_abstract_v<Output>)
					factory.log(std::format("missing vtable for {} (local: {})", symbol, typeid(Output).name()));
				return uintptr_t(-1);
			}
			return it->second;
		};
		[&]<std::size_t... Index>(std::index_sequence<Index...>) {
			((vtables[Index] = get_vtable(get<Index>(readers))), ...);
		}(std::index_sequence_for<Base, Ts...>{});
	}

	cppcoro::task<std::unique_ptr<Base>> read(ReadSession &session, uintptr_t addr) const
	{
		if (addr == 0)
			co_return nullptr;
		uintptr_t vtable = 0;
		if (auto err = co_await session.process().read({addr, {reinterpret_cast<uint8_t *>(&vtable), session.abi().pointer().size}}))
			throw std::system_error(err);
		vtable -= session.process().base_offset();
		std::unique_ptr<Base> base_ptr;
		auto read_type = [&]<std::size_t I>(index_constant<I>) -> cppcoro::task<> {
			using T = std::tuple_element_t<I, std::tuple<Base, Ts...>>;
			if constexpr (!std::is_abstract_v<T>) {
				auto ptr = std::make_unique<T>();
				auto size = get<I>(readers)->info.size;
				MemoryBuffer data(addr, size);
				if (auto err = co_await session.process().read(data))
					throw std::system_error(err);
				co_await get<I>(readers)->read(session, data, *ptr);
				base_ptr = std::move(ptr);
			}
			else
				throw std::runtime_error("trying to instantiate abstract type");
		};
		if (auto res = selectAlternative(
				[&]<std::size_t I>(index_constant<I>) { return vtable == vtables[I]; },
				read_type,
				std::index_sequence_for<Base, Ts...>{})) {
			co_await *res;
		}
		else {
			if constexpr (requires { typename Base::fallback; }) {
				if constexpr (std::same_as<typename Base::fallback, fallback_nullptr>) {
					// leave base_ptr empty
				}
				else if constexpr (std::same_as<typename Base::fallback, fallback_base>) {
					static_assert(!std::is_abstract_v<Base>);
					co_await read_type(index_constant<0>{});
				}
				else if constexpr (std::same_as<typename Base::fallback, no_fallback>) {
					throw std::runtime_error("unknown vtable address");
				}
				else static_assert(requires (Base) { requires false; }, "unsupported fallback type");
			}
			else {
				session.log(std::format("unknown vtable address for {}: {:x}", typeid(Base).name(), vtable));
				if constexpr (!std::is_abstract_v<Base>) {
					session.log(std::format("falling back to base type"));
					co_await read_type(index_constant<0>{});
				}
				else {
					session.log(std::format("falling back to null pointer"));
					// leave base_ptr empty
				}
			}
		}
		co_return base_ptr;
	}
};

} // namespace dfs

#endif
