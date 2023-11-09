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

#include <dfs/MemoryLayout.h>
#include <dfs/Process.h>
#include <dfs/Structures.h>
#include <dfs/Path.h>
#include <dfs/overloaded.h>
#include <dfs/Reader.h>
#include <dfs/ItemReader.h>
#include <dfs/CompoundReader.h>
#include <dfs/PolymorphicReader.h>

#ifdef __linux__
#include <dfs/LinuxProcess.h>
#include <dfs/WineProcess.h>
#endif

#ifdef _WIN32
#include <dfs/Win32Process.h>
#endif

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <memory>
#include <concepts>
#include <format>
#include <filesystem>
#include <iostream>
#include <typeindex>
#include <ranges>
#include <cassert>
#include <fstream>
#include <charconv>


#include "reader_df_types.h"

using namespace dfs;

struct language_name
{
	std::string first_name;
	std::string nickname;
	std::array<int32_t, 7> words;
	std::array<int16_t, 7> parts_of_speech;
	int32_t language;

	using reader_type = StructureReader<language_name, "language_name",
		Field<&language_name::first_name, "first_name">,
		Field<&language_name::nickname, "nickname">,
		Field<&language_name::words, "words">,
		Field<&language_name::parts_of_speech, "parts_of_speech">,
		Field<&language_name::language, "language">
	>;
};

struct language_word
{
	std::string word;
	std::array<std::string, 9> forms;

	using reader_type = StructureReader<language_word, "language_word",
		Field<&language_word::word, "word">,
		Field<&language_word::forms, "forms">
	>;
};

struct language_translation
{
	std::string name;
	std::vector<std::unique_ptr<std::string>> words;

	using reader_type = StructureReader<language_translation, "language_translation",
		Field<&language_translation::name, "name">,
		Field<&language_translation::words, "words">
	>;
};

struct poetic_form_part
{
	struct histfig_t {
		int32_t subject_histfig;
		using reader_type = StructureReader<histfig_t, "poetic_form_subject_target.Histfig",
		      Field<&histfig_t::subject_histfig, "subject_histfig">
		>;
	};
	struct concept_t {
		int32_t subject_topic;
		using reader_type = StructureReader<concept_t, "poetic_form_subject_target.Concept",
		      Field<&concept_t::subject_topic, "subject_topic">
		>;
	};
	std::vector<poetic_form_subject_t> line_subject;
	std::vector<std::variant<std::monostate, histfig_t, concept_t>> line_subject_target;

	auto get_target_type() const {
		return std::views::transform(line_subject, [](poetic_form_subject_t subject) -> std::size_t {
			switch (subject) {
			case poetic_form_subject::Histfig: return 0;
			case poetic_form_subject::Concept: return 1;
			default: return -1;
			}
		});
	}
};

template <>
struct dfs::compound_reader_type<poetic_form_part> {
	using type = StructureReaderSeq<poetic_form_part, "poetic_form_part",
		Field<&poetic_form_part::line_subject, "line_subject">,
		Field<&poetic_form_part::line_subject_target, "line_subject_target", &poetic_form_part::get_target_type>
	>;
};


struct poetic_form
{
	language_name name;
	std::vector<std::unique_ptr<poetic_form_part>> parts;

	using reader_type = StructureReader<poetic_form, "poetic_form",
	      Field<&poetic_form::name, "name">,
	      Field<&poetic_form::parts, "parts">
	>;
};

struct itemdef
{
	virtual ~itemdef() = default;

	std::string id;
	int subtype;

	virtual std::string_view describe() const = 0;

	using reader_type = StructureReader<itemdef, "itemdef",
	      Field<&itemdef::id, "id">,
	      Field<&itemdef::subtype, "subtype">
	>;
};

#define MAKE_ITEMDEF(type) \
struct itemdef_##type##st: itemdef \
{ \
	std::string name, name_plural; \
	std::string_view describe() const override { return name_plural; } \
	using reader_type = StructureReader<itemdef_##type##st, "itemdef_"#type"st", \
	      Base<itemdef>, \
	      Field<&itemdef_##type##st::name, "name">, \
	      Field<&itemdef_##type##st::name_plural, "name_plural"> \
	>; \
}

MAKE_ITEMDEF(ammo);
MAKE_ITEMDEF(armor);

