/* signature-status.c
 *
 * Copyright 2025 EricLin
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <glib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "date-to-days.h"
#include "signature-status.h"

#ifndef O_PATH // Compatibility for some Linux distributions
#define O_PATH 010000000
#endif

#define DIRECTORY_OPEN_FLAGS (O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
#define FILE_OPEN_FLAGS (O_RDONLY | O_NOFOLLOW | O_CLOEXEC)

#define CLAMAV_CVD_PATH "/var/lib/clamav"

#define HEADER_COLON_COUNT 8 // The header format is `ClamAV-VDB:time:version:sigs:fl:md5:dsig:builder:stime`, so there are 8 colons

typedef struct signature_status {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int signature_day_diff;
    int expiration_day;
    unsigned short status;
} signature_status;

typedef struct {
    char *date_string;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    char month_str[4];
} database_file_params;

/* Clear the file descriptor */
static inline void
clear_fd(int fd)
{
    if (fd == -1) return;
    close(fd);
    fd = -1;
}

/* Change month format to number */
static int
month_str_to_num(const char *str)
{
    g_return_val_if_fail(str != NULL && strlen(str) == 3, 0);
    
    /* Hash the first 3 characters of the month string to get a number */
    const unsigned int hash = 
        ((unsigned char)str[0] << 16) | // First character move 16 bits to the left
        ((unsigned char)str[1] << 8) | // Second character move 8 bits to the left
        (unsigned char)str[2]; // Third character
    
    /* Use the hash to get the month number */
    switch (hash)
    {
        case 0x4a616e: return 1; // "Jan"
        case 0x466562: return 2; // "Feb"
        case 0x4d6172: return 3; // "Mar"
        case 0x417072: return 4; // "Apr"
        case 0x4d6179: return 5; // "May"
        case 0x4a756e: return 6; // "Jun"
        case 0x4a756c: return 7; // "Jul"
        case 0x417567: return 8; // "Aug"
        case 0x536570: return 9; // "Sep"
        case 0x4f6374: return 10; // "Oct"
        case 0x4e6f76: return 11; // "Nov"
        case 0x446563: return 12; // "Dec"
        default: return 0;
    }
}

/* Parse the CVD header time */
/*
  * @note
  * Due to the header format is `ClamAV-VDB:time:version:sigs:fl:md5:dsig:builder:stime`, we can parse the header by using sliding window algorithm.
*/
static char *
parse_cvd_header_time(int dirfd, const char *filename)
{
    g_return_val_if_fail(dirfd != -1 && filename != NULL, NULL);

    char *result = NULL;

    int filefd = openat(dirfd, filename, FILE_OPEN_FLAGS);
    if (filefd == -1)
    {
        if (errno == ENOENT) g_warning("File not found: %s", filename); // File not found
        else g_critical("Failed to open %s: %s", filename, strerror(errno));
        return NULL;
    }

    struct stat file_stat;
    if (fstat(filefd, &file_stat) == -1) // Get the file stat
    {
        g_critical("Failed to stat %s: %s", filename, strerror(errno));
        clear_fd(filefd);
        return NULL;
    }

    if (!S_ISREG(file_stat.st_mode)) // Check whether it is a regular file
    {
        g_critical("Not a regular file: %s", filename);
        clear_fd(filefd);
        return NULL;
    }

    const size_t file_size = (size_t)file_stat.st_size;
    if (file_size < 48) // Check whether the file size is valid
    {
        g_critical("Invalid file size: %s, size: %zu", filename, file_size);
        clear_fd(filefd);
        return NULL;
    }

    char *header = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, filefd, 0);
    if (header == MAP_FAILED)
    {
        g_critical("Failed to mmap %s: %s", filename, strerror(errno));
        clear_fd(filefd);
        return NULL;
    }
    clear_fd(filefd);

    /* Parse the header */
    int colons_pos[HEADER_COLON_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1}; // Position of the colons in the header
    int colon_count = 0;

    for (int i = 0; i < file_size && colon_count < HEADER_COLON_COUNT; i++)
    {
        if (header[i] == ':') colons_pos[colon_count++] = i; // Find the colons in the header
    }

    if (colon_count < HEADER_COLON_COUNT)
    {
        g_critical("Invalid header: %s", filename);
        munmap(header, file_size);
        return NULL;
    }

    /* Only take the first two colons as the time string */
    char *str_start = header + colons_pos[0] + 1;
    int size = colons_pos[1] - colons_pos[0] - 1;

    result = g_strndup(str_start, size);

    munmap(header, file_size);
    header = NULL;

    return result;
}

/*Get database date*/
static gboolean
parse_database_file(database_file_params *params, int dirfd, const char *filename)
{
    g_return_val_if_fail(params != NULL && dirfd != -1 && filename != NULL, FALSE);

    params->date_string = parse_cvd_header_time(dirfd, filename);
    if (params->date_string == NULL) return FALSE;

    if (sscanf(params->date_string, "%d %3s %d %d-%d",
               &params->day, params->month_str, &params->year, &params->hour, &params->minute) != 5)
    {
        g_warning("Failed to parse: '%s'", params->date_string);
        g_warning("Invalid date format in %s", filename);
        g_free(params->date_string);
        params->date_string = NULL;
        return FALSE;
    }

    params->month = month_str_to_num(params->month_str);
    g_free(params->date_string);
    params->date_string = NULL;
    return TRUE;
}

