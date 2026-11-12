# The launcher starts the newest game folder and installs signed updates, see doc/launcher.md.
# blobby-update holds the parts without network and window, which the tests use too.
set(CMAKE_CXX_STANDARD 11)

# AUTO builds the launcher when its libraries are found, ON fails without them and OFF leaves it out.
# macOS keeps its app bundle without a launcher, so the launcher is never built there.
set(BLOBBY_LAUNCHER AUTO CACHE STRING "Build the launcher that installs updates: AUTO, ON or OFF")

if (SWITCH OR ANDROID)
	return()
endif ()

find_package(nlohmann_json 3.10 CONFIG QUIET)

if (NOT nlohmann_json_FOUND)
	if (BLOBBY_LAUNCHER STREQUAL "ON")
		message(FATAL_ERROR "BLOBBY_LAUNCHER is ON, but nlohmann_json was not found")
	endif ()
	message(STATUS "nlohmann_json not found, the launcher and its tests are left out")
	return()
endif ()

add_library(blobby-update STATIC
	UpdateManifest.cpp UpdateManifest.h
	../src/Sha256.cpp ../src/Sha256.h
	)
target_include_directories(blobby-update PUBLIC . ../src)
target_link_libraries(blobby-update PUBLIC monocypher nlohmann_json::nlohmann_json)
