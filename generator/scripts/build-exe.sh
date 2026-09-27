#!/usr/bin/env sh
set -eu

# ---------------------------------------------------------------------------
# Resolve project paths and parse build parameters.
# ---------------------------------------------------------------------------
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GENERATOR_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
REPOSITORY_ROOT=$(CDPATH= cd -- "$GENERATOR_ROOT/.." && pwd)
BUILD_DIR="$REPOSITORY_ROOT/build"
DIST_DIR="$REPOSITORY_ROOT/dist"
REQUESTED_PYTHON=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --python)
            [ "$#" -ge 2 ] || { echo "[ERR ] Missing value for --python." >&2; exit 2; }
            REQUESTED_PYTHON=$2
            shift 2
            ;;
        --build-dir)
            [ "$#" -ge 2 ] || { echo "[ERR ] Missing value for --build-dir." >&2; exit 2; }
            BUILD_DIR=$2
            shift 2
            ;;
        --dist-dir)
            [ "$#" -ge 2 ] || { echo "[ERR ] Missing value for --dist-dir." >&2; exit 2; }
            DIST_DIR=$2
            shift 2
            ;;
        *)
            echo "[ERR ] Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

VENV_DIR="$BUILD_DIR/venv"
VENV_PYTHON="$VENV_DIR/bin/python"
VENV_CLANG_FORMAT="$VENV_DIR/bin/clang-format"
SPEC_DIR="$BUILD_DIR/spec"
PACKAGE_SOURCE_DIR="$BUILD_DIR/package-source"

# ---------------------------------------------------------------------------
# Colored status output. Honor NO_COLOR and disable colors for redirected
# output.
# ---------------------------------------------------------------------------
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_RESET='\033[0m'
    C_INFO='\033[97m'
    C_STEP='\033[96m'
    C_OK='\033[92m'
    C_WARN='\033[93m'
    C_ERR='\033[91m'
else
    C_RESET=''
    C_INFO=''
    C_STEP=''
    C_OK=''
    C_WARN=''
    C_ERR=''
fi

info() { printf "${C_INFO}[INFO] %s${C_RESET}\n" "$*"; }
step() { printf "${C_STEP}[STEP] %s${C_RESET}\n" "$*"; }
ok()   { printf "${C_OK}[ OK ] %s${C_RESET}\n" "$*"; }
warn() { printf "${C_WARN}[WARN] %s${C_RESET}\n" "$*"; }
err()  { printf "${C_ERR}[ERR ] %s${C_RESET}\n" "$*" >&2; }

# ---------------------------------------------------------------------------
# Validate one Python executable and print "version|executable" on success.
# The actual venv capability is validated later by `python -m venv`.
# ---------------------------------------------------------------------------
probe_python() {
    candidate=$1

    "$candidate" -c '
import sys

if sys.version_info < (3, 10):
    raise SystemExit(1)

print("%d.%d.%d|%s" % (*sys.version_info[:3], sys.executable))
' 2>/dev/null
}

# ---------------------------------------------------------------------------
# Discover every Python command visible through PATH. Versioned python3.x
# commands are included so parallel installations remain selectable.
# ---------------------------------------------------------------------------
find_pythons() {
    tmp=$1
    : > "$tmp"

    for name in \
        python3 python \
        python3.10 python3.11 python3.12 python3.13 python3.14 \
        python3.15 python3.16 python3.17 python3.18 python3.19 python3.20
    do
        if command -v "$name" >/dev/null 2>&1; then
            exe=$(command -v "$name")
            result=$(probe_python "$exe" || true)

            if [ -n "$result" ]; then
                printf '%s\n' "$result" >> "$tmp"
            fi
        fi
    done

    awk -F'|' '!seen[$2]++' "$tmp" | sort > "$tmp.unique"
    mv "$tmp.unique" "$tmp"
}

# ---------------------------------------------------------------------------
# Select a Python runtime.
# ---------------------------------------------------------------------------
if [ -n "$REQUESTED_PYTHON" ]; then
    SELECTED_INFO=$(probe_python "$REQUESTED_PYTHON" || true)

    if [ -z "$SELECTED_INFO" ]; then
        err "The requested Python runtime is unavailable or unsupported: $REQUESTED_PYTHON"
        exit 1
    fi
