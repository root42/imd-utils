/*
 * ImageDisk Utility (Cross-Platform.)
 *
 * www.github.com/hharte/imd-utils
 *
 * Copyright (c) 2025, Howard M. Harte
 *
 * Reference:
 * Original ImageDisk Utilities by Dave Dunfield (Dave's Old Computers)
 * http://dunfield.classiccmp.org/img/
 *
 */

 /* Define _DEFAULT_SOURCE to enable POSIX features like strdup */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <stdarg.h>

/* Check if strdup is available (needed for POSIX compliance) */
#ifdef _WIN32
#define strdup _strdup
#else
/* Assume POSIX environment provides strdup in string.h */
#endif

#define IMDU_FILL_BYTE_DEFAULT  0   /* IMDU uses 0x00 for the default fill byte, libimd uses 0xE5. */
#ifndef IMDU_FILL_BYTE_DEFAULT
#define IMDU_FILL_BYTE_DEFAULT  LIBIMD_FILL_BYTE_DEFAULT
#endif  /* IMDU_FILL_BYTE_DEFAULT */


#include "libimd.h" /* Include the library header (defines and utils) */
#include "libimd_utils.h" /* For common utilities */

/* Define version strings - replace with actual build system values if available */
#ifndef CMAKE_VERSION_STR
#define CMAKE_VERSION_STR "0.1.0" /* Placeholder version */
#endif
#ifndef GIT_VERSION_STR
#define GIT_VERSION_STR "dev" /* Placeholder git revision */
#endif

/* --- Constants --- */
#define MAX_FILENAME 260
/* #define MAX_HEADER_LINE 256 - Defined in libimd.h */
/* #define MAX_COMMENT_SIZE 65536 - Limit handled by libimd.c */

/* Status index values (for stats array) */
#define ST_TOTAL   0
#define ST_COMP    1
#define ST_DAM     2
#define ST_BAD     3
#define ST_UNAVAIL 4

#define MAX_TRACKS 256 /* Max tracks for exclusion map */

/* Operation modes (internal) */
typedef enum {
    OP_MODE_INFO,           /* Default: Just display info */
    OP_MODE_WRITE_IMD,      /* Write output IMD (implies output_filename needed) */
    OP_MODE_WRITE_BIN,      /* Write output BIN (implies output_filename needed) */
    OP_MODE_EXTRACT_COMMENT /* Extract comment (doesn't need output_filename) */
} OperationMode;

/* --- Data Structures --- */

/* Global options structure */
typedef struct {
    const char* input_filename;
    const char* merge_filename;
    const char* output_filename;
    char* append_comment_file;  /* Use char* for strdup'd strings */
    char* extract_comment_file; /* Use char* for strdup'd strings */
    char* replace_comment_file; /* Use char* for strdup'd strings */

    OperationMode op_mode;      /* What primary operation to perform */

    /* New member to hold the compression mode state */
    int compression_mode; /* Use IMD_COMPRESSION_* defines */

    int ignore_mode_diff;   /* --ignore-mode-diff flag */
    int force_non_bad;      /* -NB flag */
    int force_non_deleted;  /* -ND flag */
    int quiet;              /* -Q flag */
    int auto_yes;           /* -Y flag */
    int detail;             /* -D flag */

    int fill_specified;
    uint8_t fill_byte;      /* -F=xx value */

    int interleave;         /* -IL[=N] value (LIBIMD_IL_AS_READ, LIBIMD_IL_BEST_GUESS, 1-99=Factor) */
    int interleave_set;     /* Flag to track if -IL was used */
    uint8_t tmode[LIBIMD_NUM_MODES]; /* -T<rate>=<rate> translation map */
    uint8_t skip_track[MAX_TRACKS]; /* -X exclusion map (uses IMD_SIDE_*_MASK) */

    /* Options for adding missing sectors */
    int add_missing_sectors_target; /* Target number of sectors per track */
    int add_missing_sectors_active; /* Flag to indicate if --add-missing is used */

} Options;

/* Global statistics */
uint64_t stats[ST_UNAVAIL + 1] = { 0 }; /* Size based on last index */

/* Mode translation lookup (index = IMD mode, value = rate code for T options) */
const int MODE_TO_RATE_CODE[] = { 5, 3, 2, 5, 3, 2 }; /* 500, 300, 250 kbps codes */
/* Data rates corresponding to IMD modes */
const int MODE_RATES[] = { 500, 300, 250, 500, 300, 250 };

/* --- Helper Functions --- */

/* Removed local error_exit */
/* Removed local warning */

/**
 * @brief Prints usage information.
 */
void print_usage(const char* prog_name) {
    const char* base_prog_name = imd_get_basename(prog_name); /* Use library function */
    if (!base_prog_name) base_prog_name = "imdu"; /* Fallback */

    fprintf(stderr, "ImageDisk Utility (Cross-Platform) %s [%s]\n",
        CMAKE_VERSION_STR, GIT_VERSION_STR);
    fprintf(stderr, "Copyright (C) 2025 - Howard M. Harte - https://github.com/hharte/imd-utils\n\n");
    fprintf(stderr, "The original MS-DOS version is available from Dave's Old Computers: http://dunfield.classiccmp.org/img/\n\n");
    printf("Usage: %s image [[merge-image] [output-image]] [options]\n\n", base_prog_name);
    printf("Core Options:\n");
    printf("  image          : Input IMD file (required).\n");
    printf("  merge-image    : (Simplified) IMD file to merge from.\n");
    printf("  output-image   : Output file (IMD or BIN depending on -B).\n");
    printf("                     If omitted, no output file is written.\n");
    printf("\nProcessing Options:\n");
    printf("  -B             : Output Binary image (raw sector data).\n");
    printf("                     Requires output-image. Defaults to 1:1 interleave if -IL not specified.\n");
    printf("  -C             : Compress uniform sectors on output (IMD only).\n");
    printf("                     Requires output-image.\n");
    printf("  -E             : Expand compressed sectors.\n");
    printf("  -NB            : Force Non-Bad status on sectors during write.\n");
    printf("  -ND            : Force Non-Deleted status on sectors during write.\n");
    printf("  -F=xx          : Fill unavailable/missing sectors with hex value xx. (default=0x%02x)\n", IMDU_FILL_BYTE_DEFAULT);
    printf("  -IL[=N]        : Re-interleave output (N:1, blank=BestGuess, default=As Read/1:1 for -B).\n");
    printf("                     Requires output-image.\n");
    printf("  --add-missing=<target_spt> : Add Missing sectors up to <target_spt> total per track,\n");
    printf("                     marked as unavailable. Requires output-image.\n");
    printf("  -T<rate>=<rate>: Translate track data rate on output (e.g., -T300=250).\n");
    printf("                     Requires output-image. Rates are 250, 300, 500 (kbps).\n");
    printf("  -X[0|1]=t[,t]  : Exclude track(s) (t or t1-t2 range). 0=side0, 1=side1, none=both.\n");
    printf("\nComment Options:\n");
    printf("  -AC=<file>     : Append Comment from text file (requires output IMD).\n");
    printf("  -EC=<file>     : Extract Comment to text file.\n");
    printf("  -RC=<file>     : Replace Comment with text file (requires output IMD).\n");
    printf("\nOther Options:\n");
    printf("  -D             : Display detailed track/sector info during processing.\n");
    printf("  -M                 : Ignore Mode difference in merge (simplified merge only).\n");
    printf("  --ignore-mode-diff : Ignore Mode difference in merge (simplified merge only).\n");
    printf("  -Q             : Quiet: suppress warnings and non-essential output.\n");
    printf("  -Y             : Auto-Yes to overwrite prompt.\n");
    printf("  --help         : Display this help message and exit.\n");
}

