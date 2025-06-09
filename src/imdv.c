/*
 * ImageDisk Viewer (Cross-Platform.)
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
#include <curses.h>  /* Requires curses library */
#include <locale.h>  /* For wide character support in ncurses */

#include "libimdf.h" /* Use our In-Memory IMD File library */

 /* Define version strings - replace with actual build system values if available */
#ifndef CMAKE_VERSION_STR
#define CMAKE_VERSION_STR "0.1.0" /* Placeholder version */
#endif
#ifndef GIT_VERSION_STR
#define GIT_VERSION_STR "dev" /* Placeholder git revision */
#endif

/* --- Constants --- */
#define MAX_FILENAME 260
#define DATA_LINES 16 /* Lines to display in hex view */
#define BYTES_PER_LINE 16
#define MAX_SEARCH_TERM 100

/* UI Window IDs (conceptual) */
#define WIN_INFO 0
#define WIN_DATA 1
#define WIN_STATUS 2

/* Color Pair IDs */
#define CP_NORMAL    1
#define CP_INFO      2
#define CP_INFO_HL   3
#define CP_DATA_ADDR 4
#define CP_DATA_HEX  5
#define CP_DATA_ASC  6
#define CP_DATA_HL   7
#define CP_STATUS    8
#define CP_ERROR     9
#define CP_EDIT_HEX  10
#define CP_EDIT_ASC  11
#define CP_INFO_SECTOR_NORMAL    12
#define CP_INFO_SECTOR_HIGHLIGHT 13
#define CP_SEARCH_BOX 14
#define CP_SEARCH_HIGHLIGHT 15


/* Character Sets */
#define CHARSET_ASCII  0
#define CHARSET_EBCDIC 1

/* Edit Modes */
#define EDIT_MODE_HEX   0
#define EDIT_MODE_ASCII 1

/* Search Types */
#define SEARCH_TYPE_NONE 0
#define SEARCH_TYPE_TEXT 1
#define SEARCH_TYPE_HEX  2

/* ESCape key */
#define ESC_KEY         (27)

/* Macro to check for Ctrl+Home equivalent keys */
#ifdef PDCURSES
#define IS_KEY_CTRL_HOME(key_code) ((key_code) == KEY_SHOME || (key_code) == CTL_HOME)
#else
#define IS_KEY_CTRL_HOME(key_code) ((key_code) == KEY_SHOME)
#endif

/* Macro to check for Ctrl+End equivalent keys */
#ifdef PDCURSES
#define IS_KEY_CTRL_END(key_code) ((key_code) == KEY_SEND || (key_code) == CTL_END)
#else
#define IS_KEY_CTRL_END(key_code) ((key_code) == KEY_SEND)
#endif

/* --- Global State --- */
WINDOW* win_info = NULL;
WINDOW* win_data = NULL;
WINDOW* win_status = NULL;

ImdTrackInfo current_track_display = { 0 };
uint8_t      current_sector_buffer[LIBIMD_MAX_SECTOR_SIZE];

size_t total_tracks_in_image = 0;
size_t current_track_index_in_image = 0;
uint32_t current_sector_logical_idx = 0;
uint32_t current_sector_logical_id = 0;
uint32_t current_sector_physical_idx = 0;

long current_data_offset_in_sector = 0;
int current_edit_mode = EDIT_MODE_HEX;
int current_charset = CHARSET_ASCII;
int ignore_interleave = 0;
int write_enabled = 0;
uint8_t xor_mask = 0;
char status_message[200] = "";
char current_filename_base[MAX_FILENAME] = "";

char last_search_term_text[MAX_SEARCH_TERM] = "";
uint8_t last_search_term_hex[MAX_SEARCH_TERM / 2];
int last_search_term_hex_len = 0;
int last_search_type = SEARCH_TYPE_NONE;

size_t g_found_on_track_idx = (size_t)-1;
uint32_t g_found_on_sector_log_idx = (uint32_t)-1;
long g_found_offset_in_sector = -1;
int g_found_len = 0;


ImdImageFile* g_imdf_handle = NULL;

const unsigned char ebcdic_to_ascii[256] = {
    0x00,0x01,0x02,0x03,0x9C,0x09,0x86,0x7F,0x97,0x8D,0x8E,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x9D,0x0A,0x08,0x87,0x18,0x19,0x92,0x8F,0x1C,0x1D,0x1E,0x1F,
    0x80,0x81,0x82,0x83,0x84,0x85,0x17,0x1B,0x88,0x89,0x8A,0x8B,0x8C,0x05,0x06,0x07,
    0x90,0x91,0x16,0x93,0x94,0x95,0x96,0x04,0x98,0x99,0x9A,0x9B,0x14,0x15,0x9E,0x1A,
    0x20,0xA0,0xE2,0xE4,0xE0,0xE1,0xE3,0xE5,0xE7,0xF1,0xA2,0x2E,0x3C,0x28,0x2B,0x7C,
    0x26,0xE9,0xEA,0xEB,0xE8,0xED,0xEE,0xEF,0xEC,0xDF,0x21,0x24,0x2A,0x29,0x3B,0x5E,
    0x2D,0x2F,0xC2,0xC4,0xC0,0xC1,0xC3,0xC5,0xC7,0xD1,0xA6,0x2C,0x25,0x5F,0x3E,0x3F,
    0xF8,0xC9,0xCA,0xCB,0xC8,0xCD,0xCE,0xCF,0xCC,0x60,0x3A,0x23,0x40,0x27,0x3D,0x22,
    0xD8,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0xAB,0xBB,0xF0,0xFD,0xFE,0xB1,
    0xB0,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0xAA,0xBA,0xE6,0xB8,0xC6,0xA4,
    0xB5,0x7E,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0xA1,0xBF,0xD0,0x5B,0xDE,0xAE,
    0xAC,0xA3,0xA5,0xB7,0xA9,0xA7,0xB6,0xBC,0xBD,0xBE,0xDD,0xA8,0xAF,0x5D,0xB4,0xD7,
    0x7B,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0xAD,0xF4,0xF6,0xF2,0xF3,0xF5,
    0x7D,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0xB9,0xFB,0xFC,0xF9,0xFA,0xFF,
    0x5C,0xF7,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0xB2,0xD4,0xD6,0xD2,0xD3,0xD5,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0xB3,0xDB,0xDC,0xD9,0xDA,0x9F
};

/* --- Forward Declarations --- */
void draw_info_window(void);
void draw_data_window(void);
void update_status(const char* msg);
void display_error(const char* msg);
void display_help_window(void);
void edit_sector(void);
int ctoh(int c);
int compare_sector_map_entries(const void* a, const void* b);
const char* get_basename(const char* path);
int load_track_for_display(size_t track_idx);
int load_sector_for_display(void); /* Returns 0 on success, -1 on error (not IMDF_ERR_UNAVAILABLE) */
int load_specific_sector_data(size_t track_idx, uint32_t sector_log_id, uint8_t* buffer, uint32_t buffer_size, ImdTrackInfo* out_track_info);
uint32_t get_physical_idx_for_display(uint32_t logical_idx_in_track);
const char* get_mode_string(uint8_t mode_code);
void build_status_message(void);
int get_search_input(const char* prompt, char* buffer, int buffer_size, int is_hex);
void search_text_from_current(const char* term, int start_from_next_byte);
void search_hex_from_current(const uint8_t* hex_term, int term_len, int start_from_next_byte);
void repeat_last_search(void);
void clear_search_highlight(void);
void adjust_view_for_match(long match_offset_in_sector, int term_len);


const char* get_basename(const char* path) {
    if (path == NULL) return NULL;
    const char* basename = path;
    const char* p = path;
    while (*p) {
        if (*p == '/' || *p == '\\') basename = p + 1;
        p++;
    }
    return basename;
}

void init_ui(void) {
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100);

    if (has_colors()) {
        start_color();
        init_pair(CP_NORMAL, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_INFO, COLOR_YELLOW, COLOR_BLUE);
        init_pair(CP_INFO_HL, COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_DATA_ADDR, COLOR_CYAN, COLOR_BLACK);
        init_pair(CP_DATA_HEX, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_DATA_ASC, COLOR_GREEN, COLOR_BLACK);
        init_pair(CP_DATA_HL, COLOR_BLACK, COLOR_GREEN);
        init_pair(CP_STATUS, COLOR_BLACK, COLOR_CYAN);
        init_pair(CP_ERROR, COLOR_WHITE, COLOR_RED);
        init_pair(CP_EDIT_HEX, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_EDIT_ASC, COLOR_BLACK, COLOR_GREEN);
        init_pair(CP_INFO_SECTOR_NORMAL, COLOR_WHITE, COLOR_BLUE);
        init_pair(CP_INFO_SECTOR_HIGHLIGHT, COLOR_BLACK, COLOR_YELLOW);
        init_pair(CP_SEARCH_BOX, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_SEARCH_HIGHLIGHT, COLOR_BLACK, COLOR_MAGENTA);

        bkgd(COLOR_PAIR(CP_NORMAL));
    }
    else {
        init_pair(CP_NORMAL, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_INFO, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_INFO_HL, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_DATA_ADDR, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_DATA_HEX, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_DATA_ASC, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_DATA_HL, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_STATUS, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_ERROR, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_EDIT_HEX, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_EDIT_ASC, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_INFO_SECTOR_NORMAL, COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_INFO_SECTOR_HIGHLIGHT, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_SEARCH_BOX, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_SEARCH_HIGHLIGHT, COLOR_BLACK, COLOR_WHITE);

        bkgd(COLOR_PAIR(CP_NORMAL));
    }

    erase();
    refresh();

    int screen_h, screen_w;
    getmaxyx(stdscr, screen_h, screen_w);

    win_info = newwin(6, screen_w, 0, 0);
    win_data = newwin(screen_h - (6 + 1), screen_w, 6, 0);
    win_status = newwin(1, screen_w, screen_h - 1, 0);

    if (!win_info || !win_data || !win_status) {
        endwin();
        fprintf(stderr, "Error creating ncurses windows.\n");
        exit(EXIT_FAILURE);
    }

    keypad(win_data, TRUE);
    scrollok(win_data, TRUE);

    if (win_info)   wbkgd(win_info, COLOR_PAIR(CP_INFO));
    if (win_data)   wbkgd(win_data, COLOR_PAIR(CP_NORMAL));
    if (win_status) wbkgd(win_status, COLOR_PAIR(CP_STATUS));
}


void cleanup_ui(void) {
    if (win_status) delwin(win_status);
    if (win_data) delwin(win_data);
    if (win_info) delwin(win_info);
    endwin();
}

void build_status_message(void) {
    snprintf(status_message, sizeof(status_message), "IMDV %s | F1=Help Arrows=Nav Enter=Edit F2=Charset F3/F4/F5=Search %sF10=Quit",
        CMAKE_VERSION_STR,
        ignore_interleave ? "I " : "  ");
}

void update_status(const char* msg) {
    if (!win_status) return;
    int max_w = getmaxx(win_status);
    werase(win_status);
    wbkgd(win_status, COLOR_PAIR(CP_STATUS));
    mvwprintw(win_status, 0, 0, "%.*s", max_w, msg);
    int len = (int)strlen(msg);
    if (len < max_w) {
        wmove(win_status, 0, len);
        for (int i = len; i < max_w; ++i) waddch(win_status, ' ');
    }
    wnoutrefresh(win_status);
}

void display_error(const char* msg) {
    if (!win_status) return;
    int max_w = getmaxx(win_status);
    werase(win_status);
    wbkgd(win_status, COLOR_PAIR(CP_ERROR));
    mvwprintw(win_status, 0, 0, "ERROR: %.*s", max_w - 7, msg);
    wrefresh(win_status);
    beep();
    timeout(-1);
    wgetch(stdscr);
    timeout(100);
    build_status_message();
    update_status(status_message);
    doupdate();
}

const char* help_text[] = {
    "IMDV Help",
    "",
    "Navigation:",
    "  Up Arrow         : Previous track",
    "  Down Arrow       : Next track",
    "  Left Arrow       : Previous sector (wraps to prev track)",
    "  Right Arrow      : Next sector (wraps to next track)",
    "  Page Up          : Scroll sector data up by one page",
    "  Page Down        : Scroll sector data down by one page",
    "  Home             : Go to first sector of current track, top of data",
    "  End              : Go to last sector of current track, top of data",
    "  Ctrl + Home      : Go to first track, first sector, top of data",
    "  Ctrl + End       : Go to last track, first sector, top of data",
    "",
    "Display & Editing:",
    "  F1               : Show this help screen",
    "  F2               : Toggle Charset (ASCII / EBCDIC)",
    "  F3               : Search for text string (pre-fills last text search)",
    "  F4               : Search for hex bytes (pre-fills last hex search)",
    "  F5               : Repeat last search from current position onward",
    "  I                : Toggle interleave ignore for sector navigation",
    "  Enter            : Edit current sector (if -W enabled)",
    "                     In Edit Mode:",
    "                       Arrows   : Move cursor",
    "                       PageUp/Dn: Scroll data",
    "                       Home/End : Move to start/end of line",
    "                       F3       : Toggle HEX/ASCII edit mode",
    "                       Type     : Modify data at cursor",
    "                       Enter    : Save changes (prompts for confirmation)",
    "                       ESC/F10  : Exit edit mode (discard changes if any)",
    "  Q / F10          : Quit IMDV",
    "",
    "Command-line Options:",
    "  -I      : Ignore interleave (show physical sector order in navigation)",
    "  -W      : Enable writing (editing) - if image not Read-Only",
    "  -E      : Start in EBCDIC display mode",
    "  -X=xx   : Apply hex XOR mask 'xx' to data view (e.g., -X=FF)",
    NULL
};


