@echo off
rem Upload Golioth device certificate + private key into LittleFS over serial.
rem Close any serial terminal on this COM port first (exclusive access).
rem
rem   set SERIAL_PORT=COM11
rem   set CERT_FILE=path\to\crt.der
rem   set KEY_FILE=path\to\key.der
setlocal
if "%SERIAL_PORT%"=="" set SERIAL_PORT=COM11
if "%CERT_FILE%"=="" set CERT_FILE=crt.der
if "%KEY_FILE%"=="" set KEY_FILE=key.der

if not exist "%CERT_FILE%" (
  echo Missing certificate: %CERT_FILE%
  exit /b 1
)
if not exist "%KEY_FILE%" (
  echo Missing private key: %KEY_FILE%
  exit /b 1
)

rem line-length 127 matches Zephyr MCUMGR_SERIAL_MAX_FRAME.
rem Do not use --mtu: newer smpmgr maps it to line-buffers 1 and then
rem still sizes packets from the device buf_size, which overflows the
rem shell transport.
echo Uploading %CERT_FILE% -^> /lfs1/credentials/crt.der on %SERIAL_PORT%
smpmgr --port %SERIAL_PORT% --line-length 127 --line-buffers 3 file upload "%CERT_FILE%" "/lfs1/credentials/crt.der"
if errorlevel 1 (
  echo Certificate upload failed.
  exit /b 1
)
echo Uploading %KEY_FILE% -^> /lfs1/credentials/key.der on %SERIAL_PORT%
smpmgr --port %SERIAL_PORT% --line-length 127 --line-buffers 3 file upload "%KEY_FILE%" "/lfs1/credentials/key.der"
if errorlevel 1 (
  echo Key upload failed.
  exit /b 1
)
echo Credentials uploaded. Reset the board so Pouch can load them.
endlocal
