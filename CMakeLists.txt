# CMakeLists.txt for imd-utils

cmake_minimum_required(VERSION 3.25)

include(FetchContent)

if(DEFINED ENV{GITHUB_ACTIONS})
  message(STATUS "Running on GitHub Actions")
FetchContent_Declare(
  libimd
  URL https://github.com/hharte/libimd/archive/refs/heads/main.zip
  DOWNLOAD_EXTRACT_TIMESTAMP true
)
else()
  message(STATUS "Not running on GitHub Actions")
FetchContent_Declare(
  libimd
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../libimd
)
endif()

FetchContent_MakeAvailable(libimd)


# --- Versioning ---
find_package(Git QUIET)
set(FULL_GIT_VERSION "0.0.0-nogit")     # Default full descriptive version
set(CMAKE_PROJECT_VERSION "0.0.0")      # Default version for project() command

if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    # 1. Get the full descriptive version (tags, hash, dirty status)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _full_git_version_temp
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    if(GIT_DESCRIBE_RESULT EQUAL 0)
        set(FULL_GIT_VERSION ${_full_git_version_temp})
        message(STATUS "Raw Git describe version: ${FULL_GIT_VERSION}")
    else()
        message(WARNING "git describe --tags --dirty --always failed. Using fallback version ${FULL_GIT_VERSION}.")
    endif()

    # 2. Try to get the latest tag and extract Major.Minor.Patch for project()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE LATEST_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_TAG_RESULT
    )
    if(GIT_TAG_RESULT EQUAL 0)
        # Try to extract X.Y.Z or X.Y from tag (allowing optional 'v' prefix)
        string(REGEX MATCH "^v?([0-9]+\\.[0-9]+(\\.[0-9]+)?)" _extracted_version ${LATEST_TAG})
        if(CMAKE_MATCH_1)
            set(CMAKE_PROJECT_VERSION ${CMAKE_MATCH_1})
            message(STATUS "Using semantic version for CMake project: ${CMAKE_PROJECT_VERSION}")
        else()
            message(WARNING "Latest Git tag (${LATEST_TAG}) doesn't match expected format (X.Y or X.Y.Z). Using default ${CMAKE_PROJECT_VERSION} for project().")
        endif()
    else()
        message(WARNING "Could not get latest Git tag. Using default version ${CMAKE_PROJECT_VERSION} for project().")
    endif()

    # If the full git version (e.g., from describe) exactly matches the semantic version tag,
    # it means we are exactly on that tag commit. Mark it as an official release.
    # Note: This assumes your tags match the CMAKE_PROJECT_VERSION format (e.g., "1.2.3", not "v1.2.3")
    # If your tags have a 'v' prefix, adjust the comparison or the CMAKE_PROJECT_VERSION extraction.
    if("${CMAKE_PROJECT_VERSION}" STREQUAL "${FULL_GIT_VERSION}")
        set(FULL_GIT_VERSION "OFFICIAL RELEASE")
        message(STATUS "Current commit matches latest tag. Setting version to OFFICIAL RELEASE.")
    endif()

else()
    message(WARNING "Git not found or not a Git repository. Using fallback versions.")
    # Defaults are already set above
endif()

# Project name and version (using sanitized, CMake-compatible version)
project(imd-utils VERSION ${CMAKE_PROJECT_VERSION} LANGUAGES C)

# Add version definitions globally for all subsequent targets
add_compile_definitions(
    CMAKE_VERSION_STR="${CMAKE_PROJECT_VERSION}"
    GIT_VERSION_STR="${FULL_GIT_VERSION}"
)

# Standard C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF) # Use pure C11, disable GNU extensions if not needed
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR")

# Define source and test directories relative to CMakeLists.txt
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# --- Executable: imdu ---
add_executable(imdu ${SOURCE_DIR}/imdu.c)
target_link_libraries(imdu PRIVATE libimd)
set_target_properties(imdu PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)

# --- Executable: imda ---
add_executable(imda ${SOURCE_DIR}/imda.c)
target_link_libraries(imda PRIVATE libimd)
set_target_properties(imda PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)

# --- Executable: bin2imd ---
add_executable(bin2imd ${SOURCE_DIR}/bin2imd.c)
target_link_libraries(bin2imd PRIVATE libimd)
set_target_properties(bin2imd PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)

# --- Executable: imdchk ---
add_executable(imdchk ${SOURCE_DIR}/imdchk.c)
# Link against the new library and potentially libimd if imdchk uses it directly
target_link_libraries(imdchk PRIVATE libimdchk libimd)
set_target_properties(imdchk PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)