void display_help_window(void) {
    if (!win_data) return;

    char original_status_buffer_temp[sizeof(status_message)];
    /* Store the current status message so it can be restored */
    strncpy(original_status_buffer_temp, status_message, sizeof(original_status_buffer_temp) - 1);
    original_status_buffer_temp[sizeof(original_status_buffer_temp) - 1] = '\0';

    int total_help_lines = 0;
    while (help_text[total_help_lines] != NULL) {
        total_help_lines++;
    }

    int help_scroll_offset = 0; /* Current top line of help_text to display */
    int ch;

    timeout(-1); /* Set blocking input for the help window */

    while (1) {
        werase(win_data); /* Clear the window for redrawing */
        int max_y, max_x;
        getmaxyx(win_data, max_y, max_x); /* Get current dimensions of win_data */

        /* Calculate the maximum possible scroll offset */
        /* This ensures the last line of text can reach the bottom of the window, but not scroll further if not needed */
        int max_scroll_offset = 0;
        if (total_help_lines > max_y) {
            max_scroll_offset = total_help_lines - max_y;
        }
        /* Ensure help_scroll_offset itself doesn't exceed this calculated maximum if window resizes etc. */
        if (help_scroll_offset > max_scroll_offset) {
            help_scroll_offset = max_scroll_offset;
        }
        if (help_scroll_offset < 0) { /* Should not happen with current logic but good failsafe */
            help_scroll_offset = 0;
        }


        /* Draw the visible portion of help text */
        for (int i = 0; i < max_y; ++i) {
            int current_help_line_idx = help_scroll_offset + i;
            if (current_help_line_idx < total_help_lines) { /* Check if index is within bounds */
                mvwprintw(win_data, i, 1, "%.*s", max_x - 2, help_text[current_help_line_idx]);
            }
            else {
                break; /* No more lines to draw */
            }
        }
        wrefresh(win_data); /* Refresh the data window to show help text */

        /* Update status bar with help navigation instructions */
        /* Keeping it concise, users often try arrows instinctively */
        update_status("Arrows/PgUp/PgDn=Scroll | SPACE/Enter/ESC/F10=Exit Help");
        doupdate(); /* Update the physical screen */

        ch = wgetch(win_data); /* Wait for user input */

        /* Check for exit keys first */
        if (ch == '\n' || ch == ' ' || ch == 'q' || ch == 'Q' || ch == KEY_ENTER || ch == ESC_KEY || ch == KEY_F(10)) {
            break; /* Exit the help display loop */
        }

        /* Handle scrolling keys */
        switch (ch) {
        case KEY_UP:
            if (help_scroll_offset > 0) {
                help_scroll_offset--;
            }
            break;
        case KEY_DOWN:
            /* max_scroll_offset is already calculated considering window height (max_y) */
            if (help_scroll_offset < max_scroll_offset) {
                help_scroll_offset++;
            }
            break;
        case KEY_PPAGE:
            help_scroll_offset -= max_y; /* Scroll up by one page (window height) */
            if (help_scroll_offset < 0) {
                help_scroll_offset = 0; /* Don't scroll beyond the beginning */
            }
            break;
        case KEY_NPAGE:
            /* max_scroll_offset is the furthest we can set the top line to */
            if (help_scroll_offset < max_scroll_offset) {
                help_scroll_offset += max_y; /* Scroll down by one page */
                if (help_scroll_offset > max_scroll_offset) {
                    help_scroll_offset = max_scroll_offset; /* Adjust to not scroll past the end */
                }
            }
            break;
        case KEY_HOME:
            help_scroll_offset = 0;
            break;
        case KEY_END:
            help_scroll_offset = max_scroll_offset;
            break;
        default:
            /* Optional: beep for unhandled keys while in help */
            /* beep(); */
            break;
        }
    }

    timeout(100); /* Restore the default non-blocking timeout for the main application */

    /* Restore original status and redraw main application windows */
    update_status(original_status_buffer_temp);
    /* Force redraw of info and data windows as they were covered */
    draw_info_window();
    draw_data_window();
    doupdate(); /* Update the physical screen with main UI */
}

void copy_track_metadata_for_display(const ImdTrackInfo* source_track) {
    if (!source_track) {
        memset(&current_track_display, 0, sizeof(ImdTrackInfo));
        current_track_display.loaded = 0;
        return;
    }
    current_track_display.mode = source_track->mode;
    current_track_display.cyl = source_track->cyl;
    current_track_display.head = source_track->head;
    current_track_display.hflag = source_track->hflag;
    current_track_display.num_sectors = source_track->num_sectors;
    current_track_display.sector_size_code = source_track->sector_size_code;
    current_track_display.sector_size = source_track->sector_size;
    memcpy(current_track_display.smap, source_track->smap, sizeof(current_track_display.smap));
    memcpy(current_track_display.cmap, source_track->cmap, sizeof(current_track_display.cmap));
    memcpy(current_track_display.hmap, source_track->hmap, sizeof(current_track_display.hmap));
    memcpy(current_track_display.sflag, source_track->sflag, sizeof(current_track_display.sflag));
    current_track_display.data = NULL;
    current_track_display.data_size = 0;
    current_track_display.loaded = source_track->loaded;
}

int load_track_for_display(size_t track_idx) {
    const ImdTrackInfo* imdf_track = imdf_get_track_info(g_imdf_handle, track_idx);
    if (!imdf_track) {
        display_error("Failed to get track info from libimdf.");
        current_track_display.loaded = 0;
        return -1;
    }

    if (g_found_len > 0 && g_found_on_track_idx != track_idx) {
        clear_search_highlight();
    }

    copy_track_metadata_for_display(imdf_track);
    current_track_index_in_image = track_idx;

    if (!(g_found_len > 0 && g_found_on_track_idx == track_idx)) {
        current_sector_logical_idx = 0;
        /* current_data_offset_in_sector is preserved if navigating to found track,
           otherwise should be reset if not explicitly set by caller. For safety, reset. */
        if (!(g_found_len > 0 && g_found_on_track_idx == track_idx && g_found_on_sector_log_idx == 0)) {
            current_data_offset_in_sector = 0;
        }

    }

    if (current_track_display.num_sectors > 0) {
        if (current_sector_logical_idx >= current_track_display.num_sectors) {
            current_sector_logical_idx = 0;
            current_data_offset_in_sector = 0;
            if (g_found_len > 0 && g_found_on_track_idx == track_idx) {
                clear_search_highlight();
            }
        }
        return load_sector_for_display();
    }
    else {
        current_sector_logical_id = 0;
        current_sector_physical_idx = 0;
        memset(current_sector_buffer, 0, sizeof(current_sector_buffer));
        if (g_found_len > 0 && g_found_on_track_idx == track_idx) {
            clear_search_highlight();
        }
    }
    return 0;
}

int load_sector_for_display(void) {
    if (!current_track_display.loaded || current_track_display.num_sectors == 0) {
        current_sector_logical_id = 0;
        current_sector_physical_idx = 0;
        memset(current_sector_buffer, 0, sizeof(current_sector_buffer));
        if (g_found_len > 0 && g_found_on_track_idx == current_track_index_in_image && g_found_on_sector_log_idx == current_sector_logical_idx) {
            clear_search_highlight();
        }
        return 0;
    }

    if (current_sector_logical_idx >= current_track_display.num_sectors) {
        current_sector_logical_idx = current_track_display.num_sectors > 0 ? current_track_display.num_sectors - 1 : 0;
    }

    if (!(g_found_len > 0 &&
        g_found_on_track_idx == current_track_index_in_image &&
        g_found_on_sector_log_idx == current_sector_logical_idx)) {
        clear_search_highlight();
    }

    current_sector_physical_idx = get_physical_idx_for_display(current_sector_logical_idx);

    if (current_sector_physical_idx >= current_track_display.num_sectors) {
        display_error("Internal error: physical sector index out of bounds during load_sector.");
        memset(current_sector_buffer, 0, sizeof(current_sector_buffer));
        current_sector_logical_id = 0;
        clear_search_highlight();
        return -1;
    }
    current_sector_logical_id = current_track_display.smap[current_sector_physical_idx];

    int res = imdf_read_sector(g_imdf_handle, current_track_display.cyl, current_track_display.head,
        current_sector_logical_id, current_sector_buffer, current_track_display.sector_size);

    if (res != IMDF_ERR_OK) {
        if (res == IMDF_ERR_UNAVAILABLE) {
            memset(current_sector_buffer, LIBIMD_FILL_BYTE_DEFAULT, current_track_display.sector_size);
        }
        else {
            char err_msg[100];
            snprintf(err_msg, sizeof(err_msg), "Failed to read sector C%u H%u S%u (err %d)",
                current_track_display.cyl, current_track_display.head, current_sector_logical_id, res);
            display_error(err_msg);
            memset(current_sector_buffer, 0, sizeof(current_sector_buffer));
        }
        if (g_found_len > 0 && g_found_on_track_idx == current_track_index_in_image && g_found_on_sector_log_idx == current_sector_logical_idx) {
            clear_search_highlight();
        }
        if (res != IMDF_ERR_UNAVAILABLE) return -1;
    }
    return 0;
}

/* Helper to load a specific sector's data without altering main display state */
/* Returns actual sector size read, or 0 on error/unavailable */
int load_specific_sector_data(size_t track_idx, uint32_t target_sector_logical_idx, uint8_t* buffer, uint32_t buffer_size, ImdTrackInfo* out_track_info) {
    const ImdTrackInfo* track_info_ptr = imdf_get_track_info(g_imdf_handle, track_idx);
    if (!track_info_ptr || !track_info_ptr->loaded || target_sector_logical_idx >= track_info_ptr->num_sectors) {
        return 0; /* Cannot load */
    }
    if (out_track_info) { /* Caller wants a copy of this track's info */
        memcpy(out_track_info, track_info_ptr, sizeof(ImdTrackInfo));
    }

    uint32_t temp_physical_idx = 0;
    /* Get physical index for the target_sector_logical_idx on track_idx */
    /* This requires a temporary way to use get_physical_idx_for_display or similar logic */
    /* For now, assume ignore_interleave=0 for this helper, or adapt get_physical_idx_for_display to take track_info */
    typedef struct { uint32_t physical_idx; uint8_t logical_id; } SectorMapEntry;
    SectorMapEntry sorted_map[LIBIMD_MAX_SECTORS_PER_TRACK];
    if (track_info_ptr->num_sectors > LIBIMD_MAX_SECTORS_PER_TRACK) return 0;
    for (uint32_t p = 0; p < track_info_ptr->num_sectors; ++p) {
        sorted_map[p].physical_idx = p;
        sorted_map[p].logical_id = track_info_ptr->smap[p];
    }
    qsort(sorted_map, track_info_ptr->num_sectors, sizeof(SectorMapEntry), compare_sector_map_entries);
    temp_physical_idx = sorted_map[target_sector_logical_idx].physical_idx;


    uint32_t target_sector_id_on_disk = track_info_ptr->smap[temp_physical_idx];
    uint32_t actual_sector_size = track_info_ptr->sector_size;
    if (actual_sector_size == 0) return 0;

    uint32_t read_size = (actual_sector_size < buffer_size) ? actual_sector_size : buffer_size;

    int res = imdf_read_sector(g_imdf_handle, track_info_ptr->cyl, track_info_ptr->head,
        target_sector_id_on_disk, buffer, read_size);

    if (res == IMDF_ERR_UNAVAILABLE) {
        memset(buffer, LIBIMD_FILL_BYTE_DEFAULT, read_size);
        return read_size; /* Return size even if filled */
    }
    else if (res != IMDF_ERR_OK) {
        return 0; /* Error */
    }
    return read_size;
}


const char* get_mode_string(uint8_t mode_code) {
    switch (mode_code) {
    case IMD_MODE_FM_500:  return "500KHz  FM";
    case IMD_MODE_FM_300:  return "300KHz  FM";
    case IMD_MODE_FM_250:  return "250KHz  FM";
    case IMD_MODE_MFM_500: return "500KHz MFM";
    case IMD_MODE_MFM_300: return "300KHz MFM";
    case IMD_MODE_MFM_250: return "250KHz MFM";
    default:               return "Unknown Mode";
    }
}

