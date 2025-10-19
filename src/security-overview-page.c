/* security-overview-page.c
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

#include "security-overview-page.h"

#define LAST_SCAN_TIME_VALID 1 // Bit mask for last scan time valid
#define SIGNATURE_VALID 0x10 // Bit mask for signature valid

struct _SecurityOverviewPage {
    GtkWidget parent_instance;

    AdwBreakpointBin *break_point;
    GtkButton *scan_overview_button;
    gulong scan_overview_button_handler_id;
    GtkButton *signature_overview_button;
    gushort health_level;
};

G_DEFINE_FINAL_TYPE (SecurityOverviewPage, security_overview_page, GTK_TYPE_WIDGET)

static void
goto_scan_page_cb (GtkButton *button, gpointer user_data)
{
    AdwViewStack *view_stack = user_data;
    adw_view_stack_set_visible_child_name (view_stack, gettext("Scan"));
}

/* Let `scan_overview_button` connect signal to `clicked` signal of goto scan page */
/*
  * @warning
  * This function should be called when `wuming-window` initialized.
*/
void
security_overview_page_connect_goto_scan_page_signal (SecurityOverviewPage *self)
{
    g_return_if_fail(self != NULL);

    AdwViewStack *view_stack = ADW_VIEW_STACK (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_VIEW_STACK));

    self->scan_overview_button_handler_id = g_signal_connect (self->scan_overview_button, "clicked", G_CALLBACK (goto_scan_page_cb), view_stack);
}

/* Show the last scan time status on the security overview page. */
/*
  * @param self
  * `SecurityOverviewPage` object.
    * @param setting [OPTIONAL]
  * `GSettings` object to save last scan time, if is NULL, it will ignore it and use `is_expired` directly.
  * 
  * @param is_expired [OPTIONAL]
  * Whether the last scan time is expired or not.
  * 
  * @note
  * If `GSettings` is not NULL, the `is_expired` parameter will be ignored.
*/
void
security_overview_page_show_last_scan_time_status (SecurityOverviewPage *self, GSettings *setting, gboolean is_expired)
{
    g_return_if_fail(self != NULL);

    GtkWidget *button_content = gtk_button_get_child (self->scan_overview_button);

    gboolean is_null = (setting == NULL);
    gchar *label = NULL;
    gchar *icon_name = NULL;
    gchar *style = NULL;

    if (is_null) // Use is_expired directly
    {
        label = is_expired ? gettext ("Scan Has Expired") : gettext ("Scan Has Not Expired");
        icon_name = is_expired ? "status-warning-symbolic" : "status-ok-symbolic";
        style = is_expired ? "warning" : "success";
        if (!is_expired) self->health_level |= LAST_SCAN_TIME_VALID;
    }
    else // Has `GSettings`, use it to get the last scan time and check if it is expired
    {
        gboolean is_older_than_a_week = is_scan_time_expired (NULL, setting);

        label = is_older_than_a_week ? gettext ("Scan Has Expired") : gettext ("Scan Has Not Expired");
        icon_name = is_older_than_a_week ? "status-warning-symbolic" : "status-ok-symbolic";
        style = is_older_than_a_week ? "warning" : "success";
        if (!is_older_than_a_week) self->health_level |= LAST_SCAN_TIME_VALID;
    }

    adw_button_content_set_label (ADW_BUTTON_CONTENT (button_content), label);
    adw_button_content_set_icon_name (ADW_BUTTON_CONTENT (button_content), icon_name);
    gtk_widget_add_css_class(GTK_WIDGET(self->scan_overview_button), style);
}

