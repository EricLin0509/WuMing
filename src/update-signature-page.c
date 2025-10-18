/* update-signature-page.c
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

#include "update-signature-page.h"
#include "wuming-window.h"
#include "updating-page.h"

#include "libs/systemd-control.h"
#include "libs/update-signature.h"

struct _UpdateSignaturePage {
  GtkWidget          parent_instance;

  /*Child*/
  GtkWidget          *clamp;
  AdwActionRow       *status_row;
  GtkButton          *update_button;
  AdwActionRow       *service_row;
};

G_DEFINE_FINAL_TYPE (UpdateSignaturePage, update_signature_page, GTK_TYPE_WIDGET)

void
update_signature_page_show_isuptodate(UpdateSignaturePage *self, const signature_status *result)
{
  GtkWidget *status_page = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_STATUS_PAGE);

  g_autofree char *signature_msg = NULL; // The title message for `AdwStatusPage`
  g_autofree char *date_msg = NULL; // The description message for `AdwStatusPage`
  g_autofree char *row_subtitle = NULL; // The subtitle message for `AdwActionRow`

  int year, month, day, hour, minute;
  signature_status_get_date(result, &year, &month, &day, &hour, &minute);

  unsigned short status = signature_status_get_status(result);

  /* Using bit mask to check the status */
  switch (status)
  {
    case 0: // Signature is oudated
      adw_status_page_set_icon_name(ADW_STATUS_PAGE (status_page), "status-warning-symbolic");
      signature_msg = g_strdup_printf (gettext("Signature Is Outdated"));
      date_msg = g_strdup_printf (gettext("Current signature date: %d.%d.%d %d:%d"), year, month, day, hour, minute);
      row_subtitle = g_strdup_printf (gettext("Outdated!"));
      break;
    case 1: // No signature found
      adw_status_page_set_icon_name(ADW_STATUS_PAGE (status_page), "status-error-symbolic");
      signature_msg = g_strdup_printf (gettext("No Signature Found"));
      date_msg = g_strdup_printf (gettext("Warning: No signature found\nPlease update the signature now!"));
      row_subtitle = g_strdup_printf (gettext("No signature"));
      break;
    case 16: // Signature is up-to-date
      adw_status_page_set_icon_name(ADW_STATUS_PAGE (status_page), "status-ok-symbolic");
      signature_msg = g_strdup_printf (gettext("Signature Is Up To Date"));
      date_msg = g_strdup_printf (gettext("Current signature date: %d.%d.%d %d:%d"), year, month, day, hour, minute);
      row_subtitle = g_strdup_printf (gettext("Is Up To Date"));
      break;
    default: // Bit mask is invalid (because these two bit mask cannot be set at the same time)
      adw_status_page_set_icon_name(ADW_STATUS_PAGE (status_page), "status-error-symbolic");
      signature_msg = g_strdup_printf (gettext("Unknown Signature Status"));
      date_msg = g_strdup_printf (gettext("Signature status: %d"), status);
      row_subtitle = g_strdup_printf (gettext("Unknown Signature Status"));
      break;
  }

  adw_status_page_set_title (ADW_STATUS_PAGE (status_page), signature_msg);
  adw_status_page_set_description (ADW_STATUS_PAGE (status_page), date_msg);

  adw_action_row_set_subtitle (self->status_row, row_subtitle);
}

void
update_signature_page_show_servicestat(UpdateSignaturePage *self)
{
  const char *service = "clamav-freshclam.service";
  int is_enabled = is_service_enabled(service);

  if (is_enabled == 1) adw_action_row_set_subtitle (self->service_row, gettext("Enabled"));
  else if (is_enabled == 0) adw_action_row_set_subtitle (self->service_row, gettext("Disabled"));
  else adw_action_row_set_subtitle (self->service_row, gettext("Failed to check!"));
}

static void
update_signature_cb (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  UpdateSignaturePage *self = UPDATE_SIGNATURE_PAGE(user_data);
  WumingWindow *window = WUMING_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), WUMING_TYPE_WINDOW));
  SecurityOverviewPage *security_overview_page = SECURITY_OVERVIEW_PAGE (wuming_window_get_security_overview_page (window));
  UpdatingPage *updating_page = UPDATING_PAGE (wuming_window_get_updating_page (window));

  if (wuming_window_is_current_page_tag (window, "updating_nav_page")) return; // No need to update signature if is in updating page

  g_print("[INFO] Update Signature\n");

  updating_page_reset (updating_page);

  wuming_window_push_page_by_tag (window, "updating_nav_page");

  start_update(window, security_overview_page, self, updating_page);
}

/*GObject Essential Functions */

static const GActionEntry update_actions[] = {
  { "update", update_signature_cb }
};

static void
update_signature_page_dispose(GObject *gobject)
{
  UpdateSignaturePage *self = UPDATE_SIGNATURE_PAGE (gobject);
  GApplication *app = g_application_get_default();

  g_action_map_remove_action_entries (G_ACTION_MAP (app),
	                                 update_actions,
	                                 G_N_ELEMENTS (update_actions));

  g_clear_pointer(&self->clamp, gtk_widget_unparent);

  G_OBJECT_CLASS(update_signature_page_parent_class)->dispose(gobject);
}

static void
update_signature_page_finalize(GObject *gobject)
{
  UpdateSignaturePage *self = UPDATE_SIGNATURE_PAGE (gobject);

  /* Reset all child widgets */
  self->clamp = NULL;
  self->status_row = NULL;
  self->update_button = NULL;
  self->service_row = NULL;

  G_OBJECT_CLASS(update_signature_page_parent_class)->finalize(gobject);
}

static void
update_signature_page_class_init (UpdateSignaturePageClass *klass)
{
  	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->dispose = update_signature_page_dispose;
    gobject_class->finalize = update_signature_page_finalize;

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);

	  gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/pages/update-signature-page.ui");

    gtk_widget_class_bind_template_child (widget_class, UpdateSignaturePage, clamp);
    gtk_widget_class_bind_template_child (widget_class, UpdateSignaturePage, status_row);
    gtk_widget_class_bind_template_child (widget_class, UpdateSignaturePage, update_button);
    gtk_widget_class_bind_template_child (widget_class, UpdateSignaturePage, service_row);
}

GtkWidget *
update_signature_page_new (void)
{
  return g_object_new (UPDATE_SIGNATURE_TYPE_PAGE, NULL);
}

static void
update_signature_page_init (UpdateSignaturePage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  /* Map update action */
  GApplication *app = g_application_get_default();
	g_action_map_add_action_entries (G_ACTION_MAP (app),
	                                 update_actions,
	                                 G_N_ELEMENTS (update_actions),
	                                 self);
}
