@echo off
setlocal

cd /d "%~dp0"

python -m pip install --upgrade pip
if errorlevel 1 goto :error

python -m pip install -r requirements.txt
if errorlevel 1 goto :error

rem Stop a previously built generator instance before replacing the EXE.
taskkill /IM kiwi.exe /F >nul 2>&1

rem Remove the previous executable explicitly so a locked file is reported clearly.
if exist "dist\kiwi.exe" (
    del /F /Q "dist\kiwi.exe" >nul 2>&1
    if exist "dist\kiwi.exe" (
        echo.
        echo ERROR: dist\kiwi.exe is locked and cannot be replaced.
        echo Close the running generator instance and retry the build.
        goto :error
    )
)

pyinstaller --noconfirm --clean kiwi.spec
if errorlevel 1 goto :error

echo.
echo Build completed. EXE is in dist\kiwi.exe
pause
exit /b 0

:error
echo.
echo Build failed.
pause
exit /b 1