/**
 * @brief Parses a numeric value from a string pointer.
 */
int parse_num(const char** ptr, unsigned long* val, int base) {
    char* endptr;
    if (!ptr || !*ptr || !**ptr) { return 0; } /* No value */
    *val = strtoul(*ptr, &endptr, base);
    if (endptr == *ptr) return 0; /* No digits parsed */
    *ptr = endptr; /* Advance pointer */
    return 1;
}

/**
 * @brief Parses track range for exclusion options.
 */
void parse_exclusion(const char* value, uint8_t side_mask, Options* opts) {
    const char* ptr = value;
    unsigned long start_track, end_track;

    while (*ptr) {
        if (!parse_num(&ptr, &start_track, 10)) {
            imd_report(IMD_REPORT_LEVEL_WARNING, "Invalid start track number in exclusion: %s", value);
            return;
        }
        end_track = start_track; /* Default to single track */

        if (*ptr == '-') {
            ptr++;
            if (!parse_num(&ptr, &end_track, 10)) {
                imd_report(IMD_REPORT_LEVEL_WARNING, "Invalid end track number in exclusion range: %s", value);
                return;
            }
            if (end_track < start_track) {
                imd_report(IMD_REPORT_LEVEL_WARNING, "End track cannot be less than start track in exclusion range: %s", value);
                return;
            }
        }

        if (start_track >= MAX_TRACKS || end_track >= MAX_TRACKS) {
            imd_report(IMD_REPORT_LEVEL_WARNING, "Track number exceeds maximum (%d) in exclusion: %s", MAX_TRACKS - 1, value);
            return;
        }

        /* Apply */
        for (unsigned long t = start_track; t <= end_track; ++t) {
            opts->skip_track[t] |= side_mask;
        }

        if (*ptr == ',') {
            ptr++; /* Skip comma for next range */
        }
        else if (*ptr != '\0') {
            imd_report(IMD_REPORT_LEVEL_WARNING, "Unexpected character in exclusion list: %s", value);
            return;
        }
    }
}

/**
 * @brief Parses command line arguments.
 * Revised logic to handle options mixed with filenames using a single pass.
 * Uses strdup for comment filenames to avoid pointer issues.
 * Attempts to rejoin option values split by the shell (e.g., -EC=file.txt).
 * Returns 0 on success, -1 on parsing error (e.g., invalid option).
 */
