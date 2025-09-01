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

#include "thread-components.h"
#include "scan.h"

#include "delete-file.h"

#include "../scan-page.h"

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

  GMutex threats_mutex; // Only protect "threat_paths" and "threat_list" fields
  GList *threat_paths; // List of threats found during scan
  GtkWidget *threat_list_box; // List view of threats found after scan

  /*No need to protect these fields because they always same after initialize*/
  AdwDialog *scan_dialog;
  gulong scan_dialog_handler_id;
  GtkWidget *navigation_view; // use for pushing the `threat_navigation_page` to the navigation view
  GtkWidget *scan_status_page;
  GtkWidget *close_button;
  gulong close_button_handler_id;
  AdwNavigationPage *threat_navigation_page; // use for pushing it to the navigation view
  GtkWidget *threat_status_page; // use for adding the `threat_list_box` to the status page
  GtkWidget *threat_button;
  AdwNavigationPage *cancel_navigation_page; // use for canceling the scan
  GtkWidget *cancel_button;
  gulong cancel_button_handler_id;
  char *path; // file/folder path

  /* Protected by atomic operation */
  volatile gint ref_count;
} ScanContext;

typedef struct {
  ScanContext *ctx;
  char *message;
} IdleData;

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

static ScanContext*
scan_context_ref(ScanContext *ctx)
{
  g_return_val_if_fail(ctx != NULL, NULL);
  g_return_val_if_fail(g_atomic_int_get(&ctx->ref_count) > 0, NULL);
  if (ctx) g_atomic_int_inc(&ctx->ref_count);
  return ctx;
}

