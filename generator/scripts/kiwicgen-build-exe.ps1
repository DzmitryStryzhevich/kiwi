$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Resolve project paths and parse build parameters.
# ---------------------------------------------------------------------------
$GeneratorRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RepositoryRoot = (Resolve-Path (Join-Path $GeneratorRoot "..")).Path
$BuildDir = Join-Path $GeneratorRoot "build"
$DistDir = Join-Path $GeneratorRoot "dist"
$RequestedPython = $null

for ($i = 0; $i -lt $args.Count; $i++) {
    switch ($args[$i]) {
        "--python" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --python." }
            $RequestedPython = $args[++$i]
        }
        "--build-dir" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --build-dir." }
            $BuildDir = $args[++$i]
        }
        "--dist-dir" {
            if ($i + 1 -ge $args.Count) { throw "Missing value for --dist-dir." }
            $DistDir = $args[++$i]
        }
        default { throw "Unknown argument: $($args[$i])" }
    }
}

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$DistDir = [System.IO.Path]::GetFullPath($DistDir)
$VenvDir = Join-Path $BuildDir "venv"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
$VenvClangFormat = Join-Path $VenvDir "Scripts\clang-format.exe"
$SpecDir = Join-Path $BuildDir "spec"

# ---------------------------------------------------------------------------
# Centralized colored status output.
# ---------------------------------------------------------------------------
function Write-Info([string]$Message) { Write-Host "[INFO] $Message" -ForegroundColor White }
function Write-Step([string]$Message) { Write-Host "[STEP] $Message" -ForegroundColor Cyan }
function Write-Ok([string]$Message) { Write-Host "[ OK ] $Message" -ForegroundColor Green }
function Write-WarnLine([string]$Message) { Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Err([string]$Message) { Write-Host "[ERR ] $Message" -ForegroundColor Red }

function Test-PythonRuntime([string]$PythonPath) {
    try {
        $output = & $PythonPath -c "import platform, sys, venv; sys.exit(1) if sys.version_info < (3,10) else None; print(platform.python_version()); print(sys.executable)" 2>$null
        if ($LASTEXITCODE -ne 0 -or $output.Count -lt 2) { return $null }
        return [PSCustomObject]@{ Version = $output[0]; Path = $output[1] }
    }
    catch { return $null }
}

# ---------------------------------------------------------------------------
# Select a Python runtime.
# ---------------------------------------------------------------------------
if ($RequestedPython) {
    $Selected = Test-PythonRuntime $RequestedPython
    if (-not $Selected) {
        Write-Err "Requested Python runtime is unavailable or unsupported: $RequestedPython"
        exit 1
    }
}
else {
    Write-Step "Searching for installed Python runtimes"
    $Candidates = @()
    $Seen = @{}

    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        foreach ($line in (& py -0p 2>$null)) {
            if ($line -match '^\s*-V:3[^\s]*\s+(.+)$') {
                $candidate = $Matches[1].Trim()
                $probe = Test-PythonRuntime $candidate
                if ($probe -and -not $Seen.ContainsKey($probe.Path.ToLowerInvariant())) {
                    $Seen[$probe.Path.ToLowerInvariant()] = $true
                    $Candidates += $probe
                }
            }
        }
    }

    if ($Candidates.Count -eq 0) {
        foreach ($name in @("python", "python3")) {
            foreach ($command in @(Get-Command $name -All -ErrorAction SilentlyContinue)) {
                $probe = Test-PythonRuntime $command.Source
                if ($probe -and -not $Seen.ContainsKey($probe.Path.ToLowerInvariant())) {
                    $Seen[$probe.Path.ToLowerInvariant()] = $true
                    $Candidates += $probe
                }
            }
        }
    }

    if ($Candidates.Count -eq 0) {
        Write-Err "No supported Python runtime was found."
        Write-Info "Python 3.10 or newer with the venv module is required."
        exit 1
    }

    Write-Info "Found $($Candidates.Count) supported Python runtime(s):"
    for ($i = 0; $i -lt $Candidates.Count; $i++) {
        Write-Host "  [$($i + 1)] Python $($Candidates[$i].Version) - $($Candidates[$i].Path)"
    }

    if ($Candidates.Count -eq 1) {
        $Selected = $Candidates[0]
        Write-Info "Using the only available Python runtime."
    }
    else {
        $index = 0
        do {
            $choice = Read-Host "Select Python for executable build [1-$($Candidates.Count)]"
            $valid = [int]::TryParse($choice, [ref]$index) -and $index -ge 1 -and $index -le $Candidates.Count
            if (-not $valid) { Write-WarnLine "Invalid selection." }
        } until ($valid)
        $Selected = $Candidates[$index - 1]
    }
}

