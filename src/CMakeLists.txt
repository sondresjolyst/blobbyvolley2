set(APP_NAME "Blobby Volley 2")
set(APP_AUTHOR "")

set(CMAKE_CXX_STANDARD 11)

if (MSVC)
	add_compile_options(/utf-8)
	# When building for MSVC with CMake,
	# the compiler warning flags (like /W3) are added by default.
else()
	add_compile_options(-Wall)
endif()

include_directories(.)

set(common_SRC
	base64.cpp base64.h
	BlobbyDebug.cpp BlobbyDebug.h
	ClientVersion.cpp ClientVersion.h
	Clock.cpp Clock.h
	DuelMatch.cpp DuelMatch.h
	FileRead.cpp FileRead.h
	FileSystem.cpp FileSystem.h
	FileWrite.cpp FileWrite.h
	File.cpp File.h
	GameLogic.cpp GameLogic.h
	GenericIO.cpp GenericIO.h
	Global.h
	Color.cpp Color.h
	NetworkMessage.cpp NetworkMessage.h
	PhysicWorld.cpp PhysicWorld.h
	SpeedController.cpp SpeedController.h
	UserConfig.cpp UserConfig.h
	PhysicState.cpp PhysicState.h
	DuelMatchState.cpp DuelMatchState.h
	GameLogicState.cpp GameLogicState.h
	InputSource.cpp InputSource.h
	PlayerInput.h PlayerInput.cpp
	IScriptableComponent.cpp IScriptableComponent.h
	PlayerIdentity.cpp PlayerIdentity.h
	ScriptBot.cpp ScriptBot.h
	ScriptedInputSource.cpp ScriptedInputSource.h
	Sha256.cpp Sha256.h
	SponsorBoard.cpp SponsorBoard.h
	SponsorSync.cpp SponsorSync.h
	StringUtil.cpp StringUtil.h
	TournamentLimits.h
	TournamentProtocol.cpp TournamentProtocol.h
	server/DedicatedServer.cpp server/DedicatedServer.h
	server/FileUtil.cpp server/FileUtil.h
	server/NetworkPlayer.cpp server/NetworkPlayer.h
	server/NetworkGame.cpp server/NetworkGame.h
	server/MatchMaker.cpp server/MatchMaker.h
	server/JoinCodes.cpp server/JoinCodes.h
	server/MatchAssignments.cpp server/MatchAssignments.h
	server/Tournament.cpp server/Tournament.h
	replays/ReplayRecorder.cpp replays/ReplayRecorder.h
	replays/ReplaySavePoint.cpp replays/ReplaySavePoint.h
	)

set (blobby_SRC ${common_SRC} ${inputdevice_SRC}
	Blood.cpp Blood.h
	TextManager.cpp TextManager.h
	EditboxWindow.cpp EditboxWindow.h
	IMGUI.cpp IMGUI.h
	InputDevice.h
	InputManager.cpp InputManager.h
	JoinLink.cpp JoinLink.h
	LocalInputSource.cpp LocalInputSource.h
	FontIndex.cpp FontIndex.h
	FutureResult.h
	RenderManager.cpp RenderManager.h
	RenderManagerGL2D.cpp RenderManagerGL2D.h
	RenderManagerSDL.cpp RenderManagerSDL.h
	RenderManagerNull.cpp RenderManagerNull.h
	SoundManager.cpp SoundManager.h
	Vector.h
	replays/ReplayPlayer.cpp replays/ReplayPlayer.h
	replays/ReplayLoader.cpp
	state/State.cpp state/State.h
	state/GameState.cpp state/GameState.h
	state/LocalGameState.cpp state/LocalGameState.h
	state/NetworkState.cpp state/NetworkState.h
	state/OptionsState.cpp state/OptionsState.h
	state/NetworkSearchState.cpp state/NetworkSearchState.h
	state/ReplayState.cpp state/ReplayState.h
	state/ReplaySelectionState.cpp state/ReplaySelectionState.h
	state/LobbyStates.cpp state/LobbyStates.h
	input_device/JoystickInput.cpp
	input_device/JoystickPool.cpp input_device/JoystickPool.h
	input_device/KeyboardInput.cpp
	input_device/MouseInput.cpp
	input_device/TouchInput.cpp
	BlobbyApp.h BlobbyApp.cpp
	)

set (blobby-server_SRC ${common_SRC}
	server/servermain.cpp
	server/SiteConfig.cpp server/SiteConfig.h
	)

