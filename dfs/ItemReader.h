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

#ifndef DFS_ITEM_READER_H
#define DFS_ITEM_READER_H

#include <dfs/Reader.h>

#include <cppcoro/when_all.hpp>

#include <format>

namespace dfs {

/**
 * Reader for integral and integral-like types.
 *
 * It accepts any integral primitive type (including enums and bitfields) and
 * Container::Pointer container (the address is used as an integral value).
 *
 * \ingroup readers
 */
template <typename T> requires (
	std::integral<T> ||
	std::is_enum_v<T> ||
	std::constructible_from<T, typename T::underlying_type>)
class ItemReader<T>
{
	std::size_t _size;
	bool _is_signed;

public:
	using output_type = T;

	ItemReader(ReaderFactory &factory, AnyTypeRef type):
		_size(factory.layout.getTypeInfo(type).size),
		_is_signed([&](){
			if (auto primitive_type = type.get_if<PrimitiveType>()) {
				switch (primitive_type->type) {
				case PrimitiveType::Int8:
				case PrimitiveType::Int16:
				case PrimitiveType::Int32:
				case PrimitiveType::Int64:
				case PrimitiveType::Long:
					return true;
				case PrimitiveType::UInt8:
				case PrimitiveType::UInt16:
				case PrimitiveType::UInt32:
				case PrimitiveType::UInt64:
				case PrimitiveType::Char:
				case PrimitiveType::Bool:
				case PrimitiveType::ULong:
				case PrimitiveType::SizeT:
					return false;
				default:
					throw TypeError(*primitive_type, typeid(T), "not an integral type");
				}
			}
			else if (auto container = type.get_if<Container>()) {
				switch (container->container_type) {
				case Container::Pointer:
					if constexpr (!std::is_same_v<T, uintptr_t>)
						throw TypeError(*container, typeid(T), "pointer requires uintptr_t");
					else
						return false;
				default:
					throw TypeError(*container, typeid(T), "incompatible type");
				}
			}
			else
				throw TypeError(type, typeid(T), "incompatible type");
		}())
	{
		if (_size > sizeof(T))
			throw TypeError(type, typeid(T), std::format("storage is too small ({}, must be at least {})", sizeof(T), _size));
	}

	std::size_t size() const {
		return _size;
	}

	cppcoro::task<> operator()(ReadSession &, MemoryView data, T &out) const {
		return (*this)(data, out);
	}

	template <typename U>
	struct cast_type {
		using type = U;
	};

	template <typename U> requires std::constructible_from<U, typename U::underlying_type>
	struct cast_type<U> {
		using type = U::underlying_type;
	};

	cppcoro::task<> operator()(MemoryView data, T &out) const {
		using out_int = cast_type<T>::type;
		if (_is_signed) {
			switch (_size) {
			case 1: out = static_cast<out_int>(ABI::get_integer<int8_t>(data)); break;
			case 2: out = static_cast<out_int>(ABI::get_integer<int16_t>(data)); break;
			case 4: out = static_cast<out_int>(ABI::get_integer<int32_t>(data)); break;
			case 8: out = static_cast<out_int>(ABI::get_integer<int64_t>(data)); break;
			default: throw std::system_error(ABIError::InvalidLength);
			}
		}
		else {
			switch (_size) {
			case 1: out = static_cast<out_int>(ABI::get_integer<uint8_t>(data)); break;
			case 2: out = static_cast<out_int>(ABI::get_integer<uint16_t>(data)); break;
			case 4: out = static_cast<out_int>(ABI::get_integer<uint32_t>(data)); break;
			case 8: out = static_cast<out_int>(ABI::get_integer<uint64_t>(data)); break;
			default: throw std::system_error(ABIError::InvalidLength);
			}
		}
		co_return;
	}
};

/**
 * Reader for `std::string`.
 *
 * It accepts PrimitiveType::StdString types.
 *
 * \todo support more string types
 *
 * \ingroup readers
 */
template <>
class ItemReader<std::string>
{
	const PrimitiveType &_primitive_type;
	std::size_t _size;

public:
	using output_type = std::string;

