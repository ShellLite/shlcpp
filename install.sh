#!/bin/bash
set -e

echo "====================================="
echo "   ShellLite Native Build Script     "
echo "====================================="

# Check for cmake
if ! command -v cmake &> /dev/null; then
    echo "Error: cmake could not be found. Please install cmake (e.g., 'sudo apt install cmake' or 'brew install cmake')."
    exit 1
fi

# Check for a C++ compiler
if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    echo "Error: No C++ compiler found. Please install g++ or clang++."
    exit 1
fi

echo "[1/4] Preparing build directory..."
mkdir -p build_cpp
cd build_cpp

echo "[2/4] Configuring CMake and fetching dependencies..."
cmake .. -DCMAKE_BUILD_TYPE=Release

echo "[3/4] Compiling native executable..."
cmake --build . --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "[4/4] Finalizing installation..."
cd ..

EXECUTABLE=""
if [ -f "build_cpp/shell_lite_exec" ]; then
    EXECUTABLE="build_cpp/shell_lite_exec"
elif [ -f "build_cpp/Release/shell_lite_exec" ]; then
    EXECUTABLE="build_cpp/Release/shell_lite_exec"
elif [ -f "build_cpp/Debug/shell_lite_exec" ]; then
    EXECUTABLE="build_cpp/Debug/shell_lite_exec"
elif [ -f "build_cpp/shell_lite_exec.exe" ]; then
    EXECUTABLE="build_cpp/shell_lite_exec.exe"
elif [ -f "build_cpp/Release/shell_lite_exec.exe" ]; then
    EXECUTABLE="build_cpp/Release/shell_lite_exec.exe"
fi

if [ -n "$EXECUTABLE" ]; then
    cp "$EXECUTABLE" shlcpp
    chmod +x shlcpp
    echo "====================================="
    echo "Success! The native 'shlcpp' executable has been built."
    echo "You can run it using: ./shlcpp <script.shl>"
    echo ""
    echo "To install it globally so you can use it from anywhere, run:"
    echo "  sudo cp shlcpp /usr/local/bin/"
    echo "  sudo mkdir -p /usr/local/share/shell_lite/stdlib"
    echo "  sudo cp -r shell_lite/stdlib/* /usr/local/share/shell_lite/stdlib/"
    echo "====================================="
else
    echo "Error: Binary not found after build."
    exit 1
fi
