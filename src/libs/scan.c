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
#include <poll.h>

#include "subprocess-components.h"
#include "scan.h"

#include "delete-file.h"

#include "../wuming-window.h"
#include "../scanning-page.h"
#include "../threat-page.h"

#define CLAMSCAN_PATH "/usr/bin/clamscan"

typedef struct {
  /* Protected by mutex */
  GMutex mutex; // Only protect initialization, "completed", "success" and "cancellable" fields
  gboolean completed;
  gboolean success;
  GCancellable *cancellable;

  /* Protected by atomic operation */
  gint total_files; // Total files scanned
  gint total_threats; // Total threats found during scan

  GMutex threats_mutex; // Only protect "threat_paths" and "ThreatPage" fields
  GList *threat_paths; // List of threat paths found during scan
  ThreatPage *threat_page; // The threat page

  /*No need to protect these fields because they always same after initialize*/
  WumingWindow *window; // The main window
  ScanningPage *scanning_page; // The scanning page
  char *path; // file/folder path

  /* Protected by atomic operation */
  volatile gint ref_count;
} ScanContext;

static ScanContext*
scan_context_new(void)
{
  ScanContext *ctx = g_new0(ScanContext, 1);
  g_mutex_init(&ctx->mutex);
  g_mutex_init(&ctx->threats_mutex);

  ctx->cancellable = g_cancellable_new(); // Initialize the cancellable object
  ctx->ref_count = 1;
  return ctx;
}

static gpointer
scan_context_ref(gpointer ctx)
{
  ScanContext *context = (ScanContext*)ctx;
  g_return_val_if_fail(context != NULL, NULL);
  g_return_val_if_fail(g_atomic_int_get(&context->ref_count) > 0, NULL);
  if (context) g_atomic_int_inc(&context->ref_count);
  return context;
}

static void
scan_context_unref(gpointer ctx)
{
  ScanContext *context = (ScanContext*)ctx;
  if (context && g_atomic_int_dec_and_test(&context->ref_count))
  {
    scanning_page_revoke_close_signal(context->scanning_page); // Revoke the close signal
    scanning_page_revoke_cancel_signal(context->scanning_page); // Revoke the cancel signal

    if (context->cancellable) g_object_unref(context->cancellable);

    g_mutex_clear(&context->mutex);
    g_mutex_clear(&context->threats_mutex);

    g_clear_pointer(&context->path, g_free);

    g_atomic_int_set(&context->ref_count, INT_MIN);
    g_free(context);
  }
}

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

/*thread-safe method to get/set the cancellable object*/
static gboolean
get_cancel_scan(ScanContext *ctx)
{
  gboolean is_cancelled = FALSE;
  g_mutex_lock(&ctx->mutex);
  if (ctx->cancellable)
  {
    is_cancelled = g_cancellable_is_cancelled(ctx->cancellable);
  }
  g_mutex_unlock(&ctx->mutex);
  return is_cancelled;
}

static void
set_cancel_scan(ScanContext *ctx)
{
  g_mutex_lock(&ctx->mutex);
  if (ctx->cancellable && !g_cancellable_is_cancelled(ctx->cancellable))
  {
    g_cancellable_cancel(ctx->cancellable);
  }
  g_mutex_unlock(&ctx->mutex);
}

/* thread-safe method to inc/get total threats */
static void
inc_total_threats(ScanContext *ctx)
{
  g_atomic_int_inc(&ctx->total_threats);
}

static gint
get_total_threats(ScanContext *ctx)
{
  return g_atomic_int_get(&ctx->total_threats);
}

/* thread-safe method to inc/get total files */
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

/* Create a new `AdwActionRow` for the threat list view */
static GtkWidget*
create_threat_action_row(GtkWidget **delete_button, const char *path)
{
  GtkWidget *action_row = adw_action_row_new(); // Create the action row for the list view
  gtk_widget_add_css_class(action_row, "property"); // Add property syle class to the action row
  gtk_widget_set_size_request(action_row, -1, 60); // Set the size request for the action row
  adw_action_row_set_subtitle(ADW_ACTION_ROW(action_row), path);

  /* Delete button for the action row */
  *delete_button = gtk_button_new();
  gtk_widget_set_size_request(*delete_button, -1, 40);
  gtk_widget_set_halign(*delete_button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(*delete_button, GTK_ALIGN_CENTER);

  GtkWidget *content = adw_button_content_new(); // Create the button content
  adw_button_content_set_label(ADW_BUTTON_CONTENT(content), gettext("Delete"));
  adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(content), "delete-symbolic");

  gtk_button_set_child(GTK_BUTTON(*delete_button), content);
  
  adw_action_row_add_suffix(ADW_ACTION_ROW(action_row), *delete_button); // Add the delete button to the action row

  return action_row;
}

