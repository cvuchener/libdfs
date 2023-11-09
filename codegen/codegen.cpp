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

#include <format>
#include <fstream>
#include <iostream>
#include <set>

#include <dfs/Structures.h>
using namespace dfs;

std::string getIntegralTypeName(const PrimitiveType &type)
{
	switch (type.type) {
	case PrimitiveType::Int8: return "int8_t";
	case PrimitiveType::UInt8: return "uint8_t";
	case PrimitiveType::Int16: return "int16_t";
	case PrimitiveType::UInt16: return "uint16_t";
	case PrimitiveType::Int32: return "int32_t";
	case PrimitiveType::UInt32: return "uint32_t";
	case PrimitiveType::Int64: return "int64_t";
	case PrimitiveType::UInt64: return "uint64_t";
	case PrimitiveType::Char: return "char";
	case PrimitiveType::Bool: return "bool";
	case PrimitiveType::Long: return "long";
	case PrimitiveType::ULong: return "unsigned long";
	case PrimitiveType::SizeT: return "size_t";
	default: throw std::invalid_argument("not an integral type");
	}
}

class CodeGenerator
{
public:
	virtual ~CodeGenerator() = default;

	virtual std::set<std::string> getInterfaceDependencies() const = 0;
	virtual std::set<std::string> getImplementationDependencies() const = 0;

	virtual void writeInterface(std::ostream &) const = 0;
	virtual void writeImplementation(std::ostream &, std::string_view use_namespace) const = 0;
};

class EnumGenerator: public CodeGenerator
{
	std::string name;
	const Enum &def;
	std::vector<std::pair<int, decltype(Enum::values)::const_iterator>> sorted_values;

	std::string getAttributeTypeName(const Enum::Attribute &attr) const
	{
		if (attr.type) {
			if (auto enum_type = attr.type->get_if<Enum>()) {
				if (enum_type == &def)
					return name;
				else
					return std::format("{0}::{0}", attr.type->name());
			}
			else if (auto primitive_type = attr.type->get_if<PrimitiveType>())
				return getIntegralTypeName(*primitive_type);
			else
				throw std::invalid_argument("unexpected attribute type");
		}
		else
			return "std::string_view";
	}

public:
	EnumGenerator(std::string name, const Enum &e):
		name(std::move(name)),
		def(e)
	{
		sorted_values.reserve(def.values.size());
		for (auto it = def.values.begin(); it != def.values.end(); ++it)
			sorted_values.emplace_back(it->second.value, it);
		std::ranges::sort(sorted_values, std::less<>{}, &decltype(sorted_values)::value_type::first);
	}

	std::set<std::string> getInterfaceDependencies() const override
	{
		return { "string_view", "optional" };
	}

	std::set<std::string> getImplementationDependencies() const override
	{
		return { "map" };
	}

	void writeInterface(std::ostream &out) const override
	{
		out << std::format("namespace {} {{\n", name);

		// Enum values
		out << std::format("enum {} {{\n", name);
		for (const auto &[value, it]: sorted_values)
			out << std::format("\t{} = {},\n", it->first, value);
		out << std::format("}};\n");

		// Enum value count
		out << std::format("inline constexpr std::underlying_type_t<{}> Count = {};\n\n", name, def.count);

		// Prototypes
		out << std::format("std::optional<{}> from_string(std::string_view str);\n", name);
		out << std::format("std::string_view to_string({} value);\n", name);
		for (const auto &[attr_name, attr_def]: def.attributes) {
			std::string attr_type = getAttributeTypeName(attr_def);
			out << std::format("{2} {1}({0} value);\n", name, attr_name, attr_type);
		}

		out << std::format("\n}} // namespace {}\n", name);
		out << std::format("using {0}_t = {0}::{0};\n\n", name);
	}