struct itemdef_foodst: itemdef
{
	std::string name;
	std::string_view describe() const override { return name; }
	using reader_type = StructureReader<itemdef_foodst, "itemdef_foodst",
	      Base<itemdef>,
	      Field<&itemdef_foodst::name, "name">
	>;
};

MAKE_ITEMDEF(gloves);
MAKE_ITEMDEF(helm);
MAKE_ITEMDEF(instrument);
MAKE_ITEMDEF(pants);
MAKE_ITEMDEF(shield);
MAKE_ITEMDEF(shoes);
MAKE_ITEMDEF(siegeammo);
MAKE_ITEMDEF(tool);
MAKE_ITEMDEF(toy);
MAKE_ITEMDEF(trapcomp);
MAKE_ITEMDEF(weapon);

template <>
struct dfs::polymorphic_reader_type<itemdef> {
	using type = PolymorphicReader<itemdef, itemdef_ammost, itemdef_armorst,
	      itemdef_foodst, itemdef_glovesst, itemdef_helmst,
	      itemdef_instrumentst, itemdef_pantsst, itemdef_shieldst,
	      itemdef_shoesst, itemdef_siegeammost, itemdef_toolst,
	      itemdef_toyst, itemdef_trapcompst, itemdef_weaponst>;
};

static_assert(std::is_same_v<typename compound_reader_parent<compound_reader_type_t<itemdef_weaponst>>::type, itemdef>);
static_assert(PolymorphicStructure<itemdef_weaponst>);
static_assert(std::is_same_v<polymorphic_base<itemdef_weaponst>::type, itemdef>);

struct item
{
	virtual ~item() = default;
	virtual const char *describe() const = 0;

	using reader_type = StructureReader<item, "item">;
};

struct item_actual: item
{
	int stack_size;

	using reader_type = StructureReader<item_actual, "item_actual",
	      Base<item>,
	      Field<&item_actual::stack_size, "stack_size">
	>;
};

struct item_crafted: item_actual
{
	const char *describe() const override { return "item_crafted"; }
	using reader_type = StructureReader<item_crafted, "item_crafted",
	      Base<item_actual>
	>;
};

struct item_constructed: item_crafted
{
	using reader_type = StructureReader<item_constructed, "item_constructed",
	      Base<item_crafted>
	>;
};

#define ITEM(type, name) \
struct type: item_constructed \
{ \
	const char *describe() const override { return name; } \
 \
	using reader_type = StructureReader<type, #type, \
	      Base<item_constructed> \
	>; \
}

ITEM(item_armorst, "Armor");
ITEM(item_shoesst, "Shoes");
ITEM(item_shieldst, "Shield");
ITEM(item_helmst, "Helm");
ITEM(item_glovesst, "Gloves");
ITEM(item_pantsst, "Pants");
ITEM(item_weaponst, "Weapon");

template <>
struct dfs::polymorphic_reader_type<item> {
	using type = PolymorphicReader<item, item_actual, item_crafted, item_constructed,
	      item_armorst, item_shoesst, item_shieldst, item_helmst,
	      item_glovesst, item_pantsst, item_weaponst>;
};

struct material_common
{
	std::array<std::string, 6> state_name;

	using reader_type = StructureReader<material_common, "material_common",
		Field<&material_common::state_name, "state_name">
	>;
};

struct material: material_common
{
	std::string prefix;

	std::string get_name(matter_state_t state) const {
		const auto &sname = state_name[static_cast<int>(state)];
		if (prefix.empty())
			return sname;
		else
			return prefix + " " + sname;
	}

	using reader_type = StructureReader<material, "material",
		Base<material_common>,
		Field<&material::prefix, "prefix">
	>;
};

struct material_template: material_common
{
	using reader_type = StructureReader<material_template, "material_template",
		Base<material_common>
	>;
};

struct inorganic_raw
{
	std::string id;
	::material material;

	using reader_type = StructureReader<inorganic_raw, "inorganic_raw",
		Field<&inorganic_raw::id, "id">,
		Field<&inorganic_raw::material, "material">
	>;
};

struct plant_raw
{
	std::string id;
	std::string name;
	std::string name_plural;
	std::string adj;
	std::vector<std::unique_ptr<::material>> material;

	using reader_type = StructureReader<plant_raw, "plant_raw",
	      Field<&plant_raw::id, "id">,
	      Field<&plant_raw::name, "name">,
	      Field<&plant_raw::name_plural, "name_plural">,
	      Field<&plant_raw::adj, "adj">,
	      Field<&plant_raw::material, "material">
	>;
};

