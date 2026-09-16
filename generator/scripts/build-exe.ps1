$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Resolve project paths and parse build parameters.
# ---------------------------------------------------------------------------
$GeneratorRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RepositoryRoot = (Resolve-Path (Join-Path $GeneratorRoot "..")).Path
$BuildDir = Join-Path $RepositoryRoot "build"
$DistDir = Join-Path $RepositoryRoot "dist"
$RequestedPython = $null

for ($i = 0; $i -lt $args.Count; $i++) {
    switch ($args[$i]) {
        "--python" {
            if ($i + 1 -ge $args.Count) {
                throw "Missing value for --python."
            }

            $RequestedPython = $args[++$i]
        }

        "--build-dir" {
            if ($i + 1 -ge $args.Count) {
                throw "Missing value for --build-dir."
            }

            $BuildDir = $args[++$i]
        }

        "--dist-dir" {
            if ($i + 1 -ge $args.Count) {
                throw "Missing value for --dist-dir."
            }

            $DistDir = $args[++$i]
        }

        default {
            throw "Unknown argument: $($args[$i])"
        }
    }
}

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$DistDir = [System.IO.Path]::GetFullPath($DistDir)
$VenvDir = Join-Path $BuildDir "venv"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
$VenvClangFormat = Join-Path $VenvDir "Scripts\clang-format.exe"
$SpecDir = Join-Path $BuildDir "spec"
$PackageSourceDir = Join-Path $BuildDir "package-source"

# ---------------------------------------------------------------------------
# Centralized colored status output.
# ---------------------------------------------------------------------------
function Write-Info([string]$Message) {
    Write-Host "[INFO] $Message" -ForegroundColor White
}

function Write-Step([string]$Message) {
    Write-Host "[STEP] $Message" -ForegroundColor Cyan
}

function Write-Ok([string]$Message) {
    Write-Host "[ OK ] $Message" -ForegroundColor Green
}

function Write-Warn([string]$Message) {
    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}

function Write-Err([string]$Message) {
    Write-Host "[ERR ] $Message" -ForegroundColor Red
}

# ---------------------------------------------------------------------------
# Probe one concrete Python executable.
# Only Python >= 3.10 is accepted here. The venv capability is validated later
# by the actual `python -m venv` command.
# ---------------------------------------------------------------------------
function Get-PythonInfo([string]$Executable) {
    if (-not (Test-Path $Executable)) {
        return $null
    }

    $PreviousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    try {
        $Probe = & $Executable -c "import sys; print(sys.executable); print('%d.%d.%d' % sys.version_info[:3])" 2>$null

        if ($LASTEXITCODE -ne 0 -or $null -eq $Probe) {
            return $null
        }

        $Lines = @($Probe)

        if ($Lines.Count -lt 2) {
            return $null
        }

        $ResolvedExecutable = "$($Lines[0])".Trim()
        $Version = "$($Lines[1])".Trim()

        if ([string]::IsNullOrWhiteSpace($ResolvedExecutable) -or
            [string]::IsNullOrWhiteSpace($Version)) {
            return $null
        }

        $VersionParts = $Version.Split(".")

        if ($VersionParts.Count -lt 2) {
            return $null
        }

        $Major = 0
        $Minor = 0

        if (-not [int]::TryParse($VersionParts[0], [ref]$Major) -or
            -not [int]::TryParse($VersionParts[1], [ref]$Minor)) {
            return $null
        }

        if (($Major -lt 3) -or (($Major -eq 3) -and ($Minor -lt 10))) {
            return $null
        }

        return [PSCustomObject]@{
            Executable = [System.IO.Path]::GetFullPath($ResolvedExecutable)
            Version = $Version
        }
    }
    catch {
        return $null
    }
    finally {
        $ErrorActionPreference = $PreviousPreference
    }
}

# ---------------------------------------------------------------------------
# Discover installed Python runtimes exactly like the reference utility:
# 1. Capture every interpreter registered with the Windows Python launcher.
# 2. Add every python/python3 application visible through PATH.
# 3. Probe, de-duplicate by concrete executable and sort the result.
# ---------------------------------------------------------------------------
function Find-PythonRuntimes {
    $ExecutablePaths = @()

    if (Get-Command py -ErrorAction SilentlyContinue) {
        $PreviousPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"

        try {
            # Python Launcher may print the interpreter list to stderr. Capture
            # both streams and accept the optional '*' default-runtime marker.
            $LauncherOutput = & py -0p 2>&1

            foreach ($Item in $LauncherOutput) {
                $Line = "$Item"

                if ($Line -match '^\s*-V:[^\s]+\s+\*?\s*(.+?python\.exe)\s*$') {
                    $ExecutablePaths += $matches[1].Trim()
                }
            }
        }
        finally {
            $ErrorActionPreference = $PreviousPreference
        }
    }

    foreach ($Name in @("python", "python3")) {
        $Commands = Get-Command $Name -All -ErrorAction SilentlyContinue

        foreach ($Command in $Commands) {
            if ($Command.CommandType -eq "Application") {
                $ExecutablePaths += $Command.Source
            }
        }
    }

    $Candidates = @()

    foreach ($Path in ($ExecutablePaths | Sort-Object -Unique)) {
        Write-Info "Checking Python runtime: $Path"
        $Info = Get-PythonInfo $Path

        if ($null -ne $Info) {
            $Candidates += $Info
        }
        else {
            Write-Warn "Python runtime probe failed: $Path"
        }
    }

    return $Candidates |
        Group-Object -Property Executable |
        ForEach-Object { $_.Group[0] } |
        Sort-Object -Property Version, Executable
}