	void writeImplementation(std::ostream &out, std::string_view use_namespace) const override
	{
		if (use_namespace.empty())
			out << std::format("namespace {} {{\n", name);
		else
			out << std::format("namespace {}::{} {{\n", use_namespace, name);

		// from_string
		out << std::format("std::optional<{0}> from_string(std::string_view str) {{\n"
				"\tstatic const std::map<std::string_view, {0}> names = {{\n",
				name);
		for (const auto &[name, item]: def.values)
			out << std::format("\t\t{{\"{0}\", {0}}},\n", name);
		out << "\t};\n"
			"\tauto it = names.find(str);\n"
			"\tif (it != names.end())\n"
			"\t\treturn it->second;\n"
			"\telse\n"
			"\t\treturn std::nullopt;\n"
			"}\n\n";

		// to_string
		out << std::format("std::string_view to_string({} value) {{\n"
				"\tswitch (value) {{\n",
				name);
		for (const auto &[name, item]: def.values)
			out << std::format("\tcase {0}: return \"{0}\";\n", name);
		out << "\tdefault: return {};\n"
			"\t}\n"
			"}\n\n";

		// attributes
		for (const auto &[attr_name, attr_def]: def.attributes) {
			std::string attr_type = getAttributeTypeName(attr_def);

			out << std::format(
					"{2} {1}({0} value) {{\n"
					"\tswitch (value) {{\n",
					name, attr_name, attr_type);
			auto attr_value_to_string = overloaded{
				[](std::string str){return std::format("\"{}\"", str);},
				[](bool value) -> std::string {return value ? "true" : "false";},
				[](std::integral auto i){return std::to_string(i);},
				[&](Enum::EnumValueIterator it) { return std::format("{}::{}", attr_type, it->first); }
			};
			for (const auto &value: sorted_values) {
				const auto &[name, item] = *value.second;
				auto attr_it = item.attributes.find(attr_name);
				if (attr_it != item.attributes.end())
					out << std::format("\tcase {}: return {};\n", name, std::visit(attr_value_to_string, attr_it->second));
			}
			if (attr_def.default_value)
				out << std::format("\tdefault: return {};\n", std::visit(attr_value_to_string, attr_def.default_value.value()));
			else
				out << std::format("\tdefault: return {{}};\n");
			out << "\t}\n"
				"}\n\n";
		}

		out << std::format("\n}} // namespace {}\n", name);
	}
};

class BitfieldGenerator: public CodeGenerator
{
	std::string name;
	const Bitfield &def;
public:
	BitfieldGenerator(std::string name, const Bitfield &b):
		name(std::move(name)),
		def(b)
	{
	}

	std::set<std::string> getInterfaceDependencies() const override
	{
		return { "cstdint" };
	}

	std::set<std::string> getImplementationDependencies() const override
	{
		return {};
	}

	void writeInterface(std::ostream &out) const override
	{
		out << std::format("union {} {{\n", name);
		auto base_type = getIntegralTypeName(def);
		out << std::format(
				"\tusing underlying_type = {0};\n"
				"\t{1}() noexcept = default;\n"
				"\texplicit {1}({0} v) noexcept: value(v) {{}}\n"
				"\t{1} &operator=({0} v) noexcept {{ value = v; return *this; }}\n"
				"\texplicit operator {0}() const noexcept {{ return value; }}\n\n",
				base_type, name);

		out << std::format("\t{} value;\n", base_type);
		out << std::format("\tstruct {{\n");
		for (const auto &f: def.flags)
			out << std::format("\t\t{} {}: {};\n", base_type, f.name, f.count);
		out << std::format("\t}} bits;\n\n");
		out << std::format("\tenum bits_t: {} {{\n", base_type);
		for (const auto &f: def.flags)
			out << std::format("\t\t{}_bits = {:#0x},\n", f.name, ((1u<<f.count)-1)<<f.offset);
		out << std::format("\t}};\n\n");
		out << std::format("\tenum pos_t {{\n");
		for (const auto &f: def.flags)
			out << std::format("\t\t{}_pos = {},\n", f.name, f.offset);
		out << std::format("\t}};\n\n");
		out << std::format("\tenum count_t {{\n");
		for (const auto &f: def.flags)
			out << std::format("\t\t{}_count = {},\n", f.name, f.count);
		out << std::format("\t}};\n");
		out << std::format("}};\n\n");
	}

	void writeImplementation(std::ostream &, std::string_view use_namespace) const override
	{
	}
};

namespace fs = std::filesystem;

static inline constexpr char usage[] = R"***(
{0} <df-structures-path> <output-prefix> [<general-options>...] <type> [<type-options> ...] ...
General options:
  --namespace <name>  add namespace around type declarations
Type options:
  --as <name>         use this name instead of df-structures name (mandatory for member types)
)***";