$SelectedPython = $Selected.Path
$SelectedVersion = $Selected.Version

Write-Step "Building standalone kiwicgen distribution"
Write-Info "Generator root: $GeneratorRoot"
Write-Info "Build directory: $BuildDir"
Write-Info "Distribution directory: $DistDir"
Write-Info "Selected Python: $SelectedVersion - $SelectedPython"

New-Item -ItemType Directory -Force -Path $BuildDir, $DistDir, $SpecDir | Out-Null

# ---------------------------------------------------------------------------
# Recreate build venv if it belongs to another Python runtime.
# ---------------------------------------------------------------------------
if (Test-Path $VenvPython) {
    $VenvBase = (& $VenvPython -c "import sys; print(sys._base_executable)" 2>$null).Trim()
    if (-not [string]::Equals($VenvBase, $SelectedPython, [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-WarnLine "Existing build virtual environment belongs to another Python runtime."
        Write-Step "Recreating build virtual environment"
        Remove-Item -Recurse -Force $VenvDir
    }
}

if (-not (Test-Path $VenvPython)) {
    Write-Step "Creating isolated build virtual environment"
    Write-Info "Virtual environment: $VenvDir"
    & $SelectedPython -m venv $VenvDir
    if ($LASTEXITCODE -ne 0) { Write-Err "Failed to create the build virtual environment."; exit $LASTEXITCODE }
    Write-Ok "Build virtual environment created."
}
else {
    Write-Info "Reusing build virtual environment: $VenvDir"
}

if (-not (& $VenvPython -m pip --version 2>$null)) {
    Write-WarnLine "pip is not available in the build virtual environment."
    Write-Step "Bootstrapping pip with ensurepip"
    & $VenvPython -m ensurepip --upgrade
}

Write-Step "Installing executable build dependencies"
Push-Location $GeneratorRoot
try {
    & $VenvPython -m pip install ".[executable]"
    if ($LASTEXITCODE -ne 0) { throw "pip install failed with exit code $LASTEXITCODE" }
}
finally { Pop-Location }
Write-Ok "Executable build dependencies installed."

if (-not (Test-Path $VenvClangFormat)) {
    Write-Err "clang-format was not installed into the build environment: $VenvClangFormat"
    exit 1
}

# ---------------------------------------------------------------------------
# Read version and generate Windows PE version resources.
# ---------------------------------------------------------------------------
$ProjectVersion = (& $VenvPython -c "import sys; sys.path.insert(0, r'$GeneratorRoot'); import kiwicgen_version; print(kiwicgen_version.__version__)").Trim()
if (-not $ProjectVersion) { Write-Err "Unable to read kiwicgen version from kiwicgen_version.py."; exit 1 }
$parts = $ProjectVersion.Split('.')
$major = [int]$parts[0]; $minor = [int]$parts[1]; $patch = [int]$parts[2]
Write-Info "kiwicgen version: $ProjectVersion"

function Write-VersionInfo([string]$Path, [string]$Description, [string]$InternalName, [string]$OriginalFilename) {
    $content = @"
VSVersionInfo(
  ffi=FixedFileInfo(
    filevers=($major, $minor, $patch, 0),
    prodvers=($major, $minor, $patch, 0),
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
    Set-Content -Path $Path -Value $content -Encoding UTF8
}

$CliVersionInfo = Join-Path $BuildDir "kiwicgen-version-info.txt"
$GuiVersionInfo = Join-Path $BuildDir "kiwicgen-gui-version-info.txt"
Write-VersionInfo $CliVersionInfo "KIWI Code Generator" "kiwicgen" "kiwicgen.exe"
Write-VersionInfo $GuiVersionInfo "KIWI Code Generator GUI" "kiwicgen-gui" "kiwicgen-gui.exe"

# ---------------------------------------------------------------------------
# Build both one-file frontends.
# ---------------------------------------------------------------------------
$Icon = Join-Path $RepositoryRoot "doc\kiwi.ico"

Write-Step "Building kiwicgen console executable"
& $VenvPython -m PyInstaller --noconfirm --clean --onefile --console --name kiwicgen --icon $Icon --version-file $CliVersionInfo --workpath (Join-Path $BuildDir "pyinstaller\kiwicgen") --specpath $SpecDir --distpath $DistDir (Join-Path $GeneratorRoot "kiwicgen_cli.py")
if ($LASTEXITCODE -ne 0) { Write-Err "kiwicgen PyInstaller build failed."; exit $LASTEXITCODE }
Write-Ok "kiwicgen executable created."

Write-Step "Building kiwicgen GUI executable"
& $VenvPython -m PyInstaller --noconfirm --clean --onefile --windowed --name kiwicgen-gui --icon $Icon --version-file $GuiVersionInfo --workpath (Join-Path $BuildDir "pyinstaller\kiwicgen-gui") --specpath $SpecDir --distpath $DistDir (Join-Path $GeneratorRoot "kiwicgen_gui.py")
if ($LASTEXITCODE -ne 0) { Write-Err "kiwicgen-gui PyInstaller build failed."; exit $LASTEXITCODE }
Write-Ok "kiwicgen-gui executable created."

# ---------------------------------------------------------------------------
# Stage external runtime resources required by the standalone distribution.
# ---------------------------------------------------------------------------
Write-Step "Staging standalone distribution resources"
foreach ($name in @("osal", "doc", "tools")) {
    $target = Join-Path $DistDir $name
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
}
New-Item -ItemType Directory -Force -Path (Join-Path $DistDir "tools") | Out-Null
Copy-Item -Recurse -Force (Join-Path $RepositoryRoot "osal") (Join-Path $DistDir "osal")
Copy-Item -Recurse -Force (Join-Path $RepositoryRoot "doc") (Join-Path $DistDir "doc")
Copy-Item -Force (Join-Path $GeneratorRoot "kiwicgen-clang-format.yaml") (Join-Path $DistDir "kiwicgen-clang-format.yaml")
Copy-Item -Force $VenvClangFormat (Join-Path $DistDir "tools\clang-format.exe")
Copy-Item -Force (Join-Path $RepositoryRoot "README.md") (Join-Path $DistDir "README.md")
Copy-Item -Force (Join-Path $GeneratorRoot "README.md") (Join-Path $DistDir "kiwicgen-README.md")
Copy-Item -Force (Join-Path $RepositoryRoot "LICENSE") (Join-Path $DistDir "LICENSE")
Write-Ok "Runtime resources staged."

# ---------------------------------------------------------------------------
# Verify the final distribution.
# ---------------------------------------------------------------------------
$CliExecutable = Join-Path $DistDir "kiwicgen.exe"
$GuiExecutable = Join-Path $DistDir "kiwicgen-gui.exe"
$TemplateProbe = Join-Path $DistDir "osal\template_osal.h"
$FormatterProbe = Join-Path $DistDir "tools\clang-format.exe"
foreach ($path in @($CliExecutable, $GuiExecutable, $TemplateProbe, $FormatterProbe)) {
    if (-not (Test-Path $path)) { Write-Err "Expected distribution artifact is missing: $path"; exit 1 }
}

Write-Step "Verifying kiwicgen executable"
& $CliExecutable --version
if ($LASTEXITCODE -ne 0) { Write-Err "kiwicgen executable verification failed."; exit $LASTEXITCODE }

Write-Ok "Standalone distribution created: $DistDir"
