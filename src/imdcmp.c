/*
 * Utility to compare ImageDisk files.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h> /* Explicitly include for va_list, va_start, etc. */
#include <ctype.h>  /* For toupper */

#include "libimd.h" /* Use the provided IMD library (includes defines) */
#include "libimd_utils.h" /* For common utilities */

 /* Define version strings - replace with actual build system values if available */
#ifndef CMAKE_VERSION_STR
#define CMAKE_VERSION_STR "0.1.0" /* Placeholder version */
#endif
#ifndef GIT_VERSION_STR
#define GIT_VERSION_STR "dev" /* Placeholder git revision */
#endif

/* --- Constants --- */
/* Exit codes */
#define EXIT_MATCH          0
#define EXIT_DIFF           1 /* General difference (content, header, maps, structure, multiple warnings w/ -Werror) */
#define EXIT_DIFF_COMPRESS  2 /* Compression difference treated as error (-S or -Werror) */
#define EXIT_DIFF_INTERLEAVE 3 /* Interleave difference treated as error (-Werror) */
#define EXIT_USAGE_ERROR    4 /* Command line usage error */
#define EXIT_FILE_ERROR     5 /* File access or read error */

/* Difference flags (internal bitmask) */
#define C_DIFF_NONE         0x000
#define C_DIFF_HEADER       0x001 /* Header mismatch (Not currently used for exit code) */
#define C_DIFF_COMMENT      0x002 /* Comment mismatch */
#define C_DIFF_TRACK_HDR    0x004 /* Track header mismatch (mode, nsec, size, cyl, head, hflag maps) */
#define C_DIFF_TRACK_MAP    0x008 /* Track map content mismatch (smap, cmap, hmap) */
#define C_DIFF_TRACK_DATA   0x010 /* Sector data content mismatch */
#define C_DIFF_TRACK_FLAG   0x020 /* Sector status/type flag mismatch (Error/DAM/Availability) */
#define C_DIFF_COMPRESS     0x040 /* Compression status differs (Normal vs Compressed) */
#define C_DIFF_INTERLEAVE   0x080 /* Calculated interleave differs */
#define C_DIFF_FILE_STRUCT  0x100 /* Files differ in track structure (e.g., one ends early) */

/* Mask for 'hard' differences that always cause exit code 1 */
#define C_MASK_HARD_DIFF (C_DIFF_COMMENT | C_DIFF_TRACK_HDR | C_DIFF_TRACK_MAP | \
                        C_DIFF_TRACK_DATA | C_DIFF_TRACK_FLAG | C_DIFF_FILE_STRUCT)


/* --- Data Structures --- */
typedef struct {
    const char* filename1;
    const char* filename2;
    int ignore_compression; /* -C flag: Treat compress diff as identical */
    int strict_compression; /* -S flag: Treat compress diff as error 2 */
    int quiet;              /* -Q flag: Suppress warnings and info */
    int warn_error;         /* -Werror flag: Treat warnings (compress, interleave) as errors */
    int detail;             /* -D flag: Show detailed differences */
} Options;

/* --- Helper Functions --- */

/* Removed local print_error */

/* Removed local print_warning */

/**
 * @brief Prints a detailed difference message to stderr, if detail mode is enabled.
 * @param opts Pointer to the options struct to check the detail flag.
 * @param format The message format string.
 * @param ... Variable arguments for the format string.
 */
