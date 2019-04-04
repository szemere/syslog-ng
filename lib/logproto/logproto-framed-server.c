/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#include "logproto-framed-server.h"
#include "messages.h"

#include <errno.h>
#include <ctype.h>
#include <string.h>

#define FRAME_LEN_SIZE 10

typedef enum
{
  LPFSS_FRAME_READ,
  LPFSS_FRAME_EXTRACT,
  LPFSS_MESSAGE_READ,
  LPFSS_MESSAGE_EXTRACT,
  LPFSS_TRIM_MESSAGE,
  LPFSS_TRIM_MESSAGE_READ,
  LPFSS_CONSUME_TRIMMED
} LogProtoFramedServerState;

typedef struct _LogProtoFramedServer
{
  LogProtoServer super;
  LogProtoFramedServerState state;

  guchar *buffer;
  guint32 buffer_size, buffer_pos, buffer_end;
  guint32 frame_len;
  gboolean half_message_in_buffer;
} LogProtoFramedServer;

static LogProtoPrepareAction
log_proto_framed_server_prepare(LogProtoServer *s, GIOCondition *cond, gint *timeout G_GNUC_UNUSED)
{
  LogProtoFramedServer *self = (LogProtoFramedServer *) s;

  *cond = self->super.transport->cond;

  /* there is a half message in our buffer so try to wait */
  if (!self->half_message_in_buffer)
    {
      if (self->buffer_pos != self->buffer_end)
        {
          /* we have a full message in our buffer so parse it without reading new data from the transport layer */
          return LPPA_FORCE_SCHEDULE_FETCH;
        }
    }

  /* if there's no pending I/O in the transport layer, then we want to do a read */
  if (*cond == 0)
    *cond = G_IO_IN;

  return LPPA_POLL_IO;
}

/* Return value indicates if it was able to read any data from the input.
 * In case of error, resets aux data and sets status to indicate the root cause.
 */
static gboolean
log_proto_framed_server_fetch_data(LogProtoFramedServer *self, gboolean *may_read,
                                   LogTransportAuxData *aux, LogProtoStatus *status)
{
  gint rc;
  *status = LPS_SUCCESS;

  if (self->buffer_pos == self->buffer_end)
    self->buffer_pos = self->buffer_end = 0;

  if (!(*may_read))
    return FALSE;

  rc = log_transport_read(self->super.transport, &self->buffer[self->buffer_end], self->buffer_size - self->buffer_end,
                          aux);

  if (rc < 0)
    {
      if (errno != EAGAIN)
        {
          msg_error("Error reading RFC6587 style framed data",
                    evt_tag_int("fd", self->super.transport->fd),
                    evt_tag_error("error"));
          log_transport_aux_data_reinit(aux);
          *status = LPS_ERROR;
        }
      else
        {
          /* we need more data to parse this message but the data is not available yet */
          self->half_message_in_buffer = TRUE;
        }
    }
  else if (rc == 0)
    {
      msg_trace("EOF occurred while reading",
                evt_tag_int(EVT_TAG_FD, self->super.transport->fd));
      log_transport_aux_data_reinit(aux);
      *status = LPS_EOF;
    }
  else
    {
      self->buffer_end += rc;
      return TRUE;
    }
  return FALSE;
}

static gboolean
log_proto_framed_server_extract_frame_length(LogProtoFramedServer *self, gboolean *need_more_data)
{
  gint i;

  *need_more_data = TRUE;
  self->frame_len = 0;
  for (i = self->buffer_pos; i < self->buffer_end; i++)
    {
      if (isdigit(self->buffer[i]) && (i - self->buffer_pos < FRAME_LEN_SIZE))
        {
          self->frame_len = self->frame_len * 10 + (self->buffer[i] - '0');
        }
      else if (self->buffer[i] == ' ')
        {
          *need_more_data = FALSE;
          self->buffer_pos = i + 1;
          return TRUE;
        }
      else
        {
          msg_error("Invalid frame header",
                    evt_tag_printf("header", "%.*s", (gint) (i - self->buffer_pos), &self->buffer[self->buffer_pos]));
          return FALSE;
        }
    }
  /* couldn't extract frame header, no error but need more data */
  return TRUE;
}

static void
_adjust_buffer_if_needed(LogProtoFramedServer *self, const gsize minimum_space_required)
{
  /* We need at least minimum_space_required amount of free space in the buffer.
   * Since we already moving memory, move it to the beginning. */
  if (self->buffer_size - self->buffer_pos < minimum_space_required)
    {
      memmove(self->buffer, &self->buffer[self->buffer_pos], self->buffer_end - self->buffer_pos);
      self->buffer_end = self->buffer_end - self->buffer_pos;
      self->buffer_pos = 0;
    }
}

/* Returns TRUE if successfully finished consuming the data. Othwerwise it is not finished, but
 * there is nothing left to read (or there was a read error) and expects to be called again. */
static gboolean
_consume_trimmed_part(LogProtoFramedServer *self, gboolean *may_read,
                      LogTransportAuxData *aux, LogProtoStatus *status)
{
  gboolean buffer_was_full;
  /* Since trimming requires a full (buffer sized) message, the consuming
   * always starts at the beginning of the buffer, with a new read. */
  self->half_message_in_buffer = FALSE;

  while (1)
    {
      if (!log_proto_framed_server_fetch_data(self, may_read, aux, status))
        return FALSE;

      // We have some data, but it is not enough.
      if (self->buffer_end < self->frame_len)
        {
          self->frame_len -= self->buffer_end;
          buffer_was_full = self->buffer_end == self->buffer_size;
          self->buffer_end = 0;

          // We might have more data, waiting for read.
          if (buffer_was_full)
            continue;

          // We haven't finished yet, but there is no more data. status is TRUE from fetch.
          return FALSE;
        }

      // We have enough data to finish consuming the message.
      self->buffer_pos += self->frame_len;
      return TRUE;
    }
}

