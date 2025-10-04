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

#include "scan-page.h"
#include "wuming-window.h"
#include "scanning-page.h"
#include "threat-page.h"

#include "libs/scan.h"

struct _ScanPage {
  GtkWidget          parent_instance;

  /*Child*/
  GtkWidget          *clamp;
  GtkButton          *scan_a_file_button;
  GtkButton          *scan_a_folder_button;
};

G_DEFINE_FINAL_TYPE (ScanPage, scan_page, GTK_TYPE_WIDGET)

static void
reset_and_start_scan (ScanPage *self, char *path)
{
  WumingWindow *window = WUMING_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_APPLICATION_WINDOW));
  ScanningPage *scanning_page = SCANNING_PAGE (wuming_window_get_scanning_page (window));
  ThreatPage *threat_page = THREAT_PAGE (wuming_window_get_threat_page (window));

  // Reset widget and start scanning
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

static void
scan_page_dispose(GObject *gobject)
{
  ScanPage *self = SCAN_PAGE (gobject);

  g_clear_pointer (&self->clamp, gtk_widget_unparent);

  G_OBJECT_CLASS(scan_page_parent_class)->dispose(gobject);
}

static void
scan_page_finalize(GObject *gobject)
{
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

  gtk_widget_class_bind_template_child (widget_class, ScanPage, clamp);
  gtk_widget_class_bind_template_child (widget_class, ScanPage, scan_a_file_button);
  gtk_widget_class_bind_template_child (widget_class, ScanPage, scan_a_folder_button);
}

GtkWidget *
scan_page_new(void)
{
  return g_object_new (SCAN_TYPE_PAGE, NULL);
}

static const GActionEntry scan_actions[] = {
    { "scan-file", file_chooser },
    { "scan-folder", folder_chooser },
};

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
