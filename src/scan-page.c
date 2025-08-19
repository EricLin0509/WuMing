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

struct _ScanPage {
  GtkWidget          parent_instance;

  /*Child*/
  GtkWidget          *clamp;
  GtkButton          *scan_a_file_button;
  GtkButton          *scan_a_folder_button;
};

G_DEFINE_FINAL_TYPE (ScanPage, scan_page, GTK_TYPE_WIDGET)

/*Test functions*/

static void
show_chosen_file_path (GObject *source_object, GAsyncResult *res, gpointer data)
{
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *file = NULL;
  GError *error = NULL;

  if (file = gtk_file_dialog_open_finish (dialog, res, &error))
    {
      g_print("[Info] You chose %s\n", g_file_get_path (file));
    }
  else
    {
      if (error->code == GTK_DIALOG_ERROR_DISMISSED)
            g_warning ("[Info] User canceled!\n");
      else
            g_warning ("[Error] Failed to open the file!\n");
    }
}

static void
show_chosen_folder_path (GObject *source_object, GAsyncResult *res, gpointer data)
{
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *file = NULL;
  GError *error = NULL;

  if (file = gtk_file_dialog_select_folder_finish (dialog, res, &error))
    {
      g_print("[Info] You chose %s\n", g_file_get_path (file));
    }
  else
    {
      if (error->code == GTK_DIALOG_ERROR_DISMISSED)
            g_warning ("[Info] User canceled!\n");
      else
            g_warning ("[Error] Failed to open the folder!\n");
    }
}

/*Callbacks*/

static void
file_chooser (GtkButton* file_button, gpointer user_data)
{
  gtk_widget_set_sensitive (GTK_WIDGET (file_button), FALSE);
  g_print("[Info] Choose a file\n");

  GtkWidget *window = gtk_widget_get_ancestor (GTK_WIDGET (file_button), ADW_TYPE_APPLICATION_WINDOW);

  GtkFileDialog *dialog;
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_open (dialog, GTK_WINDOW (window), NULL, show_chosen_file_path, NULL);

  g_object_unref (dialog);
  gtk_widget_set_sensitive (GTK_WIDGET (file_button), TRUE);
}

static void
folder_chooser (GtkButton* folder_button, gpointer user_data)
{
  gtk_widget_set_sensitive (GTK_WIDGET (folder_button), FALSE);
  g_print("[Info] Choose a folder\n");

  GtkWidget *window = gtk_widget_get_ancestor (GTK_WIDGET (folder_button), ADW_TYPE_APPLICATION_WINDOW);

  GtkFileDialog *dialog;
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_select_folder (dialog, GTK_WINDOW (window), NULL, show_chosen_folder_path, NULL);

  g_object_unref (dialog);
  gtk_widget_set_sensitive (GTK_WIDGET (folder_button), TRUE);
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

static void
scan_page_init (ScanPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect (self->scan_a_file_button, "clicked", G_CALLBACK (file_chooser), NULL);
  g_signal_connect (self->scan_a_folder_button, "clicked", G_CALLBACK (folder_chooser), NULL);
}
