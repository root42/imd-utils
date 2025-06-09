/*
 * imdchk.c
 *
 * Main command-line interface for IMD file consistency checking.
 * Uses libimdchk for the core checking logic.
 *
 * www.github.com/hharte/imd-utils
 * Copyright (c) 2025, Howard M. Harte
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include "libimd.h"      /* For IMD defines, still needed by libimdchk */
#include "libimd_utils.h" /* For reporting and utilities */
#include "libimdchk.h"   /* Use the checking library */

 /* --- Version Definitions --- */
#define IMDCHECK_NAME "imdchk"
/* Define version strings - replace with actual build system values if available */
#ifndef CMAKE_VERSION_STR
#define CMAKE_VERSION_STR "0.1.0" /* Placeholder version */
#endif
#ifndef GIT_VERSION_STR
#define GIT_VERSION_STR "dev"
#endif

/* --- Global Options (for main) --- */
static int g_verbose_mode = 0;
static int g_quiet_mode = 0;
static ImdChkOptions g_options; /* Populated by arg parsing */

/* --- Forward Declarations --- */
void print_usage(const char* prog_name);
void print_version_info(FILE* stream);
int parse_long_arg(const char* arg_name, const char* arg_val_str, long* dest);
int parse_ulong_arg(const char* arg_name, const char* arg_val_str, uint32_t* dest);
void report_results(const char* filename, const ImdChkOptions* options, const ImdChkResults* results);
const char* get_check_description(uint32_t check_bit);

/* --- Helper Functions (Argument Parsing, copied from original imdchk.c) --- */

int parse_long_arg(const char* arg_name, const char* arg_val_str, long* dest) {
    char* endptr;
    long val;
    errno = 0;
    val = strtol(arg_val_str, &endptr, 10);
    if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0)) {
        fprintf(stderr, "Error: Invalid value for %s: %s (Out of range).\n", arg_name, arg_val_str); return 0;
    }
    if (endptr == arg_val_str || *endptr != '\0') {
        fprintf(stderr, "Error: Invalid non-numeric value for %s: '%s'\n", arg_name, arg_val_str); return 0;
    }
    if (val < 0) {
        fprintf(stderr, "Error: Value for %s cannot be negative: %ld\n", arg_name, val); return 0;
    }
    *dest = val;
    return 1;
}

int parse_ulong_arg(const char* arg_name, const char* arg_val_str, uint32_t* dest) {
    char* endptr;
    unsigned long val;
    errno = 0;
    val = strtoul(arg_val_str, &endptr, 0);
    if ((errno == ERANGE && val == ULONG_MAX) || (errno != 0 && val == 0)) {
        fprintf(stderr, "Error: Invalid value for %s: %s (Out of range).\n", arg_name, arg_val_str); return 0;
    }
    if (endptr == arg_val_str || *endptr != '\0') {
        fprintf(stderr, "Error: Invalid non-numeric value for %s: '%s'\n", arg_name, arg_val_str); return 0;
    }
    if (val > 0xFFFFFFFFUL) {
        fprintf(stderr, "Error: Value for %s exceeds 32 bits: %s\n", arg_name, arg_val_str); return 0;
    }
    *dest = (uint32_t)val;
    return 1;
}

/* --- Reporting Functions --- */

/* Get description for a check bit */
const char* get_check_description(uint32_t check_bit) {
    switch (check_bit) {
    case CHECK_BIT_HEADER:         return "Invalid Header";
    case CHECK_BIT_COMMENT_TERM:   return "Bad Comment Terminator";
    case CHECK_BIT_TRACK_READ:     return "Track Read Failure";
    case CHECK_BIT_FTELL:          return "ftell Failure";
    case CHECK_BIT_CON_CYL:        return "Cylinder Constraint Violation";
    case CHECK_BIT_CON_HEAD:       return "Head Constraint Violation";
    case CHECK_BIT_CON_SECTORS:    return "Sector Constraint Violation";
    case CHECK_BIT_DUPE_SID:       return "Duplicate Sector ID";
    case CHECK_BIT_INV_SFLAG_VALUE:return "Invalid Sector Flag Value";
    case CHECK_BIT_SEQ_CYL_DEC:    return "Cylinder Sequence Decrease";
    case CHECK_BIT_SEQ_HEAD_ORDER: return "Head Sequence Out of Order";
    case CHECK_BIT_SFLAG_DATA_ERR: return "Data Error Flag Set";
    case CHECK_BIT_SFLAG_DEL_DAM:  return "Deleted DAM Flag Set";
    case CHECK_BIT_DIFF_MAX_CYL:   return "Max Cylinder Differs Between Sides";
    default:                       return "Unknown Check";
    }
}