	ItemReader(ReaderFactory &factory, AnyTypeRef type):
		_primitive_type([&]() -> const PrimitiveType & {
			if (auto primitive_type = type.get_if<PrimitiveType>()) {
				switch (primitive_type->type) {
				case PrimitiveType::PtrString:
				case PrimitiveType::StdString:
					return *primitive_type;
				default:
					throw TypeError(*primitive_type, typeid(std::string), "not a string type");
				}
			}
			else
				throw TypeError(type, typeid(std::string), "not a primitive type");
		}()),
		_size(factory.abi.primitive_type(_primitive_type.type).size)
	{
	}

	std::size_t size() const {
		return _size;
	}

	cppcoro::task<> operator()(ReadSession &session, MemoryView data, std::string &out) const
	{
		switch (_primitive_type.type) {
		case PrimitiveType::PtrString:
			throw std::system_error(ItemReaderError::NotImplemented);
		case PrimitiveType::StdString: {
			auto ret = co_await session.abi().read_string(session.process(), data);
			if (ret.err)
				throw std::system_error(ret.err);
			out = std::move(ret.str);
			break;
		}
		default:
			throw std::system_error(ItemReaderError::NotImplemented);
		}
	}
};

/**
 * Reader for bit vectors.
 *
 * It accepts and PrimitiveType::DFFlagArray types.
 *
 * \todo support PrimitiveType::StdBitVector
 *
 * \ingroup readers
 */
template <std::derived_from<std::vector<bool>> T>
class ItemReader<T>
{
	const PrimitiveType &_primitive_type;
	std::size_t _size;

public:
	using output_type = T;

	ItemReader(ReaderFactory &factory, AnyTypeRef type):
		_primitive_type([&]() -> const PrimitiveType & {
			if (auto primitive_type = type.get_if<PrimitiveType>()) {
				switch (primitive_type->type) {
				case PrimitiveType::StdBitVector:
				case PrimitiveType::DFFlagArray:
					return *primitive_type;
				default:
					throw TypeError(*primitive_type, typeid(std::string), "not a bit vector type");
				}
			}
			else
				throw TypeError(type, typeid(std::string), "not a primitive type");
		}()),
		_size(factory.abi.primitive_type(_primitive_type.type).size)
	{
	}

	std::size_t size() const {
		return _size;
	}

	cppcoro::task<> operator()(ReadSession &session, MemoryView data, std::vector<bool> &out) const
	{
		switch (_primitive_type.type) {
		case PrimitiveType::StdBitVector:
			throw std::system_error(ItemReaderError::NotImplemented);
		case PrimitiveType::DFFlagArray: {
			uintptr_t addr = session.abi().get_pointer(data);
			uint32_t len = session.abi().get_integer<uint32_t>(data.subview(session.abi().pointer().size));
			MemoryBuffer data(addr, len);
			if (auto err = co_await session.process().read(data))
				throw std::system_error(err);
			out.resize(len*8);
			for (unsigned int i = 0; i < len*8; ++i)
				out[i] = data[i/8] & (1<<(i%8));
			break;
		}
		default:
			throw std::system_error(ItemReaderError::NotImplemented);
		}
	}

};

/**
 * Reader for `std::vector` (except `std::vector<bool>`).
 *
 * It accepts Container::StdVector container.
 *
 * \ingroup readers
 */
template <typename T> requires (!std::is_same_v<T, bool>)
class ItemReader<std::vector<T>>
{
	const Container &_container;
	std::size_t _size;
	TypeInfo _item_info;
	ItemReader<T> _item_reader;

public:
	using output_type = std::vector<T>;

	ItemReader(ReaderFactory &factory, AnyTypeRef type):
		_container([&]() -> const Container & {
			if (auto container = type.get_if<Container>()) {
				if (container->container_type != Container::StdVector)
					throw TypeError(*container, typeid(std::vector<T>), "incompatible container");
				return *container;
			}
			else
				throw TypeError(type, typeid(std::vector<T>), "not a container");
		}()),
		_size(factory.abi.container_type(_container.container_type).size),
		_item_info(factory.layout.getTypeInfo(_container.item_type)),
		_item_reader(factory, _container.item_type)
	{
	}

