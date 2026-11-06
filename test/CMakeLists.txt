enable_testing()

set(CMAKE_CXX_STANDARD 11)

set(SRC
	../src/BlobbyDebug.cpp    ../src/BlobbyDebug.h
	../src/FileWrite.cpp      ../src/FileWrite.h
	../src/FileRead.cpp       ../src/FileRead.h
	../src/FileSystem.cpp     ../src/FileSystem.h
	../src/File.cpp           ../src/File.h
	../src/GenericIO.cpp      ../src/GenericIO.h
	../src/PlayerInput.h      ../src/PlayerInput.cpp
	../src/DuelMatchState.cpp ../src/DuelMatchState.h
	../src/GameLogicState.cpp ../src/GameLogicState.h
	../src/PhysicState.cpp    ../src/PhysicState.h
	../src/DuelMatch.cpp      ../src/DuelMatch.h
	../src/Clock.cpp          ../src/Clock.h
	../src/PhysicWorld.cpp    ../src/PhysicWorld.h 
	../src/GameLogic.cpp      ../src/GameLogic.h
	../src/InputSource.cpp    ../src/InputSource.h
	../src/IScriptableComponent.cpp ../src/IScriptableComponent.h
	../src/PlayerIdentity.cpp ../src/PlayerIdentity.h
	../src/UserConfig.cpp     ../src/UserConfig.h
	../src/Color.cpp          ../src/Color.h
	../src/base64.cpp         ../src/base64.h
	../src/FontIndex.cpp      ../src/FontIndex.h
	../src/SponsorBoard.cpp   ../src/SponsorBoard.h
	../src/server/MatchMaker.cpp    ../src/server/MatchMaker.h
	../src/server/NetworkPlayer.cpp ../src/server/NetworkPlayer.h
	../src/replays/ReplayLoader.cpp    ../src/replays/IReplayLoader.h
	../src/replays/ReplayPlayer.cpp    ../src/replays/ReplayPlayer.h
	../src/replays/ReplayRecorder.cpp  ../src/replays/ReplayRecorder.h
	../src/replays/ReplaySavePoint.cpp ../src/replays/ReplaySavePoint.h
)

# BOOST_TEST_DYN_LINK below needs the shared library
set(Boost_USE_STATIC_LIBS OFF)
find_package(Boost REQUIRED COMPONENTS unit_test_framework)
find_package(PhysFS REQUIRED)
find_package(SDL2 REQUIRED)

if ("${SDL2_LIBRARIES}" STREQUAL "")
	set(SDL2_LIBRARIES "SDL2::SDL2")
endif ("${SDL2_LIBRARIES}" STREQUAL "")

# Boost.Test provides main, so do not link SDL2main
if (WIN32)
	set(SDL2_LIBRARIES "SDL2::SDL2")
endif (WIN32)

add_executable(blobbytest GenericIOTest.cpp FileTest.cpp Base64Test.cpp MatchGoldenTest.cpp ScriptSafetyTest.cpp FontIndexTest.cpp MatchMakerTest.cpp ReliabilityLayerTest.cpp ReplayTest.cpp ForkVersionTest.cpp SponsorBoardTest.cpp ${SRC})

target_include_directories(blobbytest PRIVATE ${Boost_INCLUDE_DIR} ${PHYSFS_INCLUDE_DIR} ${SDL2_INCLUDE_DIRS} ../src)
target_compile_definitions(blobbytest PRIVATE "BOOST_TEST_DYN_LINK=1" "BLOBBY_TEST_DATA_DIR=\"${PROJECT_SOURCE_DIR}/data\"" "BLOBBY_TEST_VERSION_FILE=\"${PROJECT_SOURCE_DIR}/version.txt\"")
target_link_libraries(blobbytest ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} ${PHYSFS_LIBRARY} ${SDL2_LIBRARIES} lua raknet tinyxml2)

if (MSVC)
	target_compile_options(blobbytest PRIVATE /utf-8)
endif ()