void draw_info_window(void) {
    if (!win_info) return;
    werase(win_info);
    box(win_info, 0, 0);
    int max_w = getmaxx(win_info);
    int current_x_pos_line1 = 2; /* Starting X position for C/H/S info on line 1 */
    int filename_len_on_screen = 0;
    int filename_start_x = 0;

    /* Display basename of .imd filename in the right corner of line 1 */
    if (strlen(current_filename_base) > 0) {
        filename_len_on_screen = (int)strlen(current_filename_base);
        filename_start_x = max_w - filename_len_on_screen - 2; /* 2 for padding from right border */

        /* Ensure filename_start_x is not too far left, clipping if necessary */
        if (filename_start_x < 2) {
            filename_start_x = 2;
        }
        /* If filename is very long, it might overwrite C/H/S. Truncate filename display if it would overlap too much. */
        /* A simple check: if filename would start left of where C/H/S info starts + some buffer, truncate */
        if (filename_start_x < current_x_pos_line1 + 10 && filename_len_on_screen > 15) { /* Arbitrary small buffer and min length to care about */
            /* This is a basic truncation. A more robust solution might scroll or abbreviate filename */
            filename_len_on_screen = max_w - (current_x_pos_line1 + 10) - 2;
            if (filename_len_on_screen < 0) filename_len_on_screen = 0;
            filename_start_x = max_w - filename_len_on_screen - 2;
        }


        wattron(win_info, COLOR_PAIR(CP_INFO_HL)); /* Use a distinct color for the filename */
        mvwprintw(win_info, 1, filename_start_x, "%.*s", filename_len_on_screen, current_filename_base);
        wattroff(win_info, COLOR_PAIR(CP_INFO_HL));
    }

    char line1_buf[120];
    uint8_t physical_cyl_val = 0, physical_head_val = 0;
    uint32_t physical_sec_idx_val = 0;
    uint8_t logical_cyl_val = 0, logical_head_val = 0, logical_sec_id_val = 0;

    if (current_track_display.loaded) {
        physical_cyl_val = current_track_display.cyl;
        physical_head_val = current_track_display.head;

        if (current_track_display.num_sectors > 0 && current_sector_physical_idx < current_track_display.num_sectors) {
            physical_sec_idx_val = current_sector_physical_idx;
            logical_sec_id_val = current_sector_logical_id;

            if (current_track_display.hflag & IMD_HFLAG_CMAP_PRES) {
                logical_cyl_val = current_track_display.cmap[physical_sec_idx_val];
            }
            else {
                logical_cyl_val = physical_cyl_val;
            }
            if (current_track_display.hflag & IMD_HFLAG_HMAP_PRES) {
                logical_head_val = current_track_display.hmap[physical_sec_idx_val];
            }
            else {
                logical_head_val = physical_head_val;
            }
        }
        snprintf(line1_buf, sizeof(line1_buf), "Physical C/H/S: %3u/%1u/%-2u   Logical C/H/S: %3u/%1u/%-2u",
            physical_cyl_val,
            physical_head_val,
            current_track_display.num_sectors > 0 ? physical_sec_idx_val + 1 : 0,
            logical_cyl_val,
            logical_head_val,
            current_track_display.num_sectors > 0 ? logical_sec_id_val : 0);
    }
    else {
        snprintf(line1_buf, sizeof(line1_buf), "Track info not loaded.");
    }

    /* Calculate max width for C/H/S info to avoid overwriting filename */
    int chs_info_max_len = max_w - current_x_pos_line1 - 2; /* Default max width */
    if (filename_len_on_screen > 0) {
        /* If filename is displayed, C/H/S info must end before filename starts */
        chs_info_max_len = filename_start_x - current_x_pos_line1 - 1; /* -1 for a space separator */
    }
    if (chs_info_max_len < 0) chs_info_max_len = 0; /* Prevent negative length */

    wattron(win_info, COLOR_PAIR(CP_INFO));
    mvwprintw(win_info, 1, current_x_pos_line1, "%.*s", chs_info_max_len, line1_buf); /* Print C/H/S info */
    wattroff(win_info, COLOR_PAIR(CP_INFO));


    if (current_track_display.loaded && current_track_display.num_sectors > 0) {
        int current_x = 2;
        char track_prefix[20]; /* Buffer for "Track: ttt - " */

        /* Format track number, padded to 3 characters (e.g., "  0", " 12", "123") */
        snprintf(track_prefix, sizeof(track_prefix), "Track: %3zu - ", current_track_index_in_image);

        wattron(win_info, COLOR_PAIR(CP_INFO));
        mvwprintw(win_info, 2, current_x, "%sSectors: ", track_prefix);
        wattroff(win_info, COLOR_PAIR(CP_INFO));
        current_x += (int)strlen(track_prefix) + (int)strlen("Sectors: ");

        for (uint32_t p_idx = 0; p_idx < current_track_display.num_sectors; ++p_idx) {
            uint8_t logical_id_at_physical_p = current_track_display.smap[p_idx];
            char sector_str[5];
            snprintf(sector_str, sizeof(sector_str), "%u ", logical_id_at_physical_p);

            if (current_x + (int)strlen(sector_str) >= max_w - 1) {
                wattron(win_info, COLOR_PAIR(CP_INFO) | A_REVERSE);
                mvwaddch(win_info, 2, max_w - 2, '>');
                wattroff(win_info, COLOR_PAIR(CP_INFO) | A_REVERSE);
                break;
            }

            if (logical_id_at_physical_p == current_sector_logical_id) {
                wattron(win_info, COLOR_PAIR(CP_INFO_SECTOR_HIGHLIGHT));
            }
            else {
                wattron(win_info, COLOR_PAIR(CP_INFO_SECTOR_NORMAL));
            }
            mvwprintw(win_info, 2, current_x, "%s", sector_str);
            if (logical_id_at_physical_p == current_sector_logical_id) {
                wattroff(win_info, COLOR_PAIR(CP_INFO_SECTOR_HIGHLIGHT));
            }
            else {
                wattroff(win_info, COLOR_PAIR(CP_INFO_SECTOR_NORMAL));
            }
            current_x += (int)strlen(sector_str);
        }
    }
    else {
        char track_prefix[20];
        snprintf(track_prefix, sizeof(track_prefix), "Track: %3zu - ", current_track_index_in_image);
        wattron(win_info, COLOR_PAIR(CP_INFO));
        mvwprintw(win_info, 2, 2, "%sSectors: N/A", track_prefix);
        wattroff(win_info, COLOR_PAIR(CP_INFO));
    }

    const char* mode_str = "N/A";
    uint32_t num_s = 0;
    uint32_t sect_sz = 0;
    const char* data_status_str = "N/A";
    char err_indicator[10] = "";
    char dam_indicator[10] = "";

    if (current_track_display.loaded) {
        mode_str = get_mode_string(current_track_display.mode);
        num_s = current_track_display.num_sectors;
        sect_sz = current_track_display.sector_size;

        if (num_s > 0 && current_sector_physical_idx < num_s) {
            uint8_t sflag_val = current_track_display.sflag[current_sector_physical_idx];
            if (!IMD_SDR_HAS_DATA(sflag_val)) {
                data_status_str = "Unavailable";
            }
            else if (IMD_SDR_IS_COMPRESSED(sflag_val)) {
                data_status_str = "Compressed";
            }
            else {
                data_status_str = "Normal Data";
            }
            if (IMD_SDR_HAS_ERR(sflag_val)) strcpy(err_indicator, " +ERR");
            if (IMD_SDR_HAS_DAM(sflag_val)) strcpy(dam_indicator, " +DAM");
        }
    }
    char line3_buf[120];
    snprintf(line3_buf, sizeof(line3_buf), "%s, %u sectors of %u bytes, %s%s%s",
        mode_str, num_s, sect_sz, data_status_str, dam_indicator, err_indicator);
    wattron(win_info, COLOR_PAIR(CP_INFO));
    mvwprintw(win_info, 3, 2, "%.*s", max_w - 3, line3_buf);
    wattroff(win_info, COLOR_PAIR(CP_INFO));

    wattron(win_info, COLOR_PAIR(CP_INFO));
    for (int col_idx = 1; col_idx < max_w - 1; ++col_idx) {
        mvwaddch(win_info, 4, col_idx, ' '); /* Clear line 4 */
    }
    wattroff(win_info, COLOR_PAIR(CP_INFO));


    if (write_enabled) {
        int wp_stat = 0;
        imdf_get_write_protect(g_imdf_handle, &wp_stat);
        const char* write_label_str = wp_stat ? " DISK RO " : " WRITE ENABLED ";
        int label_attr = wp_stat ? COLOR_PAIR(CP_INFO) : (COLOR_PAIR(CP_INFO_HL) | A_BOLD);

        wattron(win_info, label_attr);
        mvwprintw(win_info, 4, 2, "%s", write_label_str);
        wattroff(win_info, label_attr);
    }
    else {
        int wp_stat = 1;
        if (g_imdf_handle) {
            imdf_get_write_protect(g_imdf_handle, &wp_stat);
        }
        if (wp_stat) {
            wattron(win_info, COLOR_PAIR(CP_INFO));
            mvwprintw(win_info, 4, 2, " DISK RO ");
            wattroff(win_info, COLOR_PAIR(CP_INFO));
        }
    }

    if (xor_mask != 0) {
        char xor_str[20];
        snprintf(xor_str, sizeof(xor_str), "XOR: 0x%02X", xor_mask);
        int center_pos = (max_w - (int)strlen(xor_str)) / 2;
        if (center_pos < 2) center_pos = 2;
        /* Ensure it doesn't overwrite WRITE ENABLED/DISK RO if they are long */
        int write_label_len = 0;
        if (write_enabled) write_label_len = (int)strlen((imdf_get_write_protect(g_imdf_handle, &write_label_len), write_label_len ? " DISK RO " : " WRITE ENABLED "));
        else if (g_imdf_handle && (imdf_get_write_protect(g_imdf_handle, &write_label_len), write_label_len)) write_label_len = (int)strlen(" DISK RO ");

        if (center_pos <= 2 + write_label_len + 1) center_pos = 2 + write_label_len + 2; /* Move after write status + space*/


        if (center_pos + (int)strlen(xor_str) < max_w - 1) {
            wattron(win_info, COLOR_PAIR(CP_INFO));
            mvwprintw(win_info, 4, center_pos, "%s", xor_str);
            wattroff(win_info, COLOR_PAIR(CP_INFO));
        }
    }

    const char* charset_str = (current_charset == CHARSET_ASCII) ? "ASCII" : "EBCDIC";
    int charset_len = (int)strlen(charset_str);
    int charset_pos = max_w - charset_len - 2; /* Position from the right */
    if (charset_pos >= 1 && (charset_pos + charset_len < max_w - 1)) {
        /* Check if XOR string is displayed and would overlap */
        if (xor_mask != 0) {
            char xor_str_check[20];
            snprintf(xor_str_check, sizeof(xor_str_check), "XOR: 0x%02X", xor_mask);
            int xor_center_pos = (max_w - (int)strlen(xor_str_check)) / 2;
            if (xor_center_pos < 2) xor_center_pos = 2;
            int write_label_len_check = 0;
            if (write_enabled) imdf_get_write_protect(g_imdf_handle, &write_label_len_check);
            else if (g_imdf_handle) imdf_get_write_protect(g_imdf_handle, &write_label_len_check);
            if (write_label_len_check) write_label_len_check = (int)strlen((write_enabled && !write_label_len_check) ? " WRITE ENABLED " : " DISK RO ");


            if (xor_center_pos <= 2 + write_label_len_check + 1) xor_center_pos = 2 + write_label_len_check + 2;


            if (charset_pos < xor_center_pos + (int)strlen(xor_str_check) + 1) {
                /* Overlap, move charset further left if possible, or don't print */
                charset_pos = xor_center_pos - charset_len - 2; /* Place before XOR */
            }
        }
        if (charset_pos >= 1) { /* Recheck after potential move */
            wattron(win_info, COLOR_PAIR(CP_INFO));
            mvwprintw(win_info, 4, charset_pos, "%s", charset_str);
            wattroff(win_info, COLOR_PAIR(CP_INFO));
        }
    }
    wnoutrefresh(win_info);
}