/* thread-safe method to add/clear a threat path to the list */
static void
add_threat_path(ScanContext *ctx, const char *path)
{
  g_return_if_fail(ctx);
  g_return_if_fail(path);

  g_mutex_lock(&ctx->threats_mutex);

  GtkWidget *delete_button = NULL;
  GtkWidget *action_row = create_threat_action_row(&delete_button, path); // Create the action row for the list view

  /* Add the action row to the list view */
  threat_page_add_threat (ctx->threat_page, action_row);
  GtkWidget *threat_list_box = threat_page_get_list_box(ctx->threat_page);

  /* Set file properties and connect signal */
  DeleteFileData *delete_data = delete_file_data_new(threat_list_box, action_row);
  if (!set_file_properties(delete_data)) // Set the file properties for the action row
  {
    /* If failed to set file properties, disable the AdwActionRow */
    g_critical("Failed to set file properties");
    delete_file_data_clear(delete_data); // Free the delete data
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(action_row), gettext("Failed to set file properties"));
    gtk_widget_set_sensitive(action_row, FALSE);

    g_mutex_unlock(&ctx->threats_mutex); // Release the lock before return
    return; // Otherwize, will cause double free when `clear_threat_paths` called.
  }

  // Add `DeleteFileData` to the GList
  // This shouldn't be freed by `delete_file_data_clear`, instead free it in `clear_threat_paths` using `g_list_free_full`
  // Otherwize, will cause double free when `clear_threat_paths` is called.
  ctx->threat_paths = g_list_prepend(ctx->threat_paths, delete_data);

  g_signal_connect_swapped(delete_button, "clicked", G_CALLBACK(delete_threat_file), delete_data); // Connect the delete button signal to the `delete_threat_file` function

  g_mutex_unlock(&ctx->threats_mutex);
}

/* clear the GList `data` field callback function */
static inline void
clear_list_elements_func(void *data)
{
  g_return_if_fail(data);
  DeleteFileData *delete_data = data; // Cast the data to `DeleteFileData` pointer
  delete_file_data_clear(delete_data);
}

/* clear all the threat paths in the list view and the list of threat paths */
static void
clear_threat_paths(ScanContext *ctx)
{
  g_debug("Clearing threat paths\n");

  g_return_if_fail(ctx);

  g_mutex_lock(&ctx->threats_mutex);
  threat_page_clear(ctx->threat_page);

  if (ctx->threat_paths)
  {
    g_list_free_full(g_steal_pointer(&ctx->threat_paths), (GDestroyNotify)clear_list_elements_func); // Free the threat paths list
    ctx->threat_paths = NULL;
  }

  g_mutex_unlock(&ctx->threats_mutex);
}

static void
resource_clean_up(gpointer user_data)
{
  IdleData *data = (IdleData *)user_data; // Cast the data to IdleData struct
  scan_context_unref (data->context);
  g_free(data->message);
  g_free(data);
}

/* The ui callback function for `process_output_lines()` */
static gboolean
scan_ui_callback(gpointer user_data)
{
  IdleData *data = (IdleData *)user_data;
  ScanContext *ctx = data->context;

  g_return_val_if_fail(data && ctx && g_atomic_int_get(&ctx->ref_count) > 0, 
                      G_SOURCE_REMOVE);

  char *status_marker = NULL; // Check file is OK or FOUND

  if ((status_marker = strstr(data->message, "FOUND")) != NULL)
  {
    inc_total_files(ctx);
    inc_total_threats(ctx);

    /* Add threat path to the list */
    char *colon = strchr(data->message, ':'); // Find the colon separator
    if (colon)
    {
      *colon = '\0'; // Replace the colon with null terminator
      add_threat_path(ctx, data->message);
    }
  }
  else if ((status_marker = strstr(data->message, "OK")) != NULL) inc_total_files(ctx);

  gint total_files = get_total_files(ctx);
  gint total_threats = get_total_threats(ctx);

  char *status_text = g_strdup_printf(gettext("%d files scanned\n%d threats found"), total_files, total_threats);
  
  scanning_page_set_progress(ctx->scanning_page, status_text);

  g_free(status_text);

  return G_SOURCE_REMOVE;
}