# ---------------------------------------------------------------------------
# Resolve the Python runtime selected for the build.
# --python disables interactive selection and is intended for CI/automation.
# ---------------------------------------------------------------------------
function Select-PythonRuntime {
    if (-not [string]::IsNullOrWhiteSpace($RequestedPython)) {
        $RequestedPath = [System.IO.Path]::GetFullPath($RequestedPython)
        $Info = Get-PythonInfo $RequestedPath

        if ($null -eq $Info) {
            Write-Err "The requested Python runtime is unavailable or unsupported: $RequestedPath"
            exit 1
        }

        return $Info
    }

    Write-Step "Searching for installed Python runtimes"
    Write-Info "Primary discovery source: Windows Python launcher (py -0p)"
    $Candidates = @(Find-PythonRuntimes)

    if ($Candidates.Count -eq 0) {
        Write-Err "No supported Python runtime was found."
        Write-Info "Python 3.10 or newer with the venv module is required to build kiwicgen."
        exit 1
    }

    Write-Info "Found $($Candidates.Count) supported Python runtime(s):"

    for ($i = 0; $i -lt $Candidates.Count; $i++) {
        Write-Host "  [$($i + 1)] Python $($Candidates[$i].Version) - $($Candidates[$i].Executable)"
    }

    if ($Candidates.Count -eq 1) {
        Write-Info "Using the only available Python runtime."
        return $Candidates[0]
    }

    do {
        $Selection = Read-Host "Select Python for executable build [1-$($Candidates.Count)]"
        $SelectionIndex = 0
        $Valid = [int]::TryParse($Selection, [ref]$SelectionIndex) -and
                 ($SelectionIndex -ge 1) -and
                 ($SelectionIndex -le $Candidates.Count)

        if (-not $Valid) {
            Write-Warn "Invalid selection."
        }
    } while (-not $Valid)

    return $Candidates[$SelectionIndex - 1]
}

# ---------------------------------------------------------------------------
# Reuse the build venv only if it was created from the selected Python.
# Otherwise it is a build artifact and can safely be recreated.
# ---------------------------------------------------------------------------
function Prepare-BuildEnvironment([object]$Python) {
    if (Test-Path $VenvPython) {
        $PreviousPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"

        try {
            $BaseExecutable = & $VenvPython -c "import sys; print(sys._base_executable)" 2>$null
        }
        finally {
            $ErrorActionPreference = $PreviousPreference
        }

        $Expected = [System.IO.Path]::GetFullPath($Python.Executable)

        if (($LASTEXITCODE -ne 0) -or
            [string]::IsNullOrWhiteSpace($BaseExecutable) -or
            ([System.IO.Path]::GetFullPath($BaseExecutable.Trim()) -ne $Expected)) {

            Write-Warn "Existing build virtual environment belongs to another Python runtime."
            Write-Step "Recreating build virtual environment"
            Remove-Item -Recurse -Force $VenvDir
        }
    }

    if (-not (Test-Path $VenvPython)) {
        Write-Step "Creating isolated build virtual environment"
        Write-Info "Virtual environment: $VenvDir"

        & $Python.Executable -m venv $VenvDir

        if ($LASTEXITCODE -ne 0) {
            Write-Err "Failed to create the build virtual environment."
            exit $LASTEXITCODE
        }

        Write-Ok "Build virtual environment created."
    }
    else {
        Write-Info "Reusing build virtual environment: $VenvDir"
    }

    & $VenvPython -m pip --version *> $null

    if ($LASTEXITCODE -ne 0) {
        Write-Warn "pip is not available in the build virtual environment."
        Write-Step "Bootstrapping pip with ensurepip"

        & $VenvPython -m ensurepip --upgrade

        if ($LASTEXITCODE -ne 0) {
            Write-Err "Unable to bootstrap pip in the build virtual environment."
            exit $LASTEXITCODE
        }
    }
}

$SelectedPython = Select-PythonRuntime

