@echo off
setlocal

cd /d "%~dp0"

python -m pip install --upgrade pip
if errorlevel 1 goto :error

python -m pip install -r requirements.txt
if errorlevel 1 goto :error

rem Stop running KIWI UI instance before replacing the executable.
taskkill /IM kiwi_gui.exe /F >nul 2>&1

rem Remove previous executables explicitly so locked files are reported clearly.
if exist "dist\kiwi_gui.exe" (
    del /F /Q "dist\kiwi_gui.exe" >nul 2>&1
    if exist "dist\kiwi_gui.exe" (
        echo.
        echo ERROR: dist\kiwi_gui.exe is locked and cannot be replaced.
        echo Close the running generator instance and retry the build.
        goto :error
    )
)

if exist "dist\kiwi.exe" (
    del /F /Q "dist\kiwi.exe" >nul 2>&1
    if exist "dist\kiwi.exe" (
        echo.
        echo ERROR: dist\kiwi.exe is locked and cannot be replaced.
        echo Close any running KIWI CLI instance and retry the build.
        goto :error
    )
)

pyinstaller --noconfirm --clean kiwi_gui.spec
if errorlevel 1 goto :error

pyinstaller --noconfirm --clean kiwi.spec
if errorlevel 1 goto :error

echo.
echo Build completed. EXEs are in dist\kiwi_gui.exe and dist\kiwi.exe
pause
exit /b 0

:error
echo.
echo Build failed.
pause
exit /b 1
