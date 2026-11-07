@echo off
rem Lets blobby:// join links from the tournament site open this copy of the game.
rem It writes only below HKEY_CURRENT_USER\Software\Classes\blobby and needs no admin rights.
"%~dp0blobby.exe" --register-link-handler
