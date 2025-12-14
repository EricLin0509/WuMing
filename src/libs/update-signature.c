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

  pid_t pid;

  /*No need to protect these fields because they always same after initialize*/
  WumingWindow *window;
  gulong popped_signal_id;
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
  g_autofree char *error_message = NULL;

  updating_page_set_final_result(ctx->updating_page, message, icon_name);

  if (is_success) // Re-scan the signature if update is successful
  {
    /*Re-scan the signature*/
    wuming_window_update_signature_status (ctx->window, TRUE, -1); // Use -1 to ingore the expiration time
  }
  else // Show error message if update is failed
  {
    /* Show error message */
    int exit_status = get_idle_exit_status(data);
    error_message = g_strdup_printf(gettext("Signature update failed with exit status %d"), exit_status);
    wuming_window_send_toast_notification(ctx->window, error_message, 10);
  }

  if (!wuming_window_is_hide(ctx->window))
  {
    wuming_window_send_notification(ctx->window, G_NOTIFICATION_PRIORITY_URGENT, message, error_message); // Send notification if the window is not active
  }

  wuming_window_set_hide_on_close(ctx->window, FALSE, NULL); // Allow the window to be closed when update is complete

  return G_SOURCE_REMOVE;
}

static gboolean
update_sync_callback(gpointer user_data)
{
  UpdateContext *ctx = user_data;
  pid_t pid = ctx->pid;

  const gint exit_status = wait_for_process(pid, WNOHANG);
  if (exit_status == -1) return G_SOURCE_CONTINUE; // Process is still running

  gboolean success = exit_status == 0;
  set_completion_state(ctx, TRUE, success);

  const char *status_text = success ?
      gettext("Signature Update Complete") : gettext("Signature Update Failed");

  send_final_message((void *)ctx, status_text, success, exit_status, update_complete_callback);

  return G_SOURCE_REMOVE;
}

static void
start_update_async(UpdateContext *ctx)
{
  /*Spawn update process*/
  if (!spawn_new_process_no_pipes(&ctx->pid,
      PKEXEC_PATH, "pkexec", FRESHCLAM_PATH, "--verbose", NULL))
  {
      g_warning("Failed to spawn freshclam process");
      send_final_message((void *)ctx, gettext("Signature Update Failed"), FALSE, -1, update_complete_callback);
      return;
  }

  /* Use Async I/O to check the update status */
  GSource *source = g_timeout_source_new(BASE_TIMEOUT_MS);
  g_source_set_callback(source, (GSourceFunc)update_sync_callback, ctx, NULL);
  g_source_attach(source, g_main_context_default());
}

void
update_context_clear(UpdateContext **ctx)
{
  g_return_if_fail(ctx && *ctx);

  /* Revoke signals */
  wuming_window_revoke_popped_signal((*ctx)->window, (*ctx)->popped_signal_id);

  g_mutex_clear(&(*ctx)->mutex);
  g_free(*ctx);
  *ctx = NULL;
}

static void
update_context_reset(UpdateContext *ctx)
{
  g_return_if_fail(ctx);

  ctx->pid = 0; // Reset PID

  /* Reset `UpdateContext` */
  set_completion_state(ctx, FALSE, FALSE);

  /* Reset Widgets */
  updating_page_reset(ctx->updating_page);
}

static void
on_page_popped(AdwNavigationView* self, AdwNavigationPage* page, gpointer user_data)
{
  g_return_if_fail(self && page && user_data);

  UpdateContext *ctx = user_data;

  const char *tag = adw_navigation_page_get_tag(page);

  if (g_strcmp0(tag, "updating_nav_page") == 0)
    update_context_reset(ctx);
}

UpdateContext*
update_context_new(WumingWindow *window, UpdatingPage *updating_page)
{
  g_return_val_if_fail(window && updating_page, NULL);

  UpdateContext *ctx = g_new0(UpdateContext, 1);
  g_mutex_init(&ctx->mutex);

  ctx->completed = FALSE;
  ctx->success = FALSE;
  ctx->window = window;
  ctx->updating_page = updating_page;

  /* Connect signals */
  ctx->popped_signal_id = wuming_window_connect_popped_signal(window, (GCallback)on_page_popped, ctx);

  return ctx;
}

void
start_update(UpdateContext *ctx)
{
  g_return_if_fail(ctx);

  /* Reset `UpdateContext` */
  update_context_reset(ctx);

  wuming_window_push_page_by_tag (ctx->window, "updating_nav_page");
  wuming_window_set_hide_on_close(ctx->window, TRUE, gettext("Updating...")); // Hide the window instead of closing it

  start_update_async(ctx);
}

