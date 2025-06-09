/*
 * ImageDisk Analyzer (Cross-Platform.)
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
#include <ctype.h>

#include "libimd.h" /* Use our IMD library (includes defines) */
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
/* MAX_HEADER_LINE is defined in libimd.h */

/* Analysis Constants (based on original IMDA.C - specific to this tool) */
#define BYTES_PER_SEC_500K 62500 /* 500,000 / 8 */
#define BYTES_PER_SEC_300K 37500 /* 300,000 / 8 */
#define BYTES_PER_SEC_250K 31250 /* 250,000 / 8 */
#define SECTOR_OVERHEAD_GUESS 85
#define CYLINDER_OVERHEAD_GUESS 85

/* Drive descriptor flags (conceptual, based on original IMDA.C - specific to this tool) */
/* These represent potential output drive types */
#define DRIVE_TYPE_D35_DD  0x00 /* 3.5" DD 80-track */
#define DRIVE_TYPE_D35_HD  0x01 /* 3.5" HD 80-track */
#define DRIVE_TYPE_D525_DD_40 0x02 /* 5.25" DD 40-track */
#define DRIVE_TYPE_D525_DD_80 0x03 /* 5.25" DD 80-track (QD) */
#define DRIVE_TYPE_D525_HD 0x04 /* 5.25" HD 80-track */
#define DRIVE_TYPE_D8    0x05 /* 8"    SS/DS 77-track */
#define DRIVE_TYPE_MASK  0x07

/* Option flags (conceptual - specific to this tool) */
#define OPTION_DSTEP 0x10 /* Double-stepping needed */
#define OPTION_T32   0x20 /* Translate 300->250 kbps */
#define OPTION_T23   0x40 /* Translate 250->300 kbps */

/* Note flags (conceptual - specific to this tool) */
#define NOTE_40TRACK   0x0100 /* Fits on 40 tracks */
#define NOTE_77TRACK   0x0200 /* Likely 77 track drive (8") */
#define NOTE_360RPM    0x0400 /* Fits on 360 RPM drive (relevant for 500kbps) */

/* --- Global State --- */
int quiet_mode = 0;

/* --- Helper Functions --- */

/* removed local error_exit */

/**
 * @brief Prints usage information.
 */
void print_usage(const char* prog_name) {
    const char* base_prog_name = imd_get_basename(prog_name); /* Get basename using library function */
    fprintf(stderr, "ImageDisk Analyzer (Cross-Platform) %s [%s]\n",
        CMAKE_VERSION_STR, GIT_VERSION_STR);
    fprintf(stderr, "Copyright (C) 2025 - Howard M. Harte - https://github.com/hharte/imd-utils\n\n");
    fprintf(stderr, "The original MS-DOS version is available from http://dunfield.classiccmp.org/img/\n\n");

    fprintf(stderr, "Usage: %s <image.imd> [-Q]\n\n", base_prog_name);
    fprintf(stderr, "Analyzes an IMD file and recommends drive types/options for recreation.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -Q      : Quiet mode (suppress summary and comment display).\n");
    fprintf(stderr, "  --help  : Display this help message and exit.\n");
}

/**
 * @brief Prints drive recommendation based on flags.
 */
void print_drive_recommendation(uint32_t flags, uint8_t* notes_printed, int* note_idx) {
    printf("\n");
    switch (flags & DRIVE_TYPE_MASK) {
    case DRIVE_TYPE_D35_DD:     printf(" 3.5\" DD 80-track"); break;
    case DRIVE_TYPE_D35_HD:     printf(" 3.5\" HD 80-track"); break;
    case DRIVE_TYPE_D525_DD_40: printf(" 5.25\" DD 40-track"); break;
    case DRIVE_TYPE_D525_DD_80: printf(" 5.25\" QD 80-track"); break;
    case DRIVE_TYPE_D525_HD:    printf(" 5.25\" HD 80-track"); break;
    case DRIVE_TYPE_D8:         printf(" 8\"    SS/DS 77-track"); break;
    default:                    printf(" Unknown Drive Type"); break;
    }

    /* Print associated notes */
    if (flags & (NOTE_40TRACK | NOTE_77TRACK | NOTE_360RPM)) {
        printf("   NOTE:");
        if (flags & NOTE_40TRACK) {
            /* FIX C4244: Cast int to uint8_t */
            if (!(notes_printed[0])) { notes_printed[0] = (uint8_t)(++(*note_idx)); }
            printf(" *%d", notes_printed[0]);
        }
        if (flags & NOTE_77TRACK) {
            /* FIX C4244: Cast int to uint8_t */
            if (!(notes_printed[1])) { notes_printed[1] = (uint8_t)(++(*note_idx)); }
            printf(" *%d", notes_printed[1]);
        }
        if (flags & NOTE_360RPM) {
            /* FIX C4244: Cast int to uint8_t */
            if (!(notes_printed[2])) { notes_printed[2] = (uint8_t)(++(*note_idx)); }
            printf(" *%d", notes_printed[2]);
        }
    }
    printf("\n");

    /* Print associated options */
    printf("   IMD Options: ");
    int first_opt = 1;
    if (flags & OPTION_DSTEP) { printf("DS=1 (Double Step)"); first_opt = 0; }
    if (flags & OPTION_T32) { printf("%sT300=250", first_opt ? "" : ", "); first_opt = 0; }
    if (flags & OPTION_T23) { printf("%sT250=300", first_opt ? "" : ", "); first_opt = 0; }
    if (first_opt) { printf("(none)"); }
    printf("\n");
}