else
    step "Searching for installed Python runtimes"
    CANDIDATES=$(mktemp)
    trap 'rm -f "$CANDIDATES"' EXIT HUP INT TERM
    find_pythons "$CANDIDATES"
    COUNT=$(wc -l < "$CANDIDATES" | tr -d ' ')

    if [ "$COUNT" -eq 0 ]; then
        err "No supported Python runtime was found."
        info "Python 3.10 or newer with the venv module is required to build kiwicgen."
        exit 1
    fi

    info "Found $COUNT supported Python runtime(s):"
    i=1
    while IFS='|' read -r version executable; do
        printf '  [%s] Python %s - %s\n' "$i" "$version" "$executable"
        i=$((i + 1))
    done < "$CANDIDATES"

    if [ "$COUNT" -eq 1 ]; then
        SELECT=1
        info "Using the only available Python runtime."
    else
        while :; do
            printf 'Select Python for executable build [1-%s]: ' "$COUNT"
            IFS= read -r SELECT

            case "$SELECT" in
                ''|*[!0-9]*)
                    warn "Invalid selection."
                    ;;
                *)
                    if [ "$SELECT" -ge 1 ] && [ "$SELECT" -le "$COUNT" ]; then
                        break
                    fi
                    warn "Invalid selection."
                    ;;
            esac
        done
    fi

    SELECTED_INFO=$(sed -n "${SELECT}p" "$CANDIDATES")
fi

SELECTED_VERSION=$(printf '%s' "$SELECTED_INFO" | cut -d'|' -f1)
SELECTED_PYTHON=$(printf '%s' "$SELECTED_INFO" | cut -d'|' -f2-)

step "Building standalone kiwicgen distribution"
info "Generator root: $GENERATOR_ROOT"
info "Build directory: $BUILD_DIR"
info "Distribution directory: $DIST_DIR"
info "Selected Python: $SELECTED_VERSION - $SELECTED_PYTHON"

mkdir -p "$BUILD_DIR" "$DIST_DIR" "$SPEC_DIR"

# ---------------------------------------------------------------------------
# Recreate build/venv when it belongs to another Python runtime.
# ---------------------------------------------------------------------------
if [ -x "$VENV_PYTHON" ]; then
    BASE_PYTHON=$("$VENV_PYTHON" -c 'import sys; print(sys._base_executable)' 2>/dev/null || true)
    if [ "$BASE_PYTHON" != "$SELECTED_PYTHON" ]; then
        warn "Existing build virtual environment belongs to another Python runtime."
        step "Recreating build virtual environment"
        rm -rf "$VENV_DIR"
    fi
fi

if [ ! -x "$VENV_PYTHON" ]; then
    step "Creating isolated build virtual environment"
    info "Virtual environment: $VENV_DIR"
    "$SELECTED_PYTHON" -m venv "$VENV_DIR"
    ok "Build virtual environment created."
else
    info "Reusing build virtual environment: $VENV_DIR"
fi

if ! "$VENV_PYTHON" -m pip --version >/dev/null 2>&1; then
    warn "pip is not available in the build virtual environment."
    step "Bootstrapping pip with ensurepip"
    "$VENV_PYTHON" -m ensurepip --upgrade
fi

# ---------------------------------------------------------------------------
# Stage packaging input in build/ so setuptools cannot create egg-info beside
# source files. pyproject.toml remains the dependency metadata source.
# ---------------------------------------------------------------------------
step "Preparing isolated packaging source"
rm -rf "$PACKAGE_SOURCE_DIR"
mkdir -p "$PACKAGE_SOURCE_DIR"
cp "$GENERATOR_ROOT/pyproject.toml" "$PACKAGE_SOURCE_DIR/pyproject.toml"
for package in core formatter cli gui; do
    cp -R "$GENERATOR_ROOT/$package" "$PACKAGE_SOURCE_DIR/$package"
done
ok "Packaging source staged inside build directory."

step "Installing executable build dependencies"
(
    cd "$PACKAGE_SOURCE_DIR"
    "$VENV_PYTHON" -m pip install '.[executable]'
)
ok "Executable build dependencies installed."

if [ ! -x "$VENV_CLANG_FORMAT" ]; then
    err "clang-format was not installed into the build environment: $VENV_CLANG_FORMAT"
    exit 1
