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
#include <sys/wait.h>
#include <limits.h>

#include "update-signature.h"

#define FRESHCLAM_PATH "/usr/bin/freshclam"
#define PKEXEC_PATH "/usr/bin/pkexec"

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
  FILE *discriptor = NULL;
  char header[256] = {0};

  char *date_start = NULL;
  char *date_end = NULL;
  char *result = NULL;

  discriptor = fopen(filepath, "rb");

  if (!discriptor)
  {
    g_warning("%s not found\n", filepath);
    return NULL;
  }

  /* Get the first line of the file */
  if (!fgets(header, sizeof(header), discriptor))
  {
    g_warning("failed to read the header\n");
    fclose(discriptor);
    return NULL;
  }
  fclose(discriptor);

  if (strlen(header) < 20 || !strstr(header, "ClamAV-VDB:"))
  {
    g_warning("Invalid CVD header format");
    return NULL;
  }

  if ((date_start = strstr(header, "ClamAV-VDB:")) != NULL)
  {
    date_start += strlen("ClamAV-VDB:"); // Skip prefix
    while (*date_start == ' ') date_start++; // Skip spaces
    date_end = strchr(date_start, '-');
    if (date_end)
    {
      *date_end = '\0'; // Cut
      result = calloc(strlen(date_start) + 1, sizeof(char)); // the return result should be store at heap
      memmove(result, date_start, strlen(date_start));
    }
  }

  return result;
}

/*Scan the signature date*/
void
scan_signature_date(scan_result *result)
{
  bool is_daily_data_exist = false;

  /* .cvd date data*/
  char *cvd_result = NULL;
  int cvd_year = 0;
  char cvd_month_str[4] = "Jan";
  int cvd_month = 0;
  int cvd_day = 0;
  int cvd_time = 0;

  /* .cld date data*/
  char *cld_result = NULL;
  int cld_year = 0;
  char cld_month_str[4] = "Jan";
  int cld_month = 0;
  int cld_day = 0;
  int cld_time = 0;

  char *filepath = NULL;
  int size = 0;
  const char *database_dir = cl_retdbdir();

  if (database_dir == NULL)
  {
    g_warning("The database dictionary not found!\n");
    return;
  }

  if (strlen(database_dir) > PATH_MAX - 50)
  {
    g_warning("Database path length exceeds system limit");
    return;
  }

  /* First try daily.cvd */
  size = strlen(database_dir) + strlen("/daily.cvd") + 1;

  filepath = calloc(size, sizeof(char));
  if (filepath == NULL)
  {
    g_warning("Calloc failed!\n");
    return;
  }

  snprintf(filepath, size, "%s/daily.cvd", database_dir);
  cvd_result = parse_cvd_header(filepath);

  free(filepath);
  filepath = NULL;

  if (cvd_result != NULL)
  {
    if ((sscanf(cvd_result, "%d %3s %d %d", &cvd_day, cvd_month_str, &cvd_year, &cvd_time)) != 4)
    {
      g_warning("Invalid date format\n");
      free(cvd_result);
      cvd_result = NULL;
      goto scan_cld;
    }
    else is_daily_data_exist = true;

    free(cvd_result);
    cvd_result = NULL;

    cvd_month = month_str_to_num(cvd_month_str);
  }

  /* Try daily.cld */
scan_cld:
  size = strlen(database_dir) + strlen("/daily.cld") + 1;

  filepath = calloc(size, sizeof(char));
  if (filepath == NULL)
  {
    g_warning("Calloc failed!\n");
    return;
  }

  snprintf(filepath, size, "%s/daily.cld", database_dir);
  cld_result = parse_cvd_header(filepath);

  free(filepath);
  filepath = NULL;

  if (cld_result != NULL)
  {
    if ((sscanf(cld_result, "%d %3s %d %d", &cld_day, cld_month_str, &cld_year, &cld_time)) != 4)
    {
      g_warning("Invalid date format\n");
      free(cld_result);
      cld_result = NULL;
      goto scan_main;
    }
    else is_daily_data_exist = true;

    free(cld_result);
    cld_result = NULL;
    cld_month = month_str_to_num(cld_month_str);
  }

  /* If no daily database, try main.cvd */
scan_main:
  if (!is_daily_data_exist)
  {
    result->is_warning = true; // set warning flag
    size = strlen(database_dir) + strlen("/main.cvd") + 1;

    filepath = calloc(size, sizeof(char));
    if (filepath == NULL)
    {
      g_warning("Calloc failed!\n");
      return;
    }

    snprintf(filepath, size, "%s/main.cvd", database_dir);
    cvd_result = parse_cvd_header(filepath);

    free(filepath);
    filepath = NULL;

    if (cvd_result == NULL)
    {
      result->is_success = false;
      return;
    }

    if ((sscanf(cvd_result, "%d %3s %d %d", &cvd_day, cvd_month_str, &cvd_year, &cvd_time)) != 4)
    {
      g_warning("Invalid date format\n");
      free(cvd_result);
      cvd_result = NULL;
      return;
    }

    free(cvd_result);
    cvd_result = NULL;
  }

  /* Compare result date */
  int cvd_days = date_to_days(cvd_year, cvd_month, cvd_day);
  int cld_days = date_to_days(cld_year, cld_month, cld_day);

  const bool cvd_valid = (cvd_days > 0);
  const bool cld_valid = (cld_days > 0);

  if (!cvd_valid && !cld_valid)
  {
    result->is_success = false;
    g_warning("No valid signature dates found");
    return;
  }
  else if (cvd_valid && cld_valid)
  {
    if (cvd_days >= cld_days)
    {
      result->year = cvd_year;
      result->month = cvd_month;
      result->day = cvd_day;
      result->time = cvd_time;
    }
    else
    {
      result->year = cld_year;
      result->month = cld_month;
      result->day = cld_day;
      result->time = cld_time;
    }
  }
  else
  {
    if (cvd_valid)
    {
      result->year = cvd_year;
      result->month = cvd_month;
      result->day = cvd_day;
      result->time = cvd_time;
    }
    else
    {
      result->year = cld_year;
      result->month = cld_month;
      result->day = cld_day;
      result->time = cld_time;
    }
  }

  result->is_success = true;
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

  g_free(data->message);
  g_free(data);
  return G_SOURCE_REMOVE;
}

static void
close_button_cb(GtkButton* self, gpointer dialog)
{
  adw_dialog_force_close(ADW_DIALOG(dialog));
}

static gpointer
update_thread(gpointer data)
{
  UpdateContext *ctx = update_context_ref(data);

  int pipefd[2];
  pid_t pid;

  if (pipe(pipefd) == -1)
  {
    perror("pipe");
    return NULL;
  }

  if ((pid = fork()) == -1)
  {
    perror("fork");
    return NULL;
  }

  if (pid == 0) // Child process
  {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);

    execl(PKEXEC_PATH, "pkexec", FRESHCLAM_PATH, "--verbose", NULL);
    exit(EXIT_FAILURE);
  }
  else // Parent process
  {
    close(pipefd[1]);
    char buffer[1024];
    FILE *stream = fdopen(pipefd[0], "r");

    while (fgets(buffer, sizeof(buffer), stream) != NULL)
    {
      g_async_queue_push(ctx->output_queue, g_strdup(buffer));
      IdleData *callback_data = g_new0(IdleData, 1);
      callback_data->ctx = update_context_ref(ctx);
      callback_data->message = g_strdup(buffer);
      g_idle_add(update_ui_callback, callback_data);
    }

    fclose(stream);
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
  g_signal_connect(GTK_BUTTON(close_button), "clicked", G_CALLBACK(close_button_cb), dialog);

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