void draw_data_window(void) {
    if (!win_data) return;
    werase(win_data);

    if (!current_track_display.loaded || current_track_display.num_sectors == 0 || current_track_display.sector_size == 0) {
        wnoutrefresh(win_data);
        return;
    }

    int max_y, max_x; getmaxyx(win_data, max_y, max_x);
    int lines_to_draw = (max_y > 0) ? max_y : DATA_LINES;
    int current_highlight_active = (g_found_len > 0 &&
        g_found_on_track_idx == current_track_index_in_image &&
        g_found_on_sector_log_idx == current_sector_logical_idx);

    for (int line = 0; line < lines_to_draw; ++line) {
        uint32_t line_offset_in_sector = (uint32_t)current_data_offset_in_sector + (line * BYTES_PER_LINE);
        if (line_offset_in_sector >= current_track_display.sector_size) break;

        wattron(win_data, COLOR_PAIR(CP_DATA_ADDR));
        mvwprintw(win_data, line, 0, "%04X:", line_offset_in_sector);
        wattroff(win_data, COLOR_PAIR(CP_DATA_ADDR));

        wmove(win_data, line, 6);
        for (int i = 0; i < BYTES_PER_LINE; ++i) {
            uint32_t byte_offset_in_sector = line_offset_in_sector + i;
            int is_highlighted_byte = 0;

            if (current_highlight_active &&
                byte_offset_in_sector >= (uint32_t)g_found_offset_in_sector &&
                byte_offset_in_sector < (uint32_t)(g_found_offset_in_sector + g_found_len)) {
                is_highlighted_byte = 1;
            }

            if (byte_offset_in_sector >= current_track_display.sector_size) {
                wprintw(win_data, "   ");
            }
            else {
                uint8_t val = current_sector_buffer[byte_offset_in_sector] ^ xor_mask;
                wattron(win_data, COLOR_PAIR(is_highlighted_byte ? CP_SEARCH_HIGHLIGHT : CP_DATA_HEX));
                wprintw(win_data, " %02X", val);
                wattroff(win_data, COLOR_PAIR(is_highlighted_byte ? CP_SEARCH_HIGHLIGHT : CP_DATA_HEX));
            }
            if ((i + 1) % 8 == 0 && i < 15) waddch(win_data, ' ');
        }

        int ascii_start_col = 6 + (BYTES_PER_LINE * 3) + (BYTES_PER_LINE / 8) + 1;
        if (ascii_start_col < max_x) {
            wmove(win_data, line, ascii_start_col);
            for (int i = 0; i < BYTES_PER_LINE; ++i) {
                uint32_t byte_offset_in_sector = line_offset_in_sector + i;
                int is_highlighted_byte = 0;

                if (current_highlight_active &&
                    byte_offset_in_sector >= (uint32_t)g_found_offset_in_sector &&
                    byte_offset_in_sector < (uint32_t)(g_found_offset_in_sector + g_found_len)) {
                    is_highlighted_byte = 1;
                }

                if (byte_offset_in_sector >= current_track_display.sector_size) {
                    waddch(win_data, ' ');
                }
                else {
                    uint8_t val = current_sector_buffer[byte_offset_in_sector] ^ xor_mask;
                    uint8_t display_char = (current_charset == CHARSET_EBCDIC) ? ebcdic_to_ascii[val] : val;
                    if (display_char == '\t') display_char = ' ';
                    else if (display_char == '\r') display_char = '<';
                    else if (display_char == '\n') display_char = '>';

                    wattron(win_data, COLOR_PAIR(is_highlighted_byte ? CP_SEARCH_HIGHLIGHT : CP_DATA_ASC));
                    waddch(win_data, (isprint(display_char) ? display_char : '.'));
                    wattroff(win_data, COLOR_PAIR(is_highlighted_byte ? CP_SEARCH_HIGHLIGHT : CP_DATA_ASC));
                }
            }
        }
    }
    wnoutrefresh(win_data);
}

uint32_t get_physical_idx_for_display(uint32_t logical_idx_in_track_smap_order) {
    if (!current_track_display.loaded || logical_idx_in_track_smap_order >= current_track_display.num_sectors) {
        if (current_track_display.num_sectors == 0 && logical_idx_in_track_smap_order == 0) return 0;
        return 0;
    }

    if (ignore_interleave) {
        return logical_idx_in_track_smap_order;
    }
    else {
        typedef struct { uint32_t physical_idx; uint8_t logical_id; } SectorMapEntry;
        SectorMapEntry sorted_map[LIBIMD_MAX_SECTORS_PER_TRACK];

        if (current_track_display.num_sectors > LIBIMD_MAX_SECTORS_PER_TRACK) return 0;

        for (uint32_t p = 0; p < current_track_display.num_sectors; ++p) {
            sorted_map[p].physical_idx = p;
            sorted_map[p].logical_id = current_track_display.smap[p];
        }

        qsort(sorted_map, current_track_display.num_sectors, sizeof(SectorMapEntry), compare_sector_map_entries);
        return sorted_map[logical_idx_in_track_smap_order].physical_idx;
    }
}


int compare_sector_map_entries(const void* a, const void* b) {
    typedef struct { uint32_t physical_idx; uint8_t logical_id; } SectorMapEntry;
    const SectorMapEntry* entry_a = (const SectorMapEntry*)a;
    const SectorMapEntry* entry_b = (const SectorMapEntry*)b;

    if (entry_a->logical_id < entry_b->logical_id) return -1;
    if (entry_a->logical_id > entry_b->logical_id) return 1;
    if (entry_a->physical_idx < entry_b->physical_idx) return -1;
    if (entry_a->physical_idx > entry_b->physical_idx) return 1;
    return 0;
}

void clear_search_highlight(void) {
    g_found_on_track_idx = (size_t)-1;
    g_found_on_sector_log_idx = (uint32_t)-1;
    g_found_offset_in_sector = -1;
    g_found_len = 0;
}

void adjust_view_for_match(long match_offset_in_sector, int term_len) {
    if (!win_data) return; /* Should not happen */
    int data_win_height = getmaxy(win_data);
    if (data_win_height <= 0) data_win_height = DATA_LINES; /* Fallback if window not sized */


    long match_line_start_offset = (match_offset_in_sector / BYTES_PER_LINE) * BYTES_PER_LINE;
    long match_end_offset = match_offset_in_sector + term_len - 1;

    long current_view_first_byte_offset = current_data_offset_in_sector;
    long current_view_last_byte_offset = current_data_offset_in_sector + (data_win_height * BYTES_PER_LINE) - 1;

    /* Check if the start of the match is already visible */
    if (match_offset_in_sector >= current_view_first_byte_offset && match_offset_in_sector <= current_view_last_byte_offset) {
        /* Start is visible. Check if end is also visible. */
        if (match_end_offset <= current_view_last_byte_offset) {
            /* Entire match is visible, no scroll needed. */
            return;
        }
    }
    /* Match is not (fully) visible, scroll to make its starting line the top line */
    current_data_offset_in_sector = match_line_start_offset;
}


