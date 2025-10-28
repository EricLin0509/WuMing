/* update-signature.c
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
#include <fcntl.h>
#include <poll.h>

#include "subprocess-components.h"
#include "update-signature.h"
#include "signature-status.h"

#define FRESHCLAM_PATH "/usr/bin/freshclam"
#define PKEXEC_PATH "/usr/bin/pkexec"

/*Update Signature*/
typedef struct UpdateContext {
  /* Protected by mutex */
  GMutex mutex; // Only protect "completed" and "success" fields
  gboolean completed;
  gboolean success;

  /*No need to protect these fields because they always same after initialize*/
  WumingWindow *window;
  SecurityOverviewPage *security_overview_page;
  UpdateSignaturePage *update_signature_page;
  UpdatingPage *updating_page;

} UpdateContext;

/* thread-safe method to get/set states */
void
set_completion_state(UpdateContext *ctx, gboolean completed, gboolean success)
{
  g_mutex_lock(&ctx->mutex);
  ctx->completed = completed;
  ctx->success = success;
  g_mutex_unlock(&ctx->mutex);
}

static void
get_completion_state(UpdateContext *ctx, gboolean *out_completed, gboolean *out_success)
{
  g_mutex_lock(&ctx->mutex);
  /* If one of the output pointers is NULL, only return the another one */
  if (out_completed) *out_completed = ctx->completed;
  if (out_success) *out_success = ctx->success;
  g_mutex_unlock(&ctx->mutex);
}

static gboolean
update_complete_callback(gpointer user_data)
{
  IdleData *data = user_data;
  UpdateContext *ctx = (UpdateContext *)get_idle_context(data);

  g_return_val_if_fail(data && ctx, G_SOURCE_REMOVE);

  gboolean is_success = FALSE;
  get_completion_state(ctx, NULL, &is_success); // Get the completion state for thread-safe access

  const char *icon_name = is_success ? "status-ok-symbolic" : "status-error-symbolic";
  const char *message = get_idle_message(data);

  updating_page_set_final_result(ctx->updating_page, message, icon_name);

  if (is_success) // Re-scan the signature if update is successful
  {
    /*Re-scan the signature*/
    signature_status *result = signature_status_new();

    update_signature_page_show_isuptodate(ctx->update_signature_page, result);
    security_overview_page_show_signature_status(ctx->security_overview_page, result);
    security_overview_page_show_health_level(ctx->security_overview_page);

    signature_status_clear(&result);
  }

  wuming_window_set_hide_on_close(ctx->window, FALSE); // Allow the window to be closed when update is complete

  return G_SOURCE_REMOVE;
}

static gpointer
update_thread(gpointer data)
{
    UpdateContext *ctx = data;
    pid_t pid;
    
    /*Spawn update process*/
    if (!spawn_new_process_no_pipes(&pid,
        PKEXEC_PATH, "pkexec", FRESHCLAM_PATH, "--verbose", NULL))
    {
        return NULL;
    }

    /*Clean up*/
    const gint exit_status = wait_for_process(pid);
    gboolean success = exit_status == 0;
    set_completion_state(ctx, TRUE, success);

    const char *status_text = success ?
        gettext("Update Complete") : gettext("Update Failed");

    send_final_message((void *)ctx, status_text, success, update_complete_callback);

    return NULL;
}

UpdateContext *
update_context_new(WumingWindow *window, SecurityOverviewPage *security_overview_page, UpdateSignaturePage *update_signature_page, UpdatingPage *updating_page)
{
  g_return_val_if_fail(window && security_overview_page && update_signature_page && updating_page, NULL);

  UpdateContext *ctx = g_new0(UpdateContext, 1);
  g_mutex_init(&ctx->mutex);

  ctx->completed = FALSE;
  ctx->success = FALSE;
  ctx->window = window;
  ctx->security_overview_page = security_overview_page;
  ctx->update_signature_page = update_signature_page;
  ctx->updating_page = updating_page;

  return ctx;
}

void
update_context_clear(UpdateContext **ctx)
{
  g_return_if_fail(ctx && *ctx);

  g_mutex_clear(&(*ctx)->mutex);
  g_free(*ctx);
  *ctx = NULL;
}

static void
update_context_reset(UpdateContext *ctx)
{
  g_return_if_fail(ctx);

  /* Reset `UpdateContext` */
  set_completion_state(ctx, FALSE, FALSE);

  /* Reset Widgets */
  updating_page_reset(ctx->updating_page);
}

void
start_update(UpdateContext *ctx)
{
  g_return_if_fail(ctx);

  /* Reset `UpdateContext` */
  update_context_reset(ctx);

  wuming_window_push_page_by_tag (ctx->window, "updating_nav_page");
  wuming_window_set_hide_on_close(ctx->window, TRUE); // Hide the window instead of closing it

  /* Start update thread */
  g_thread_new("update-thread", update_thread, ctx);
}

