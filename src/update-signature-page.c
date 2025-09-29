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

#include "libs/systemd-control.h"

struct _UpdateSignaturePage {
  GtkWidget          parent_instance;

  /*Child*/
  GtkWidget          *clamp;
  AdwActionRow       *status_row;
  GtkButton          *update_button;
  AdwActionRow       *service_row;

  /* Private */
  AdwSpinnerPaintable *spinner;
  /* UpdatingPage */
  AdwNavigationPage  *update_nav_page;
  AdwStatusPage      *status_page;
  GtkButton          *cancel_button;
  gulong                cancel_button_signal_id;
  GtkButton          *close_button;
  gulong                close_button_signal_id;
};

G_DEFINE_FINAL_TYPE (UpdateSignaturePage, update_signature_page, GTK_TYPE_WIDGET)

void
update_signature_page_show_isuptodate(UpdateSignaturePage *self, const scan_result *result)
{
  GtkWidget *status_page = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_STATUS_PAGE);

  g_autofree char *signature_msg = NULL; // The title message for `AdwStatusPage`
  g_autofree char *date_msg = NULL; // The description message for `AdwStatusPage`
  g_autofree char *row_subtitle = NULL; // The subtitle message for `AdwActionRow`

  /* Using bit mask to check the status */
  switch (result->status)
  {
    case 0: // Signature is oudated
      adw_status_page_set_icon_name(ADW_STATUS_PAGE (status_page), "status-warning-symbolic");
      signature_msg = g_strdup_printf (gettext("Signature Is Outdated"));
      date_msg = g_strdup_printf (gettext("Current signature date: %d.%d.%d %d"), result->year, result->month, result->day, result->time);
      row_subtitle = g_strdup_printf (gettext("Outdated!"));
      break;
    case 1: // No signature found
      adw_status_page_set_icon_name(ADW_STATUS_PAGE (status_page), "status-warning-symbolic");
      signature_msg = g_strdup_printf (gettext("No Signature Found"));
      date_msg = g_strdup_printf (gettext("Warning: No signature found\nPlease update the signature now!"));
      row_subtitle = g_strdup_printf (gettext("No signature"));
      break;
    case 16: // Signature is up-to-date
      adw_status_page_set_icon_name(ADW_STATUS_PAGE (status_page), "status-ok-symbolic");
      signature_msg = g_strdup_printf (gettext("Signature Is Up To Date"));
      date_msg = g_strdup_printf (gettext("Current signature date: %d.%d.%d %d"), result->year, result->month, result->day, result->time);
      row_subtitle = g_strdup_printf (gettext("Is Up To Date"));
      break;
    default: // Bit mask is invalid (because these two bit mask cannot be set at the same time)
      adw_status_page_set_icon_name(ADW_STATUS_PAGE (status_page), "status-warning-symbolic");
      signature_msg = g_strdup_printf (gettext("Unknown Signature Status"));
      date_msg = g_strdup_printf (gettext("Unknown signature status: %d"), result->status);
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

  switch (is_enabled)
  {
    case 0: // Service is disabled
      adw_action_row_set_subtitle (self->service_row, gettext("Disabled"));
      break;
    case 1: // Service is enabled
      adw_action_row_set_subtitle (self->service_row, gettext("Enabled"));
      break;
    default: // Error
      adw_action_row_set_subtitle (self->service_row, gettext("Failed to check!"));
      break;
  }
}

static void
updating_page_reset(UpdateSignaturePage *self)
{
  /* Reset `AdwStatusPage` */
  adw_status_page_set_paintable(self->status_page, GDK_PAINTABLE (self->spinner));
  adw_status_page_set_title (self->status_page, gettext("Updating..."));
  adw_status_page_set_description (self->status_page, gettext("This might take a while"));

  /* Enable cancel button */
  gtk_widget_set_sensitive (GTK_WIDGET (self->cancel_button), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->cancel_button), TRUE);

  /* Disable close button */
  gtk_widget_set_sensitive (GTK_WIDGET (self->close_button), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->close_button), FALSE);

  /* Reset signal id */
  if (self->cancel_button_signal_id > 0)
  {
    g_signal_handler_disconnect(self->cancel_button, self->cancel_button_signal_id);
    self->cancel_button_signal_id = 0;
  }

  if(self->close_button_signal_id > 0)
  {
    g_signal_handler_disconnect(self->close_button, self->close_button_signal_id);
    self->close_button_signal_id = 0;
  }
}

