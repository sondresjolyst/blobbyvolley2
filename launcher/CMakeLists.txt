# The launcher starts the newest game folder and installs signed updates, see doc/launcher.md.
# blobby-update holds the parts without network and window, which the tests use too.
set(CMAKE_CXX_STANDARD 11)

# AUTO builds the launcher when its libraries are found, ON fails without them and OFF leaves it out.
# macOS keeps its app bundle without a launcher, so the launcher is never built there.
set(BLOBBY_LAUNCHER AUTO CACHE STRING "Build the launcher that installs updates: AUTO, ON or OFF")

if (SWITCH OR ANDROID)
	return()
endif ()

if (BLOBBY_LAUNCHER STREQUAL "OFF")
	return()
endif ()

find_package(nlohmann_json 3.10 CONFIG QUIET)
find_package(LibArchive QUIET)

if (NOT nlohmann_json_FOUND OR NOT LibArchive_FOUND)
	if (BLOBBY_LAUNCHER STREQUAL "ON")
		message(FATAL_ERROR "BLOBBY_LAUNCHER is ON, but nlohmann_json or libarchive was not found")
	endif ()
	message(STATUS "nlohmann_json or libarchive not found, the launcher and its tests are left out")
	return()
endif ()

add_library(blobby-update STATIC
	FsUtil.cpp FsUtil.h
	InstallDir.cpp InstallDir.h
	PackageExtract.cpp PackageExtract.h
	Process.cpp Process.h
	UpdateManifest.cpp UpdateManifest.h
	Updater.cpp Updater.h
	Utf8.h
	../src/LauncherLink.cpp ../src/LauncherLink.h
	../src/Sha256.cpp ../src/Sha256.h
	)
target_include_directories(blobby-update PUBLIC . ../src ${LibArchive_INCLUDE_DIRS})
target_link_libraries(blobby-update PUBLIC monocypher nlohmann_json::nlohmann_json ${LibArchive_LIBRARIES})
if (NOT MSVC)
	target_compile_options(blobby-update PRIVATE -Wall)
endif ()

# The program itself needs libcurl, SDL2 for the progress window and PhysFS for the settings folder.
if (APPLE)
	message(STATUS "macOS keeps its app bundle, so the launcher program is not built")
	return()
endif ()
find_package(CURL QUIET)
if (NOT CURL_FOUND)
	if (BLOBBY_LAUNCHER STREQUAL "ON")
		message(FATAL_ERROR "BLOBBY_LAUNCHER is ON, but libcurl was not found")
	endif ()
	message(STATUS "libcurl not found, the launcher program is left out")
	return()
endif ()

set(blobby-launcher_SRC
	CurlFetcher.cpp CurlFetcher.h
	LauncherMain.cpp
	ProgressWindow.cpp ProgressWindow.h
	)
if (WIN32)
	# the same icon and file description as the game, since players start the launcher as the game
	add_executable(blobby-launcher WIN32 ${blobby-launcher_SRC} ../win/res.rc)
	target_compile_definitions(blobby-launcher PRIVATE $<$<COMPILE_LANGUAGE:RC>:BLOBBY_RC_TARGET=1>)
else ()
	add_executable(blobby-launcher ${blobby-launcher_SRC})
endif ()
target_link_libraries(blobby-launcher blobby-update PhysFS::PhysFS SDL2::SDL2main SDL2::SDL2)
if (TARGET CURL::libcurl)
	target_link_libraries(blobby-launcher CURL::libcurl)
else ()
	target_include_directories(blobby-launcher PRIVATE ${CURL_INCLUDE_DIRS})
	target_link_libraries(blobby-launcher ${CURL_LIBRARIES})
endif ()
if (NOT MSVC)
	target_compile_options(blobby-launcher PRIVATE -Wall)
endif ()
if (MSYS)
	set_target_properties(blobby-launcher PROPERTIES LINK_FLAGS "-mwindows")
endif ()
