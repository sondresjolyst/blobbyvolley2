if (UNIX AND NOT APPLE)
	install(FILES blobby.appdata.xml DESTINATION share/metainfo)
	install(FILES blobby.desktop DESTINATION share/applications)
	install(FILES blobby.png DESTINATION share/icons/hicolor/128x128/apps)
	# Join link helpers. The installed blobby is on PATH, so they need no argument.
	install(PROGRAMS register-join-links.sh DESTINATION bin RENAME blobby-register-join-links)
	install(PROGRAMS unregister-join-links.sh DESTINATION bin RENAME blobby-unregister-join-links)
endif (UNIX AND NOT APPLE)
