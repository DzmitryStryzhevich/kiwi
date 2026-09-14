@echo off
setlocal

cd /d "%~dp0"

python -m pip install --upgrade pip
if errorlevel 1 goto :error

python -m pip install -r kiwicgen-requirements.txt
if errorlevel 1 goto :error

rem Stop a running kiwicgen GUI instance before replacing the executable.
taskkill /IM kiwicgen-gui.exe /F >nul 2>&1

rem Remove previous executables explicitly so locked files are reported clearly.
if exist "dist\kiwicgen-gui.exe" (
    del /F /Q "dist\kiwicgen-gui.exe" >nul 2>&1
    if exist "dist\kiwicgen-gui.exe" (
        echo.
        echo ERROR: dist\kiwicgen-gui.exe is locked and cannot be replaced.
        echo Close the running generator instance and retry the build.
        goto :error
    )
)

if exist "dist\kiwicgen-cli.exe" (
    del /F /Q "dist\kiwicgen-cli.exe" >nul 2>&1
    if exist "dist\kiwicgen-cli.exe" (
        echo.
        echo ERROR: dist\kiwicgen-cli.exe is locked and cannot be replaced.
        echo Close any running kiwicgen CLI instance and retry the build.
        goto :error
    )
)

pyinstaller --noconfirm --clean kiwicgen-gui.spec
if errorlevel 1 goto :error

pyinstaller --noconfirm --clean kiwicgen-cli.spec
if errorlevel 1 goto :error

echo.
echo Build completed. EXEs are in dist\kiwicgen-gui.exe and dist\kiwicgen-cli.exe
pause
exit /b 0

:error
echo.
echo Build failed.
pause
exit /b 1
