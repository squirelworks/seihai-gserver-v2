@echo off
rem Seihai lab world: start gs2emu on 127.0.0.1 (Windows).
rem Part of the Seihai fork of GServer-v2 (GPL-3.0): see CHANGES-SEIHAI.md.
rem
rem   start.bat            start both lab servers, each in its own window:
rem                          servers\default  2.00 to 3.153  127.0.0.1:14802
rem                          servers\classic  1.33 to 1.41r1 127.0.0.1:14803
rem   start.bat classic    start one server folder, in this window
rem
rem Close a server's window (or press Ctrl+C in it) to stop that server.

setlocal
cd /d "%~dp0"

if not exist gs2emu.exe (
	echo start.bat: no gs2emu.exe next to this script. Build it or unpack a release first, see README.md.
	pause
	exit /b 1
)

if not "%~1"=="" (
	if not exist "servers\%~1\config\serveroptions.txt" (
		echo start.bat: no server folder servers\%~1
		exit /b 1
	)
	echo Starting %~1. Stop with Ctrl+C.
	gs2emu.exe --server "%~1"
	exit /b %errorlevel%
)

for %%s in (default classic) do (
	if not exist "servers\%%s\config\serveroptions.txt" (
		echo start.bat: no server folder servers\%%s
		pause
		exit /b 1
	)
)

start "Seihai lab: default (127.0.0.1:14802)" gs2emu.exe --server default
start "Seihai lab: classic (127.0.0.1:14803)" gs2emu.exe --server classic
echo Started default on 127.0.0.1:14802 and classic on 127.0.0.1:14803, each in its own window.
echo Close a window to stop that server.
