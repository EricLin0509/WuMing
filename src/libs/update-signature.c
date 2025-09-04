/* update-signature.c
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

#include <glib/gi18n.h>
#include <clamav.h>
#include <limits.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>

#include "subprocess-components.h"
#include "update-signature.h"

#include "../update-signature-page.h"

#define FRESHCLAM_PATH "/usr/bin/freshclam"
#define PKEXEC_PATH "/usr/bin/pkexec"

typedef struct {
    char *result_ref;
    int year;
    int month;
    int day;
    int time;
    char month_str[4];
} DatabaseFileParams;

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

static char*
parse_cvd_header(const char *filepath)
{
  const size_t cvd_header_size = 128; // Only read the first 128 bytes to get the header

  int discriptor = -1;
  char *file_mapped = MAP_FAILED;
  struct stat st;

  const char header_prefix[] = "ClamAV-VDB:";
  const size_t prefix_len = sizeof(header_prefix) - 1;
  char *result = NULL;

  if ((discriptor = open(filepath, O_RDONLY)) == -1)
    {
      g_warning("%s not found: %s\n", filepath, strerror(errno));
      return NULL;
    }

  if (fstat (discriptor, &st) == -1)
    {
      g_warning("Can't get the file size\n");
      goto clean_up;
    }

  /* Check file size */
  if (st.st_size < (off_t)(cvd_header_size + 512)) // 128 bytes header + 512 bytes sha256 sum
  {
     g_warning("%s is invalid signature, file size %ld < 640 bytes\n", filepath, (long)st.st_size);
     goto clean_up;
  }

  file_mapped = mmap (NULL, cvd_header_size, PROT_READ, MAP_PRIVATE, discriptor, 0);
  if (file_mapped == MAP_FAILED)
    {
      g_warning ("mmap failed: %s\n", strerror(errno));
      goto clean_up;
    }

  char *header_start = memmem(file_mapped, cvd_header_size, header_prefix, prefix_len);
  if (!header_start)
    {
      g_warning("CVD header not found\n");
      goto clean_up;
    }

  if ((header_start + prefix_len) > (file_mapped + cvd_header_size))
  {
    g_warning("Header prefix out of bounds\n");
    goto clean_up;
  }

  char *date_start = header_start + prefix_len;
  const char *buffer_end = file_mapped + cvd_header_size; // the Buffer end
  while (date_start < buffer_end && *date_start == ' ') date_start++;

  const size_t max_date_len = 20; // Maximum date length is 20 characters
  if (date_start + max_date_len > buffer_end) // Check if date is out of bounds
  {
    g_warning("Date field exceeds header boundary");
    goto clean_up;
  }


  char *date_end = memchr(date_start, '-',  buffer_end - date_start);
  if (!date_end || date_end >= buffer_end) // Check if date terminator is found
  {
    g_warning("Invalid date terminator");
    goto clean_up;
  }

  if (date_end) result = g_strndup(date_start, date_end - date_start);

clean_up:
  if (file_mapped != MAP_FAILED) munmap(file_mapped, cvd_header_size);
  if (discriptor != -1) close(discriptor);
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
parse_database_file(DatabaseFileParams *params, const char *database_dir, const char *filename)
{
    char* filepath = build_database_path(database_dir, filename);
    params->result_ref = parse_cvd_header(filepath);
    g_free(filepath);

    if (!params->result_ref) return FALSE;

    if (sscanf(params->result_ref, "%d %3s %d %d",
               &params->day, params->month_str, &params->year, &params->time) != 4)
    {
        g_warning("Failed to parse: '%s'", params->result_ref);
        g_warning("Invalid date format in %s", filename);
        g_free(params->result_ref);
        params->result_ref = NULL;
        return FALSE;
    }

    params->month = month_str_to_num(params->month_str);
    g_free(params->result_ref);
    params->result_ref = NULL;
    return TRUE;
}

/*Choose the latest date*/
static void
update_scan_result(scan_result *result, int cvd_days, int cld_days,
                   DatabaseFileParams *cvd, DatabaseFileParams *cld)
{
    const gboolean cvd_valid = (cvd_days > 0);
    const gboolean cld_valid = (cld_days > 0);

    if (!cvd_valid && !cld_valid)
    {
        result->is_success = FALSE;
        g_warning("No valid signature dates found");
        return;
    }

    DatabaseFileParams* latest = (cvd_days >= cld_days) ? cvd : cld;
    result->year   = latest->year;
    result->month  = latest->month;
    result->day    = latest->day;
    result->time   = latest->time;
    result->is_success = TRUE;
}

