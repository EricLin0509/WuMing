/* scan.c
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
#include <limits.h>
#include <fcntl.h>

#include "subprocess-components.h"
#include "scan-options-configs.h"
#include "scan.h"

#include "delete-file.h"

#define CLAMSCAN_PATH "/usr/bin/clamscan"

typedef struct ScanContext {
  /* Protected by mutex */
  GMutex mutex; // Only protect initialization, "completed", "success" fields
  gboolean completed;
  gboolean success;

  int pipefd[2];
  pid_t pid;
  RingBuffer ring_buffer; // Ring buffer to store the output of the scan process

  /* Protected by atomic operation */
  gboolean should_cancel; // Whether the scan should be cancelled
  gint total_files; // Total files scanned
  gint total_threats; // Total threats found during scan

  GMutex threats_mutex; // Only protect "ThreatPage" fields
  ThreatPage *threat_page; // The threat page

  /*No need to protect these fields because they always same after initialize*/
  WumingWindow *window; // The main window
  SecurityOverviewPage *security_overview_page; // The security overview page
  ScanPage *scan_page; // The scan page
  ScanningPage *scanning_page; // The scanning page
  char *path; // file/folder path

} ScanContext;

/* thread-safe method to get/set states */
static void
set_completion_state(ScanContext *ctx, gboolean completed, gboolean success)
{
  g_mutex_lock(&ctx->mutex);
  ctx->completed = completed;
  ctx->success = success;
  g_mutex_unlock(&ctx->mutex);
}

static void
get_completion_state(ScanContext *ctx, gboolean *out_completed, gboolean *out_success)
{
  g_mutex_lock(&ctx->mutex);
  /* If one of the output pointers is NULL, only return the another one */
  if (out_completed) *out_completed = ctx->completed;
  if (out_success) *out_success = ctx->success;
  g_mutex_unlock(&ctx->mutex);
}

/*thread-safe method to get/set/reset the cancellable object*/
static gboolean
get_cancel_scan(ScanContext *ctx)
{
  g_return_val_if_fail(ctx, FALSE);

  return g_atomic_int_get(&ctx->should_cancel);
}

static void
set_cancel_scan(ScanContext *ctx)
{
  g_return_if_fail(ctx);

  g_atomic_int_set(&ctx->should_cancel, TRUE);
}

static void
reset_cancel_scan(ScanContext *ctx)
{
  g_return_if_fail(ctx);

  g_atomic_int_set(&ctx->should_cancel, FALSE);
}

/* thread-safe method to inc/get/reset total threats */
static void
inc_total_threats(ScanContext *ctx)
{
  g_atomic_int_inc(&ctx->total_threats);
}

static void
reset_total_threats(ScanContext *ctx)
{
  g_atomic_int_set(&ctx->total_threats, 0);
}

static gint
get_total_threats(ScanContext *ctx)
{
  return g_atomic_int_get(&ctx->total_threats);
}

/* thread-safe method to inc/get/reset total files */
static void
inc_total_files(ScanContext *ctx)
{
  g_atomic_int_inc(&ctx->total_files);
}

static gint
get_total_files(ScanContext *ctx)
{
  return g_atomic_int_get(&ctx->total_files);
}

static void
reset_total_files(ScanContext *ctx)
{
  g_atomic_int_set(&ctx->total_files, 0);
}

/* Create a new `AdwActionRow` for the threat list view */
static GtkWidget*
create_threat_expander_row(GtkWidget **delete_button, const char *path, const char *virname)
{
  GtkWidget *expander_row = adw_expander_row_new(); // Create the action row for the list view
  gtk_widget_add_css_class(expander_row, "property"); // Add property syle class to the action row
  adw_expander_row_set_subtitle(ADW_EXPANDER_ROW(expander_row), path);

  /* Delete button for the action row */
  *delete_button = gtk_button_new();
  gtk_widget_set_size_request(*delete_button, -1, 40);
  gtk_widget_add_css_class(*delete_button, "button-default");
  gtk_widget_set_halign(*delete_button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(*delete_button, GTK_ALIGN_CENTER);

  GtkWidget *content = adw_button_content_new(); // Create the button content
  adw_button_content_set_label(ADW_BUTTON_CONTENT(content), gettext("Delete"));
  adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(content), "delete-symbolic");

  gtk_button_set_child(GTK_BUTTON(*delete_button), content);
  
  adw_expander_row_add_suffix(ADW_EXPANDER_ROW(expander_row), *delete_button); // Add the delete button to the action row

  GtkWidget *vir_row = adw_action_row_new(); // Create the virname row
  gtk_widget_add_css_class(vir_row, "property"); // Add property style class to the virname row

  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(vir_row), gettext("Threat Characteristics"));
  adw_action_row_set_subtitle(ADW_ACTION_ROW(vir_row), virname);

  adw_expander_row_add_row(ADW_EXPANDER_ROW(expander_row), vir_row); // Add the virname row to the action row

  return expander_row;
}

