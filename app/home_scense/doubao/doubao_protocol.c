/****************************************************************************
 * app/home_scense/doubao/doubao_protocol.c
 * Doubao RealtimeAPI binary envelope helpers.
 ****************************************************************************/

#include "doubao_protocol.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DOUBAO_VERSION_HEADER_4      0x11
#define DOUBAO_FULL_CLIENT_EVENT     0x14
#define DOUBAO_AUDIO_CLIENT_EVENT    0x24
#define DOUBAO_FULL_SERVER_EVENT     0x90
#define DOUBAO_AUDIO_SERVER_EVENT    0xb0
#define DOUBAO_ERROR_MESSAGE         0xf0
#define DOUBAO_SERIALIZATION_JSON    0x10
#define DOUBAO_SERIALIZATION_RAW     0x00
#define DOUBAO_NO_COMPRESSION         0x00

static void write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)(value >> 24);
  output[1] = (uint8_t)(value >> 16);
  output[2] = (uint8_t)(value >> 8);
  output[3] = (uint8_t)value;
}

static uint32_t read_u32(const uint8_t *input)
{
  return ((uint32_t)input[0] << 24) |
         ((uint32_t)input[1] << 16) |
         ((uint32_t)input[2] << 8) |
         (uint32_t)input[3];
}

