#ifndef MCLIENT_H
#define MCLIENT_H

/*
 *  $Id$
 *
 *  Copyright (C) 2005, Rick Stuart
 *  http://mvpmc.sourceforge.net/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


#define MCLIENT_DISABLE		0
#define MCLIENT			1
#define MCLIENT_OTHER		2        /* Maybe someday UPNP? */

#define SERVER_PORT    3483

#define CLIENT_PORT    34443

#define STATE_STARTUP 0

#define RECV_BUF_SIZE 65536
#define OUT_BUF_SIZE  131072
#define DISPLAY_SIZE 128
#define LINE_LENGTH 40
#define LINE_COUNT 2

#define CLI_PORT    9090
#define RECV_BUF_SIZE_CLI 32768
#define CLI_MAX_TRACKS 8


/*
 * Command Line Interface Defines.
 */
#define MAX_ID_SIZE 32
#define MAX_CMD_SIZE 32
#define MAX_PARAM_SIZE 64
#define MAX_PARAMS 16
#define MAX_REPLY_LENGTH 256

/*
 * Define states of the cli process.
 */
enum
{
    UPDATE_PLAYLIST_MINMINUS1 = 0,
    UPDATE_PLAYLIST_INDEX,
    UPDATE_PLAYLIST_NUM_TRACKS,
    UPDATE_PLAYLIST_ARTIST,
    UPDATE_PLAYLIST_TITLE,
    UPDATE_PLAYLIST_MAXPLUS1,
    UPDATE_RADIO_MINMINUS1,
    UPDATE_RADIO_STATION,
    UPDATE_RADIO_NOWPLAYING,
    UPDATE_RADIO_MAXPLUS1,
};

/*
 * Define small widget states.
 */
enum
{
    SHOW = 0,
    HIDE,
};

typedef struct
{
    char type;
    char reserved1[5];
    unsigned short wptr;
    unsigned short rptr;
    unsigned short seq;
    char mac_addr[6];
} packet_ack;

typedef struct
{
    void *buf;
    int head;
    int tail;
    int size;
    int playmode;
} ring_buf;

extern ring_buf *outbuf;

typedef struct
{
    char type;
    char reserved1;
    unsigned short wptr;
    unsigned short rptr;
    char reserved2[12];
} request_data_struct;

typedef struct
{
    char type;
    char zero;
    /* optimizer wants to put two bytes here */
    unsigned long time;         /* since startup, in 625kHz ticks = 0.625 * microsecs */
    char codeset;               /* 0xff for JVC */
    char bits;                  /* 16 for JVC */
    /* optimizer wants to put two bytes here */
    unsigned long code;
    char mac_addr[6];
} __attribute__ ((packed)) send_ir_struct; /* be smarter than the optimizer */

typedef struct
{
    char type;
    char control;
    char reserved1[4];
    unsigned short wptr;
    char reserved2[2];
    unsigned short seq;
    char reserved3[6];
} receive_mpeg_header;


typedef struct
{
    char player_id[MAX_ID_SIZE];
    char cmd[MAX_CMD_SIZE];
    char param[MAX_PARAMS][MAX_PARAM_SIZE];
} mclient_cmd;

/*
 * Stores cli data and states.
 */
typedef struct
{
    int state;                  // State we are in - what are we asking for over the CLI.
    int tracks;                 // Tracks on disk.
    int index_playing;          // Track we are playing.
    int index_info;             // Track we are gather information on.
    int index_line;             // Line we are printing title on.
    char artist[50];
    char titles[50];
    char title_history[10][50];
} cli_data_type;

/*
 * Functions:
 */
extern int music_client (void);
extern void mclient_idle_callback (mvp_widget_t *);
extern unsigned long curses2ir (int);
extern void mclient_exit (void);
extern void mclient_local_init (void);
extern int mclient_server_connect (void);
extern void receive_volume_data (unsigned short *, int);
extern void read_packet (int);
extern ring_buf *ring_buf_create (int);
extern void receive_mpeg_data (int, receive_mpeg_header *, int);
extern void send_ack (int, unsigned short);
extern void send_discovery (int);
extern void cli_init (void);
extern void *mcg_socket_reader_thread (void *);
extern void cli_send_discovery (int);
extern void cli_update_playlist (int socket);
extern void cli_send_packet (int, char *);
extern int cli_read_message (int, char *);
extern void cli_read_data (int);
extern mvp_widget_t *mclient;
extern char *mclient_server;
extern int cli_server_connect (void);
extern void cli_pick_starting_index (void);
extern int cli_decode_response (char *, mclient_cmd *);
extern void cli_parse_playlist (mclient_cmd *);
extern void cli_parse_display (mclient_cmd *);
extern void cli_parse_player (mclient_cmd *);


/*
 * Track which music server is being used (none, slim, UPNP,...)
 */
extern int mclient_type;

/*
 * Need the MAC to uniquely identify this mvpmc box to
 * mclient server.
 */
extern unsigned char *mac_address_ptr;

extern pthread_mutex_t mclient_mutex;

extern int mclient_socket;

extern struct in_addr *server_addr_mclient;

extern struct timeval uptime;   /* time we started */

extern void *recvbuf;

extern char *recvbuf_back;

extern char slimp3_display[];

/*
 * Tracks states of cli interface.
 */
extern cli_data_type cli_data;

/*
 * Tracks timeout of small mclient widget.
 */
extern int cli_small_widget_timeout;
extern int cli_small_widget_state;

/*
 * Tracks delays between identical CLI states.
 */
extern int cli_identical_state_interval_timer;

#endif /* MCLIENT_H */