/* thread-safe method to add/clear a threat path to the list */
static void
add_threat_path(ScanContext *ctx, const char *path, const char *virname)
{
  g_return_if_fail(ctx);
  g_return_if_fail(path);

  g_mutex_lock(&ctx->threats_mutex);

  GtkWidget *delete_button = NULL;
  GtkWidget *expander_row = create_threat_expander_row(&delete_button, path, virname); // Create the action row for the list view

  /* Add the action row to the list view */
  threat_page_add_threat (ctx->threat_page, expander_row);

  /* Add the threat path to the list */
  DeleteFileData *delete_data = delete_file_data_table_insert(GTK_WIDGET(ctx->threat_page), path, expander_row); // Add the delete data to the list
  if (delete_data == NULL) // Failed to add delete data to list
  {
    g_critical("Failed to add delete data to list");
    g_mutex_unlock(&ctx->threats_mutex);
    return;
  }

  g_signal_connect_swapped(delete_button, "clicked", G_CALLBACK(delete_threat_file), delete_data); // Connect the delete button signal to the `delete_threat_file` function

  g_mutex_unlock(&ctx->threats_mutex);
}

static char *
get_status_text(ScanContext *ctx)
{
  g_return_val_if_fail(ctx, NULL);

  gint total_files = get_total_files(ctx);
  gint total_threats = get_total_threats(ctx);

  char *status_text = g_strdup_printf(gettext("%d files scanned\n%d threats found"), total_files, total_threats);

  return g_steal_pointer(&status_text);
}

/* The ui callback function for `process_output_lines()` */
static gboolean
scan_ui_callback(gpointer user_data)
{
  IdleData *data = (IdleData *)user_data;
  ScanContext *ctx = (ScanContext *)get_idle_context(data);

  g_return_val_if_fail(data && ctx, G_SOURCE_REMOVE);

  const char *message = get_idle_message(data); // Get the message from the ring buffer
  char *status_marker = NULL; // Check file is OK or FOUND

  if ((status_marker = strstr(message, "FOUND")) != NULL)
  {
    inc_total_files(ctx);
    inc_total_threats(ctx);

    /* Add threat path to the list */
    char *colon = strchr(message, ':'); // Find the colon separator
    if (colon)
    {
      *colon = '\0'; // Replace the colon with null terminator

      char *last_space = status_marker - 1; // Find the last space before FOUND
      *last_space = '\0'; // Replace the last space with null terminator
      char *virname = colon + 2 >= last_space ? NULL : colon + 2; // Get the virname from the message

      add_threat_path(ctx, message, virname);
    }
  }
  else if ((status_marker = strstr(message, "OK")) != NULL) inc_total_files(ctx);

  char *status_text = get_status_text(ctx);

  scanning_page_set_progress(ctx->scanning_page, status_text);

  g_clear_pointer(&status_text, g_free);

  return G_SOURCE_REMOVE;
}