static void print_detail(const Options* opts, const char* format, ...) {
    if (!opts || !opts->detail) return;
    va_list args;
    /* Indent detail lines for clarity */
    fprintf(stderr, "  Detail: "); /* Keep stderr for detail as it often follows a warning */
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/**
 * @brief Prints an array of bytes in hex format for detail view.
 * @param opts Options struct to check detail flag.
 * @param label Label for the array (e.g., "smap File 1").
 * @param array Pointer to the byte array.
 * @param size Number of bytes in the array.
 */
static void print_hex_array(const Options* opts, const char* label, const uint8_t* array, size_t size) {
    if (!opts || !opts->detail || !array || size == 0) return;
    fprintf(stderr, "    %s (%zu bytes):", label, size);
    for (size_t i = 0; i < size; ++i) {
        if (i % 16 == 0) fprintf(stderr, "\n      ");
        fprintf(stderr, " %02X", array[i]);
    }
    fprintf(stderr, "\n");
}

/**
 * @brief Prints a block of data in hex dump format for detail view.
 * @param opts Options struct to check detail flag.
 * @param label Label for the data block (e.g., "Data File 1").
 * @param data Pointer to the data.
 * @param size Number of bytes in the data block.
 */
static void print_hex_dump(const Options* opts, const char* label, const uint8_t* data, size_t size) {
    if (!opts || !opts->detail || !data || size == 0) return;
    fprintf(stderr, "    %s (%zu bytes):\n", label, size);
    for (size_t offset = 0; offset < size; offset += 16) {
        fprintf(stderr, "      %04zX: ", offset); /* Print offset */
        /* Print hex bytes */
        for (size_t i = 0; i < 16; ++i) {
            if (offset + i < size) {
                fprintf(stderr, "%02X ", data[offset + i]);
            }
            else {
                fprintf(stderr, "   "); /* Pad if past end */
            }
            if (i == 7) fprintf(stderr, " "); /* Extra space in middle */
        }
        fprintf(stderr, " |");
        /* Print ASCII chars */
        for (size_t i = 0; i < 16; ++i) {
            if (offset + i < size) {
                uint8_t c = data[offset + i];
                fprintf(stderr, "%c", (isprint(c) ? c : '.'));
            }
            else {
                fprintf(stderr, " ");
            }
        }
        fprintf(stderr, "|\n");
    }
}


/**
 * @brief Prints usage information. (Always printed when requested)
 * @param prog_name The program executable name.
 */
static void print_usage(const char* prog_name) {
    const char* base_prog_name = imd_get_basename(prog_name);
    if (!base_prog_name) base_prog_name = "imdcmp"; /* Fallback */

    fprintf(stderr, "ImageDisk Compare Utility %s [%s]\n", CMAKE_VERSION_STR, GIT_VERSION_STR);
    fprintf(stderr, "Usage: %s [options] <image1.imd> <image2.imd>\n\n", base_prog_name);
    fprintf(stderr, "Compares two ImageDisk (.IMD) files.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -C        : Ignore differences caused solely by sector compression flags.\n");
    fprintf(stderr, "              (Sectors must still match data content when expanded).\n");
    fprintf(stderr, "  -S        : Strict Compression check. Exit with code %d if compression flags\n", EXIT_DIFF_COMPRESS);
    fprintf(stderr, "              differ, even if data content matches.\n");
    fprintf(stderr, "  -Q        : Quiet mode. Suppress warnings and non-essential output.\n");
    fprintf(stderr, "  -D        : Detail mode. Print specific information about differences found.\n");
    fprintf(stderr, "  -Werror   : Treat warnings (like compression or interleave differences)\n");
    fprintf(stderr, "              as errors. Overridden by -S for compression.\n");
    fprintf(stderr, "  --help, -h: Display this help message and exit.\n");
    fprintf(stderr, "\nExit Codes:\n");
    fprintf(stderr, "  %d : Files match (or differ only by warnings without -Werror/-S).\n", EXIT_MATCH);
    fprintf(stderr, "  %d : Files differ (content, header, maps, flags, structure, multiple warnings w/ -Werror).\n", EXIT_DIFF);
    fprintf(stderr, "  %d : Files differ ONLY in compression flags (requires -S or -Werror).\n", EXIT_DIFF_COMPRESS);
    fprintf(stderr, "  %d : Files differ ONLY in sector interleave (requires -Werror).\n", EXIT_DIFF_INTERLEAVE);
    fprintf(stderr, "  %d : Command line usage error.\n", EXIT_USAGE_ERROR);
    fprintf(stderr, "  %d : File access or read error.\n", EXIT_FILE_ERROR);
}

/**
 * @brief Parses command line arguments.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param opts Pointer to Options structure to fill.
 * @return 0 on success, -1 on error.
 */
static int parse_args(int argc, char* argv[], Options* opts) {
    memset(opts, 0, sizeof(Options)); /* Initialize options */
    int file_count = 0;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            /* Check for multi-char options first */
            if (strcmp(argv[i], "-Werror") == 0) {
                opts->warn_error = 1;
            }
            else if (strcmp(argv[i], "--help") == 0) {
                print_usage(argv[0]);
                exit(EXIT_MATCH); /* Exit 0 for help */
            }
            /* Handle single char options */
            else if (strlen(argv[i]) == 2) {
                char opt_char = (char)toupper((unsigned char)argv[i][1]);
                switch (opt_char) {
                case 'C':
                    if (opts->strict_compression) {
                        imd_report(IMD_REPORT_LEVEL_WARNING, "-S specified, ignoring -C option.");
                    }
                    else {
                        opts->ignore_compression = 1;
                    }
                    break;
                case 'S': /* -S overrides -C */
                    opts->strict_compression = 1;
                    if (opts->ignore_compression) {
                        imd_report(IMD_REPORT_LEVEL_WARNING, "Overriding -C with -S.");
                        opts->ignore_compression = 0;
                    }
                    break;
                case 'Q': opts->quiet = 1; break;
                case 'D': opts->detail = 1; break;
                case 'W': /* Check if it's just -W, treat as -Werror */
                    if (argv[i][1] == 'W') {
                        opts->warn_error = 1;
                    }
                    else {
                        fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
                        return -1;
                    }
                    break;
                case 'H': /* Handle -h */
                    print_usage(argv[0]);
                    exit(EXIT_MATCH); /* Exit 0 for help */
                default:
                    fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
                    return -1;
                }
            }
            else {
                fprintf(stderr, "Error: Unknown or malformed option '%s'\n", argv[i]);
                return -1;
            }
        }
        else {
            /* Assume it's a filename */
            if (file_count == 0) {
                opts->filename1 = argv[i];
            }
            else if (file_count == 1) {
                opts->filename2 = argv[i];
            }
            else {
                fprintf(stderr, "Error: Too many filename arguments.\n");
                return -1;
            }
            file_count++;
        }
    }

    if (file_count != 2) {
        fprintf(stderr, "Error: Exactly two filenames are required.\n");
        return -1;
    }

    return 0; /* Success */
}