int parse_args(int argc, char* argv[], Options* opts) {
    memset(opts, 0, sizeof(Options));
    opts->fill_byte = IMDU_FILL_BYTE_DEFAULT;
    opts->interleave = LIBIMD_IL_AS_READ; /* Default */
    opts->interleave_set = 0; /* Initialize flag */
    opts->op_mode = OP_MODE_INFO; /* Default mode */
    opts->compression_mode = IMD_COMPRESSION_AS_READ; /* Default compression */
    opts->add_missing_sectors_target = 0;
    opts->add_missing_sectors_active = 0;
    uint8_t output_filename_needed = 0;

    for (int i = 0; i < LIBIMD_NUM_MODES; ++i) opts->tmode[i] = (uint8_t)i;

    const char* potential_filenames[100]; /* Max possible filenames */
    int potential_file_count = 0;
    const char* base_prog_name = imd_get_basename(argv[0]); /* Use library function */
    if (!base_prog_name) base_prog_name = "imdu"; /* Fallback */

    for (int arg_index = 1; arg_index < argc; ++arg_index) {
        char* arg = argv[arg_index];

        if (strcmp(arg, "--help") == 0) {
            print_usage(base_prog_name);
            exit(EXIT_SUCCESS);
        }
        if (strcmp(arg, "--ignore-mode-diff") == 0) {
            opts->ignore_mode_diff = 1;
            continue; /* Next argument */
        }
        if (strncmp(arg, "--add-missing=", strlen("--add-missing=")) == 0) {
            const char* value_str = arg + strlen("--add-missing=");
            unsigned long val;
            if (parse_num(&value_str, &val, 10) && *value_str == '\0' && val > 0 && val <= LIBIMD_MAX_SECTORS_PER_TRACK) {
                opts->add_missing_sectors_target = (int)val;
                opts->add_missing_sectors_active = 1;
                output_filename_needed = 1;
            }
            else {
                imd_report(IMD_REPORT_LEVEL_WARNING, "Invalid value for --add-missing: %s", arg + strlen("--add-missing="));
            }
            continue;
        }


        if (arg[0] == '-') { /* It's an option */
            char opt_char = (char)toupper((unsigned char)arg[1]);
            char* value = NULL;
            char* equals_sign = strchr(arg, '=');
            char combined_value_buffer[MAX_FILENAME * 2] = { 0 }; /* Buffer for rejoined values */
            int value_rejoined = 0;

            if (equals_sign != NULL) { /* Option with value */
                value = equals_sign + 1;

                if (arg_index + 1 < argc && argv[arg_index + 1][0] != '-') {
                    strncpy(combined_value_buffer, value, sizeof(combined_value_buffer) - 1);
                    strncat(combined_value_buffer, argv[arg_index + 1], sizeof(combined_value_buffer) - strlen(combined_value_buffer) - 1);
                    value = combined_value_buffer;
                    arg_index++;
                    value_rejoined = 1;
                }

                if (*value == '\0') {
                    imd_report(IMD_REPORT_LEVEL_WARNING, "Missing value after '=' for option %s", arg);
                    value = NULL;
                }
            }

            /* Process options */
            switch (opt_char) {
            case 'B':
                if (!value) {
                    opts->op_mode = OP_MODE_WRITE_BIN;
                    output_filename_needed = 1;
                }
                else {
                    imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring value for -B option: %s", arg);
                }
                break;
            case 'C':
                if (!value && arg[2] == '\0') {
                    opts->compression_mode = IMD_COMPRESSION_FORCE_COMPRESS;
                    if (opts->op_mode != OP_MODE_WRITE_BIN) opts->op_mode = OP_MODE_WRITE_IMD;
                    output_filename_needed = 1;
                }
                else if (toupper((unsigned char)arg[2]) == 'C' && equals_sign && value) {
                    if (toupper((unsigned char)arg[1]) == 'A') {
                        if (opts->append_comment_file) free(opts->append_comment_file);
                        opts->append_comment_file = strdup(value);
                        if (!opts->append_comment_file) imd_report_error_exit("Memory allocation failed for comment filename.");
                        if (opts->op_mode != OP_MODE_WRITE_BIN) opts->op_mode = OP_MODE_WRITE_IMD;
                        output_filename_needed = 1;
                    }
                    else if (toupper((unsigned char)arg[1]) == 'R') {
                        if (opts->replace_comment_file) free(opts->replace_comment_file);
                        opts->replace_comment_file = strdup(value);
                        if (!opts->replace_comment_file) imd_report_error_exit("Memory allocation failed for comment filename.");
                        if (opts->op_mode != OP_MODE_WRITE_BIN) opts->op_mode = OP_MODE_WRITE_IMD;
                        output_filename_needed = 1;
                    }
                    else {
                        imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring invalid option: %s", arg);
                    }
                }
                else {
                    imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring invalid format or value for -C option: %s", arg);
                }
                break;
            case 'E':
                if (toupper((unsigned char)arg[2]) == 'C' && equals_sign && value) {
                    if (opts->extract_comment_file) free(opts->extract_comment_file);
                    opts->extract_comment_file = strdup(value);
                    if (!opts->extract_comment_file) imd_report_error_exit("Memory allocation failed for comment filename.");
                    if (opts->op_mode == OP_MODE_INFO) opts->op_mode = OP_MODE_EXTRACT_COMMENT;
                }
                else if (!value && arg[2] == '\0') {
                    opts->compression_mode = IMD_COMPRESSION_FORCE_DECOMPRESS;
                    if (opts->op_mode != OP_MODE_WRITE_BIN) opts->op_mode = OP_MODE_WRITE_IMD;
                    output_filename_needed = 1;
                }
                else {
                    imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring invalid format for -E option: %s", arg);
                }
                break;
            case 'M': /* Only the simple -M for ignore-mode-diff */
                if (!value && arg[2] == '\0') {
                    opts->ignore_mode_diff = 1;
                }
                else {
                    imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring invalid -M option format: %s. Use --add-missing=N for sectors, or --ignore-mode-diff / simple -M for merge.", arg);
                }
                break;
            case 'Q': if (!value) opts->quiet = 1; else imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring value for -Q option: %s", arg); break;
            case 'Y': if (!value) opts->auto_yes = 1; else imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring value for -Y option: %s", arg); break;
            case 'D': if (!value) opts->detail = 1; else imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring value for -D option: %s", arg); break;
            case 'N':
                if (toupper((unsigned char)arg[2]) == 'B' && !value && arg[3] == '\0') {
                    opts->force_non_bad = 1;
                    output_filename_needed = 1;
                }
                else if (toupper((unsigned char)arg[2]) == 'D' && !value && arg[3] == '\0') {
                    opts->force_non_deleted = 1;
                    output_filename_needed = 1;
                }
                else imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring invalid -N option: %s", arg);
                break;
            case 'F':
                if (value) {
                    char* endptr;
                    unsigned long val = strtoul(value, &endptr, 16);

                    if (*endptr == '\0' && val <= 0xFF) {
                        opts->fill_byte = (uint8_t)val;
                        opts->fill_specified = 1;
                        output_filename_needed = 1;
                    }
                    else { imd_report(IMD_REPORT_LEVEL_WARNING, "Invalid hex value for -F=: %s", value); }
                }
                else { imd_report(IMD_REPORT_LEVEL_WARNING, "Missing value for -F= option."); }
                break;
            case 'A':
                if (toupper((unsigned char)arg[2]) != 'C') imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring invalid comment option: %s", arg);
                break;
            case 'R':
                if (toupper((unsigned char)arg[2]) != 'C') imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring invalid comment option: %s", arg);
                break;
            case 'X':
                if (value) {
                    uint8_t side_mask = IMD_SIDE_BOTH_MASK;
                    const char* range_ptr = value;

                    if (isdigit((unsigned char)arg[2]) && arg[3] == '=') {
                        if (arg[2] == '0') side_mask = IMD_SIDE_0_MASK;
                        else if (arg[2] == '1') side_mask = IMD_SIDE_1_MASK;
                        else { imd_report(IMD_REPORT_LEVEL_WARNING, "Invalid side specifier for -X: %s", arg); break; }

                        if (!value_rejoined) range_ptr = &arg[4];
                    }
                    else if (arg[2] != '=') {
                        imd_report(IMD_REPORT_LEVEL_WARNING, "Invalid format for -X option: %s", arg); break;
                    }

                    parse_exclusion(range_ptr, side_mask, opts);
                    output_filename_needed = 1;
                }
                else {
                    imd_report(IMD_REPORT_LEVEL_WARNING, "Missing track range for -X option.");
                }
                break;
            case 'I':
                if (toupper((unsigned char)arg[2]) == 'L') {
                    opts->interleave_set = 1;
                    if (value) {
                        char* endptr;
                        unsigned long val = strtoul(value, &endptr, 10);
                        if (*endptr == '\0' && val >= 1 && val <= 99) { opts->interleave = (int)val; }
                        else { imd_report(IMD_REPORT_LEVEL_WARNING, "Invalid interleave factor N for -IL=N (must be 1-99): %s", value); }
                    }
                    else { opts->interleave = LIBIMD_IL_BEST_GUESS; }
                    output_filename_needed = 1;
                }
                else {
                    fprintf(stderr, "Error: Unknown option '%s'\n", arg);
                    return -1;
                }
                break;
            case 'T':
                if (isdigit((unsigned char)arg[2]) && value) {
                    unsigned long rate_from, rate_to;
                    const char* ptr_from = &arg[2];
                    const char* ptr_to = value;
                    int mode_from = -1, mode_to = -1;

                    if (!parse_num(&ptr_from, &rate_from, 10) || *ptr_from != '=') {
                        imd_report(IMD_REPORT_LEVEL_WARNING, "Invalid format for -T<rate>=<rate> option: %s", arg); break;
                    }
                    if (!parse_num(&ptr_to, &rate_to, 10) || *ptr_to != '\0') {
                        imd_report(IMD_REPORT_LEVEL_WARNING, "Invalid format for -T<rate>=<rate> option: %s", value); break;
                    }

                    for (int i = 0; i < LIBIMD_NUM_MODES; ++i) {
                        int current_rate_code = MODE_TO_RATE_CODE[i];
                        if ((current_rate_code == 2 && rate_from == 250) || (current_rate_code == 3 && rate_from == 300) || (current_rate_code == 5 && rate_from == 500)) {
                            mode_from = i;
                            for (int j = 0; j < LIBIMD_NUM_MODES; ++j) {
                                int target_rate_code = MODE_TO_RATE_CODE[j];
                                if (((target_rate_code == 2 && rate_to == 250) || (target_rate_code == 3 && rate_to == 300) || (target_rate_code == 5 && rate_to == 500)) &&
                                    ((i < 3 && j < 3) || (i >= 3 && j >= 3))) {
                                    mode_to = j;
                                    opts->tmode[mode_from] = (uint8_t)mode_to;
                                    if (!opts->quiet) printf("  Applying translation: Mode %d -> Mode %d (%lu kbps -> %lu kbps)\n", mode_from, mode_to, rate_from, rate_to);
                                    output_filename_needed = 1;
                                    break;
                                }
                            }
                            if (mode_to == -1) { imd_report(IMD_REPORT_LEVEL_WARNING, "Cannot translate mode %d (%lu kbps) to %lu kbps (check FM/MFM compatibility).\n", mode_from, rate_from, rate_to); }
                            break;
                        }
                    }
                    if (mode_from == -1) {
                        fprintf(stderr, "Warning: Invalid source rate for -T option: %lu\n", rate_from);
                    }
                }
                else { imd_report(IMD_REPORT_LEVEL_WARNING, "Invalid format or missing value for -T option: %s", arg); }
                break;
            default:
                fprintf(stderr, "Error: Unknown option '%s'\n", arg);
                return -1;
            }
        }
        else { /* It's a potential filename */
            if (potential_file_count < 100) { /* Avoid buffer overflow */
                potential_filenames[potential_file_count++] = arg;
            }
            else {
                imd_report(IMD_REPORT_LEVEL_WARNING, "Too many filename arguments found.");
            }
        }
    }

    /* Assign filenames based on the count of non-option arguments found */
    if (potential_file_count >= 1) opts->input_filename = potential_filenames[0];
    if (potential_file_count == 2) opts->output_filename = potential_filenames[1];
    if (potential_file_count >= 3) {
        opts->merge_filename = potential_filenames[1];
        opts->output_filename = potential_filenames[2];
        if (opts->op_mode != OP_MODE_WRITE_BIN) opts->op_mode = OP_MODE_WRITE_IMD; /* Merge implies IMD unless -B */
        output_filename_needed = 1; /* Merge requires output */
    }
    if (potential_file_count > 3) {
        imd_report(IMD_REPORT_LEVEL_WARNING, "Ignoring extra file arguments starting from '%s'", potential_filenames[3]);
    }

    /* Check for required output filename */
    if (output_filename_needed && !opts->output_filename) {
        fprintf(stderr, "Error: Output file required for the selected operation (e.g., -B, -C, -E, merge, -IL, -T, -NB, -ND, -F, -X, -AC, -RC, --add-missing) but none specified.\n");
        return -1;
    }

    /* Automatically set IMD output mode if output file is specified and not binary/comment */
    if (opts->output_filename != NULL && opts->op_mode != OP_MODE_WRITE_BIN && opts->op_mode != OP_MODE_EXTRACT_COMMENT) {
        opts->op_mode = OP_MODE_WRITE_IMD;
    }

    /* Apply default interleave for -B if -IL was not specified */
    if (opts->op_mode == OP_MODE_WRITE_BIN && !opts->interleave_set) {
        opts->interleave = 1; /* Default to 1:1 for binary output */
    }

    return 0; /* Success */
}


