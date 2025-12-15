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
#include <stddef.h>

#include "config.h"

#include "libs/systemd-control.h"
#include "libs/update-signature.h"
#include "libs/scan.h"

#include "wuming-window.h"
#include "wuming-preferences-dialog.h"

#include "update-signature-page.h"
#include "scan-page.h"
#include "updating-page.h"
#include "scanning-page.h"
#include "threat-page.h"
#include "security-overview-page.h"

#define WUMING_WINDOW_NOTIFICATION_ID "wuming-notification"

#define WUMING_WINDOW_COMPONENT_ENTRY(name) \
    { #name, offsetof(WumingWindow, name) } // Define the component entry for the `WumingWindow` struct

struct _WumingWindow
{
	AdwApplicationWindow  parent_instance;

	/* Template widgets */
    AdwToastOverlay     *toast_overlay; // ToastOverlay
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
    WumingPreferencesDialog *prefrences_dialog;
    GNotification       *notification;
    GtkDropTarget       *drop_target;
    GApplication        *app;
    gboolean            is_hidden;
    gulong              close_request_signal_id;
    gulong              show_signal_id;
    signature_status      *status;
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

/* Connect `popped` signal */
gulong
wuming_window_connect_popped_signal (WumingWindow *self, GCallback callback, gpointer user_data)
{
    g_return_val_if_fail (self != NULL && callback != NULL, 0); // Check if the object is valid

    return g_signal_connect (self->navigation_view, "popped", callback, user_data);
}

/* Revoke the `popped` signal */
void
wuming_window_revoke_popped_signal (WumingWindow *self, gulong signal_id)
{
    g_return_if_fail (self != NULL); // Check if the object is valid

    if (signal_id == 0) return;

    g_signal_handler_disconnect (self->navigation_view, signal_id);
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

/* Get Widgets from the `WumingWindow` */
void *
wuming_window_get_component(WumingWindow *self, const char *component_name)
{
    g_return_val_if_fail(self != NULL, NULL);

    typedef struct {
        const char *name;
        size_t offset;
    } ComponentEntry;

    static const ComponentEntry components[] = {
        WUMING_WINDOW_COMPONENT_ENTRY(navigation_view),
        WUMING_WINDOW_COMPONENT_ENTRY(main_nav_page),
        WUMING_WINDOW_COMPONENT_ENTRY(view_stack),
        WUMING_WINDOW_COMPONENT_ENTRY(security_overview_page),
        WUMING_WINDOW_COMPONENT_ENTRY(scan_page),
        WUMING_WINDOW_COMPONENT_ENTRY(scanning_page),
        WUMING_WINDOW_COMPONENT_ENTRY(update_signature_page),
        WUMING_WINDOW_COMPONENT_ENTRY(update_context),
        WUMING_WINDOW_COMPONENT_ENTRY(scan_context),
    };

    for (size_t i = 0; i < G_N_ELEMENTS(components); i++)
    {
        if (g_strcmp0(components[i].name, component_name) == 0)
        {
            return *(void **)((char *)self + components[i].offset);
        }
    }
    g_critical("Component '%s' not found in WumingWindow", component_name);
    return NULL;
}


/* Get hide the window on close */
gboolean
wuming_window_is_hide (WumingWindow *self)
{
    return gtk_window_is_active (GTK_WINDOW (self));
}

static gboolean
wuming_window_on_close_request (GtkWindow *self, gpointer user_data)
{
    WumingWindow *window = WUMING_WINDOW (self);

    const char *message = (const char *)user_data;

    g_atomic_int_set (&window->is_hidden, TRUE);

    wuming_window_send_notification (window, G_NOTIFICATION_PRIORITY_LOW, message, gettext("Click to show details"));

    return FALSE;
}

static void
wuming_window_on_show (GtkWindow *self, gpointer user_data)
{
    WumingWindow *window = WUMING_WINDOW (self);

    g_atomic_int_set (&window->is_hidden, FALSE);

    wuming_window_close_notification (window);
}

/* Set hide the window on close */
void
wuming_window_set_hide_on_close (WumingWindow *self, gboolean hide_on_close, const char *message)
{
    gtk_window_set_hide_on_close (GTK_WINDOW (self), hide_on_close);

    if (hide_on_close)
    {
        self->close_request_signal_id = g_signal_connect (self, "close-request", G_CALLBACK (wuming_window_on_close_request), (void *)message);
        self->show_signal_id = g_signal_connect (self, "show", G_CALLBACK (wuming_window_on_show), NULL);
        return;
    }

    if (self->close_request_signal_id > 0)
    {
        g_signal_handler_disconnect (self, self->close_request_signal_id);
        self->close_request_signal_id = 0;
    }
    if (self->show_signal_id > 0)
    {
        g_signal_handler_disconnect (self, self->show_signal_id);
        self->show_signal_id = 0;
    }
}

/* Send the notification */
void
wuming_window_send_notification (WumingWindow *self, GNotificationPriority priority, const char *title, const char *message)
{
    g_return_if_fail (self != NULL); // Check if the object is valid

    const char *real_title = title == NULL ? "" : title;
    const char *real_message = message == NULL ? "" : message;

    g_notification_set_title (self->notification, real_title);
    g_notification_set_body (self->notification, real_message);
    g_notification_set_priority (self->notification, priority);

    g_application_send_notification (self->app, WUMING_WINDOW_NOTIFICATION_ID, self->notification);
}

/* Close the notification */
void
wuming_window_close_notification (WumingWindow *self)
{
    g_return_if_fail (self != NULL); // Check if the object is valid

    g_application_withdraw_notification (self->app, WUMING_WINDOW_NOTIFICATION_ID);
}

/* Send toast notification */
void
wuming_window_send_toast_notification (WumingWindow *self, const char *message, int timeout)
{
    g_return_if_fail (self != NULL && message != NULL); // Check if the object is valid

    AdwToast *toast = adw_toast_new (message);
    adw_toast_set_timeout (toast, timeout);

    adw_toast_overlay_add_toast (self->toast_overlay, toast);
}

/* Dismiss toast notification */
void
wuming_window_dismiss_toast_notification (WumingWindow *self)
{
    g_return_if_fail (self != NULL); // Check if the object is valid

    adw_toast_overlay_dismiss_all (self->toast_overlay);
}

/* Update the signture status */
void
wuming_window_update_signature_status (WumingWindow *self, gboolean need_rescan_signature, gint signature_expiration_time)
{
    g_return_if_fail (self != NULL); // Check if the object is valid

    signature_status_update (self->status, need_rescan_signature, signature_expiration_time);

    update_signature_page_show_isuptodate (self->update_signature_page, self->status);
    security_overview_page_show_signature_status (self->security_overview_page, self->status);
    security_overview_page_show_health_level (self->security_overview_page);
}

static void
wuming_window_on_drag_drop (GtkDropTarget* self, const GValue* value, gdouble x, gdouble y, gpointer user_data)
{
    WumingWindow *window = WUMING_WINDOW (user_data);

    if (!wuming_window_is_in_main_page(window)) return; // Prevent multiple tasks running at the same time

    GFile *file = g_value_get_object (value);

    if (file == NULL) return;

    const char *path = g_file_get_path (file);

    if (path == NULL) return;

    start_scan (window->scan_context, path);
}

/* GObject essential functions */
static void
wuming_window_dispose (GObject *object)
{
    WumingWindow *self = WUMING_WINDOW (object);

    signature_status_clear (&self->status);
    update_context_clear (&self->update_context);
    scan_context_clear (&self->scan_context);

    wuming_window_set_hide_on_close (self, FALSE, NULL);
    wuming_window_dismiss_toast_notification (self);
    wuming_window_close_notification (self);

    g_clear_object (&self->prefrences_dialog);
    g_clear_object (&self->notification);
    gtk_widget_remove_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (self->drop_target));

    GtkWidget *toast_overlay = GTK_WIDGET (self->toast_overlay);
    g_clear_pointer (&toast_overlay, gtk_widget_unparent);

    G_OBJECT_CLASS (wuming_window_parent_class)->dispose (object);
}

static void
wuming_window_finalize (GObject *object)
{
    WumingWindow *self = WUMING_WINDOW (object);

    /* Reset all child widgets */
    self->toast_overlay = NULL;
    self->navigation_view = NULL;
    self->main_nav_page = NULL;
    self->view_stack = NULL;
    self->security_overview_page = NULL;
    self->scan_page = NULL;
    self->update_signature_page = NULL;
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

    /* Template widgets */
    gtk_widget_class_bind_template_child (widget_class, WumingWindow, toast_overlay);
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
wuming_window_init_settings (WumingWindow *self, GSettings *settings)
{
    /* Setting the window size */
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

    /* Update the `ScanPage` */
    scan_page_show_last_scan_time (self->scan_page, NULL, last_scan_time);
    scan_page_show_last_scan_time_status (self->scan_page, NULL, is_expired);

    gint signature_expiration_time = g_settings_get_int (settings, "signature-expiration-time");

    /* Check systemd service status */
    int status = is_service_enabled ("clamav-freshclam.service");

    /* Scan the Database */
    self->status = signature_status_new (signature_expiration_time);

    /* Update the `UpdateSignaturePage` */
    update_signature_page_show_isuptodate (self->update_signature_page, self->status);
    update_signature_page_show_servicestat (self->update_signature_page, status);

    /* Update the `SecurityOverviewPage` */
    security_overview_page_show_signature_status (self->security_overview_page, self->status);
    security_overview_page_show_last_scan_time_status (self->security_overview_page, NULL, is_expired);
    security_overview_page_connect_goto_scan_page_signal (self->security_overview_page);
    security_overview_page_show_servicestat (self->security_overview_page, status);
    security_overview_page_show_health_level (self->security_overview_page);
}

static void
wuming_window_init (WumingWindow *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

    self->update_context = update_context_new(self, self->updating_page);
    self->scan_context = scan_context_new(self, self->security_overview_page, self->scan_page, self->scanning_page, self->threat_page);

    self->notification = g_notification_new ("WuMing");
    g_object_ref_sink (self->notification); // Keep the reference count

    self->prefrences_dialog = wuming_preferences_dialog_new (self);
    g_object_ref_sink (self->prefrences_dialog); // Keep the reference count

    GSettings *settings = wuming_preferences_dialog_get_settings (self->prefrences_dialog);

    /* Initialize the settings */
    wuming_window_init_settings (self, settings);

    self->drop_target = gtk_drop_target_new (G_TYPE_FILE, GDK_ACTION_COPY);
    g_signal_connect (self->drop_target, "drop", G_CALLBACK (wuming_window_on_drag_drop), self);
    gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (self->drop_target));

    self->is_hidden = FALSE;

    self->app = g_application_get_default ();
}