/* Report final results based on ImdChkResults */
void report_results(const char* filename, const ImdChkOptions* options, const ImdChkResults* results) {
    int error_count = 0;
    int warning_count = 0;

    if (g_quiet_mode) return; /* Skip reporting if quiet */

    printf("\n--- Check Summary ---\n");
    printf("File Checked:        %s\n", filename ? filename : "N/A");
    printf("Error Mask Applied:  0x%04X\n", options->error_mask);
    printf("Tracks Scanned:      %d\n", results->track_read_count);
    printf("Detected Sides:      %d\n", results->max_head_seen + 1);
    if (results->max_head_seen >= 0) {
        printf("Max Cylinder Side 0: %d\n", results->max_cyl_side0);
        if (results->max_head_seen >= 1) printf("Max Cylinder Side 1: %d\n", results->max_cyl_side1);
        for (int h = 2; h <= results->max_head_seen; ++h) printf("Max Cylinder Side %d: (Detected)\n", h);
    }
    else {
        printf("Max Cylinder:        N/A\n");
    }
    if (results->detected_interleave > 0) printf("Detected Interleave: %d\n", results->detected_interleave);
    else if (results->detected_interleave == 0) printf("Detected Interleave: Unknown\n");
    else printf("Detected Interleave: N/A\n");

    /* Sector Statistics */
    printf("\nSector Statistics:\n");
    printf("  Total Sectors Found: %lld\n", results->total_sector_count);
    printf("  Unavailable Sectors: %lld\n", results->unavailable_sector_count);
    printf("  Compressed Sectors:  %lld\n", results->compressed_sector_count);
    printf("  Deleted DAM Sectors: %lld\n", results->deleted_sector_count);
    printf("  Data Error Sectors:  %lld\n", results->data_error_sector_count);

    /* Errors and Warnings Summary */
    printf("\nConsistency Check Results:\n");
//    printf("  Checks Performed Mask: 0x%04X\n", results->checks_performed_mask);
    printf("  Check Failures Mask:   0x%04X\n", results->check_failures_mask);
    if (results->check_failures_mask != 0) {
        printf("  Failed Checks (%s):\n", (options->error_mask & results->check_failures_mask) ? "Errors/Warnings" : "Warnings Only");
        for (uint32_t bit = 1; bit != 0; bit <<= 1) {
//            if ((results->check_failures_mask & bit) && (results->checks_performed_mask & bit)) {
                int is_error = (options->error_mask & bit);
                printf("    - [%s] %s (0x%04X)\n",
                    is_error ? "ERROR" : "Warn ",
                    get_check_description(bit),
                    bit);
                if (is_error) error_count++; else warning_count++;
//            }
        }
    }
    else {
        printf("  No check failures detected.\n");
    }
    printf("  Errors Reported:       %d\n", error_count);
    printf("  Warnings Reported:     %d\n", warning_count);
    printf("--------------------------\n");
}

