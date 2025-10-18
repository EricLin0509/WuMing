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
typedef struct {
  /* Protected by mutex */
  GMutex mutex; // Only protect "completed" and "success" fields
  gboolean completed;
  gboolean success;

  /*No need to protect these fields because they always same after initialize*/
  WumingWindow *window;
  SecurityOverviewPage *security_overview_page;
  UpdateSignaturePage *update_signature_page;
  UpdatingPage *updating_page;

  /* Protected by atomic ref count */
  volatile gint ref_count;
} UpdateContext;

static UpdateContext*
update_context_new(void)
{
  UpdateContext *ctx = g_new0(UpdateContext, 1);
  g_mutex_init(&ctx->mutex);
  ctx->ref_count = 1;
  return ctx;
}

static gpointer
update_context_ref(gpointer ctx)
{
  UpdateContext *context = (UpdateContext*)ctx;
  g_return_val_if_fail(context != NULL, NULL);
  g_return_val_if_fail(g_atomic_int_get(&context->ref_count) > 0, NULL);
  if (context) g_atomic_int_inc(&context->ref_count);
  return context;
}

static void
update_context_unref(gpointer ctx)
{
  UpdateContext *context = (UpdateContext*)ctx;
  if (context && g_atomic_int_dec_and_test(&context->ref_count))
  {
    g_mutex_clear(&context->mutex);
    g_atomic_int_set(&context->ref_count, INT_MIN);
    g_free(context);
  }
}


/* thread-safe method to get/set states */
static void
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

static void
resource_clean_up(gpointer user_data)
{
  IdleData *data = (IdleData *)user_data; // Cast the data to IdleData struct
  update_context_unref (data->context);
  g_free(data->message);
  g_free(data);
}

static gboolean
update_complete_callback(gpointer user_data)
{
  IdleData *data = user_data;
  UpdateContext *ctx = data->context;

  g_return_val_if_fail(data && ctx && g_atomic_int_get(&ctx->ref_count) > 0, 
                      G_SOURCE_REMOVE);

  gboolean is_success = FALSE;
  get_completion_state(ctx, NULL, &is_success); // Get the completion state for thread-safe access

  const char *icon_name = is_success ? "status-ok-symbolic" : "status-error-symbolic";

  updating_page_set_final_result(ctx->updating_page, data->message, icon_name);

  if (is_success) // Re-scan the signature if update is successful
  {
    /*Re-scan the signature*/
    signature_status *result = signature_status_new();

    update_signature_page_show_isuptodate(ctx->update_signature_page, result);
    security_overview_page_show_signature_status(ctx->security_overview_page, result);
    security_overview_page_show_health_level(ctx->security_overview_page);

    signature_status_clear(&result);
  }

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
        update_context_unref(ctx);
        return NULL;
    }

    /*Clean up*/
    const gint exit_status = wait_for_process(pid);
    gboolean success = exit_status == 0;
    set_completion_state(ctx, TRUE, success);

    const char *status_text = success ?
        gettext("Update Complete") : gettext("Update Failed");

    send_final_message(update_context_ref, (void *)ctx, status_text, success, update_complete_callback, resource_clean_up);
    
    update_context_unref(ctx);
    return NULL;
}

void
start_update(WumingWindow *window, SecurityOverviewPage *security_overview_page, UpdateSignaturePage *update_signature_page, UpdatingPage *updating_page)
{
  UpdateContext *ctx = update_context_new();

  g_mutex_lock(&ctx->mutex);
  ctx->completed = FALSE;
  ctx->success = FALSE;
  ctx->window = window;
  ctx->security_overview_page = security_overview_page;
  ctx->update_signature_page = update_signature_page;
  ctx->updating_page = updating_page;
  ctx->ref_count = G_ATOMIC_REF_COUNT_INIT;
  g_mutex_unlock(&ctx->mutex);

  /* Start update thread */
  g_thread_new("update-thread", update_thread, ctx);
}