/* Show the signature status on the security overview page. */
/*
  * @param self
  * `SecurityOverviewPage` object.
  * 
  * @param result
  * `scan_result` object to get signature status.
*/
void
security_overview_page_show_signature_status (SecurityOverviewPage *self, const signature_status *result)
{
    g_return_if_fail(self != NULL);

    GtkWidget *button_content = gtk_button_get_child (self->signature_overview_button);

    gchar *label = NULL;
    gchar *icon_name = NULL;
    gchar *style = NULL;

    switch(signature_status_get_status(result))
    {
        case 0: // Signature is oudated
            label = gettext ("Signature Is Outdated");
            icon_name = "status-warning-symbolic";
            style = "warning";
            break;
        case 1: // No signature found
            label = gettext ("No Signature Found");
            icon_name = "status-error-symbolic";
            style = "error";
            break;
        case 16: // Signature is up-to-date
            label = gettext ("Signature Is Up To Date");
            icon_name = "status-ok-symbolic";
            style = "success";
            self->health_level |= SIGNATURE_VALID;
            break;
        default: // Bit mask is invalid (because these two bit mask cannot be set at the same time)
            label = gettext ("Unknown Signature Status");
            icon_name = "status-error-symbolic";
            style = "error";
            break;
    }

    adw_button_content_set_label (ADW_BUTTON_CONTENT (button_content), label);
    adw_button_content_set_icon_name (ADW_BUTTON_CONTENT (button_content), icon_name);
    gtk_widget_add_css_class(GTK_WIDGET(self->signature_overview_button), style);
}

/* Show the health level on the security overview page. */
void
security_overview_page_show_health_level (SecurityOverviewPage *self)
{
    g_return_if_fail(self != NULL);

    GtkWidget *status_page = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_STATUS_PAGE);

    gchar *title = NULL;
    gchar *message = NULL;
    gchar *icon_name = NULL;

    switch(self->health_level)
    {
        case 0: // No health level
            title = gettext ("Poor Status");
            message = gettext ("Please take action immediately");
            icon_name = "status-error-symbolic";
            break;
        case 1: // Only last scan time is valid, signature has issue
            title = gettext ("Need Attention");
            message = gettext("Something wrong with the signature");
            icon_name = "status-warning-symbolic";
            break;
        case 16: // Only signature is valid, last scan time has issue
            title = gettext ("Need Attention");
            message = gettext ("Scan Has Expired");
            icon_name = "status-warning-symbolic";
            break;
        case 17: // Both last scan time and signature are valid
            title = gettext ("All Good");
            message = gettext ("All set, have a nice day");
            icon_name = "status-ok-symbolic";
            break;
        default: // Bit mask is invalid
            title = gettext ("Unknown Health Level");
            message = gettext ("Please check the logs for more information");
            icon_name = "status-error-symbolic";
            break;
    }

    adw_status_page_set_title (ADW_STATUS_PAGE (status_page), title);
    adw_status_page_set_description (ADW_STATUS_PAGE (status_page), message);
    adw_status_page_set_icon_name (ADW_STATUS_PAGE (status_page), icon_name);
}

/* GObject essential functions */

static void
security_overview_page_dispose (GObject *object)
{
    SecurityOverviewPage *self = SECURITY_OVERVIEW_PAGE (object);

    g_signal_handler_disconnect (self->scan_overview_button, self->scan_overview_button_handler_id);

    GtkWidget *break_point = GTK_WIDGET (self->break_point);

    g_clear_pointer (&break_point, gtk_widget_unparent);

    G_OBJECT_CLASS (security_overview_page_parent_class)->dispose (object);
}

static void
security_overview_page_finalize (GObject *object)
{
    SecurityOverviewPage *self = SECURITY_OVERVIEW_PAGE (object);

    /* Reset all widgets */
    self->break_point = NULL;
    self->scan_overview_button = NULL;
    self->scan_overview_button_handler_id = 0;
    self->signature_overview_button = NULL;

    self->health_level = 0;

    G_OBJECT_CLASS (security_overview_page_parent_class)->finalize (object);
}

static void
security_overview_page_class_init (SecurityOverviewPageClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
    
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = security_overview_page_dispose;
    object_class->finalize = security_overview_page_finalize;

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);

    gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/pages/security-overview-page.ui");

    gtk_widget_class_bind_template_child (widget_class, SecurityOverviewPage, break_point);
    gtk_widget_class_bind_template_child (widget_class, SecurityOverviewPage, scan_overview_button);
    gtk_widget_class_bind_template_child (widget_class, SecurityOverviewPage, signature_overview_button);
}

GtkWidget *
security_overview_page_new (void)
{
    return g_object_new (SECURITY_OVERVIEW_TYPE_PAGE, NULL);
}

static void
security_overview_page_init (SecurityOverviewPage *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    self->health_level = 0;
}