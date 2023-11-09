# DFS {#mainpage}

DFS is a library for parsing [df-structures](https://github.com/DFHack/df-structures) XML into C++ objects (see `dfs::Structures` and [Types](@ref types)).

Using parsed data and given `dfs::ABI` DFS can also:
 - compute size of types and offsets of compound members using `dfs::MemoryLayout`;
 - access Dwarf Fortress process using one of `dfs::Process` subclass;
 - read structured data using [readers](@ref readers).

`dfs-codegen` tool is also provided to generate C++ code for enums and bitfields (see [Codegen](@ref codegen)).
