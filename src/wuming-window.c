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

#include <glib/gi18n.h>

#include "config.h"

#include "libs/realtime-notification.h"
#include "libs/systemd-control.h"
#include "libs/update-signature.h"
#include "libs/scan.h"

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

    /* Private */
    GDBusConnection       *notification;
    gboolean             is_hide;
    gulong              close_request_id;
    gulong              activate_request_id;
    UpdateContext       *update_context;
    ScanContext         *scan_context;
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

/* Check the AdwNavigation is in the `main_page` */
gboolean
wuming_window_is_in_main_page (WumingWindow *self)
{
    g_return_val_if_fail (self != NULL, FALSE); // Check if the object is valid

    const char *current_page_tag = wuming_window_get_current_page_tag (self);

    return g_strcmp0 (current_page_tag, "main_nav_page") == 0;
}

/* Get `UpdateContext` */
/*
  * @warning
  * This return a void pointer, which need to be cast to the `UpdateContext` pointer type.
*/
void *
wuming_window_get_update_context (WumingWindow *self)
{
    g_return_val_if_fail (self, NULL);

    return (void *)self->update_context;
}

/* Get `ScanContext` */
/*
  * @warning
  * This return a void pointer, which need to be cast to the `ScanContext` pointer type.
*/
void *
wuming_window_get_scan_context (WumingWindow *self)
{
    g_return_val_if_fail(self, NULL);

    return (void *)self->scan_context;
}

/* Get hide the window on close */
gboolean
wuming_window_is_hide (WumingWindow *self)
{
    g_return_val_if_fail (self != NULL, FALSE); // Check if the object is valid

    gboolean is_hide = g_atomic_int_get (&self->is_hide);

    return is_hide; // Use atomic operation to get the value
}

static void
wuming_window_on_show (GtkWidget *window, gpointer user_data)
{
    WumingWindow *self = WUMING_WINDOW (window);

    g_atomic_int_set (&self->is_hide, FALSE);

    realtime_notification_close (self->notification);
}

static gboolean
wuming_window_on_close_hide (GtkWindow *window, gpointer user_data)
{
    WumingWindow *self = WUMING_WINDOW (window);

    g_atomic_int_set (&self->is_hide, TRUE);

    return FALSE; // Don't close the window
}

/* Set hide the window on close */
void
wuming_window_set_hide_on_close (WumingWindow *self, gboolean hide_on_close)
{
    gtk_window_set_hide_on_close (GTK_WINDOW (self), hide_on_close);

    if (hide_on_close)
    {
        self->close_request_id = g_signal_connect (self, "close-request", G_CALLBACK (wuming_window_on_close_hide), NULL);
        self->activate_request_id = g_signal_connect (self, "show", G_CALLBACK (wuming_window_on_show), NULL);
    }
    else
    {
        if (self->close_request_id > 0)
        {
            g_signal_handler_disconnect (self, self->close_request_id);
            self->close_request_id = 0;
        }
        if (self->activate_request_id > 0)
        {
            g_signal_handler_disconnect (self, self->activate_request_id);
            self->activate_request_id = 0;
        }
        g_atomic_int_set (&self->is_hide, FALSE);
    }
}

/* Send the notification */
void
wuming_window_send_notification (WumingWindow *self, const char *title, const char *message)
{
    g_return_if_fail (self != NULL); // Check if the object is valid

    realtime_notification_send (self->notification, "com.ericlin.wuming", gettext("WuMing"), title, message);
}

/* GObject essential functions */
static void
wuming_window_dispose (GObject *object)
{
    WumingWindow *self = WUMING_WINDOW (object);

    update_context_clear (&self->update_context);
    scan_context_clear (&self->scan_context);

    wuming_window_set_hide_on_close (self, FALSE); // Reset the `close-request` signal

    realtime_notification_clear(&self->notification);

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

    /* Check systemd service status */
    int status = is_service_enabled ("clamav-freshclam.service");

    /* Scan the Database */
    signature_status *result = signature_status_new ();

    /* Update the `UpdateSignaturePage` */
    update_signature_page_show_isuptodate (self->update_signature_page, result);
    update_signature_page_show_servicestat (self->update_signature_page, status);

    /* Update the `SecurityOverviewPage` */
    security_overview_page_show_signature_status (self->security_overview_page, result);
    security_overview_page_connect_goto_scan_page_signal (self->security_overview_page);
    security_overview_page_show_servicestat (self->security_overview_page, status);
    security_overview_page_show_health_level (self->security_overview_page);

    signature_status_clear (&result);

    self->update_context = update_context_new(self, self->security_overview_page, self->update_signature_page, self->updating_page);
    self->scan_context = scan_context_new(self, self->security_overview_page, self->scan_page, self->scanning_page, self->threat_page);

    self->notification = realtime_notification_init ();
}