void handle_input(void) {
    int ch = wgetch(win_data);
    if (ch == ERR) return;

    int redraw_info = 0;
    int redraw_data = 0;
    int needs_sector_load = 0;
    int navigation_key_pressed = 0;

    size_t target_track_idx = current_track_index_in_image;
    uint32_t target_logical_sector_idx = current_sector_logical_idx;
    long target_data_offset = current_data_offset_in_sector;

    switch (ch) {
    case KEY_UP:
    case KEY_DOWN:
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_PPAGE:
    case KEY_NPAGE:
    case KEY_HOME:  /* Corrected behavior */
    case KEY_END:   /* Corrected behavior */
    case KEY_SHOME: /* Covers KEY_SHOME for all platforms */
#ifdef PDCURSES
    case CTL_HOME:  /* Also covers CTL_HOME if PDCURSES is defined */
#endif
    case KEY_SEND:  /* Covers KEY_SEND for all platforms */
#ifdef PDCURSES
    case CTL_END:   /* Also covers CTL_END if PDCURSES is defined */
#endif
        navigation_key_pressed = 1;
        clear_search_highlight(); /* Cleared for any navigation */

        if (ch == KEY_UP) {
            if (current_track_index_in_image > 0) {
                target_track_idx--; target_logical_sector_idx = 0; target_data_offset = 0;
            }
            else { beep(); }
        }
        else if (ch == KEY_DOWN) {
            if (current_track_index_in_image < total_tracks_in_image - 1) {
                target_track_idx++; target_logical_sector_idx = 0; target_data_offset = 0;
            }
            else { beep(); }
        }
        else if (ch == KEY_LEFT) {
            if (current_sector_logical_idx > 0) {
                target_logical_sector_idx--; target_data_offset = 0;
            }
            else if (current_track_index_in_image > 0) {
                target_track_idx--; target_logical_sector_idx = 0xFFFF; /* Signal to wrap to last sector */ target_data_offset = 0;
            }
            else { beep(); }
        }
        else if (ch == KEY_RIGHT) {
            if (current_track_display.loaded && current_track_display.num_sectors > 0 && current_sector_logical_idx < (uint32_t)(current_track_display.num_sectors - 1)) {
                target_logical_sector_idx++; target_data_offset = 0;
            }
            else if (current_track_index_in_image < total_tracks_in_image - 1) {
                target_track_idx++; target_logical_sector_idx = 0; target_data_offset = 0;
            }
            else { beep(); }
        }
        else if (ch == KEY_PPAGE) {
            int data_win_h_pp = getmaxy(win_data) > 0 ? getmaxy(win_data) : DATA_LINES;
            if (current_data_offset_in_sector >= (long)(data_win_h_pp * BYTES_PER_LINE)) {
                target_data_offset -= (data_win_h_pp * BYTES_PER_LINE);
            }
            else if (current_data_offset_in_sector > 0) {
                target_data_offset = 0;
            }
            else { /* At the top of the current sector's data */
                if (current_sector_logical_idx > 0) {
                    target_logical_sector_idx--;
                    /* Position at end of previous sector's view */
                    const ImdTrackInfo* prev_track_info = imdf_get_track_info(g_imdf_handle, current_track_index_in_image);
                    if (prev_track_info && prev_track_info->sector_size > 0) {
                        target_data_offset = (long)prev_track_info->sector_size - (data_win_h_pp * BYTES_PER_LINE);
                        if (target_data_offset < 0) target_data_offset = 0;
                        target_data_offset = (target_data_offset / BYTES_PER_LINE) * BYTES_PER_LINE; /* Align */
                    }
                    else {
                        target_data_offset = 0;
                    }
                }
                else if (current_track_index_in_image > 0) {
                    target_track_idx--; target_logical_sector_idx = 0xFFFF; /* Signal to wrap to last sector */
                    target_data_offset = 0xFFFFFFFF; /* Signal to go to end of data view on new track/sector */
                }
                else { beep(); }
            }
        }
        else if (ch == KEY_NPAGE) {
            int data_win_h_np = getmaxy(win_data) > 0 ? getmaxy(win_data) : DATA_LINES;
            if (current_data_offset_in_sector + (long)(data_win_h_np * BYTES_PER_LINE) < (long)current_track_display.sector_size) {
                target_data_offset += (data_win_h_np * BYTES_PER_LINE);
            }
            else if (current_track_display.sector_size > 0 &&
                current_data_offset_in_sector < (((long)current_track_display.sector_size - 1) / BYTES_PER_LINE * BYTES_PER_LINE)) {
                target_data_offset = (((long)current_track_display.sector_size - 1) / BYTES_PER_LINE) * BYTES_PER_LINE;
                if ((long)current_track_display.sector_size > (long)(data_win_h_np * BYTES_PER_LINE)) {
                    long temp_offset = (long)current_track_display.sector_size - (data_win_h_np * BYTES_PER_LINE);
                    if (temp_offset < 0) temp_offset = 0;
                    target_data_offset = (temp_offset / BYTES_PER_LINE) * BYTES_PER_LINE;
                    if (target_data_offset + (long)(data_win_h_np * BYTES_PER_LINE) < (long)current_track_display.sector_size && target_data_offset < (((long)current_track_display.sector_size - 1) / BYTES_PER_LINE * BYTES_PER_LINE)) {
                        target_data_offset = ((long)current_track_display.sector_size - 1) - ((data_win_h_np - 1) * BYTES_PER_LINE);
                        if (target_data_offset < 0) target_data_offset = 0;
                        target_data_offset = (target_data_offset / BYTES_PER_LINE) * BYTES_PER_LINE;
                    }
                }
                else {
                    target_data_offset = 0;
                }
                if (target_data_offset < 0) target_data_offset = 0;
            }
            else {
                if (current_track_display.loaded && current_track_display.num_sectors > 0 && current_sector_logical_idx < (uint32_t)(current_track_display.num_sectors - 1)) {
                    target_logical_sector_idx++; target_data_offset = 0;
                }
                else if (current_track_index_in_image < total_tracks_in_image - 1) {
                    target_track_idx++; target_logical_sector_idx = 0; target_data_offset = 0;
                }
                else { beep(); }
            }
        }
        else if (ch == KEY_HOME) {
            /* Corrected: Go to first sector of current track, top of data */
            if (current_track_display.loaded) { /* Ensure track info is available */
                target_logical_sector_idx = 0;
                target_data_offset = 0;
            }
            else {
                beep(); /* Should not happen if track is loaded */
            }
        }
        else if (ch == KEY_END) {
            /* Corrected: Go to last sector of current track, top of data */
            if (current_track_display.loaded && current_track_display.num_sectors > 0) {
                target_logical_sector_idx = current_track_display.num_sectors - 1;
                target_data_offset = 0;
            }
            else {
                beep(); /* No sectors or track not loaded */
            }
        }
        else if (IS_KEY_CTRL_HOME(ch)) { /* Handles KEY_SHOME and CTL_HOME (if PDCURSES) */
            target_track_idx = 0; target_logical_sector_idx = 0; target_data_offset = 0;
        }
        else if (IS_KEY_CTRL_END(ch)) { /* Handles KEY_SEND and CTL_END (if PDCURSES) */
            if (total_tracks_in_image > 0) {
                target_track_idx = total_tracks_in_image - 1;
                target_logical_sector_idx = 0; target_data_offset = 0;
            }
            else { beep(); }
        }
        break; /* End of grouped navigation key handling */

    case 'q': case 'Q': case KEY_F(10):
        cleanup_ui();
        if (g_imdf_handle) imdf_close(g_imdf_handle);
        exit(EXIT_SUCCESS);
        break;
    case ESC_KEY:
        update_status("Press F10 or Q to quit.");
        doupdate();
        timeout(1000);
        wgetch(win_data);
        timeout(100);
        build_status_message();
        update_status(status_message);
        break;
    case KEY_F(1):
        clear_search_highlight();
        display_help_window();
        return;
    case KEY_F(2):
        current_charset = (current_charset == CHARSET_ASCII) ? CHARSET_EBCDIC : CHARSET_ASCII;
        redraw_info = 1; redraw_data = 1;
        clear_search_highlight();
        break;
    case KEY_F(3):
    {
        char search_input_buf[MAX_SEARCH_TERM];
        clear_search_highlight();
        if (get_search_input("Text?", search_input_buf, sizeof(search_input_buf), 0)) {
            strncpy(last_search_term_text, search_input_buf, MAX_SEARCH_TERM - 1);
            last_search_term_text[MAX_SEARCH_TERM - 1] = '\0';
            last_search_type = SEARCH_TYPE_TEXT;
            search_text_from_current(last_search_term_text, 0);
        }
        else {
            build_status_message();
            update_status(status_message);
            draw_info_window();
            draw_data_window();
            doupdate();
        }
    }
    return;
    case KEY_F(4):
    {
        char search_input_buf[MAX_SEARCH_TERM];
        clear_search_highlight();
        if (get_search_input("Hex Bytes?", search_input_buf, sizeof(search_input_buf), 1)) {
            last_search_term_hex_len = 0;
            if (strlen(search_input_buf) > 0) {
                for (size_t i = 0; (i + 1) < strlen(search_input_buf); i += 2) {
                    if (last_search_term_hex_len < (MAX_SEARCH_TERM / 2)) {
                        int hi = ctoh(search_input_buf[i]);
                        int lo = ctoh(search_input_buf[i + 1]);
                        if (hi != -1 && lo != -1) {
                            last_search_term_hex[last_search_term_hex_len++] = (uint8_t)((hi << 4) | lo);
                        }
                        else {
                            display_error("Invalid hex character in input.");
                            last_search_term_hex_len = 0;
                            break;
                        }
                    }
                    else {
                        display_error("Hex search term too long.");
                        last_search_term_hex_len = 0;
                        break;
                    }
                }
            }

            if (last_search_term_hex_len > 0) {
                last_search_type = SEARCH_TYPE_HEX;
                search_hex_from_current(last_search_term_hex, last_search_term_hex_len, 0);
            }
            else if (strlen(search_input_buf) > 0) {
                int error_shown = (strstr(status_message, "Invalid hex") != NULL ||
                    strstr(status_message, "Hex search term too long") != NULL ||
                    strstr(status_message, "even number of digits") != NULL);
                if (!error_shown) display_error("No valid hex bytes entered for search.");
            }
            else {
                build_status_message();
                update_status(status_message);
                draw_info_window();
                draw_data_window();
                doupdate();
            }
        }
        else {
            build_status_message();
            update_status(status_message);
            draw_info_window();
            draw_data_window();
            doupdate();
        }
    }
    return;
    case KEY_F(5):
        repeat_last_search();
        return;

    case '\n':
    case KEY_ENTER:
        clear_search_highlight();
        if (write_enabled) {
            if (current_track_display.loaded && current_track_display.num_sectors > 0 && current_track_display.sector_size > 0) {
                uint8_t sflag_check = 0;
                if (current_sector_physical_idx < LIBIMD_MAX_SECTORS_PER_TRACK) {
                    sflag_check = current_track_display.sflag[current_sector_physical_idx];
                }
                else {
                    display_error("Sector index out of bounds for sflag check.");
                    return;
                }
                if (!IMD_SDR_HAS_DATA(sflag_check)) {
                    display_error("Sector unavailable (no data), cannot edit.");
                }
                else {
                    edit_sector();
                }
            }
            else {
                if (current_track_display.loaded && current_track_display.num_sectors > 0 && current_track_display.sector_size == 0) {
                    display_error("Cannot edit 0-byte sector.");
                }
                else {
                    display_error("No sector loaded to edit.");
                }
            }
        }
        else {
            display_error("Write mode not enabled (-W).");
        }
        return;
    case 'i': case 'I':
        clear_search_highlight();
        ignore_interleave = !ignore_interleave;
        build_status_message();
        update_status(status_message);
        needs_sector_load = 1;
        redraw_info = 1;
        redraw_data = 1;
        break;
    default:
        break;
    }

    /* Apply navigation changes */
    if (target_track_idx != current_track_index_in_image) {
        current_track_index_in_image = target_track_idx;
        load_track_for_display(current_track_index_in_image);

        if (target_logical_sector_idx == 0xFFFF && current_track_display.num_sectors > 0) {
            current_sector_logical_idx = current_track_display.num_sectors - 1;
        }
        else if (target_logical_sector_idx == 0xFFFF) {
            current_sector_logical_idx = 0;
        }
        else {
            current_sector_logical_idx = target_logical_sector_idx;
        }
        if (current_track_display.num_sectors > 0 && current_sector_logical_idx >= current_track_display.num_sectors) {
            current_sector_logical_idx = current_track_display.num_sectors - 1;
        }
        else if (current_track_display.num_sectors == 0) {
            current_sector_logical_idx = 0;
        }

        if (target_data_offset == 0xFFFFFFFF) {
            if (current_track_display.sector_size > 0) {
                int data_win_h_nav = getmaxy(win_data) > 0 ? getmaxy(win_data) : DATA_LINES;
                current_data_offset_in_sector = (long)current_track_display.sector_size - (data_win_h_nav * BYTES_PER_LINE);
                if (current_data_offset_in_sector < 0) current_data_offset_in_sector = 0;
                current_data_offset_in_sector = (current_data_offset_in_sector / BYTES_PER_LINE) * BYTES_PER_LINE;
            }
            else { current_data_offset_in_sector = 0; }
        }
        else {
            current_data_offset_in_sector = target_data_offset;
        }
        if (current_data_offset_in_sector < 0) current_data_offset_in_sector = 0;
        if (current_track_display.sector_size > 0 && current_data_offset_in_sector >= (long)current_track_display.sector_size) {
            current_data_offset_in_sector = (((long)current_track_display.sector_size - 1) / BYTES_PER_LINE) * BYTES_PER_LINE;
            if (current_data_offset_in_sector < 0) current_data_offset_in_sector = 0;
        }
        else if (current_track_display.sector_size == 0) {
            current_data_offset_in_sector = 0;
        }

        load_sector_for_display();
        redraw_info = 1; redraw_data = 1;
    }
    else if (target_logical_sector_idx != current_sector_logical_idx) {
        if (target_logical_sector_idx == 0xFFFF && current_track_display.num_sectors > 0) {
            current_sector_logical_idx = current_track_display.num_sectors - 1;
        }
        else if (target_logical_sector_idx == 0xFFFF) {
            current_sector_logical_idx = 0;
        }
        else {
            current_sector_logical_idx = target_logical_sector_idx;
        }
        if (current_track_display.num_sectors > 0 && current_sector_logical_idx >= current_track_display.num_sectors) {
            current_sector_logical_idx = current_track_display.num_sectors - 1;
        }
        else if (current_track_display.num_sectors == 0) {
            current_sector_logical_idx = 0;
        }

        current_data_offset_in_sector = target_data_offset;
        if (target_data_offset == 0xFFFFFFFF) {
            if (current_track_display.sector_size > 0) {
                int data_win_h_nav = getmaxy(win_data) > 0 ? getmaxy(win_data) : DATA_LINES;
                current_data_offset_in_sector = (long)current_track_display.sector_size - (data_win_h_nav * BYTES_PER_LINE);
                if (current_data_offset_in_sector < 0) current_data_offset_in_sector = 0;
                current_data_offset_in_sector = (current_data_offset_in_sector / BYTES_PER_LINE) * BYTES_PER_LINE;
            }
            else { current_data_offset_in_sector = 0; }
        }
        if (current_data_offset_in_sector < 0) current_data_offset_in_sector = 0;
        if (current_track_display.sector_size > 0 && current_data_offset_in_sector >= (long)current_track_display.sector_size) {
            current_data_offset_in_sector = (((long)current_track_display.sector_size - 1) / BYTES_PER_LINE) * BYTES_PER_LINE;
            if (current_data_offset_in_sector < 0) current_data_offset_in_sector = 0;
        }
        else if (current_track_display.sector_size == 0) {
            current_data_offset_in_sector = 0;
        }

        load_sector_for_display();
        redraw_info = 1; redraw_data = 1;
    }
    else if (target_data_offset != current_data_offset_in_sector) {
        current_data_offset_in_sector = target_data_offset;
        if (current_data_offset_in_sector < 0) current_data_offset_in_sector = 0;

        if (current_track_display.sector_size > 0) {
            if (current_data_offset_in_sector >= (long)current_track_display.sector_size) {
                int data_win_h_nav = getmaxy(win_data) > 0 ? getmaxy(win_data) : DATA_LINES;
                long last_line_start_offset = (((long)current_track_display.sector_size - 1) / BYTES_PER_LINE) * BYTES_PER_LINE;
                current_data_offset_in_sector = last_line_start_offset;
                /* Adjust so the view shows the last page containing data, not just the last line */
                if ((long)current_track_display.sector_size > (long)(data_win_h_nav * BYTES_PER_LINE)) {
                    long temp_offset = (long)current_track_display.sector_size - (data_win_h_nav * BYTES_PER_LINE);
                    if (temp_offset < 0) temp_offset = 0;
                    current_data_offset_in_sector = (temp_offset / BYTES_PER_LINE) * BYTES_PER_LINE;
                    /* Ensure this adjustment does not hide the actual last line if sector ends mid-page */
                    if (current_data_offset_in_sector > last_line_start_offset && last_line_start_offset >= 0) {
                        current_data_offset_in_sector = last_line_start_offset;
                    }
                    else if (current_data_offset_in_sector < 0) {
                        current_data_offset_in_sector = 0;
                    }
                }
                if (current_data_offset_in_sector < 0) current_data_offset_in_sector = 0;
            }
        }
        else {
            current_data_offset_in_sector = 0;
        }
        redraw_data = 1;
    }


    if (needs_sector_load) {
        if (current_track_display.num_sectors > 0 && current_sector_logical_idx >= current_track_display.num_sectors) {
            current_sector_logical_idx = 0;
            current_data_offset_in_sector = 0;
        }
        else if (current_track_display.num_sectors == 0) {
            current_sector_logical_idx = 0;
            current_data_offset_in_sector = 0;
        }
        load_sector_for_display();
        redraw_info = 1; redraw_data = 1;
    }

    if (redraw_info) draw_info_window();
    if (redraw_data) draw_data_window();

    if (redraw_info || redraw_data || navigation_key_pressed || needs_sector_load || ch == ESC_KEY) {
        doupdate();
    }
}

