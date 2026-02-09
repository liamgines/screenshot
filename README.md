screenshot
========

screenshot is a Windows application for taking Desktop screen captures.

Controls
--------
`PrtSc` displays the selection screen.<br>
`Left Click` creates, resizes or moves a selection.<br>
`Ctrl+S` saves a selection.<br>
`Esc` closes the selection screen.

Installation
--------
First, run:

	vcvarsall.bat x64

Then compile with:

	cl /ZI /D _UNICODE /D UNICODE main.c User32.lib Gdi32.lib Shlwapi.lib /Fe:screenshot.exe

If you want to add the app to startup, run `install.bat`.

Otherwise, manually run the app as admin.