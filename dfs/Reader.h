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

#ifndef DFS_READER_H
#define DFS_READER_H

#include <dfs/Structures.h>
#include <dfs/MemoryLayout.h>
#include <dfs/Pointer.h>

#include <typeindex>
#include <cppcoro/shared_task.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/when_all.hpp>

#include <format>

namespace dfs {

/**
 * \defgroup item_reader_error ItemReaderError
 *
 * \{
 */
/**
 * Errors when using ItemReader.
 *
 * \sa item_reader_category()
 */
enum class ItemReaderError {
	NotImplemented,
	TypeMismatch,
	AbstractType,
	CastError,
	InvalidField,
	InvalidDiscriminator,
};

/**
 * Error category for ItemReaderError.
 */
const std::error_category &item_reader_category() noexcept;

std::error_code make_error_code(ItemReaderError e);

/// \}

} // namespace dfs

///< \ingroup item_reader_error
template <>
struct std::is_error_code_enum<dfs::ItemReaderError>: true_type {};

namespace dfs {

/**
 * Type mismatch errors when creating ItemReader objects.
 */
class TypeError: public std::exception
{
	std::string _message;
public:

	TypeError(const PrimitiveType &df_type, const std::type_info &local_type, std::string_view message):
		_message(std::format("{} (df: {}, local: {})",
				message,
				PrimitiveType::to_string(df_type.type),
				local_type.name()))
	{
	}
	TypeError(const Container &df_type, const std::type_info &local_type, std::string_view message):
		_message(std::format("{} (df: {}, local: {})",
				message,
				Container::to_string(df_type.container_type),
				local_type.name()))
	{
	}
	template <typename T> requires requires (T t){t.debug_name;}
	TypeError(const T &df_type, const std::type_info &local_type, std::string_view message):
		_message(std::format("{} (df: {}, local: {})",
				message,
				df_type.debug_name,
				local_type.name()))
	{
	}
	TypeError(AnyTypeRef df_type, const std::type_info &local_type, std::string_view message):
		_message(std::format("{} (df: {}, local: {})",
				message,
				df_type.visit(overloaded{
					[](const PrimitiveType &type){return PrimitiveType::to_string(type.type);},
					[](const Container &type){return Container::to_string(type.container_type);},
					[](const Padding &){return std::string("padding");},
					[](const auto &type){return type.debug_name;}}),
				local_type.name()))
	{
	}

	~TypeError() override = default;

