@echo off

rem https://gist.github.com/maciakl/f30629d1a606030e55855842447472c5
if "%1" == "elevated" goto start
powershell -command "Start-Process %~nx0 elevated -Verb runas"
goto :EOF

:start
cd %~dp0
schtasks /delete /tn "Screenshot" /f
rmdir /S /Q "C:\Program Files\Screenshot"
IF EXIST "C:\Program Files\Screenshot" (
	echo:
	echo Program was removed from startup. However, C:\Program Files\Screenshot could not be removed.
	echo Please ensure no programs, like screenshot.exe, are running from this directory and try again.
	echo:
	PAUSE
) ELSE (
	echo:
	echo Program is uninstalled and removed from startup.
	echo:
	PAUSE
)