# --- Executable: imdcmp ---
add_executable(imdcmp ${SOURCE_DIR}/imdcmp.c)
target_link_libraries(imdcmp PRIVATE libimd)
set_target_properties(imdcmp PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)

# --- Executable: imdv ---
add_executable(imdv ${SOURCE_DIR}/imdv.c)

if(MSVC)
    include(ExternalProject)
    ExternalProject_Add(PDCurses
#      GIT_REPOSITORY     https://github.com/Bill-Gray/PDCursesMod.git
      URL               https://github.com/Bill-Gray/PDCursesMod/archive/refs/tags/v4.5.1.zip
      URL_HASH          MD5=79db9a0864a114a5bd149d3d374838f4
      CONFIGURE_COMMAND ""
      BUILD_COMMAND		nmake -f <SOURCE_DIR>/wincon/Makefile.vc PDCURSES_SRCDIR=<SOURCE_DIR>
      DOWNLOAD_EXTRACT_TIMESTAMP true

     INSTALL_COMMAND   ${CMAKE_COMMAND} -E make_directory "${PDCURSES_INSTALL_DIR}/lib" &&
                       ${CMAKE_COMMAND} -E copy "<SOURCE_DIR>-build/pdcurses.lib" "${PDCURSES_INSTALL_DIR}/lib/pdcurses.lib" &&
                       ${CMAKE_COMMAND} -E make_directory "${PDCURSES_INSTALL_DIR}/include" &&
                       ${CMAKE_COMMAND} -E copy "<SOURCE_DIR>/curses.h" "${PDCURSES_INSTALL_DIR}/include/curses.h"
    )

    target_include_directories(imdv PRIVATE ${PDCURSES_INSTALL_DIR}/include)
    target_link_directories(imdv PRIVATE ${PDCURSES_INSTALL_DIR}/lib)

    # Link your application against the imported library
    target_link_libraries(imdv PRIVATE libimdf pdcurses Winmm.lib)

    add_dependencies(imdv PDCurses)
else()
    # non-MSVC builds (e.g., Linux with ncurses).
    find_package(Curses REQUIRED)
    if(CURSES_FOUND)
        include_directories(${CURSES_INCLUDE_DIR})
        target_link_libraries(imdv PRIVATE libimdf ${CURSES_LIBRARIES})
        message(STATUS "curses found, linking IMDV.")
    else()
        message(FATAL_ERROR "curses library not found. IMDV requires curses.")
    endif()
endif()

# Set properties common to both versions of the imdv target.
set_target_properties(imdv PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)

# --- Compiler Flags ---
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(COMMON_C_FLAGS -Wall -Wextra -Wpedantic)
elseif(MSVC)
    set(COMMON_C_FLAGS "/W4") # /WX)
    set(COMMON_C_FLAGS "/GR")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS) # Suppress MSVC warnings
endif()

if (COMMON_C_FLAGS)
    target_compile_options(imdu PRIVATE ${COMMON_C_FLAGS})
    target_compile_options(imda PRIVATE ${COMMON_C_FLAGS})
    target_compile_options(imdchk PRIVATE ${COMMON_C_FLAGS})
    target_compile_options(imdcmp PRIVATE ${COMMON_C_FLAGS})
    target_compile_options(bin2imd PRIVATE ${COMMON_C_FLAGS})
    target_compile_options(imdv PRIVATE ${COMMON_C_FLAGS})
endif()

# --- Installation (Optional) ---
install(FILES README.md LICENSE DESTINATION .)
install(TARGETS imdu imda imdchk imdcmp imdv bin2imd DESTINATION bin)

# --- CPack configuration ---

# Set basic package information
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "Howard M. Harte")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ImageDisk Utilities (Cross Platform)") # Optional: A short description
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# Set the generator based on the OS
if(WIN32)
  set(CPACK_GENERATOR "ZIP") # Use ZIP archive generator on Windows
else()
  set(CPACK_GENERATOR "TGZ") # Use TGZ (tar.gz) archive generator on other systems (OSX, Linux)
endif()

# Include the CPack module - this must come AFTER setting CPACK variables
include(CPack)

# --- Output ---
message(STATUS "Configuring IMD Utilities project...")
message(STATUS "  Source directory: ${SOURCE_DIR}")
message(STATUS "  Test directory: ${TEST_DIR}")
message(STATUS "  Building executable: imdu")
message(STATUS "  Building executable: imda")
message(STATUS "  Building executable: imdchk")
message(STATUS "  Building executable: imdcmp")
message(STATUS "  Building executable: bin2imd")
message(STATUS "  Building executable: imdv")