static gboolean
scan_complete_callback(gpointer user_data)
{
  IdleData *data = user_data;
  ScanContext *ctx = (ScanContext *)get_idle_context(data);

  g_return_val_if_fail(data && ctx, G_SOURCE_REMOVE);

  gboolean is_success = FALSE;
  get_completion_state(ctx, NULL, &is_success); // Get the completion state for thread-safe access

  bool has_threat = (ctx->total_threats > 0);

  char *status_text = get_status_text(ctx);

  const char *icon_name = has_threat ? "status-warning-symbolic" : (is_success ? "status-ok-symbolic" : "status-error-symbolic");
  const char *message = get_idle_message(data); // Get the message

  scanning_page_set_final_result(ctx->scanning_page, has_threat, message, status_text, icon_name);

  if (!is_success)
  {
    int exit_status = get_idle_exit_status(data);
    g_autofree char *error_message = g_strdup_printf(gettext("Scan failed with exit status %d"), exit_status);
    wuming_window_send_toast_notification(ctx->window, error_message, 10);
  }

  if (has_threat) // If threats found, push the page to the threat page
  {
    wuming_window_push_page_by_tag(ctx->window, "threat_nav_page");
  }

  if (!wuming_window_is_hide(ctx->window))
  {
    wuming_window_send_notification(ctx->window, G_NOTIFICATION_PRIORITY_URGENT, message, status_text); // Send notification if the window is not active
  }

  g_clear_pointer(&status_text, g_free);

  wuming_window_set_hide_on_close(ctx->window, FALSE, NULL); // Allow the window to be closed when the scan is complete

  return G_SOURCE_REMOVE;
}

static void
get_extra_args(char *extra_args[SCAN_OPTIONS_N_ELEMENTS])
{
  const char *args_list[SCAN_OPTIONS_N_ELEMENTS] = { "--max-filesize=2048M", "--detect-pua=yes", "--scan-archive=yes", "--scan-mail=yes", "--alert-exceeds-max=yes", "--alert-encrypted=yes" };
  
  GSettings *settings = g_settings_new("com.ericlin.wuming");
  int bitmask = g_settings_get_int(settings, "scan-options-bitmask");
  g_object_unref(settings);

  int i = 0;
  int j = 0;
  int options_bit = 1;

  while (options_bit <= bitmask && i < SCAN_OPTIONS_N_ELEMENTS)
  {
    if (bitmask & options_bit) extra_args[j++] = g_strdup(args_list[i]);

    i++;
    options_bit <<= 1; // Move to the next option bit
  }
}

static void
extra_args_free(char *extra_args[SCAN_OPTIONS_N_ELEMENTS])
{
  for (int i = 0; i < SCAN_OPTIONS_N_ELEMENTS; i++)
  {
    g_clear_pointer(&extra_args[i], g_free);
  }
}

static gboolean
scan_sync_callback(gpointer user_data)
{
  ScanContext *ctx = user_data;

  if (get_cancel_scan(ctx)) // Check if the scan has been cancelled
  {
      g_warning("[INFO] User cancelled the scan");
      kill(ctx->pid, SIGTERM);
      wait_for_process(ctx->pid, 0); // Update the exit status
      send_final_message((void *)ctx, gettext("Scan Canceled"), FALSE, SIGTERM, scan_complete_callback);
      return G_SOURCE_REMOVE;
  }

  if (handle_input_event(&ctx->ring_buffer, ctx->pipefd[0]))
    process_output_lines(&ctx->ring_buffer, ctx, scan_ui_callback);

  const int exit_status = wait_for_process(ctx->pid, WNOHANG);

  if (exit_status == -1) return G_SOURCE_CONTINUE; // The process is still running

  gboolean success = (exit_status == 0) || (exit_status == 1);
  set_completion_state(ctx, TRUE, success);

  const char *status_text = success ? gettext("Scan Complete") : gettext("Scan Failed");

  send_final_message((void *)ctx, status_text, success, exit_status, scan_complete_callback);

  close(ctx->pipefd[0]);
  close(ctx->pipefd[1]);

  return G_SOURCE_REMOVE;
}

static void
start_scan_async(ScanContext *ctx)
{
    char *extra_args[SCAN_OPTIONS_N_ELEMENTS] = { NULL };
    get_extra_args(extra_args);

    /* Spawn scan process */
    if (!spawn_new_process(ctx->pipefd, &ctx->pid,
        CLAMSCAN_PATH, "clamscan", ctx->path, "--recursive",extra_args[0], extra_args[1], extra_args[2], extra_args[3], extra_args[4], extra_args[5], NULL))
    {
          g_critical("Failed to spawn clamscan process");
          extra_args_free(extra_args);
          send_final_message((void *)ctx, gettext("Scan Failed"), FALSE, -1, scan_complete_callback);
          return;
    }
    extra_args_free(extra_args);

    ring_buffer_init(&ctx->ring_buffer);

    /* Use Async I/O to check the progress of the scan */
    GSource *source = g_timeout_source_new(BASE_TIMEOUT_MS);
    g_source_set_callback(source, (GSourceFunc) scan_sync_callback, ctx, NULL);
    g_source_attach(source, g_main_context_default());
}

