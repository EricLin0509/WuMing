/* updating-page.c
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

#include "updating-page.h"

struct _UpdatingPage {
    GtkWidget parent_instance;

    /* Child */
    AdwToolbarView *toolbar_view;
    GtkButton *cancel_button;
    AdwStatusPage *status_page;
    GtkButton *close_button;

    /* Private */
    AdwSpinnerPaintable *spinner;
};

G_DEFINE_FINAL_TYPE(UpdatingPage, updating_page, GTK_TYPE_WIDGET)

void
updating_page_reset (UpdatingPage *self)
{
    adw_status_page_set_paintable(self->status_page, GDK_PAINTABLE(self->spinner));
    adw_status_page_set_title(self->status_page, gettext("Updating..."));
    adw_status_page_set_description(self->status_page, gettext("This might take a while"));

    /* Enable the cancel button */
    gtk_widget_set_sensitive(GTK_WIDGET(self->cancel_button), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(self->cancel_button), TRUE);

    /* Disable the close button */
    gtk_widget_set_sensitive(GTK_WIDGET(self->close_button), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(self->close_button), FALSE);
}

void
updating_page_set_final_result (UpdatingPage *self, const char *result, const char *icon_name)
{
    adw_status_page_set_title(self->status_page, result);
    adw_status_page_set_description(self->status_page, NULL);
    adw_status_page_set_icon_name(self->status_page, icon_name);

    /* Disable the cancel button */
    gtk_widget_set_sensitive(GTK_WIDGET(self->cancel_button), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(self->cancel_button), FALSE);

    /* Enable the close button */
    gtk_widget_set_sensitive(GTK_WIDGET(self->close_button), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(self->close_button), TRUE);
}

/* GObject essential functions */
static void
updating_page_dispose(GObject *object)
{
    UpdatingPage *self = UPDATING_PAGE(object);

    GtkWidget *toolbar_view = GTK_WIDGET(self->toolbar_view);

    g_clear_object(&self->spinner);

    g_clear_pointer(&toolbar_view, gtk_widget_unparent);

    G_OBJECT_CLASS(updating_page_parent_class)->dispose(object);
}

static void
updating_page_finalize(GObject *object)
{
    G_OBJECT_CLASS(updating_page_parent_class)->finalize(object);
}

static void
updating_page_class_init(UpdatingPageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = updating_page_dispose;
    object_class->finalize = updating_page_finalize;

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);

    gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/pages/updating-page.ui");

    gtk_widget_class_bind_template_child (widget_class, UpdatingPage, toolbar_view);
    gtk_widget_class_bind_template_child (widget_class, UpdatingPage, cancel_button);
    gtk_widget_class_bind_template_child (widget_class, UpdatingPage, status_page);
    gtk_widget_class_bind_template_child (widget_class, UpdatingPage, close_button);
}

static void
updating_page_init(UpdatingPage *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    self->spinner = adw_spinner_paintable_new(GTK_WIDGET(self->status_page));
    g_object_ref_sink(self->spinner); // Keep the spinner alive

    updating_page_reset(self); // Reset the status page
}

GtkWidget *
updating_page_new(void)
{
    return g_object_new(UPDATING_TYPE_PAGE, NULL);
}