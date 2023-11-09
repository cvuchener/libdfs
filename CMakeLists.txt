cmake_minimum_required(VERSION 3.5)
project(dfs)

option(BUILD_SHARED_LIBS "Build dfs as a shared library" OFF)
option(BUILD_TESTS_AND_EXAMPLES "Build tests and examples" OFF)

find_package(pugixml REQUIRED)
find_package(cppcoro REQUIRED)
if(TARGET cppcoro)
	add_library(cppcoro::cppcoro ALIAS cppcoro)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	find_package(OpenSSL REQUIRED)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	if (MSVC AND BUILD_TESTS_AND_EXAMPLES)
		find_package(unofficial-getopt-win32 REQUIRED)
	endif()
endif()

add_subdirectory(dfs)
add_subdirectory(codegen)
add_subdirectory(doc)

if(BUILD_TESTS_AND_EXAMPLES)
add_subdirectory(tests_and_examples)
endif()

install(EXPORT dfs_targets
	FILE dfs-targets.cmake
	NAMESPACE dfs::
	DESTINATION lib/cmake/dfs
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/dfs-config.cmake.in
	${CMAKE_CURRENT_BINARY_DIR}/cmake/dfs-config.cmake
	INSTALL_DESTINATION lib/cmake/dfs
	NO_SET_AND_CHECK_MACRO
	NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
install(FILES
	${CMAKE_CURRENT_BINARY_DIR}/cmake/dfs-config.cmake
	DESTINATION lib/cmake/dfs
)

