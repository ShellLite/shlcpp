# Contributing to shlcpp

First off, thank you for considering contributing to `shlcpp`!

## Getting Started

1. **Fork the repository** on GitHub.
2. **Clone your fork** locally:

   ```bash
   git clone https://github.com/your-username/shlcpp.git
   cd shlcpp
   ```

3. **Build the Runtime**:
   `shlcpp` is a C++17 project built using CMake. Helper scripts are provided for quick compilation.

   **Windows (PowerShell)**:
   ```powershell
   .\compile_shl.bat
   ```

   **Linux / macOS (Bash)**:
   ```bash
   ./install.sh
   ```

4. **Install Test Dependencies**:
   ```bash
   pip install pytest anyio
   ```

## Development Workflow

1. Create a branch for your feature or fix:
   ```bash
   git checkout -b feature/amazing-feature
   ```

2. Make changes to the C++ core (`shell_lite/`) or standard library (`stdlib/`).
3. Compile with `.\compile_shl.bat` or `./install.sh`.
4. Validate changes:
   ```bash
   shlcpp check <script.shl>
   pytest tests/ -v
   ```
5. Commit and open a Pull Request against `main`.

## Code Style & Standards

- **C++**: Follow C++17 standards. Ensure memory management is through the VM arena allocator and GC.
- **Diagnostics**: Ensure any new syntax or parsing errors provide clear error messages with accurate line/column locations.
- **Reproducibility**: Pinned dependencies must not be changed without a documented version update.
