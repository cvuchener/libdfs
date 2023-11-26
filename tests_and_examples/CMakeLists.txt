cmake_minimum_required(VERSION 3.5)
project(dfs)

add_executable(test-structures test-structures.cpp)
target_link_libraries(test-structures dfs::dfs)

add_executable(structcheck structcheck.cpp)
target_link_libraries(structcheck dfs::dfs)
if(MSVC)
	target_link_libraries(structcheck unofficial::getopt-win32::getopt)
endif()

set(DF_STRUCTURES_PATH CACHE PATH "Path to df-structures xml")

add_executable(reader reader.cpp)
dfs_generate_df_types(TARGET reader
	STRUCTURES ${DF_STRUCTURES_PATH}
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/reader_df_types
	TYPES
		poetic_form_subject
		item_type
		matter_state
		unit_labor_category
		unit_labor
		mood_type
		caste_raw_flags
		unit_flags1
		unit_flags2
		unit_flags3
		unit_flags4
		cie_add_tag_mask1
)
target_include_directories(reader PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(reader dfs::dfs)
if(MSVC)
	target_link_libraries(reader unofficial::getopt-win32::getopt)
endif()