	std::size_t size() const {
		return _size;
	}

	template <std::ranges::random_access_range... Args> requires ReadableType<T, std::ranges::range_value_t<Args>...>
	cppcoro::task<> operator()(ReadSession &session, MemoryView data, std::vector<T> &out, Args &&...args) const
	{
		if (_container.container_type == Container::StdVector) {
			auto vec_info = co_await session.abi().read_vector(session.process(), data, _item_info);
			if (vec_info.err)
				throw std::system_error(vec_info.err);
			if (vec_info.size == 0)
				co_return;
			std::size_t bytes = vec_info.size * _item_info.size;
			MemoryBuffer item_data(vec_info.data, bytes);
			if (auto err = co_await session.process().read(item_data))
				throw std::system_error(err);
			out.resize(vec_info.size);
			using std::size;
			if (!((size(args) == vec_info.size) && ...))
				throw std::runtime_error("extra args size does not match vector size");
			std::vector<cppcoro::task<>> tasks;
			tasks.reserve(vec_info.size);
			for (std::size_t i = 0; i < vec_info.size; ++i)
				tasks.push_back(_item_reader(session,
					     item_data.view(i*_item_info.size, _item_info.size),
					     out[i],
					     *std::next(std::begin(std::forward<decltype(args)>(args)), i)...));
			co_await cppcoro::when_all(std::move(tasks));
		}
		else
			throw std::system_error(ItemReaderError::NotImplemented);
	}
};

/**
 * Reader for `std::array`.
 *
 * It accepts Container::StaticArray with matching extent.
 *
 * \ingroup readers
 */
template <typename T, std::size_t N>
class ItemReader<std::array<T, N>>
{
	const Container &_container;
	TypeInfo _item_info;
	ItemReader<T> _item_reader;

public:
	using output_type = std::array<T, N>;

	ItemReader(ReaderFactory &factory, AnyTypeRef type):
		_container([&]() -> const Container & {
			if (auto container = type.get_if<Container>()) {
				if (container->container_type != Container::StaticArray)
					throw TypeError(*container, typeid(output_type), "invalid container type");
				if (container->extent != N)
					throw TypeError(*container, typeid(output_type), "invalid array size");
				return *container;
			}
			else
				throw TypeError(type, typeid(output_type), "not a container");
		}()),
		_item_info(factory.layout.getTypeInfo(_container.item_type)),
		_item_reader(factory, _container.item_type)
	{
	}

	std::size_t size() const {
		return _item_info.size * N;
	}

	cppcoro::task<> operator()(ReadSession &session, MemoryView data, std::array<T, N> &out) const
	{
		std::vector<cppcoro::task<>> tasks;
		tasks.reserve(N);
		for (std::size_t i = 0; i < N; ++i)
			tasks.push_back(_item_reader(session, data.subview(i*_item_info.size, _item_info.size), out[i]));
		co_await cppcoro::when_all(std::move(tasks));
	}
};

/**
 * Reader for structures.
 *
 * The structure must satisfy ReadableStructure.
 *
 * \ingroup readers
 */
template <ReadableStructure T>
class ItemReader<T>
{
	compound_reader_type_t<T> *_compound_reader;

public:
	using output_type = T;

	ItemReader(ReaderFactory &factory, AnyTypeRef type):
		_compound_reader([&](){
			auto compound_reader = factory.getCompoundReader<T>();
			if (compound_reader->type != type.get_if<Compound>())
				throw TypeError(type, typeid(T), "invalid type");
			return compound_reader;
		}())
	{
	}

	std::size_t size() const {
		return _compound_reader->info.size;
	}

	template <typename... Args> requires CompoundReaderWithArgs<compound_reader_type_t<T>, Args...>
	cppcoro::task<> operator()(ReadSession &session, MemoryView data, T &out, Args &&...args) const
	{
		co_await _compound_reader->read(session, data, out, std::forward<Args>(args)...);
	}
};

/**
 * Reader for `std::variant`.
 *
 * It accepts union compounds where the members match the variant alternative
 * exactly in the same order.
 *
 * The first alternative may be `std::monostate` for invalid alternative indices.
 *
 * \ingroup readers
 */
template <typename T, typename... Ts>
class ItemReader<std::variant<T, Ts...>>
{
	std::conditional_t<std::is_same_v<T, std::monostate>,
		std::tuple<std::optional<ItemReader<Ts>>...>,
		std::tuple<std::optional<ItemReader<T>>, std::optional<ItemReader<Ts>>...>
	> _readers;
	const Compound &_compound;
	std::size_t _size;

