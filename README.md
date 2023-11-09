DFS
===

DFS is a library for parsing [df-structures](https://github.com/DFHack/df-structures) XML into C++ objects, computing size of types and offsets of structure members, access and parse raw memory from Dwarf Fortress processes.

Building
--------

Building DFS requires a modern C++20 compiler, CMake and the following dependencies:

 - pugixml
 - cppcoro
 - OpenSSL (linux-only)

CMake options:

 - `BUILD_SHARED_LIBS` (default `OFF`): build as a shared library instead of a static library.
 - `BUILD_TESTS_AND_EXAMPLES` (default `OFF`): build programs from the `tests_and_examples` directory.

Usage
-----

Use cmake to link to the library:

```cmake
find_package(dfs REQUIRED)

# ...

target_link_libraries(my_target dfs::dfs)
```

This package also provides a [dfs-codegen](doc/codegen.md) program to generate enums and bitfields from df-structures.

Licenses
--------

dfs library is distributed under LGPLv3.

`dfs-codegen` and examples are distributed under GPLv3.