/*Scan the signature date*/
void
scan_signature_date(scan_result *result)
{
  gboolean has_daily = FALSE;

  const char *database_dir = cl_retdbdir();
  if (!check_database_dir (database_dir)) return;

  /*First try daily.cvd*/
  DatabaseFileParams *cvd_date = g_new0(DatabaseFileParams, 1);
  has_daily = parse_database_file (cvd_date, database_dir, "daily.cvd");

  /*Try daily.cld*/
  DatabaseFileParams *cld_date = g_new0(DatabaseFileParams, 1);
  has_daily |= parse_database_file (cld_date, database_dir, "daily.cld");

  /*If no daily database found, try main.cvd*/
  if (!has_daily)
    {
      result->is_warning = TRUE;
      if (!parse_database_file (cvd_date, database_dir, "main.cvd"))
        {
          result->is_success = FALSE;
          goto clean_up;
        }
    }

  int cvd_days = date_to_days(cvd_date->year, cvd_date->month, cvd_date->day);
  int cld_days = date_to_days(cld_date->year, cld_date->month, cld_date->day);
  update_scan_result(result, cvd_days, cld_days, cvd_date, cld_date);

clean_up:
  g_free (cvd_date);
  g_free (cld_date);
}

/*Check whether the signature is up to date*/
void
is_signature_uptodate(scan_result *result)
{
  /* Get Current Time */
  time_t t = time(NULL);
  struct tm current_time;
  gmtime_r(&t, &current_time);

  /* Get day diff */
  int day_diff = date_to_days(current_time.tm_year + 1900, current_time.tm_mon + 1, current_time.tm_mday) -
                 date_to_days(result->year, result->month, result->day);
  g_print("[INFO] day diff: %d\n", day_diff);

  if (day_diff > 1)
  {
    result->is_uptodate = false;
    return;
  }
  result->is_uptodate = true;
}

/*Update Signature*/
typedef struct {
  /* Protected by mutex */
  GMutex mutex; // Only protect "completed" and "success" fields
  gboolean completed;
  gboolean success;

  /*No need to protect these fields because they always same after initialize*/
  GtkWidget *main_page;
  AdwDialog *update_dialog;
  GtkWidget *update_status_page;
  GtkWidget *close_button;

  /* Protected by atomic ref count */
  volatile gint ref_count;
} UpdateContext;

typedef struct {
  UpdateContext *ctx;
  char *message;
} IdleData;

static UpdateContext*
update_context_new(void)
{
  UpdateContext *ctx = g_new0(UpdateContext, 1);
  g_mutex_init(&ctx->mutex);
  ctx->ref_count = 1;
  return ctx;
}

static UpdateContext*
update_context_ref(UpdateContext *ctx)
{
  g_return_val_if_fail(ctx != NULL, NULL);
  g_return_val_if_fail(g_atomic_int_get(&ctx->ref_count) > 0, NULL);
  if (ctx) g_atomic_int_inc(&ctx->ref_count);
  return ctx;
}

static void
update_context_unref(UpdateContext *ctx)
{
  if (ctx && g_atomic_int_dec_and_test(&ctx->ref_count))
  {
    g_mutex_clear(&ctx->mutex);
    g_atomic_int_set(&ctx->ref_count, INT_MIN);
    g_free(ctx);
  }
}


/* thread-safe method to get/set states */
static void
set_completion_state(UpdateContext *ctx, gboolean completed, gboolean success)
{
  g_mutex_lock(&ctx->mutex);
  ctx->completed = completed;
  ctx->success = success;
  g_mutex_unlock(&ctx->mutex);
}

static void
get_completion_state(UpdateContext *ctx, gboolean *out_completed, gboolean *out_success)
{
  g_mutex_lock(&ctx->mutex);
  /* If one of the output pointers is NULL, only return the another one */
  if (out_completed) *out_completed = ctx->completed;
  if (out_success) *out_success = ctx->success;
  g_mutex_unlock(&ctx->mutex);
}

static void
resource_clean_up(IdleData *data)
{
  update_context_unref (data->ctx);
  g_free(data->message);
  g_free(data);
}

static gboolean
update_ui_callback(gpointer user_data)
{
  IdleData *data = (IdleData *)user_data;

  g_return_val_if_fail(data && data->ctx && g_atomic_int_get(&data->ctx->ref_count) > 0, 
                      G_SOURCE_REMOVE);

  if (data->ctx && data->ctx->update_status_page && GTK_IS_WIDGET(data->ctx->update_status_page))
    adw_status_page_set_description(
      ADW_STATUS_PAGE(data->ctx->update_status_page),
      data->message
    );

  return G_SOURCE_REMOVE;
}

