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
  AdwStatusPage      *status_page;
  GtkButton          *scan_a_file_button;
  GtkButton          *scan_a_folder_button;
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

/*GObject Essential Functions */

static void
scan_page_dispose(GObject *gobject)
{
  ScanPage *self = SCAN_PAGE (gobject);

  GApplication *app = g_application_get_default();

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
}
