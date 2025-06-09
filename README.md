# IMD-Utils (Cross-Platform)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build Status](https://github.com/hharte/imd-test/actions/workflows/release.yml/badge.svg)](https://github.com/hharte/imd-test/actions/workflows/release.yml)

## Introduction

This project provides a collection of cross-platform command-line utilities for working with ImageDisk (`.IMD`) floppy disk image files. These tools are a modern C-based port of the original MS-DOS ImageDisk utilities created by Dave Dunfield, designed for portability across Linux, macOS, and Windows.

The original MS-DOS version is available from [Dave's Old Computers](http://dunfield.classiccmp.org/img/).

## Utilities

This suite includes the following command-line tools:

* **`imdu`**: The main ImageDisk Utility. It can display image information, convert `.IMD` files to raw binary (`.BIN`), merge tracks, manage comments, apply compression, and modify track properties like data rates and interleaving.
* **`imda`**: Analyzes an `.IMD` file to recommend suitable physical floppy drive types and `imdu` options for recreating the disk.
* **`imdchk`**: Checks the format consistency of an `.IMD` file. It reports errors in headers, comments, sector maps, and track sequences.
* **`imdcmp`**: Compares two `.IMD` files track by track and sector by sector, reporting any differences found.
* **`imdv`**: An interactive, terminal-based viewer and editor for `.IMD` files. It allows navigation through tracks and sectors, and supports editing data in both hex and ASCII/EBCDIC modes.
* **`bin2imd`**: Converts a raw binary image file (sector dump) into the `.IMD` format, requiring track and sector format definitions to be provided.

## Building

These utilities are written in C (C11) and use CMake as the build system.

### Prerequisites

* A C compiler (GCC, Clang, MSVC, etc.)
* CMake (version 3.25 or later recommended)
* **For `imdv`:**
    * Linux/macOS: ncurses development library (e.g., `libncurses-dev`)
    * Windows: PDCursesMod (fetched automatically by CMake)

### Build Steps

1.  **Configure:** Create a build directory and run CMake from the project root.
    ```bash
    cmake -S . -B build
    ```
    For Visual Studio on Windows, you might use:
    ```bash
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    ```

2.  **Build:** Compile the project using CMake's build command.
    ```bash
    cmake --build build --config Release
    ```

The compiled executables will be located in the `build` directory (or a subdirectory within it, depending on your generator).

## Basic Usage

Run any command with `--help` for a full list of options.

```bash
# Display information about an IMD file
./imdu <image.imd>

# Convert IMD to raw binary sector dump
./imdu <image.imd> <output.bin> -B

# Compare two IMD files, ignoring compression differences
./imdcmp -C <file1.imd> <file2.imd>

# Analyze an IMD file for suitable drive types/options
./imda <image.imd>

# View an IMD file interactively
./imdv <image.imd>

# View and enable editing for an IMD file (use with caution!)
./imdv -W <image.imd>

# Convert a raw binary file to IMD (e.g., 80 cyl, 2 heads, 512b, 18 sectors)
./bin2imd <input.bin> <output.imd> -N=80 -2 -DM=5 -SS=512 -SM=1-18
```

## Installation (Optional)

If configured, CMake can install the libraries, headers, and executables:

```
# From the build directory
cmake --install . --prefix /path/to/install/location
# Or (for single-configuration generators like Makefiles):
# make install
# Or (for multi-configuration generators like Visual Studio):
# cmake --build . --target install --config Release
```

This typically installs:
* Executables (`imdu`, `imda`, `imdchk`, `imdcmp`, `imdv`, `bin2imd`) to `<prefix>/bin`.
* Static libraries (`libimd.a`, `libimdf.a` or `.lib`) to `<prefix>/lib`.
* Header files (`libimd.h`, `libimdf.h`) to `<prefix>/include`.
* Documentation files (`README.md`, `LICENSE`) to `<prefix>`.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.

## Credits

* **Dave Dunfield:** Creator of the original [ImageDisk software for MS-DOS](http://dunfield.classiccmp.org/img/).
* **[PDCursesMod](https://github.com/Bill-Gray/PDCursesMod):** Used for the `imdv` terminal interface on Windows.
