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

struct _SecurityOverviewPage {
    GtkWidget parent_instance;

    AdwBreakpointBin *break_point;
    GtkButton *scan_overview_button;
    GtkButton *signature_overview_button;
};

G_DEFINE_FINAL_TYPE (SecurityOverviewPage, security_overview_page, GTK_TYPE_WIDGET)

/* GObject essential functions */

static void
security_overview_page_dispose (GObject *object)
{
    SecurityOverviewPage *self = SECURITY_OVERVIEW_PAGE (object);

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
    self->signature_overview_button = NULL;

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
}