template <typename Enum>
class FlagArray: public std::vector<bool>
{
public:
	bool isSet(Enum n) const {
		return n < size() && operator[](n);
	}
};

struct caste_raw
{
	std::string caste_id;
	std::array<std::string, 3> caste_name;
	FlagArray<caste_raw_flags_t> flags;

	using reader_type = StructureReader<caste_raw, "caste_raw",
	      Field<&caste_raw::caste_id, "caste_id">,
	      Field<&caste_raw::caste_name, "caste_name">,
	      Field<&caste_raw::flags, "flags">
	>;
};

struct creature_raw
{
	std::string creature_id;
	std::array<std::string, 3> name;
	std::vector<std::unique_ptr<caste_raw>> caste;
	std::vector<std::unique_ptr<::material>> material;

	using reader_type = StructureReader<creature_raw, "creature_raw",
		Field<&creature_raw::creature_id, "creature_id">,
		Field<&creature_raw::name, "name">,
		Field<&creature_raw::caste, "caste">,
		Field<&creature_raw::material, "material">
	>;
};


struct unit_preference
{
	enum preference_type: int16_t {
		LikeMaterial,
		LikeCreature,
		LikeFood,
		HateCreature,
		LikeItem,
		LikePlant,
		LikeTree,
		LikeColor,
		LikeShape,
		LikePoeticForm,
		LikeMusicalForm,
		LikeDanceForm,
	} type;
	union target_t {
		item_type_t item_type;
		int16_t creature_id;
		int16_t color_id;
		int16_t shape_id;
		int16_t plant_id;
		int32_t poetic_form_id;
		int32_t musical_form_id;
		int32_t dance_form_id;

		using reader_type = UnionReader<unit_preference::target_t, "unit_preference.(item_type)",
		      Field<&unit_preference::target_t::item_type, "item_type">,
		      Field<&unit_preference::target_t::creature_id, "creature_id">,
		      Field<&unit_preference::target_t::color_id, "color_id">,
		      Field<&unit_preference::target_t::shape_id, "shape_id">,
		      Field<&unit_preference::target_t::plant_id, "plant_id">,
		      Field<&unit_preference::target_t::poetic_form_id, "poetic_form_id">,
		      Field<&unit_preference::target_t::musical_form_id, "musical_form_id">,
		      Field<&unit_preference::target_t::dance_form_id, "dance_form_id">
		>;
	} target;
	int32_t item_subtype;
	int32_t mattype;
	int32_t matindex;
	matter_state_t mat_state;
	std::size_t get_target_type() const {
		switch (type) {
			case LikeMaterial: return -1;
			case LikeCreature: return 1;
			case LikeFood: return 0;
			case HateCreature: return 1;
			case LikeItem: return 0;
			case LikePlant: return 4;
			case LikeTree: return 4;
			case LikeColor: return 2;
			case LikeShape: return 3;
			case LikePoeticForm: return 5;
			case LikeMusicalForm: return 6;
			case LikeDanceForm: return 7;
			default: return -1;
		};
	}

	using reader_type = StructureReaderSeq<unit_preference, "unit_preference",
	      Field<&unit_preference::type, "type">,
	      Field<&unit_preference::target, "(item_type)", &unit_preference::get_target_type>,
	      Field<&unit_preference::item_subtype, "item_subtype">,
	      Field<&unit_preference::mattype, "mattype">,
	      Field<&unit_preference::matindex, "matindex">,
	      Field<&unit_preference::mat_state, "mat_state">
	>;
};

struct unit_soul
{
	std::vector<std::unique_ptr<unit_preference>> preferences;

	using reader_type = StructureReader<unit_soul, "unit_soul",
		Field<&unit_soul::preferences, "preferences">
	>;
};

struct unit_inventory_item
{
	std::unique_ptr<item> item_;

	using reader_type = StructureReader<unit_inventory_item, "unit_inventory_item",
	      Field<&unit_inventory_item::item_, "item">
	>;
};

struct unit
{
	language_name name;
	int32_t race;
	int16_t caste;
	unit_flags1 flags1;
	unit_flags2 flags2;
	unit_flags3 flags3;
	unit_flags4 flags4;
	int32_t id;
	int32_t civ_id;
	mood_type_t mood;
	std::vector<std::unique_ptr<unit_inventory_item>> inventory;
	struct curse_t {
		cie_add_tag_mask1 add_tags1, rem_tags1;
		using reader_type = StructureReader<curse_t, "unit.curse",
		      Field<&curse_t::add_tags1, "add_tags1">,
		      Field<&curse_t::rem_tags1, "rem_tags1">
		>;
	} curse;
	uintptr_t undead;
	std::unique_ptr<unit_soul> current_soul;
	std::array<bool, 94> labors;

