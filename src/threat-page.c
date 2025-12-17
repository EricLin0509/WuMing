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

// Warning: this macro should be started and ended with a space, otherwise may cause comparison error
#define SYSTEM_DIRECTORIES " /usr /lib /lib64 /etc /opt /var /sys /proc " // System directories that should be warned before deleting

struct _ThreatPage {
    GtkWidget parent_instance;

    AdwToolbarView *toolbar_view;
    GtkButton *delete_all_button;
    GtkListBox *threat_list;

    /* Private */
    AdwDialog *alert_dialog;
    GHashTable *delete_file_table;
};

G_DEFINE_FINAL_TYPE(ThreatPage, threat_page, GTK_TYPE_WIDGET)

static void
set_file_properties(GtkWidget *expander_row, const char *path)
{
    if (!path || !*path || path[0] != '/') // Check if the path is valid and is absolute
    {
        g_warning("Invalid absolute path: %s", path ? path : "(null)");
        return;
    }

    /* Get the first directory name in the path */
    // Because all the paths should be absolute, the first character should be a slash
    char *second_slash = strchr(path + 1, '/');

    if (second_slash) *second_slash = '\0'; // Cut the path to the first directory name temporarily

    gchar *query = g_strdup_printf(" %s ", path); // Use for searching in the `SYSTEM_DIRECTORIES`
    gboolean is_system_direcotry = (strstr(SYSTEM_DIRECTORIES, query) != NULL);

    adw_preferences_row_set_title(
        ADW_PREFERENCES_ROW(expander_row),
        is_system_direcotry ? 
            gettext("Maybe a system file, delete it with caution!") : 
            gettext("Normal file")
    );

    g_free(query);
    
    if (second_slash) *second_slash = '/'; // Restore the path
}

/* Create a new `AdwExpanderRow` for the threat list view */
static GtkWidget *
create_threat_expander_row (GtkWidget **delete_button, const char *path, const char *virname)
{
    GtkWidget *expander_row = adw_expander_row_new (); // Create the action row for the list view
    gtk_widget_add_css_class (expander_row, "property"); // Add property syle class to the action row
    set_file_properties(expander_row, path); // Set the file properties for the expander row
    adw_expander_row_set_subtitle (ADW_EXPANDER_ROW (expander_row), path);

    /* Delete button for the action row */
    *delete_button = gtk_button_new ();
    gtk_widget_set_size_request (*delete_button, -1, 40);
    gtk_widget_add_css_class (*delete_button, "button-default");
    gtk_widget_set_halign (*delete_button, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (*delete_button, GTK_ALIGN_CENTER);

    GtkWidget *content = adw_button_content_new (); // Create the button content
    adw_button_content_set_label (ADW_BUTTON_CONTENT (content), gettext ("Delete"));
    adw_button_content_set_icon_name (ADW_BUTTON_CONTENT (content), "delete-symbolic");

    gtk_button_set_child (GTK_BUTTON (*delete_button), content);

    adw_expander_row_add_suffix (ADW_EXPANDER_ROW (expander_row), *delete_button); // Add the delete button to the action row

    GtkWidget *vir_row = adw_action_row_new (); // Create the virname row
    gtk_widget_add_css_class (vir_row, "property"); // Add property style class to the virname row

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (vir_row), gettext ("Threat Identity"));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (vir_row), virname);

    adw_expander_row_add_row (ADW_EXPANDER_ROW (expander_row), vir_row); // Add the virname row to the expander row

    return expander_row;
}

/* error operation */
static void
delete_error_operation(GtkWidget *expander_row, FileSecurityStatus status)
{
    g_return_if_fail(expander_row != NULL);

    gtk_widget_set_sensitive(expander_row, FALSE);
    switch (status)
    {
        case FILE_SECURITY_OK:
        case FILE_SECURITY_OPERATION_SKIPPED:
            break;
        case FILE_SECURITY_DIR_MODIFIED:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander_row), gettext("Directory modified, try removing it manually!"));
            break;
        case FILE_SECURITY_FILE_MODIFIED:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander_row), gettext("File may compromised, try removing it manually!"));
            break;
        case FILE_SECURITY_DIR_NOT_FOUND:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander_row), gettext("Directory not found!"));
            break;
        case FILE_SECURITY_FILE_NOT_FOUND:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander_row), gettext("File not found!"));
            break;
        case FILE_SECURITY_INVALID_PATH:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander_row), gettext("Invalid path!"));
            break;
        case FILE_SECURITY_PERMISSION_DENIED:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander_row), gettext("Permission denied!"));
            break;
        case FILE_SECURITY_OPERATION_FAILED:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander_row), gettext("Operation failed!"));
            break;
        default:
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander_row), gettext("Unknown error!"));
            break;
    }
}