Write-Step "Building standalone kiwicgen distribution"
Write-Info "Generator root: $GeneratorRoot"
Write-Info "Build directory: $BuildDir"
Write-Info "Distribution directory: $DistDir"
Write-Info "Selected Python: $($SelectedPython.Version) - $($SelectedPython.Executable)"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
New-Item -ItemType Directory -Force -Path $SpecDir | Out-Null

Prepare-BuildEnvironment $SelectedPython

# ---------------------------------------------------------------------------
# Stage Python packaging input inside build/ before invoking setuptools. This
# keeps egg-info and every other temporary packaging artifact out of sources.
# pyproject.toml remains the single dependency/version metadata source.
# ---------------------------------------------------------------------------
Write-Step "Preparing isolated packaging source"
if (Test-Path $PackageSourceDir) {
    Remove-Item -Recurse -Force $PackageSourceDir
}
New-Item -ItemType Directory -Force -Path $PackageSourceDir | Out-Null
Copy-Item (Join-Path $GeneratorRoot "pyproject.toml") $PackageSourceDir
foreach ($Package in @("core", "formatter", "cli", "gui")) {
    Copy-Item -Recurse -Force (Join-Path $GeneratorRoot $Package) (Join-Path $PackageSourceDir $Package)
}
Write-Ok "Packaging source staged inside build directory."