static void
reset_updating_page_and_start_update(UpdateSignaturePage *self)
{
  /* Get `AdwNavigationView` */
  AdwNavigationView *view = ADW_NAVIGATION_VIEW(gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW));

  if (adw_navigation_view_get_visible_page(view) == self->update_nav_page) return; // No need to update when is updating

  g_print("[INFO] Update Signature\n");

  updating_page_reset(self);
  self->close_button_signal_id = g_signal_connect_swapped(self->close_button, "clicked", G_CALLBACK(adw_navigation_view_pop), view);

  adw_navigation_view_push (view, self->update_nav_page);

  /* Start update */
  start_update(self);
}

void
updating_page_show_final_result(UpdateSignaturePage *self, const char *message, const char *icon_name)
{
  adw_status_page_set_icon_name(self->status_page, icon_name);
  adw_status_page_set_title (self->status_page, message);
  adw_status_page_set_description (self->status_page, NULL);

  /* Enable close button */ 
  gtk_widget_set_sensitive (GTK_WIDGET (self->close_button), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->close_button), TRUE);

  /* Disable cancel button */
  gtk_widget_set_sensitive (GTK_WIDGET (self->cancel_button), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->cancel_button), FALSE);
}

static void
update_signature_cb (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  UpdateSignaturePage *self = UPDATE_SIGNATURE_PAGE(user_data);
  GtkButton *update_button = GTK_BUTTON(self->update_button);

  gtk_widget_set_sensitive (GTK_WIDGET (update_button), FALSE);

  /*Reset Widget and start update*/
  reset_updating_page_and_start_update(self);

  gtk_widget_set_sensitive (GTK_WIDGET (update_button), TRUE);
}

static void
build_updating_page(UpdateSignaturePage *self)
{
  GtkBuilder *builder = gtk_builder_new_from_resource("/com/ericlin/wuming/pages/updating-page.ui");
  self->update_nav_page = ADW_NAVIGATION_PAGE(gtk_builder_get_object(builder, "update_nav_page"));
  g_object_ref_sink(self->update_nav_page); // Keep a reference to the object
  self->status_page = ADW_STATUS_PAGE(gtk_builder_get_object(builder, "status_page"));
  self->cancel_button = GTK_BUTTON(gtk_builder_get_object(builder, "cancel_button"));
  self->close_button = GTK_BUTTON(gtk_builder_get_object(builder, "close_button"));
  g_object_unref(builder);
}

/*GObject Essential Functions */

static void
update_signature_page_dispose(GObject *gobject)
{
  UpdateSignaturePage *self = UPDATE_SIGNATURE_PAGE (gobject);

  GtkWidget *update_nav_page = GTK_WIDGET (self->update_nav_page); // Cast it for cleaning up

  if (self->close_button_signal_id > 0) g_signal_handler_disconnect(self->close_button, self->close_button_signal_id); // Remove signal id

  g_clear_object(&self->spinner);
  g_clear_pointer(&update_nav_page, gtk_widget_unparent);
  g_clear_pointer(&self->clamp, gtk_widget_unparent);

  G_OBJECT_CLASS(update_signature_page_parent_class)->dispose(gobject);
}

static void
update_signature_page_finalize(GObject *gobject)
{
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

static const GActionEntry update_actions[] = {
  { "update", update_signature_cb }
};

static void
update_signature_page_init (UpdateSignaturePage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  /*Build Updating Page*/
  build_updating_page(self);

  /* Create `AdwSpinnerPaintable` */
  self->spinner = adw_spinner_paintable_new(GTK_WIDGET (self->status_page));
  g_object_ref_sink(self->spinner); // Keep the reference to the object

  /* Map update action */
  GApplication *app = g_application_get_default();
	g_action_map_add_action_entries (G_ACTION_MAP (app),
	                                 update_actions,
	                                 G_N_ELEMENTS (update_actions),
	                                 self);
}
