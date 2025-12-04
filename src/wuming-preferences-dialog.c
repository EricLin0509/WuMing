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

#include "libs/scan-options-configs.h"

#include "wuming-window.h"
#include "wuming-preferences-dialog.h"

struct _WumingPreferencesDialog {
    GtkWidget parent_instance;

    AdwSwitchRow *enable_large_file;
    AdwSwitchRow *enable_pua;
    AdwSwitchRow *scan_archives;
    AdwSwitchRow *scan_mail;
    AdwSwitchRow *alert_exceeds_max;
    AdwSwitchRow *alert_encrypted;

    GtkAdjustment *signature_expiry_days;

    /* Private */
    WumingWindow *window;
    GSettings *settings;
};

enum {
  PROP_0,
  PROP_WINDOW,
  N_PROPS
};

G_DEFINE_FINAL_TYPE(WumingPreferencesDialog, wuming_preferences_dialog, ADW_TYPE_PREFERENCES_DIALOG)

static GParamSpec *properties [N_PROPS];

static void
show_dialog (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    WumingPreferencesDialog *dialog = WUMING_PREFERENCES_DIALOG (user_data);

    if (!wuming_window_is_in_main_page(dialog->window)) return; // Prevent multiple tasks running at the same time

    adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (dialog->window));
}

static void
on_signature_expiration_changed (GtkAdjustment *adjustment, WumingPreferencesDialog *self)
{
    g_return_if_fail (self->settings != NULL);

    gint new_value = gtk_adjustment_get_value (adjustment);

    g_settings_set_int (self->settings, "signature-expiration-time", new_value);

    wuming_window_update_signature_status (self->window, FALSE, new_value);
}

static void
on_scan_options_changed (AdwSwitchRow *row, GParamSpec *pspec, WumingPreferencesDialog *self)
{
    g_return_if_fail (self->settings != NULL);

    int bitmask = g_settings_get_int (self->settings, "scan-options-bitmask");

    AdwSwitchRow *rows[] = {
        self->enable_large_file,
        self->enable_pua,
        self->scan_archives,
        self->scan_mail,
        self->alert_exceeds_max,
        self->alert_encrypted,
        NULL
    };

    int options_bit = 1;
    for (int i = 0; rows[i] != NULL && rows[i] != row; i++)
    {
        options_bit <<= 1; // Move to the next option
    }
    bitmask ^= options_bit; // Toggle the option

    g_settings_set_int (self->settings, "scan-options-bitmask", bitmask);
}

GSettings *
wuming_preferences_dialog_get_settings (WumingPreferencesDialog *self)
{
    return self->settings;
}

/* GObject essential functions */

static const GActionEntry preferences_actions[] = {
    { "preferences", show_dialog }
};

static void
wuming_preferences_dialog_set_property (GObject *object,
                                      guint prop_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
    WumingPreferencesDialog *self = WUMING_PREFERENCES_DIALOG (object);

    switch (prop_id)
    {
        case PROP_WINDOW:
            self->window = g_value_get_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
wuming_preferences_dialog_get_property (GObject *object,
                                      guint prop_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
    WumingPreferencesDialog *self = WUMING_PREFERENCES_DIALOG (object);

    switch (prop_id)
    {
        case PROP_WINDOW:
            g_value_set_object (value, self->window);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
wuming_preferences_dialog_dispose (GObject *object)
{
    WumingPreferencesDialog *self = WUMING_PREFERENCES_DIALOG (object);

    g_clear_object (&self->settings);

    G_OBJECT_CLASS (wuming_preferences_dialog_parent_class)->dispose (object);
}

static void
wuming_preferences_dialog_class_init (WumingPreferencesDialogClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    gobject_class->set_property = wuming_preferences_dialog_set_property;
    gobject_class->get_property = wuming_preferences_dialog_get_property;
    gobject_class->dispose = wuming_preferences_dialog_dispose;

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);

    properties [PROP_WINDOW] =
    g_param_spec_object ("window",
                         "Window",
                         "The wuming window",
                         WUMING_TYPE_WINDOW,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties (gobject_class, N_PROPS, properties);

    gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/wuming-preferences-dialog.ui");

    gtk_widget_class_bind_template_child (widget_class, WumingPreferencesDialog, enable_large_file);
    gtk_widget_class_bind_template_child (widget_class, WumingPreferencesDialog, enable_pua);
    gtk_widget_class_bind_template_child (widget_class, WumingPreferencesDialog, scan_archives);
    gtk_widget_class_bind_template_child (widget_class, WumingPreferencesDialog, scan_mail);
    gtk_widget_class_bind_template_child (widget_class, WumingPreferencesDialog, alert_exceeds_max);
    gtk_widget_class_bind_template_child (widget_class, WumingPreferencesDialog, alert_encrypted);

    gtk_widget_class_bind_template_child (widget_class, WumingPreferencesDialog, signature_expiry_days);
}

WumingPreferencesDialog *
wuming_preferences_dialog_new (WumingWindow *window)
{
    g_return_val_if_fail(WUMING_IS_WINDOW(window), NULL);

    WumingPreferencesDialog *self = WUMING_PREFERENCES_DIALOG (g_object_new (WUMING_TYPE_PREFERENCES_DIALOG, "window", window, NULL));

    return self;
}

static void
wuming_preferences_dialog_init_scan_options (WumingPreferencesDialog *self)
{
    g_return_if_fail (self->settings != NULL);

    int bitmask = g_settings_get_int (self->settings, "scan-options-bitmask");

    AdwSwitchRow *rows[] = {
        self->enable_large_file,
        self->enable_pua,
        self->scan_archives,
        self->scan_mail,
        self->alert_exceeds_max,
        self->alert_encrypted,
        NULL
    };

    int options_bit = 1;
    for (int i = 0; rows[i] != NULL; i++)
    {
        adw_switch_row_set_active (rows[i], (bitmask & options_bit));
        g_signal_connect (rows[i], "notify::active", G_CALLBACK (on_scan_options_changed), self);
        options_bit <<= 1; // Move to the next option
    }
}

static void
wuming_preferences_dialog_init (WumingPreferencesDialog *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    self->settings = g_settings_new ("com.ericlin.wuming");

    wuming_preferences_dialog_init_scan_options (self);

    g_settings_bind (self->settings, "signature-expiration-time", self->signature_expiry_days, "value", G_SETTINGS_BIND_DEFAULT);

    g_signal_connect (self->signature_expiry_days, "value-changed", G_CALLBACK (on_signature_expiration_changed), self);

    GApplication *app = g_application_get_default();
    g_action_map_add_action_entries (G_ACTION_MAP (app),
                                     preferences_actions,
                                     G_N_ELEMENTS (preferences_actions),
                                     self);
}