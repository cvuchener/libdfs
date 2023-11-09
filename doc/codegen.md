# Codegen {#codegen}

## Command usage

```
dfs-codegen <df-structures-path> <output-prefix> [<general-options>...] <type> [<type-options> ...] ...
General options:
  --namespace <name>  add namespace around type declarations
Type options:
  --as <name>         use this name instead of df-structures name (mandatory for member types)
```

`dfs-codegen` will use data from `<df-structures-path>` to generate enums and bitfields in `<output-prefix>.h` and `<output-prefix>.cpp`.

If `--namespace` option is used, all types will be included in the given namespace.

`<type>` must be the name of a enum or a bitfield or the "path" to a nested one (e.g. `compound_name.subcompound.nest_enum`). Type may be renamed using `--as`, this option is mandatory for nested types.

## Enum

```c++
namespace enum_name {

// Enum values
enum enum_name {
    // ...
};

inline constexpr std::underlying_type_t<enum_name> Count = /*...*/;

// String conversions
std::optional<enum_name> from_string(std::string_view)
std::string_view to_string(enum_name)

// Attributes
attribute_type attribute_name(enum_name);
//...

} // namespace enum_name

using enum_name_t = enum_name::enum_name;
```

Enums will be declared inside a namespace with the same name as the enum (itself inside the namespace from the `--namespace` option). An alias with the suffix `_t` is also created outside the namespace for convenience.

Inside the enum namespace are also provided:
 - a constant `Count` is also declared whose value is the greatest enum value plus one,
 - string conversions functions `from_string` and `to_string`,
 - attributes accessors named after the attribute.

If an attribute type is another enum it will need be declared using its default name.

## Bitfield

```c++
union bitfield_name {
    using underlying_type = /*integer type*/;
    bitfield_name() noexcept;
    explicit bitfield_name(underlying_type) noexcept;
    bitfield_name &operator=(underlying_type) noexcept;
    explicit operator underlying_type() const noexcept;

    underlying_type value;
    struct {
        // flags...
    } bits;

    enum bits_t {
        // <flagname>_bits
        // ...
    };
    enum pos_t {
        // <flagname>_pos
        // ...
    };
    enum count_t {
        // <flagname>_count
        // ...
    };
};
```

Bitfields can be constructed from, assigned, or converted to the underlying integral type. This allows these types to be compatible with the `ItemReader` for integral types.

Individual flags can be accessed trough the `bits` nested structure. For each flag several enum values are also provided: `bits_t` is a bit mask, `pos_t` the position of the first bit of the flag, `count_t` the bit count.

## CMake

A CMake function is provided for generating source files using `dfs-codegen`.

```cmake
generate_df_types(TARGET <target>
                  STRUCTURES <df-structures-path>
                  OUTPUT <output-prefix>
                  [NAMESPACE <namespace>]
                  TYPES [types...])
```

Arguments are the same as the command. Type options (e.g. `--as`) can be included in the `types...` list. The generated `.h` and `.cpp` files will be added to `<target>` sources.

### Example:

```cmake
add_executable(mytarget
    main.cpp
)
generate_df_types(TARGET mytarget
	STRUCTURES ${CMAKE_CURRENT_SOURCE_DIR}/structures
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/DFEnums
	NAMESPACE df
	TYPES
		language_name_component
		unit_labor_category
		unit_labor
		work_detail.work_detail_flags --as work_detail_flags
        work_detail_mode
)
target_include_directories(mytarget PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}
)
```