/* Print usage instructions (copied from original imdchk.c) */
void print_usage(const char* prog_name) {
    fprintf(stderr, "%s %s [%s] - Check IMD file format consistency.\n",
        IMDCHECK_NAME, CMAKE_VERSION_STR, GIT_VERSION_STR);
    fprintf(stderr, "Copyright (C) 2025 - Howard M. Harte - https://github.com/hharte/imd-utils\n\n");
    fprintf(stderr, "Usage: %s [options] <input_file.imd>\n\n", prog_name);
    fprintf(stderr, "  Checks an IMD file for format consistency using libimd.\n");
    fprintf(stderr, "  Displays a summary of the disk parameters found (unless -q).\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -v                : Verbose output (prints info for each track - handled by library).\n");
    fprintf(stderr, "  -q, --quiet       : Quiet mode. Suppress informational output (stdout) and warnings/errors (stderr).\n");
    fprintf(stderr, "                      NOTE: Does not suppress FINAL_FAILURE_MASK output to stderr.\n");
    fprintf(stderr, "  -e, --error-mask MASK : Set hex bitmask for checks considered errors (default: 0x%04X).\n", DEFAULT_ERROR_MASK);
    fprintf(stderr, "                        Use '0' to treat all checks as warnings.\n");
    fprintf(stderr, "                        Use '0xFFFFFFFF' to treat all checks as errors.\n");
    fprintf(stderr, "  -c, --cylinders N : Set maximum allowed cylinder number to N.\n");
    fprintf(stderr, "  -h, --head N      : Require all tracks to use head number N (0 or 1).\n");
    fprintf(stderr, "  -s, --sectors N   : Set maximum allowed sectors per track to N.\n");
    fprintf(stderr, "  --help            : Display this help message and exit.\n");
    fprintf(stderr, "  --version         : Display version information and exit.\n\n");
    fprintf(stderr, "Error Mask Bits (Hex):\n");
    fprintf(stderr, "  0x0001: Invalid Header        0x0002: Bad Comment Term      0x0004: Track Read Fail\n");
    fprintf(stderr, "  0x0008: ftell Fail            0x0010: Cyl Constraint        0x0020: Head Constraint\n");
    fprintf(stderr, "  0x0040: Sector Constraint     0x0080: Cyl Sequence Dec(*)   0x0100: Head Sequence Ord(*)\n");
    fprintf(stderr, "  0x0200: Duplicate Sector ID   0x0400: Invalid SFlag Value   0x0800: Data Error Flag(*)\n");
    fprintf(stderr, "  0x1000: Deleted DAM Flag(*)   0x2000: Diff Max Cyl(*)\n");
    fprintf(stderr, "  (*) Denotes checks treated as warnings by default.\n\n");
    fprintf(stderr, "Exit Codes:\n");
    fprintf(stderr, "  0 : File format OK (no checks failed OR failures were masked by --error-mask).\n");
    fprintf(stderr, "  1 : Checks failed AND were considered errors according to --error-mask.\n");
    fprintf(stderr, "  -1: Usage error, file access error, or invalid arguments.\n");
    fprintf(stderr, "Output:\n");
    fprintf(stderr, "  Informational output to stdout (suppressed by -q).\n");
    fprintf(stderr, "  Error/Warning messages to stderr (suppressed by -q).\n");
    fprintf(stderr, "  'FINAL_FAILURE_MASK: 0x<hex_mask>' output to stderr (ALWAYS printed).\n");
}

/* Prints the program name and version information (copied from original imdchk.c) */
void print_version_info(FILE* stream) {
    fprintf(stream, "%s %s [%s]\n", IMDCHECK_NAME, CMAKE_VERSION_STR, GIT_VERSION_STR);
    fprintf(stream, "Copyright (C) 2025 Howard M. Harte\n");
    fprintf(stream, "Utility to check IMD file format consistency using libimd.\n");
}