	const char *what() const noexcept override
	{
		return _message.c_str();
	}
};

/**
 * \defgroup readers Readers
 *
 * More information in \ref readers.
 *
 * \{
 */

class ReaderFactory;
class ReadSession;

/**
 * A type that can read a compound (union, struct or class).
 */
template <typename T>
concept CompoundReaderConcept =
	std::constructible_from<T, const Structures &> &&
	std::same_as<decltype(T::type), const Compound *> &&
	std::same_as<decltype(T::info), TypeInfo> &&
	Path<decltype(T::type_path)> &&
	requires (T t, ReaderFactory factory) {
		t.setLayout(factory);
	};

/**
 * A type that can read a compound using specified extra arguments.
 */
template <typename T, typename... Args>
concept CompoundReaderWithArgs = CompoundReaderConcept<T> && requires (
		const T t,
		ReadSession session,
		const MemoryView data,
		typename T::output_type out,
		Args &&...args)
{
	{ t.read(session, data, out, std::forward<Args>(args)...) } -> std::same_as<cppcoro::task<>>;
};

/**
 * A type that can read a pointer from polymorphic type family.
 */
template <typename T>
concept PolymorphicReaderConcept = requires (
		T t,
		ReaderFactory factory,
		ReadSession session,
		const uintptr_t addr)
{
	typename T::output_type;
	requires std::default_initializable<T>;
	t.setLayout(factory);
	{ std::as_const(t).read(session, addr) } -> std::same_as<cppcoro::task<std::unique_ptr<typename T::output_type>>>;
};

/**
 * Type trait for finding the compound reader type for \p T.
 *
 * If T defines the nested type `reader_type` it is used. This template can
 * specialized for other types.
 *
 * \sa CompoundReaderWithArgs StructureReader UnionReader
 */
template <typename T>
struct compound_reader_type;

template <typename T> requires requires { typename T::reader_type; }
struct compound_reader_type<T> { using type = typename T::reader_type; };

template <typename T>
using compound_reader_type_t = typename compound_reader_type<T>::type;

/**
 * A type that can be read using a compound reader.
 */
template <typename T>
concept ReadableStructure = CompoundReaderConcept<compound_reader_type_t<T>>;

/**
 * Type trait for finding the polymorphic reader type for T.
 *
 * This template must specialized for the base type of the polymorphic type family
 *
 * \sa PolymorphicReaderConcept PolymorphicReader
 */
template <typename T>
struct polymorphic_reader_type;

template <typename T>
using polymorphic_reader_type_t = typename polymorphic_reader_type<T>::type;

/**
 * Type trait for finding the base of the type T (or T itself if it is the base).
 */
template <typename T>
struct polymorphic_base;

template <typename T>
concept PolymorphicStructure = requires { typename polymorphic_base<T>::type; };

/**
 * Type trait for finding the compound_reader associated with the parent of the
 * type read by the compound reader T.
 */
template <CompoundReaderConcept T>
struct compound_reader_parent;

template <typename T> requires (std::has_virtual_destructor_v<T> && PolymorphicReaderConcept<polymorphic_reader_type_t<T>>)
struct polymorphic_base<T>
{
	using type = T;
};

template <typename T> requires (
		std::has_virtual_destructor_v<T> &&
		!requires { requires PolymorphicReaderConcept<polymorphic_reader_type_t<T>>; } &&
		PolymorphicStructure<typename compound_reader_parent<compound_reader_type_t<T>>::type>)
struct polymorphic_base<T>
{
	using type = polymorphic_base<typename compound_reader_parent<compound_reader_type_t<T>>::type>::type;
};

/**
 * Reads an object of type T from DF memory.
 *
 * An item reader must have a constructor whose prototype id
 * `ItemReader(ReaderFactory &factory, AnyTypeRef type)` where `type` is a
 * reference to the DF type the reader will need to read. It may throw if the
 * type is unsupported.
 *
 * It must have a method `std::size_t size() const` returning the size of the
 * DF object. And one or more `cppcoro::task<> operator()(ReadSession &session,
 * MemoryView data, output_type &out, ...)` where `data` is a view on DF memory
 * of the required size and `out` the variable that must be initialized. It can
 * takes extra parameters as needed, for example variant and union require an
 * index for the type alternative to read.
 *
 * \todo concept
 *
 * \sa ReadableType
 */
template <typename T>
class ItemReader;

/**
 * A type that can be read using a ItemReader provided the extra arguments
 * \p Args.
 */
template <typename T, typename... Args>
concept ReadableType = requires (
		const ItemReader<T> reader,
		ReadSession session,
		const MemoryView data,
		typename ItemReader<T>::output_type out,
		Args &&...args)
{
	requires std::constructible_from<ItemReader<T>, ReaderFactory &, AnyTypeRef>;
	{ std::as_const(reader).size() } -> std::convertible_to<std::size_t>;
	{ reader(session, data, out, std::forward<Args>(args)...) } -> std::same_as<cppcoro::task<>>;
};

/**
 * Creates and caches compound and polymorphic readers.
 *
 * The user only needs to create it and pass it to ReadSession objects.
 *
 * Adding new Readers (ItemReader, CompoundReaderConcept, ...) will
 * need to access the public members and cached readers using
 * getCompoundReader() and getPolymorphicReader().
 *
 * Errors happening during reader initialization will be logged with \ref log
 * (default to writing to `std::cerr`), and a exception will be thrown.
 */
class ReaderFactory
{
public:
	std::function<void (std::string_view)> log;
	const Structures &structures;
        const ABI abi;
	const MemoryLayout layout;
	const Structures::VersionInfo &version;

	/**
	 * Constructs a factory for \p structures using version \p version.
	 *
	 * It may throw if it fails to match an ABI for this version, or if it
	 * fails to initialize the memory layout.
	 *
	 * \throws std::runtime_error
	 */
	ReaderFactory(const Structures &structures, const Structures::VersionInfo &version);

