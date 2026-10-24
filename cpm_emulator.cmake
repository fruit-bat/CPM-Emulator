# The emulator, for building into another program (e.g. on a microcontroller):
#   include(path/to/CPM-Emulator/cpm_emulator.cmake)
#   target_link_libraries(my_app cpm_emulator)
# The program provides host_console.h and host_fs.h on its include path, e.g.
# sources/host/fatfs for files on a FAT file system (see sources/host/std for the desktop ones).
# The emulator reports errors with C++ exceptions.

if (NOT TARGET cpm_emulator)
	add_library(cpm_emulator INTERFACE)

	target_sources(cpm_emulator INTERFACE
		${CMAKE_CURRENT_LIST_DIR}/external/Z80/sources/Z80.c
	)

	target_include_directories(cpm_emulator INTERFACE
		${CMAKE_CURRENT_LIST_DIR}/sources
		${CMAKE_CURRENT_LIST_DIR}/external/Z80/API
		${CMAKE_CURRENT_LIST_DIR}/external/Zeta/API
	)

	# As the desktop build (see CMakeLists.txt)
	target_compile_definitions(cpm_emulator INTERFACE
		Z80_STATIC
		Z80_WITH_EXECUTE
		Z80_WITH_Q
		Z80_WITH_ZILOG_NMOS_LD_A_IR_BUG
	)
endif()
