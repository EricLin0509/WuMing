/* threat-page.c
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

#include "libs/delete-file.h"

#include "wuming-window.h"
#include "scanning-page.h"
#include "threat-page.h"

struct _ThreatPage {
    GtkWidget parent_instance;

    AdwToolbarView *toolbar_view;
    GtkButton *delete_all_button;
    GtkListBox *threat_list;

    /* Private */
    AdwDialog *alert_dialog;
};

G_DEFINE_FINAL_TYPE(ThreatPage, threat_page, GTK_TYPE_WIDGET)

void
threat_page_add_threat (ThreatPage *self, GtkWidget *row)
{
    gtk_list_box_prepend (self->threat_list, GTK_WIDGET (row));
}

void
threat_page_remove_threat (ThreatPage *self, GtkWidget *row)
{
    gtk_list_box_remove (self->threat_list, row);

    if (gtk_list_box_get_row_at_index (self->threat_list, 0) != NULL) return;

    // If the list is empty, pop this page from the stack
    WumingWindow *window = WUMING_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), WUMING_TYPE_WINDOW));
    wuming_window_pop_page (window);

    ScanningPage *scanning_page = wuming_window_get_component (window, "scanning_page");
    scanning_page_disable_threat_button (scanning_page);
    scanning_page_set_final_result (scanning_page, FALSE, gettext("All Clear"), gettext("All threats have been removed!"), "status-ok-symbolic");
}

void
threat_page_clear (ThreatPage *self)
{
    gtk_list_box_remove_all (self->threat_list); // Remove all items from the list
}

static void
on_alert_dialog_response (AdwAlertDialog *dialog, GAsyncResult *result, gpointer user_data)
{
    const char *response = adw_alert_dialog_choose_finish (dialog, result);

    if (g_strcmp0 (response, "delete_all") == 0) delete_all_threat_files ();
}

static void
show_alert_dialog (ThreatPage *self)
{
    adw_alert_dialog_choose (ADW_ALERT_DIALOG (self->alert_dialog), GTK_WIDGET (self), NULL, (GAsyncReadyCallback) on_alert_dialog_response, NULL);
}

/* GObject essential functions */

static void
threat_page_dispose (GObject *object)
{
    ThreatPage *self = THREAT_PAGE (object);

    GtkWidget *toolbar_view = GTK_WIDGET (self->toolbar_view);

    g_clear_object (&self->alert_dialog);
    threat_page_clear (self);
    g_clear_pointer (&toolbar_view, gtk_widget_unparent);

    G_OBJECT_CLASS (threat_page_parent_class)->dispose(object);
}

static void
threat_page_finalize (GObject *object)
{
    ThreatPage *self = THREAT_PAGE (object);

    /* Reset all child widgets */
    self->toolbar_view = NULL;
    self->delete_all_button = NULL;
    self->threat_list = NULL;
    self->alert_dialog = NULL;

    G_OBJECT_CLASS (threat_page_parent_class)->finalize(object);
}

static void
threat_page_class_init (ThreatPageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = threat_page_dispose;
    object_class->finalize = threat_page_finalize;

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);

    gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/pages/threat-page.ui");

    gtk_widget_class_bind_template_child (widget_class, ThreatPage, toolbar_view);
    gtk_widget_class_bind_template_child (widget_class, ThreatPage, delete_all_button);
    gtk_widget_class_bind_template_child (widget_class, ThreatPage, threat_list);
}

static AdwDialog *
build_alert_dialog (void)
{
    AdwDialog *alert_dialog = adw_alert_dialog_new (gettext("Delete All Threats?"),
                                        gettext("This will delete all threat files and cannot be undone. Are you sure?"));

    adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (alert_dialog),
                                    "cancel", gettext("Cancel"),
                                    "delete_all", gettext("Delete All"),
                                    NULL);

    adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (alert_dialog),
                                              "delete_all", ADW_RESPONSE_DESTRUCTIVE);

    adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert_dialog), "cancel");
    adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (alert_dialog), "cancel");

    return alert_dialog;
}

static void
threat_page_init (ThreatPage *self)
{
    gtk_widget_init_template (GTK_WIDGET(self));

    self->alert_dialog = build_alert_dialog ();
    g_object_ref_sink (self->alert_dialog);

    g_signal_connect_swapped (self->delete_all_button, "clicked", G_CALLBACK (show_alert_dialog), self);
}

GtkWidget *
threat_page_new (void)
{
    return g_object_new (THREAT_TYPE_PAGE, NULL);
}