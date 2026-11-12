# Monocypher 4.0.2 from https://monocypher.org, with the optional Ed25519 files.
# The launcher checks the update signatures with crypto_ed25519_check, which uses SHA-512.
# The files are unchanged copies of src/ and src/optional/ of monocypher-4.0.2.tar.gz.
add_library(monocypher STATIC monocypher.c monocypher.h monocypher-ed25519.c monocypher-ed25519.h)
target_include_directories(monocypher PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
set_target_properties(monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)
