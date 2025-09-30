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

  /*ScanDialog*/
  AdwDialog          *dialog;
  GtkWidget           *navigation_view;

  AdwNavigationPage *scan_navigation_page;
  GtkWidget          *scan_status;
  GtkWidget          *close_button;

  AdwNavigationPage *threat_navigation_page;
  GtkWidget          *threat_status; // store the threat status
  GtkWidget          *threat_button; // show the threat status

  AdwNavigationPage *cancel_navigation_page;
  GtkWidget          *cancel_status; // store the cancel status
  GtkWidget          *cancel_button; // comfirm the cancel status

  /*Child*/
  GtkWidget          *clamp;
  GtkButton          *scan_a_file_button;
  GtkButton          *scan_a_folder_button;
};

G_DEFINE_FINAL_TYPE (ScanPage, scan_page, GTK_TYPE_WIDGET)

static void
reset_scan_dialog_and_start_scan (ScanPage *self, char *path)
{
  adw_status_page_set_title (ADW_STATUS_PAGE (self->scan_status), gettext("Scanning..."));
  adw_status_page_set_description (ADW_STATUS_PAGE (self->scan_status), gettext ("Preparing..."));
  gtk_widget_set_visible(self->threat_button, FALSE);
  gtk_widget_set_sensitive (self->threat_button, FALSE);
  gtk_widget_set_visible(self->close_button, FALSE);
  gtk_widget_set_sensitive (self->close_button, FALSE);

  /* Pop pages to the scan page */
  while (adw_navigation_view_get_visible_page (ADW_NAVIGATION_VIEW (self->navigation_view))!= self->scan_navigation_page)
    adw_navigation_view_pop (ADW_NAVIGATION_VIEW (self->navigation_view));

  /*Present dialog*/
  adw_dialog_present (self->dialog, GTK_WIDGET (self));

  /*Start scan*/
  start_scan(self->dialog,
             self->navigation_view,
             self->scan_status,
             self->close_button,
             self->threat_navigation_page,
             self->threat_status,
             self->threat_button,
             self->cancel_navigation_page,
             self->cancel_button,
             path);
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
      reset_scan_dialog_and_start_scan (self, filepath);

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
      reset_scan_dialog_and_start_scan (self, folderpath);

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

static void
show_threat_status(ScanPage *self)
{
  adw_navigation_view_push (ADW_NAVIGATION_VIEW (self->navigation_view), self->threat_navigation_page);
}

static void
build_scan_dialog (ScanPage *self)
{
  /* Create dialog */
  self->dialog = g_object_ref_sink(adw_dialog_new());
  adw_dialog_set_can_close(ADW_DIALOG (self->dialog), FALSE); // Make sure the `close-attempt` signal can be emitted
  adw_dialog_set_content_height (self->dialog, 320);
  adw_dialog_set_content_width (self->dialog, 420);

  /* Add AdwNavigationView */
  self->navigation_view = adw_navigation_view_new ();
  adw_dialog_set_child (self->dialog, self->navigation_view);

  /* Scan Page */
  GtkWidget *scan_toolbar = adw_toolbar_view_new();
  GtkWidget *scan_header = adw_header_bar_new();
  // Only show the close button on the scan page

  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (scan_toolbar), scan_header);

  self->scan_status = adw_status_page_new ();

  GtkWidget *box_button = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5); // Create a box to store the buttons
  gtk_widget_set_halign (box_button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (box_button, GTK_ALIGN_CENTER);
  adw_status_page_set_child (ADW_STATUS_PAGE (self->scan_status), box_button);

  self->threat_button = gtk_button_new_with_label (gettext("Show threats")); // Create a button to show the threats
  gtk_widget_add_css_class (self->threat_button, "warning");
  gtk_box_append (GTK_BOX (box_button), self->threat_button);

  self->close_button = gtk_button_new_with_label (gettext("Close")); // Close button on the scan page
  gtk_box_append (GTK_BOX (box_button), self->close_button);

  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (scan_toolbar), self->scan_status); // Add AdwStatusPage to AdwToolbarView

  self->scan_navigation_page = adw_navigation_page_new(scan_toolbar, gettext("Scan"));
  adw_navigation_view_add (ADW_NAVIGATION_VIEW (self->navigation_view), self->scan_navigation_page);

  /* Threat Page */
  GtkWidget *threat_toolbar = adw_toolbar_view_new();
  GtkWidget *threat_header = adw_header_bar_new();
  adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (threat_header), FALSE); // Hide the buttons on the right side

  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (threat_toolbar), threat_header);

  self->threat_status = adw_status_page_new ();
  adw_status_page_set_title(ADW_STATUS_PAGE (self->threat_status), gettext("Found Threats"));
  
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (threat_toolbar), self->threat_status); // Add AdwStatusPage to AdwToolbarView

  self->threat_navigation_page = adw_navigation_page_new(threat_toolbar, gettext("Threats"));
  adw_navigation_view_add (ADW_NAVIGATION_VIEW (self->navigation_view), self->threat_navigation_page);

  /* Cancel Page */
  GtkWidget *cancel_toolbar = adw_toolbar_view_new();
  GtkWidget *cancel_header = adw_header_bar_new();
  adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (cancel_header), FALSE); // Hide the buttons on the right side

  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (cancel_toolbar), cancel_header);

  self->cancel_status = adw_status_page_new ();
  adw_status_page_set_title(ADW_STATUS_PAGE (self->cancel_status), gettext("Cancel Scan Process?"));
  adw_status_page_set_description (ADW_STATUS_PAGE (self->cancel_status), gettext("This will stop the current scan process. Are you sure?"));

  self->cancel_button = gtk_button_new_with_label (gettext("Cancel"));
  gtk_widget_add_css_class (self->cancel_button, "destructive-action");
  gtk_widget_set_halign (self->cancel_button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (self->cancel_button, GTK_ALIGN_CENTER);
  adw_status_page_set_child (ADW_STATUS_PAGE (self->cancel_status), self->cancel_button);
  
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (cancel_toolbar), self->cancel_status); // Add AdwStatusPage to AdwToolbarView

  self->cancel_navigation_page = adw_navigation_page_new(cancel_toolbar, gettext("Cancel Scan Process"));
  adw_navigation_view_add (ADW_NAVIGATION_VIEW (self->navigation_view), self->cancel_navigation_page);

  g_signal_connect_swapped (self->threat_button, "clicked", G_CALLBACK (show_threat_status), self);
}

/*GObject Essential Functions */

static void
scan_page_dispose(GObject *gobject)
{
  ScanPage *self = SCAN_PAGE (gobject);

  GtkWidget *dialog = GTK_WIDGET(self->dialog); // Cast it for cleaning up

  g_clear_pointer (&dialog, gtk_widget_unparent);
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

  /*Build ScanDialog*/
  build_scan_dialog (self);

  /* Map scan actions */
  GApplication *app = g_application_get_default();
	g_action_map_add_action_entries (G_ACTION_MAP (app),
	                                 scan_actions,
	                                 G_N_ELEMENTS (scan_actions),
	                                 self);
}
