@echo off
rem Stops blobby:// join links from opening the game.
"%~dp0blobby.exe" --unregister-link-handler