fi

PROJECT_VERSION=$(PYTHONPATH="$GENERATOR_ROOT" "$VENV_PYTHON" -c 'from core.version import __version__; print(__version__)')
if [ -z "$PROJECT_VERSION" ]; then
    err "Unable to read kiwicgen version from core/version.py."
    exit 1
fi
info "kiwicgen version: $PROJECT_VERSION"

# ---------------------------------------------------------------------------
# Build both one-file frontends. Generated specs remain inside build/.
# ---------------------------------------------------------------------------
step "Building kiwicgen console executable"
"$VENV_PYTHON" -m PyInstaller \
    --noconfirm \
    --clean \
    --onefile \
    --console \
    --name kiwicgen \
    --workpath "$BUILD_DIR/pyinstaller/kiwicgen" \
    --specpath "$SPEC_DIR" \
    --distpath "$DIST_DIR" \
    --paths "$GENERATOR_ROOT" \
    "$GENERATOR_ROOT/cli/main.py"
ok "kiwicgen executable created."

step "Building kiwicgen GUI executable"
"$VENV_PYTHON" -m PyInstaller \
    --noconfirm \
    --clean \
    --onefile \
    --windowed \
    --name kiwicgen-gui \
    --workpath "$BUILD_DIR/pyinstaller/kiwicgen-gui" \
    --specpath "$SPEC_DIR" \
    --distpath "$DIST_DIR" \
    --paths "$GENERATOR_ROOT" \
    "$GENERATOR_ROOT/gui/main.py"
ok "kiwicgen-gui executable created."

# ---------------------------------------------------------------------------
# Stage every external runtime resource required by the standalone tools.
# ---------------------------------------------------------------------------
step "Staging standalone distribution resources"
rm -rf "$DIST_DIR/templates" "$DIST_DIR/doc" "$DIST_DIR/tools"
mkdir -p "$DIST_DIR/tools"
cp -R "$GENERATOR_ROOT/resources/templates" "$DIST_DIR/templates"
cp -R "$REPOSITORY_ROOT/doc" "$DIST_DIR/doc"
cp "$GENERATOR_ROOT/resources/kiwicgen-clang-format.yaml" "$DIST_DIR/kiwicgen-clang-format.yaml"
cp "$VENV_CLANG_FORMAT" "$DIST_DIR/tools/clang-format"
cp "$REPOSITORY_ROOT/README.md" "$DIST_DIR/README.md"
cp "$REPOSITORY_ROOT/doc/kiwicgen.md" "$DIST_DIR/kiwicgen-README.md"
cp "$REPOSITORY_ROOT/LICENSE" "$DIST_DIR/LICENSE"
chmod +x "$DIST_DIR/tools/clang-format"
ok "Runtime resources staged."

# ---------------------------------------------------------------------------
# Verify the final distribution.
# ---------------------------------------------------------------------------
CLI_EXECUTABLE="$DIST_DIR/kiwicgen"
GUI_EXECUTABLE="$DIST_DIR/kiwicgen-gui"
for path in \
    "$CLI_EXECUTABLE" \
    "$GUI_EXECUTABLE" \
    "$DIST_DIR/templates/osal/template_osal.h" \
    "$DIST_DIR/tools/clang-format"
do
    if [ ! -e "$path" ]; then
        err "Expected distribution artifact is missing: $path"
        exit 1
    fi
done

step "Verifying kiwicgen executable"
"$CLI_EXECUTABLE" --version

step "Running standalone generation smoke test"
SMOKE_DIR="$BUILD_DIR/smoke-generated"
rm -rf "$SMOKE_DIR"
"$CLI_EXECUTABLE" \
    --module-prefix=SmokeModule \
    --port=FreeRTOS \
    --language=C \
    --use-thread-api \
    --output="$SMOKE_DIR" \
    --no-color
SMOKE_PROBE="$SMOKE_DIR/smoke_module/smoke_module_osal.h"
if [ ! -f "$SMOKE_PROBE" ]; then
    err "Smoke-test artifact is missing: $SMOKE_PROBE"
    exit 1
fi
ok "Standalone generation smoke test passed."

ok "Standalone distribution created: $DIST_DIR"
