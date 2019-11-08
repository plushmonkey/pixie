#ifndef PIXIE_PROTOCOL_H_
#define PIXIE_PROTOCOL_H_

#include "pixie.h"

typedef enum {
  PXE_PROTOCOL_STATE_HANDSHAKING = 0,
  PXE_PROTOCOL_STATE_STATUS,
  PXE_PROTOCOL_STATE_LOGIN,
  PXE_PROTOCOL_STATE_PLAY,

  PXE_PROTOCOL_STATE_COUNT,
} pxe_protocol_state;

typedef enum {
  PXE_PROTOCOL_INBOUND_HANDSHAKING_HANDSHAKE = 0x00
} pxe_protocol_inbound_handshaking_id;

typedef enum {
  PXE_PROTOCOL_INBOUND_STATUS_REQUEST = 0x00,
  PXE_PROTOCOL_INBOUND_STATUS_PING,
} pxe_protocol_inbound_status_id;

typedef enum {
  PXE_PROTOCOL_OUTBOUND_STATUS_RESPONSE = 0x00,
  PXE_PROTOCOL_OUTBOUND_STATUS_PONG,
} pxe_protocol_outbound_status_id;

typedef enum {
  PXE_PROTOCOL_INBOUND_LOGIN_START = 0x00,
  PXE_PROTOCOL_INBOUND_LOGIN_ENCRYPTION_RESPONSE,
  PXE_PROTOCOL_INBOUND_LOGIN_PLUGIN_RESPONSE,
} pxe_protocol_inbound_login_id;

typedef enum {
  PXE_PROTOCOL_OUTBOUND_LOGIN_DISCONNECT = 0x00,
  PXE_PROTOCOL_OUTBOUND_LOGIN_ENCRYPTION_REQUEST,
  PXE_PROTOCOL_OUTBOUND_LOGIN_SUCCESS,
  PXE_PROTOCOL_OUTBOUND_LOGIN_SET_COMPRESSION,
  PXE_PROTOCOL_OUTBOUND_LOGIN_PLUGIN_REQUEST,
} pxe_protocol_outbound_login_id;

typedef enum {
  PXE_PROTOCOL_INBOUND_PLAY_TELEPORT_CONFIRM = 0x00,
  PXE_PROTOCOL_INBOUND_PLAY_CHAT = 0x03,
  PXE_PROTOCOL_INBOUND_PLAY_CLIENT_SETTINGS = 0x05,
  PXE_PROTOCOL_INBOUND_PLAY_PLUGIN_MESSAGE = 0x0B,
  PXE_PROTOCOL_INBOUND_PLAY_PLAYER_POSITION_AND_LOOK = 0x12,
} pxe_protocol_inbound_play_id;

typedef enum {
  PXE_PROTOCOL_OUTBOUND_PLAY_CHAT = 0x0E,
  PXE_PROTOCOL_OUTBOUND_PLAY_DISCONNECT = 0x1A,
  PXE_PROTOCOL_OUTBOUND_PLAY_KEEP_ALIVE = 0x20,
  PXE_PROTOCOL_OUTBOUND_PLAY_CHUNK_DATA = 0x21,
  PXE_PROTOCOL_OUTBOUND_PLAY_JOIN_GAME = 0x25,
  PXE_PROTOCOL_OUTBOUND_PLAY_PLAYER_ABILITIES = 0x31,
  PXE_PROTOCOL_OUTBOUND_PLAY_PLAYER_POSITION_AND_LOOK = 0x35,
} pxe_protocol_outbound_play_id;

#endif
