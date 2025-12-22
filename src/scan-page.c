/* scan-page.c
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

#include "libs/check-scan-time.h"
#include "libs/scan.h"

#include "scan-page.h"
#include "wuming-window.h"

struct _ScanPage {
  GtkWidget          parent_instance;

  /*Child*/
  AdwStatusPage      *status_page;
  GtkButton          *scan_a_file_button;
  GtkButton          *scan_a_folder_button;

  /* Private */
  GtkFileDialog *dialog;
};

G_DEFINE_FINAL_TYPE (ScanPage, scan_page, GTK_TYPE_WIDGET)

/* Show whether the last scan time is expired or not */
/*
  * @param self
  * `ScanPage` object
  * 
  * @param timestamp
  * Timestamp string to set as last scan time
  * 
  * @param is_expired
  * Whether the last scan time is expired or not.
  * 
  * @note
  * If `GSettings` is not NULL, the `is_expired` parameter will be ignored.
*/
void
scan_page_show_last_scan_time_status (ScanPage *self, const gchar *timestamp, gboolean is_expired)
{
  g_return_if_fail (SCAN_IS_PAGE (self));

  GtkWidget *status_page = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_STATUS_PAGE);

  gchar *title = NULL;
  gchar *icon_name = NULL;
  g_autofree gchar *description = g_strdup_printf (gettext("Last Scan Time: %s"), timestamp);

  title = is_expired ? gettext("Scan Has Expired") : gettext("Scan Has Not Expired");
  icon_name = is_expired ? "status-warning-symbolic" : "status-ok-symbolic";

  adw_status_page_set_title (self->status_page, title);
  adw_status_page_set_description (self->status_page, description);
  adw_status_page_set_icon_name (self->status_page, icon_name);
}

static void
start_scan_file (GObject *source_object, GAsyncResult *res, gpointer data)
{
  GtkFileDialog *file_dialog = GTK_FILE_DIALOG (source_object);
  ScanContext *context = data;
  GFile *file = NULL;
  GError *error = NULL;

  if ((file = gtk_file_dialog_open_finish (file_dialog, res, &error)) != NULL)
  {
    char *filepath = g_file_get_path (file);

    /* Start scannning */
    start_scan (context, filepath);

    g_object_unref (file); // Only unref the file if it is successfully opened
  }
  else
  {
    if (error->code == GTK_DIALOG_ERROR_DISMISSED)
          g_warning ("[INFO] User canceled the file selection!");
    else
          g_critical ("[ERROR] Failed to open the file!");
  }
  g_clear_error (&error);
}

static void
start_scan_folder (GObject *source_object, GAsyncResult *res, gpointer data)
{
  GtkFileDialog *file_dialog = GTK_FILE_DIALOG (source_object);
  ScanContext *context = data;
  GFile *file = NULL;
  GError *error = NULL;

  if ((file = gtk_file_dialog_select_folder_finish (file_dialog, res, &error)) != NULL)
  {
    char *folderpath = g_file_get_path (file);

    /* Start scannning */
    start_scan (context, folderpath);

    g_object_unref (file); // Only unref the file if it is successfully opened
  }
  else
  {
    if (error->code == GTK_DIALOG_ERROR_DISMISSED)
          g_warning ("[INFO] User canceled the folder selection!");
    else
          g_critical ("[ERROR] Failed to open the folder!");
  }
  g_clear_error (&error);
}

/*Callbacks*/

static void
file_chooser (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  ScanPage *self = user_data;

  WumingWindow *window = WUMING_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_APPLICATION_WINDOW));

  if (!wuming_window_is_in_main_page(window)) return; // Prevent multiple tasks running at the same time

  ScanContext *context = (ScanContext *)wuming_window_get_component (window, "scan_context");

  g_print("[INFO] Choose a file\n");

  gtk_file_dialog_open (self->dialog, GTK_WINDOW (window), NULL, start_scan_file, context); // Select a file
}

static void
folder_chooser (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  ScanPage *self = user_data;

  WumingWindow *window = WUMING_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_APPLICATION_WINDOW));

  if (!wuming_window_is_in_main_page(window)) return; // Prevent multiple tasks running at the same time

  ScanContext *context = (ScanContext *)wuming_window_get_component (window, "scan_context");

  g_print("[INFO] Choose a folder\n");

  gtk_file_dialog_select_folder (self->dialog, GTK_WINDOW (window), NULL, start_scan_folder, context); // Select a folder
}

/*GObject Essential Functions */

static const GActionEntry scan_actions[] = {
    { "scan-file", file_chooser },
    { "scan-folder", folder_chooser },
};

static void
scan_page_dispose(GObject *gobject)
{
  ScanPage *self = SCAN_PAGE (gobject);

  GApplication *app = g_application_get_default();

  g_action_map_remove_action_entries (G_ACTION_MAP (app),
	                                 scan_actions,
	                                 G_N_ELEMENTS (scan_actions));

  GtkWidget *status_page = GTK_WIDGET (self->status_page);
  g_clear_pointer (&status_page, gtk_widget_unparent);

  G_OBJECT_CLASS(scan_page_parent_class)->dispose(gobject);
}

static void
scan_page_finalize(GObject *gobject)
{
  ScanPage *self = SCAN_PAGE (gobject);

  /* Reset all child widgets */
  self->status_page = NULL;
  self->scan_a_file_button = NULL;
  self->scan_a_folder_button = NULL;

  G_OBJECT_CLASS(scan_page_parent_class)->finalize(gobject);
}

static void
scan_page_class_init (ScanPageClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->dispose = scan_page_dispose;
  gobject_class->finalize = scan_page_finalize;

  gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);

  gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/pages/scan-page.ui");

  gtk_widget_class_bind_template_child (widget_class, ScanPage, status_page);
  gtk_widget_class_bind_template_child (widget_class, ScanPage, scan_a_file_button);
  gtk_widget_class_bind_template_child (widget_class, ScanPage, scan_a_folder_button);
}

GtkWidget *
scan_page_new(void)
{
  return g_object_new (SCAN_TYPE_PAGE, NULL);
}

static void
scan_page_init (ScanPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  /* Set initial folder */
  self->dialog = gtk_file_dialog_new();
  g_object_ref_sink(self->dialog);

  /* Map scan actions */
  GApplication *app = g_application_get_default();
	g_action_map_add_action_entries (G_ACTION_MAP (app),
	                                 scan_actions,
	                                 G_N_ELEMENTS (scan_actions),
	                                 self);
}
