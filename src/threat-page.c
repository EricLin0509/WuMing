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

#include "threat-page.h"

struct _ThreatPage {
    GtkWidget parent_instance;

    AdwToolbarView *toolbar_view;
    GtkListBox *threat_list;
};

G_DEFINE_FINAL_TYPE(ThreatPage, threat_page, GTK_TYPE_WIDGET)

void
threat_page_add_threat (ThreatPage *self, GtkWidget *row)
{
    gtk_list_box_prepend (self->threat_list, GTK_WIDGET (row));
}

void
threat_page_remove_threat (ThreatPage *self, GtkWidget *row)
{
    gtk_list_box_remove (self->threat_list, row);
}

void
threat_page_clear (ThreatPage *self)
{
    gtk_list_box_remove_all (self->threat_list); // Remove all items from the list
}

/* GObject essential functions */

static void
threat_page_dispose (GObject *object)
{
    ThreatPage *self = THREAT_PAGE (object);

    GtkWidget *toolbar_view = GTK_WIDGET (self->toolbar_view);

    threat_page_clear (self);
    g_clear_pointer (&toolbar_view, gtk_widget_unparent);

    G_OBJECT_CLASS (threat_page_parent_class)->dispose(object);
}

static void
threat_page_finalize (GObject *object)
{
    ThreatPage *self = THREAT_PAGE (object);

    /* Reset all child widgets */
    self->toolbar_view = NULL;
    self->threat_list = NULL;

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
    gtk_widget_class_bind_template_child (widget_class, ThreatPage, threat_list);
}

static void
threat_page_init (ThreatPage *self)
{
    gtk_widget_init_template (GTK_WIDGET(self));
}

GtkWidget *
threat_page_new (void)
{
    return g_object_new (THREAT_TYPE_PAGE, NULL);
}