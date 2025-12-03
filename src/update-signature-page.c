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

  g_autofree char *date_msg = NULL; // The description message for `AdwStatusPage` (This need autofree because it will be used in `g_strdup_printf`)

  char *signature_msg = NULL; // The title message for `AdwStatusPage`
  char *row_subtitle = NULL; // The subtitle message for `AdwActionRow`
  char *button_style = NULL; // The style of `update_button`
  char *icon_name = NULL; // The icon name for `AdwStatusPage`

  int year, month, day, hour, minute;
  signature_status_get_date(result, &year, &month, &day, &hour, &minute);

  unsigned short status = signature_status_get_status(result);

  /* Using bit mask to check the status */
  switch (status)
  {
    case 0: // Signature is oudated
      date_msg = g_strdup_printf (gettext("Current Signature Date: %4d.%02d.%02d %02d:%02d"), year, month, day, hour, minute);

      signature_msg = gettext("Signature Is Outdated");
      row_subtitle = gettext("Outdated!");
      button_style = "button-suggestion";
      icon_name = "status-warning-symbolic";
      break;
    case SIGNATURE_STATUS_NOT_FOUND: // No signature found
      date_msg = g_strdup (gettext("Warning: No signature found\nPlease update the signature now!"));

      signature_msg = gettext("No Signature Found");
      row_subtitle = gettext("Signature Not Found");
      button_style = "button-suggestion";
      icon_name = "status-error-symbolic";
      break;
    case SIGNATURE_STATUS_UPTODATE: // Signature is up-to-date
      date_msg = g_strdup_printf (gettext("Current Signature Date: %4d.%02d.%02d %02d:%02d"), year, month, day, hour, minute);

      signature_msg = gettext("Signature Is Up To Date");
      row_subtitle = gettext("Is Up To Date");
      button_style = "button-default";
      icon_name = "status-ok-symbolic";
      break;
    default: // Bit mask is invalid (because these two bit mask cannot be set at the same time)
      date_msg = g_strdup_printf (gettext("Signature Status: %d"), status);

      signature_msg = gettext("Unknown Signature Status");
      row_subtitle = gettext("Unknown Signature Status");
      button_style = "button-suggestion";
      icon_name = "status-error-symbolic";
      break;
  }

  /* Remove the old style class and add the new one */
  gtk_widget_remove_css_class (GTK_WIDGET (self->update_button), "button-default");
  gtk_widget_remove_css_class (GTK_WIDGET (self->update_button), "button-suggestion");

  gtk_widget_add_css_class (GTK_WIDGET (self->update_button), button_style);

  adw_status_page_set_title (ADW_STATUS_PAGE (status_page), signature_msg);
  adw_status_page_set_description (ADW_STATUS_PAGE (status_page), date_msg);
  adw_status_page_set_icon_name (ADW_STATUS_PAGE (status_page), icon_name);

  adw_action_row_set_subtitle (self->status_row, row_subtitle);
}

void
update_signature_page_show_servicestat(UpdateSignaturePage *self, int service_status)
{
  if (service_status == 1) adw_action_row_set_subtitle (self->service_row, gettext("Enabled"));
  else if (service_status == 0) adw_action_row_set_subtitle (self->service_row, gettext("Disabled"));
  else adw_action_row_set_subtitle (self->service_row, gettext("Failed to check!"));
}

static void
update_signature_cb (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  UpdateSignaturePage *self = UPDATE_SIGNATURE_PAGE(user_data);
  WumingWindow *window = WUMING_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), WUMING_TYPE_WINDOW));
  UpdateContext *context = (UpdateContext *)wuming_window_get_component(window, "update_context");

  if (!wuming_window_is_in_main_page (window)) return; // Prevent multiple tasks running at the same time

  g_print("[INFO] Update Signature\n");

  start_update(context);
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