static LogProtoStatus
log_proto_framed_server_fetch(LogProtoServer *s, const guchar **msg, gsize *msg_len, gboolean *may_read,
                              LogTransportAuxData *aux, Bookmark *bookmark)
{
  LogProtoFramedServer *self = (LogProtoFramedServer *) s;
  LogProtoStatus status;
  gboolean need_more_data;

  if (G_UNLIKELY(!self->buffer))
    {
      self->buffer_size = self->super.options->init_buffer_size;
      self->buffer = g_malloc(self->buffer_size);
    }

  while (1)
    {
      switch (self->state)
        {
        case LPFSS_FRAME_READ:
          if (!log_proto_framed_server_fetch_data(self, may_read, aux, &status))
            return status;
          self->state = LPFSS_FRAME_EXTRACT;

        case LPFSS_FRAME_EXTRACT:
          if (!log_proto_framed_server_extract_frame_length(self, &need_more_data))
            {
              /* invalid frame header */
              log_transport_aux_data_reinit(aux);
              return LPS_ERROR;
            }
          if (need_more_data)
            {
              self->state = LPFSS_FRAME_READ;
              _adjust_buffer_if_needed(self, FRAME_LEN_SIZE);
              continue;
            }

          self->state = LPFSS_MESSAGE_EXTRACT;
          if (self->frame_len > self->super.options->max_msg_size)
            {
              if (self->super.options->trim_large_messages)
                {
                  msg_error("Incoming frame larger than log_msg_size(), need to trim.",
                            evt_tag_int("log_msg_size", self->super.options->max_msg_size),
                            evt_tag_int("frame_length", self->frame_len));
                  self->state = LPFSS_TRIM_MESSAGE_READ;
                }
              else
                {
                  msg_error("Incoming frame larger than log_msg_size()",
                            evt_tag_int("log_msg_size", self->super.options->max_msg_size),
                            evt_tag_int("frame_length", self->frame_len));
                  log_transport_aux_data_reinit(aux);
                  return LPS_ERROR;
                }
            }
          _adjust_buffer_if_needed(self, self->frame_len);
          continue;

        case LPFSS_TRIM_MESSAGE_READ:
          if (!log_proto_framed_server_fetch_data(self, may_read, aux, &status))
            return status;
          self->state = LPFSS_TRIM_MESSAGE;

        case LPFSS_TRIM_MESSAGE:
          if (self->buffer_end == self->buffer_size)
            {
              /* The buffer is full */
              *msg = &self->buffer[self->buffer_pos];
              *msg_len = self->buffer_end - self->buffer_pos;
              self->frame_len -= *msg_len;

              self->state = LPFSS_CONSUME_TRIMMED;
              self->half_message_in_buffer = TRUE;
              self->buffer_pos = self->buffer_end = 0;
              aux->trimmed = TRUE;
              return LPS_SUCCESS;
            }
          self->state = LPFSS_TRIM_MESSAGE_READ;
          continue;

        case LPFSS_CONSUME_TRIMMED:
          if (_consume_trimmed_part(self, may_read, aux, &status))
            {
              self->state = LPFSS_FRAME_EXTRACT;
              /* If there is data in the buffer, try to process it immediately.
               * (or if we reached the end, than MIGHT be data waiting for read.) */
              if ((self->buffer_pos != self->buffer_end) ||
                  (self->buffer_end == self->buffer_size))
                {
                  continue;
                }
            }
          return status;

        case LPFSS_MESSAGE_READ:
          if (!log_proto_framed_server_fetch_data(self, may_read, aux, &status))
            return status;
          self->state = LPFSS_MESSAGE_EXTRACT;

        case LPFSS_MESSAGE_EXTRACT:
          /* NOTE: here we can assume that the complete message fits into
           * the buffer because of the checks/move operation in the
           * LPFSS_FRAME_READ state */
          if (self->buffer_end - self->buffer_pos >= self->frame_len)
            {
              /* ok, we already have the complete message */
              *msg = &self->buffer[self->buffer_pos];
              *msg_len = self->frame_len;
              self->buffer_pos += self->frame_len;
              self->state = LPFSS_FRAME_EXTRACT;

              /* we have the full message here so reset the half message flag */
              self->half_message_in_buffer = FALSE;
              return LPS_SUCCESS;
            }
          self->state = LPFSS_MESSAGE_READ;
          continue;

        default:
          break;
        }
    }
}

static void
log_proto_framed_server_free(LogProtoServer *s)
{
  LogProtoFramedServer *self = (LogProtoFramedServer *) s;
  g_free(self->buffer);

  log_proto_server_free_method(s);
}

LogProtoServer *
log_proto_framed_server_new(LogTransport *transport, const LogProtoServerOptions *options)
{
  LogProtoFramedServer *self = g_new0(LogProtoFramedServer, 1);

  log_proto_server_init(&self->super, transport, options);
  self->super.prepare = log_proto_framed_server_prepare;
  self->super.fetch = log_proto_framed_server_fetch;
  self->super.free_fn = log_proto_framed_server_free;
  self->half_message_in_buffer = FALSE;
  self->state = LPFSS_FRAME_READ;
  return &self->super;
}
