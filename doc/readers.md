# Readers {#readers}

Readers class let you read Dwarf Fortress data without have to write a parser for each type.

A `dfs::ReaderFactory` object must be created from `dfs::Structures` and `dfs::ABI`. Then each time data is read, a `dfs::ReadSession` object is created and then destroyed.
```c++
{
    using namespace dfs::literals;
    dfs::ReadSession session(factory, process);
    int gamemode;
    df_raws_t raws;
    bool success = session.sync(
        session.read("gamemode"_path, gamemode),
        session.read("world.raws"_path, raws)
    );
}
```

Any type with a `dfs::ItemReader` specialization may be read.

## Using compound readers

The most simple way to make a structure or union readable is adding a `reader_type` alias to an instance of `dfs::StructureReader`, `dfs::StructureReaderSeq`, `dfs::UnionReader`, or any other type satisfying the `dfs::CompoundReaderConcept` concept.

`dfs::StructureReader`, `dfs::StructureReaderSeq`, `dfs::UnionReader` have the template parameters:
 - The type being read itself
 - The name or [path](@ref path) to the type
 - Any number of `dfs::Base`, `dfs::Field` or `dfs::VTable` types

```c++
struct unit
{
	language_name name;
	profession_t profession;
	int32_t race;
	int16_t caste;
	int32_t id;
	mood_type_t mood;
	uintptr_t undead;
	std::array<bool, 94> labors;
	int hist_figure_id;
	std::vector<std::unique_ptr<occupation>> occupations;
	std::unique_ptr<unit_soul> current_soul;

	using reader_type = dfs::StructureReader<unit, "unit",
		dfs::Field<&unit::name, "name">,
		dfs::Field<&unit::profession, "profession">,
		dfs::Field<&unit::race, "race">,
		dfs::Field<&unit::caste, "caste">,
		dfs::Field<&unit::id, "id">,
		dfs::Field<&unit::mood, "mood">,
		dfs::Field<&unit::undead, "enemy.undead">,
		dfs::Field<&unit::labors, "status.labors">,
		dfs::Field<&unit::hist_figure_id, "hist_figure_id">,
		dfs::Field<&unit::occupations, "occupations">,
		dfs::Field<&unit::current_soul, "status.current_soul">,
	>;
};
```

An alternative to the alias `reader_type` is adding an explicit specialization for `dfs::compound_reader_type`.

```c++
template <>
struct dfs::compound_reader_type<unit> {
	using type = dfs::StructureReader<unit, "unit",
		dfs::Field<&unit::name, "name">,
		dfs::Field<&unit::profession, "profession">,
		dfs::Field<&unit::race, "race">,
		dfs::Field<&unit::caste, "caste">,
		dfs::Field<&unit::id, "id">,
		dfs::Field<&unit::mood, "mood">,
		dfs::Field<&unit::undead, "enemy.undead">,
		dfs::Field<&unit::labors, "status.labors">,
		dfs::Field<&unit::hist_figure_id, "hist_figure_id">,
		dfs::Field<&unit::occupations, "occupations">,
		dfs::Field<&unit::current_soul, "status.current_soul">,
	>;
};
```

### Base

### Field

### VTable

### Unions

## Using polymorphic readers

Each type of the class hierarchy must be readable itself. Derived types must use the `dfs::Base` to specify their base type. Finally `dfs::polymorphic_reader_type` must be specialized for the base type. `dfs::PolymorphicReader` take the base type as its first parameter and other known types after. Any pointer read to one of type from the hierarchy will check the vtable and instantiate the right type.

```c++
struct histfig_entity_link
{
	int entity_id;
	int link_strength;

	virtual ~histfig_entity_link() = default;

	virtual histfig_entity_link_type_t type() const
	{
		return static_cast<histfig_entity_link_type_t>(-1);
	}

	using reader_type = dfs::StructureReader<histfig_entity_link, "histfig_entity_link",
		dfs::Field<&histfig_entity_link::entity_id, "entity_id">,
		dfs::Field<&histfig_entity_link::link_strength, "link_strength">
	>;
};

struct histfig_entity_link_member: histfig_entity_link
{
	histfig_entity_link_type_t type() const override
	{
		return histfig_entity_link_type::MEMBER;
	}

	using reader_type = dfs::StructureReader<histfig_entity_link_member, "histfig_entity_link_memberst",
		dfs::Base<histfig_entity_link>
	>;
};

struct histfig_entity_link_position: histfig_entity_link
{
	int assignment_id;

	histfig_entity_link_type_t type() const override
	{
		return histfig_entity_link_type::POSITION;
	}

	using reader_type = dfs::StructureReader<histfig_entity_link_position, "histfig_entity_link_positionst",
		dfs::Base<histfig_entity_link>,
		dfs::Field<&histfig_entity_link_position::assignment_id, "assignment_id">
	>;
};

template <>
struct dfs::polymorphic_reader_type<histfig_entity_link> {
	using type = dfs::PolymorphicReader<histfig_entity_link,
	      histfig_entity_link_member,
	      histfig_entity_link_position
	>;
};
```

## Item readers

### Included item readers

| C++ type                              | df-structures type                   |
|---------------------------------------|--------------------------------------|
| `std::unique_ptr<T>`                  | compatible `pointer`                 |
| `std::shared_ptr<T>`                  | compatible `pointer`                 |
| `std::array<T, N>`                    | `static-array` of compatible type and same extent |
| `std::string`                         | `stl-string`                         |
| `std::variant<...>`                   | ``is-union='true'`` compound with matching members |
| `std::vector<T>`                      | `stl-vector` of compatible type      |
| any integral, enum or "integral-like" type | any integral primitive type, enum, bitfield or pointer |
| structure, union                      | exact type specified in the `dfs::StructureReader` or `dfs::UnionReader` |
| `std::vector<bool>`                   | `df-flagarray`                       |

"integral-like" type have a `underlying_type` nested alias to an integral type they can be constructed from.

### Adding custom item readers

