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
#include <clamav.h>
#include <fcntl.h>
#include <unistd.h>

#include "signature-status.h"

typedef struct signature_status {
    int year;
    int month;
    int day;
    int hour;
    int minute;
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

/* Change month format to number */
static int
month_str_to_num(const char *str)
{
    if (!str || strlen(str) != 3) return 0;
    
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


/* Use for comparing the databases date */
static int
date_to_days(int year, int month, int day)
{
    /* Check is valid date */
    if (year < 1) return 0;
    if (month < 1 || month > 12) return 0;
    if (day < 1 || day > 31) return 0;

    /* Turn years to days */
    int year_days = (year - 1) * 365 +
                    (year - 1) / 4 -
                    (year - 1) / 100 +
                    (year - 1) / 400;

    /* Turn months to days */
    const bool current_year_is_leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);

    static const int cumulative_days[] = { // Use cumulative days to calculate the dates of each month
        0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };

    int month_days = cumulative_days[month];
    if (current_year_is_leap_year && month > 2) month_days += 1; // If current month is greater than February and current year is leap year, add 1 to the month_days

    const int max_day = 
        (month == 2 && current_year_is_leap_year) ? 29 : 
        (cumulative_days[month+1] - cumulative_days[month]);

    if (day > max_day) return 0; // Check is valid day again

    return year_days + month_days + day;
}

static char *
parse_cvd_header_time(const char *filepath)
{
    char *result = NULL;
    struct cl_cvd *signature;

    if (access(filepath, R_OK) != 0)
    {
        g_warning("No access to %s: %s", filepath, strerror(errno));
        return NULL;
    }

    if ((signature = cl_cvdhead(filepath)) == NULL)
    {
        g_warning("Failed to read CVD header from %s", filepath);
        return NULL;
    }

    result = g_strdup(signature->time);

    cl_cvdfree(signature);
    signature = NULL;

    return result;
}

/*Check Database dir*/
static gboolean
check_database_dir(const char *database_dir)
{
    if (!database_dir)
    {
        g_warning ("%s is not vaild dictionary\n", database_dir);
        return FALSE;
    }

    if (strlen(database_dir) > (PATH_MAX - 50))
    {
        g_warning("Database path length exceeds system limit\n");
        return FALSE;
    }

  return TRUE;
}

/*Build database path*/
static char*
build_database_path(const char *database_dir, const char *filename)
{
    return g_strdup_printf("%s/%s", database_dir, filename);
}

/*Get database date*/
static gboolean
parse_database_file(database_file_params *params, const char *database_dir, const char *filename)
{
    char* filepath = build_database_path(database_dir, filename);
    params->date_string = parse_cvd_header_time(filepath);
    g_free(filepath);

    if (!params->date_string) return FALSE;

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
update_scan_result(signature_status *result, int cvd_days, int cld_days,
                   database_file_params *cvd, database_file_params *cld)
{
    g_return_if_fail(result != NULL && cvd != NULL && cld != NULL);
    if (result->status & SIGNATURE_STATUS_NOT_FOUND) return; // Signature not found, no need to choose

    const gboolean cvd_valid = (cvd_days > 0);
    const gboolean cld_valid = (cld_days > 0);

    if (!cvd_valid && !cld_valid)
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
    gboolean has_daily = FALSE;

    const char *database_dir = cl_retdbdir();
    if (!check_database_dir (database_dir)) return;

    /*First try daily.cvd*/
    database_file_params *cvd_date = g_new0(database_file_params, 1);
    has_daily = parse_database_file (cvd_date, database_dir, "daily.cvd");

    /*Try daily.cld*/
    database_file_params *cld_date = g_new0(database_file_params, 1);
    has_daily |= parse_database_file (cld_date, database_dir, "daily.cld");

    /*If no daily database found, try main.cvd*/
    if (!has_daily && !parse_database_file (cvd_date, database_dir, "main.cvd"))
    {
        result->status |= SIGNATURE_STATUS_NOT_FOUND;
        goto clean_up;
    }

    int cvd_days = date_to_days(cvd_date->year, cvd_date->month, cvd_date->day);
    int cld_days = date_to_days(cld_date->year, cld_date->month, cld_date->day);
    update_scan_result(result, cvd_days, cld_days, cvd_date, cld_date);

clean_up:
    g_free (cvd_date);
    g_free (cld_date);
}

/*Check whether the signature is up to date*/
static void
is_signature_uptodate(signature_status *result)
{
    g_return_if_fail(result != NULL);
    if (result->status & SIGNATURE_STATUS_NOT_FOUND) return; // Signature not found, no need to check

    /* Get Current Time */
    time_t t = time(NULL);
    struct tm current_time;
    gmtime_r(&t, &current_time);

    /* Get day diff */
    int day_diff = date_to_days(current_time.tm_year + 1900, current_time.tm_mon + 1, current_time.tm_mday) -
                    date_to_days(result->year, result->month, result->day);
    g_print("[INFO] Signature day diff: %d\n", day_diff);

    if (day_diff > 1) return; // Signature is not up to date
    result->status |= SIGNATURE_STATUS_UPTODATE;
}

signature_status *
signature_status_new(void)
{
    signature_status *status = g_new0(signature_status, 1);

    scan_signature_date(status);
    is_signature_uptodate(status);

    return status;
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