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

  /*UpdateDialog*/
  AdwDialog          *dialog;
  GtkWidget          *update_status;
  GtkWidget          *close_button;

  /*Child*/
  GtkWidget          *clamp;
  AdwActionRow       *status_row;
  GtkButton          *update_button;
  AdwActionRow       *service_row;
};

G_DEFINE_FINAL_TYPE (UpdateSignaturePage, update_signature_page, GTK_TYPE_WIDGET)

void
update_signature_page_show_date(UpdateSignaturePage *self, scan_result result)
{
  GtkWidget *page = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_STATUS_PAGE);

  g_autofree char *message = NULL; // use g_autofree to avoid memory leak

  if (!result.is_success)
    {
      message = g_strdup_printf (gettext("Warning! No signature found! Please Update the signature!"));
      adw_status_page_set_description (ADW_STATUS_PAGE (page), message);
      return;
    }

  if (result.is_warning)
    {
      message = g_strdup_printf (gettext ("Warning! Only found main.cvd! Date: %d.%d.%d %d"), result.year, result.month, result.day, result.time);
      adw_status_page_set_description (ADW_STATUS_PAGE (page), message);
      return;
    }
  message = g_strdup_printf (gettext("Current signature date: %d.%d.%d %d"), result.year, result.month, result.day, result.time);
  adw_status_page_set_description (ADW_STATUS_PAGE (page), message);
  return;
}

void
update_signature_page_show_isuptodate(UpdateSignaturePage *self, bool is_uptodate)
{
  if (is_uptodate)adw_action_row_set_subtitle (self->status_row, gettext("Is up to date"));
  else 
  {
    adw_action_row_set_subtitle (self->status_row, gettext("Outdated!"));
    gtk_widget_add_css_class (GTK_WIDGET (self->update_button), "suggested-action");
  }
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
reset_update_dialog_and_start_update(UpdateSignaturePage *self)
{
  adw_status_page_set_title (ADW_STATUS_PAGE (self->update_status), gettext("Updating..."));
  adw_status_page_set_description (ADW_STATUS_PAGE (self->update_status), gettext("Asking for permission..."));
  gtk_widget_set_visible(self->close_button, FALSE);
  gtk_widget_set_sensitive (self->close_button, FALSE);

  adw_dialog_present (self->dialog, GTK_WIDGET (self));
  start_update(self->dialog, self->update_status, self->close_button, GTK_WIDGET (self->update_button));
}

static void
update_signature_cb (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  UpdateSignaturePage *self = UPDATE_SIGNATURE_PAGE(user_data);
  GtkButton *update_button = GTK_BUTTON(self->update_button);

  gtk_widget_set_sensitive (GTK_WIDGET (update_button), FALSE);
  g_print("[INFO] Update Signature\n");

  /*Reset Widget and start update*/
  reset_update_dialog_and_start_update(self);

  gtk_widget_set_sensitive (GTK_WIDGET (update_button), TRUE);
}

static void
build_update_dialog(UpdateSignaturePage *self)
{
  /* Create dialog */
  self->dialog = g_object_ref_sink(adw_dialog_new());
  adw_dialog_set_can_close(ADW_DIALOG (self->dialog), FALSE);
  adw_dialog_set_content_height (self->dialog, 320);
  adw_dialog_set_content_width (self->dialog, 420);

  /* Create header */
  GtkWidget *toolbar = adw_toolbar_view_new();
  GtkWidget *header = adw_header_bar_new();
  GtkWidget *title = gtk_label_new(gettext("Update Signature"));
  gtk_widget_add_css_class(title, "heading");
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title);
  adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), FALSE); // Hide the buttons on the right side
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

  adw_dialog_set_child(self->dialog, toolbar);

  /* Update Status */
  self->update_status = adw_status_page_new ();
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), self->update_status);

  self->close_button = gtk_button_new ();
  gtk_widget_set_halign (self->close_button, GTK_ALIGN_CENTER);
  gtk_button_set_label (GTK_BUTTON (self->close_button), gettext("Close"));
  adw_status_page_set_child (ADW_STATUS_PAGE (self->update_status), self->close_button);

  // Connect close button signal
  g_signal_connect_swapped(GTK_BUTTON(self->close_button), "clicked", G_CALLBACK(adw_dialog_force_close), ADW_DIALOG (self->dialog));
}

/*GObject Essential Functions */

static void
update_signature_page_dispose(GObject *gobject)
{
  UpdateSignaturePage *self = UPDATE_SIGNATURE_PAGE (gobject);

  GtkWidget *dialog = GTK_WIDGET (self->dialog); // Cast it for cleaning up

  g_clear_pointer(&dialog, gtk_widget_unparent);
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

  /*Build UpdateDialog*/
  build_update_dialog(self);

  /* Map update action */
  GApplication *app = g_application_get_default();
	g_action_map_add_action_entries (G_ACTION_MAP (app),
	                                 update_actions,
	                                 G_N_ELEMENTS (update_actions),
	                                 self);
}
