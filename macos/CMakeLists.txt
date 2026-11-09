if (UNIX AND APPLE)
	if(BUILD_MACOS_BUNDLE)
		# enable bundle
		set_target_properties(blobby PROPERTIES MACOSX_BUNDLE TRUE)

		# add data folder content and bundle icon to bundle
		set(icns_file "${CMAKE_CURRENT_SOURCE_DIR}/icon.icns")
		# The zip archives are made in the data folder, so the src folder of the target must know
		# that they are generated, and they must be made before the bundle is put together.
		if(CMAKE_VERSION VERSION_LESS 3.18)
			message(FATAL_ERROR "BUILD_MACOS_BUNDLE needs CMake 3.18 or newer")
		endif()
		set(zip_files ${data_files})
		list(FILTER zip_files INCLUDE REGEX "\\.zip$")
		set_property(SOURCE ${zip_files} DIRECTORY "${PROJECT_SOURCE_DIR}/src" PROPERTY GENERATED TRUE)
		add_dependencies(blobby gfx_zip sounds_zip scripts_zip backgrounds_zip rules_zip)
		target_sources(blobby PUBLIC ${data_files} ${icns_file})
		set_target_properties(blobby PROPERTIES RESOURCE "${data_files};${icns_file}")

		# add plist
		set_target_properties(blobby PROPERTIES 
		                             XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE YES
		                             XCODE_ATTRIBUTE_INFOPLIST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
		                             MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")

		# Allow c++ code to change behavior
		target_compile_definitions(blobby PUBLIC BUILD_MACOS_BUNDLE)

		# install bundle only
		install(TARGETS blobby BUNDLE DESTINATION .)

		# add libs to bundle
		install(CODE [=[ include(BundleUtilities)
		                 fixup_bundle("${CMAKE_INSTALL_PREFIX}/blobby.app" "" "") ]=])
	else()
		install(TARGETS blobby blobby-server DESTINATION bin)
		install(FILES ${data_files} DESTINATION share/blobby)
	endif()
endif()
