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
#include "libs/scan.h"

struct _ScanPage {
  GtkWidget          parent_instance;

  /*Child*/
  GtkWidget          *clamp;
  GtkButton          *scan_a_file_button;
  GtkButton          *scan_a_folder_button;

    /*Scanning Page*/
  AdwNavigationPage  *scan_nav_page;
  GtkButton          *cancel_button;
  gulong             cancel_button_signal_id;
  AdwStatusPage      *scan_status;
  GtkButton          *threat_button;
  gulong             threat_button_signal_id;
  GtkButton          *close_button;
  gulong             close_button_signal_id;

  /* Threat Page*/
  AdwNavigationPage  *threat_nav_page;
  AdwStatusPage      *threat_status;
  GtkListBox         *threat_list;

  /* Private */
  AdwSpinnerPaintable *spinner;
};

G_DEFINE_FINAL_TYPE (ScanPage, scan_page, GTK_TYPE_WIDGET)

static void
reset_signal_handlers (ScanPage *self)
{
  if (self->cancel_button_signal_id > 0)
  {
    g_signal_handler_disconnect (self->cancel_button, self->cancel_button_signal_id);
    self->cancel_button_signal_id = 0;
  }

  if (self->threat_button_signal_id > 0)
  {
    g_signal_handler_disconnect (self->threat_button, self->threat_button_signal_id);
    self->threat_button_signal_id = 0;
  }

  if (self->close_button_signal_id > 0)
  {
    g_signal_handler_disconnect (self->close_button, self->close_button_signal_id);
    self->close_button_signal_id = 0;
  }
}

static void
reset_pages (ScanPage *self, AdwNavigationView *view)
{
  /* Reset scanning page */
  adw_status_page_set_paintable (self->scan_status, GDK_PAINTABLE (self->spinner));
  adw_status_page_set_title (self->scan_status, gettext("Scanning..."));
  adw_status_page_set_description (self->scan_status, gettext("Preparing..."));

  /* Reset threat page */
  gtk_list_box_remove_all (self->threat_list);

  /* Disable close button */
  gtk_widget_set_sensitive (GTK_WIDGET (self->close_button), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->close_button), FALSE);

  /* Disable threat button */
  gtk_widget_set_sensitive (GTK_WIDGET (self->threat_button), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->threat_button), FALSE);

  /* Enable cancel button */
  gtk_widget_set_sensitive (GTK_WIDGET (self->cancel_button), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->cancel_button), TRUE);

  reset_signal_handlers (self);
}

GtkWidget *
threat_page_get_list(ScanPage *self)
{
  return GTK_WIDGET (self->threat_list);
}

void
threat_page_prepend_threat(ScanPage *self, AdwActionRow *row)
{
  gtk_list_box_prepend (self->threat_list, GTK_WIDGET (row));
}

void
show_threat_page (ScanPage *self)
{
  /* Enable threat button */
  gtk_widget_set_sensitive (GTK_WIDGET (self->threat_button), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->threat_button), TRUE);

  AdwNavigationView *view = ADW_NAVIGATION_VIEW (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW));
  adw_navigation_view_push (view, self->threat_nav_page);
}

void
threat_page_clear_list(ScanPage *self)
{
  gtk_list_box_remove_all (self->threat_list);
}

void
cancel_signal_bind(ScanPage *self, GCallback cancel_callback, gpointer user_data)
{
  self->cancel_button_signal_id = g_signal_connect_swapped (self->cancel_button, "clicked", G_CALLBACK (cancel_callback), user_data);
}

void
cancel_signal_clear(ScanPage *self)
{
  if (self->cancel_button_signal_id > 0)
  {
    g_signal_handler_disconnect (self->cancel_button, self->cancel_button_signal_id);
    self->cancel_button_signal_id = 0;
  }
}

static void
reset_pages_and_start_scan (ScanPage *self, char *path)
{
  AdwNavigationView *view = ADW_NAVIGATION_VIEW (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW));
  AdwNavigationPage *current_page = adw_navigation_view_get_visible_page (view);

  if (current_page == self->scan_nav_page || current_page == self->threat_nav_page) return; // Do not allow multiple scans

  reset_pages (self, view);

  self->threat_button_signal_id = g_signal_connect_swapped (self->threat_button, "clicked", G_CALLBACK (show_threat_page), self);
  self->close_button_signal_id = g_signal_connect_swapped (self->close_button, "clicked", G_CALLBACK (adw_navigation_view_pop), view);

  adw_navigation_view_push (view, self->scan_nav_page);
  start_scan (self, path);
}

void
scanning_page_set_current_status(ScanPage *self, const char *status)
{
  adw_status_page_set_description (self->scan_status, status);
}