# ---------------------------------------------------------------------------
# Install runtime and executable-build dependencies only into build/venv.
# Nothing is installed into any system Python runtime or generated in sources.
# ---------------------------------------------------------------------------
Write-Step "Installing executable build dependencies"
Push-Location $PackageSourceDir
try {
    & $VenvPython -m pip install ".[executable]"

    if ($LASTEXITCODE -ne 0) {
        Write-Err "Failed to install executable build dependencies."
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
Write-Ok "Executable build dependencies installed."

if (-not (Test-Path $VenvClangFormat)) {
    Write-Err "clang-format was not installed into the build environment: $VenvClangFormat"
    exit 1
}

# ---------------------------------------------------------------------------
# Read the release version from the single generator version source.
# ---------------------------------------------------------------------------
$ProjectVersion = & $VenvPython -c `
    "import sys; sys.path.insert(0, r'$GeneratorRoot'); from core.version import __version__; print(__version__)"

if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($ProjectVersion)) {
    Write-Err "Unable to read kiwicgen version from core/version.py."
    exit 1
}

$ProjectVersion = $ProjectVersion.Trim()
$VersionMatch = [regex]::Match($ProjectVersion, '^([0-9]+)\.([0-9]+)\.([0-9]+)$')

if (-not $VersionMatch.Success) {
    Write-Err "Invalid kiwicgen version: $ProjectVersion"
    exit 1
}

$VersionMajor = [int]$VersionMatch.Groups[1].Value
$VersionMinor = [int]$VersionMatch.Groups[2].Value
$VersionPatch = [int]$VersionMatch.Groups[3].Value
Write-Info "kiwicgen version: $ProjectVersion"

function Write-VersionInfo([string]$Path, [string]$Description, [string]$InternalName, [string]$OriginalFilename) {
    $Content = @"
VSVersionInfo(
  ffi=FixedFileInfo(
    filevers=($VersionMajor, $VersionMinor, $VersionPatch, 0),
    prodvers=($VersionMajor, $VersionMinor, $VersionPatch, 0),
    mask=0x3f,
    flags=0x0,
    OS=0x40004,
    fileType=0x1,
    subtype=0x0,
    date=(0, 0)
  ),
  kids=[
    StringFileInfo([
      StringTable(u'040904B0', [
        StringStruct(u'CompanyName', u''),
        StringStruct(u'FileDescription', u'$Description'),
        StringStruct(u'FileVersion', u'$ProjectVersion'),
        StringStruct(u'InternalName', u'$InternalName'),
        StringStruct(u'OriginalFilename', u'$OriginalFilename'),
        StringStruct(u'ProductName', u'kiwicgen'),
        StringStruct(u'ProductVersion', u'$ProjectVersion')
      ])
    ]),
    VarFileInfo([VarStruct(u'Translation', [1033, 1200])])
  ]
)
"@
    Set-Content -Path $Path -Value $Content -Encoding UTF8
}

$CliVersionInfo = Join-Path $BuildDir "kiwicgen-version-info.txt"
$GuiVersionInfo = Join-Path $BuildDir "kiwicgen-gui-version-info.txt"
Write-VersionInfo $CliVersionInfo "KIWI Code Generator" "kiwicgen" "kiwicgen.exe"
Write-VersionInfo $GuiVersionInfo "KIWI Code Generator GUI" "kiwicgen-gui" "kiwicgen-gui.exe"

# ---------------------------------------------------------------------------
# Build both one-file frontends. Generated specs remain inside build/.
# ---------------------------------------------------------------------------
$Icon = Join-Path $RepositoryRoot "doc\kiwi.ico"

Write-Step "Building kiwicgen console executable"
& $VenvPython -m PyInstaller `
    --noconfirm `
    --clean `
    --onefile `
    --console `
    --name kiwicgen `
    --icon $Icon `
    --version-file $CliVersionInfo `
    --workpath (Join-Path $BuildDir "pyinstaller\kiwicgen") `
    --specpath $SpecDir `
    --distpath $DistDir `
    --paths $GeneratorRoot `
    (Join-Path $GeneratorRoot "cli\main.py")
if ($LASTEXITCODE -ne 0) {
    Write-Err "kiwicgen PyInstaller build failed."
    exit $LASTEXITCODE
}
Write-Ok "kiwicgen executable created."

Write-Step "Building kiwicgen GUI executable"
& $VenvPython -m PyInstaller `
    --noconfirm `
    --clean `
    --onefile `
    --windowed `
    --name kiwicgen-gui `
    --icon $Icon `
    --version-file $GuiVersionInfo `
    --workpath (Join-Path $BuildDir "pyinstaller\kiwicgen-gui") `
    --specpath $SpecDir `
    --distpath $DistDir `
    --paths $GeneratorRoot `
    (Join-Path $GeneratorRoot "gui\main.py")
if ($LASTEXITCODE -ne 0) {
    Write-Err "kiwicgen-gui PyInstaller build failed."
    exit $LASTEXITCODE
}
Write-Ok "kiwicgen-gui executable created."

# ---------------------------------------------------------------------------
# Stage every external runtime resource required by the standalone tools.
# ---------------------------------------------------------------------------
Write-Step "Staging standalone distribution resources"
foreach ($Path in @("templates", "doc", "tools")) {
    $Target = Join-Path $DistDir $Path
    if (Test-Path $Target) {
        Remove-Item -Recurse -Force $Target
    }
}
New-Item -ItemType Directory -Force -Path (Join-Path $DistDir "tools") | Out-Null
Copy-Item -Recurse -Force (Join-Path $GeneratorRoot "resources\templates") (Join-Path $DistDir "templates")
Copy-Item -Recurse -Force (Join-Path $RepositoryRoot "doc") (Join-Path $DistDir "doc")
Copy-Item -Force (Join-Path $GeneratorRoot "resources\kiwicgen-clang-format.yaml") (Join-Path $DistDir "kiwicgen-clang-format.yaml")
Copy-Item -Force $VenvClangFormat (Join-Path $DistDir "tools\clang-format.exe")
Copy-Item -Force (Join-Path $RepositoryRoot "README.md") (Join-Path $DistDir "README.md")
Copy-Item -Force (Join-Path $GeneratorRoot "README.md") (Join-Path $DistDir "kiwicgen-README.md")
Copy-Item -Force (Join-Path $RepositoryRoot "LICENSE") (Join-Path $DistDir "LICENSE")
Write-Ok "Runtime resources staged."

# ---------------------------------------------------------------------------
# Verify the final distribution, not an intermediate PyInstaller artifact.
# ---------------------------------------------------------------------------
$CliExecutable = Join-Path $DistDir "kiwicgen.exe"
$GuiExecutable = Join-Path $DistDir "kiwicgen-gui.exe"
$TemplateProbe = Join-Path $DistDir "templates\osal\template_osal.h"
$FormatterProbe = Join-Path $DistDir "tools\clang-format.exe"

foreach ($Path in @($CliExecutable, $GuiExecutable, $TemplateProbe, $FormatterProbe)) {
    if (-not (Test-Path $Path)) {
        Write-Err "Expected distribution artifact is missing: $Path"
        exit 1
    }
}

Write-Step "Verifying kiwicgen executable"
& $CliExecutable --version
if ($LASTEXITCODE -ne 0) {
    Write-Err "kiwicgen executable verification failed."
    exit $LASTEXITCODE
}

Write-Step "Running standalone generation smoke test"
$SmokeDir = Join-Path $BuildDir "smoke-generated"
if (Test-Path $SmokeDir) {
    Remove-Item -Recurse -Force $SmokeDir
}
& $CliExecutable `
    --module-prefix=SmokeModule `
    --port=FreeRTOS `
    --language=C `
    --use-thread-api `
    --output=$SmokeDir `
    --no-color
if ($LASTEXITCODE -ne 0) {
    Write-Err "Standalone generation smoke test failed."
    exit $LASTEXITCODE
}
$SmokeProbe = Join-Path $SmokeDir "smoke_module\smoke_module_osal.h"
if (-not (Test-Path $SmokeProbe)) {
    Write-Err "Smoke-test artifact is missing: $SmokeProbe"
    exit 1
}
Write-Ok "Standalone generation smoke test passed."

Write-Ok "Standalone distribution created: $DistDir"