/* --- Main Entry Point --- */

int main(int argc, char* argv[]) {
    const char* input_filename = NULL;
    FILE* fimd = NULL;
    char header_line[LIBIMD_MAX_HEADER_LINE]; /* Use define from libimd.h */
    ImdTrackInfo track_info;
    /* Initialize to prevent uninitialized use */
    /* Initialize to prevent uninitialized use */
    /* Initialize to prevent uninitialized use */
    int result = EXIT_FAILURE;

    /* --- Argument Parsing --- */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-Q") == 0) {
            quiet_mode = 1;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "Warning: Unknown option '%s'\n", argv[i]);
        }
        else if (!input_filename) {
            input_filename = argv[i];
        }
        else {
            fprintf(stderr, "Error: Too many file arguments.\n");
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!input_filename) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    /* Set verbosity level for reporting library */
    imd_set_verbosity(quiet_mode, 0); /* imda only has quiet mode */

    if (!quiet_mode) printf("ImageDisk Analyzer (Cross-Platform) %s [%s] - Analyzing '%s'\n",
        CMAKE_VERSION_STR, GIT_VERSION_STR, input_filename);

    /* --- Open Input File --- */
    fimd = fopen(input_filename, "rb");
    if (!fimd) {
        fprintf(stderr, "Error: Cannot open input file '%s': %s\n", input_filename, strerror(errno));
        goto cleanup;
    }

    /* --- Read Header & Comment --- */
    /* Use libimd function to read header */
    if (imd_read_file_header(fimd, NULL, header_line, sizeof(header_line)) != 0) {
        imd_report_error_exit("Failed to read or parse IMD header line.");
    }
    if (!quiet_mode) printf("IMD Header: %s\n", header_line);

    /* Skip comment block */
    if (!quiet_mode) printf("Comment:\n---\n");
    if (imd_skip_comment_block(fimd) != 0) {
        /* If not quiet, print the comment characters as they are skipped */
        if (!quiet_mode) {
            /* Need to rewind and read char by char for display */
            /* This is slightly inefficient but matches original behavior */
            long comment_start_pos = ftell(fimd); /* Get position after header */
            if (comment_start_pos >= 0) {
                fseek(fimd, -(long)strlen(header_line) - 2, SEEK_CUR); /* Approx rewind */
                /* Re-read header to position correctly */
                (void)fgets(header_line, sizeof(header_line), fimd);
                int c;
                while ((c = fgetc(fimd)) != EOF && c != LIBIMD_COMMENT_EOF_MARKER) {
                    putchar(c);
                }
                if (c == EOF) imd_report_error_exit("EOF found before comment terminator (0x1A).");
            }
            else {
                imd_report_error_exit("Could not determine comment start position.");
            }
        }
        else {
            /* If quiet, just check the return value of skip */
            imd_report_error_exit("EOF found before comment terminator (0x1A).");
        }
    }
    else if (!quiet_mode) {
        /* If skip succeeded and not quiet, print the terminator line */
        printf("\n---\n");
    }


    /* --- Analyze Tracks --- */
    uint8_t max_cyl = 0;
    uint8_t max_head = 0;
    uint8_t modes_used = 0; /* Bitmask: bit 0=500k, bit 1=300k, bit 2=250k */
    uint32_t max_track_bytes_estimate = 0;
    uint32_t track_count = 0;

    while (1) {
        /* Use imd_read_track_header to only read header info, not data */
        int load_status = imd_read_track_header(fimd, &track_info);
        if (load_status == 0) break; /* EOF */
        if (load_status < 0) {
            fprintf(stderr, "Error reading track header for track index %u.\n", track_count);
            goto cleanup;
        }
        track_count++;

        if (track_info.cyl > max_cyl) max_cyl = track_info.cyl;
        if (track_info.head > max_head) max_head = track_info.head;

        /* Track modes used */
        switch (track_info.mode % 3) { /* Logic relies on mode numbering pattern */
        case 0: modes_used |= 1; break; /* 500 kbps */
        case 1: modes_used |= 2; break; /* 300 kbps */
        case 2: modes_used |= 4; break; /* 250 kbps */
        }

        /* Estimate track size */
        if (track_info.num_sectors > 0) {
            uint32_t track_bytes = (track_info.sector_size + SECTOR_OVERHEAD_GUESS) * track_info.num_sectors
                + CYLINDER_OVERHEAD_GUESS;
            if (track_bytes > max_track_bytes_estimate) {
                max_track_bytes_estimate = track_bytes;
            }
        }
        /* No need to free track data as imd_read_track_header doesn't load it */
    }

    /* --- Print Summary --- */
    if (!quiet_mode) {
        printf("\nAnalysis Summary:\n");
        printf("  Required Cylinders : %u (0-%u)\n", max_cyl + 1, max_cyl);
        printf("  Required Heads     : %u\n", max_head + 1);
        printf("  Data Rate(s) Used  :");
        if (modes_used & 4) printf(" 250kbps");
        if (modes_used & 2) printf(" 300kbps");
        if (modes_used & 1) printf(" 500kbps");
        if (modes_used == 0) printf(" (None found)");
        printf("\n");
        printf("  Est. Max Track Size: %u bytes\n", max_track_bytes_estimate);
    }

    /* --- Determine Recommendations --- */
    int num_modes = (modes_used & 1) + ((modes_used >> 1) & 1) + ((modes_used >> 2) & 1);
    if (num_modes > 1) {
        imd_report_error_exit("Mixed data rates found - cannot recommend single drive type.");
    }
    if (modes_used == 0 && track_count > 0) {
        imd_report_error_exit("Image contains tracks but no identifiable data rate.");
    }
    if (track_count == 0) {
        printf("\nImage appears to contain no tracks.\n");
        result = EXIT_SUCCESS;
        goto cleanup;
    }


    printf("\nPossible Drive Types / IMD Options:\n");

    uint32_t drive_flags = 0;
    uint8_t notes_printed[3] = { 0 }; /* 40t, 77t, 360rpm */
    int note_idx = 0;

    /* Determine base options */
    if (max_cyl < 40) drive_flags |= OPTION_DSTEP; /* Needs double step if less than 40 */
    else if (max_cyl == 39) drive_flags |= NOTE_40TRACK; /* Specific note for exactly 40 tracks */

    if (max_cyl == 76) drive_flags |= NOTE_77TRACK; /* Specific note for 77 tracks (8") */


    switch (modes_used) {
    case 1: /* 500 kbps only */
        if (max_track_bytes_estimate < (BYTES_PER_SEC_500K / 6)) { /* Heuristic from original */
            drive_flags |= NOTE_360RPM;
        }
        print_drive_recommendation(DRIVE_TYPE_D35_HD | drive_flags, notes_printed, &note_idx);
        print_drive_recommendation(DRIVE_TYPE_D525_HD | drive_flags, notes_printed, &note_idx);
        if (max_cyl <= 76) { /* Check if it fits 8" drive */
            print_drive_recommendation(DRIVE_TYPE_D8 | drive_flags, notes_printed, &note_idx);
        }
        break;

    case 2: /* 300 kbps only */
        /* 300kbps implies a 5.25" HD drive running at low speed (needs T32) OR a 3.5" drive */
        print_drive_recommendation(DRIVE_TYPE_D525_HD | OPTION_T32 | drive_flags, notes_printed, &note_idx);
        print_drive_recommendation(DRIVE_TYPE_D35_DD | drive_flags, notes_printed, &note_idx);
        print_drive_recommendation(DRIVE_TYPE_D35_HD | drive_flags, notes_printed, &note_idx);
        /* Original IMDA also listed D525_DD_80 + T32, seems less common */
        print_drive_recommendation(DRIVE_TYPE_D525_DD_80 | OPTION_T32 | drive_flags, notes_printed, &note_idx);
        /* Original IMDA also listed D525_DD_40 + T32, only if 40 tracks */
        if (drive_flags & NOTE_40TRACK) {
            print_drive_recommendation(DRIVE_TYPE_D525_DD_40 | OPTION_T32 | drive_flags, notes_printed, &note_idx);
        }

        break;

    case 4: /* 250 kbps only */
        /* 250kbps implies standard DD drives (3.5" or 5.25") OR 5.25" HD needing T23 */
        if (drive_flags & NOTE_40TRACK) { /* Only show 40 track drive if image fits */
            print_drive_recommendation(DRIVE_TYPE_D525_DD_40 | drive_flags, notes_printed, &note_idx);
        }
        print_drive_recommendation(DRIVE_TYPE_D525_DD_80 | drive_flags, notes_printed, &note_idx);
        print_drive_recommendation(DRIVE_TYPE_D525_HD | OPTION_T23 | drive_flags, notes_printed, &note_idx);
        print_drive_recommendation(DRIVE_TYPE_D35_DD | drive_flags, notes_printed, &note_idx);
        print_drive_recommendation(DRIVE_TYPE_D35_HD | drive_flags, notes_printed, &note_idx);
        break;
    }

    /* Print collected notes */
    if (note_idx > 0) {
        printf("\nNotes:\n");
        if (notes_printed[0]) printf(" *%d: 40 track image will use only first half of 80 track drive.\n", notes_printed[0]);
        if (notes_printed[1]) printf(" *%d: 77 track image likely requires an 8\" drive.\n", notes_printed[1]);
        if (notes_printed[2]) printf(" *%d: Track size suggests 360 RPM drive; writing on 300 RPM may work but leave extra gap.\n", notes_printed[2]);
    }


    result = EXIT_SUCCESS; /* Success! */

cleanup:
    if (fimd) fclose(fimd);
    return (result == EXIT_SUCCESS ? 0 : 1);
}