/* --- Main Comparison Logic --- */

int main(int argc, char* argv[]) {
    Options opts;
    FILE* fimd1 = NULL, * fimd2 = NULL;
    char header1[LIBIMD_MAX_HEADER_LINE], header2[LIBIMD_MAX_HEADER_LINE]; /* Buffers for raw header line */
    char* comment1 = NULL, * comment2 = NULL;
    size_t comment1_size = 0, comment2_size = 0;
    ImdTrackInfo track1 = { 0 }, track2 = { 0 };
    int final_return_code = EXIT_MATCH;
    int diff_flags = C_DIFF_NONE;
    int eof1 = 0, eof2 = 0;
    int track_count = 0; /* Keep track of the track number for messages */
    ImdHeaderInfo header_info1, header_info2; /* To store parsed header info (optional) */

    /* --- Argument Parsing --- */
    if (parse_args(argc, argv, &opts) != 0) {
        print_usage(argv[0]);
        return EXIT_USAGE_ERROR;
    }

    /* Set verbosity level for reporting library */
    /* imdcmp uses -Q (quiet) and -D (detail/verbose) */
    imd_set_verbosity(opts.quiet, opts.detail);

    /* --- Open files --- */
    fimd1 = fopen(opts.filename1, "rb");
    if (!fimd1) {
        imd_report(IMD_REPORT_LEVEL_ERROR, "Cannot open input file '%s': %s", opts.filename1, strerror(errno));
        return EXIT_FILE_ERROR;
    }
    fimd2 = fopen(opts.filename2, "rb");
    if (!fimd2) {
        imd_report(IMD_REPORT_LEVEL_ERROR, "Cannot open input file '%s': %s", opts.filename2, strerror(errno));
        fclose(fimd1);
        return EXIT_FILE_ERROR;
    }

    /* --- Compare Headers and Comments --- */
    /* Read headers using libimd function */
    if (imd_read_file_header(fimd1, &header_info1, header1, sizeof(header1)) != 0) {
        imd_report(IMD_REPORT_LEVEL_ERROR, "Error reading file header from '%s'.", opts.filename1);
        final_return_code = EXIT_FILE_ERROR;
        goto cleanup;
    }
    if (imd_read_file_header(fimd2, &header_info2, header2, sizeof(header2)) != 0) {
        imd_report(IMD_REPORT_LEVEL_ERROR, "Invalid IMD signature in '%s'.", opts.filename2);
        final_return_code = EXIT_FILE_ERROR;
        goto cleanup;
    }
    /* Optionally compare header content if needed, but libimd already validated the signature */
    /* For example: if (strcmp(header1, header2) != 0) { diff_flags |= C_DIFF_HEADER; } */

    /* Read and compare comments */
    comment1 = imd_read_comment_block(fimd1, &comment1_size);
    comment2 = imd_read_comment_block(fimd2, &comment2_size);
    if (comment1 == NULL || comment2 == NULL) {
        imd_report(IMD_REPORT_LEVEL_ERROR, "Error reading comments.");
        final_return_code = EXIT_FILE_ERROR; goto cleanup;
    }
    if (comment1_size != comment2_size || memcmp(comment1, comment2, comment1_size) != 0) {
        imd_report(IMD_REPORT_LEVEL_WARNING, "Comments differ.");
        diff_flags |= C_DIFF_COMMENT;
        print_detail(&opts, "Comment sizes: %zu vs %zu", comment1_size, comment2_size);
        /* Optionally add print_hex_dump for comments here if needed for detail */
    }

    /* --- Track Comparison Loop --- */
    while (!eof1 || !eof2) {
        int load1_status = 0, load2_status = 0;
        int current_track_diffs = C_DIFF_NONE;

        if (!eof1) {
            load1_status = imd_load_track(fimd1, &track1, LIBIMD_FILL_BYTE_DEFAULT);
            if (load1_status == 0) eof1 = 1;
            else if (load1_status < 0) { imd_report(IMD_REPORT_LEVEL_ERROR, "Error loading track from %s", opts.filename1); final_return_code = EXIT_FILE_ERROR; break; }
        }
        if (!eof2) {
            load2_status = imd_load_track(fimd2, &track2, LIBIMD_FILL_BYTE_DEFAULT);
            if (load2_status == 0) eof2 = 1;
            else if (load2_status < 0) { imd_report(IMD_REPORT_LEVEL_ERROR, "Error loading track from %s", opts.filename2); final_return_code = EXIT_FILE_ERROR; break; }
        }

        if (eof1 != eof2) {
            imd_report(IMD_REPORT_LEVEL_WARNING, "Files differ in number of tracks (Structure mismatch).");
            print_detail(&opts, "File %d ended prematurely.", eof1 ? 1 : 2); /* Use local print_detail */
            diff_flags |= C_DIFF_FILE_STRUCT; break;
        }
        if (eof1 && eof2) break;

        track_count++;

        /* Compare track headers */
        if (track1.mode != track2.mode || track1.cyl != track2.cyl || track1.head != track2.head ||
            track1.num_sectors != track2.num_sectors || track1.sector_size_code != track2.sector_size_code ||
            track1.hflag != track2.hflag)
        {
            imd_report(IMD_REPORT_LEVEL_WARNING, "Track %d (C:%u H:%u vs C:%u H:%u): Headers differ.", track_count, track1.cyl, track1.head, track2.cyl, track2.head);
            if (track1.cyl != track2.cyl) print_detail(&opts, "Cylinder mismatch: %u vs %u", track1.cyl, track2.cyl); /* Use local print_detail */
            if (track1.head != track2.head) print_detail(&opts, "Head mismatch: %u vs %u", track1.head, track2.head);
            if (track1.mode != track2.mode) print_detail(&opts, "Mode mismatch: %u vs %u", track1.mode, track2.mode);
            if (track1.num_sectors != track2.num_sectors) print_detail(&opts, "Num Sectors mismatch: %u vs %u", track1.num_sectors, track2.num_sectors);
            if (track1.sector_size_code != track2.sector_size_code) print_detail(&opts, "Sector Size Code mismatch: %u vs %u", track1.sector_size_code, track2.sector_size_code);
            if (track1.hflag != track2.hflag) print_detail(&opts, "Head Flags mismatch: 0x%02X vs 0x%02X", track1.hflag, track2.hflag);
            current_track_diffs |= C_DIFF_TRACK_HDR;
        }
        else { /* Only compare maps/data if basic headers match */
            /* Compare optional maps if present in both */
            if ((track1.hflag & IMD_HFLAG_CMAP_PRES) && memcmp(track1.cmap, track2.cmap, track1.num_sectors) != 0) {
                imd_report(IMD_REPORT_LEVEL_WARNING, "Track %d (C:%u H:%u): Cylinder Map content differs.", track_count, track1.cyl, track1.head);
                current_track_diffs |= C_DIFF_TRACK_MAP;
                print_hex_array(&opts, "cmap File 1", track1.cmap, track1.num_sectors);
                print_hex_array(&opts, "cmap File 2", track2.cmap, track2.num_sectors);
            }
            if ((track1.hflag & IMD_HFLAG_HMAP_PRES) && memcmp(track1.hmap, track2.hmap, track1.num_sectors) != 0) {
                imd_report(IMD_REPORT_LEVEL_WARNING, "Track %d (C:%u H:%u): Head Map content differs.", track_count, track1.cyl, track1.head);
                current_track_diffs |= C_DIFF_TRACK_MAP;
                print_hex_array(&opts, "hmap File 1", track1.hmap, track1.num_sectors);
                print_hex_array(&opts, "hmap File 2", track2.hmap, track2.num_sectors);
            }
            /* Compare sector maps */
            if (memcmp(track1.smap, track2.smap, track1.num_sectors) != 0) {
                imd_report(IMD_REPORT_LEVEL_WARNING, "Track %d (C:%u H:%u): Sector numbering maps (smap) differ.", track_count, track1.cyl, track1.head);
                current_track_diffs |= C_DIFF_TRACK_MAP;
                print_hex_array(&opts, "smap File 1", track1.smap, track1.num_sectors);
                print_hex_array(&opts, "smap File 2", track2.smap, track2.num_sectors);
            }

            /* Calculate and Compare Interleave */
            int il1 = imd_calculate_best_interleave(&track1);
            int il2 = imd_calculate_best_interleave(&track2);
            if (il1 != il2) {
                imd_report(IMD_REPORT_LEVEL_WARNING, "Track %d (C:%u H:%u): Calculated interleave differs (%d vs %d)", track_count, track1.cyl, track1.head, il1, il2);
                current_track_diffs |= C_DIFF_INTERLEAVE;
            }

            /* Compare sector flags and data */
            for (int i = 0; i < track1.num_sectors; ++i) {
                uint8_t flag1 = track1.sflag[i];
                uint8_t flag2 = track2.sflag[i];
                uint8_t* data1 = track1.data + (i * track1.sector_size);
                uint8_t* data2 = track2.data + (i * track2.sector_size);
                size_t data_size = track1.sector_size; /* Assume sizes match */

                /* Compare data content first */
                if (memcmp(data1, data2, data_size) != 0) {
                    imd_report(IMD_REPORT_LEVEL_WARNING, "Track %d (C:%u H:%u) Sector %d (ID %u): Data differs.", track_count, track1.cyl, track1.head, i, track1.smap[i]);
                    current_track_diffs |= C_DIFF_TRACK_DATA;
                    /* Detail: Print full hex dump of both sectors */
                    print_hex_dump(&opts, "Data File 1", data1, data_size);
                    print_hex_dump(&opts, "Data File 2", data2, data_size);
                }

                /* Compare flags */
                if (flag1 != flag2) {
                    /* Only report detailed flag diff if not ignoring compression or if flags differ by more than just compression */
                    int compress_diff_only = (
                        (IMD_SDR_IS_COMPRESSED(flag1) != IMD_SDR_IS_COMPRESSED(flag2)) &&
                        (IMD_SDR_HAS_DATA(flag1) == IMD_SDR_HAS_DATA(flag2)) &&
                        (IMD_SDR_HAS_ERR(flag1) == IMD_SDR_HAS_ERR(flag2)) &&
                        (IMD_SDR_HAS_DAM(flag1) == IMD_SDR_HAS_DAM(flag2)));

                    if (compress_diff_only && !opts.ignore_compression) {
                        imd_report(IMD_REPORT_LEVEL_WARNING, "Track %d (C:%u H:%u) Sector %d (ID %u): Compression status differs (0x%02X vs 0x%02X).",
                            track_count, track1.cyl, track1.head, i, track1.smap[i], flag1, flag2);
                        print_detail(&opts, "Flags: File1=0x%02X, File2=0x%02X", flag1, flag2); /* Use local print_detail */
                        current_track_diffs |= C_DIFF_COMPRESS;
                    }
                    else if (!compress_diff_only) { /* Difference is more than just compression (or compression ignored) */
                        imd_report(IMD_REPORT_LEVEL_WARNING, "Track %d (C:%u H:%u) Sector %d (ID %u): Flags differ (0x%02X vs 0x%02X).",
                            track_count, track1.cyl, track1.head, i, track1.smap[i], flag1, flag2);
                        print_detail(&opts, "Flags: File1=0x%02X, File2=0x%02X", flag1, flag2);
                        current_track_diffs |= C_DIFF_TRACK_FLAG;
                    }
                }

            } /* End sector loop */
        } /* End if headers match */

        diff_flags |= current_track_diffs;

        imd_free_track_data(&track1);
        imd_free_track_data(&track2);
        memset(&track1, 0, sizeof(ImdTrackInfo));
        memset(&track2, 0, sizeof(ImdTrackInfo));

        if (diff_flags & C_MASK_HARD_DIFF) break; /* Stop if hard difference found */
    } /* End while tracks */


cleanup:
    if (comment1) free(comment1);
    if (comment2) free(comment2);
    imd_free_track_data(&track1);
    imd_free_track_data(&track2);
    if (fimd1) fclose(fimd1);
    if (fimd2) fclose(fimd2);

    /* --- Determine Final Exit Code --- */
    if (final_return_code == EXIT_FILE_ERROR) { /* File error already set */ }
    else if (diff_flags & C_MASK_HARD_DIFF) {
        final_return_code = EXIT_DIFF; /* Keep local print_error for final summary */
        /* Use imd_report_error for consistency if desired, or keep local fprintf */
        fprintf(stderr, "Error: Files differ (Hard mismatch found).\n");
    }
    else { /* No hard differences, check warnings */
        int has_compress_diff = (diff_flags & C_DIFF_COMPRESS);
        int has_interleave_diff = (diff_flags & C_DIFF_INTERLEAVE);

        if (has_compress_diff && opts.strict_compression) {
            final_return_code = EXIT_DIFF_COMPRESS;
            fprintf(stderr, "Error: Files differ: Compression mismatch (Strict Mode).\n"); /* Keep local fprintf */
        }
        else if ((has_compress_diff || has_interleave_diff) && opts.warn_error) {
            if (has_compress_diff && has_interleave_diff) {
                final_return_code = EXIT_DIFF;
                fprintf(stderr, "Error: Files differ: Multiple warnings treated as errors (-Werror).\n"); /* Keep local fprintf */
            }
            else if (has_compress_diff) {
                final_return_code = EXIT_DIFF_COMPRESS;
                fprintf(stderr, "Error: Files differ: Compression warning treated as error (-Werror).\n"); /* Keep local fprintf */
            }
            else { /* Only interleave diff */
                final_return_code = EXIT_DIFF_INTERLEAVE;
                fprintf(stderr, "Error: Files differ: Interleave warning treated as error (-Werror).\n"); /* Keep local fprintf */
            }
        }
        else if (has_compress_diff || has_interleave_diff) {
            final_return_code = EXIT_MATCH;
            if (!opts.quiet) {
                /* Use the library warning function for consistency */
                if (has_compress_diff && has_interleave_diff) imd_report(IMD_REPORT_LEVEL_WARNING, "Files differ only by warnings (Compression, Interleave). Treating as match.");
                else if (has_compress_diff) imd_report(IMD_REPORT_LEVEL_WARNING, "Files differ only by warning (Compression). Treating as match.");
                else imd_report(IMD_REPORT_LEVEL_WARNING, "Files differ only by warning (Interleave). Treating as match.");
            }
        }
        else {
            final_return_code = EXIT_MATCH;
            if (!opts.quiet) printf("Files match.\n");
        }
    }

    return final_return_code;
}
