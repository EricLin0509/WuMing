/* wuming-window.c
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

#include "config.h"

#include "wuming-window.h"

#include "update-signature.h"
#include "update-signature-page.h"

struct _WumingWindow
{
	AdwApplicationWindow  parent_instance;

	/* Template widgets */

    /* Top window elements */
	AdwViewStack        *view_stack; // ViewStack

    /* Security Overview Page */
    AdwStatusPage       *security_overview_page;

    /* Scan Page */
    AdwStatusPage       *scan_page;

    /* Update Signature Page */
    UpdateSignaturePage *update_signature_page;

    /* Check History Page */
    AdwStatusPage       *check_history_page;
};

G_DEFINE_FINAL_TYPE (WumingWindow, wuming_window, ADW_TYPE_APPLICATION_WINDOW)

static void
wuming_window_class_init (WumingWindowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/wuming-window.ui");

    /* Top window elements */
	gtk_widget_class_bind_template_child (widget_class, WumingWindow, view_stack);

    /* Main page */
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, security_overview_page);

    /* Scan Page */
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, scan_page);

    /* Update Database Page */
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, update_signature_page);

    /* Check History Page */
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, check_history_page);

    g_type_ensure(UPDATE_SIGNATURE_TYPE_PAGE);
}

static void
wuming_window_init (WumingWindow *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

    /* GSettings Stuff */
    GSettings *settings = g_settings_new ("com.ericlin.wuming");

    g_settings_bind (settings, "width",
                     self, "default-width",
                     G_SETTINGS_BIND_DEFAULT);

    g_settings_bind (settings, "height",
                     self, "default-height",
                      G_SETTINGS_BIND_DEFAULT);

    g_settings_bind (settings, "is-maximized",
                     self, "maximized",
                     G_SETTINGS_BIND_DEFAULT);

    g_settings_bind (settings, "is-fullscreen",
                     self, "fullscreened",
                     G_SETTINGS_BIND_DEFAULT);


    /* Scan the Database First */
    scan_result *result = g_new0 (scan_result, 1);
    scan_signature_date (result);
    is_signature_uptodate (result);

    /* Merge Window Elements */
    update_signature_page_show_date (self->update_signature_page, *result);
    update_signature_page_show_isuptodate(self->update_signature_page, result->is_uptodate);
    g_free (result);
}
