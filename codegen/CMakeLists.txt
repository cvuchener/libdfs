cmake_minimum_required(VERSION 3.5)
project(dfs)

add_executable(dfs-codegen codegen.cpp)
target_link_libraries(dfs-codegen dfs)

install(TARGETS dfs-codegen
	EXPORT dfs_targets
	RUNTIME
		DESTINATION bin
		COMPONENT Development
)
add_executable(dfs::dfs-codegen ALIAS dfs-codegen)
include(../cmake/dfs-functions.cmake)
