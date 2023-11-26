cmake_minimum_required(VERSION 3.5)
project(dfs)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set(PLATFORM_SOURCES
		LinuxProcess.cpp
		LinuxProcessCommon.cpp
		WineProcess.cpp
	)
	set(PLATFORM_HEADERS
		LinuxProcess.h
		LinuxProcessCommon.h
		WineProcess.h
	)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set(PLATFORM_SOURCES
		Win32Process.cpp
	)
	set(PLATFORM_HEADERS
		Win32Process.h
	)
endif()

add_library(dfs
	Structures.cpp
	Type.cpp
	Enum.cpp
	Bitfield.cpp
	Compound.cpp
	Container.cpp
	ABI.cpp
	MemoryLayout.cpp
	Process.cpp
	Path.cpp
	Reader.cpp
	${PLATFORM_SOURCES}
)
set(DFS_PUBLIC_HEADER
	ABI.h
	Bitfield.h
	Compound.h
	CompoundReader.h
	Container.h
	Enum.h
	ItemReader.h
	MemoryLayout.h
	overloaded.h
	Path.h
	Pointer.h
	PolymorphicReader.h
	Process.h
	Reader.h
	Structures.h
	Type.h
	${PLATFORM_HEADERS}
)
set_target_properties(dfs PROPERTIES
	PUBLIC_HEADER "${DFS_PUBLIC_HEADER}"
)
target_include_directories(dfs PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
	$<INSTALL_INTERFACE:include>
)
target_compile_features(dfs PUBLIC cxx_std_20)
target_link_libraries(dfs PUBLIC pugixml cppcoro::cppcoro)
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	target_link_libraries(dfs PUBLIC OpenSSL::Crypto)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	if (MSVC)
		target_compile_options(dfs PUBLIC /bigobj)
	else()
		target_compile_options(dfs PUBLIC -Wa,-mbig-obj)
	endif()
endif()
install(TARGETS dfs
	EXPORT dfs_targets
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
	PUBLIC_HEADER DESTINATION include/dfs
)
add_library(dfs::dfs ALIAS dfs)