	using reader_type = StructureReader<unit, "unit",
		Field<&unit::name, "name">,
		Field<&unit::race, "race">,
		Field<&unit::caste, "caste">,
		Field<&unit::flags1, "flags1">,
		Field<&unit::flags2, "flags2">,
		Field<&unit::flags3, "flags3">,
		Field<&unit::flags4, "flags4">,
		Field<&unit::id, "id">,
		Field<&unit::civ_id, "civ_id">,
		Field<&unit::mood, "mood">,
		Field<&unit::inventory, "inventory">,
		Field<&unit::curse, "curse">,
		Field<&unit::undead, "enemy.undead">,
		Field<&unit::current_soul, "status.current_soul">,
		Field<&unit::labors, "status.labors">
	>;
};

struct historical_figure
{
	int race, caste;
	language_name name;
	int id;

	using reader_type = StructureReader<historical_figure, "historical_figure",
		Field<&historical_figure::race, "race">,
		Field<&historical_figure::caste, "caste">,
		Field<&historical_figure::name, "name">,
		Field<&historical_figure::id, "id">
	>;
};

struct world_raws
{
	std::vector<std::unique_ptr<material_template>> material_templates;
	std::vector<std::shared_ptr<inorganic_raw>> inorganics;
	struct plants_t
	{
		std::vector<std::shared_ptr<plant_raw>> all;
		std::vector<std::shared_ptr<plant_raw>> bushes;
		std::vector<int> bushes_idx;
		std::vector<std::shared_ptr<plant_raw>> trees;
		std::vector<int> trees_idx;
		std::vector<std::shared_ptr<plant_raw>> grasses;
		std::vector<int> grasses_idx;

		using reader_type = StructureReader<plants_t, "world_raws.plants",
			Field<&plants_t::all, "all">,
			Field<&plants_t::bushes, "bushes">,
			Field<&plants_t::bushes_idx, "bushes_idx">,
			Field<&plants_t::trees, "trees">,
			Field<&plants_t::trees_idx, "trees_idx">,
			Field<&plants_t::grasses, "grasses">,
			Field<&plants_t::grasses_idx, "grasses_idx">
		>;
	} plants;
	struct creature_handler
	{
		std::vector<std::shared_ptr<creature_raw>> alphabetic;
		std::vector<std::shared_ptr<creature_raw>> all;

		using reader_type = StructureReader<creature_handler, "creature_handler",
			Field<&creature_handler::alphabetic, "alphabetic">,
			Field<&creature_handler::all, "all">
		>;
	} creatures;
	struct itemdefs_t
	{
		std::vector<std::shared_ptr<itemdef>> all;
		std::vector<std::shared_ptr<itemdef_weaponst>> weapons;
		std::vector<std::shared_ptr<itemdef_trapcompst>> trapcomps;
		std::vector<std::shared_ptr<itemdef_toyst>> toys;
		std::vector<std::shared_ptr<itemdef_toolst>> tools;
		std::array<std::vector<std::shared_ptr<itemdef_toolst>>, 26> tools_by_type;
		std::vector<std::shared_ptr<itemdef_instrumentst>> instruments;
		std::vector<std::shared_ptr<itemdef_armorst>> armor;
		std::vector<std::shared_ptr<itemdef_ammost>> ammo;
		std::vector<std::shared_ptr<itemdef_siegeammost>> siege_ammo;
		std::vector<std::shared_ptr<itemdef_glovesst>> gloves;
		std::vector<std::shared_ptr<itemdef_shoesst>> shoes;
		std::vector<std::shared_ptr<itemdef_shieldst>> shields;
		std::vector<std::shared_ptr<itemdef_helmst>> helms;
		std::vector<std::shared_ptr<itemdef_pantsst>> pants;
		std::vector<std::shared_ptr<itemdef_foodst>> food;