	/**
	 * Creates a reader for local type \p T from DF type \p type.
	 */
	template <typename T>
	ItemReader<T> make_item_reader(AnyTypeRef type) {
		return ItemReader<T>(*this, type);
	}

	/**
	 * Creates or get from cache the compound reader for \p T.
	 *
	 * \throws TypeError std::runtime_error
	 */
	template <ReadableStructure T>
	auto getCompoundReader() {
		auto [it, inserted] = _readers.try_emplace(std::type_index(typeid(T)));
		if (inserted) {
			auto ptr = std::make_shared<compound_reader_type_t<T>>(structures);
			it->second = ptr;
			ptr->setLayout(*this); // may reenter
			return ptr.get();
		}
		else
			return static_cast<compound_reader_type_t<T> *>(it->second.get());
	}

	/**
	 * Creates or get from cache the polymorphic reader for \p T.
	 *
	 * \throws TypeError std::runtime_error
	 */
	template <typename T> requires PolymorphicReaderConcept<polymorphic_reader_type_t<T>>
	auto getPolymorphicReader() {
		auto [it, inserted] = _polymorphic_readers.try_emplace(std::type_index(typeid(T)));
		if (inserted) {
			auto ptr = std::make_shared<polymorphic_reader_type_t<T>>();
			it->second = ptr;
			ptr->setLayout(*this); // may reenter
			return ptr.get();
		}
		else
			return static_cast<polymorphic_reader_type_t<T> *>(it->second.get());
	}

private:
	std::unordered_map<std::type_index, std::shared_ptr<void>> _readers;
	std::unordered_map<std::type_index, std::shared_ptr<void>> _polymorphic_readers;
};

/**
 * Manage a reading session.
 *
 * Stops the process when initialized and resumes it when destroyed.
 *
 * Read operations can be created by calling one of the \ref read methods. But
 * they won't actually execute until passed to \ref sync which can execute
 * several reads concurrently (if the process type allows it). Or \ref
 * read_sync can be used for executing a single read operation synchronously.
 *
 * \c ReadSession also manage a `std::shared_ptr` cache. If the same address is
 * read multiple times in the same session for a `std::shared_ptr`, the same
 * pointer is used. Cache life-time for specific types can be extended using
 * \ref addSharedObjectsCache.
 *
 * Errors during reading will be logged using \ref log (will default to using
 * ReaderFactory::log) and \ref sync will return \c false.
 */
class ReadSession
{
public:
	std::function<void (std::string_view)> log;

	/**
	 * Creates a new session, using readers from \p factory and reads
	 * memory from \p process.
	 *
	 * \p process is stopped.
	 */
	ReadSession(ReaderFactory &factory, Process &process);
	/**
	 * Finish the session.
	 *
	 * The process is resumed.
	 */
	~ReadSession();

	ReadSession(const ReadSession &) = delete;
	ReadSession(ReadSession &&) = delete;
	ReadSession &operator=(const ReadSession &) = delete;
	ReadSession &operator=(ReadSession &&) = delete;

	Process &process() { return _process; }
	const ABI &abi() const { return _factory.abi; }

	/**
	 * Find the address and type of the global specified by \p path.
	 */
	template <Path T>
	Pointer getGlobal(T &&path) const {
		return Pointer::fromGlobal(
				_factory.structures,
				_factory.version,
				_factory.layout,
				std::forward<T>(path),
				&_process);
	}

	/**
	 * Reads from \p ptr and initializes \p var.
	 */
	template <ReadableType T>
	cppcoro::task<> read(Pointer ptr, T &var)
	{
		auto reader = _factory.make_item_reader<T>(ptr.type);
		MemoryBuffer data(ptr.address, reader.size());
		if (auto err = co_await _process.read(data))
			throw std::system_error(err);
		co_await reader(*this, data, var);
	}

	/**
	 * Reads from the global path \p path and initializes \p var.
	 */
	template <Path Rng, ReadableType T>
	cppcoro::task<> read(Rng &&path, T &var)
	{
		return read(getGlobal(std::forward<Rng>(path)), var);
	}