static gboolean
scan_complete_callback(gpointer user_data)
{
  IdleData *data = user_data;
  ScanContext *ctx = data->context;

  g_return_val_if_fail(data && ctx && g_atomic_int_get(&ctx->ref_count) > 0, 
                      G_SOURCE_REMOVE);

  gboolean is_success = FALSE;
  get_completion_state(ctx, NULL, &is_success); // Get the completion state for thread-safe access

  bool has_threat = (ctx->total_threats > 0);
  const char *is_canceled = get_cancel_scan(ctx) ? gettext("User cancelled the scan") : NULL;
  const char *icon_name = has_threat ? "status-warning-symbolic" : (is_success ? "status-ok-symbolic" : "status-error-symbolic");

  scanning_page_set_final_result(ctx->scanning_page, has_threat, data->message, is_canceled, icon_name);

  if (has_threat) // If threats found, push the page to the threat page
  {
    wuming_window_push_page_by_tag(ctx->window, "threat_nav_page");
  }

  return G_SOURCE_REMOVE;
}

static gpointer
scan_thread(gpointer data)
{
    ScanContext *ctx = data;
    int pipefd[2];
    pid_t pid;

    /* Initialize ring buffer */
    RingBuffer ring_buf;
    ring_buffer_init(&ring_buf);

    /*Spawn scan process*/
    if (!spawn_new_process(pipefd, &pid, 
        CLAMSCAN_PATH, "clamscan", ctx->path, "--recursive", NULL))
    {
        scan_context_unref(ctx);
        return NULL;
    }

    /*Initialize IO context*/
    IOContext io_ctx = {
        .pipefd = pipefd[0],
        .ring_buf = &ring_buf,
    };

    /*Start scan thread*/
    struct pollfd fds = { .fd = pipefd[0], .events = POLLIN };
    int idle_counter = 0;
    int dynamic_timeout = BASE_TIMEOUT_MS;
    int ready = 0; // Whether there is data to read from the pipe
    gboolean eof_received = FALSE;

    while (!eof_received)
    {
        if (get_cancel_scan(ctx)) // Check if the scan has been cancelled
        {
          g_warning("[INFO] User cancelled the scan");
          kill(pid, SIGTERM);
          set_completion_state(ctx, TRUE, FALSE);
          break;
        }

        int timeout = calculate_dynamic_timeout(&idle_counter, &dynamic_timeout, &ready);
        ready = poll(&fds, 1, timeout);

        if (ready > 0)
        {
          process_output_lines(&io_ctx, scan_context_ref, ctx, scan_ui_callback, resource_clean_up);

          if (!handle_io_event(&io_ctx))
          {
              eof_received = TRUE;
          }
        }
    }

    /*Clean up*/
    const gint exit_status = wait_for_process(pid);
    gboolean success = (exit_status == 0) || (exit_status == 1);
    set_completion_state(ctx, TRUE, success);

    const char *status_text = NULL;
    if (get_cancel_scan(ctx)) // User cancelled the scan
    {
      status_text = gettext("Scan Canceled");
    }
    else if (success) // Scan completed successfully
    {
        status_text = gettext("Scan Complete");
    }
    else // Scan failed
    {
        status_text = gettext("Scan Failed");
    }

    send_final_message(scan_context_ref, (void *)ctx, status_text, success, scan_complete_callback, resource_clean_up);

    close(pipefd[0]);
    return NULL;
}

/* Clear the list view and force close the dialog */
static void
clear_box_list_and_close(ScanContext *ctx)
{
  clear_threat_paths(ctx); // Clear the list view

  wuming_window_pop_page(ctx->window); // Pop the scanning page
  scan_context_unref(ctx); // unref at here to avoid fucked up the `ScanContext` before getting the `threat_list_box`
}

void
start_scan(WumingWindow *window, ScanningPage *scanning_page, ThreatPage *threat_page, const char *path)
{
  ScanContext *ctx = scan_context_new();

  g_mutex_lock(&ctx->mutex);
  ctx->completed = FALSE;
  ctx->success = FALSE;
  ctx->total_files = 0;
  ctx->total_threats = 0;
  ctx->threat_paths = NULL;
  ctx->window = window;
  ctx->scanning_page = scanning_page;
  ctx->threat_page = threat_page;
  ctx->path = path;
  ctx->ref_count = G_ATOMIC_REF_COUNT_INIT;
  g_mutex_unlock(&ctx->mutex);

  scanning_page_set_close_signal (scanning_page, (GCallback) clear_box_list_and_close, ctx);
  scanning_page_set_cancel_signal (scanning_page, (GCallback) set_cancel_scan, ctx);

  /* Start scan thread */
  g_thread_new("scan-thread", scan_thread, ctx);
}