static void
scan_context_unref(ScanContext *ctx)
{
  if (ctx && g_atomic_int_dec_and_test(&ctx->ref_count))
  {
    if (ctx->cancellable) g_object_unref(ctx->cancellable);

    g_mutex_clear(&ctx->mutex);
    g_mutex_clear(&ctx->threats_mutex);

    g_clear_pointer(&ctx->path, g_free);

    g_clear_signal_handler (&ctx->scan_dialog_handler_id, G_OBJECT (ctx->scan_dialog));
    g_clear_signal_handler (&ctx->close_button_handler_id, G_OBJECT (ctx->close_button));
    g_clear_signal_handler (&ctx->cancel_button_handler_id, G_OBJECT (ctx->cancel_button));

    g_atomic_int_set(&ctx->ref_count, INT_MIN);
    g_free(ctx);
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

/* thread-safe method to add/output/clear a threat path to the list */
static void
add_threat_path(ScanContext *ctx, const char *path)
{
  g_mutex_lock(&ctx->threats_mutex);
  ctx->threat_paths = g_list_append(ctx->threat_paths, g_strdup(path));
  g_mutex_unlock(&ctx->threats_mutex);
}

static void
output_threat_path(ScanContext *ctx) // This will add to the AdwStatusPage
{
  ctx->threat_list_box = gtk_list_box_new(); // Create the list view for threats
  if (!ctx->threat_list_box)
  {
    g_critical("Failed to create list box");
    return;
  }

  gtk_widget_add_css_class(ctx->threat_list_box, "boxed-list"); // Add boxed-list style class to the list view
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(ctx->threat_list_box), GTK_SELECTION_NONE); // Disable selection for the list view
  
  GList *copy = NULL;
  g_mutex_lock(&ctx->threats_mutex);
  if (ctx->threat_paths) copy = g_list_copy(ctx->threat_paths); // Make a copy of the threat paths list
  g_mutex_unlock(&ctx->threats_mutex);

  GList *iter = copy;
  while (iter)
  {
    GtkWidget *action_row = adw_action_row_new(); // Create the action row for the list view
    gtk_widget_add_css_class(action_row, "property"); // Add property syle class to the action row
    adw_action_row_set_subtitle(ADW_ACTION_ROW(action_row), (const char *)iter->data);

    /* Delete button for the action row */
    GtkWidget *delete_button = gtk_button_new_with_label(gettext("Delete"));
    gtk_widget_set_halign(delete_button, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(delete_button, GTK_ALIGN_CENTER);
    
    adw_action_row_add_suffix(ADW_ACTION_ROW(action_row), delete_button);

    gtk_list_box_append(GTK_LIST_BOX(ctx->threat_list_box), action_row);

    /* Set file properties and connect signal */
    DeleteFileData *delete_data = delete_file_data_new(ctx->threat_list_box, action_row);
    if (!set_file_properties(delete_data)) // Set the file properties for the action row
    {
      /* If failed to set file properties, disable the AdwActionRow */
      g_critical("Failed to set file properties");
      adw_preferences_row_set_title(ADW_PREFERENCES_ROW(action_row), gettext("Failed to set file properties"));
      gtk_widget_set_sensitive(action_row, FALSE);
      goto next;
    }

    g_signal_connect_swapped(delete_button, "clicked", G_CALLBACK(delete_threat_file), delete_data); // Connect the delete button signal to the `delete_threat_file` function

  next:
    iter = iter->next;
  }
  g_list_free_full(g_steal_pointer(&ctx->threat_paths), g_free); // Free the copy of the threat paths list

  adw_status_page_set_child (ADW_STATUS_PAGE(ctx->threat_status_page), ctx->threat_list_box);
}

static void
clear_threat_paths(ScanContext *ctx)
{
  g_mutex_lock(&ctx->threats_mutex);
  GtkWidget *threat_list_box = ctx->threat_list_box;
  if (threat_list_box && GTK_IS_WIDGET(threat_list_box))
  {
    gtk_list_box_remove_all(GTK_LIST_BOX(threat_list_box)); // First clear all the action rows in the list view

    ctx->threat_list_box = NULL;

    g_clear_pointer(&ctx->threat_list_box, gtk_widget_unparent); // Then unparent the list view
  }

  if (ctx->threat_paths)
  {
    g_list_free_full(g_steal_pointer(&ctx->threat_paths), g_free); // Free the threat paths list
    ctx->threat_paths = NULL;
  }

  ctx->threat_list_box = NULL; // Reset the list view pointer
  g_mutex_unlock(&ctx->threats_mutex);
}

static void
resource_clean_up(IdleData *data)
{
  scan_context_unref (data->ctx);
  g_free(data->message);
  g_free(data);
}

static gboolean
scan_ui_callback(gpointer user_data)
{
  IdleData *data = (IdleData *)user_data;

  g_return_val_if_fail(data && data->ctx && g_atomic_int_get(&data->ctx->ref_count) > 0, 
                      G_SOURCE_REMOVE);

  char *status_marker = NULL; // Check file is OK or FOUND

  if ((status_marker = strstr(data->message, "FOUND")) != NULL)
  {
    inc_total_files(data->ctx);
    inc_total_threats(data->ctx);

    /* Add threat path to the list */
    char *colon = strchr(data->message, ':'); // Find the colon separator
    if (colon)
    {
      *colon = '\0'; // Replace the colon with null terminator
      add_threat_path(data->ctx, data->message);
    }
  }
  else if ((status_marker = strstr(data->message, "OK")) != NULL) inc_total_files(data->ctx);

  gint total_files = get_total_files(data->ctx);
  gint total_threats = get_total_threats(data->ctx);

  char *status_text = g_strdup_printf(gettext("%d files scanned\n%d threats found"), total_files, total_threats);
  
  if (data->ctx->scan_status_page && GTK_IS_WIDGET(data->ctx->scan_status_page))
  {
    adw_status_page_set_description(
      ADW_STATUS_PAGE(data->ctx->scan_status_page), 
      status_text
    );
  }

  g_free(status_text);

  return G_SOURCE_REMOVE;
}

static gboolean
scan_complete_callback(gpointer user_data)
{
  IdleData *data = user_data;

  g_return_val_if_fail(data && data->ctx && g_atomic_int_get(&data->ctx->ref_count) > 0, 
                      G_SOURCE_REMOVE);

  gboolean is_success = FALSE;
  get_completion_state(data->ctx, NULL, &is_success); // Get the completion state for thread-safe access

  if (get_cancel_scan(data->ctx)) // Check if the scan has been cancelled
  {
    adw_status_page_set_description(
      ADW_STATUS_PAGE(data->ctx->scan_status_page),
      gettext("User cancelled the scan")
    );
  }

  if (data->ctx->cancel_navigation_page && GTK_IS_WIDGET(data->ctx->cancel_navigation_page) &&
      data->ctx->navigation_view && GTK_IS_WIDGET(data->ctx->navigation_view) && 
      adw_navigation_view_get_visible_page(ADW_NAVIGATION_VIEW(data->ctx->navigation_view)) == data->ctx->cancel_navigation_page) // If the current page is the cancel page, pop it
  {
    g_print("[INFO] Scan process done, popping the cancel page\n");
    adw_navigation_view_pop(ADW_NAVIGATION_VIEW(data->ctx->navigation_view));
  }

  if (data->ctx->total_threats > 0) // If threats found, output the list view
  {
    output_threat_path(data->ctx);
    
    if (data->ctx->threat_button && GTK_IS_WIDGET(data->ctx->threat_button))
    {
      gtk_widget_set_sensitive(data->ctx->threat_button, TRUE);
      gtk_widget_set_visible(data->ctx->threat_button, TRUE);
    }

    if (data->ctx->threat_navigation_page && GTK_IS_WIDGET(data->ctx->threat_navigation_page) &&
        data->ctx->navigation_view && GTK_IS_WIDGET(data->ctx->navigation_view))
    {
      adw_navigation_view_push(ADW_NAVIGATION_VIEW(data->ctx->navigation_view), data->ctx->threat_navigation_page);
    }
  }

  if (data->ctx->scan_status_page && GTK_IS_WIDGET(data->ctx->scan_status_page))
  {
    adw_status_page_set_title(
      ADW_STATUS_PAGE(data->ctx->scan_status_page),
      data->message
    );
  }

  if (data->ctx->close_button && GTK_IS_WIDGET(data->ctx->close_button))
  {
    gtk_widget_set_visible(data->ctx->close_button, TRUE);
    gtk_widget_set_sensitive(data->ctx->close_button, TRUE);
  }

  return G_SOURCE_REMOVE;
}

static void
process_output_lines(RingBuffer *ring_buf, LineAccumulator *acc, ScanContext *ctx)
{
    char *line;
    while (ring_buffer_read_line(ring_buf, acc, &line))
    {
        IdleData *data = g_new0(IdleData, 1);
        data->message = g_strdup(line);
        data->ctx = scan_context_ref(ctx);
        g_main_context_invoke_full(
                       g_main_context_default(),
                       G_PRIORITY_HIGH_IDLE,
                       (GSourceFunc) scan_ui_callback,
                       data,
                       (GDestroyNotify)resource_clean_up);
    }
}

static void
send_final_status(ScanContext *ctx, gboolean success)
{
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

    /* Create final status message */
    IdleData *complete_data = g_new0(IdleData, 1);
    complete_data->ctx = scan_context_ref(ctx);
    complete_data->message = g_strdup(status_text);

    /* Send final status message to main thread */
    if (G_LIKELY((complete_data->ctx || g_atomic_int_get(&complete_data->ctx->ref_count) > 0 )
                      && complete_data->ctx->scan_status_page))
    {
        g_main_context_invoke_full(
                       g_main_context_default(),
                       G_PRIORITY_HIGH_IDLE,
                       (GSourceFunc) scan_complete_callback,
                       complete_data,
                       (GDestroyNotify)resource_clean_up);
    }
    else
    {
        g_warning("Attempted to send status to invalid context");
        scan_context_unref(complete_data->ctx);
        g_free(complete_data->message);
        g_free(complete_data);
    }
}

static gpointer
scan_thread(gpointer data)
{
    ScanContext *ctx = data;
    int pipefd[2];
    pid_t pid;

    /*Initialize ring buffer and line accumulator*/
    RingBuffer ring_buf;
    ring_buffer_init(&ring_buf);
    LineAccumulator acc;
    line_accumulator_init(&acc);

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
        .acc = &acc,
    };

    /*Start scan thread*/
    struct pollfd fds = { .fd = pipefd[0], .events = POLLIN };
    int idle_counter = 0;
    int dynamic_timeout = BASE_TIMEOUT_MS;
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
    process_output_lines(&ring_buf, &acc, ctx);
    gboolean success = wait_for_process(pid, TRUE);
    send_final_status(ctx, success);

    close(pipefd[0]);
    return NULL;
}

/* Clear the list view and force close the dialog */
static void
clear_box_list_and_force_close(ScanContext *ctx)
{
  clear_threat_paths(ctx); // Clear the list view

  adw_dialog_force_close(ctx->scan_dialog);
  scan_context_unref(ctx); // unref at here to avoid fucked up the `ScanContext` before getting the `threat_list_box`
}

/* Attempts to cancel the scan */
static void
cancel_scan_attempt(ScanContext *ctx)
{
  gboolean is_completed = FALSE;
  get_completion_state(ctx, &is_completed, NULL);
  /* Only push the cancel page if the scan is not completed */
  if (!is_completed) adw_navigation_view_push(ADW_NAVIGATION_VIEW(ctx->navigation_view), ctx->cancel_navigation_page);
  else clear_box_list_and_force_close(ctx);
}

/* Comfirm the cancelation of the scan */
static void
confirm_cancel_scan(ScanContext *ctx)
{
  set_cancel_scan(ctx);
  adw_navigation_view_pop(ADW_NAVIGATION_VIEW(ctx->navigation_view));
}

void
start_scan(AdwDialog *dialog,
             GtkWidget *navigation_view,
             GtkWidget *scan_status,
             GtkWidget *close_button,
             AdwNavigationPage *threat_navigation_page,
             GtkWidget *threat_status,
             GtkWidget *threat_button,
             AdwNavigationPage *cancel_navigation_page,
             GtkWidget *cancel_button,
             char *path)
{
  ScanContext *ctx = scan_context_new();

  g_mutex_lock(&ctx->mutex);
  ctx->completed = FALSE;
  ctx->success = FALSE;
  ctx->total_files = 0;
  ctx->total_threats = 0;
  ctx->threat_paths = NULL;
  ctx->threat_list_box = NULL;
  ctx->scan_dialog = dialog;
  ctx->scan_dialog_handler_id = 
          g_signal_connect_swapped(ctx->scan_dialog, "close-attempt", G_CALLBACK(cancel_scan_attempt), ctx); // Connect the close-attempt signal to the cancel_scan_attempt function
  ctx->navigation_view = navigation_view;
  ctx->scan_status_page = scan_status;
  ctx->close_button = close_button;
  ctx->close_button_handler_id = 
          g_signal_connect_swapped(GTK_BUTTON(ctx->close_button), "clicked", G_CALLBACK(clear_box_list_and_force_close), ctx); // Connect the close button to the clear_box_list_and_force_close function
  ctx->threat_navigation_page = threat_navigation_page;
  ctx->threat_status_page = threat_status;
  ctx->threat_button = threat_button;
  ctx->cancel_navigation_page = cancel_navigation_page;
  ctx->cancel_button = cancel_button;
  ctx->cancel_button_handler_id = 
          g_signal_connect_swapped(GTK_BUTTON(ctx->cancel_button), "clicked", G_CALLBACK(confirm_cancel_scan), ctx); // Connect the cancel button to the confirm_cancel_scan function
  ctx->path = path;
  ctx->ref_count = G_ATOMIC_REF_COUNT_INIT;
  g_mutex_unlock(&ctx->mutex);

  /* Start scan thread */
  g_thread_new("scan-thread", scan_thread, ctx);
}