	static constexpr std::size_t alternative_count = std::tuple_size_v<decltype(_readers)>;
	static constexpr std::size_t index_offset = std::variant_size_v<std::variant<T, Ts...>> - alternative_count;

	template <std::size_t... Index>
	void init_readers(ReaderFactory &factory, const Compound &compound, std::index_sequence<Index...>)
	{
		([&](auto &reader, std::size_t index) {
			try {
				reader.emplace(factory, compound.members[index].type);
			}
			catch (std::exception &e) {
				factory.log(std::format("In {} index {} (local: {}): {}\n",
						compound.debug_name, index,
						typeid(output_type).name(), e.what()));
			}
		}(get<Index>(_readers), Index), ...);
	}

public:
	using output_type = std::variant<T, Ts...>;

	ItemReader(ReaderFactory &factory, AnyTypeRef type):
		_compound([&]() -> const Compound & {
			if (auto compound = type.get_if<Compound>()) {
				if (!compound->is_union)
					throw TypeError(*compound, typeid(output_type), "not a union");
				if (compound->members.size() != alternative_count)
					throw TypeError(*compound, typeid(output_type), "invalid union size");
				return *compound;
			}
			else
				throw TypeError(type, typeid(output_type), "not a compound");
		}()),
		_size(factory.layout.type_info.at(&_compound).size)
	{
		init_readers(factory, _compound, std::make_index_sequence<alternative_count>{});
	}

	std::size_t size() const {
		return _size;
	}

	cppcoro::task<> operator()(ReadSession &session, MemoryView data, output_type &out, std::size_t discriminator) const
	{
		if (auto res = selectAlternative(
				[&]<std::size_t I>(index_constant<I>){ return I == discriminator; },
				[&]<std::size_t I>(index_constant<I>) -> cppcoro::task<> {
					if (auto reader = get<I>(_readers))
						return (*reader)(session, data, out.template emplace<I+index_offset>());
					return {};
				},
				std::make_index_sequence<alternative_count>{}))
			co_await *res;
		else
			out = {};

	}
};

template <typename T>
class PointerReader
{
public:
	PointerReader(ReaderFactory &factory, AnyTypeRef type):
		container([&]() -> const Container & {
			if (auto container = type.get_if<Container>()) {
				if (container->container_type != Container::Pointer)
					throw TypeError(*container, typeid(T *), "not a pointer");
				return *container;
			}
			else
				throw TypeError(type, typeid(T *), "not a container");
		}())
	{
	}

	virtual ~PointerReader() = default;

	virtual cppcoro::task<std::unique_ptr<T>> make_unique(ReadSession &session, uintptr_t addr) const = 0;
	virtual cppcoro::task<std::shared_ptr<T>> make_shared(ReadSession &session, uintptr_t addr) const = 0;

protected:
	const Container &container;

	template <typename Base>
	cppcoro::task<std::shared_ptr<T>> make_shared_impl(ReadSession &session, uintptr_t addr) const
	{
		auto object_factory = [this](ReadSession &session, uintptr_t addr) {
			return [](const PointerReader<T> &self, ReadSession &session, uintptr_t addr)
				-> cppcoro::shared_task<std::shared_ptr<void>> {
					co_return co_await self.make_unique(session, addr);
			}(*this, session, addr);
		};
		if (addr == 0) {
			co_return nullptr;
		}
		else {
			auto ptr = co_await session.getSharedObject<Base>(addr, object_factory);
			co_return static_pointer_cast<T>(std::move(ptr));
		}

	}
};

template <typename T> requires (!PolymorphicStructure<T>)
class StaticPointerReader: public PointerReader<T>
{
	TypeInfo _item_info;
	ItemReader<T> _item_reader;

public:
	StaticPointerReader(ReaderFactory &factory, AnyTypeRef type):
		PointerReader<T>(factory, type),
		_item_info(factory.layout.getTypeInfo(this->container.item_type)),
		_item_reader(factory, this->container.item_type)
	{
	}

