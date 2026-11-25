# Seihai project, 2026-11-25: the Graal3D tests are skipped without the GRAAL3D option; see CHANGES-SEIHAI.md.
cmake_minimum_required(VERSION 3.22)

if(TESTS)
	include(utility)

	find_package(Catch2 CONFIG REQUIRED)

	subdir_list(SUBDIRS ${CMAKE_CURRENT_SOURCE_DIR})

	foreach(subdir ${SUBDIRS})
		# Seihai: the Graal 3D tests need the Graal 3D add-on (option GRAAL3D).
		if(subdir STREQUAL "Graal3D" AND NOT GRAAL3D)
			continue()
		endif()
		add_test_og(${subdir} ${CMAKE_CURRENT_SOURCE_DIR})
	endforeach()
endif()