void
scanning_page_set_final_result(ScanPage *self, const char *result, const char *detail, const char *icon_name)
{
  if (result) adw_status_page_set_title (self->scan_status, result);
  if (detail) adw_status_page_set_description (self->scan_status, detail);
  if (icon_name) adw_status_page_set_icon_name (self->scan_status, icon_name);

  /* Enable the close button */
  gtk_widget_set_sensitive (GTK_WIDGET (self->close_button), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->close_button), TRUE);

  /* Disable the cancel button */
  gtk_widget_set_sensitive (GTK_WIDGET (self->cancel_button), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->cancel_button), FALSE);
}

static void
build_scanning_page (ScanPage *self)
{
  GtkBuilder *builder = gtk_builder_new_from_resource ("/com/ericlin/wuming/pages/scanning-page.ui");

  self->scan_nav_page = ADW_NAVIGATION_PAGE (gtk_builder_get_object (builder, "scan_nav_page"));
  g_object_ref_sink (self->scan_nav_page); // Keep the reference to the scanning page

  self->cancel_button = GTK_BUTTON (gtk_builder_get_object (builder, "cancel_button"));
  self->scan_status = ADW_STATUS_PAGE (gtk_builder_get_object (builder, "status_page"));
  self->threat_button = GTK_BUTTON (gtk_builder_get_object (builder, "threat_button"));
  self->close_button = GTK_BUTTON (gtk_builder_get_object (builder, "close_button"));

  g_object_unref (builder);
}

static void
build_threat_page (ScanPage *self)
{
  GtkBuilder *builder = gtk_builder_new_from_resource ("/com/ericlin/wuming/pages/threat-page.ui"); // Load the threat page UI

  self->threat_nav_page = ADW_NAVIGATION_PAGE (gtk_builder_get_object (builder, "threat_nav_page"));
  g_object_ref_sink (self->threat_nav_page); // Keep the reference to the threat page

  self->threat_status = ADW_STATUS_PAGE (gtk_builder_get_object (builder, "status_page"));
  self->threat_list = GTK_LIST_BOX (gtk_builder_get_object (builder, "list_box"));

  g_object_unref (builder);
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
      reset_pages_and_start_scan (self, filepath);

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
      reset_pages_and_start_scan (self, folderpath);

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

/*Callbacks*/

static void
file_chooser (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  ScanPage *self = user_data;
  GtkButton *file_button = GTK_BUTTON(self->scan_a_file_button);

  gtk_widget_set_sensitive (GTK_WIDGET (file_button), FALSE);
  g_print("[INFO] Choose a file\n");

  GtkWidget *window = gtk_widget_get_ancestor (GTK_WIDGET (file_button), ADW_TYPE_APPLICATION_WINDOW);

  GtkFileDialog *dialog;
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_open (dialog, GTK_WINDOW (window), NULL, start_scan_file, user_data); // Select a file

  g_object_unref (dialog);
  gtk_widget_set_sensitive (GTK_WIDGET (file_button), TRUE);
}

static void
folder_chooser (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  ScanPage *self = user_data;
  GtkButton *folder_button = GTK_BUTTON(self->scan_a_folder_button);

  gtk_widget_set_sensitive (GTK_WIDGET (folder_button), FALSE);
  g_print("[INFO] Choose a folder\n");

  GtkWidget *window = gtk_widget_get_ancestor (GTK_WIDGET (folder_button), ADW_TYPE_APPLICATION_WINDOW);

  GtkFileDialog *dialog;
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_select_folder (dialog, GTK_WINDOW (window), NULL, start_scan_folder, user_data); // Select a folder

  g_object_unref (dialog);
  gtk_widget_set_sensitive (GTK_WIDGET (folder_button), TRUE);
}

/*GObject Essential Functions */

static void
scan_page_dispose(GObject *gobject)
{
  ScanPage *self = SCAN_PAGE (gobject);

  GtkWidget *scan_nav_page = GTK_WIDGET(self->scan_nav_page); // Cast it for cleaning up
  GtkWidget *threat_nav_page = GTK_WIDGET(self->threat_nav_page); // Cast it for cleaning up

  reset_signal_handlers (self);

  g_clear_object (&self->spinner);
  g_clear_pointer (&scan_nav_page, gtk_widget_unparent);
  g_clear_pointer (&threat_nav_page, gtk_widget_unparent);
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

  /*Build Scanning Page*/
  build_scanning_page (self);

  /*Build Threat Page*/
  build_threat_page (self);

  /*Create `AdwSpinnerPaintable` for scanning page*/
  self->spinner = adw_spinner_paintable_new (GTK_WIDGET (self->scan_status));
  g_object_ref_sink (self->spinner); // Keep the reference to the spinner

  /* Map scan actions */
  GApplication *app = g_application_get_default();
	g_action_map_add_action_entries (G_ACTION_MAP (app),
	                                 scan_actions,
	                                 G_N_ELEMENTS (scan_actions),
	                                 self);
}
