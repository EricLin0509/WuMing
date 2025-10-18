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
#include "security-overview-page.h"
#include "scanning-page.h"
#include "threat-page.h"

struct _ScanPage {
  GtkWidget          parent_instance;

  /*Child*/
  GtkWidget          *break_point;
  GtkButton          *scan_a_file_button;
  GtkButton          *scan_a_folder_button;
};

G_DEFINE_FINAL_TYPE (ScanPage, scan_page, GTK_TYPE_WIDGET)


/* Show the last scan time to the status page */
/*
  * @param self
  * `ScanPage` object
  * 
  * @param setting [OPTIONAL]
  * `GSettings` object to save last scan time, if this is NULL, it will create a new one.
  * 
  * @param timestamp [OPTIONAL]
  * Timestamp string to set as last scan time, if this is NULL, it will use the `GSSettings` object to get the last scan time.
  * 
  * @note
  * if `timestamp` is provided, the `setting` parameter will be ignored.
  * 
  * @warning
  * If `GSettings` is not NULL, you need to unref it manually. This allow sharing the same `GSettings` object with other parts of the program.
*/
void
scan_page_show_last_scan_time (ScanPage *self, GSettings *setting, const gchar *timestamp)
{
  g_return_if_fail (SCAN_PAGE (self));

  gboolean has_timestamp = (timestamp != NULL);
  gboolean is_null = (setting == NULL);

  GtkWidget *status_page = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_STATUS_PAGE);

  g_autofree gchar *description = NULL;
  if (has_timestamp) // Has timestamp, use it directly
  {
    description = g_strdup_printf (gettext("Last Scan Time: %s"), timestamp); // Use the provided timestamp
  }
  else // No timestamp, get from GSettings
  {
    GSettings *setting_choice = is_null ? g_settings_new ("com.ericlin.wuming") : setting;

    g_autofree gchar *stored_timestamp = g_settings_get_string (setting_choice, "last-scan-time");
    description = g_strdup_printf (gettext("Last Scan Time: %s"), stored_timestamp); // No timestamp, get from GSettings

    if (is_null) g_object_unref (setting_choice); // Only unref if it is created here
  }

  adw_status_page_set_description (ADW_STATUS_PAGE (status_page), description);
}

/* Show whether the last scan time is expired or not */
/*
  * @param self
  * `ScanPage` object
  * 
  * @param setting [OPTIONAL]
  * `GSettings` object to save last scan time, if is NULL, it will ignore it and use `is_expired` directly.
  * 
  * @param is_expired [OPTIONAL]
  * Whether the last scan time is expired or not.
  * 
  * @note
  * If `GSettings` is not NULL, the `is_expired` parameter will be ignored.
*/
void
scan_page_show_last_scan_time_status (ScanPage *self, GSettings *setting, gboolean is_expired)
{
  g_return_if_fail (SCAN_PAGE (self));

  GtkWidget *status_page = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_STATUS_PAGE);

  gboolean is_null = (setting == NULL);
  gchar *title = NULL;
  gchar *icon_name = NULL;
  if (is_null) // No `GSettings`, use `is_expired` directly
  {
    title = is_expired ? gettext("Scan Has Expired") : gettext("Scan Has Not Expired");
    icon_name = is_expired ? "status-warning-symbolic" : "status-ok-symbolic";
  }
  else // Has `GSettings`, use it to get the last scan time and check if it is expired
  {
    gboolean is_older_than_a_week = is_scan_time_expired (NULL, setting);

    title = is_older_than_a_week ? gettext("Scan Has Expired") : gettext("Scan Has Not Expired");
    icon_name = is_older_than_a_week ? "status-warning-symbolic" : "status-ok-symbolic";
  }

  adw_status_page_set_title (ADW_STATUS_PAGE (status_page), title);
  adw_status_page_set_icon_name (ADW_STATUS_PAGE (status_page), icon_name);
}

/* Save last scan time to GSettings */
/*
  * @param setting
  * `GSettings` object to save last scan time, if this is NULL, it will create a new one.
  * 
  * @param need_timestamp [optional]
  * If this is true, it will generate a new timestamp and save it to GSettings. otherwise return NULL
  * 
  * @warning
  * If `GSettings` is not NULL, you need to unref it manually. This allow sharing the same `GSettings` object with other parts of the program.
*/
static gchar *
save_last_scan_time (GSettings *setting, gboolean need_timestamp)
{
  gboolean is_null = (setting == NULL);

  GSettings *setting_choice = is_null ? g_settings_new ("com.ericlin.wuming") : setting;

  GDateTime *now = g_date_time_new_now_local();
  g_autofree gchar *timestamp = g_date_time_format(now, "%Y.%m.%d %H:%M:%S"); // Format: YYYY.MM.DD HH:MM:SS
  g_settings_set_string (setting_choice, "last-scan-time", timestamp);
  g_date_time_unref (now);

  if (is_null) g_object_unref (setting_choice);

  return need_timestamp ? g_steal_pointer(&timestamp) : NULL;
}