int ctoh(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int get_search_input(const char* prompt, char* buffer, int buffer_size, int is_hex) {
    WINDOW* popup_win;
    int screen_h, screen_w;
    getmaxyx(stdscr, screen_h, screen_w);

    int win_h = 4;
    int term_display_width = is_hex ? (MAX_SEARCH_TERM / 2 * 2) : MAX_SEARCH_TERM;
    int win_w = (int)strlen(prompt) + term_display_width + 8;
    if (win_w > screen_w - 4) win_w = screen_w - 4;
    int input_field_width = win_w - (int)strlen(prompt) - 4;
    if (input_field_width < 10) input_field_width = 10;
    if (input_field_width >= buffer_size) input_field_width = buffer_size - 1;


    int start_y = (screen_h - win_h) / 2;
    int start_x = (screen_w - win_w) / 2;

    popup_win = newwin(win_h, win_w, start_y, start_x);
    wbkgd(popup_win, COLOR_PAIR(CP_SEARCH_BOX));
    box(popup_win, 0, 0);
    keypad(popup_win, TRUE);

    mvwprintw(popup_win, 1, 2, "%s", prompt);

    buffer[0] = '\0';
    if (is_hex) {
        if (last_search_type == SEARCH_TYPE_HEX && last_search_term_hex_len > 0) {
            for (int i = 0; i < last_search_term_hex_len; ++i) {
                if ((size_t)(i * 2 + 2) < (size_t)buffer_size) {
                    snprintf(buffer + (i * 2), 3, "%02X", last_search_term_hex[i]);
                }
                else break;
            }
        }
    }
    else {
        if (last_search_type == SEARCH_TYPE_TEXT && strlen(last_search_term_text) > 0) {
            strncpy(buffer, last_search_term_text, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
        }
    }

    int current_len = (int)strlen(buffer);
    int cursor_pos = current_len;
    int ch_input;
    int MveX = 2;

    curs_set(1);

    while (1) {
        wattron(popup_win, COLOR_PAIR(CP_SEARCH_BOX));
        mvwprintw(popup_win, 2, MveX, "%-*.*s", input_field_width, input_field_width, buffer);
        for (int k = current_len; k < input_field_width; ++k) {
            mvwaddch(popup_win, 2, MveX + k, ' ');
        }
        wattroff(popup_win, COLOR_PAIR(CP_SEARCH_BOX));

        wmove(popup_win, 2, MveX + cursor_pos);
        wrefresh(popup_win);

        ch_input = wgetch(popup_win);

        switch (ch_input) {
        case KEY_BACKSPACE: case 127: case '\b':
            if (cursor_pos > 0) {
                memmove(&buffer[cursor_pos - 1], &buffer[cursor_pos], current_len - cursor_pos + 1);
                cursor_pos--;
                current_len--;
            }
            break;
        case KEY_DC:
            if (cursor_pos < current_len) {
                memmove(&buffer[cursor_pos], &buffer[cursor_pos + 1], current_len - cursor_pos);
                current_len--;
            }
            break;
        case KEY_LEFT:
            if (cursor_pos > 0) cursor_pos--;
            break;
        case KEY_RIGHT:
            if (cursor_pos < current_len) cursor_pos++;
            break;
        case '\n': case KEY_ENTER:
            goto process_input_label;
        case ESC_KEY:
            buffer[0] = '\0';
            goto process_input_label;
        default:
            if (isprint(ch_input)) {
                if (is_hex && !isxdigit(ch_input)) {
                    beep();
                }
                else if (current_len < input_field_width && current_len < buffer_size - 1) {
                    if (cursor_pos < current_len) {
                        memmove(&buffer[cursor_pos + 1], &buffer[cursor_pos], current_len - cursor_pos);
                    }
                    buffer[cursor_pos] = (char)ch_input;
                    cursor_pos++;
                    current_len++;
                    buffer[current_len] = '\0';
                }
                else {
                    beep();
                }
            }
            break;
        }
    }

process_input_label:
    curs_set(0);
    delwin(popup_win);

    touchwin(stdscr); refresh();


    if (strlen(buffer) == 0) return 0;

    if (is_hex) {
        if (strlen(buffer) % 2 != 0) {
            display_error("Hex string must have an even number of digits.");
            return 0;
        }
    }
    return 1;
}

/*
 * Common internal search function to find a pattern (text or hex) in the disk image.
 * Iterates through tracks and sectors, handling intra-sector and inter-sector (span) matches.
 *
 * Parameters:
 * raw_term: Pointer to the search term (bytes for hex, char* for text).
 * raw_term_len: Length of the search term.
 * is_text_search: 1 if text search (implies EBCDIC conversion of disk data if enabled), 0 for hex.
 * start_from_next_byte: 1 to start searching from the byte after the last find/current relevant position, 0 to start from current.
 * out_found_track_idx: Pointer to store the track index of the found match.
 * out_found_sector_log_idx: Pointer to store the logical sector index of the found match.
 * out_found_offset_in_sector: Pointer to store the byte offset within the sector of the found match.
 * out_found_actual_len: Pointer to store the length of the found match (should be raw_term_len).
 *
 * Returns:
 * 1 if the pattern is found.
 * 0 if the pattern is not found.
 * -1 if a memory allocation error occurred.
 * If found (returns 1), the out_* parameters are populated with the location details.
 */
static int find_pattern_in_image(
    const uint8_t* raw_term,
    int raw_term_len,
    int is_text_search,
    int start_from_next_byte,
    size_t* out_found_track_idx,
    uint32_t* out_found_sector_log_idx,
    long* out_found_offset_in_sector,
    int* out_found_actual_len
) {
    uint8_t* primary_sector_data = NULL;
    uint8_t* processed_primary_sector_data = NULL;
    uint8_t* next_sector_data = NULL;
    uint8_t* processed_next_sector_data = NULL;
    int search_status = 0; /* 0 = not found, 1 = found, -1 = error */

    size_t search_start_track = current_track_index_in_image;
    uint32_t search_start_sector_log = current_sector_logical_idx;
    long initial_offset_for_first_sector = 0;

    if (start_from_next_byte) {
        if (g_found_len > 0 &&
            g_found_on_track_idx == search_start_track &&
            g_found_on_sector_log_idx == search_start_sector_log) {
            initial_offset_for_first_sector = g_found_offset_in_sector + 1;
        }
        else if (g_found_len <= 0) { /* If start_from_next_byte is true, but no prior global find */
            /* This case is less common; implies advancing from current view pos */
            initial_offset_for_first_sector = current_data_offset_in_sector + 1; /* Or another relevant start point */
            /* Ensure it's within bounds of where current_data_offset_in_sector might be */
            if (current_track_display.loaded && current_track_display.sector_size > 0 &&
                initial_offset_for_first_sector >= (long)current_track_display.sector_size) {
                initial_offset_for_first_sector = current_track_display.sector_size; /* Will search 0 bytes in this specific case */
            }
        }
        /* if g_found_len > 0 but on different track/sector, start_from_next_byte on a new track/sector implies start from 0 */
    }

    primary_sector_data = (uint8_t*)malloc(LIBIMD_MAX_SECTOR_SIZE);
    processed_primary_sector_data = (uint8_t*)malloc(LIBIMD_MAX_SECTOR_SIZE);
    next_sector_data = (uint8_t*)malloc(LIBIMD_MAX_SECTOR_SIZE);
    processed_next_sector_data = (uint8_t*)malloc(LIBIMD_MAX_SECTOR_SIZE);

    if (!primary_sector_data || !processed_primary_sector_data || !next_sector_data || !processed_next_sector_data) {
        display_error("Memory allocation failed for search buffers.");
        search_status = -1;
        goto cleanup_find_pattern;
    }

    for (size_t tk_idx = search_start_track; tk_idx < total_tracks_in_image; ++tk_idx) {
        const ImdTrackInfo* search_track_info_ptr = imdf_get_track_info(g_imdf_handle, tk_idx);
        if (!search_track_info_ptr || !search_track_info_ptr->loaded) continue;

        uint32_t sec_idx_start_loop = (tk_idx == search_start_track) ? search_start_sector_log : 0;

        for (uint32_t sec_log_idx = sec_idx_start_loop; sec_log_idx < search_track_info_ptr->num_sectors; ++sec_log_idx) {
            int primary_sector_actual_size = load_specific_sector_data(tk_idx, sec_log_idx, primary_sector_data, LIBIMD_MAX_SECTOR_SIZE, NULL);

            if (primary_sector_actual_size == 0) {
                if (tk_idx == search_start_track && sec_log_idx == search_start_sector_log) {
                    initial_offset_for_first_sector = 0; /* Reset for next viable sector */
                }
                continue;
            }

            for (int k = 0; k < primary_sector_actual_size; ++k) {
                uint8_t val = primary_sector_data[k] ^ xor_mask;
                if (is_text_search && current_charset == CHARSET_EBCDIC) {
                    processed_primary_sector_data[k] = ebcdic_to_ascii[val];
                }
                else {
                    processed_primary_sector_data[k] = val;
                }
            }

            long offset_in_sector_to_search_from = 0;
            if (tk_idx == search_start_track && sec_log_idx == search_start_sector_log) {
                offset_in_sector_to_search_from = initial_offset_for_first_sector;
            }
            /* Ensure the offset is not negative or excessively large */
            if (offset_in_sector_to_search_from < 0) offset_in_sector_to_search_from = 0;
            if (offset_in_sector_to_search_from >= primary_sector_actual_size) {
                /* Effectively, no search in this sector if starting point is already at or past the end */
                offset_in_sector_to_search_from = primary_sector_actual_size;
            }


            /* Pass 1: Search within the primary sector */
            if (raw_term_len <= primary_sector_actual_size) { /* Optimization */
                for (long i = offset_in_sector_to_search_from; (i + (long)raw_term_len) <= primary_sector_actual_size; ++i) {
                    if (memcmp(processed_primary_sector_data + i, raw_term, raw_term_len) == 0) {
                        *out_found_track_idx = tk_idx;
                        *out_found_sector_log_idx = sec_log_idx;
                        *out_found_offset_in_sector = i;
                        *out_found_actual_len = raw_term_len;
                        search_status = 1;
                        goto cleanup_find_pattern;
                    }
                }
            }

            /* Pass 2: Search for spans into the next sector (if raw_term_len > 1) */
            if (raw_term_len > 1) {
                for (int len_in_primary = 1; len_in_primary < raw_term_len; ++len_in_primary) {
                    if (primary_sector_actual_size < len_in_primary) continue;

                    long primary_match_start_offset = primary_sector_actual_size - len_in_primary;

                    if (primary_match_start_offset < offset_in_sector_to_search_from &&
                        (tk_idx == search_start_track && sec_log_idx == search_start_sector_log)) {
                        continue;
                    }
                    if (primary_match_start_offset < 0) continue; /* Should not happen if primary_sector_actual_size >= len_in_primary */


                    if (memcmp(processed_primary_sector_data + primary_match_start_offset, raw_term, len_in_primary) == 0) {
                        int remaining_len = raw_term_len - len_in_primary;
                        size_t next_s_trk_idx = tk_idx;
                        uint32_t next_s_sec_log_idx = sec_log_idx + 1;
                        const ImdTrackInfo* next_lookup_track_info = imdf_get_track_info(g_imdf_handle, next_s_trk_idx);

                        if (!next_lookup_track_info || !next_lookup_track_info->loaded) continue;

                        if (next_s_sec_log_idx >= next_lookup_track_info->num_sectors) {
                            next_s_trk_idx++;
                            next_s_sec_log_idx = 0;
                            if (next_s_trk_idx >= total_tracks_in_image) continue;
                        }

                        int next_sector_actual_s = load_specific_sector_data(next_s_trk_idx, next_s_sec_log_idx, next_sector_data, LIBIMD_MAX_SECTOR_SIZE, NULL);

                        if (next_sector_actual_s >= remaining_len) {
                            for (int k = 0; k < remaining_len; ++k) {
                                uint8_t val = next_sector_data[k] ^ xor_mask;
                                if (is_text_search && current_charset == CHARSET_EBCDIC) {
                                    processed_next_sector_data[k] = ebcdic_to_ascii[val];
                                }
                                else {
                                    processed_next_sector_data[k] = val;
                                }
                            }
                            if (memcmp(processed_next_sector_data, raw_term + len_in_primary, remaining_len) == 0) {
                                *out_found_track_idx = tk_idx;
                                *out_found_sector_log_idx = sec_log_idx;
                                *out_found_offset_in_sector = primary_match_start_offset;
                                *out_found_actual_len = raw_term_len;
                                search_status = 1;
                                goto cleanup_find_pattern;
                            }
                        }
                    }
                }
            }

            if (tk_idx == search_start_track && sec_log_idx == search_start_sector_log) {
                initial_offset_for_first_sector = 0; /* Processed the first specific sector, subsequent ones start at 0 */
            }
        }
        search_start_sector_log = 0; /* For next track, start search from its sector 0 */
    }

cleanup_find_pattern:
    if (primary_sector_data) free(primary_sector_data);
    if (processed_primary_sector_data) free(processed_primary_sector_data);
    if (next_sector_data) free(next_sector_data);
    if (processed_next_sector_data) free(processed_next_sector_data);
    return search_status;
}

void search_text_from_current(const char* term, int start_from_next_byte) {
    size_t term_len;
    int search_result;
    size_t found_tk_idx;
    uint32_t found_sec_log_idx;
    long found_offset;
    int found_term_len;

    if (term == NULL || (term_len = strlen(term)) == 0) {
        update_status("Search: No text term provided."); doupdate();
        clear_search_highlight(); /* Ensure no old highlight persists */
        return;
    }
    if (term_len >= MAX_SEARCH_TERM) {
        display_error("Search term is too long.");
        clear_search_highlight();
        return;
    }

    update_status("Searching for text..."); doupdate();

    search_result = find_pattern_in_image(
        (const uint8_t*)term, (int)term_len,
        1, /* is_text_search = true */
        start_from_next_byte,
        &found_tk_idx, &found_sec_log_idx, &found_offset, &found_term_len
    );

    if (search_result == 1) { /* Found */
        current_track_index_in_image = found_tk_idx;
        current_sector_logical_idx = found_sec_log_idx;
        /* current_data_offset_in_sector will be handled by adjust_view_for_match */

        if (load_track_for_display(current_track_index_in_image) != 0) {
            /* Error displayed by load_track_for_display */
            clear_search_highlight(); /* Clean up search state */
            return;
        }
        /* current_sector_logical_idx is already set, load_sector_for_display will use it */
        if (load_sector_for_display() != 0) {
            /* Error displayed by load_sector_for_display */
            clear_search_highlight();
            return;
        }

        g_found_on_track_idx = found_tk_idx;
        g_found_on_sector_log_idx = found_sec_log_idx;
        g_found_offset_in_sector = found_offset;
        g_found_len = found_term_len;

        adjust_view_for_match(found_offset, found_term_len);
        snprintf(status_message, sizeof(status_message), "Found at Trk:%zu SecLogIdx:%u Offset:%ld", found_tk_idx, found_sec_log_idx, found_offset);
        update_status(status_message);
        draw_info_window();
        draw_data_window();
        doupdate();
    }
    else if (search_result == 0) { /* Not found */
        update_status("Search: Text not found."); doupdate();
        beep();
        clear_search_highlight(); /* Important to clear if not found */
        /* Redraw to remove any old highlights if necessary, though clear_search_highlight should trigger it */
        draw_data_window();
        doupdate();
    }
    else { /* search_result == -1, Error (e.g., memory allocation) */
        /* display_error was called by find_pattern_in_image */
        clear_search_highlight();
        /* Redraw to ensure UI is consistent after error popup */
        draw_info_window();
        draw_data_window();
        doupdate();
    }
}

void search_hex_from_current(const uint8_t* hex_term, int term_len, int start_from_next_byte) {
    int search_result;
    size_t found_tk_idx;
    uint32_t found_sec_log_idx;
    long found_offset;
    int found_term_len_actual;

    if (hex_term == NULL || term_len == 0) {
        update_status("Search: No hex term provided."); doupdate();
        clear_search_highlight();
        return;
    }
    /* MAX_SEARCH_TERM is for text chars, hex_term is bytes (half of that for input chars) */
    if (term_len >= MAX_SEARCH_TERM / 2) {
        display_error("Search hex term is too long.");
        clear_search_highlight();
        return;
    }

    update_status("Searching for hex..."); doupdate();

    search_result = find_pattern_in_image(
        hex_term, term_len,
        0, /* is_text_search = false */
        start_from_next_byte,
        &found_tk_idx, &found_sec_log_idx, &found_offset, &found_term_len_actual
    );

    if (search_result == 1) { /* Found */
        current_track_index_in_image = found_tk_idx;
        current_sector_logical_idx = found_sec_log_idx;

        if (load_track_for_display(current_track_index_in_image) != 0) {
            clear_search_highlight();
            return;
        }
        if (load_sector_for_display() != 0) {
            clear_search_highlight();
            return;
        }

        g_found_on_track_idx = found_tk_idx;
        g_found_on_sector_log_idx = found_sec_log_idx;
        g_found_offset_in_sector = found_offset;
        g_found_len = found_term_len_actual;

        adjust_view_for_match(found_offset, found_term_len_actual);
        snprintf(status_message, sizeof(status_message), "Found hex at Trk:%zu SecLogIdx:%u Offset:%ld", found_tk_idx, found_sec_log_idx, found_offset);
        update_status(status_message);
        draw_info_window();
        draw_data_window();
        doupdate();
    }
    else if (search_result == 0) { /* Not found */
        update_status("Search: Hex pattern not found."); doupdate();
        beep();
        clear_search_highlight();
        draw_data_window();
        doupdate();
    }
    else { /* search_result == -1, Error */
        clear_search_highlight();
        draw_info_window();
        draw_data_window();
        doupdate();
    }
}

void repeat_last_search(void) {
    if (last_search_type == SEARCH_TYPE_NONE) {
        update_status("No previous search to repeat."); doupdate(); beep();
        clear_search_highlight();
        return;
    }
    if (last_search_type == SEARCH_TYPE_TEXT) {
        search_text_from_current(last_search_term_text, 1);
    }
    else if (last_search_type == SEARCH_TYPE_HEX) {
        search_hex_from_current(last_search_term_hex, last_search_term_hex_len, 1);
    }
}


void edit_sector(void) {
    long edit_cursor_offset_in_sector = current_data_offset_in_sector;
    int data_modified_this_edit_session = 0;
    int redraw_needed = 1;
    char original_status_dynamic[sizeof(status_message)]; /* To store the dynamically built status */

    uint8_t* edit_buffer = NULL;
    uint8_t* original_sector_data_at_edit_start = NULL;
    int pending_nibble_value = -1;
    long pending_nibble_offset = -1;
    char edit_status_msg[sizeof(status_message)];

    build_status_message(); /* Ensure status_message is current */
    strncpy(original_status_dynamic, status_message, sizeof(original_status_dynamic) - 1);
    original_status_dynamic[sizeof(original_status_dynamic) - 1] = 0;

    if (current_track_display.sector_size > LIBIMD_MAX_SECTOR_SIZE) {
        display_error("Sector too large to edit.");
        goto exit_edit_loop; /* No memory allocated yet */
    }
    if (current_track_display.sector_size == 0) {
        display_error("Cannot edit 0-byte sector.");
        goto exit_edit_loop; /* No memory allocated yet */
    }

    /* Allocate buffers on the heap */
    edit_buffer = (uint8_t*)malloc(current_track_display.sector_size);
    original_sector_data_at_edit_start = (uint8_t*)malloc(current_track_display.sector_size);

    if (!edit_buffer || !original_sector_data_at_edit_start) {
        display_error("Memory allocation failed for edit buffers.");
        goto exit_edit_loop; /* free any partially allocated buffer */
    }

    memcpy(original_sector_data_at_edit_start, current_sector_buffer, current_track_display.sector_size);
    memcpy(edit_buffer, original_sector_data_at_edit_start, current_track_display.sector_size);


    snprintf(edit_status_msg, sizeof(edit_status_msg), "EDIT | Arrows=Move F3=Mode Enter/ESC/F10=Exit | Type to modify");
    update_status(edit_status_msg); /* Uses wnoutrefresh */
    doupdate(); /* Make initial edit status visible */
    curs_set(1);

    while (1) {
        if (edit_cursor_offset_in_sector < 0) edit_cursor_offset_in_sector = 0;
        if (current_track_display.sector_size > 0) { /* Check sector_size to prevent access if 0 */
            if ((uint32_t)edit_cursor_offset_in_sector >= current_track_display.sector_size) {
                edit_cursor_offset_in_sector = (long)current_track_display.sector_size - 1;
            }
        }
        else { /* Should have been caught by initial checks */
            edit_cursor_offset_in_sector = 0;
        }


        long first_visible_offset = current_data_offset_in_sector;
        int data_win_h = getmaxy(win_data) > 0 ? getmaxy(win_data) : DATA_LINES;


        if (edit_cursor_offset_in_sector < first_visible_offset) {
            current_data_offset_in_sector = (edit_cursor_offset_in_sector / BYTES_PER_LINE) * BYTES_PER_LINE;
            redraw_needed = 1;
        }
        else if (edit_cursor_offset_in_sector >= first_visible_offset + (data_win_h * BYTES_PER_LINE)) {
            current_data_offset_in_sector = ((edit_cursor_offset_in_sector - (data_win_h * BYTES_PER_LINE) + BYTES_PER_LINE) / BYTES_PER_LINE) * BYTES_PER_LINE;
            if (current_data_offset_in_sector < 0) current_data_offset_in_sector = 0;
            redraw_needed = 1;
        }


        if (redraw_needed) {
            if (current_track_display.sector_size > 0) {
                memcpy(current_sector_buffer, edit_buffer, current_track_display.sector_size);
                draw_data_window();
            }
            else {
                draw_data_window();
            }
            redraw_needed = 0;
        }

        int cursor_line_in_window = (int)((edit_cursor_offset_in_sector - current_data_offset_in_sector) / BYTES_PER_LINE);
        int cursor_col_offset_in_byte_line = (int)((edit_cursor_offset_in_sector - current_data_offset_in_sector) % BYTES_PER_LINE);
        int screen_col_for_byte;

        if (memcmp(current_sector_buffer, edit_buffer, current_track_display.sector_size) != 0) {
            memcpy(current_sector_buffer, edit_buffer, current_track_display.sector_size);
            draw_data_window();
        }

        if (current_track_display.sector_size > 0 && cursor_line_in_window >= 0 && cursor_line_in_window < data_win_h) {
            if (current_edit_mode == EDIT_MODE_HEX) {
                screen_col_for_byte = 6 + (cursor_col_offset_in_byte_line * 3) + (cursor_col_offset_in_byte_line / 8) + 1;
                uint8_t display_byte_val;
                int cursor_on_first_nibble_char_pos = 1;

                if (pending_nibble_value != -1 && pending_nibble_offset == edit_cursor_offset_in_sector) {
                    uint8_t original_low_nibble = (edit_buffer[edit_cursor_offset_in_sector] ^ xor_mask) & 0x0F;
                    display_byte_val = (pending_nibble_value << 4) | original_low_nibble;
                    cursor_on_first_nibble_char_pos = 0;
                }
                else {
                    display_byte_val = edit_buffer[edit_cursor_offset_in_sector] ^ xor_mask;
                }

                wattron(win_data, COLOR_PAIR(CP_EDIT_HEX) | A_REVERSE);
                mvwprintw(win_data, cursor_line_in_window, screen_col_for_byte, "%02X", display_byte_val);
                wattroff(win_data, COLOR_PAIR(CP_EDIT_HEX) | A_REVERSE);
                wmove(win_data, cursor_line_in_window, screen_col_for_byte + (cursor_on_first_nibble_char_pos ? 0 : 1));
            }
            else { /* EDIT_MODE_ASCII */
                screen_col_for_byte = 6 + (BYTES_PER_LINE * 3) + (BYTES_PER_LINE / 8) + 1 + cursor_col_offset_in_byte_line;
                uint8_t val_ascii = edit_buffer[edit_cursor_offset_in_sector] ^ xor_mask;
                uint8_t display_char_ascii = (current_charset == CHARSET_EBCDIC) ? ebcdic_to_ascii[val_ascii] : val_ascii;
                if (display_char_ascii == '\t') display_char_ascii = ' '; else if (display_char_ascii == '\r') display_char_ascii = '<'; else if (display_char_ascii == '\n') display_char_ascii = '>';
                char char_to_display_ascii = (isprint(display_char_ascii) ? display_char_ascii : '.');

                wattron(win_data, COLOR_PAIR(CP_EDIT_ASC) | A_REVERSE);
                mvwaddch(win_data, cursor_line_in_window, screen_col_for_byte, char_to_display_ascii);
                wattroff(win_data, COLOR_PAIR(CP_EDIT_ASC) | A_REVERSE);
                wmove(win_data, cursor_line_in_window, screen_col_for_byte);
            }
        }
        else {
            if (win_data) wmove(win_data, 0, 0);
        }
        wrefresh(win_data);


        int ch = wgetch(win_data);

        if (pending_nibble_value != -1) {
            int is_nav_key_or_mode_switch = (ch == KEY_UP || ch == KEY_DOWN || ch == KEY_LEFT || ch == KEY_RIGHT ||
                ch == KEY_PPAGE || ch == KEY_NPAGE || ch == KEY_HOME || ch == KEY_END ||
                ch == KEY_F(3));
            int is_valid_hex_for_second_digit = (ctoh(ch) != -1);

            if (is_nav_key_or_mode_switch) {
                pending_nibble_value = -1; pending_nibble_offset = -1;
                redraw_needed = 1;
                snprintf(edit_status_msg, sizeof(edit_status_msg), "EDIT | Nibble entry cancelled. Arrows=Move F3=Mode Enter/ESC/F10=Exit"); update_status(edit_status_msg);
            }
            else if (pending_nibble_offset == edit_cursor_offset_in_sector && ch != ERR &&
                !is_valid_hex_for_second_digit && ch != '\n' && ch != KEY_ENTER && ch != ESC_KEY && ch != KEY_F(10)) {
                pending_nibble_value = -1; pending_nibble_offset = -1;
                beep();
                redraw_needed = 1;
                snprintf(edit_status_msg, sizeof(edit_status_msg), "EDIT | Invalid 2nd hex digit. Cancelled. Arrows=Move F3=Mode Enter/ESC/F10=Exit"); update_status(edit_status_msg);
            }
        }


        switch (ch) {
        case KEY_UP:    if (current_track_display.sector_size > 0) { edit_cursor_offset_in_sector -= BYTES_PER_LINE; }
                   else { beep(); } break;
        case KEY_DOWN:  if (current_track_display.sector_size > 0) { edit_cursor_offset_in_sector += BYTES_PER_LINE; }
                     else { beep(); } break;
        case KEY_LEFT:  if (current_track_display.sector_size > 0) { edit_cursor_offset_in_sector--; }
                     else { beep(); } break;
        case KEY_RIGHT: if (current_track_display.sector_size > 0) { edit_cursor_offset_in_sector++; }
                      else { beep(); } break;
        case KEY_PPAGE: if (current_track_display.sector_size > 0) { edit_cursor_offset_in_sector -= (data_win_h * BYTES_PER_LINE); }
                      else { beep(); } break;
        case KEY_NPAGE: if (current_track_display.sector_size > 0) { edit_cursor_offset_in_sector += (data_win_h * BYTES_PER_LINE); }
                      else { beep(); } break;
        case KEY_HOME:  if (current_track_display.sector_size > 0) {
            edit_cursor_offset_in_sector = (edit_cursor_offset_in_sector / BYTES_PER_LINE) * BYTES_PER_LINE;
        }
                     else { beep(); } break;
        case KEY_END:   if (current_track_display.sector_size > 0) {
            edit_cursor_offset_in_sector = ((edit_cursor_offset_in_sector / BYTES_PER_LINE) * BYTES_PER_LINE) + BYTES_PER_LINE - 1;
            if (edit_cursor_offset_in_sector >= (long)current_track_display.sector_size) edit_cursor_offset_in_sector = current_track_display.sector_size - 1;
        }
                    else { beep(); } break;
        case KEY_F(3):
            current_edit_mode = (current_edit_mode == EDIT_MODE_HEX) ? EDIT_MODE_ASCII : EDIT_MODE_HEX;
            pending_nibble_value = -1; pending_nibble_offset = -1;
            redraw_needed = 1;
            snprintf(edit_status_msg, sizeof(edit_status_msg), "EDIT | Mode: %s. Arrows=Move F3=Mode Enter/ESC/F10=Exit", current_edit_mode == EDIT_MODE_HEX ? "HEX" : "ASCII"); update_status(edit_status_msg);
            break;

        case '\n':
        case KEY_ENTER:
        case ESC_KEY:
        case KEY_F(10):
            pending_nibble_value = -1; pending_nibble_offset = -1;
            curs_set(0);

            if (data_modified_this_edit_session && ch != ESC_KEY && ch != KEY_F(10)) {
                update_status("Save sector changes to disk? (Y/N)"); doupdate();
                timeout(-1); int confirm_ch; do { confirm_ch = tolower(wgetch(win_data)); } while (confirm_ch != 'y' && confirm_ch != 'n' && confirm_ch != ESC_KEY); timeout(100);

                if (confirm_ch == 'y') {
                    update_status("Writing sector..."); doupdate();
                    int write_res = imdf_write_sector(g_imdf_handle, current_track_display.cyl, current_track_display.head,
                        current_sector_logical_id, edit_buffer, current_track_display.sector_size);

                    if (write_res == IMDF_ERR_OK) {
                        memcpy(current_sector_buffer, edit_buffer, current_track_display.sector_size);
                        const ImdTrackInfo* updated_imdf_track = imdf_get_track_info(g_imdf_handle, current_track_index_in_image);
                        if (updated_imdf_track) {
                            uint32_t preserved_logical_idx = current_sector_logical_idx;
                            long preserved_data_offset = current_data_offset_in_sector;

                            copy_track_metadata_for_display(updated_imdf_track);
                            if (current_track_display.num_sectors == 0) {
                                current_sector_logical_idx = 0; current_data_offset_in_sector = 0;
                            }
                            else {
                                if (preserved_logical_idx >= current_track_display.num_sectors) {
                                    current_sector_logical_idx = current_track_display.num_sectors - 1; preserved_data_offset = 0;
                                }
                                else { current_sector_logical_idx = preserved_logical_idx; }

                                if (preserved_data_offset >= (long)current_track_display.sector_size) {
                                    current_data_offset_in_sector = 0;
                                }
                                else { current_data_offset_in_sector = preserved_data_offset; }
                            }
                            load_sector_for_display();
                        }
                        else {
                            display_error("ERR: Post-write track info fetch failed!");
                        }
                        draw_info_window();
                        draw_data_window();
                        update_status("Sector written successfully.");
                        doupdate();

                        timeout(1000);
                        wgetch(win_data);
                        timeout(100);
                    }
                    else {
                        char err_buf[100]; snprintf(err_buf, sizeof(err_buf), "Error writing sector: %d", write_res);
                        memcpy(current_sector_buffer, original_sector_data_at_edit_start, current_track_display.sector_size);
                        display_error(err_buf);
                    }
                }
                else {
                    memcpy(current_sector_buffer, original_sector_data_at_edit_start, current_track_display.sector_size);
                    update_status("Changes discarded."); doupdate();
                    timeout(1000); wgetch(win_data); timeout(100);
                }
            }
            else if (ch == ESC_KEY || ch == KEY_F(10)) {
                if (data_modified_this_edit_session) {
                    memcpy(current_sector_buffer, original_sector_data_at_edit_start, current_track_display.sector_size);
                    update_status("Changes discarded (ESC/F10).");
                }
                else {
                    update_status("Edit cancelled (ESC/F10).");
                }
                doupdate();
                timeout(1000); wgetch(win_data); timeout(100);
            }
            goto exit_edit_loop;

        default:
            if (current_track_display.sector_size == 0) { beep(); break; }

            if (current_edit_mode == EDIT_MODE_HEX) {
                int nibble = ctoh(ch);
                if (nibble != -1) {
                    if (pending_nibble_value != -1 && pending_nibble_offset == edit_cursor_offset_in_sector) {
                        uint8_t final_byte_value = (pending_nibble_value << 4) | nibble;
                        if ((uint32_t)edit_cursor_offset_in_sector < current_track_display.sector_size) {
                            edit_buffer[edit_cursor_offset_in_sector] = final_byte_value ^ xor_mask;
                            data_modified_this_edit_session = 1;
                        }
                        pending_nibble_value = -1;
                        pending_nibble_offset = -1;
                        edit_cursor_offset_in_sector++;
                        redraw_needed = 1;
                        snprintf(edit_status_msg, sizeof(edit_status_msg), "EDIT | Byte 0x%02lX written.", (edit_cursor_offset_in_sector > 0 && (uint32_t)edit_cursor_offset_in_sector <= current_track_display.sector_size) ? edit_cursor_offset_in_sector - 1 : edit_cursor_offset_in_sector); update_status(edit_status_msg);
                    }
                    else {
                        pending_nibble_value = nibble;
                        pending_nibble_offset = edit_cursor_offset_in_sector;
                        redraw_needed = 1;
                        snprintf(edit_status_msg, sizeof(edit_status_msg), "EDIT | Enter 2nd hex digit for byte 0x%02lX...", edit_cursor_offset_in_sector); update_status(edit_status_msg);
                    }
                }
                else { beep(); }
            }
            else {
                pending_nibble_value = -1; pending_nibble_offset = -1;
                if (isprint(ch) || ch == '.') {
                    uint8_t byte_to_write_ascii = (uint8_t)ch;
                    if (current_charset == CHARSET_EBCDIC) {
                        int found_ebcdic_char = 0;
                        for (int eb_val = 0; eb_val < 256; ++eb_val) {
                            if (ebcdic_to_ascii[eb_val] == ch) {
                                byte_to_write_ascii = (uint8_t)eb_val;
                                found_ebcdic_char = 1;
                                break;
                            }
                        }
                        if (!found_ebcdic_char && ch != '.') {
                            beep();
                            snprintf(edit_status_msg, sizeof(edit_status_msg), "EDIT | Char '%c' has no EBCDIC equivalent.", ch); update_status(edit_status_msg);
                            break;
                        }
                        else if (!found_ebcdic_char && ch == '.') {
                            for (int eb_val = 0; eb_val < 256; ++eb_val) if (ebcdic_to_ascii[eb_val] == '.') { byte_to_write_ascii = (uint8_t)eb_val; break; }
                        }
                    }
                    if ((uint32_t)edit_cursor_offset_in_sector < current_track_display.sector_size) {
                        edit_buffer[edit_cursor_offset_in_sector] = byte_to_write_ascii ^ xor_mask;
                        data_modified_this_edit_session = 1;
                    }
                    edit_cursor_offset_in_sector++;
                    redraw_needed = 1;
                    snprintf(edit_status_msg, sizeof(edit_status_msg), "EDIT | Byte 0x%02lX written.", (edit_cursor_offset_in_sector > 0 && (uint32_t)edit_cursor_offset_in_sector <= current_track_display.sector_size) ? edit_cursor_offset_in_sector - 1 : edit_cursor_offset_in_sector); update_status(edit_status_msg);
                }
                else { beep(); }
            }
            break;
        }

        int status_updated_by_action = 0;
        const char* current_msg_ptr_check = edit_status_msg;
        if (strstr(current_msg_ptr_check, "written.") != NULL || strstr(current_msg_ptr_check, "digit") != NULL ||
            strstr(current_msg_ptr_check, "Mode:") != NULL || strstr(current_msg_ptr_check, "Cancelled.") != NULL ||
            strstr(current_msg_ptr_check, "equivalent.") != NULL) {
            status_updated_by_action = 1;
        }

        if (ch != ERR && !status_updated_by_action) {
            if (pending_nibble_value != -1 && pending_nibble_offset == edit_cursor_offset_in_sector) {
                /* Message for pending nibble already set */
            }
            else {
                snprintf(edit_status_msg, sizeof(edit_status_msg), "EDIT | Arrows=Move F3=Mode Enter/ESC/F10=Exit | Type to modify"); update_status(edit_status_msg);
            }
        }
        if (ch != ERR || redraw_needed) {
            doupdate();
        }

    }

exit_edit_loop:
    curs_set(0);
    update_status(original_status_dynamic);
    draw_info_window();
    draw_data_window();
    doupdate();

    /* Free allocated memory */
    if (edit_buffer) {
        free(edit_buffer);
        edit_buffer = NULL;
    }
    if (original_sector_data_at_edit_start) {
        free(original_sector_data_at_edit_start);
        original_sector_data_at_edit_start = NULL;
    }
}


int main(int argc, char* argv[]) {
    char* input_filename = NULL;
    const char* base_filename_ptr = NULL;
    int imdf_res;

    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "/?") == 0) {
        fprintf(stderr, "ImageDisk Viewer (IMDF) %s [%s]\n", CMAKE_VERSION_STR, GIT_VERSION_STR);
        fprintf(stderr, "Copyright (C) 2025 - Howard M. Harte - https://github.com/hharte/imd-utils\n\n");
        fprintf(stderr, "Usage: %s <image.imd> [options]\n", get_basename(argv[0]));
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -I      : Ignore interleave (show physical sector order in nav)\n");
        fprintf(stderr, "  -W      : Enable writing (editing) - if image not RO\n");
        fprintf(stderr, "  -E      : Use EBCDIC display\n");
        fprintf(stderr, "  -X=xx   : Apply hex XOR mask xx to data view\n");
        fprintf(stderr, "  --help  : Show this help message\n");
        return 1;
    }
    input_filename = argv[1];
    base_filename_ptr = get_basename(input_filename);
    if (base_filename_ptr) {
        strncpy(current_filename_base, base_filename_ptr, MAX_FILENAME - 1);
        current_filename_base[MAX_FILENAME - 1] = 0;
    }
    else {
        strcpy(current_filename_base, "?.imd");
    }


    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-I") == 0) ignore_interleave = 1;
        else if (strcmp(argv[i], "-W") == 0) write_enabled = 1;
        else if (strcmp(argv[i], "-E") == 0) current_charset = CHARSET_EBCDIC;
        else if (strncmp(argv[i], "-X=", 3) == 0) {
            char* endptr;
            unsigned long val = strtoul(argv[i] + 3, &endptr, 16);
            if (*endptr == '\0' && val <= 0xFF) {
                xor_mask = (uint8_t)val;
            }
            else {
                fprintf(stderr, "Warning: Invalid hex value for -X= option: %s\n", argv[i]);
            }
        }
        else {
            fprintf(stderr, "Warning: Unknown option: %s\n", argv[i]);
        }
    }

    imdf_res = imdf_open(input_filename, (write_enabled == 0), &g_imdf_handle);
    if (imdf_res != IMDF_ERR_OK) {
        fprintf(stderr, "Error: Cannot open IMD file '%s' using libimdf (Error %d).\n", input_filename, imdf_res);
        return 1;
    }

    if (write_enabled) {
        int wp_stat = 1;
        imdf_get_write_protect(g_imdf_handle, &wp_stat);
        if (wp_stat) {
            fprintf(stderr, "Warning: Image is write-protected by libimdf.\n");
            write_enabled = 0;
        }
    }

    imdf_get_num_tracks(g_imdf_handle, &total_tracks_in_image);
    if (total_tracks_in_image == 0) {
        fprintf(stderr, "No tracks found in image '%s'.\n", input_filename);
        imdf_close(g_imdf_handle);
        return 1;
    }

    init_ui();
    clear_search_highlight();

    build_status_message();
    update_status(status_message);

    current_track_index_in_image = 0;
    current_sector_logical_idx = 0;
    current_data_offset_in_sector = 0;
    if (load_track_for_display(current_track_index_in_image) != 0) {
        cleanup_ui();
        fprintf(stderr, "Error loading initial track for display.\n");
        imdf_close(g_imdf_handle);
        return 1;
    }

    draw_info_window();
    draw_data_window();
    doupdate();

    while (1) {
        handle_input();
    }

    return 0;
}