/* Clear the list view and force close the dialog */
static void
clear_box_list_and_close(ScanContext *ctx)
{
  delete_file_data_table_clear(); // Clear the list of delete data

  wuming_window_pop_page(ctx->window); // Pop the scanning page
}

static void
scan_context_clear_path(ScanContext *ctx)
{
  g_return_if_fail(ctx);

  g_clear_pointer(&ctx->path, g_free);
}

static void
scan_context_add_path(ScanContext *ctx, const char *path)
{
  g_return_if_fail(ctx && path);

  if (ctx->path) scan_context_clear_path(ctx); // If have a path, clear it first

  ctx->path = (char *)g_steal_pointer(&path); // Add the new path to the context
}

ScanContext *
scan_context_new(WumingWindow *window, SecurityOverviewPage *security_overview_page, ScanPage *scan_page, ScanningPage *scanning_page, ThreatPage *threat_page)
{
  g_return_val_if_fail(window && scanning_page && threat_page, NULL);

  ScanContext *ctx = g_new0(ScanContext, 1);
  g_mutex_init(&ctx->mutex);
  g_mutex_init(&ctx->threats_mutex);

  ctx->completed = FALSE;
  ctx->success = FALSE;
  ctx->total_files = 0;
  ctx->total_threats = 0;
  ctx->window = window;
  ctx->security_overview_page = security_overview_page;
  ctx->scan_page = scan_page;
  ctx->scanning_page = scanning_page;
  ctx->threat_page = threat_page;
  ctx->path = NULL;

  ctx->should_cancel = FALSE;

  /* Bind the signal */
  scanning_page_set_close_signal(scanning_page, (GCallback) clear_box_list_and_close, ctx);
  scanning_page_set_cancel_signal(scanning_page, (GCallback) set_cancel_scan, ctx);

  return ctx;
}

/* Clear `ScanContext` */
/*
  * @warning
  * This function won't free the widget pointers in `ScanContext`
  * They should be freed by the `dispose` function
*/
void
scan_context_clear(ScanContext **ctx)
{
  g_return_if_fail(ctx && *ctx);

  /* Revoke the signal */
  scanning_page_revoke_close_signal((*ctx)->scanning_page);
  scanning_page_revoke_cancel_signal((*ctx)->scanning_page);

  g_mutex_clear(&(*ctx)->mutex);
  g_mutex_clear(&(*ctx)->threats_mutex);

  if ((*ctx)->path) scan_context_clear_path(*ctx); // Clear the path if have one

  g_clear_pointer(ctx, g_free);
}

static void
scan_context_reset(ScanContext *ctx)
{
  g_return_if_fail(ctx);

  /* Reset `ScanContext` */
  reset_cancel_scan(ctx); // Reset the cancel scan flag
  reset_total_files(ctx); // Reset the total files
  reset_total_threats(ctx); // Reset the total threats
  set_completion_state(ctx, FALSE, FALSE); // Reset the completion state

  /* Reset Widgets */
  scanning_page_reset(ctx->scanning_page);
  threat_page_clear(ctx->threat_page);

  g_autofree gchar *timestamp = save_last_scan_time(NULL, TRUE);
  scan_page_show_last_scan_time(ctx->scan_page, NULL, timestamp);
  scan_page_show_last_scan_time_status(ctx->scan_page, NULL, FALSE);
  security_overview_page_show_last_scan_time_status(ctx->security_overview_page, NULL, FALSE);
  security_overview_page_show_health_level(ctx->security_overview_page);
}

void
start_scan(ScanContext *ctx, const char *path)
{
  g_return_if_fail(ctx && path);

  scan_context_add_path(ctx, path);

  /* Reset `ScanContext` */
  scan_context_reset(ctx);

  wuming_window_push_page_by_tag(ctx->window, "scanning_nav_page");
  wuming_window_set_hide_on_close(ctx->window, TRUE, gettext("Scanning...")); // Hide the window instead of closing it

  start_scan_async(ctx);
}