/* --- Main Function --- */
int main(int argc, char* argv[]) {
    const char* input_filename = NULL;
    int final_exit_code = 0; /* 0 or 1 based on checks and mask */
    ImdChkResults results;
    /* Use library function to get basename */
    const char* prog_name = imd_get_basename(argv[0]);
    if (!prog_name) { prog_name = argv[0]; }

    /* Initialize options to defaults */
    /* Initialize reporting first */
    imd_set_verbosity(0, 0); /* Default */

    g_options.error_mask = DEFAULT_ERROR_MASK;
    g_options.max_allowed_cyl = -1;
    g_options.required_head = -1;
    g_options.max_allowed_sectors = -1;

    /* Argument Parsing */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) { g_verbose_mode = 1; } /* Keep for potential future use */
        else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) { g_quiet_mode = 1; }
        else if (strcmp(argv[i], "--help") == 0) { print_usage(prog_name); return 0; }
        else if (strcmp(argv[i], "--version") == 0) { if (!g_quiet_mode) print_version_info(stdout); return 0; }
        else if (strcmp(argv[i], "--error-mask") == 0 || strcmp(argv[i], "-e") == 0) {
            if (++i < argc) { if (!parse_ulong_arg(argv[i - 1], argv[i], &g_options.error_mask)) return -1; }
            else { fprintf(stderr, "Error: Option %s requires MASK.\n", argv[i - 1]); return -1; }
        }
        else if (strcmp(argv[i], "--cylinders") == 0 || strcmp(argv[i], "-c") == 0) {
            if (++i < argc) { if (!parse_long_arg(argv[i - 1], argv[i], &g_options.max_allowed_cyl)) return -1; }
            else { fprintf(stderr, "Error: Option %s requires N.\n", argv[i - 1]); return -1; }
        }
        else if (strcmp(argv[i], "--head") == 0 || strcmp(argv[i], "-h") == 0) {
            if (++i < argc) {
                if (!parse_long_arg(argv[i - 1], argv[i], &g_options.required_head)) return -1;
                if (g_options.required_head != 0 && g_options.required_head != 1) { fprintf(stderr, "Error: Head must be 0 or 1.\n"); return -1; }
            }
            else { fprintf(stderr, "Error: Option %s requires N.\n", argv[i - 1]); return -1; }
        }
        else if (strcmp(argv[i], "--sectors") == 0 || strcmp(argv[i], "-s") == 0) {
            if (++i < argc) { if (!parse_long_arg(argv[i - 1], argv[i], &g_options.max_allowed_sectors)) return -1; }
            else { fprintf(stderr, "Error: Option %s requires N.\n", argv[i - 1]); return -1; }
        }
        else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]); print_usage(prog_name); return -1;
        }
        else if (input_filename == NULL) { input_filename = argv[i]; }
        else { fprintf(stderr, "Error: Too many input files ('%s').\n", argv[i]); print_usage(prog_name); return -1; }
    }
    /* Update reporting verbosity based on parsed args (final check) */
    imd_set_verbosity(g_quiet_mode, g_verbose_mode);

    if (input_filename == NULL) { fprintf(stderr, "Error: Input file not specified.\n"); print_usage(prog_name); return -1; }

    /* Print sign-on message */
    if (!g_quiet_mode) {
        print_version_info(stdout);
        printf("\nChecking IMD file: %s\n", input_filename);
        printf("Error Mask: 0x%04X\n", g_options.error_mask);
        if (g_options.max_allowed_cyl != -1) printf("Constraint: Max Cylinder <= %ld\n", g_options.max_allowed_cyl);
        if (g_options.required_head != -1) printf("Constraint: Head == %ld\n", g_options.required_head);
        if (g_options.max_allowed_sectors != -1) printf("Constraint: Sectors <= %ld\n", g_options.max_allowed_sectors);
        if (g_options.max_allowed_cyl != -1 || g_options.required_head != -1 || g_options.max_allowed_sectors != -1 || g_options.error_mask != DEFAULT_ERROR_MASK) printf("\n");
        printf("Scanning tracks...\n");
    }

    /* Call the library function to perform checks */
    int check_status = imdchk_check_file(input_filename, &g_options, &results);

    if (check_status != 0) {
        fprintf(stderr, "Error: Failed to open or process file '%s'.\n", input_filename);
        /* Always print failure mask even on critical error */
        fprintf(stderr, "FINAL_FAILURE_MASK: 0x%04X\n", results.check_failures_mask);
        return -1; /* Indicate critical file/processing error */
    }

    /* Report results based on the returned structure */
    report_results(input_filename, &g_options, &results);

    /* Determine final exit code (0 or 1) */
//    if ((results.checks_performed_mask & results.check_failures_mask & g_options.error_mask) != 0) {
        final_exit_code = 1; /* At least one failure was considered an error */
//    }
//    else {
//        final_exit_code = 0; /* No failures or only warnings */
//    }

    /* Always print the raw failure mask to stderr */
    fprintf(stderr, "FINAL_FAILURE_MASK: 0x%04X\n", results.check_failures_mask);

    /* Final result message */
    if (!g_quiet_mode) {
        if (final_exit_code != 0) {
            printf("Result: FAIL - Checks failed according to error mask (Exit Code: %d)\n", final_exit_code);
        }
        else {
            if (results.check_failures_mask != 0) {
                printf("Result: OK - File format acceptable (Failures occurred but were masked, Exit Code: %d)\n", final_exit_code);
            }
            else {
                printf("Result: OK - File format consistency check passed (Exit Code: %d)\n", final_exit_code);
            }
        }
    }

    return final_exit_code;
}
