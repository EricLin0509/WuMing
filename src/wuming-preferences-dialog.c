/* wuming-preferences-dialog.c
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

#include "wuming-window.h"
#include "wuming-preferences-dialog.h"

struct _WumingPreferencesDialog {
    GtkWidget parent_instance;

    GtkAdjustment *signature_expiry_days;

    /* Private */
    GtkWidget *window;
    GSettings *settings;
};

G_DEFINE_FINAL_TYPE(WumingPreferencesDialog, wuming_preferences_dialog, ADW_TYPE_PREFERENCES_DIALOG)

static void
show_dialog (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    WumingPreferencesDialog *dialog = WUMING_PREFERENCES_DIALOG (user_data);

    adw_dialog_present (ADW_DIALOG (dialog), dialog->window);
}

static void
wuming_preferences_dialog_on_expiration_changed (GtkAdjustment *adjustment, WumingPreferencesDialog *self)
{
    g_return_if_fail (self->settings != NULL);

    g_settings_set_int (self->settings, "signature-expiration-time", gtk_adjustment_get_value (adjustment));

    wuming_window_update_signature_status (WUMING_WINDOW (self->window), FALSE);
}

static void
wuming_preferences_dialog_init_settings (WumingPreferencesDialog *self)
{
    g_return_if_fail (self->settings != NULL);

    g_settings_bind (self->settings, "signature-expiration-time", self->signature_expiry_days, "value", G_SETTINGS_BIND_DEFAULT);

    g_signal_connect (self->signature_expiry_days, "value-changed", G_CALLBACK (wuming_preferences_dialog_on_expiration_changed), self);
}

/* GObject essential functions */

static const GActionEntry preferences_actions[] = {
    { "preferences", show_dialog }
};

static void
wuming_preferences_dialog_class_init (WumingPreferencesDialogClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);

    gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/wuming-preferences-dialog.ui");

    gtk_widget_class_bind_template_child (widget_class, WumingPreferencesDialog, signature_expiry_days);
}

WumingPreferencesDialog *
wuming_preferences_dialog_new (GtkWidget *window)
{
    WumingPreferencesDialog *self = WUMING_PREFERENCES_DIALOG (g_object_new (WUMING_TYPE_PREFERENCES_DIALOG, NULL));

    self->window = window;
    self->settings = wuming_window_get_component (WUMING_WINDOW (window), "settings");

    wuming_preferences_dialog_init_settings (self);

    return self;
}

static void
wuming_preferences_dialog_init (WumingPreferencesDialog *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    GApplication *app = g_application_get_default();
    g_action_map_add_action_entries (G_ACTION_MAP (app),
                                     preferences_actions,
                                     G_N_ELEMENTS (preferences_actions),
                                    self);
}