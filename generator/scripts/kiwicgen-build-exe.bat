@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ---------------------------------------------------------------------------
rem Resolve project paths and parse build parameters.
rem ---------------------------------------------------------------------------
for %%I in ("%~dp0..") do set "GENERATOR_ROOT=%%~fI"
for %%I in ("%GENERATOR_ROOT%\..") do set "REPOSITORY_ROOT=%%~fI"
set "BUILD_DIR=%GENERATOR_ROOT%\build"
set "DIST_DIR=%GENERATOR_ROOT%\dist"
set "REQUESTED_PYTHON="

:parse_args
if "%~1"=="" goto :args_done

if "%~1"=="--python" (
    if "%~2"=="" (
        echo [ERR ] Missing value for --python.
        exit /b 2
    )
    set "REQUESTED_PYTHON=%~2"
    shift
    shift
    goto :parse_args
)

if "%~1"=="--build-dir" (
    if "%~2"=="" (
        echo [ERR ] Missing value for --build-dir.
        exit /b 2
    )
    set "BUILD_DIR=%~2"
    shift
    shift
    goto :parse_args
)

if "%~1"=="--dist-dir" (
    if "%~2"=="" (
        echo [ERR ] Missing value for --dist-dir.
        exit /b 2
    )
    set "DIST_DIR=%~2"
    shift
    shift
    goto :parse_args
)

echo [ERR ] Unknown argument: %~1
exit /b 2

:args_done

for /F "delims=" %%E in ('echo prompt $E^| cmd') do set "ESC=%%E"
set "C_RESET=!ESC![0m"
set "C_INFO=!ESC![97m"
set "C_STEP=!ESC![96m"
set "C_OK=!ESC![92m"
set "C_WARN=!ESC![93m"
set "C_ERR=!ESC![91m"

set "VENV_DIR=%BUILD_DIR%\venv"
set "VENV_PYTHON=%VENV_DIR%\Scripts\python.exe"
set "VENV_CLANG_FORMAT=%VENV_DIR%\Scripts\clang-format.exe"
set "SPEC_DIR=%BUILD_DIR%\spec"

rem ---------------------------------------------------------------------------
rem Select a Python runtime.
rem If --python was provided, validate it directly.
rem Otherwise use Python Launcher first, then PATH as fallback.
rem ---------------------------------------------------------------------------
if not "%REQUESTED_PYTHON%"=="" (
    if not exist "%REQUESTED_PYTHON%" (
        echo !C_ERR![ERR ] Requested Python runtime does not exist: %REQUESTED_PYTHON%!C_RESET!
        exit /b 1
    )

    "%REQUESTED_PYTHON%" -c "import sys, venv; raise SystemExit(0 if sys.version_info >= (3,10) else 1)" >nul 2>&1
    if errorlevel 1 (
        echo !C_ERR![ERR ] Requested Python runtime is unsupported: %REQUESTED_PYTHON%!C_RESET!
        exit /b 1
    )

    set "SELECTED_PYTHON=%REQUESTED_PYTHON%"
    for /F "delims=" %%V in ('"!SELECTED_PYTHON!" -c "import platform; print(platform.python_version())"') do set "SELECTED_VERSION=%%V"
    goto :python_selected
)

echo !C_STEP![STEP] Searching for installed Python runtimes!C_RESET!
set "COUNT=0"

where py >nul 2>&1
if not errorlevel 1 (
    for /F "tokens=1,*" %%A in ('py -0p 2^>^&1') do (
        set "TAG=%%A"
        set "PATHPART=%%B"

        echo !TAG! | findstr /R /C:"^-V:3" >nul
        if not errorlevel 1 (
            if exist "!PATHPART!" (
                "!PATHPART!" -c "import sys, venv; raise SystemExit(0 if sys.version_info >= (3,10) else 1)" >nul 2>&1
                if not errorlevel 1 (
                    set /A COUNT+=1
                    set "PY_PATH_!COUNT!=!PATHPART!"
                    for /F "delims=" %%V in ('"!PATHPART!" -c "import platform; print(platform.python_version())"') do set "PY_VER_!COUNT!=%%V"
                )
            )
        )
    )
)

rem PATH fallback when Python Launcher is unavailable or found nothing.
if "!COUNT!"=="0" (
    for %%N in (python.exe python3.exe) do (
        for /F "delims=" %%P in ('where %%N 2^>nul') do (
            "%%P" -c "import sys, venv; raise SystemExit(0 if sys.version_info >= (3,10) else 1)" >nul 2>&1
            if not errorlevel 1 (
                set /A COUNT+=1
                set "PY_PATH_!COUNT!=%%P"
                for /F "delims=" %%V in ('"%%P" -c "import platform; print(platform.python_version())"') do set "PY_VER_!COUNT!=%%V"
            )
        )
    )
)

