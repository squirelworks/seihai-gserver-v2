# Seihai project, 2026-11-26: see CHANGES-SEIHAI.md.
# Applies PATCH to the current directory (a FetchContent source), once: a
# reconfigure finds it already applied and leaves the tree as it is.
execute_process(
	COMMAND git apply --reverse --check "${PATCH}"
	RESULT_VARIABLE _already
	OUTPUT_QUIET ERROR_QUIET
)
if(NOT _already EQUAL 0)
	execute_process(COMMAND git apply "${PATCH}" RESULT_VARIABLE _result)
	if(NOT _result EQUAL 0)
		message(FATAL_ERROR "Could not apply ${PATCH}")
	endif()
endif()
