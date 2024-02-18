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

#ifndef DFS_PROCESS_H
#define DFS_PROCESS_H

#include <map>
#include <memory>
#include <span>
#include <system_error>
#include <vector>

#include <cppcoro/task.hpp>
#include <cppcoro/shared_task.hpp>
#include <cppcoro/async_auto_reset_event.hpp>
#include <cppcoro/single_consumer_event.hpp>

namespace dfs {

/**
 * \defgroup process Process
 *
 * \{
 */

/**
 * A view over raw memory.
 */
struct MemoryView
{
	uintptr_t address;
	std::span<const uint8_t> data;

	/**
	 * \returns a sub-view of \p length from \p offset
	 */
	const MemoryView subview(std::size_t offset, std::size_t length) const {
		assert(offset+length <= data.size());
		return {address+offset, {data.begin()+offset, data.begin()+offset+length}};
	}
	/**
	 * \returns a sub-view from \p offset to the end of this view
	 */
	const MemoryView subview(std::size_t offset) const {
		assert(offset <= data.size());
		return {address+offset, {data.begin()+offset, data.end()}};
	}

};

/**
 * A non-onwing reference to a MemoryBuffer.
 *
 * Unlike MemoryView \ref data can be written to. It is used a destination
 * for copying raw memory data.
 */
struct MemoryBufferRef
{
	uintptr_t address;
	std::span<uint8_t> data;

	operator MemoryView() const {
		return { address, data };
	}
};

/**
 * A buffer for raw memory data.
 */
class MemoryBuffer
{
	uintptr_t _address;
	std::unique_ptr<uint8_t[]> _data;
	std::size_t _size;

public:
	/**
	 * Constructs a buffer of size \p size corresponding to address \p
	 * address.
	 */
	MemoryBuffer(uintptr_t address, std::size_t size):
		_address(address),
		_data(std::make_unique<uint8_t[]>(size)),
		_size(size)
	{
	}

	uintptr_t address() const { return _address; }
	uint8_t *data() { return _data.get(); }
	const uint8_t *data() const { return _data.get(); }
	const uint8_t &operator[](std::size_t n) const { return _data.get()[n]; }
	std::size_t size() const { return _size; }

	uint8_t *begin() { return _data.get(); }
	uint8_t *end() { return _data.get()+_size; }
	const uint8_t *begin() const { return _data.get(); }
	const uint8_t *end() const { return _data.get()+_size; }

	operator MemoryView() const {
		return { _address, {_data.get(), _size}};
	}

	operator const MemoryBufferRef() {
		return { _address, {_data.get(), _size}};
	}

	/**
	 * \returns a view of \p length from \p offset.
	 */
	const MemoryView view(std::size_t offset, std::size_t length) const {
		assert(offset+length <= _size);
		return {_address+offset, {_data.get()+offset, _data.get()+offset+length}};
	}
	/**
	 * \returns a view  from \p offset to the end of the buffer.
	 */
	const MemoryView view(std::size_t offset = 0) const {
		assert(offset <= _size);
		return {_address+offset, {_data.get()+offset, _data.get()+_size}};
	}
};

/**
 * Interface for interacting with Dwarf Fortress processes.
 */
class Process
{
public:
	virtual ~Process() = default;

	/**
	 * Identifier of the process (timestamp or md5).
	 */
	virtual std::span<const uint8_t> id() const = 0;
	/**
	 * Offset of the process memory compared to addresses found in
	 * df-structures symbols.xml.
	 */
	virtual intptr_t base_offset() const = 0;

	/**
	 * Stop the process for reading memory.
	 */
	[[nodiscard]] virtual std::error_code stop() = 0;
	/**
	 * Resume the process after reading memory.
	 */
	[[nodiscard]] virtual std::error_code cont() = 0;

