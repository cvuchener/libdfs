/*
 * Copyright 2023 Clement Vuchener
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <filesystem>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>
#include <set>

#include <format>

#include <dfs/Type.h>
#include <dfs/Structures.h>
#include <dfs/ABI.h>
#include <dfs/MemoryLayout.h>
#include <dfs/Path.h>
#include <dfs/Pointer.h>

#ifdef __linux__
#include <dfs/LinuxProcess.h>
#include <dfs/WineProcess.h>
#endif

#ifdef _WIN32
#include <dfs/Win32Process.h>
#endif


#include <cppcoro/when_all.hpp>

#include <variant>
#include <charconv>

extern "C" {
#include <getopt.h>
}

namespace fs = std::filesystem;

using namespace dfs;

struct ObjectChecker
{
	static inline constexpr std::size_t MaxVectorSize = 10000000;
	const ABI &abi;
	MemoryLayout layout;
	Process &process;
        std::map<uintptr_t, const Compound *> class_from_vtable;
	struct pointer_details {
		bool valid;
		const AbstractType *type;
		std::string location;
	};
	std::map<uintptr_t, pointer_details> visited_pointers;

	bool show_vtable_errors = true;

	template<typename T>
	void print_raw_words(uintptr_t addr, const TypeInfo &info) {
		static constexpr std::size_t byte_per_line = 16;
		static constexpr std::size_t word_per_line = byte_per_line/sizeof(T);
		auto begin = (addr&~uintptr_t(byte_per_line-1)) - byte_per_line;
		auto end = ((addr+info.size+byte_per_line-1)&~uintptr_t(byte_per_line-1)) + byte_per_line;
		std::array<T, word_per_line> line;
		for (; begin < end; begin += byte_per_line) {
			std::cout << std::format("{:x}\t", begin);
			if (begin > addr && begin < addr+info.size)
				std::cout << std::format("\e[1;33m");
			if (auto err = process.read_sync({begin, {reinterpret_cast<uint8_t *>(line.data()), byte_per_line}})) {
				std::cout << std::format("{}", addr, err.message());
			}
			else for (std::size_t i = 0; i < word_per_line; ++i) {
				if (begin + i*sizeof(T) == addr)
					std::cout << std::format("\e[1;33m");
				else if (begin + i*sizeof(T) == addr+info.size)
					std::cout << std::format("\e[0m");
				std::cout << std::format(" {:0{}x}", line[i], sizeof(T)*2);
			}
			if (begin+byte_per_line > addr && begin+byte_per_line <= addr+info.size)
				std::cout << std::format("\e[0m");
			std::cout << std::format("\n");
		}
	}

	void print_raw_data(uintptr_t addr, const TypeInfo &info) {
		switch (info.align) {
		case 1:
			print_raw_words<uint8_t>(addr, info);
			break;
		case 2:
			print_raw_words<uint16_t>(addr, info);
			break;
		default:
		case 4:
			print_raw_words<uint32_t>(addr, info);
			break;
		case 8:
			print_raw_words<uint64_t>(addr, info);
			break;
		}
	}

        ObjectChecker(const Structures &structures,
                      const Structures::VersionInfo &version, Process &process):
		abi(ABI::fromVersionName(version.version_name)), layout(structures, abi), process(process)
	{
		for (const auto &[name, type]: structures.allCompoundTypes())
			if (type.vtable) {
				auto it = version.vtables_addresses.find(type.symbol ? *type.symbol : name);
				if (it == version.vtables_addresses.end()) {
					if (show_vtable_errors)
						std::cerr << std::format("Missing vtable for type {}\n", name);
					continue;
				}
				class_from_vtable.emplace(it->second + process.base_offset(), &type);
			}
        }

        template<typename T>
	cppcoro::task<> check_object(const std::string &name, uintptr_t address, const T &type)
	{
		auto type_info = layout.type_info.at(&type);
		MemoryBuffer data(address, type_info.size);
		if (auto err = co_await process.read(data)) {
			std::cout << std::format("{} ({:#x}): invalid global object ({})\n", name, address, err.message());
			co_return;
		}
		co_await check_value(name, data, type);
	}

	cppcoro::task<> check_value(std::string name, MemoryView data, const AbstractType &)
	{
		co_return;
	}

	cppcoro::task<> check_value(std::string name, MemoryView data, const Compound &compound)
	{
		if (compound.is_union)
			co_return;
		if (compound.vtable) {
			// TODO: check vtable
		}
		if (compound.parent) {
			co_await check_value(name, data, *compound.parent->get());
		}
		std::vector<cppcoro::task<>> tasks;
		for (std::size_t i = 0; i < compound.members.size(); ++i) {
			const auto &member = compound.members[i];
			auto offset = layout.compound_layout.at(&compound).member_offsets[i];
			member.type.visit([&, this](const auto &type) {
				tasks.push_back(check_value(
						std::format("{}.{}", name, member.name),
						data.subview(offset),
						type));
			});
		}
		co_await cppcoro::when_all(std::move(tasks));
	}

	cppcoro::task<> check_value(std::string name, MemoryView data, const Container &container)
	{
		auto type_info = layout.getTypeInfo(container.item_type);
		if (type_info.size == 0) // skip missing types
			co_return;
		const auto &container_info = abi.container_type(container.container_type);
		switch (container.container_type) {
		case Container::Type::StdVector: {
			auto vec = co_await abi.read_vector(process, data, type_info);
			if (vec.err) {
				std::cout << std::format("{} ({:#x}): invalid vector ({})\n", name, data.address,
						vec.err.message());
				print_raw_data(data.address, container_info);
				co_return;
			}
			else if (vec.size > MaxVectorSize) {
				std::cout << std::format("{} ({:#x}): vector too big (size = {})\n", name, data.address, vec.size);
				print_raw_data(data.address, container_info);
				co_return;
			}
			else if (vec.size == 0)
				co_return;
			MemoryBuffer item_data(vec.data, vec.size * type_info.size);
			auto err = co_await process.read(item_data);
			if (err) {
				std::cout << std::format("{} ({:#x}): invalid vector data {:#x}@{} ({})\n", name, data.address, vec.data, vec.size, err.message());
				print_raw_data(data.address, container_info);
				co_return;
			}
			std::vector<cppcoro::task<>> tasks;
			for (std::size_t i = 0; i < vec.size; ++i) {
				container.item_type.visit([&, this](const auto &type){
					tasks.push_back(check_value(
							std::format("{}[{}]", name, i),
							item_data.view(i * type_info.size, type_info.size),
							type));
				});
			}
			co_await cppcoro::when_all(std::move(tasks));
			break;
		}
		case Container::Type::Pointer: {
			if (container.has_bad_pointers)
				break;
                        auto ptr = abi.get_pointer(data);
			if (ptr == 0)
				co_return;
                        auto actual_type = container.item_type.get_if<AbstractType>();
			// down cast class types
			const Compound *downcast_type = nullptr;
			auto compound = container.item_type.get_if<Compound>();
			if (compound && compound->vtable) {
				uintptr_t vtable = 0;
				co_await process.read({ptr, {reinterpret_cast<uint8_t *>(&vtable), abi.pointer().size}});
				auto it = class_from_vtable.find(vtable);
				if (it == class_from_vtable.end()) {
					if (show_vtable_errors)
						std::cout << std::format("{} ({:#x}): unknown vtable {:#x}\n", name, data.address, vtable);
				}
				else {
					actual_type = downcast_type = it->second;
					type_info = layout.type_info.at(actual_type);
				}
			}
			// check pointer
			if (ptr % type_info.align != 0) {
				std::cout << std::format("{} ({:#x}): invalid pointer {:#x} unaligned (required {})\n", name, data.address, ptr, type_info.align);
				print_raw_data(data.address, container_info);
				co_return;
			}
			auto it = visited_pointers.lower_bound(ptr);
			if (it != visited_pointers.end() && it->first == ptr) {
				if (!it->second.valid) {
					std::cout << std::format("{} ({:#x}): invalid pointer {:#x} (first visited: {})\n", name, data.address, ptr, it->second.location);
					print_raw_data(data.address, container_info);
				}
				else if (it->second.type != actual_type) {
					std::cout << std::format("{} ({:#x}): pointer {:#x} already visited with different type ({}).\n", name, data.address, ptr, it->second.location);
					print_raw_data(data.address, container_info);
				}
				co_return;
			}
			it = visited_pointers.emplace_hint(it, ptr, pointer_details{true, actual_type, name});
			MemoryBuffer item_data(ptr, type_info.size);
			if (auto err = co_await process.read(item_data)) {
				it->second.valid = false;
				std::cout << std::format("{} ({:#x}): invalid pointer {:#x} ({})\n", name, data.address, ptr, err.message());
				print_raw_data(data.address, container_info);
				co_return;
			}
			if (downcast_type)
				co_await check_value(std::format("(*{})", name), item_data, *downcast_type);
			else
				co_await container.item_type.visit([
					this,
					name = std::format("(*{})", name),
					data = item_data.view()
				](const auto &type){
					return check_value(name, data, type);
				});
			break;
		}
		case Container::Type::StaticArray: {
			assert(container.extent != Container::NoExtent);
			std::vector<cppcoro::task<>> tasks;
			for (std::size_t i = 0; i < container.extent; ++i) {
				container.item_type.visit([&, this](const auto &type){
					tasks.push_back(check_value(
							std::format("{}[{}]", name, i),
							data.subview(i * type_info.size, type_info.size),
							type));
				});
			}
			co_await cppcoro::when_all(std::move(tasks));
			break;
		}
		default:
			break;
		}
	}

	cppcoro::task<> check_value(std::string name, MemoryView data, const PrimitiveType &type)
	{
		auto type_info = abi.primitive_type(type.type);
		switch (type.type) {
		case PrimitiveType::StdString: {
			auto res = co_await abi.read_string(process, data);
			if (res.err) {
				std::cout << std::format("{} ({:#x}): invalid string ({})\n", name, data.address,
						res.err.message());
				print_raw_data(data.address, type_info);
				co_return;
			}
			break;
		}
		default:
			break;
		}
	}
};

static constexpr const char *usage = "{} [options...] df_structures pid\n"
	"df_structures must be a path to a directory containing df-structures xml.\n"
	"Options are:\n"
	" -t, --type type   Process type (native or wine)\n"
	" -c, --cache       Use cache\n"
	" -v, --vectorize   Use vectorizer\n"
	" --no-vtable-errors Hide vtable errors\n"
	" -h, --help        Print this help message\n";

int main(int argc, char *argv[]) try
{
	using namespace std::literals::string_view_literals;

	std::string process_type = "native";
	bool use_cache = false;
	bool use_vectorizer = false;
	int no_vtable_errors = false;
	static option options[] = {
		{"type", required_argument, nullptr, 't'},
		{"no-vtable-errors", no_argument, &no_vtable_errors, 1},
		{"help", no_argument, nullptr, 'h'},
		{nullptr, 0, nullptr, 0}
	};
	{
		int opt;
		while ((opt = getopt_long(argc, argv, ":t:cv", options, nullptr)) != -1) {
			switch (opt) {
			case 0:
				break;
			case 't': // type
				process_type = optarg;
				break;
			case 'c': // cache
				use_cache = true;
				break;
			case 'v': // vectorize
				use_vectorizer = true;
				break;
			case '?': // invalid option
				std::cerr << "Invalid option\n";
				std::cerr << std::format(usage, argv[0]);
				return EXIT_FAILURE;
			case ':': // missing argument
				std::cerr << "Missing option argument\n";
				std::cerr << std::format(usage, argv[0]);
				return EXIT_FAILURE;
			case 'h': // help
				std::cerr << std::format(usage, argv[0]);
				return EXIT_SUCCESS;
			}
		}
	}
	if (argc - optind < 2) {
		std::cerr << "This command must have at least two parameters\n";
		std::cerr << std::format(usage, argv[0]);
		return EXIT_FAILURE;
	}
	fs::path df_structures_path = argv[optind];
	Structures structures(df_structures_path);

	int pid = 0;
	{
		std::string_view arg = argv[optind+1];
		auto res = std::from_chars(arg.data(), arg.data()+arg.size(), pid);
		if (res.ptr != arg.data()+arg.size()) {
			std::cerr << "Invalid pid\n";
			return EXIT_FAILURE;
		}
	}

	std::unique_ptr<Process> process;
	if (process_type == "native") {
#if defined(__linux__)
		process = std::make_unique<LinuxProcess>(pid);
#elif defined(_WIN32)
		process = std::make_unique<Win32Process>(pid);
#else
		std::cerr << "\"native\" process not supported on this platform\n";
		return EXIT_FAILURE;
#endif
	}
#ifdef __linux__
	else if (process_type == "wine") {
		process = std::make_unique<WineProcess>(pid);
	}
#endif
	else {
		std::cerr << std::format("Invalid process type: {}\n", process_type);
		return EXIT_FAILURE;
	}

	if (use_vectorizer) {
		auto tmp = std::move(process);
		process = std::make_unique<ProcessVectorizer>(std::move(tmp), 48*1024*1024);
	}
	if (use_cache) {
		auto tmp = std::move(process);
		process = std::make_unique<ProcessCache>(std::move(tmp));
	}

	auto version = structures.versionById(process->id());
	if (!version) {
		std::cerr << std::format("Version not found\n");
		for (const auto &version: structures.allVersions()) {
			std::cout << std::format("{}:", version.version_name);
			for (auto byte: version.id)
				std::cout << std::format(" {:02x}", byte);
			std::cout << std::format("\n");
		}
		return EXIT_FAILURE;
	}
	std::cerr << std::format("Found version {}\n", version->version_name);

        ObjectChecker checker(structures, *version, *process);
	checker.show_vtable_errors = !no_vtable_errors;

	if (argc - optind >= 3) for (int i = optind+2; i < argc; ++i) {
		auto ptr = Pointer::fromGlobal(structures, *version, checker.layout, parse_path(argv[i]), process.get());
		ptr.type.visit([&checker, name = std::string(argv[i]), address = ptr.address](const auto &type) {
			checker.process.sync(checker.check_object(name, address, type));
		});
	}
	else for (const auto &[name, type]: structures.allGlobalObjects()) {
		auto it = version->global_addresses.find(name);
		if (it == version->global_addresses.end()) {
			std::cerr << std::format("Missing address for {}\n", name);
			continue;
		}
		auto address = it->second + process->base_offset();
		type.visit([&checker, name = name, address](const auto &type) {
			checker.process.sync(checker.check_object(name, address, type));
		});
	}

	return 0;
}
catch (std::exception &e) {
	std::cerr << std::format("Could not load structures: {}\n", e.what());
	return -1;
}

