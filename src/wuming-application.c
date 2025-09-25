/* wuming-application.c
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
#include <glib/gi18n.h>

#include "wuming-application.h"
#include "wuming-window.h"

struct _WumingApplication
{
	AdwApplication parent_instance;
};

G_DEFINE_FINAL_TYPE (WumingApplication, wuming_application, ADW_TYPE_APPLICATION)

WumingApplication *
wuming_application_new (const char        *application_id,
                        GApplicationFlags  flags)
{
	g_return_val_if_fail (application_id != NULL, NULL);

	return g_object_new (WUMING_TYPE_APPLICATION,
	                     "application-id", application_id,
	                     "flags", flags,
	                     "resource-base-path", "/com/ericlin/wuming",
	                     NULL);
}

static void
wuming_application_activate (GApplication *app)
{
	GtkWindow *window;

	g_assert (WUMING_IS_APPLICATION (app));

	window = gtk_application_get_active_window (GTK_APPLICATION (app));

	if (window == NULL)
		window = g_object_new (WUMING_TYPE_WINDOW,
		                       "application", app,
		                       NULL);

	gtk_window_present (window);
}

static void
wuming_application_class_init (WumingApplicationClass *klass)
{
	GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

	app_class->activate = wuming_application_activate;
}

static void
wuming_application_about_action (GSimpleAction *action,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
	static const char *developers[] = {"EricLin", NULL};
	WumingApplication *self = user_data;
	GtkWindow *window = NULL;

	g_assert (WUMING_IS_APPLICATION (self));

	window = gtk_application_get_active_window (GTK_APPLICATION (self));

	adw_show_about_dialog (GTK_WIDGET (window),
	                       "application-name", "WuMing",
	                       "application-icon", "com.ericlin.wuming",
	                       "developer-name", "EricLin",
	                       "translator-credits", _("translator-credits"),
	                       "version", "0.2.beta3",
	                       "developers", developers,
	                       "copyright", "© 2025 EricLin",
                           "license-type", GTK_LICENSE_GPL_3_0,
	                       NULL);
}

static void
wuming_application_quit_action (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
	WumingApplication *self = user_data;

	g_assert (WUMING_IS_APPLICATION (self));

	g_application_quit (G_APPLICATION (self));
}

static const GActionEntry app_actions[] = {
	{ "quit", wuming_application_quit_action },
	{ "about", wuming_application_about_action },
};

static void
wuming_application_init (WumingApplication *self)
{
	g_action_map_add_action_entries (G_ACTION_MAP (self),
	                                 app_actions,
	                                 G_N_ELEMENTS (app_actions),
	                                 self);
	gtk_application_set_accels_for_action (GTK_APPLICATION (self),
	                                       "app.quit",
	                                       (const char *[]) { "<primary>q", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (self),
	                                       "app.scan-file",
	                                       (const char *[]) { "<primary>s", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (self),
	                                       "app.scan-folder",
	                                       (const char *[]) { "<primary>f", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (self),
	                                       "app.update",
	                                       (const char *[]) { "<primary>u", NULL });
}