/*Choose the latest date*/
static void
update_scan_result(signature_status *result, database_file_params *cvd, database_file_params *cld)
{
    g_return_if_fail(result != NULL && cvd != NULL && cld != NULL);
    if (result->status & SIGNATURE_STATUS_NOT_FOUND) return; // Signature not found, no need to choose

    int cvd_days = date_to_days(cvd->year, cvd->month, cvd->day);
    int cld_days = date_to_days(cld->year, cld->month, cld->day);

    if (!(cvd_days > 0 || cld_days > 0))
    {
        result->status |= SIGNATURE_STATUS_NOT_FOUND;
        g_warning("No valid signature dates found");
        return;
    }

    database_file_params* latest = (cvd_days >= cld_days) ? cvd : cld;
    result->year = latest->year;
    result->month = latest->month;
    result->day = latest->day;
    result->hour = latest->hour;
    result->minute = latest->minute;
}

/*Scan the signature date*/
static void
scan_signature_date(signature_status *result)
{
    g_return_if_fail(result != NULL);

    result->status &= ~SIGNATURE_STATUS_NOT_FOUND; // Reset the status

    gboolean has_daily = FALSE;

    const char *database_dir = CLAMAV_CVD_PATH;
    int dirfd = open(database_dir, DIRECTORY_OPEN_FLAGS);
    if (dirfd == -1)
    {
        g_critical("Failed to open %s: %s", database_dir, strerror(errno));
        result->status |= SIGNATURE_STATUS_NOT_FOUND;
        return;
    }

    /*First try daily.cvd*/
    database_file_params *cvd_date = g_new0(database_file_params, 1);
    has_daily = parse_database_file (cvd_date, dirfd, "daily.cvd");

    /*Try daily.cld*/
    database_file_params *cld_date = g_new0(database_file_params, 1);
    has_daily |= parse_database_file (cld_date, dirfd, "daily.cld");

    /*If no daily database found, try main.cvd*/
    if (!has_daily && !parse_database_file (cvd_date, dirfd, "main.cvd"))
    {
        clear_fd(dirfd);
        g_free (cvd_date);
        g_free (cld_date);
        result->status |= SIGNATURE_STATUS_NOT_FOUND;
        return;
    }
    clear_fd(dirfd);

    update_scan_result(result, cvd_date, cld_date);

    g_free (cvd_date);
    g_free (cld_date);
}

/*Check whether the signature is up to date*/
static void
is_signature_uptodate(signature_status *result, gboolean need_calculate_day_diff)
{
    g_return_if_fail(result != NULL);
    if (result->status & SIGNATURE_STATUS_NOT_FOUND) return; // Signature not found, no need to check

    result->status &= ~SIGNATURE_STATUS_UPTODATE; // Reset the status

    if (need_calculate_day_diff)
    {
        /* Get Current Time */
        time_t t = time(NULL);
        struct tm current_time;
        gmtime_r(&t, &current_time);

        /* Get day diff */
        result->signature_day_diff = date_to_days(current_time.tm_year + 1900, current_time.tm_mon + 1, current_time.tm_mday) -
                    date_to_days(result->year, result->month, result->day);
        g_print("[INFO] Signature day diff: %d\n", result->signature_day_diff);
    }

    if (result->signature_day_diff > result->expiration_day) return; // Signature is not up to date

    result->status |= SIGNATURE_STATUS_UPTODATE;
}

signature_status *
signature_status_new(gint signature_expiration_time)
{
    signature_status *status = g_new0(signature_status, 1);

    status->expiration_day = signature_expiration_time > 0 ? signature_expiration_time : 5; // Default expiration time is 5 days

    gint temp_status = 0;

    scan_signature_date(status);
    is_signature_uptodate(status, TRUE);

    return status;
}

/* Update the signature status */
/*
  * @param status
  * The signature status object.
  * 
  * @param need_rescan_database
  * Whether the database needs to be rescanned.
  * 
  * @param signature_expiration_time
  * The expiration time of the signature.
  * 
  * @warning
  * If `signature_expiration_time` is less than or equal to 0, this argument will be ignored.
*/
void
signature_status_update(signature_status *status, gboolean need_rescan_database, gint signature_expiration_time)
{
    g_return_if_fail(status != NULL);

    gint temp_status = 0;

    if (signature_expiration_time > 0) status->expiration_day = signature_expiration_time;

    if (need_rescan_database)
    {
        scan_signature_date(status);
    }

    is_signature_uptodate(status, need_rescan_database);
}

void
signature_status_clear(signature_status **status)
{
    g_return_if_fail(status != NULL && *status != NULL);
    g_free(*status);

    *status = NULL;
}

/* Get the status of the signature */
/*
  * @param status
  * The signature status object.
  * 
  * @return
  * The status of the signature.
*/
unsigned short
signature_status_get_status(const signature_status *status)
{
    g_return_val_if_fail(status != NULL, 0);

    return status->status;
}

/* Get the date of the signature */
/*
  * @param status
  * The signature status object.
  * 
  * @param year (out)
  * The year of the signature.
  * 
  * @param month (out)
  * The month of the signature.
  * 
  * @param day (out)
  * The day of the signature.
  * 
  * @param hour (out)
  * The hour of the signature.
  * 
  * @param minute (out)
  * The minute of the signature.
*/
void
signature_status_get_date(const signature_status *status, int *year, int *month, int *day, int *hour, int *minute)
{
    *year = status->year;
    *month = status->month;
    *day = status->day;
    *hour = status->hour;
    *minute = status->minute;
}