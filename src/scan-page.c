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
  AdwBin             parent_instance;

  /*Child*/
  AdwStatusPage      *status_page;
  AdwClamp           *clamp;
  GtkBox             *box_main;
  GtkButton          *scan_a_file_button;
  GtkButton          *scan_a_folder_button;
};

enum {
  PROP_0,
  PROP_CLAMP_MAXIMUM_SIZE,
  PROP_CLAMP_TIGHTENING_THRESHOLD,
  PROP_BOX_MAIN_ORIENTATION,
  PROP_BOX_MAIN_MARGIN_BOTTOM,
  PROP_BOX_MAIN_SPACING,
  PROP_BOX_MAIN_HEIGHT_REQUEST,
  N_PROPERTIES
};

static GParamSpec *properties [N_PROPERTIES];

G_DEFINE_FINAL_TYPE (ScanPage, scan_page, ADW_TYPE_BIN)

static void
scan_page_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  ScanPage *self = SCAN_PAGE (object);

  switch (prop_id)
    {
    case PROP_CLAMP_MAXIMUM_SIZE:
      g_value_set_int (value, adw_clamp_get_maximum_size (self->clamp));
      break;
    case PROP_CLAMP_TIGHTENING_THRESHOLD:
      g_value_set_int (value, adw_clamp_get_tightening_threshold (self->clamp));
      break;
    case PROP_BOX_MAIN_ORIENTATION:
      g_value_set_enum (value, gtk_orientable_get_orientation (GTK_ORIENTABLE (self->box_main)));
      break;
    case PROP_BOX_MAIN_MARGIN_BOTTOM:
      g_value_set_int (value, gtk_widget_get_margin_bottom (GTK_WIDGET (self->box_main)));
      break;
    case PROP_BOX_MAIN_SPACING:
      g_value_set_int (value, gtk_box_get_spacing (self->box_main));
      break;
    case PROP_BOX_MAIN_HEIGHT_REQUEST:
      {
        int height;
        gtk_widget_get_size_request (GTK_WIDGET (self->box_main), NULL, &height);
        g_value_set_int (value, height);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
scan_page_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  ScanPage *self = SCAN_PAGE (object);

  switch (prop_id)
    {
    case PROP_CLAMP_MAXIMUM_SIZE:
      adw_clamp_set_maximum_size (self->clamp, g_value_get_int (value));
      break;
    case PROP_CLAMP_TIGHTENING_THRESHOLD:
      adw_clamp_set_tightening_threshold (self->clamp, g_value_get_int (value));
      break;
    case PROP_BOX_MAIN_ORIENTATION:
      gtk_orientable_set_orientation (GTK_ORIENTABLE (self->box_main), g_value_get_enum (value));
      break;
    case PROP_BOX_MAIN_MARGIN_BOTTOM:
      gtk_widget_set_margin_bottom (GTK_WIDGET (self->box_main), g_value_get_int (value));
      break;
    case PROP_BOX_MAIN_SPACING:
      gtk_box_set_spacing (self->box_main, g_value_get_int (value));
      break;
    case PROP_BOX_MAIN_HEIGHT_REQUEST:
      gtk_widget_set_size_request (GTK_WIDGET (self->box_main), -1, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

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
  gobject_class->get_property = scan_page_get_property;
  gobject_class->set_property = scan_page_set_property;

  properties [PROP_CLAMP_MAXIMUM_SIZE] =
    g_param_spec_int ("clamp-maximum-size", NULL, NULL,
                      0, G_MAXINT, 450,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties [PROP_CLAMP_TIGHTENING_THRESHOLD] =
    g_param_spec_int ("clamp-tightening-threshold", NULL, NULL,
                      0, G_MAXINT, 300,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties [PROP_BOX_MAIN_ORIENTATION] =
    g_param_spec_enum ("box-main-orientation", NULL, NULL,
                       GTK_TYPE_ORIENTATION, GTK_ORIENTATION_HORIZONTAL,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties [PROP_BOX_MAIN_MARGIN_BOTTOM] =
    g_param_spec_int ("box-main-margin-bottom", NULL, NULL,
                      0, G_MAXINT, 60,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties [PROP_BOX_MAIN_SPACING] =
    g_param_spec_int ("box-main-spacing", NULL, NULL,
                      0, G_MAXINT, 40,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties [PROP_BOX_MAIN_HEIGHT_REQUEST] =
    g_param_spec_int ("box-main-height-request", NULL, NULL,
                      -1, G_MAXINT, 60,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);

  gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);

  gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/pages/scan-page.ui");

  gtk_widget_class_bind_template_child (widget_class, ScanPage, status_page);
  gtk_widget_class_bind_template_child (widget_class, ScanPage, clamp);
  gtk_widget_class_bind_template_child (widget_class, ScanPage, box_main);
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
