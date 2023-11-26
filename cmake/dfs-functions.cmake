function(dfs_generate_df_types)
	cmake_parse_arguments(ARG "" "TARGET;STRUCTURES;OUTPUT;NAMESPACE" "TYPES" ${ARGN})
	if(NOT DEFINED ARG_TARGET)
		message(FATAL_ERROR "Missing TARGET")
	endif()
	if(NOT DEFINED ARG_STRUCTURES)
		message(FATAL_ERROR "Missing STRUCTURES")
	endif()
	if(NOT DEFINED ARG_OUTPUT)
		message(FATAL_ERROR "Missing OUTPUT")
	endif()
	if (DEFINED ARG_NAMESPACE)
		set(USE_NAMESPACE --namespace ${ARG_NAMESPACE})
	endif()
	set(GENERATED_FILES ${ARG_OUTPUT}.h ${ARG_OUTPUT}.cpp)
	add_custom_command(OUTPUT ${GENERATED_FILES}
		COMMAND dfs::dfs-codegen ARGS
			"${ARG_STRUCTURES}"
			"${ARG_OUTPUT}"
			${USE_NAMESPACE}
			${ARG_TYPES}
		DEPENDS dfs::dfs-codegen)
	target_sources(${ARG_TARGET} PRIVATE ${GENERATED_FILES})
endfunction()