static gboolean
update_complete_callback(gpointer user_data)
{
  IdleData *data = user_data;

  g_return_val_if_fail(data && data->ctx && g_atomic_int_get(&data->ctx->ref_count) > 0, 
                      G_SOURCE_REMOVE);

  gboolean is_success = FALSE;
  get_completion_state(data->ctx, NULL, &is_success); // Get the completion state for thread-safe access

  if (data->ctx->update_status_page && GTK_IS_WIDGET(data->ctx->update_status_page))
  {
    adw_status_page_set_title(
      ADW_STATUS_PAGE(data->ctx->update_status_page),
      data->message
    );
  }

  if (data->ctx->close_button && GTK_IS_WIDGET(data->ctx->close_button))
  {
    gtk_widget_set_visible(data->ctx->close_button, TRUE);
    gtk_widget_set_sensitive(data->ctx->close_button, TRUE);
  }

  if (data->ctx->main_page && GTK_IS_WIDGET(data->ctx->main_page) && is_success) // Re-scan the signature if update is successful
  {
    /*Re-scan the signature*/
    scan_result *result = g_new0 (scan_result, 1);
    scan_signature_date (result);
    is_signature_uptodate (result);

    update_signature_page_show_date (UPDATE_SIGNATURE_PAGE (data->ctx->main_page), *result);
    update_signature_page_show_isuptodate(UPDATE_SIGNATURE_PAGE (data->ctx->main_page), result->is_uptodate);
    g_free (result);
  }

  return G_SOURCE_REMOVE;
}

static void
process_output_lines(RingBuffer *ring_buf, LineAccumulator *acc, UpdateContext *ctx)
{
    char *line;
    while (ring_buffer_read_line(ring_buf, acc, &line))
    {
        IdleData *data = g_new0(IdleData, 1);
        gchar *escaped = g_markup_escape_text(line, -1);
        data->message = escaped;
        data->ctx = update_context_ref(ctx);
        g_main_context_invoke_full(
                       g_main_context_default(),
                       G_PRIORITY_HIGH_IDLE,
                       (GSourceFunc) update_ui_callback,
                       data,
                       (GDestroyNotify)resource_clean_up);
    }
}

static void 
send_final_status(UpdateContext *ctx, gboolean success)
{
    set_completion_state(ctx, TRUE, success);

    const char *status_text = success ? 
        gettext("Update Complete") : gettext("Update Failed");
    
    /* Create final status message */
    IdleData *complete_data = g_new0(IdleData, 1);
    complete_data->ctx = update_context_ref(ctx);
    complete_data->message = g_strdup(status_text);
    
    /* Send final status message to main thread */
    if (G_LIKELY((complete_data->ctx || g_atomic_int_get(&complete_data->ctx->ref_count) > 0 )
                      && complete_data->ctx->update_status_page))
    {
        g_main_context_invoke_full(
                       g_main_context_default(),
                       G_PRIORITY_HIGH_IDLE,
                       (GSourceFunc) update_complete_callback,
                       complete_data,
                       (GDestroyNotify)resource_clean_up);
    }
    else
    {
        g_warning("Attempted to send status to invalid context");
        update_context_unref(complete_data->ctx);
        g_free(complete_data->message);
        g_free(complete_data);
    }
}

static gpointer
update_thread(gpointer data)
{
    UpdateContext *ctx = data;
    int pipefd[2];
    pid_t pid;
    
    /*Initialize ring buffer and line accumulator*/
    RingBuffer ring_buf;
    ring_buffer_init(&ring_buf);
    LineAccumulator acc;
    line_accumulator_init(&acc);
    
    /*Spawn update process*/
    if (!spawn_new_process(pipefd, &pid,
        PKEXEC_PATH, "pkexec", FRESHCLAM_PATH, "--verbose", NULL))
    {
        update_context_unref(ctx);
        return NULL;
    }

    /*Initialize IO context*/
    IOContext io_ctx = {
        .pipefd = pipefd[0],
        .ring_buf = &ring_buf,
        .acc = &acc,
    };

    /*Start update thread*/
    struct pollfd fds = { .fd = pipefd[0], .events = POLLIN };
    int idle_counter = 0;
    int dynamic_timeout = BASE_TIMEOUT_MS;
    gboolean eof_received = FALSE;

    while (!eof_received)
    {
        int timeout = calculate_dynamic_timeout(&idle_counter, &dynamic_timeout);
        int ready = poll(&fds, 1, timeout);
        
        if (ready > 0)
        {
            idle_counter = 0;
            dynamic_timeout = BASE_TIMEOUT_MS;
            
            if (!handle_io_event(&io_ctx))
            {
                eof_received = TRUE;
            }
        }
        
        process_output_lines(&ring_buf, &acc, ctx);
    }

    /*Clean up*/
    gboolean success = wait_for_process(pid, FALSE);
    send_final_status(ctx, success);
    
    close(pipefd[0]);
    update_context_unref(ctx);
    return NULL;
}

void
start_update(AdwDialog *dialog, GtkWidget *page, GtkWidget *close_button, GtkWidget *update_button)
{
  UpdateContext *ctx = update_context_new();

  g_mutex_lock(&ctx->mutex);
  ctx->completed = FALSE;
  ctx->success = FALSE;
  ctx->main_page =
          gtk_widget_get_ancestor (update_button, UPDATE_SIGNATURE_TYPE_PAGE); // Get the main page
  ctx->update_dialog = dialog;
  ctx->update_status_page = page;
  ctx->close_button = close_button;
  ctx->ref_count = G_ATOMIC_REF_COUNT_INIT;
  g_mutex_unlock(&ctx->mutex);

  /* Start update thread */
  g_thread_new("update-thread", update_thread, ctx);
}

