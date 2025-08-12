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
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/prctl.h>

#include "update-signature.h"

#define FRESHCLAM_PATH "/usr/bin/freshclam"
#define PKEXEC_PATH "/usr/bin/pkexec"

#define JITTER_RANGE 30
#define MAX_IDLE_COUNT 5 // If idle_counter greater than this, use MAX_TIMEOUT_MS
#define BASE_TIMEOUT_MS 50
#define MAX_TIMEOUT_MS 1000


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
month_str_to_num(char *str)
{
  if (str == NULL) return 0;
  if (strlen(str) != 3) return 0;

  if (strcmp(str, "Jan") == 0) return 1;
  if (strcmp(str, "Feb") == 0) return 2;
  if (strcmp(str, "Mar") == 0) return 3;
  if (strcmp(str, "Apr") == 0) return 4;
  if (strcmp(str, "May") == 0) return 5;
  if (strcmp(str, "Jun") == 0) return 6;
  if (strcmp(str, "Jul") == 0) return 7;
  if (strcmp(str, "Aug") == 0) return 8;
  if (strcmp(str, "Sep") == 0) return 9;
  if (strcmp(str, "Oct") == 0) return 10;
  if (strcmp(str, "Nov") == 0) return 11;
  if (strcmp(str, "Dec") == 0) return 12;
  return 0;
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
  bool current_year_is_leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);

  int month_day_list[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };

  if (current_year_is_leap_year) month_day_list[1] = 29;

  int month_days = 0;

  for (int i = 0; i < month - 1; i++) month_days += month_day_list[i];

  if (day > month_day_list[month - 1]) return 0; // check valid day again because of leap year

  return year_days + month_days + day;
}

static char*
parse_cvd_header(const char *filepath)
{
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

  file_mapped = mmap (NULL, st.st_size, PROT_READ, MAP_PRIVATE, discriptor, 0);
  if (file_mapped == MAP_FAILED)
    {
      g_warning ("mmap failed: %s\n", strerror(errno));
      goto clean_up;
    }

  char *header_start = memmem(file_mapped, st.st_size, header_prefix, prefix_len);
  if (!header_start)
    {
      g_warning("CVD header not found\n");
      goto clean_up;
    }

  char *date_start = header_start + prefix_len;
  while (*date_start == ' ' && date_start < (file_mapped + st.st_size)) date_start++;

  char *date_end = memchr(date_start, '-', file_mapped + st.st_size - date_start);

  if (date_end) result = g_strndup(date_start, date_end - date_start);

clean_up:
  if (file_mapped != MAP_FAILED) munmap(file_mapped, st.st_size);
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
  GAsyncQueue *output_queue;
  gboolean completed;
  gboolean success;
  AdwDialog *update_dialog;
  GtkWidget *update_status_page;
  GtkWidget *close_button;
  gint ref_count;
} UpdateContext;

typedef struct {
  UpdateContext *ctx;
  char *message;
} IdleData;

static UpdateContext*
update_context_ref(UpdateContext *ctx)
{
  if (ctx) ctx->ref_count++;
  return ctx;
}

static void
update_context_unref(UpdateContext *ctx)
{
  if (!ctx || --ctx->ref_count > 0) return;

  if (ctx->output_queue)
    g_async_queue_unref(ctx->output_queue);

  g_free(ctx);
}

static gboolean
update_ui_callback(gpointer user_data)
{
  IdleData *data = (IdleData *)user_data;

  if (data->ctx && data->ctx->update_status_page)
    adw_status_page_set_description(
      ADW_STATUS_PAGE(data->ctx->update_status_page),
      data->message
    );

  update_context_unref(data->ctx);
  g_free(data->message);
  g_free(data);
  return G_SOURCE_REMOVE;
}

static gboolean
update_complete_callback(gpointer user_data)
{
  IdleData *data = user_data;

  if (data->ctx->update_status_page)
  {
    adw_status_page_set_title(
      ADW_STATUS_PAGE(data->ctx->update_status_page),
      data->message
    );
  }

  if (data->ctx->close_button)
  {
    gtk_widget_set_visible(data->ctx->close_button, TRUE);
    gtk_widget_set_sensitive(data->ctx->close_button, TRUE);
  }

  update_context_unref(data->ctx);
  g_free(data->message);
  g_free(data);
  return G_SOURCE_REMOVE;
}