# The tournament site client of the server needs libcurl and nlohmann_json. Only the server uses them.
# AUTO builds the client when both are found, ON fails without them and OFF leaves the client out.
set(BLOBBY_SITE_SYNC AUTO CACHE STRING "Build the tournament site client into the server: AUTO, ON or OFF")
set(BLOBBY_SITE_SYNC_ENABLED OFF)
if (NOT BLOBBY_SITE_SYNC STREQUAL "OFF" AND NOT SWITCH AND NOT ANDROID)
	find_package(CURL QUIET)
	find_package(nlohmann_json 3.10 CONFIG QUIET)
	if (CURL_FOUND AND nlohmann_json_FOUND)
		set(BLOBBY_SITE_SYNC_ENABLED ON)
		message(STATUS "The server is built with the tournament site client")
	elseif (BLOBBY_SITE_SYNC STREQUAL "ON")
		message(FATAL_ERROR "BLOBBY_SITE_SYNC is ON, but libcurl or nlohmann_json was not found")
	else ()
		message(STATUS "libcurl or nlohmann_json not found, the server is built without the tournament site client")
	endif ()
endif ()

find_package(Boost REQUIRED)
find_package(OpenGL)
add_subdirectory(raknet)
add_subdirectory(blobnet)

if (SWITCH)
	find_library(NX nx)
endif (SWITCH)

if (OPENGL_FOUND)
	include_directories(${OPENGL_INCLUDE_DIR})
	add_definitions(-DHAVE_LIBGL)
endif (OPENGL_FOUND)

if (UNIX AND (NOT ANDROID))
	add_definitions("-DBLOBBY_DATA_DIR=\"${CMAKE_INSTALL_PREFIX}/share/blobby\"")
endif()

set(BLOBBY_COMMON_LIBS lua::lua blobnet::blobnet tinyxml2::tinyxml2 PhysFS::PhysFS SDL2::SDL2main SDL2::SDL2 Boost::boost)

# other additional dependencies
if (NOT WIN32)
	find_package(Threads REQUIRED)
	set(BLOBBY_COMMON_LIBS ${BLOBBY_COMMON_LIBS} Threads::Threads)
endif ()

if (SWITCH)
	add_executable(blobby.elf main.cpp ${blobby_SRC})
	target_link_libraries(blobby.elf ${DEVKITPRO}/portlibs/switch/lib/libEGL.a ${DEVKITPRO}/portlibs/switch/lib/libdrm_nouveau.a ${DEVKITPRO}/portlibs/switch/lib/libglapi.a ${DEVKITPRO}/libnx/lib/libnx.a)
	add_custom_command(
		OUTPUT blobby.nro
		COMMAND ${DEVKITPRO}/tools/bin/elf2nro blobby.elf blobby.nro --icon=${CMAKE_SOURCE_DIR}/data/Icon.jpg --nacp=blobby.nacp
		DEPENDS blobby.elf blobby.nacp)
	add_custom_command(
		OUTPUT blobby.nacp
		COMMAND ${DEVKITPRO}/tools/bin/nacptool --create "${APP_NAME}" "${APP_AUTHOR}" "${PROJECT_VERSION}" VERBATIM blobby.nacp
	)
	add_custom_target(blobby ALL DEPENDS blobby.nro)
else ()
	if (ANDROID)
		add_library(blobby SHARED main.cpp ${blobby_SRC})
	elseif (WIN32)
	    add_executable(blobby WIN32 main.cpp ${blobby_SRC})
	else ()
		add_executable(blobby main.cpp ${blobby_SRC})
	endif ()
endif ()

target_link_libraries(blobby ${BLOBBY_COMMON_LIBS} ${OPENGL_LIBRARIES})


if (UNIX AND (NOT ANDROID) OR WIN32)
	add_executable(blobby-server ${blobby-server_SRC})
	target_link_libraries(blobby-server ${BLOBBY_COMMON_LIBS})
	if (BLOBBY_SITE_SYNC_ENABLED)
		target_sources(blobby-server PRIVATE
			server/SiteSync.cpp server/SiteSync.h
			server/CurlHttpClient.cpp server/CurlHttpClient.h)
		target_compile_definitions(blobby-server PRIVATE BLOBBY_SITE_SYNC)
		target_link_libraries(blobby-server nlohmann_json::nlohmann_json)
		if (TARGET CURL::libcurl)
			target_link_libraries(blobby-server CURL::libcurl)
		else ()
			target_include_directories(blobby-server PRIVATE ${CURL_INCLUDE_DIRS})
			target_link_libraries(blobby-server ${CURL_LIBRARIES})
		endif ()
	endif ()
endif ()

