@echo off

rem https://gist.github.com/maciakl/f30629d1a606030e55855842447472c5
if "%1" == "elevated" goto start
powershell -command "Start-Process %~nx0 elevated -Verb runas"
goto :EOF

:start
cd %~dp0
IF EXIST screenshot.exe (
	del "C:\Program Files\Screenshot\screenshot.exe"
	IF EXIST "C:\Program Files\Screenshot\screenshot.exe" (
		echo:
		echo ERROR: screenshot.exe could not be added to startup. Please ensure that the existing screenshot.exe at C:\Program Files\Screenshot is not running and try again.
		echo:
		PAUSE
	) ELSE (
		mkdir "C:\Program Files\Screenshot"
		copy /B /Y "screenshot.exe" "C:\Program Files\Screenshot\screenshot.exe" /B
		schtasks /create /sc ONLOGON /tn "Screenshot" /tr "'C:\Program Files\Screenshot\screenshot.exe' C:\Users\%USERNAME%\Desktop" /rl HIGHEST /f
		echo:
		echo Program is installed and added to startup.
		echo:
		PAUSE
	)
) ELSE (
	echo:
	echo ERROR: Place screenshot.exe in the same directory as install.bat and try again.
	echo:
	PAUSE
)