	/**
	 * Reads one block of memory.
	 *
	 * \param[in,out] buffer gives the address and size to read and stores the resulting data
	 */
	[[nodiscard]] virtual cppcoro::task<std::error_code> read(MemoryBufferRef buffer) = 0;
	/**
	 * Reads one block of memory.
	 *
	 * Same as read(MemoryBufferRef) but runs synchronously.
	 */
	std::error_code read_sync(MemoryBufferRef buffer) {
		std::error_code ret;
		sync([&, this]() -> cppcoro::task<> { ret = co_await read(buffer); }());
		return ret;
	}
	/**
	 * Reads multiple blocks of memory.
	 *
	 * \param[in,out] tasks gives the address and size to read and stores the resulting data for each block
	 */
	[[nodiscard]] virtual cppcoro::task<std::error_code> readv(std::span<const MemoryBufferRef> tasks);
	/**
	 * Reads multiple blocks of memory.
	 *
	 * Same as readv but runs synchronously.
	 */
	std::error_code readv_sync(std::span<const MemoryBufferRef> tasks) {
		std::error_code ret;
		sync([&, this]() -> cppcoro::task<> { ret = co_await readv(tasks); }());
		return ret;
	}
	/**
	 * Read a block of memory as an object \p T at address \p address.
	 */
	template <typename T>
	cppcoro::task<std::error_code> read(uintptr_t address, T &dest) {
		return read({address, {reinterpret_cast<uint8_t *>(&dest), sizeof(dest)}});
	}
	/**
	 * Read a block of memory as an object \p T at address \p address.
	 *
	 * Same as read(uintptr_t, T &) but runs synchronously.
	 */
	template <typename T>
	std::error_code read_sync(uintptr_t address, T &dest) {
		return read_sync({address, {reinterpret_cast<uint8_t *>(&dest), sizeof(dest)}});
	}

	/**
	 * Wait for the reading task to finish.
	 */
	virtual void sync(cppcoro::task<> &&task);
};

/**
 * Helper class for wrapping other process objects.
 *
 * Override any method and call the wrapped process using process().
 */
class ProcessWrapper: public Process
{
public:
	ProcessWrapper(std::unique_ptr<Process> &&process):
		_p(std::move(process)),
		_id(_p->id()),
		_base_offset(_p->base_offset())
	{
	}
	template <typename T, typename... Args>
	ProcessWrapper(std::in_place_type_t<T>, Args &&...args):
		ProcessWrapper(std::make_unique<T>(std::forward<Args>(args)...))
	{
	}

	std::span<const uint8_t> id() const override { return _id; }
	intptr_t base_offset() const override { return _base_offset; }
	std::error_code stop() override { return _p->stop(); }
	std::error_code cont() override { return _p->cont(); }

	void sync(cppcoro::task<> &&task) override { _p->sync(std::move(task)); }

protected:
	/**
	 * Access the wrapped process
	 */
	Process &process() { return *_p; }

private:
	std::unique_ptr<Process> _p;
	std::span<const uint8_t> _id;
	intptr_t _base_offset;
};

/**
 * Stores results of reads in case the memory is read multiple times.
 */
class ProcessCache final: public ProcessWrapper
{
public:
	using ProcessWrapper::ProcessWrapper;

	std::error_code stop() override { _cache.clear(); return ProcessWrapper::stop(); }
	std::error_code cont() override { _cache.clear(); return ProcessWrapper::cont(); }
	[[nodiscard]] cppcoro::task<std::error_code> read(MemoryBufferRef buffer) override;

private:
	struct chunk_t {
		std::unique_ptr<uint8_t[]> data;
		std::size_t size;
		cppcoro::shared_task<std::error_code> task;
		chunk_t(std::size_t);
	};
	std::map<uintptr_t, chunk_t> _cache;
};

/**
 * Vectorize reads.
 *
 * Delay single read operations and group them in a single call to the
 * underlying Process::readv().
 *
 * It will try to keep the aggregated read size below a limit. It
 * will fail if a single read is too big.
 */
class ProcessVectorizer: public ProcessWrapper
{
public:
	/**
	 * Constructs a vectorizer for \p process, trying to keep reads below
	 * \p max_size.
	 */
	ProcessVectorizer(std::unique_ptr<Process> &&process, std::size_t max_size);

	[[nodiscard]] cppcoro::task<std::error_code> read(MemoryBufferRef buffer) override;
	void sync(cppcoro::task<> &&task) override;

private:
	struct task_result_t {
		cppcoro::single_consumer_event ready;
		std::error_code ec;
	};
	std::size_t _max_total_size = 0;
	std::size_t _current_total_size = 0;
	std::vector<MemoryBufferRef> _read_tasks;
	std::vector<task_result_t *> _read_results;
	cppcoro::async_auto_reset_event _has_read_pending;

	cppcoro::task<> read_pending();
};

} // namespace dfs

#endif