if "!COUNT!"=="0" (
    echo !C_ERR![ERR ] No supported Python runtime was found.!C_RESET!
    echo !C_INFO![INFO] Python 3.10 or newer with the venv module is required.!C_RESET!
    exit /b 1
)

echo !C_INFO![INFO] Found !COUNT! supported Python runtime(s):!C_RESET!
for /L %%I in (1,1,!COUNT!) do (
    echo   [%%I] Python !PY_VER_%%I! - !PY_PATH_%%I!
)

if "!COUNT!"=="1" (
    set "SELECT=1"
    echo !C_INFO![INFO] Using the only available Python runtime.!C_RESET!
    goto :selection_done
)

:select_python
set /P "SELECT=Select Python for executable build [1-!COUNT!]: "
echo(!SELECT!| findstr /R "^[1-9][0-9]*$" >nul
if errorlevel 1 (
    echo !C_WARN![WARN] Invalid selection.!C_RESET!
    goto :select_python
)

if !SELECT! LSS 1 (
    echo !C_WARN![WARN] Invalid selection.!C_RESET!
    goto :select_python
)

if !SELECT! GTR !COUNT! (
    echo !C_WARN![WARN] Invalid selection.!C_RESET!
    goto :select_python
)

:selection_done
for %%I in (!SELECT!) do (
    set "SELECTED_PYTHON=!PY_PATH_%%I!"
    set "SELECTED_VERSION=!PY_VER_%%I!"
)

:python_selected
echo !C_STEP![STEP] Building standalone kiwicgen distribution!C_RESET!
echo !C_INFO![INFO] Generator root: %GENERATOR_ROOT%!C_RESET!
echo !C_INFO![INFO] Build directory: %BUILD_DIR%!C_RESET!
echo !C_INFO![INFO] Distribution directory: %DIST_DIR%!C_RESET!
echo !C_INFO![INFO] Selected Python: %SELECTED_VERSION% - %SELECTED_PYTHON%!C_RESET!

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
if not exist "%SPEC_DIR%" mkdir "%SPEC_DIR%"

rem ---------------------------------------------------------------------------
rem Recreate build venv if it belongs to another Python runtime.
rem ---------------------------------------------------------------------------
if exist "%VENV_PYTHON%" (
    set "VENV_BASE="
    for /F "delims=" %%P in ('"%VENV_PYTHON%" -c "import sys; print(sys._base_executable)" 2^>nul') do set "VENV_BASE=%%P"

    if /I not "!VENV_BASE!"=="%SELECTED_PYTHON%" (
        echo !C_WARN![WARN] Existing build virtual environment belongs to another Python runtime.!C_RESET!
        echo !C_STEP![STEP] Recreating build virtual environment!C_RESET!
        rmdir /S /Q "%VENV_DIR%"
    )
)

if not exist "%VENV_PYTHON%" (
    echo !C_STEP![STEP] Creating isolated build virtual environment!C_RESET!
    echo !C_INFO![INFO] Virtual environment: %VENV_DIR%!C_RESET!

    "%SELECTED_PYTHON%" -m venv "%VENV_DIR%"
    if errorlevel 1 (
        echo !C_ERR![ERR ] Failed to create the build virtual environment.!C_RESET!
        exit /b !ERRORLEVEL!
    )

    echo !C_OK![ OK ] Build virtual environment created.!C_RESET!
) else (
    echo !C_INFO![INFO] Reusing build virtual environment: %VENV_DIR%!C_RESET!
)

rem ---------------------------------------------------------------------------
rem Ensure pip exists inside the isolated build environment.
rem ---------------------------------------------------------------------------
"%VENV_PYTHON%" -m pip --version >nul 2>&1
if errorlevel 1 (
    echo !C_WARN![WARN] pip is not available in the build virtual environment.!C_RESET!
    echo !C_STEP![STEP] Bootstrapping pip with ensurepip!C_RESET!

    "%VENV_PYTHON%" -m ensurepip --upgrade
    if errorlevel 1 (
        echo !C_ERR![ERR ] Unable to bootstrap pip.!C_RESET!
        exit /b !ERRORLEVEL!
    )
)

rem ---------------------------------------------------------------------------
rem Install runtime/build dependencies only into build/venv.
rem ---------------------------------------------------------------------------
echo !C_STEP![STEP] Installing executable build dependencies!C_RESET!

pushd "%GENERATOR_ROOT%" >nul
"%VENV_PYTHON%" -m pip install ".[executable]"
set "PIP_RC=!ERRORLEVEL!"
popd >nul

if not "!PIP_RC!"=="0" (
    echo !C_ERR![ERR ] Failed to install executable build dependencies.!C_RESET!
    exit /b !PIP_RC!
)

echo !C_OK![ OK ] Executable build dependencies installed.!C_RESET!

if not exist "%VENV_CLANG_FORMAT%" (
    echo !C_ERR![ERR ] clang-format was not installed into the build environment: %VENV_CLANG_FORMAT%!C_RESET!
    exit /b 1
)

rem ---------------------------------------------------------------------------
rem Read the release version from the single generator version source.
rem ---------------------------------------------------------------------------
set "PROJECT_VERSION="
for /F "delims=" %%V in ('"%VENV_PYTHON%" -c "import sys; sys.path.insert(0, r'%GENERATOR_ROOT%'); import kiwicgen_version; print(kiwicgen_version.__version__)"') do (
    set "PROJECT_VERSION=%%V"
)

if "!PROJECT_VERSION!"=="" (
    echo !C_ERR![ERR ] Unable to read kiwicgen version from kiwicgen_version.py.!C_RESET!
    exit /b 1
)

for /F "tokens=1-3 delims=." %%A in ("!PROJECT_VERSION!") do (
    set "VERSION_MAJOR=%%A"
    set "VERSION_MINOR=%%B"
    set "VERSION_PATCH=%%C"
)

set "CLI_VERSION_INFO=%BUILD_DIR%\kiwicgen-version-info.txt"
set "GUI_VERSION_INFO=%BUILD_DIR%\kiwicgen-gui-version-info.txt"

> "%CLI_VERSION_INFO%" (
    echo VSVersionInfo(
    echo   ffi=FixedFileInfo(
    echo     filevers=(!VERSION_MAJOR!, !VERSION_MINOR!, !VERSION_PATCH!, 0),
    echo     prodvers=(!VERSION_MAJOR!, !VERSION_MINOR!, !VERSION_PATCH!, 0),
    echo     mask=0x3f,
    echo     flags=0x0,
    echo     OS=0x40004,
    echo     fileType=0x1,
    echo     subtype=0x0,
    echo     date=(0, 0)
    echo   ^),
    echo   kids=[
    echo     StringFileInfo([StringTable(u'040904B0', [
    echo       StringStruct(u'CompanyName', u''),
    echo       StringStruct(u'FileDescription', u'KIWI Code Generator'),
    echo       StringStruct(u'FileVersion', u'!PROJECT_VERSION!'),
    echo       StringStruct(u'InternalName', u'kiwicgen'),
    echo       StringStruct(u'OriginalFilename', u'kiwicgen.exe'),
    echo       StringStruct(u'ProductName', u'kiwicgen'),
    echo       StringStruct(u'ProductVersion', u'!PROJECT_VERSION!')
    echo     ]^)]^),
    echo     VarFileInfo([VarStruct(u'Translation', [1033, 1200])])
    echo   ]
    echo ^)
)

> "%GUI_VERSION_INFO%" (
    echo VSVersionInfo(
    echo   ffi=FixedFileInfo(
    echo     filevers=(!VERSION_MAJOR!, !VERSION_MINOR!, !VERSION_PATCH!, 0),
    echo     prodvers=(!VERSION_MAJOR!, !VERSION_MINOR!, !VERSION_PATCH!, 0),
    echo     mask=0x3f,
    echo     flags=0x0,
    echo     OS=0x40004,
    echo     fileType=0x1,
    echo     subtype=0x0,
    echo     date=(0, 0)
    echo   ^),
    echo   kids=[
    echo     StringFileInfo([StringTable(u'040904B0', [
    echo       StringStruct(u'CompanyName', u''),
    echo       StringStruct(u'FileDescription', u'KIWI Code Generator GUI'),
    echo       StringStruct(u'FileVersion', u'!PROJECT_VERSION!'),
    echo       StringStruct(u'InternalName', u'kiwicgen-gui'),
    echo       StringStruct(u'OriginalFilename', u'kiwicgen-gui.exe'),
    echo       StringStruct(u'ProductName', u'kiwicgen'),
    echo       StringStruct(u'ProductVersion', u'!PROJECT_VERSION!')
    echo     ]^)]^),
    echo     VarFileInfo([VarStruct(u'Translation', [1033, 1200])])
    echo   ]
    echo ^)
)

echo !C_INFO![INFO] kiwicgen version: !PROJECT_VERSION!!C_RESET!

rem ---------------------------------------------------------------------------
rem Build both one-file frontends. The generated spec files stay in build/.
rem ---------------------------------------------------------------------------
echo !C_STEP![STEP] Building kiwicgen console executable!C_RESET!
"%VENV_PYTHON%" -m PyInstaller ^
    --noconfirm ^
    --clean ^
    --onefile ^
    --console ^
    --name kiwicgen ^
    --icon "%REPOSITORY_ROOT%\doc\kiwi.ico" ^
    --version-file "%CLI_VERSION_INFO%" ^
    --workpath "%BUILD_DIR%\pyinstaller\kiwicgen" ^
    --specpath "%SPEC_DIR%" ^
    --distpath "%DIST_DIR%" ^
    "%GENERATOR_ROOT%\kiwicgen_cli.py"
if errorlevel 1 (
    echo !C_ERR![ERR ] kiwicgen PyInstaller build failed.!C_RESET!
    exit /b !ERRORLEVEL!
)
echo !C_OK![ OK ] kiwicgen executable created.!C_RESET!

echo !C_STEP![STEP] Building kiwicgen GUI executable!C_RESET!
"%VENV_PYTHON%" -m PyInstaller ^
    --noconfirm ^
    --clean ^
    --onefile ^
    --windowed ^
    --name kiwicgen-gui ^
    --icon "%REPOSITORY_ROOT%\doc\kiwi.ico" ^
    --version-file "%GUI_VERSION_INFO%" ^
    --workpath "%BUILD_DIR%\pyinstaller\kiwicgen-gui" ^
    --specpath "%SPEC_DIR%" ^
    --distpath "%DIST_DIR%" ^
    "%GENERATOR_ROOT%\kiwicgen_gui.py"
if errorlevel 1 (
    echo !C_ERR![ERR ] kiwicgen-gui PyInstaller build failed.!C_RESET!
    exit /b !ERRORLEVEL!
)
echo !C_OK![ OK ] kiwicgen-gui executable created.!C_RESET!

rem ---------------------------------------------------------------------------
rem Stage every external runtime resource required by the standalone tools.
rem ---------------------------------------------------------------------------
echo !C_STEP![STEP] Staging standalone distribution resources!C_RESET!

if exist "%DIST_DIR%\osal" rmdir /S /Q "%DIST_DIR%\osal"
if exist "%DIST_DIR%\doc" rmdir /S /Q "%DIST_DIR%\doc"
if exist "%DIST_DIR%\tools" rmdir /S /Q "%DIST_DIR%\tools"
mkdir "%DIST_DIR%\tools" >nul 2>&1

xcopy "%REPOSITORY_ROOT%\osal" "%DIST_DIR%\osal\" /E /I /Y >nul
if errorlevel 1 (
    echo !C_ERR![ERR ] Failed to copy OSAL templates into the distribution.!C_RESET!
    exit /b !ERRORLEVEL!
)

xcopy "%REPOSITORY_ROOT%\doc" "%DIST_DIR%\doc\" /E /I /Y >nul
if errorlevel 1 (
    echo !C_ERR![ERR ] Failed to copy GUI artwork into the distribution.!C_RESET!
    exit /b !ERRORLEVEL!
)

copy /Y "%GENERATOR_ROOT%\kiwicgen-clang-format.yaml" "%DIST_DIR%\kiwicgen-clang-format.yaml" >nul
copy /Y "%VENV_CLANG_FORMAT%" "%DIST_DIR%\tools\clang-format.exe" >nul
copy /Y "%REPOSITORY_ROOT%\README.md" "%DIST_DIR%\README.md" >nul
copy /Y "%GENERATOR_ROOT%\README.md" "%DIST_DIR%\kiwicgen-README.md" >nul
copy /Y "%REPOSITORY_ROOT%\LICENSE" "%DIST_DIR%\LICENSE" >nul

echo !C_OK![ OK ] Runtime resources staged.!C_RESET!

rem ---------------------------------------------------------------------------
rem Verify the final distribution, not an intermediate PyInstaller artifact.
rem ---------------------------------------------------------------------------
set "CLI_EXECUTABLE=%DIST_DIR%\kiwicgen.exe"
set "GUI_EXECUTABLE=%DIST_DIR%\kiwicgen-gui.exe"

if not exist "%CLI_EXECUTABLE%" (
    echo !C_ERR![ERR ] Expected executable was not created: %CLI_EXECUTABLE%!C_RESET!
    exit /b 1
)
if not exist "%GUI_EXECUTABLE%" (
    echo !C_ERR![ERR ] Expected executable was not created: %GUI_EXECUTABLE%!C_RESET!
    exit /b 1
)
if not exist "%DIST_DIR%\osal\template_osal.h" (
    echo !C_ERR![ERR ] OSAL templates are missing from the final distribution.!C_RESET!
    exit /b 1
)
if not exist "%DIST_DIR%\tools\clang-format.exe" (
    echo !C_ERR![ERR ] clang-format is missing from the final distribution.!C_RESET!
    exit /b 1
)

echo !C_STEP![STEP] Verifying kiwicgen executable!C_RESET!
"%CLI_EXECUTABLE%" --version
if errorlevel 1 (
    echo !C_ERR![ERR ] kiwicgen executable verification failed.!C_RESET!
    exit /b !ERRORLEVEL!
)

echo !C_OK![ OK ] Standalone distribution created: %DIST_DIR%!C_RESET!
exit /b 0