/**
 * @brief Prints the final statistics.
 */
void print_stats(uint32_t track_count) {
    printf("%u tracks processed, %llu sectors total", track_count, (unsigned long long)stats[ST_TOTAL]);
    int first_stat = 1;
    const char* stat_names[] = { "Compressed", "Deleted", "Bad", "Unavailable" };
    for (int i = 0; i < 4; ++i) {
        if (stats[i + 1] > 0) {
            printf("%s%llu %s", first_stat ? " (" : ", ", (unsigned long long)stats[i + 1], stat_names[i]);
            first_stat = 0;
        }
    }
    if (!first_stat) printf(")");
    printf("\n");
}

/* --- Main Entry Point --- */

int main(int argc, char* argv[]) {
    Options opts;
    FILE* fimd = NULL, * fmerge = NULL, * fout = NULL, * fcomment = NULL;
    char* comment_buffer = NULL;
    size_t comment_size = 0;
    int result = EXIT_FAILURE;
    ImdTrackInfo primary_track = { 0 }, merge_track = { 0 };
    uint32_t track_count = 0;
    int primary_eof = 0, merge_eof = 0;
    int header_read_status;
    int comment_read_status;
    ImdHeaderInfo header_info;

    static int last_mode_printed = -1, last_nsec_printed = -1;
    static uint32_t last_size_printed = (uint32_t)-1;

    /* --- Argument Parsing --- */
    if (parse_args(argc, argv, &opts) != 0) {
        print_usage(argv[0]);
        if (opts.append_comment_file) free(opts.append_comment_file);
        if (opts.extract_comment_file) free(opts.extract_comment_file);
        if (opts.replace_comment_file) free(opts.replace_comment_file);
        return EXIT_FAILURE;
    }

    /* Initialize Reporting after parsing args */
    imd_set_verbosity(opts.quiet, opts.detail);

    if (!opts.input_filename) {
        print_usage(argv[0]);
        if (opts.append_comment_file) free(opts.append_comment_file);
        if (opts.extract_comment_file) free(opts.extract_comment_file);
        if (opts.replace_comment_file) free(opts.replace_comment_file);
        return EXIT_FAILURE;
    }
    if (!opts.quiet) {
        fprintf(stderr, "ImageDisk Utility (Cross-Platform) %s [%s]\n\n",
            CMAKE_VERSION_STR, GIT_VERSION_STR);
        fprintf(stderr, "The original MS-DOS version is available from Dave's Old Computers: http://dunfield.classiccmp.org/img/\n\n");
    }


    /* --- Open Input File --- */
    fimd = fopen(opts.input_filename, "rb");
    if (!fimd) {
        fprintf(stderr, "Error: Cannot open input file '%s': %s\n", opts.input_filename, strerror(errno));
        goto cleanup;
    }

    /* --- Open Merge File (if specified) --- */
    if (opts.merge_filename) {
        fmerge = fopen(opts.merge_filename, "rb");
        if (!fmerge) {
            fprintf(stderr, "Error: Cannot open merge file '%s': %s\n", opts.merge_filename, strerror(errno));
            /* Continue without merge? Let's exit. */
            goto cleanup;
        }
        else {
            if (!opts.quiet) printf("Merge file opened: %s\n", opts.merge_filename);
            header_read_status = imd_read_file_header(fmerge, NULL, NULL, 0);
            if (header_read_status != 0) {
                fprintf(stderr, "Error reading merge header.\n");
                goto cleanup;
            }
            comment_read_status = imd_skip_comment_block(fmerge);
            if (comment_read_status != 0) {
                fprintf(stderr, "Error skipping merge comment.\n");
                goto cleanup;
            }
        }
    }

    /* --- Handle Output File --- */
    if (opts.output_filename) {
        if (opts.op_mode != OP_MODE_WRITE_IMD && opts.op_mode != OP_MODE_WRITE_BIN) {
            /* Allow if only doing comment extract */
            if (opts.op_mode != OP_MODE_EXTRACT_COMMENT)
                imd_report(IMD_REPORT_LEVEL_WARNING, "Output file '%s' specified, but no operation requires it (e.g., -B, -C -E). File may not be created.", opts.output_filename);
        }
        else {
            if (!opts.auto_yes) {
                FILE* test_out = fopen(opts.output_filename, "rb");
                if (test_out) {
                    fclose(test_out);
                    printf("Output file '%s' already exists. Overwrite (Y/N)? ", opts.output_filename);
                    fflush(stdout);
                    int choice = getchar();
                    int c_in; while ((c_in = getchar()) != '\n' && c_in != EOF);
                    if (toupper((unsigned char)choice) != 'Y') {
                        printf("Operation cancelled.\n");
                        result = EXIT_SUCCESS;
                        goto cleanup;
                    }
                }
            }
            fout = fopen(opts.output_filename, "wb");
            if (!fout) {
                fprintf(stderr, "Error: Cannot open output file '%s': %s\n", opts.output_filename, strerror(errno));
                goto cleanup;
            }
        }
    }
    else if (opts.op_mode == OP_MODE_INFO && !opts.extract_comment_file && !opts.quiet) {
        imd_report(IMD_REPORT_LEVEL_WARNING, "No output file specified and no output operation selected. Only displaying information.");
    }


    /* --- Read Header and Comment (Primary File) using libimd --- */
    char main_header_line_buf[LIBIMD_MAX_HEADER_LINE];
    header_read_status = imd_read_file_header(fimd, &header_info, main_header_line_buf, sizeof(main_header_line_buf));
    if (header_read_status != 0) {
        fprintf(stderr, "Error: Failed to read or parse IMD header line (Status: %d).\n", header_read_status);
        goto cleanup;
    }
    if (!opts.quiet) printf("IMD Header: %s\n", main_header_line_buf);

    comment_buffer = imd_read_comment_block(fimd, &comment_size);
    if (!comment_buffer) {
        fprintf(stderr, "Error: Failed to read IMD comment block.\n");
        goto cleanup;
    }

    if (!opts.quiet && comment_size > 0) {
        printf("%s\n", comment_buffer);
    }

    /* --- Handle Comment Options --- */
    if (opts.extract_comment_file) {
        fcomment = fopen(opts.extract_comment_file, "wb"); /* Use binary mode for consistency */
        if (!fcomment) {
            fprintf(stderr, "Error opening comment extraction file '%s': %s\n", opts.extract_comment_file, strerror(errno));
        }
        else {
            if (fwrite(comment_buffer, 1, comment_size, fcomment) != comment_size) {
                perror("Error writing extracted comment");
            }
            fclose(fcomment); fcomment = NULL;
            if (!opts.quiet) printf("Comment extracted to '%s'\n", opts.extract_comment_file);
        }
    }

    if (opts.replace_comment_file) {
        if (opts.op_mode == OP_MODE_WRITE_BIN) { imd_report(IMD_REPORT_LEVEL_WARNING, "-RC ignored when writing binary output (-B)."); }
        else if (fout) {
            fcomment = fopen(opts.replace_comment_file, "r");
            if (!fcomment) { fprintf(stderr, "Error opening replacement comment file '%s': %s\n", opts.replace_comment_file, strerror(errno)); }
            else {
                fseek(fcomment, 0, SEEK_END);
                long new_comment_size_long = ftell(fcomment);
                if (new_comment_size_long < 0) { perror("ftell replace file"); fclose(fcomment); goto cleanup; }
                size_t new_comment_size = (size_t)new_comment_size_long;
                fseek(fcomment, 0, SEEK_SET);
                char* new_comment_buffer = (char*)malloc(new_comment_size + 1);
                if (!new_comment_buffer) { perror("malloc replace comment"); fclose(fcomment); goto cleanup; }
                if (fread(new_comment_buffer, 1, new_comment_size, fcomment) != new_comment_size) {
                    perror("Error reading replacement comment"); free(new_comment_buffer); fclose(fcomment); goto cleanup;
                }
                new_comment_buffer[new_comment_size] = '\0';
                fclose(fcomment); fcomment = NULL;
                free(comment_buffer);
                comment_buffer = new_comment_buffer;
                comment_size = new_comment_size;
                if (!opts.quiet) printf("Comment replaced from '%s'\n", opts.replace_comment_file);
            }
        }
    }
    else if (opts.append_comment_file) {
        if (opts.op_mode == OP_MODE_WRITE_BIN) { imd_report(IMD_REPORT_LEVEL_WARNING, "-AC ignored when writing binary output (-B)."); }
        else if (fout) {
            fcomment = fopen(opts.append_comment_file, "r");
            if (!fcomment) { fprintf(stderr, "Error opening append comment file '%s': %s\n", opts.append_comment_file, strerror(errno)); }
            else {
                fseek(fcomment, 0, SEEK_END);
                long append_size_long = ftell(fcomment);
                if (append_size_long < 0) { perror("ftell append file"); fclose(fcomment); goto cleanup; }
                size_t append_size = (size_t)append_size_long;
                fseek(fcomment, 0, SEEK_SET);
                size_t needs_crlf = (comment_size > 0 && comment_buffer[comment_size - 1] != '\n') ? 2 : 0;
                size_t new_size = comment_size + needs_crlf + append_size;
                char* new_buffer = (char*)realloc(comment_buffer, new_size + 1);
                if (!new_buffer) { perror("realloc comment for append"); fclose(fcomment); goto cleanup; }
                comment_buffer = new_buffer;
                if (needs_crlf) { comment_buffer[comment_size++] = '\r'; comment_buffer[comment_size++] = '\n'; }
                if (fread(comment_buffer + comment_size, 1, append_size, fcomment) != append_size) { perror("Error reading append comment file"); /* Continue? */ }
                comment_size += append_size;
                comment_buffer[comment_size] = '\0';
                fclose(fcomment); fcomment = NULL;
                if (!opts.quiet) printf("Comment appended from '%s'\n", opts.append_comment_file);
            }
        }
    }


    /* --- Write Header and (Modified) Comment to Output using libimd --- */
    if (fout && opts.op_mode == OP_MODE_WRITE_IMD) {
        char version_buf[64];
        snprintf(version_buf, sizeof(version_buf), "(Cross-Platform) %s [%s]", CMAKE_VERSION_STR, GIT_VERSION_STR);
        if (imd_write_file_header(fout, version_buf) != 0) {
            fprintf(stderr, "Error: Failed to write header to output file.\n");
            goto cleanup;
        }
        if (imd_write_comment_block(fout, comment_buffer, comment_size) != 0) {
            fprintf(stderr, "Error: Failed to write comment to output file.\n");
            goto cleanup;
        }
    }

    /* --- Print Binary Interleave Info (if applicable) --- */
    if (fout && opts.op_mode == OP_MODE_WRITE_BIN && !opts.quiet) {
        const char* il_desc;
        if (opts.interleave == LIBIMD_IL_AS_READ) { il_desc = "As Read"; }
        else if (opts.interleave == LIBIMD_IL_BEST_GUESS) { il_desc = "Best Guess"; }
        else { static char il_buf[10]; snprintf(il_buf, sizeof(il_buf), "%d:1", opts.interleave); il_desc = il_buf; }
        printf("Writing Binary, Interleave: %s\n", il_desc);
    }


    /* --- Process Tracks (with potential merge) --- */
    memset(&primary_track, 0, sizeof(ImdTrackInfo));
    memset(&merge_track, 0, sizeof(ImdTrackInfo));

    ImdWriteOpts write_opts = { 0 };
    write_opts.compression_mode = opts.compression_mode; /* Use the parsed mode */
    write_opts.force_non_bad = opts.force_non_bad;
    write_opts.force_non_deleted = opts.force_non_deleted;
    memcpy(write_opts.tmode, opts.tmode, sizeof(opts.tmode));
    write_opts.interleave_factor = opts.interleave;


    while (!primary_eof || !merge_eof) {
        ImdTrackInfo* track_to_process = NULL; /* Declare outside to use after block */

        if (!primary_eof && !primary_track.loaded) {
            int load_status = imd_load_track(fimd, &primary_track, opts.fill_specified ? opts.fill_byte : IMDU_FILL_BYTE_DEFAULT);
            if (load_status == 0) { primary_eof = 1; primary_track.loaded = 0; }
            else if (load_status < 0) { fprintf(stderr, "Error: Failed to load track from primary input file.\n"); goto cleanup; }
        }
        if (fmerge && !merge_eof && !merge_track.loaded) {
            int load_status = imd_load_track(fmerge, &merge_track, opts.fill_specified ? opts.fill_byte : IMDU_FILL_BYTE_DEFAULT);
            if (load_status == 0) { merge_eof = 1; merge_track.loaded = 0; }
            else if (load_status < 0) { fprintf(stderr, "Error: Failed to load track from merge input file.\n"); goto cleanup; }
        }


        if (primary_track.loaded && merge_track.loaded) {
            if (primary_track.cyl < merge_track.cyl || (primary_track.cyl == merge_track.cyl && primary_track.head < merge_track.head)) {
                track_to_process = &primary_track;
            }
            else if (merge_track.cyl < primary_track.cyl || (merge_track.cyl == primary_track.cyl && merge_track.head < primary_track.head)) {
                track_to_process = &merge_track;
            }
            else { /* Tracks match C/H */
                if (!opts.quiet && opts.detail) printf("  Merging C:%u H:%u (Using Primary)\n", primary_track.cyl, primary_track.head);
                track_to_process = &primary_track;
                imd_free_track_data(&merge_track);
                memset(&merge_track, 0, sizeof(ImdTrackInfo));
            }
        }
        else if (primary_track.loaded) { track_to_process = &primary_track; }
        else if (merge_track.loaded) { track_to_process = &merge_track; }
        else { break; }


        if (track_to_process) {
            if (!opts.quiet) {
                int format_changed = (track_to_process->mode != last_mode_printed ||
                    track_to_process->num_sectors != last_nsec_printed ||
                    track_to_process->sector_size != last_size_printed);
                if (format_changed) {
                    printf("%u/%u ", track_to_process->cyl, track_to_process->head);
                    if (track_to_process->mode < LIBIMD_NUM_MODES) {
                        printf("%u kbps %s %ux%u\n", MODE_RATES[track_to_process->mode],
                            (track_to_process->mode > 2 ? "MFM" : "FM"),
                            track_to_process->num_sectors, track_to_process->sector_size);
                    }
                    else {
                        printf("InvalidMode %u %ux%u\n", track_to_process->mode,
                            track_to_process->num_sectors, track_to_process->sector_size);
                    }
                    last_mode_printed = track_to_process->mode;
                    last_nsec_printed = track_to_process->num_sectors;
                    last_size_printed = track_to_process->sector_size;
                }
            }

            uint8_t skip_mask = opts.skip_track[track_to_process->cyl];
            uint8_t side_bit = (track_to_process->head == 0) ? IMD_SIDE_0_MASK : IMD_SIDE_1_MASK;
            if (skip_mask & side_bit) {
                if (!opts.quiet && opts.detail) printf("  Skipping Track: C=%u H=%u (Excluded by -X)\n", track_to_process->cyl, track_to_process->head);
                imd_free_track_data(track_to_process);
                if (track_to_process == &primary_track) memset(&primary_track, 0, sizeof(ImdTrackInfo));
                else memset(&merge_track, 0, sizeof(ImdTrackInfo));
                continue;
            }

            /* --- Add Missing Sectors --- */
            if (opts.add_missing_sectors_active && track_to_process->sector_size > 0 &&
                track_to_process->num_sectors < (uint8_t)opts.add_missing_sectors_target) {
                uint8_t target_total_spt = (uint8_t)opts.add_missing_sectors_target;
                if (target_total_spt > LIBIMD_MAX_SECTORS_PER_TRACK) {
                    imd_report(IMD_REPORT_LEVEL_WARNING, "Target sectors %d for C:%u H:%u exceeds max %d. Clamping.",
                        target_total_spt, track_to_process->cyl, track_to_process->head, LIBIMD_MAX_SECTORS_PER_TRACK);
                    target_total_spt = LIBIMD_MAX_SECTORS_PER_TRACK;
                }

                uint8_t num_to_add = 0;
                if (target_total_spt > track_to_process->num_sectors) { // Ensure target is greater
                    num_to_add = target_total_spt - track_to_process->num_sectors;
                }


                if (num_to_add > 0) {
                    if (!opts.quiet && opts.detail) {
                        printf("  Adding %u missing sectors to C:%u H:%u (current: %u, target: %u)\n",
                            num_to_add, track_to_process->cyl, track_to_process->head, track_to_process->num_sectors, target_total_spt);
                    }

                    size_t old_data_size = track_to_process->data_size;
                    size_t new_required_data_size = (size_t)target_total_spt * track_to_process->sector_size;
                    uint8_t* new_data_ptr = track_to_process->data;

                    if (new_required_data_size > old_data_size || (track_to_process->data == NULL && new_required_data_size > 0)) {
                        new_data_ptr = (uint8_t*)realloc(track_to_process->data, new_required_data_size);
                        if (!new_data_ptr && new_required_data_size > 0) {
                            imd_report(IMD_REPORT_LEVEL_ERROR, "Failed to realloc data buffer for adding sectors on C:%u H:%u.",
                                track_to_process->cyl, track_to_process->head);
                            num_to_add = 0;
                        }
                        else {
                            track_to_process->data = new_data_ptr;
                            if (new_data_ptr && new_required_data_size > old_data_size) {
                                memset(track_to_process->data + old_data_size,
                                    opts.fill_specified ? opts.fill_byte : IMDU_FILL_BYTE_DEFAULT,
                                    new_required_data_size - old_data_size);
                            }
                        }
                    }
                    track_to_process->data_size = new_required_data_size;


                    if (num_to_add > 0 && track_to_process->data) {
                        uint8_t used_ids[256] = { 0 };
                        for (uint8_t k = 0; k < track_to_process->num_sectors; ++k) {
                            if (track_to_process->smap[k] < 256) used_ids[track_to_process->smap[k]] = 1;
                        }

                        uint8_t next_smap_id_candidate = 0;
                        uint8_t current_physical_idx_for_add = track_to_process->num_sectors;

                        for (uint8_t k = 0; k < num_to_add; ++k) {
                            if (current_physical_idx_for_add >= LIBIMD_MAX_SECTORS_PER_TRACK) break;

                            while (next_smap_id_candidate < 256 && used_ids[next_smap_id_candidate]) {
                                next_smap_id_candidate++;
                            }
                            if (next_smap_id_candidate >= 256) {
                                imd_report(IMD_REPORT_LEVEL_WARNING, "Could not find unique ID for added sector on C:%u H:%u.",
                                    track_to_process->cyl, track_to_process->head);
                                break;
                            }

                            track_to_process->smap[current_physical_idx_for_add] = next_smap_id_candidate;
                            if (next_smap_id_candidate < 256) used_ids[next_smap_id_candidate] = 1;

                            track_to_process->sflag[current_physical_idx_for_add] = IMD_SDR_UNAVAILABLE;
                            if (track_to_process->hflag & IMD_HFLAG_CMAP_PRES) {
                                track_to_process->cmap[current_physical_idx_for_add] = track_to_process->cyl;
                            }
                            if (track_to_process->hflag & IMD_HFLAG_HMAP_PRES) {
                                track_to_process->hmap[current_physical_idx_for_add] = track_to_process->head;
                            }
                            current_physical_idx_for_add++;
                        }
                        track_to_process->num_sectors = current_physical_idx_for_add;
                    }
                }
            }
            /* --- End Add Missing Sectors --- */


            track_count++;
            if (!opts.quiet && opts.detail) {
                printf("  SMap:"); for (int i = 0; i < track_to_process->num_sectors; ++i) printf(" %u", track_to_process->smap[i]); printf("\n");
                if (track_to_process->hflag & IMD_HFLAG_CMAP_PRES) { printf("  CMap:"); for (int i = 0; i < track_to_process->num_sectors; ++i) printf(" %u", track_to_process->cmap[i]); printf("\n"); }
                if (track_to_process->hflag & IMD_HFLAG_HMAP_PRES) { printf("  HMap:"); for (int i = 0; i < track_to_process->num_sectors; ++i) printf(" %u", track_to_process->hmap[i]); printf("\n"); }
                printf("  Flags:"); for (int i = 0; i < track_to_process->num_sectors; ++i) printf(" %02X", track_to_process->sflag[i]); printf("\n");
            }


            if (fout) {
                if (opts.op_mode == OP_MODE_WRITE_BIN) {
                    if (imd_write_track_bin(fout, track_to_process, &write_opts) != 0) {
                        fprintf(stderr, "Error: Failed to write binary track data.\n"); goto cleanup;
                    }
                }
                else if (opts.op_mode == OP_MODE_WRITE_IMD) {
                    if (imd_write_track_imd(fout, track_to_process, &write_opts) != 0) {
                        fprintf(stderr, "Error: Failed to write IMD track data.\n"); goto cleanup;
                    }
                }
            }

            /* Update stats based on final state after potential write modifications */
            {
                uint8_t final_sflag[LIBIMD_MAX_SECTORS_PER_TRACK];
                /* Determine final flags based on write options (simulate write logic) */
                for (uint8_t i = 0; i < track_to_process->num_sectors; ++i) {
                    uint8_t original_flag = track_to_process->sflag[i];
                    uint8_t target_base_type;
                    int target_has_dam = 0;
                    int target_has_err = 0;

                    if (!IMD_SDR_HAS_DATA(original_flag)) {
                        final_sflag[i] = IMD_SDR_UNAVAILABLE;
                    }
                    else {
                        int is_uniform_sector = 0;
                        if (track_to_process->data && track_to_process->sector_size > 0 && track_to_process->data_size >= ((size_t)i + 1) * track_to_process->sector_size) {
                            uint8_t* sector_data = track_to_process->data + ((size_t)i * track_to_process->sector_size);
                            uint8_t dummy_fill;
                            is_uniform_sector = imd_is_uniform(sector_data, track_to_process->sector_size, &dummy_fill);
                        }
                        switch (write_opts.compression_mode) {
                        case IMD_COMPRESSION_FORCE_COMPRESS: target_base_type = is_uniform_sector ? IMD_SDR_COMPRESSED : IMD_SDR_NORMAL; break;
                        case IMD_COMPRESSION_FORCE_DECOMPRESS: target_base_type = IMD_SDR_NORMAL; break;
                        case IMD_COMPRESSION_AS_READ: default:
                            target_base_type = IMD_SDR_IS_COMPRESSED(original_flag) ? (is_uniform_sector ? IMD_SDR_COMPRESSED : IMD_SDR_NORMAL) : IMD_SDR_NORMAL; break;
                        }
                        target_has_dam = IMD_SDR_HAS_DAM(original_flag) && !write_opts.force_non_deleted;
                        target_has_err = IMD_SDR_HAS_ERR(original_flag) && !write_opts.force_non_bad;
                        if (target_base_type == IMD_SDR_NORMAL) {
                            if (target_has_dam && target_has_err) final_sflag[i] = IMD_SDR_DELETED_ERR;
                            else if (target_has_err) final_sflag[i] = IMD_SDR_NORMAL_ERR;
                            else if (target_has_dam) final_sflag[i] = IMD_SDR_NORMAL_DAM;
                            else final_sflag[i] = IMD_SDR_NORMAL;
                        }
                        else {
                            if (target_has_dam && target_has_err) final_sflag[i] = IMD_SDR_COMPRESSED_DEL_ERR;
                            else if (target_has_err) final_sflag[i] = IMD_SDR_COMPRESSED_ERR;
                            else if (target_has_dam) final_sflag[i] = IMD_SDR_COMPRESSED_DAM;
                            else final_sflag[i] = IMD_SDR_COMPRESSED;
                        }
                    }
                }
                /* Calculate stats based on final_sflag */
                for (uint8_t i = 0; i < track_to_process->num_sectors; ++i) {
                    stats[ST_TOTAL]++;
                    uint8_t flag = final_sflag[i];
                    if (IMD_SDR_HAS_DATA(flag)) {
                        if (IMD_SDR_IS_COMPRESSED(flag)) stats[ST_COMP]++;
                        if (IMD_SDR_HAS_DAM(flag)) stats[ST_DAM]++;
                        if (IMD_SDR_HAS_ERR(flag)) stats[ST_BAD]++;
                    }
                    else {
                        stats[ST_UNAVAIL]++;
                    }
                }
            } /* End stats update block */

            imd_free_track_data(track_to_process);
            if (track_to_process == &primary_track) memset(&primary_track, 0, sizeof(ImdTrackInfo));
            else memset(&merge_track, 0, sizeof(ImdTrackInfo));
        }

    } /* end while tracks */

    if (!opts.quiet) print_stats(track_count);
    result = EXIT_SUCCESS; /* Success! */

cleanup:
    if (fimd) fclose(fimd);
    if (fmerge) fclose(fmerge);
    if (fout) fclose(fout);
    if (comment_buffer) free(comment_buffer);
    imd_free_track_data(&primary_track);
    imd_free_track_data(&merge_track);
    if (opts.append_comment_file) free(opts.append_comment_file);
    if (opts.extract_comment_file) free(opts.extract_comment_file);
    if (opts.replace_comment_file) free(opts.replace_comment_file);


    return (result == EXIT_SUCCESS ? 0 : 1);
}