static int encode_event_packet(uint8_t message_type, uint8_t serialization,
                               int event, const char *session_id,
                               const uint8_t *payload, size_t payload_size,
                               uint8_t *output, size_t capacity,
                               size_t *output_size)
{
  size_t session_size;
  size_t needed;
  uint8_t *cursor;

  if (!session_id || !output || !output_size ||
      (!payload && payload_size > 0) || payload_size > UINT32_MAX)
    {
      return -EINVAL;
    }

  session_size = strlen(session_id);
  if (session_size > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  /* Connection events omit the session-id field. */
  if (event == DOUBAO_EVENT_START_CONNECTION ||
      event == DOUBAO_EVENT_FINISH_CONNECTION)
    {
      session_size = 0;
      needed = 4 + 4 + 4 + payload_size;
    }
  else
    {
      needed = 4 + 4 + 4 + session_size + 4 + payload_size;
    }

  if (capacity < needed)
    {
      return -EMSGSIZE;
    }

  output[0] = DOUBAO_VERSION_HEADER_4;
  output[1] = message_type;
  output[2] = serialization | DOUBAO_NO_COMPRESSION;
  output[3] = 0;
  cursor = output + 4;

  write_u32(cursor, (uint32_t)event);
  cursor += 4;

  if (event != DOUBAO_EVENT_START_CONNECTION &&
      event != DOUBAO_EVENT_FINISH_CONNECTION)
    {
      write_u32(cursor, (uint32_t)session_size);
      cursor += 4;
      memcpy(cursor, session_id, session_size);
      cursor += session_size;
    }

  write_u32(cursor, (uint32_t)payload_size);
  cursor += 4;
  if (payload_size > 0)
    {
      memcpy(cursor, payload, payload_size);
    }

  *output_size = needed;
  return 0;
}

int doubao_protocol_encode_json(int event, const char *session_id,
                                const char *json, uint8_t *output,
                                size_t capacity, size_t *output_size)
{
  if (!json)
    {
      return -EINVAL;
    }

  return encode_event_packet(DOUBAO_FULL_CLIENT_EVENT,
                             DOUBAO_SERIALIZATION_JSON, event, session_id,
                             (const uint8_t *)json, strlen(json), output,
                             capacity, output_size);
}

int doubao_protocol_encode_audio(int event, const char *session_id,
                                 const uint8_t *audio, size_t audio_size,
                                 uint8_t *output, size_t capacity,
                                 size_t *output_size)
{
  return encode_event_packet(DOUBAO_AUDIO_CLIENT_EVENT,
                             DOUBAO_SERIALIZATION_RAW, event, session_id,
                             audio, audio_size, output, capacity, output_size);
}

int doubao_protocol_decode(const uint8_t *input, size_t input_size,
                           doubao_packet_t *packet)
{
  uint8_t message_type;
  uint8_t flags;
  const uint8_t *cursor;
  size_t remaining;
  uint32_t session_size;
  uint32_t payload_size;

  if (!input || !packet || input_size < 8 || input[0] != DOUBAO_VERSION_HEADER_4)
    {
      return -EINVAL;
    }

  message_type = input[1] & 0xf0;
  flags = input[1] & 0x0f;
  cursor = input + 4;
  remaining = input_size - 4;
  memset(packet, 0, sizeof(*packet));

  if (message_type == DOUBAO_ERROR_MESSAGE)
    {
      if (remaining < 8)
        {
          return -EMSGSIZE;
        }
      packet->error_code = (int)read_u32(cursor);
      cursor += 4;
      remaining -= 4;
      packet->kind = DOUBAO_PACKET_ERROR;
    }
  else if (message_type == DOUBAO_FULL_SERVER_EVENT)
    {
      packet->kind = DOUBAO_PACKET_JSON;
    }
  else if (message_type == DOUBAO_AUDIO_SERVER_EVENT)
    {
      packet->kind = DOUBAO_PACKET_AUDIO;
    }
  else
    {
      return -EPROTO;
    }

  if (flags & 0x01 || flags & 0x03)
    {
      if (remaining < 4)
        {
          return -EMSGSIZE;
        }
      packet->sequence = (int32_t)read_u32(cursor);
      cursor += 4;
      remaining -= 4;
    }

  if (flags & 0x04)
    {
      if (remaining < 4)
        {
          return -EMSGSIZE;
        }
      packet->event = (int)read_u32(cursor);
      cursor += 4;
      remaining -= 4;

      if (packet->event == DOUBAO_EVENT_CONNECTION_STARTED ||
          packet->event == 51 || packet->event == 52)
        {
          /* Connection events contain connect-id instead of session-id. */
          if (remaining < 4)
            {
              return -EMSGSIZE;
            }
          session_size = read_u32(cursor);
          cursor += 4;
          remaining -= 4;
          if (session_size > remaining)
            {
              return -EMSGSIZE;
            }
          cursor += session_size;
          remaining -= session_size;
        }
      else if (remaining >= 4)
        {
          session_size = read_u32(cursor);
          cursor += 4;
          remaining -= 4;
          if (session_size > remaining)
            {
              return -EMSGSIZE;
            }
          cursor += session_size;
          remaining -= session_size;
        }
    }

  if (remaining < 4)
    {
      return -EMSGSIZE;
    }

  payload_size = read_u32(cursor);
  cursor += 4;
  remaining -= 4;
  if (payload_size > remaining)
    {
      return -EMSGSIZE;
    }

  packet->payload = cursor;
  packet->payload_size = payload_size;
  return 0;
}

int doubao_protocol_get_event(const uint8_t *json, size_t json_size)
{
  char local[64];
  const char *key = "\"event\":";
  const char *start;
  size_t length;

  if (!json || json_size == 0 || json_size >= sizeof(local))
    {
      return 0;
    }

  memcpy(local, json, json_size);
  local[json_size] = '\0';
  start = strstr(local, key);
  if (!start)
    {
      return 0;
    }

  start += strlen(key);
  length = strspn(start, "0123456789");
  return length ? (int)strtol(start, NULL, 10) : 0;
}

bool doubao_protocol_get_text(const uint8_t *json, size_t json_size,
                              const char *key, char *output,
                              size_t output_size)
{
  const uint8_t *cursor;
  const uint8_t *end;
  size_t key_size;
  size_t written = 0;

  if (!json || !key || !output || output_size == 0 || json_size == 0)
    {
      return false;
    }

  key_size = strlen(key);
  cursor = json;
  end = json + json_size;
  while (cursor < end)
    {
      if (*cursor++ != '"')
        {
          continue;
        }
      if ((size_t)(end - cursor) < key_size ||
          memcmp(cursor, key, key_size) != 0 ||
          cursor + key_size >= end || cursor[key_size] != '"')
        {
          continue;
        }
      cursor += key_size + 1;
      while (cursor < end && isspace((unsigned char)*cursor))
        {
          cursor++;
        }
      if (cursor == end || *cursor++ != ':')
        {
          continue;
        }
      while (cursor < end && isspace((unsigned char)*cursor))
        {
          cursor++;
        }
      if (cursor == end || *cursor++ != '"')
        {
          continue;
        }

      while (cursor < end && *cursor != '"')
        {
          uint8_t value = *cursor++;
          if (value == '\\')
            {
              if (cursor == end)
                {
                  return false;
                }
              value = *cursor++;
              switch (value)
                {
                  case 'b': value = '\b'; break;
                  case 'f': value = '\f'; break;
                  case 'n': value = '\n'; break;
                  case 'r': value = '\r'; break;
                  case 't': value = '\t'; break;
                  case 'u':
                    if ((size_t)(end - cursor) < 4)
                      {
                        return false;
                      }
                    cursor += 4;
                    value = '?';
                    break;
                  default: break;
                }
            }
          if (written + 1 < output_size)
            {
              output[written++] = (char)value;
            }
        }
      if (cursor == end)
        {
          return false;
        }
      output[written] = '\0';
      return true;
    }

  return false;
}
