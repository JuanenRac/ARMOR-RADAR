@echo off
rem ARMOR-RADAR - writes the image of one node to a board over USB.
rem Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
rem
rem   tools\flash.bat perimetro-1 COM5          writes dist\perimetro-1-s3-eth.bin to the board on COM5 (the Waveshare ESP32-S3-ETH)
rem   tools\flash.bat perimetro-1 COM5 s3-wifi  writes dist\perimetro-1-s3-wifi.bin instead (an ESP32-S3-WROOM-1 N16R8 with no Ethernet)
rem   tools\flash.bat perimetro-1 COM5 monitor  ... and then shows its serial log (Ctrl+] leaves)
rem   tools\flash.bat perimetro-1 COM5 erase    erases the whole flash first: the settings and users of the node go too (a clean start)
rem The words s3-eth, s3-wifi, monitor and erase may come in any order after the port. An image is for ONE board: use the one of the board you flash.
rem
rem Without "erase" the settings and the users stored in the node survive a new firmware.
rem
rem The board is put in download mode by esptool itself over the native USB port. If it does not answer, hold BOOT, press and
rem release RESET, release BOOT, and run this again.
setlocal
set NODE=%1
set PORT=%2
if "%NODE%"=="" goto usage
if "%PORT%"=="" goto usage
set ROOT=%~dp0..
set BOARD=s3-eth
set ERASE=0
set MONITOR=0
for %%A in (%3 %4 %5) do (
  if /I "%%A"=="s3-eth" set BOARD=s3-eth
  if /I "%%A"=="s3-wifi" set BOARD=s3-wifi
  if /I "%%A"=="erase" set ERASE=1
  if /I "%%A"=="monitor" set MONITOR=1
)
set IMAGE=%NODE%-%BOARD%
set PY=%ROOT%\tools\.venv\Scripts\python.exe
if not exist "%PY%" (
  echo Creating the flashing tools ^(esptool^) once...
  python -m venv "%ROOT%\tools\.venv" || goto fail
  "%PY%" -m pip install --quiet esptool pyserial || goto fail
)
if not exist "%ROOT%\dist\%IMAGE%.bin" (
  echo dist\%IMAGE%.bin does not exist: build it first with tools/build_node.sh %NODE% %BOARD% ^(from WSL^).
  goto fail
)
if "%ERASE%"=="1" "%PY%" -m esptool --chip esp32s3 -p %PORT% erase_flash || goto fail
"%PY%" -m esptool --chip esp32s3 -p %PORT% -b 460800 write_flash 0x0 "%ROOT%\dist\%IMAGE%.bin" || goto fail
echo.
echo Written. Press RESET on the board.
if "%MONITOR%"=="1" "%PY%" -m serial.tools.miniterm %PORT% 115200
goto end
:usage
echo usage: tools\flash.bat NODE_ID COMx [s3-eth^|s3-wifi] [monitor] [erase]
:fail
exit /b 1
:end
endlocal