	/**
	 * Waits for all the \p reads to finish.
	 *
	 * \p reads should be return task from one of the \ref read functions.
	 */
	template <typename... Reads>
	[[nodiscard]] bool sync(Reads &&...reads) {
		try {
			_process.sync([](Reads &&... reads) -> cppcoro::task<> {
				co_await cppcoro::when_all(std::forward<Reads>(reads)...);
			}(std::forward<Reads>(reads)...));
			return true;
		}
		catch (std::exception &e) {
			log(std::format("failed to read data: {}\n", e.what()));
			return false;
		}
	}

	/**
	 * Same as read(Pointer, T &) but runs synchronously.
	 */
	template <ReadableType T>
	[[nodiscard]] bool read_sync(Pointer ptr, T &var)
	{
		return sync(read(ptr, var));
	}

	/**
	 * Same as read(Rng &&, T &) but runs synchronously.
	 */
	template <Path Rng, ReadableType T>
	[[nodiscard]] bool read_sync(Rng &&path, T &var)
	{
		return sync(read(std::forward<Rng>(path), var));
	}

	/**
	 * Get the `std::shared_ptr<T>` from \p address from cache if it was
	 * already read, or using \p object_factory if not.
	 *
	 * For polymorphic types \p T must be the base type.
	 *
	 * \p object_factory must accept a reference to this session and the
	 * address as parameter and return a
	 * `cppcoro::shared_task<std::shared_ptr<void>>`.
	 */
	template <typename T, std::invocable<ReadSession &, uintptr_t> F>
	cppcoro::shared_task<std::shared_ptr<void>> getSharedObject(uintptr_t address, F &&object_factory)
	{
		static_assert(std::is_same_v<
				std::invoke_result_t<F, ReadSession &, uintptr_t>,
				cppcoro::shared_task<std::shared_ptr<void>>
			>);
		std::type_index type = typeid(T);
		auto external = _external_shared_objects.find(type);
		auto &cache = external != _external_shared_objects.end()
			? *external->second
			: _shared_objects;
		auto [it, inserted] = cache.try_emplace(
				address,
				std::make_pair(type, cppcoro::shared_task<std::shared_ptr<void>>{}));
		if (inserted)
			it->second.second = std::invoke(object_factory, *this, address);
		else if (it->second.first != type)
			throw std::system_error(ItemReaderError::TypeMismatch);
		return it->second.second;
	}

	/**
	 * Type for a external shared objects cache.
	 *
	 * \sa addSharedObjectsCache
	 */
	using shared_objects_cache_t = std::unordered_map<uintptr_t, std::pair<std::type_index, cppcoro::shared_task<std::shared_ptr<void>>>>;

	/**
	 * Add a external cache for type \p T.
	 *
	 * The session will use \p cache for `std::shared_ptr<T>` instead of
	 * its own. This allows for manually managing the life-time of cache
	 * for specific types.
	 *
	 * For polymorphic types \p T must be the base type.
	 */
	template <typename T>
	void addSharedObjectsCache(shared_objects_cache_t &cache)
	{
		auto [it, inserted] = _external_shared_objects.try_emplace(typeid(T), &cache);
		if (!inserted)
			throw std::runtime_error("Duplicate cache");
	}
private:
	ReaderFactory &_factory;
	Process &_process;
	shared_objects_cache_t _shared_objects;
	std::map<std::type_index, shared_objects_cache_t *> _external_shared_objects;
};

///< \}

template <std::size_t Index>
struct index_constant: std::integral_constant<std::size_t, Index> {};

/**
 * Performs an action depending on a condition.
 *
 * Calls `action(index_constant<Index>)` for the first \p Index where
 * `condition(index_constant<Index>)` is true.
 *
 * \returns the result of \p action call, or \c std::nullopt if all \p condition failed
 */
template <typename Condition, typename Action, std::size_t... Index>
auto selectAlternative(Condition &&condition, Action &&action, std::index_sequence<Index...>)
	-> std::optional<std::invoke_result_t<Action, index_constant<0>>>
{
	std::optional<std::invoke_result_t<Action, index_constant<0>>> res = std::nullopt;
	((condition(index_constant<Index>{}) && (res = action(index_constant<Index>{}), true)) || ...);
	return res;
}

} // namespace dfs

#endif