static void
threat_page_remove_threat (ThreatPage *self, GtkWidget *row, FileSecurityStatus status)
{
    g_return_if_fail (THREAT_IS_PAGE (self) && GTK_IS_WIDGET (row));

    switch (status)
    {
        case FILE_SECURITY_OK:
            break;
        case FILE_SECURITY_OPERATION_SKIPPED:
            return; // Skip the delete operation, do nothing
        default: // Delete failed, show the error message
            delete_error_operation(row, status);
            return;
    }

    gtk_list_box_remove (self->threat_list, row);

    if (gtk_list_box_get_row_at_index (self->threat_list, 0) != NULL) return;

    /* If has no more threats, pop the page and show the final result */
    WumingWindow *window = WUMING_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), WUMING_TYPE_WINDOW));
    wuming_window_pop_page (window);

    ScanningPage *scanning_page = wuming_window_get_component (window, "scanning_page");
    scanning_page_disable_threat_button (scanning_page);
    scanning_page_set_final_result (scanning_page, FALSE, gettext("All Clear"), gettext("All threats have been removed!"), "status-ok-symbolic");
}

static void
on_delete_button_clicked (DeleteFileData *data)
{
    g_return_if_fail (data != NULL);
    GtkWidget *expander_row = delete_file_data_get_expander_row (data);
    ThreatPage *page = THREAT_PAGE (gtk_widget_get_ancestor (expander_row, THREAT_TYPE_PAGE));

    FileSecurityStatus status = delete_threat_file (page->delete_file_table, data);
    threat_page_remove_threat (page, expander_row, status);
}

gboolean
threat_page_add_threat (ThreatPage *self, const char *threat_path, const char *threat_name)
{
    g_return_val_if_fail (THREAT_IS_PAGE (self), FALSE);
    g_return_val_if_fail (threat_path != NULL, FALSE);

    GtkWidget *delete_button = NULL;
    GtkWidget *expander_row = create_threat_expander_row (&delete_button, threat_path, threat_name);
    gtk_list_box_prepend (self->threat_list, expander_row); // Add the expander row to the list

    DeleteFileData *delete_data = delete_file_data_table_insert (self->delete_file_table, threat_path, expander_row); // Add the delete data to the list
    if (delete_data == NULL)
    {
        g_critical ("Failed to add delete data to the table");
        return FALSE;
    }

    g_signal_connect_swapped (delete_button, "clicked", G_CALLBACK(on_delete_button_clicked), delete_data); // Connect the delete button signal to the `delete_threat_file` function

    return TRUE;
}

void
threat_page_clear_threats (ThreatPage *self)
{
    g_return_if_fail (THREAT_IS_PAGE (self));

    gtk_list_box_remove_all (self->threat_list); // Remove all items from the list
    g_hash_table_remove_all (self->delete_file_table); // Remove all delete data from the table
}

static void
delete_all_threat_files(ThreatPage *self)
{
    g_return_if_fail(THREAT_IS_PAGE(self));

    GList *keys = NULL;
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, self->delete_file_table);

    while (g_hash_table_iter_next(&iter, &key, NULL))
    {
        keys = g_list_prepend(keys, key);
    }

    for (GList *elements = keys; elements != NULL; elements = elements->next)
    {
        DeleteFileData *data = (DeleteFileData *)elements->data;
        GtkWidget *expander_row = delete_file_data_get_expander_row(data);
        FileSecurityStatus status = delete_threat_file(self->delete_file_table, data);
        threat_page_remove_threat(self, expander_row, status);
    }

    g_list_free(keys);
}

static void
on_alert_dialog_response (AdwAlertDialog *dialog, GAsyncResult *result, gpointer user_data)
{
    g_return_if_fail (user_data != NULL);

    ThreatPage *self = THREAT_PAGE (user_data);

    const char *response = adw_alert_dialog_choose_finish (dialog, result);

    if (g_strcmp0 (response, "delete_all") == 0) delete_all_threat_files (self);
}

static void
show_alert_dialog (ThreatPage *self)
{
    adw_alert_dialog_choose (ADW_ALERT_DIALOG (self->alert_dialog), GTK_WIDGET (self), NULL, (GAsyncReadyCallback) on_alert_dialog_response, self);
}

/* GObject essential functions */

static void
threat_page_dispose (GObject *object)
{
    ThreatPage *self = THREAT_PAGE (object);

    GtkWidget *toolbar_view = GTK_WIDGET (self->toolbar_view);

    threat_page_clear_threats (self);
    g_hash_table_unref (self->delete_file_table); // Unref the delete data table
    g_clear_object (&self->alert_dialog);
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

    self->delete_file_table = delete_file_data_table_new();

    g_signal_connect_swapped (self->delete_all_button, "clicked", G_CALLBACK (show_alert_dialog), self);
}

GtkWidget *
threat_page_new (void)
{
    return g_object_new (THREAT_TYPE_PAGE, NULL);
}