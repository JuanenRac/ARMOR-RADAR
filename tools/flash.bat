@echo off
rem ARMOR-RADAR - writes the image of one node to a board over USB.
rem Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
rem
rem   tools\flash.bat perimetro-1 COM5        writes dist\perimetro-1.bin to the board on COM5
rem   tools\flash.bat perimetro-1 COM5 monitor  ... and then shows its serial log (Ctrl+] leaves)
rem
rem The board is put in download mode by esptool itself over the native USB port. If it does not answer, hold BOOT, press and
rem release RESET, release BOOT, and run this again.
setlocal
set NODE=%1
set PORT=%2
if "%NODE%"=="" goto usage
if "%PORT%"=="" goto usage
set ROOT=%~dp0..
set PY=%ROOT%\tools\.venv\Scripts\python.exe
if not exist "%PY%" (
  echo Creating the flashing tools ^(esptool^) once...
  python -m venv "%ROOT%\tools\.venv" || goto fail
  "%PY%" -m pip install --quiet esptool pyserial || goto fail
)
if not exist "%ROOT%\dist\%NODE%.bin" (
  echo dist\%NODE%.bin does not exist: build it first with tools/build_node.sh %NODE% ^(from WSL^).
  goto fail
)
"%PY%" -m esptool --chip esp32s3 -p %PORT% -b 460800 write_flash 0x0 "%ROOT%\dist\%NODE%.bin" || goto fail
echo.
echo Written. Press RESET on the board.
if /I "%3"=="monitor" "%PY%" -m serial.tools.miniterm %PORT% 115200
goto end
:usage
echo usage: tools\flash.bat NODE_ID COMx [monitor]
:fail
exit /b 1
:end
endlocal