static gpointer
update_thread(gpointer data)
{
  UpdateContext *ctx = update_context_ref(data);

  int pipefd[2];
  pid_t pid;

  char buffer[4096];
  GString *line_buf = g_string_new(NULL);

  if (pipe(pipefd) == -1 || fcntl(pipefd[0], F_SETFL, O_NONBLOCK) == -1)
  {
    perror("pipe/fcntl");
    return NULL;
  }

  if ((pid = fork()) == -1)
  {
    perror("fork");
    return NULL;
  }

  if (pid == 0) // Child process
  {
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);

    execl(PKEXEC_PATH, "pkexec", FRESHCLAM_PATH, "--verbose", NULL);
    exit(EXIT_FAILURE);
  }
  else // Parent process
  {
    close(pipefd[1]);
    struct pollfd fds = { .fd = pipefd[0], .events = POLLIN };

    srand((unsigned)(g_get_monotonic_time() ^ getpid()));

    /*Dynamic timeout duration*/
    int idle_counter = 0;
    int dynamic_timeout = BASE_TIMEOUT_MS;

    while (true)
    {
      const int jitter = rand() % JITTER_RANGE; // use random perturbations
      const int timeout_ms = line_buf->len > 0 ? 0 : CLAMP(dynamic_timeout + jitter, BASE_TIMEOUT_MS, MAX_TIMEOUT_MS);

      int ready = poll(&fds, 1, timeout_ms);
      if (ready == -1)
      {
        if (errno == EINTR) continue;
        perror("poll");
        g_critical ("Poll operation failed\n");
        break;
      }

      if (ready == 0) // if is time out
      {
        if (++idle_counter > MAX_IDLE_COUNT)
        {
          dynamic_timeout = MIN(dynamic_timeout * 2, MAX_TIMEOUT_MS);
          idle_counter = 0;

          g_debug ("Set timeout to %dms\n", dynamic_timeout);
        }
        continue;
      }
      else
      {
        idle_counter = 0;
        dynamic_timeout = BASE_TIMEOUT_MS;
      }

      ssize_t n = read(pipefd[0], buffer, sizeof(buffer)-1);
      if (n < 0)
      {
        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
        perror("read");
        break;
      }
      if (n == 0) break; // EOF
      buffer[n] = '\0';
      g_string_append(line_buf, buffer);

      /*Process the output lines*/
      char *line_end;
      while ((line_end = strchr(line_buf->str, '\n')) != NULL)
      {
        *line_end = '\0';
        IdleData *callback_data = g_new0(IdleData, 1);
        callback_data->ctx = update_context_ref(ctx);
        callback_data->message = g_strdup(line_buf->str);
        g_idle_add(update_ui_callback, callback_data);
        g_string_erase(line_buf, 0, line_end - line_buf->str + 1);
      }
    }

    /*Process the rest data*/
    if (line_buf->len > 0)
    {
      IdleData *callback_data = g_new0(IdleData, 1);
      callback_data->ctx = update_context_ref(ctx);
      callback_data->message = g_strdup(line_buf->str);
      g_idle_add(update_ui_callback, callback_data);
    }
    g_string_free(line_buf, TRUE);

    int status;
    waitpid(pid, &status, 0);

    gboolean success = WIFEXITED(status) && (WEXITSTATUS(status) == 0);

    IdleData *complete_data = g_new0(IdleData, 1);
    complete_data->ctx = update_context_ref(ctx);
    complete_data->message = g_strdup(success ? gettext("Update Complete") : gettext("Update Failed"));
    g_idle_add(update_complete_callback, complete_data);
  }

  update_context_unref(ctx);
  return NULL;
}

void
start_update(AdwDialog *dialog, GtkWidget *page, GtkWidget *close_button, GtkWidget *update_button)
{
  g_signal_connect_swapped(GTK_BUTTON(close_button), "clicked", G_CALLBACK(adw_dialog_force_close), ADW_DIALOG (dialog));

  UpdateContext *ctx = g_new0(UpdateContext, 1);

  *ctx = (UpdateContext){
    .output_queue = g_async_queue_new(),
    .completed = FALSE,
    .success = FALSE,
    .update_dialog = dialog,
    .update_status_page = page,
    .close_button = close_button,
    .ref_count = 1,
  };

  g_thread_new("update-thread", update_thread, ctx);
}