		using reader_type = StructureReader<world_raws::itemdefs_t, "world_raws.itemdefs",
			Field<&world_raws::itemdefs_t::all, "all">,
			Field<&world_raws::itemdefs_t::weapons, "weapons">,
			Field<&world_raws::itemdefs_t::trapcomps, "trapcomps">,
			Field<&world_raws::itemdefs_t::toys, "toys">,
			Field<&world_raws::itemdefs_t::tools, "tools">,
			Field<&world_raws::itemdefs_t::tools_by_type, "tools_by_type">,
			Field<&world_raws::itemdefs_t::instruments, "instruments">,
			Field<&world_raws::itemdefs_t::armor, "armor">,
			Field<&world_raws::itemdefs_t::ammo, "ammo">,
			Field<&world_raws::itemdefs_t::siege_ammo, "siege_ammo">,
			Field<&world_raws::itemdefs_t::gloves, "gloves">,
			Field<&world_raws::itemdefs_t::shoes, "shoes">,
			Field<&world_raws::itemdefs_t::shields, "shields">,
			Field<&world_raws::itemdefs_t::helms, "helms">,
			Field<&world_raws::itemdefs_t::pants, "pants">,
			Field<&world_raws::itemdefs_t::food, "food">
		>;
	} itemdefs;
	struct language_t
	{
		std::vector<std::unique_ptr<language_word>> words;
		std::vector<std::unique_ptr<language_translation>> translations;

		using reader_type = StructureReader<world_raws::language_t, "world_raws.language",
			Field<&world_raws::language_t::words, "words">,
			Field<&world_raws::language_t::translations, "translations">
		>;
	} language;
	struct special_mat_table
	{
		std::array<std::unique_ptr<material>, 659> builtin;
		using reader_type = StructureReader<special_mat_table, "special_mat_table",
			Field<&special_mat_table::builtin, "builtin">
		>;
	} mat_table;

	using reader_type = StructureReader<world_raws, "world_raws",
		Field<&world_raws::material_templates, "material_templates">,
		Field<&world_raws::inorganics, "inorganics">,
		Field<&world_raws::plants, "plants">,
		Field<&world_raws::creatures, "creatures">,
		Field<&world_raws::itemdefs, "itemdefs">,
		Field<&world_raws::language, "language">,
		Field<&world_raws::mat_table, "mat_table">
	>;
};

struct world
{
	std::vector<std::unique_ptr<unit>> active_units;
	std::vector<std::unique_ptr<historical_figure>> historical_figures;
	std::vector<std::unique_ptr<poetic_form>> poetic_forms;
	world_raws raws;

	using reader_type = StructureReader<world, "world",
		Field<&world::active_units, "units.active">,
		Field<&world::historical_figures, "history.figures">,
		Field<&world::poetic_forms, "poetic_forms.all">,
		Field<&world::raws, "raws">
	>;
};

class ProcessStats: public ProcessWrapper
{
	std::size_t read_count;
	std::size_t bytes_count;
	std::chrono::steady_clock::duration total_duration;
	std::string name;
	std::ofstream output;
public:
	ProcessStats(std::string_view name, std::unique_ptr<Process> &&p):
		ProcessWrapper(std::move(p)),
		name(name),
		output(std::format("stats_{}.dat", name))
	{
	}

	~ProcessStats() override
	{
	}

	std::error_code stop() override
	{
		read_count = 0;
		bytes_count = 0;
		total_duration = decltype(total_duration)::zero();
		return ProcessWrapper::stop();
	}

