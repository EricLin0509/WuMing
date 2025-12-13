/* scanning-page.c
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

#include "scanning-page.h"

struct _ScanningPage {
    GtkWidget parent_instance;

    /* Child */
    AdwToolbarView *toolbar_view;
    GtkButton *cancel_button;
    gulong cancel_button_handler_id;
    AdwStatusPage *status_page;
    GtkButton *threat_button;
    GtkButton *close_button;

    /* Private */
    AdwSpinnerPaintable *spinner;
};

G_DEFINE_FINAL_TYPE(ScanningPage, scanning_page, GTK_TYPE_WIDGET)

void
scanning_page_disable_threat_button (ScanningPage *self)
{
    gtk_widget_set_sensitive(GTK_WIDGET(self->threat_button), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(self->threat_button), FALSE);
}

void
scanning_page_reset (ScanningPage *self)
{
    adw_status_page_set_title(self->status_page, gettext("Scanning..."));
    adw_status_page_set_description(self->status_page, gettext("Preparing..."));
    adw_status_page_set_paintable(self->status_page, GDK_PAINTABLE(self->spinner));

    /* Set can-pop property to false */
    AdwNavigationPage *page = ADW_NAVIGATION_PAGE(gtk_widget_get_ancestor(GTK_WIDGET(self->status_page), ADW_TYPE_NAVIGATION_PAGE));
    if (page != NULL) adw_navigation_page_set_can_pop(page, FALSE);

    /* Disable close button */
    gtk_widget_set_sensitive(GTK_WIDGET(self->close_button), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(self->close_button), FALSE);

    /* Disable threat button */
    scanning_page_disable_threat_button(self);

    /* Enable cancel button */
    gtk_widget_set_sensitive(GTK_WIDGET(self->cancel_button), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(self->cancel_button), TRUE);
}

void
scanning_page_set_progress (ScanningPage *self, const char *progress)
{
    adw_status_page_set_description(self->status_page, progress);
}

void
scanning_page_set_final_result (ScanningPage *self, gboolean has_threat, const char *result, const char *detail, const char *icon_name)
{
    if (result) adw_status_page_set_title(self->status_page, result);
    if (detail) adw_status_page_set_description(self->status_page, detail);
    if (icon_name) adw_status_page_set_icon_name(self->status_page, icon_name);

    /* Set can-pop property to true */
    AdwNavigationPage *page = ADW_NAVIGATION_PAGE(gtk_widget_get_ancestor(GTK_WIDGET(self->status_page), ADW_TYPE_NAVIGATION_PAGE));
    if (page != NULL) adw_navigation_page_set_can_pop(page, TRUE);

    /* Disable cancel button */
    gtk_widget_set_sensitive(GTK_WIDGET(self->cancel_button), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(self->cancel_button), FALSE);

    /* Enable close button */
    gtk_widget_set_sensitive(GTK_WIDGET(self->close_button), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(self->close_button), TRUE);

    /* Show threat button if has threat */
    if (!has_threat) return; // No need to show threat button if no threat found
    gtk_widget_set_sensitive(GTK_WIDGET(self->threat_button), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(self->threat_button), TRUE);
}

void
scanning_page_set_cancel_signal (ScanningPage *self, GCallback cancel_signal_cb, gpointer user_data)
{
    self->cancel_button_handler_id = g_signal_connect_swapped(self->cancel_button, "clicked", cancel_signal_cb, user_data);
}

void
scanning_page_revoke_cancel_signal (ScanningPage *self)
{
    if (self->cancel_button_handler_id == 0) return; // No need to revoke if not connected to any signal

    g_signal_handler_disconnect(self->cancel_button, self->cancel_button_handler_id);
    self->cancel_button_handler_id = 0;
}

/* GObject essential functions */

static void
scanning_page_dispose(GObject *object)
{
    ScanningPage *self = SCANNING_PAGE(object);

    scanning_page_revoke_cancel_signal(self);

    GtkWidget *toolbar_view = GTK_WIDGET(self->toolbar_view);

    g_clear_object(&self->spinner);
    g_clear_pointer(&toolbar_view, gtk_widget_unparent);

    G_OBJECT_CLASS(scanning_page_parent_class)->dispose(object);
}

static void
scanning_page_finalize(GObject *object)
{
    ScanningPage *self = SCANNING_PAGE(object);

    /* Reset all child widgets */
    self->toolbar_view = NULL;
    self->cancel_button = NULL;
    self->status_page = NULL;
    self->threat_button = NULL;
    self->close_button = NULL;
    self->spinner = NULL;

    G_OBJECT_CLASS(scanning_page_parent_class)->finalize(object);
}

static void
scanning_page_class_init(ScanningPageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = scanning_page_dispose;
    object_class->finalize = scanning_page_finalize;

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);

    gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/pages/scanning-page.ui");

    gtk_widget_class_bind_template_child (widget_class, ScanningPage, toolbar_view);
    gtk_widget_class_bind_template_child (widget_class, ScanningPage, cancel_button);
    gtk_widget_class_bind_template_child (widget_class, ScanningPage, status_page);
    gtk_widget_class_bind_template_child (widget_class, ScanningPage, threat_button);
    gtk_widget_class_bind_template_child (widget_class, ScanningPage, close_button);
}

static void
scanning_page_init(ScanningPage *self)
{
    gtk_widget_init_template (GTK_WIDGET(self));

    self->spinner = adw_spinner_paintable_new(GTK_WIDGET(self->status_page));
    g_object_ref_sink(self->spinner); // Keep the spinner alive

    scanning_page_reset(self);
}

GtkWidget *
scanning_page_new (void)
{
    return g_object_new (SCANNING_TYPE_PAGE, NULL);
}