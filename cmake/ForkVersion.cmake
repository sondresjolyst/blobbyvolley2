# The fork version lives in version.txt and is updated by release-please.
# The upstream version stays in project(blobby VERSION ...).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/version.txt")
file(STRINGS "${PROJECT_SOURCE_DIR}/version.txt" BLOBBY_FORK_VERSION LIMIT_COUNT 1)
if(NOT BLOBBY_FORK_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
	message(FATAL_ERROR "version.txt must hold a version like 1.2.3, found '${BLOBBY_FORK_VERSION}'")
endif()
configure_file("${PROJECT_SOURCE_DIR}/cmake/ForkVersion.h.in" "${PROJECT_BINARY_DIR}/generated/ForkVersion.h" @ONLY)
include_directories("${PROJECT_BINARY_DIR}/generated")