int main(int argc, char *argv[]) try
{
	if (argc < 3) {
		std::cerr << std::format(usage, argv[0]);
		return EXIT_FAILURE;
	}
	fs::path df_structures_path = argv[1];
	fs::path out_path = argv[2];

	Structures structures(df_structures_path);

	std::string use_namespace;
	int arg_index = 3;
	// General options
	while (arg_index < argc && argv[arg_index][0] == '-') {
		using namespace std::literals;
		if ("--namespace"s == argv[arg_index]) {
			if (arg_index+1 >= argc) {
				std::cerr << "missing namespace name" << std::endl;
				return EXIT_FAILURE;
			}
			use_namespace = argv[arg_index+1];
			arg_index += 2;
		}
		else {
			std::cerr << "unknown general option: " << argv[arg_index] << std::endl;
			std::cerr << std::format(usage, argv[0]);
			return EXIT_FAILURE;
		}
	}

	std::vector<std::unique_ptr<CodeGenerator>> generators;
	while (arg_index < argc) {
		std::string name = argv[arg_index++];
		auto path = parse_path(name);
		std::string alias;
		// Type options
		while (arg_index < argc && argv[arg_index][0] == '-') {
			using namespace std::literals;
			if ("--as"s == argv[arg_index]) {
				if (arg_index+1 >= argc) {
					std::cerr << "missing type name" << std::endl;
					return EXIT_FAILURE;
				}
				alias = argv[arg_index+1];
				arg_index += 2;
			}
			else {
				std::cerr << "unknown type option: " << argv[arg_index] << std::endl;
				std::cerr << std::format(usage, argv[0]);
				return EXIT_FAILURE;
			}
		}
		if (path.size() == 1 && holds_alternative<path::identifier>(path[0])) {
			if (alias.empty())
				alias = name;
			if (auto type = structures.findEnum(name))
				generators.push_back(std::make_unique<EnumGenerator>(std::move(alias), *type));
			else if (auto type = structures.findBitfield(name))
				generators.push_back(std::make_unique<BitfieldGenerator>(std::move(alias), *type));
			else
				throw std::runtime_error("type not found");
		}
		else if (path.size() > 1) {
			if (alias.empty())
				throw std::runtime_error("nested types require an alias");
			auto compound = structures.findCompound(path | std::views::take(path.size()-1));
			if (!compound)
				throw std::runtime_error("compound not found");
			if (auto member_name = get_if<path::identifier>(&*std::prev(path.end()))) {
				auto r = compound->searchMember(member_name->identifier);
				const auto &member = r.back().first->members.at(r.back().second);
				generators.push_back(member.type.visit(overloaded{
					[&alias](const Enum &e) -> std::unique_ptr<CodeGenerator> {
						return std::make_unique<EnumGenerator>(std::move(alias), e);
					},
					[&alias](const Bitfield &bf) -> std::unique_ptr<CodeGenerator> {
						return std::make_unique<BitfieldGenerator>(std::move(alias), bf);
					},
					[](const AbstractType &) -> std::unique_ptr<CodeGenerator> {
						throw std::runtime_error("unsupported type");
					}
				}));
			}
			else
				throw std::runtime_error("path must end with an identifier");
		}
		else
			throw std::runtime_error("invalid path");
	}

	auto header_filename = out_path.concat(".h");
	auto source_filename = out_path.replace_extension(".cpp");
	if (auto header = std::ofstream(header_filename.c_str())) {
		header << std::format(
			"#ifndef INCLUDED_{0}\n"
			"#define INCLUDED_{0}\n\n",
			header_filename.stem().string());
		std::set<std::string> deps;
		for (const auto &g: generators)
			deps.merge(g->getInterfaceDependencies());
		for (const auto &d: deps)
			header << std::format("#include <{}>\n", d);
		header << "\n";
		if (!use_namespace.empty())
			header << std::format("namespace {} {{\n\n", use_namespace);
		for (const auto &g: generators)
			g->writeInterface(header);
		if (!use_namespace.empty())
			header << std::format("\n}} // namespace {}\n\n", use_namespace);
		header << "#endif\n";
	}
	else throw std::runtime_error("Failed to create header file");
	if (auto source = std::ofstream(source_filename.c_str())) {
		source << std::format("#include \"{}\"\n\n", header_filename.filename().string());
		std::set<std::string> deps;
		for (const auto &g: generators)
			deps.merge(g->getImplementationDependencies());
		for (const auto &d: deps)
			source << std::format("#include <{}>\n", d);
		source << "\n";
		for (const auto &g: generators)
			g->writeImplementation(source, use_namespace);
	}
	else throw std::runtime_error("Failed to create source file");
	return 0;
}
catch (std::exception &e) {
	std::cerr << std::format("Failed: {}\n", e.what());
}
