/* check-history-page.c
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

#include "check-history-page.h"

struct _CheckHistoryPage {
    GtkWidget          parent_instance;

    /* Child */
    AdwClamp            *clamp;
    GtkListBox         *list_box;
};

G_DEFINE_FINAL_TYPE (CheckHistoryPage, check_history_page, GTK_TYPE_WIDGET)

/*GObject Essential Functions */

static void
check_history_page_dispose (GObject *object)
{
    CheckHistoryPage *self = CHECK_HISTORY_PAGE (object);

    GtkWidget *clamp = GTK_WIDGET (self->clamp);

    g_clear_pointer (&clamp, gtk_widget_unparent);

    G_OBJECT_CLASS (check_history_page_parent_class)->dispose (object);
}

static void
check_history_page_finalize (GObject *object)
{
    CheckHistoryPage *self = CHECK_HISTORY_PAGE (object);

    self->clamp = NULL;
    self->list_box = NULL;

    G_OBJECT_CLASS (check_history_page_parent_class)->finalize (object);
}

static void
check_history_page_class_init (CheckHistoryPageClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->dispose = check_history_page_dispose;
    gobject_class->finalize = check_history_page_finalize;

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);

    gtk_widget_class_set_template_from_resource (widget_class, "/com/ericlin/wuming/pages/check-history-page.ui");

    gtk_widget_class_bind_template_child (widget_class, CheckHistoryPage, clamp);
    gtk_widget_class_bind_template_child (widget_class, CheckHistoryPage, list_box);
}

GtkWidget *
check_history_page_new (void)
{
    return g_object_new (CHECK_HISTORY_TYPE_PAGE, NULL);
}

static void
check_history_page_init (CheckHistoryPage *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));
}