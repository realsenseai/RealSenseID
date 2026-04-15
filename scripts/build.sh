#!/usr/bin/env bash
set -e

usage() {
    cat <<'EOF'
Build RealSenseID Host SDK (Release by default)
Usage: ./scripts/build.sh [options]
  --samples      Build C/C++/C#/Python samples
  --python       Build Python wrapper (rsid_py)
  --debug        Build Debug instead of Release (output to build-debug/)
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="Release"
CMAKE_OPTS=()

for arg in "$@"; do
    case "$arg" in
        --samples)      CMAKE_OPTS+=(-DRSID_SAMPLES=ON) ;;
        --python)       CMAKE_OPTS+=(-DRSID_PY=ON) ;;
        --debug)        BUILD_TYPE="Debug" ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $arg (try --help)"
            exit 1
            ;;
    esac
done

if ! command -v cmake >/dev/null; then
    echo "Error: cmake not found. Please install CMake and ensure it is on your PATH."
    exit 1
fi

BUILD_DIR="$PROJECT_DIR/build"
if [ "$BUILD_TYPE" = "Debug" ]; then
    BUILD_DIR="$PROJECT_DIR/build-debug"
fi

echo "=== RealSenseID Host SDK ==="
echo "Build type: $BUILD_TYPE"
echo ""

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DRSID_PREVIEW=ON "${CMAKE_OPTS[@]}" "$PROJECT_DIR"

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

echo ""
echo "=== Build complete ==="
echo "Binaries: $BUILD_DIR/bin/"
echo "Libraries: $BUILD_DIR/lib/"