	~StaticPointerReader() override = default;

	cppcoro::task<std::unique_ptr<T>> make_unique(ReadSession &session, uintptr_t addr) const override
	{
		if (addr == 0)
			co_return nullptr;
		MemoryBuffer item_data(addr, _item_info.size);
		if (auto err = co_await session.process().read(item_data))
			throw std::system_error(err);
		auto res = std::make_unique<T>();
		co_await _item_reader(session, item_data, *res);
		co_return res;
	}

	cppcoro::task<std::shared_ptr<T>> make_shared(ReadSession &session, uintptr_t addr) const override
	{
		return PointerReader<T>::template make_shared_impl<T>(session, addr);
	}
};

template <PolymorphicStructure T>
class PolymorphicPointerReader: public PointerReader<T>
{
	using base = typename polymorphic_base<T>::type;
	polymorphic_reader_type_t<base> *_polymorphic_reader;

public:
	PolymorphicPointerReader(ReaderFactory &factory, AnyTypeRef type):
		PointerReader<T>(factory, type),
		_polymorphic_reader(factory.getPolymorphicReader<base>())
	{
		// TODO: check type
	}

	~PolymorphicPointerReader() override = default;

	cppcoro::task<std::unique_ptr<T>> make_unique(ReadSession &session, uintptr_t addr) const override
	{
		if (addr == 0)
			co_return nullptr;
		auto ptr = co_await _polymorphic_reader->read(session, addr);
		if constexpr (std::is_same_v<T, base>) {
			co_return ptr;
		}
		else {
			if (dynamic_cast<T *>(ptr.get()))
				co_return std::unique_ptr<T>(static_cast<T *>(ptr.release()));
			throw std::system_error(ItemReaderError::CastError);
		}
	}

	cppcoro::task<std::shared_ptr<T>> make_shared(ReadSession &session, uintptr_t addr) const override
	{
		return PointerReader<T>::template make_shared_impl<base>(session, addr);
	}
};

template <typename T>
struct pointer_reader_traits;

template <typename T>
struct pointer_reader_traits<std::unique_ptr<T>>
{
	using value_type = T;
	static constexpr auto make_pointer = &PointerReader<T>::make_unique;
};

template <typename T>
struct pointer_reader_traits<std::shared_ptr<T>>
{
	using value_type = T;
	static constexpr auto make_pointer = &PointerReader<T>::make_shared;
};

/**
 * Reader for pointers (`std::unique_ptr` or `std::shared_ptr`).
 *
 * It accepts pointer container types.
 *
 * If pointed type is a PolymorphicStructure, the \ref
 * polymorphic_reader_type_t will be used to read the object. Otherwise
 * ItemReader is used.
 *
 * \ingroup readers
 */
template <typename Ptr> requires requires { typename pointer_reader_traits<Ptr>::value_type; }
class ItemReader<Ptr>
{
	using traits = pointer_reader_traits<Ptr>;
	using T = typename traits::value_type;
	std::unique_ptr<PointerReader<T>> _reader;
	std::size_t _size;

public:
	using output_type = Ptr;

	ItemReader(ReaderFactory &factory, AnyTypeRef type):
		_reader([&]() -> std::unique_ptr<PointerReader<T>> {
			if constexpr (PolymorphicStructure<T>)
				return std::make_unique<PolymorphicPointerReader<T>>(factory, type);
			else
				return std::make_unique<StaticPointerReader<T>>(factory, type);
		}()),
		_size(factory.abi.pointer().size)
	{
	}

	std::size_t size() const {
		return _size;
	}

	cppcoro::task<> operator()(ReadSession &session, MemoryView data, Ptr &out) const
	{
		auto addr = session.abi().get_pointer(data);
		out = co_await ((*_reader).*traits::make_pointer)(session, addr);
	}
};

} // namespace dfs

#endif