	std::error_code cont() override
	{
		std::cerr << std::format("Stats for {}\n", name);
		std::cerr << std::format("read count: {}\n", read_count);
		std::cerr << std::format("bytes read: {}\n", bytes_count);
		std::cerr << std::format("duration: {}ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(total_duration).count());
		std::cerr << std::format("bandwidth: {}MB/s\n", bytes_count/std::chrono::duration<double>(total_duration).count()/1024.0/1024.0);
		return ProcessWrapper::cont();
	}

	[[nodiscard]] cppcoro::task<std::error_code> read(MemoryBufferRef buffer) override
	{
		auto start = std::chrono::steady_clock::now();
		auto res = co_await process().read(buffer);
		auto end = std::chrono::steady_clock::now();
		auto duration = end - start;
		output << buffer.data.size() << "\t"
			<< std::chrono::duration<double, std::micro>(duration).count() << "\t"
			<< buffer.data.size()/std::chrono::duration<double>(duration).count()/1024.0/1024.0 << "\n";
		++read_count;
		++bytes_count += buffer.data.size();
		total_duration += duration;
		co_return res;
	}

	[[nodiscard]] cppcoro::task<std::error_code> readv(std::span<const MemoryBufferRef> tasks) override
	{
		auto start = std::chrono::steady_clock::now();
		auto res = co_await process().readv(tasks);
		auto end = std::chrono::steady_clock::now();
		auto duration = end - start;
		std::size_t len = 0;
		for (const auto &t: tasks)
			len += t.data.size();
		output << len << "\t"
			<< std::chrono::duration<double, std::micro>(duration).count() << "\t"
			<< len/std::chrono::duration<double>(duration).count()/1024.0/1024.0 << "\n";
		++read_count;
		++bytes_count += len;
		total_duration += duration;
		co_return res;
	}
};

extern "C" {
#include <getopt.h>
}

static constexpr const char *usage = "{} [options...] df_structures pid\n"
	"df_structures must be a path to a directory containing df-structures xml.\n"
	"Options are:\n"
	" -t, --type type   Process type (native or wine)\n"
	" -c, --cache       Use cache\n"
	" -v, --vectorize   Use vectorizer\n"
	" -h, --help        Print this help message\n";

int main(int argc, char *argv[]) try
{
	using namespace std::literals::string_view_literals;
	namespace fs = std::filesystem;

	static option options[] = {
		{"type", required_argument, nullptr, 't'},
		{"cache", no_argument, nullptr, 'c'},
		{"vectorize", no_argument, nullptr, 'v'},
		{"help", no_argument, nullptr, 'h'},
		{nullptr, 0, nullptr, 0}
	};
	std::string process_type = "native";
	bool use_cache = false;
	bool use_vectorizer = false;
	{
		int opt;
		while ((opt = getopt_long(argc, argv, ":t:cv", options, nullptr)) != -1) {
			switch (opt) {
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
	if (argc - optind != 2) {
		std::cerr << "This command must have exactly two parameters\n";
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
	{
		auto tmp = std::move(process);
		process = std::make_unique<ProcessStats>("actual", std::move(tmp));
	}

	if (use_vectorizer) {
		auto tmp = std::move(process);
		process = std::make_unique<ProcessVectorizer>(std::move(tmp), 48*1024*1024);
	}
	if (use_cache) {
		auto tmp = std::move(process);
		process =  std::make_unique<ProcessCache>(std::move(tmp));
	}

	auto version = structures.versionById(process->id());
	if (!version) {
		std::cerr << std::format("Version not found\n");
		for (const auto &version: structures.allVersions()) {
			std::cerr << std::format("{}:", version.version_name);
			for (auto byte: version.id)
				std::cerr << std::format(" {:02x}", byte);
			std::cerr << std::format("\n");
		}
		std::cerr << std::format("Current process:");
		for (auto byte: process->id())
			std::cerr << std::format(" {:02x}", byte);
		std::cerr << std::format("\n");
		return EXIT_FAILURE;
	}
	std::cerr << std::format("Found version {}\n", version->version_name);

	ReaderFactory reader(structures, *version);
	world w;
	int fortress_civ_id;
	{
		using namespace literals;
		auto start = std::chrono::steady_clock::now();
		ReadSession session(reader, *process);
		if (!session.sync(
				session.read("world"_path, w),
				session.read("plotinfo.civ_id"_path, fortress_civ_id))) {
			std::cerr << std::format("Reading failed\n");
			return -1;
		}
		auto end = std::chrono::steady_clock::now();
		std::cout << "Data read in " << std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count() << "ms" << std::endl;
	}
	auto is_crazed = [&](const unit &u) {
		return !u.flags3.bits.scuttle &&
			!u.curse.rem_tags1.bits.CRAZED && (
				u.curse.add_tags1.bits.CRAZED ||
				w.raws.creatures.all.at(u.race)
					->caste.at(u.caste)
					->flags.isSet(caste_raw_flags::CRAZED)
				);
	};
	auto is_opposed_to_life = [&](const unit &u) {
		return !u.curse.rem_tags1.bits.OPPOSED_TO_LIFE && (
				u.curse.add_tags1.bits.OPPOSED_TO_LIFE ||
				w.raws.creatures.all.at(u.race)
					->caste.at(u.caste)
					->flags.isSet(caste_raw_flags::OPPOSED_TO_LIFE)
				);
	};
	auto is_fort_controlled = [&](const unit &u) {
		if (u.mood == mood_type::Berserk ||
				is_crazed(u) ||
				is_opposed_to_life(u) ||
				u.undead ||
				u.flags3.bits.ghostly)
			return false;
		if (u.flags1.bits.marauder ||
				u.flags1.bits.invader_origin ||
				u.flags1.bits.active_invader ||
				u.flags1.bits.forest ||
				u.flags1.bits.merchant ||
				u.flags1.bits.diplomat)
			return false;
		if (u.flags1.bits.tame)
			return true;
		if (u.flags2.bits.visitor ||
				u.flags2.bits.visitor_uninvited ||
				u.flags2.bits.underworld ||
				u.flags2.bits.resident ||
				u.flags4.bits.agitated_wilderness_creature)
			return false;
		return u.civ_id != -1 && u.civ_id == fortress_civ_id;
	};

	struct {
		const world_raws::language_t &language;

		std::string_view english_word(const language_name &name, int index) const {
			assert(index >= 0 && index < 7);
			assert(name.words[index] >= 0 && unsigned(name.words[index]) < language.words.size());
			assert(name.parts_of_speech[index] >= 0 && name.parts_of_speech[index] < 9);
			return language.words[name.words[index]]->forms[name.parts_of_speech[index]];
		}
		std::string_view local_word(const language_name &name, int index) const {
			assert(index >= 0 && index < 7);
			assert(name.language >= 0 && unsigned(name.language) < language.translations.size());
			assert(name.words[index] >= 0 && unsigned(name.words[index]) < language.translations[name.language]->words.size());
			return *language.translations[name.language]->words[name.words[index]];
		}
		std::string english_name(const language_name &name) const {
			std::string str;
			if (name.words[0] != -1)
				str += english_word(name, 0);
			if (name.words[1] != -1)
				str += english_word(name, 1);
			bool word_added = false;
			for (int i = 2; i < 6; ++i) {
				if (name.words[i] != -1) {
					if (!word_added) {
						str += " the ";
						word_added = true;
					}
					else if (i == 5 && name.words[4] != -1)
						str += "-";
					else
						str += " ";
					str += english_word(name, i);
				}
			}
			if (name.words[6] != -1) {
				str += " of ";
				str += english_word(name, 6);
			}
			return str;
		}
		std::string local_name(const language_name &name) const {
			std::string str;
			if (name.words[0] != -1)
				str += local_word(name, 0);
			if (name.words[1] != -1)
				str += local_word(name, 1);
			bool word_added = false;
			for (int i = 2; i < 6; ++i) {
				if (name.words[i] != -1) {
					if (!word_added) {
						str += " ";
						word_added = true;
					}
					str += local_word(name, i);
				}
			}
			if (name.words[6] != -1) {
				str += " ";
				str += local_word(name, 6);
			}
			return str;
		}
	} make_name{w.raws.language};

	auto item_name = [&itemdefs = w.raws.itemdefs] (item_type_t type, int subtype) -> std::string_view {
		auto subtype_name = [&](const auto &vector) -> std::string_view {
			auto it = std::ranges::find_if(vector, [subtype](auto ptr){return ptr && ptr->subtype == subtype;});
			if (it != vector.end())
				return (*it)->describe();
			else
				return "unknown itemdef";
		};
		switch (type) {
		case item_type::INSTRUMENT: return subtype_name(itemdefs.instruments);
		case item_type::TOY: return subtype_name(itemdefs.toys);
		case item_type::WEAPON: return  subtype_name(itemdefs.weapons);
		case item_type::ARMOR: return subtype_name(itemdefs.armor);
		case item_type::SHOES: return subtype_name(itemdefs.shoes);
		case item_type::SHIELD: return subtype_name(itemdefs.shields);
		case item_type::HELM: return subtype_name(itemdefs.helms);
		case item_type::GLOVES: return subtype_name(itemdefs.gloves);
		case item_type::AMMO: return subtype_name(itemdefs.ammo);
		case item_type::PANTS: return subtype_name(itemdefs.pants);
		case item_type::SIEGEAMMO: return subtype_name(itemdefs.siege_ammo);
		case item_type::TRAPCOMP: return subtype_name(itemdefs.trapcomps);
		case item_type::FOOD: return subtype_name(itemdefs.food);
		case item_type::TOOL: return subtype_name(itemdefs.tools);
		default: return caption(type);
		}
	};
	auto find_material = [&](int index, int type) -> const material & {
		if (index < 0)
			return *w.raws.mat_table.builtin[type];
		else if (type == 0)
			return w.raws.inorganics[index]->material;
		else if (type < 19)
			return *w.raws.mat_table.builtin[type];
		else if (type < 219)
			return *w.raws.creatures.all[index]->material[type-19];
		else if (type < 419) {
			auto histfig = std::ranges::lower_bound(w.historical_figures, index, std::less<int>{}, [](const auto &ptr){return ptr->id;});
			if (histfig == w.historical_figures.end())
				throw std::invalid_argument("invalid historical figure id");
			return *w.raws.creatures.all[(*histfig)->race]->material[type-219];
		}
		else if (type < 619)
			return *w.raws.plants.all[index]->material[type-419];
		throw std::invalid_argument("invalid material");
	};

	for (const auto &u: w.active_units) {
		if (!is_fort_controlled(*u))
			continue;
		std::cout << u->id << " - " << u->name.first_name;
		if (!u->name.nickname.empty())
			std::cout << " \"" << u->name.nickname << "\"";
		std::cout << " " << make_name.local_name(u->name);
		std::cout << " (" << make_name.english_name(u->name) << ")";
		std::cout << std::endl;
		//std::cout << "  Labors:" << std::endl;
		//for (int i = 0; i < u->labors.size(); ++i)
		//	if (u->labors[i])
		//		std::cout << "   - " << to_string(unit_labor_t(i)) << std::endl;
		if (u->current_soul) {
			std::cout << "  Preferences:" << std::endl;
			for (const auto &pref: u->current_soul->preferences) {
				switch (pref->type) {
				case unit_preference::LikeMaterial:
					std::cout << "   - Material: " << find_material(pref->matindex, pref->mattype).get_name(pref->mat_state) << std::endl;
					break;
				case unit_preference::LikeCreature:
					std::cout << "   - Creature: " << w.raws.creatures.all[pref->target.creature_id]->name[0] << std::endl;
					break;
				case unit_preference::LikeFood:
					std::cout << "   - Food: " << item_name(pref->target.item_type, pref->item_subtype) << std::endl;
					break;
				case unit_preference::HateCreature:
					std::cout << "   - Hate: " << w.raws.creatures.all[pref->target.creature_id]->name[0] << std::endl;
					break;
				case unit_preference::LikeItem:
					std::cout << "   - Item: " << item_name(pref->target.item_type, pref->item_subtype) << std::endl;
					break;
				case unit_preference::LikePlant:
					std::cout << "   - Plant: " << w.raws.plants.all[pref->target.plant_id]->name_plural << std::endl;
					break;
				case unit_preference::LikeTree:
					std::cout << "   - Tree: " << std::endl;
					break;
				case unit_preference::LikeColor:
					std::cout << "   - Color: " << std::endl;
					break;
				case unit_preference::LikeShape:
					std::cout << "   - Shape: " << std::endl;
					break;
				case unit_preference::LikePoeticForm:
					std::cout << "   - Poetic form: " << std::endl;
					break;
				case unit_preference::LikeMusicalForm:
					std::cout << "   - Musical form: " << std::endl;
					break;
				case unit_preference::LikeDanceForm:
					std::cout << "   - Dance form: " << std::endl;
					break;
				}
			}
		}
		std::cout << "  Inventory:" << std::endl;
		for (const auto &inventory_item: u->inventory) {
			assert(inventory_item);
			if (inventory_item->item_)
				std::cout << "   - " << inventory_item->item_->describe() << std::endl;
			else
				std::cout << "   - unknown" << std::endl;

		}
	}

	//std::cout << "Itemdefs:" << std::endl;
	//for (const auto &id: w.raws.itemdefs.all) {
	//	std::cout << " - " << id->id << " / " << id->subtype << " / " << id->describe() << " (" << typeid(*id).name() << ")" << std::endl;
	//}

	//std::cout << "Poetic forms:" << std::endl;
	//for (const auto &form: w.poetic_forms) {
	//	std::cout << " - " << make_name.english_name(form->name) << std::endl;
	//	for (const auto &part: form->parts) {
	//		std::cout << "   - " << part->line_subject.size() << ", " << part->line_subject_target.size() << std::endl;
	//		for (std::size_t i = 0; i < part->line_subject.size(); ++i)
	//			std::cout << "     - " << to_string(part->line_subject[i]) << ", " << part->line_subject_target[i].index() << std::endl;
	//	}
	//}
}
catch (std::exception &e) {
	std::cerr << "Could not load structures: " << e.what() << std::endl;
	return -1;
}
