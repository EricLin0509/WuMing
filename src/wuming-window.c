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

#include "update-signature-page.h"
#include "scan-page.h"
#include "updating-page.h"
#include "scanning-page.h"
#include "threat-page.h"
#include "security-overview-page.h"

struct _WumingWindow
{
	AdwApplicationWindow  parent_instance;

	/* Template widgets */
    AdwNavigationView   *navigation_view; // NavigationView

    /* Main Navigation Page */
    AdwNavigationPage   *main_nav_page;

    /* Top window elements */
	AdwViewStack        *view_stack; // ViewStack

    /* Security Overview Page */
    SecurityOverviewPage       *security_overview_page;

    /* Scan Page */
    ScanPage            *scan_page;

    /* Update Signature Page */
    UpdateSignaturePage *update_signature_page;

    /* Check History Page */
    AdwStatusPage       *check_history_page;

    /* Updating Navigation Page */
    AdwNavigationPage   *updating_nav_page;
    UpdatingPage        *updating_page;

    /* Scanning Navigation Page */
    AdwNavigationPage   *scanning_nav_page;
    ScanningPage        *scanning_page;

    /* Threat Navigation Page */
    AdwNavigationPage   *threat_nav_page;
    ThreatPage          *threat_page;
};

G_DEFINE_FINAL_TYPE (WumingWindow, wuming_window, ADW_TYPE_APPLICATION_WINDOW)

/* Push a page by tag */
void
wuming_window_push_page_by_tag (WumingWindow *self, const char *tag)
{
    adw_navigation_view_push_by_tag (self->navigation_view, tag);
}

/* Pop the current page */
void
wuming_window_pop_page (WumingWindow *self)
{
    adw_navigation_view_pop (self->navigation_view);
}

/* Get current Page tag */
const char *
wuming_window_get_current_page_tag (WumingWindow *self)
{
    return adw_navigation_view_get_visible_page_tag (self->navigation_view);
}

/* Get `SecurityOverviewPage` */
GtkWidget *
wuming_window_get_security_overview_page (WumingWindow *self)
{
    return GTK_WIDGET (self->security_overview_page);
}

/* Get `UpdatingPage` */
GtkWidget *
wuming_window_get_updating_page (WumingWindow *self)
{
    return GTK_WIDGET (self->updating_page);
}

/* Get `ScanningPage` */
GtkWidget *
wuming_window_get_scanning_page (WumingWindow *self)
{
    return GTK_WIDGET (self->scanning_page);
}

/* Get `ThreatPage` */
GtkWidget *
wuming_window_get_threat_page (WumingWindow *self)
{
    return GTK_WIDGET (self->threat_page);
}

/* Compare current page tag with the given tag */
/*
  * @param self
  * the WumingWindow instance
  * @param tag
  * the tag to compare with
  * @return
  * true if the current page tag is the same as the given tag, false otherwise
*/
gboolean
wuming_window_is_current_page_tag (WumingWindow *self, const char *tag)
{
    const char *current_tag = wuming_window_get_current_page_tag (self);
    return g_strcmp0 (current_tag, tag) == 0;
}

/* GObject essential functions */
static void
wuming_window_dispose (GObject *object)
{
    WumingWindow *self = WUMING_WINDOW (object);

    GtkWidget *navigation_view = GTK_WIDGET (self->navigation_view);
    g_clear_pointer (&navigation_view, gtk_widget_unparent);

    G_OBJECT_CLASS (wuming_window_parent_class)->dispose (object);
}

static void
wuming_window_finalize (GObject *object)
{
    WumingWindow *self = WUMING_WINDOW (object);

    /* Reset all child widgets */
    self->navigation_view = NULL;
    self->main_nav_page = NULL;
    self->view_stack = NULL;
    self->security_overview_page = NULL;
    self->scan_page = NULL;
    self->update_signature_page = NULL;
    self->check_history_page = NULL;
    self->updating_nav_page = NULL;
    self->updating_page = NULL;
    self->scanning_nav_page = NULL;
    self->scanning_page = NULL;
    self->threat_nav_page = NULL;
    self->threat_page = NULL;

    G_OBJECT_CLASS (wuming_window_parent_class)->finalize (object);
}

static void
wuming_window_class_init (WumingWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = wuming_window_dispose;
    object_class->finalize = wuming_window_finalize;

	gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/wuming-window.ui");

    gtk_widget_class_bind_template_child (widget_class, WumingWindow, navigation_view);

    /* Main Navigation Page */
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, main_nav_page);

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

    /* Updating Navigation Page */
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, updating_nav_page);
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, updating_page);

    /* Scanning Navigation Page */
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, scanning_nav_page);
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, scanning_page);

    /* Threat Navigation Page */
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, threat_nav_page);
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, threat_page);

    g_type_ensure (SECURITY_OVERVIEW_TYPE_PAGE);
    g_type_ensure (SCAN_TYPE_PAGE);
    g_type_ensure (SCANNING_TYPE_PAGE);
    g_type_ensure (THREAT_TYPE_PAGE);
    g_type_ensure (UPDATE_SIGNATURE_TYPE_PAGE);
    g_type_ensure (UPDATING_TYPE_PAGE);
}

static void
wuming_window_init_settings (WumingWindow *self)
{
    /* Setting the window size */
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

    g_autofree gchar *last_scan_time = g_settings_get_string (settings, "last-scan-time");
    gboolean is_expired = is_scan_time_expired(last_scan_time, NULL);
    scan_page_show_last_scan_time (self->scan_page, NULL, last_scan_time);
    scan_page_show_last_scan_time_status (self->scan_page, NULL, is_expired);

    security_overview_page_show_last_scan_time_status (self->security_overview_page, NULL, is_expired);

    g_object_unref (settings); // Free the GSettings object
}

static void
wuming_window_init (WumingWindow *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

    /* Initialize the settings */
    wuming_window_init_settings (self);

    /* Scan the Database */
    signature_status *result = signature_status_new ();

    /* Update the `UpdateSignaturePage` */
    update_signature_page_show_isuptodate (self->update_signature_page, result);
    update_signature_page_show_servicestat (self->update_signature_page);

    /* Update the `SecurityOverviewPage` */
    security_overview_page_show_signature_status (self->security_overview_page, result);
    security_overview_page_connect_goto_scan_page_signal (self->security_overview_page);
    security_overview_page_show_health_level (self->security_overview_page);

    signature_status_clear (&result);
}