# headless client for load tests of a tournament server, see tools/loadbot/README.md
if (BUILD_LOADBOT)
	add_executable(blobby-loadbot ../tools/loadbot/loadbot.cpp
		../tools/loadbot/LoadbotArgs.cpp
		ScriptBot.cpp
		BlobbyDebug.cpp
		ClientVersion.cpp
		Clock.cpp
		Color.cpp
		DuelMatch.cpp
		DuelMatchState.cpp
		File.cpp
		FileRead.cpp
		FileSystem.cpp
		FileWrite.cpp
		GameLogic.cpp
		GameLogicState.cpp
		GenericIO.cpp
		InputSource.cpp
		IScriptableComponent.cpp
		NetworkMessage.cpp
		PhysicState.cpp
		PhysicWorld.cpp
		PlayerIdentity.cpp
		PlayerInput.cpp
		ScriptedInputSource.cpp
		StringUtil.cpp
		TournamentProtocol.cpp
		UserConfig.cpp
		)
	target_include_directories(blobby-loadbot PRIVATE ../tools/loadbot)
	# the file with the main of the bot never includes SDL.h, so it must not get the main of SDL2main
	set(LOADBOT_LIBS ${BLOBBY_COMMON_LIBS})
	list(REMOVE_ITEM LOADBOT_LIBS SDL2::SDL2main)
	target_link_libraries(blobby-loadbot ${LOADBOT_LIBS})
endif ()

if (UNIX AND (NOT ANDROID))
	add_executable(blobby-runtest EXCLUDE_FROM_ALL runtest.cpp ${blobby_SRC})
	target_link_libraries(blobby-runtest ${BLOBBY_COMMON_LIBS} ${OPENGL_LIBRARIES})

	add_executable(botbench EXCLUDE_FROM_ALL botbench.cpp ${blobby_SRC})
	target_link_libraries(botbench ${BLOBBY_COMMON_LIBS} ${OPENGL_LIBRARIES})
endif ()

if (MSYS)
	set_target_properties(blobby PROPERTIES LINK_FLAGS "-mwindows") # disable the console window
	set_target_properties(blobby-server PROPERTIES LINK_FLAGS "-mconsole") # enable the console window
endif()

if (WIN32)
	install(TARGETS blobby blobby-server DESTINATION .)
	install(FILES ../win/register-join-links.cmd ../win/unregister-join-links.cmd DESTINATION .)
	install(FILES ../COPYING ../README.md ../scripts/tournament-server.ps1 DESTINATION .)
elseif (ANDROID)
	install(TARGETS blobby DESTINATION bin)
elseif (SWITCH)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/blobby.nro DESTINATION .)
elseif (UNIX AND NOT APPLE)
	install(TARGETS blobby blobby-server DESTINATION bin)
endif ()

if (WIN32)
	# application icon and file properties, BLOBBY_RC_TARGET picks the description in res.rc
	target_sources(blobby PRIVATE ../win/res.rc)
	target_compile_definitions(blobby PRIVATE $<$<COMPILE_LANGUAGE:RC>:BLOBBY_RC_TARGET=1>)
	target_sources(blobby-server PRIVATE ../win/res.rc)
	target_compile_definitions(blobby-server PRIVATE $<$<COMPILE_LANGUAGE:RC>:BLOBBY_RC_TARGET=2>)
	if (BUILD_LOADBOT)
		target_sources(blobby-loadbot PRIVATE ../win/res.rc)
		target_compile_definitions(blobby-loadbot PRIVATE $<$<COMPILE_LANGUAGE:RC>:BLOBBY_RC_TARGET=3>)
	endif ()
endif (WIN32)

# Header Checks
# Create a test target, consisting of one cpp file for each header, where the cpp file does nothing but
# include its header. If this compiles, then all our headers are self-contained.

# gather up the header files
file(GLOB_RECURSE HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.h)

# create a new target for this test. We use a static library (instead of an executable), because then
# we don't have to worry about things actually linking.
add_library(test-compile-headers STATIC EXCLUDE_FROM_ALL)

foreach(HEADER ${HEADERS})
	# construct a new file name based on the headers name
	string(MAKE_C_IDENTIFIER "${HEADER}" HEADER_TEST_FILE)
	set(TEST_COMPILE_FILE "${CMAKE_CURRENT_BINARY_DIR}/test_compile/${HEADER_TEST_FILE}.cpp")

	# create the test file and add to the target sources
	configure_file(test_compile.cpp.in ${TEST_COMPILE_FILE} @ONLY)
	target_sources(test-compile-headers PUBLIC ${TEST_COMPILE_FILE})
endforeach()

target_link_libraries(test-compile-headers PRIVATE ${BLOBBY_COMMON_LIBS})
