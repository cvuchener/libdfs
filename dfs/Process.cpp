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

#include "Process.h"

#include <algorithm>
#include <cppcoro/when_all.hpp>
#include <cppcoro/sync_wait.hpp>

using namespace dfs;

cppcoro::task<std::error_code> Process::readv(std::span<const MemoryBufferRef> tasks)
{
	std::vector<cppcoro::task<std::error_code>> t;
	t.reserve(tasks.size());
	for (const auto &buffer: tasks)
		t.push_back(read(buffer));
	auto res = co_await cppcoro::when_all(std::move(t));
	for (auto r: res)
		if (r)
			co_return r;
	co_return std::error_code{};
}

void Process::sync(cppcoro::task<> &&task)
{
	cppcoro::sync_wait(std::move(task));
}

static constexpr std::size_t PageSize = 4096;

ProcessCache::chunk_t::chunk_t(std::size_t len):
	data(std::make_unique<uint8_t[]>(len)),
	size(len)
{
}

cppcoro::task<std::error_code> ProcessCache::read(MemoryBufferRef buffer)
{
	auto chunk_end = [](auto &chunk_pair){ return chunk_pair.first + chunk_pair.second.size; };
	auto read_chunk = [this](auto hint, uintptr_t start_page, uintptr_t end_page)
			-> decltype(_cache)::iterator {
		auto chunk_len = end_page-start_page;
		auto it = _cache.emplace_hint(hint, start_page, chunk_len);
		it->second.task = [](Process &p, MemoryBufferRef buffer) -> cppcoro::shared_task<std::error_code> {
			co_return co_await p.read(buffer);

		}(process(), {start_page, {it->second.data.get(), chunk_len}});
		return it;
	};

	auto start_page = buffer.address & ~(static_cast<uintptr_t>(PageSize)-1);
	auto end_page = ((buffer.address+buffer.data.size()-1) & ~(static_cast<uintptr_t>(PageSize)-1))+PageSize;
	auto ub = _cache.upper_bound(buffer.address);
	decltype(_cache)::iterator it;
	std::vector<decltype(_cache)::iterator> chunks;
	if (ub == _cache.begin() || chunk_end(*prev(ub)) <= buffer.address) {
		if (ub != _cache.end() && buffer.address+buffer.data.size() > ub->first)
			it = read_chunk(ub, start_page, ub->first);
		else
			it = read_chunk(ub, start_page, end_page);
	}
	else {
		it = prev(ub);
	}
	chunks.push_back(it);
	while (it != _cache.end() && chunk_end(*it) < end_page) {
		if (next(it) == _cache.end() || next(it)->first >= end_page) {
			it = read_chunk(next(it), chunk_end(*it), end_page);
		}
		else if (chunk_end(*it) != next(it)->first) {
			it = read_chunk(next(it), chunk_end(*it), next(it)->first);
		}
		else {
			++it;
		}
		chunks.push_back(it);
	}
	std::vector<cppcoro::shared_task<std::error_code>> tasks;
	tasks.reserve(chunks.size());
	for (auto chunk: chunks)
		tasks.push_back(chunk->second.task);
	auto res = co_await cppcoro::when_all(tasks);
	std::error_code ret = {};
	for (std::size_t i = 0; i < chunks.size(); ++i) {
		if (auto cr = res[i].get()) {
			ret = cr;
		}
		else {
			auto &chunk = *chunks[i];
			auto addr = chunk.first;
			auto data =  chunk.second.data.get();
			auto size = chunk.second.size;
			auto end = addr + size;
			std::copy(
				data + (addr < buffer.address ? buffer.address-addr : 0),
				data + size + (end > buffer.address+buffer.data.size()
					? buffer.address+buffer.data.size()-end : 0),
				buffer.data.data() + (addr < buffer.address ? 0 : addr-buffer.address)
			);
		}
	}
	co_return ret;
}

ProcessVectorizer::ProcessVectorizer(std::unique_ptr<Process> &&process, std::size_t max_size):
	ProcessWrapper(std::move(process)),
	_max_total_size(max_size)
{
}

[[nodiscard]] cppcoro::task<std::error_code> ProcessVectorizer::read(MemoryBufferRef buffer)
{
	if (_current_total_size + buffer.data.size() > _max_total_size)
		co_await read_pending();
	_current_total_size += buffer.data.size();
	_read_tasks.emplace_back(buffer);
	task_result_t result;
	_read_results.emplace_back(&result);
	_has_read_pending.set();
	co_await result.ready;
	co_return result.ec;
}

void ProcessVectorizer::sync(cppcoro::task<> &&task)
{
	auto shared_task = [](auto &&task) -> cppcoro::shared_task<> {
		co_await std::move(task);
	}(std::move(task));
	ProcessWrapper::sync([this, shared_task]() -> cppcoro::task<> {
		co_await cppcoro::when_all(shared_task, [this, shared_task]() -> cppcoro::task<> {
			while (!shared_task.is_ready()) {
				co_await _has_read_pending;
				co_await read_pending();
			}
		}());
	}());
}

cppcoro::task<> ProcessVectorizer::read_pending()
{
	auto tasks = std::move(_read_tasks);
	auto results = std::move(_read_results);
	_current_total_size = 0;
	if (!tasks.empty()) {
		std::error_code err;
		co_await process().readv(tasks);
		for (auto &r: results) {
			r->ec = err;
			r->ready.set();
		}
	}
}