static void
reset_and_start_scan (ScanPage *self, char *path)
{
  WumingWindow *window = WUMING_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_APPLICATION_WINDOW));
  SecurityOverviewPage *security_overview_page = SECURITY_OVERVIEW_PAGE (wuming_window_get_security_overview_page (window));
  ScanningPage *scanning_page = SCANNING_PAGE (wuming_window_get_scanning_page (window));
  ThreatPage *threat_page = THREAT_PAGE (wuming_window_get_threat_page (window));

  g_autofree gchar *timestamp = save_last_scan_time (NULL, TRUE);
  scan_page_show_last_scan_time (self, NULL, timestamp);
  scan_page_show_last_scan_time_status (self, NULL, FALSE);
  security_overview_page_show_last_scan_time_status (security_overview_page, NULL, FALSE);
  security_overview_page_show_health_level (security_overview_page);

  /* Reset widget and start scanning */
  scanning_page_reset (scanning_page);
  threat_page_clear (threat_page);
  wuming_window_push_page_by_tag (window, "scanning_nav_page");

  start_scan (window, scanning_page, threat_page, path);
}

static void
start_scan_file (GObject *source_object, GAsyncResult *res, gpointer data)
{
  GtkFileDialog *file_dialog = GTK_FILE_DIALOG (source_object);
  ScanPage *self = data;
  GFile *file = NULL;
  GError *error = NULL;

  if (file = gtk_file_dialog_open_finish (file_dialog, res, &error))
    {
      char *filepath = g_file_get_path (file);

      /* Reset widget and start scannning */
      reset_and_start_scan (self, filepath);

      g_object_unref (file); // Only unref the file if it is successfully opened
    }
  else
    {
      if (error->code == GTK_DIALOG_ERROR_DISMISSED)
            g_warning ("[INFO] User canceled the file selection!");
      else
            g_warning ("[ERROR] Failed to open the file!");

      g_clear_error (&error);
    }
}

static void
start_scan_folder (GObject *source_object, GAsyncResult *res, gpointer data)
{
  GtkFileDialog *file_dialog = GTK_FILE_DIALOG (source_object);
  ScanPage *self = data;
  GFile *file = NULL;
  GError *error = NULL;

  if (file = gtk_file_dialog_select_folder_finish (file_dialog, res, &error))
    {
      char *folderpath = g_file_get_path (file);

      /* Reset widget and start scannning */
      reset_and_start_scan (self, folderpath);

      g_object_unref (file); // Only unref the file if it is successfully opened
    }
  else
    {
      if (error->code == GTK_DIALOG_ERROR_DISMISSED)
            g_warning ("[INFO] User canceled the folder selection!");
      else
            g_warning ("[ERROR] Failed to open the folder!");

      g_clear_error (&error);
    }
}

static gboolean
check_is_scanning (WumingWindow *window)
{
  g_return_val_if_fail (WUMING_IS_WINDOW (window), TRUE); // Fallback save if window is not a WumingWindow

  if (wuming_window_is_current_page_tag (window, "scanning_nav_page")) return TRUE;
  if (wuming_window_is_current_page_tag (window, "threat_nav_page")) return TRUE;

  return FALSE;
}

/*Callbacks*/

static void
file_chooser (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  ScanPage *self = user_data;

  WumingWindow *window = WUMING_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_APPLICATION_WINDOW));

  if (check_is_scanning (window)) return; // Prevent multiple scans

  g_print("[INFO] Choose a file\n");

  GtkFileDialog *dialog;
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_open (dialog, GTK_WINDOW (window), NULL, start_scan_file, user_data); // Select a file

  g_object_unref (dialog);
}

static void
folder_chooser (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  ScanPage *self = user_data;

  WumingWindow *window = WUMING_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_APPLICATION_WINDOW));

  if (check_is_scanning (window)) return; // Prevent multiple scans

  g_print("[INFO] Choose a folder\n");

  GtkFileDialog *dialog;
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_select_folder (dialog, GTK_WINDOW (window), NULL, start_scan_folder, user_data); // Select a folder

  g_object_unref (dialog);
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

  g_clear_pointer (&self->break_point, gtk_widget_unparent);

  G_OBJECT_CLASS(scan_page_parent_class)->dispose(gobject);
}

static void
scan_page_finalize(GObject *gobject)
{
  ScanPage *self = SCAN_PAGE (gobject);

  /* Reset all child widgets */
  self->break_point = NULL;
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

  gtk_widget_class_bind_template_child (widget_class, ScanPage, break_point);
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

  /* Map scan actions */
  GApplication *app = g_application_get_default();
	g_action_map_add_action_entries (G_ACTION_MAP (app),
	                                 scan_actions,
	                                 G_N_ELEMENTS (scan_actions),
	                                 self);
}
