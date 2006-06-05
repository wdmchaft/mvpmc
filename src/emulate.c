 /*
 *  Copyright (C) 2004, 2005, 2006, Jon Gettler
 *  http://mvpmc.sourceforge.net/
 *  
 *   Based on MediaMVP VNC Viewer
 *   http://www.rst38.org.uk/vdr/mediamvp
 *
 *   Copyright (C) 2005, Dominic Morris
 *
 *   $Id: main.c,v 1.1.1.1 2005/02/28 15:19:06 dom Exp $
 *   $Date: 2005/02/28 15:19:06 $
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * 
 *
 */


#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <pwd.h>
#include "zlib.h"


#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>


#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <mvp_osd.h>

#include "mvpmc.h"
#include "emulate.h"
#include "config.h"
#include "mvp_osd.h"

#include <vncviewer.h>
#include <nano-X.h>


#if 0
#define PRINTF(x...) printf(x)
#define TRC(fmt, args...) printf(fmt, ## args)
#define VERBOSE_DEBUG 1
#else
#define PRINTF(x...)
#define TRC(fmt, args...)
#endif


void query_host_parameters(void);
void client_play(char *filename);
void client_stop(void);
void client_pause(void);
void client_rewind(void);
void client_forward(void);

extern video_callback_t mvp_functions;
void UpdateFinished(void);
void SetDisplayState(int state);
Bool media_init(stream_t *stream, char *hostname, int port);
Bool media_send_request(stream_t *stream, char *uri);
Bool media_send_read(stream_t *stream);
Bool media_send_step(stream_t *stream, Bool forward);
Bool media_send_seek(stream_t *stream, int64_t offset);
Bool media_send_stop(stream_t *stream);
Bool media_send_eight(stream_t *stream);
void media_queue_data(stream_t *stream);

int platform_init(void);
int rfb_init(char *hostname, int port);
void mvp_fdinput_callback(mvp_widget_t *widget, int fd);
void vnc_timer_callback(mvp_widget_t *widget);
void mvp_server_cleanup(void);

int udp_listen(const char *iface, int port);
int udp_broadcast(char *data, int len, int port);

void ClearSurface(osd_surface_t *sfc);
Bool direct_init(stream_t *stream, char *hostname, int port);

#define rfbRdcAck 7
#define rfbRdcProgress 9

Bool RDCSendStop(int sock);
Bool RDCSendProgress(stream_t *stream);
Bool RDCSendRequestAck(int sock,int type);

#define  programName "mvpmc"

//static char    *c_config_file = NULL;

static char     *c_server_host = NULL;
static int      c_gui_port    = 5906;
static int      c_stream_port = 6337;
static int      c_direct_port = 0;
static char     c_query_host  = 0;
//static char     c_help        = 0;
static char    *c_interface   = "eth0";

static char    *c_addr        = NULL;

stream_t  mystream;

extern Bool shareDesktop;

extern char *passwdFile;
extern int updateRequestX;
extern int updateRequestY;
extern int updateRequestW;
extern int updateRequestH;
extern int useHauppageExtentions;

void PauseDisplayState(void);

static osd_surface_t    *surface = NULL;
static osd_surface_t    *surface_blank = NULL;
static osd_surface_t    *surface2 = NULL;

static int surface_y;
static int mvp_media = 0;

int output_pipe;
#define MVP_NAMED_PIPE "/tmp/FIFO"

int connect_to_servers(void);
Bool media_read_message(stream_t *stream);

int mvp_server_init(void)
{

    c_server_host=mvp_server;

    passwdFile = NULL;
    updateRequestX = 0;
    updateRequestY = 0;
    updateRequestW = 0;
    updateRequestH = 0;
    shareDesktop = False;
    if ( c_query_host == 0 && c_server_host == NULL ) {
        printf("Should either query for connection parameters or specify them\n");
        return -1;
    }
    surface_y = si.rows;

    if ( (surface = osd_create_surface(720,surface_y) )  == NULL ) {
        printf("Couldn't create surface\n");
        return -1;
    }
    if ( (surface2 = osd_create_surface(720,surface_y) )  == NULL ) {
        printf("Couldn't create surface\n");
        osd_destroy_surface(surface);
        surface = NULL;
        return -1;
    }
    if ( (surface_blank = osd_create_surface(720,surface_y) )  == NULL ) {
        printf("Couldn't create surface\n");
        osd_destroy_surface(surface2);
        surface2 = NULL;
        osd_destroy_surface(surface);
        surface = NULL;
        return -1;
    }

    osd_display_surface(surface);
    
    ClearSurface(surface_blank);

    PRINTF("Created surface %p\n",surface);

    if ( connect_to_servers() == -1 ) {
        mvp_server_cleanup();
        return -1;
    }
    return 0;
}

/* Connect to servers */
int connect_to_servers(void)
{
    int   i = 0;

    /* Close any previously established connections */

    if ( mystream.sock != -1 ) {
        close(mystream.sock);
        mystream.sock = -1;
    }

    if ( mystream.directsock != -1 ) {
        close(mystream.directsock);
        mystream.directsock = -1;
        //mvpav_setfd(-1);
    }
    
    query_host_parameters();
    if (c_addr==NULL) {
        return -1;
    }
    c_server_host = c_addr;  /* Leak first time if configured... */

    for (i=0;i<10;i++) {
        printf("RFB address %s:%d\n",c_server_host,c_gui_port);
        printf("Media address %s:%d\n",c_server_host,c_stream_port);
        PRINTF("Direct address %s:%d\n",c_server_host,c_direct_port);
        if ( rfb_init(c_server_host,c_gui_port)  == -1 ) {
            sleep(1);
            continue;
        }  
        if ( c_direct_port <= 0 ) {
            if ( media_init(&mystream,c_server_host,c_stream_port) == False ) {
                printf("False\n");
                sleep(1);
                continue;
            }           
        } else {
            if ( direct_init(&mystream,c_server_host,c_direct_port) == False ) {
                sleep(1);
                continue;
            }
        }
        break;
    }
    return 0;
}

/** \brief Poll for host parameters and keep going till then
 */
void query_host_parameters(void)
{
    u_int8_t   ether[6];
    u_int8_t   buf[52];
    u_int8_t  *ptr;
    u_int32_t  ipaddr;
    int        s;
    int        listen;
    struct     timeval tv;
    fd_set     fds;
    int attempts;

#ifdef linux
    struct ifreq s_ether;
    struct ifreq s_ipaddr;
    struct sockaddr_in *sin;


    PRINTF("Using interface %s\n",c_interface);
    /* Nasty bit of hacking for linux */
    s = socket(PF_INET, SOCK_DGRAM, 0);
    memset(&s_ether, 0x00, sizeof(s_ether));
    memset(&s_ipaddr, 0x00, sizeof(s_ipaddr));
    strcpy(s_ether.ifr_name, c_interface);
    strcpy(s_ipaddr.ifr_name, c_interface);
    ioctl(s, SIOCGIFHWADDR, &s_ether);
    ioctl(s, SIOCGIFADDR, &s_ipaddr);
    memcpy(ether,s_ether.ifr_hwaddr.sa_data,6);
    sin = (struct sockaddr_in *)&s_ipaddr.ifr_addr;
    memcpy(&ipaddr,&sin->sin_addr,4);
    ipaddr = ntohl(ipaddr);
    close(s);
#endif

    c_addr = NULL;

    listen = udp_listen(NULL,16882);

    if (listen==-1) {
        return;
    }
    attempts = 0;

    while ( attempts < 20 ) {

        memset(buf,0,sizeof(buf));
        ptr = buf;
        INT32_TO_BUF(1,ptr);
        INT16_TO_BUF(0xbabe,ptr);
        INT16_TO_BUF(0xfafe,ptr);
        memcpy(ptr,ether,6);
        ptr += 6;
        INT16_TO_BUF(0,ptr);
        INT32_TO_BUF(ipaddr,ptr);
        INT16_TO_BUF(16882,ptr);

        PRINTF("Querying host parameters\n");
        if ( udp_broadcast((char *)buf,52,16881) == -1 ) {
            perror("broadcast\n");
            exit(1);
        }

        FD_ZERO(&fds);
        FD_SET(listen,&fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        if (select(FD_SETSIZE,&fds,NULL,NULL,&tv) < 0 ) {
            perror("select");
            continue;
        }

        if ( FD_ISSET(listen,&fds) ) {
            if  ( ReadExact(listen,(char *)buf,52) ) {
                char        addr[32];

                u_int16_t   val16;
                u_int32_t   val32;

                ptr = buf + 4;
                BUF_TO_INT16(val16,ptr);
                if ( val16 != 0xfafe ) {
                    continue;
                }
                BUF_TO_INT16(val16,ptr);
                if ( val16 != 0xbabe ) {
                    continue;
                }
                ptr = buf + 24;
                BUF_TO_INT32(val32,ptr);

                BUF_TO_INT16(c_gui_port,ptr);
                ptr += 6;
                BUF_TO_INT16(c_stream_port,ptr);
                ptr += 4;
                BUF_TO_INT16(c_direct_port,ptr);

                snprintf(addr,sizeof(addr),"%d.%d.%d.%d",
                         ( val32 >> 24 ) & 0xFF,
                         ( val32 >> 16 ) & 0xFF,
                         ( val32 >> 8 ) & 0xFF,
                         val32 & 0xFF);
                
                FREENULL(c_addr);
                c_addr = STRDUP(addr);
                if (strcmp(mvp_server,"?")) {
                    if (strcmp(mvp_server,c_addr)==0) {
                       return;
                    } else {
                        printf("IP did not match %s\n",c_addr);
                        FREENULL(c_addr);
                        attempts++;
                    }
                } else {
                    return;
                }
            }
        } else {
            attempts++;
        }
    }
}

pthread_t mvp_server_thread;
void *mvp_server_start(void *arg);

int mvp_server_register(void)
{
    pthread_create(&mvp_server_thread, &thread_attr_small,mvp_server_start, NULL);
    printf("output thread started\n");
    return 1;
}


void *mvp_server_start(void *arg)
{
    fd_set      fds;
    fd_set      wfds;
    struct timeval tv;

    printf("Starting mvp media writer\n");

    /* Create a pipe for funnelling through to the appropriate thread */
    pipe(&mystream.socks[0]);

    /* Set the writer to be non-blocking */
    fcntl(mystream.socks[1],F_SETFL,O_NONBLOCK);

    unlink(MVP_NAMED_PIPE);
    mkfifo(MVP_NAMED_PIPE, S_IRWXU);
    mvp_media = 1;
    
    while ( mvp_media == 1 ) {
        if ( mystream.sock <= 0)  {
            usleep(10000);
            continue;
        }
        tv.tv_sec = 1;
        tv.tv_usec = 100;

        FD_ZERO(&fds);
        FD_ZERO(&wfds);

        if ( mystream.inbuflen ) {
            FD_SET(mystream.socks[1],&wfds);
        }

        FD_SET(mystream.sock,&fds);

        if (select(FD_SETSIZE, &fds, NULL, NULL, &tv) < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(mystream.socks[1], &wfds) ) {
            media_queue_data(&mystream);
        }

        if (FD_ISSET(mystream.sock, &fds)) {
            if (!media_read_message(&mystream)) {
                break;
            }
        }
    }
    if ( mystream.sock != -1 ) {
        close(mystream.sock);
        mystream.sock = -1;
    } else if ( mystream.directsock != -1 ) {
        close(mystream.directsock);
        mystream.directsock = -1;
        //mvpav_setfd(-1);
    }
    close(mystream.socks[0]);
    close(mystream.socks[1]);
    unlink(MVP_NAMED_PIPE);
    printf("Stopped mvp media writer\n");
    return NULL;
}

void mvp_server_stop(void) 
{
    mvp_media = 0;
}

void mvp_server_cleanup(void) 
{
    osd_destroy_surface(surface2);
    surface2 = NULL;
    osd_destroy_surface(surface_blank);
    surface_blank = NULL;
    osd_destroy_surface(surface);
    surface = NULL;
}

void mvp_server_remote_key(char key)
{
    static int hauppageKey[] = { 
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,    
        0x09, 0x0a, 0x36, 0x24, 0x28, 0x1e, 0x37, 0x15,
        0x11, 0x10, 0x35, 0x00, 0x33, 0x34, 0x32, 0x31,
        0x2d, 0x2e, 0x2f, 0x00, 0x2c, 0x30, 0x2b, 0x20,
        0x12, 0x13, 0x00, 0x00, 0x2a, 0x0d, 0x00, 0x00,
        0x00, 0x27, 0x00, 0x00, 0x00, 0x00, 0x25, 0x00,
        0x1c, 0x00, 0x0e, 0x00, 0x0f, 0x19, 0x1b, 0x1a,
        0x26, 0x00, 0x00, 0x23, 0x29, 0x14
    };
    if (mystream.mediatype==TYPE_VIDEO) {
        switch (key) {
            case MVPW_KEY_STOP:
                client_stop();
                break;
            case MVPW_KEY_PLAY:
                if (paused) {
                    client_pause();
                }
                break;
            case MVPW_KEY_PAUSE:
                client_pause();
                break;
            default:
                break;
        }
    } else {
        int key1;
        if (key > 0x61) {
            key1 = key;
        } else {
            key1 = hauppageKey[(int)key];
            if (key1==0) {
                key1 = key;
            }
        }
        PRINTF("keymap %i = %x\n", key, key1);
        SendKeyEvent(key1, -1);
		SendIncrementalFramebufferUpdateRequest();
    }
    return;
}


int udp_listen(const char *iface, int port)
{
    int                 sock;
    struct sockaddr_in  serv_addr;
    int                 trueval = 1;


    memset((char *)&serv_addr, 0, sizeof(serv_addr));

    if (iface == NULL) {
        serv_addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_aton(iface, &serv_addr.sin_addr) == 0) {
        return -1;
    }


    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock == -1) {
        return -1;
    }


    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&trueval, sizeof(trueval) ) == -1 ) {
        close(sock);
        return -1;
    }


    serv_addr.sin_port = htons((u_short)port);
    serv_addr.sin_family = AF_INET;

    if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1 ) {
        close(sock);
        return -1;
    }

    return sock;

}


int udp_broadcast(char *data, int len, int port)
{
    int                 n;
    int                 sock;
    struct sockaddr_in  serv_addr;
    int                 on = 1;

    memset((char *)&serv_addr, 0, sizeof(serv_addr));


    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock == - 1) {
        return -1;
    }

    if(setsockopt (sock, SOL_SOCKET, SO_BROADCAST, (char *)&on, sizeof(on))< 0) {
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((u_short)port);
    serv_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    n = sendto(sock, data, len, 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    close(sock);

    return n;
}




/*
 * sockets.c - functions to deal with sockets.
 */


/* fix bad MIPS sys headers...*/
#ifndef SOCK_STREAM
#define SOCK_STREAM	2	/* <asm/socket.h>*/
#endif

void PrintInHex(char *buf, int len);

Bool errorMessageFromReadExact = True;

/*
 * Read an exact number of bytes, and don't return until you've got them.
 */

Bool
ReadExact(int sock, char *buf, int n)
{
    int i = 0;
    int j;

    while (i < n) {
        j = read(sock, buf + i, (n - i));
        if (j <= 0) {
            if (j < 0) {
                fprintf(stderr,programName);
                perror(": read");
            } else {
                if (errorMessageFromReadExact) {
                    fprintf(stderr,"%s: read failed\n",programName);
                }
            }
            return False;
        }
        i += j;
    }
#ifdef VERBOSE_DEBUG
    if (n < 100) {
        PrintInHex(buf,n);
    }
#endif
    return True;
}


/*
 * Write an exact number of bytes, and don't return until you've sent them.
 */

Bool
WriteExact(int sock, char *buf, int n)
{
    int i = 0;
    int j;

    while (i < n) {
	j = write(sock, buf + i, (n - i));
	if (j <= 0) {
	    if (j < 0) {
		fprintf(stderr,programName);
		perror(": write");
	    } else {
		fprintf(stderr,"%s: write failed\n",programName);
	    }
	    return False;
	}
	i += j;
    }
    return True;
}


/*
 * ConnectToTcpAddr connects to the given TCP port.
 */

int
ConnectToTcpAddr(unsigned int host, int port)
{
    int sock;
    struct sockaddr_in addr;
    int one = 1;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = host;


    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
	    fprintf(stderr,programName);
	    perror(": ConnectToTcpAddr: socket");
	    return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	    fprintf(stderr,programName);
	    perror(": ConnectToTcpAddr: connect");
	    close(sock);
	    return -1;
    }

    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,(char *)&one, sizeof(one)) < 0) {
	    fprintf(stderr,programName);
	    perror(": ConnectToTcpAddr: setsockopt");
	    close(sock);
	    return -1;
    }
    return sock;
}



/*
 * ListenAtTcpPort starts listening at the given TCP port.
 */

int
ListenAtTcpPort(int port)
{
    int sock;
    struct sockaddr_in addr;
    int one = 1;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
	fprintf(stderr,programName);
	perror(": ListenAtTcpPort: socket");
	return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
		   (const char *)&one, sizeof(one)) < 0) {
	fprintf(stderr,programName);
	perror(": ListenAtTcpPort: setsockopt");
	close(sock);
	return -1;
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	fprintf(stderr,programName);
	perror(": ListenAtTcpPort: bind");
	close(sock);
	return -1;
    }

    if (listen(sock, 5) < 0) {
	fprintf(stderr,programName);
	perror(": ListenAtTcpPort: listen");
	close(sock);
	return -1;
    }

    return sock;
}


/*
 * AcceptTcpConnection accepts a TCP connection.
 */

int
AcceptTcpConnection(int listenSock)
{
    int sock;
    struct sockaddr_in addr;
    unsigned int addrlen = sizeof(addr);
    int one = 1;

    sock = accept(listenSock, (struct sockaddr *) &addr, &addrlen);
    if (sock < 0) {
	fprintf(stderr,programName);
	perror(": AcceptTcpConnection: accept");
	return -1;
    }

    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
		   (char *)&one, sizeof(one)) < 0) {
	fprintf(stderr,programName);
	perror(": AcceptTcpConnection: setsockopt");
	close(sock);
	return -1;
    }

    return sock;
}


/*
 * StringToIPAddr - convert a host string to an IP address.
 */

int
StringToIPAddr(const char *str, unsigned int *addr)
{
    struct hostent *hp;

    if ((*addr = inet_addr(str)) == -1)
    {
	if (!(hp = gethostbyname(str)))
	    return 0;

	*addr = *(unsigned int *)hp->h_addr;
    }

    return 1;
}


/*
 * Test if the other end of a socket is on the same machine.
 */

Bool
SameMachine(int sock)
{
    struct sockaddr_in peeraddr, myaddr;
    unsigned int addrlen = sizeof(struct sockaddr_in);

    getpeername(sock, (struct sockaddr *)&peeraddr, &addrlen);
    getsockname(sock, (struct sockaddr *)&myaddr, &addrlen);

    return (peeraddr.sin_addr.s_addr == myaddr.sin_addr.s_addr);
}


/*
 * Print out the contents of a packet for debugging.
 */

void
PrintInHex(char *buf, int len)
{
    int i, j;
    char c, str[17];

    str[16] = 0;

    fprintf(stderr,"ReadExact: ");

    for (i = 0; i < len; i++)
    {
	if ((i % 16 == 0) && (i != 0)) {
	    fprintf(stderr,"           ");
	}
	c = buf[i];
	str[i % 16] = (((c > 31) && (c < 127)) ? c : '.');
	fprintf(stderr,"%02x ",(unsigned char)c);
	if ((i % 4) == 3)
	    fprintf(stderr," ");
	if ((i % 16) == 15)
	{
	    fprintf(stderr,"%s\n",str);
	}
    }
    if ((i % 16) != 0)
    {
	for (j = i % 16; j < 16; j++)
	{
	    fprintf(stderr,"   ");
	    if ((j % 4) == 3) fprintf(stderr," ");
	}
	str[i % 16] = 0;
	fprintf(stderr,"%s\n",str);
    }

    fflush(stderr);
}

/*
 * vncviewer.c - viewer for MVP - based on nano-X version
 *
 * The rfb_viewer() is called from a separate thread
 */


#ifdef NANOX
static void HandleEvents(GR_EVENT *ev);
#endif

/** \brief Initialise a connection to the vnc server
 *
 *  \param hostname Hostname
 *  \param port Pot 
 *
 *  \retval -1 - Connection failed
 *  \retval other - socket
 */
int rfb_init(char *hostname, int port)
{
    
    if ( !ConnectToRFBServer(hostname, port)) {
        return -1;
    } 

    PRINTF("Socket %d\n",rfbsock);
    updateRequestH = si.rows;
    if (!InitialiseRFBConnection(rfbsock)) {
        close(rfbsock);
        return -1;
    }
    updateRequestH = si.rows;
    useHauppageExtentions = 1;
    if (!SetFormatAndEncodings()) {
        close(rfbsock);
        return -1;
    }
    if (!SendFramebufferUpdateRequest(updateRequestX, updateRequestY,
                                      updateRequestW, updateRequestH, False)) {
        close(rfbsock);
        return -1;
    }
    PRINTF("End rfb_init\n");
    
    return rfbsock;
}

#if 0
    while (True) {
        if (!HandleKeyBoard() ) {
            ShutdownDisplay();
            return;
        }
        tvp = NULL;

        if (sendUpdateRequest) {
            gettimeofday(&tv, NULL);

            msWait = (updateRequestPeriodms +
                      ((updateRequestTime.tv_sec - tv.tv_sec) * 1000) +
                      ((updateRequestTime.tv_usec - tv.tv_usec) / 1000));

            if (msWait > 0) {
                tv.tv_sec = msWait / 1000;
                tv.tv_usec = (msWait % 1000) * 1000;

                tvp = &tv;
            } else {
                if (!SendIncrementalFramebufferUpdateRequest()) {
                    ShutdownDisplay();
                    return;
                }
            }
        }

        FD_ZERO(&fds);
        FD_SET(rfbsock,&fds);

        if (select(FD_SETSIZE, &fds, NULL, NULL, tvp) < 0) {
            perror("select");
            ShutdownDisplay();
            exit(1);
        }

        if (FD_ISSET(rfbsock, &fds)) {
            if (!HandleRFBServerMessage()) {
                return;
            }
        }
    }

    return;
}
#endif


/*
 *
 *   Handle the Hauppauge extensions to the RFB protocol
 */



Bool RDCSendStop(int sock)
{
    char    buf[34];

    memset(buf,0,sizeof(buf));

    buf[0] = rfbRdcAck;
    buf[1] = RDC_STOP;

    if (!WriteExact(sock,buf,sizeof(buf)) ) {
        return False;
    }
    return True;
}

Bool RDCSendRequestAck(int sock,int type)
{
    char    buf[34];

    memset(buf,0,sizeof(buf));

    buf[0] = rfbRdcAck;
    buf[1] = type;
    buf[2]=1;

    if (!WriteExact(sock,buf,sizeof(buf)) ) {
        return False;
    }
    return True;
}

Bool RDCSendProgress(stream_t *stream)
{
    char    buf[10];
    char *ptr;

    memset(buf,0,sizeof(buf));

    buf[0] = rfbRdcProgress;
    ptr = buf + 4;
    INT32_TO_BUF(stream->current_position,ptr);

    if (!WriteExact(rfbsock,buf,sizeof(buf)) ) {
        return False;
    }
    return True;
}


Bool HandleRDCMessage(int sock)
{
    char   buf[64];


    if ( !ReadExact(sock,buf + 1,33) ) {
        return False;
    }

    buf[0] = rfbRdcAck;

    PRINTF("Received RDC command %d\n",buf[1]);

    switch ( buf[1] ) {
    case RDC_PLAY:
    {
		PRINTF("RDC_PLAY\n");
        int   length = buf[8];   
        char *filename;

        filename = malloc(length + 1);

        if (!ReadExact(sock,filename,length) ) {
            return False;
        }
        filename[length] = 0;
        if (length==0 && current!=NULL) {
            free(filename);
            filename = strdup(current);
        }
        PRINTF("We need to play %s %d\n",filename,length);
        client_play(filename);
        free(filename);
        return True; /* We don't want to send an ack at this stage */
        break;
    }
    case RDC_PAUSE:
    {
		PRINTF("RDC_PAUSE\n");
        client_pause();
        break;
    }

    case RDC_STOP:
    {
		PRINTF("RDC_STOP\n");
        client_stop();
        return True;
        break;
    }
    case RDC_REWIND:
    {
		PRINTF("RDC_REWIND\n");
        client_rewind();
        break;
    }
    case RDC_FORWARD:
    {
		PRINTF("RDC_FORWARD\n");
        client_forward();
        break;
    }
    case RDC_VOLUP:
    {
		PRINTF("RDC_VOLUP\n");
        break;
    }
    case RDC_VOLDOWN:
    {
		PRINTF("RDC_VOLDOWN\n");
        break;
    }
    case RDC_MENU:
    {
		PRINTF("RDC_MENU\n");
        char display[2];
        if ( !ReadExact(sock,display,2) ) {
            return False;
        }
        PRINTF("Display is state %d\n",!display[1]);
        SetDisplayState(display[1]);
        RDCSendRequestAck(rfbsock,RDC_MENU);
        return True;
    }
    case RDC_MUTE:
    {
		PRINTF("RDC_MUTE\n");
        break;
    }
    case RDC_SETTINGS:
    {
        char   *settings;
		PRINTF("RDC_SETTINGS ");
        PRINTF("Buf length %d\n",buf[7]);
        settings=malloc(buf[7]);
        memset(settings,0,buf[7]);
        if ( !ReadExact(sock,settings,buf[7]) ) {
            free(settings);
            return False;
        }
        PRINTF("Settings command %d\n",settings[0]);
        if ( settings[0] == RDC_SETTINGS_GET ) {
//            RDCSendRequestAck(rfbsock,RDC_SETTINGS);
            buf[2]=1;
            if ( !WriteExact(sock,buf,34) ) {
                free(settings);
                return False;
            }
            if (si.rows==480) {
                settings[1] = 0;
            } else {
                settings[1] = 1;
            }
            /*
            settings[2] = 3 ;  config->av_video_output 
            settings[3] = 1 ;  flicker
            settings[4] = config->av_aspect & 1 ;  
            */

            if ( !WriteExact(sock,settings,buf[7]) ) {
                free(settings);
                return False;
            }
        }
        free(settings);
        return True;
        break;
    }
	default:
	{
    	PRINTF("RDC_UNHANDLED %d\n",buf[1]);
		break;
	}

    }

    if ( !WriteExact(sock,buf,34) ) {
        return False;
    }

    return True;
}


/*
client.c
 */


void client_play(char *filename)
{
    media_send_eight(&mystream);
    current = strdup(filename);
}

void client_stop(void)
{
    media_send_stop(&mystream);
    free(current);
    current=NULL;
}

void client_pause(void)
{
    /* native pause for now */
   	if (av_pause()) {
        paused = 1;
    } else {
        paused = 0;
    }
}

void client_rewind(void)
{

}

void client_forward(void)
{


}


/*
 *   stream.c
 *   Implementation of Hauppauge Streaming protocol
 */

// static pthread_t     media_thread = 0;
// static void         *media_read_start(void *);


Bool media_init(stream_t *stream, char *hostname, int port)
{
    unsigned int host;

    PRINTF("%s %d\n",hostname,port);

    if (!StringToIPAddr(hostname, &host)) {
        fprintf(stderr,"%s: couldn't convert '%s' to host address\n",
                programName,hostname);
        return -1;
    }

    stream->sock = ConnectToTcpAddr(host, port);


    return stream->sock != -1 ? True : False;
}


Bool media_send_request(stream_t *stream, char *uri)
{
    u_int8_t *buf;
    u_int16_t len = strlen(uri);
    u_int8_t *ptr;


    PRINTF("Requesting %s\n",uri);
    buf = CALLOC(40 + len + 1, sizeof(char));

    buf[0] = MEDIA_REQUEST;

    ptr = buf + 36;
    INT16_TO_BUF(len + 1,ptr);
    strcpy((char *)buf+40,uri);

    if (!WriteExact(stream->sock,(char *)buf,40) ) {
        FREE(buf);
        return False;
    }
    if (!WriteExact(stream->sock,uri,len + 1) ) {
        FREE(buf);
        return False;
    }
    FREE(buf);

    stream->last_command = MEDIA_REQUEST;

    return True;
}

Bool media_send_read(stream_t *stream)
{
    u_int8_t    buf[40];
    u_int8_t   *ptr;


    memset(buf,0,sizeof(buf));
    memcpy(buf+6,stream->fileid,2);

    
    buf[0] = MEDIA_BLOCK;

    ptr = buf + 8;
    INT32_TO_BUF(stream->blocklen,ptr);
    
    if (!WriteExact(stream->sock,buf,40) ) {
        return False;
    }

    stream->last_command = MEDIA_BLOCK;

    return True;
}



Bool media_send_step(stream_t *stream, Bool forward)
{
    u_int8_t    buf[40];

    memset(buf,0,sizeof(buf));
    memcpy(buf+6,stream->fileid,2);

    buf[0] = MEDIA_STEP;
    buf[9] = forward ? 1 : 0;

    if (!WriteExact(stream->sock,buf,40) ) {
        return False;
    }

    stream->last_command = MEDIA_STEP;

    return True;
}


Bool media_send_seek(stream_t *stream, int64_t offset)
{
    u_int8_t   buf[40];
    u_int8_t  *ptr;

    memset(buf,0,sizeof(buf));
    buf[0] = MEDIA_SEEK;
    memcpy(buf+6,stream->fileid,2);


    ptr = buf + 8;
    INT32_TO_BUF(offset,ptr);

    if (!WriteExact(stream->sock,buf,40) ) {
        return False;
    }

    stream->last_command = MEDIA_SEEK;

    return True;
}

Bool media_send_eight(stream_t *stream)
{
    u_int8_t   buf[40];

    memset(buf,0,sizeof(buf));
    buf[0] = MEDIA_8;

    if (!WriteExact(stream->sock,buf,40) ) {
        return False;
    }

    stream->last_command = MEDIA_8;

    return True;
}


Bool media_send_stop(stream_t *stream)
{
    u_int8_t  buf[40];

    memset(buf,0,sizeof(buf));
    buf[0] = MEDIA_STOP;
    memcpy(buf+6,stream->fileid,2);


    if (!WriteExact(stream->sock,buf,40) ) {
        return False;
    }

    stream->last_command = MEDIA_STOP;

    return True;
}


Bool media_read_message(stream_t *stream)
{
    u_int8_t   buf[40];
    u_int8_t  *ptr;

    if (!ReadExact(stream->sock,buf,40) ) {
        return False;
    }

    switch ( buf[0] ) {
    case MEDIA_REQUEST:
    {
		PRINTF("Media Request ");
        if ( buf[4] == 0 ) {
            PRINTF("Aborted reading\n");
        } else {

            /* We've opened the file, send an ack for it now */
            RDCSendRequestAck(rfbsock,RDC_PLAY);

            stream->mediatype = buf[4];
            if ( buf[4] == TYPE_VIDEO ) {
				PRINTF("Video\n");
                stream->blocklen = 200000;
            } else {
				PRINTF("Audio\n");
                stream->blocklen = 20000;
            }
            ptr = buf + 16;
            BUF_TO_INT32(stream->length,ptr);
            memcpy(stream->fileid,buf+34,2);
            switch ( buf[4] ) {
            case 0x01:           /* MPEG */
                // seem to lose this anyway
                video_functions = &mvp_functions;
                video_clear();
                mvpw_set_timer(vnc_widget, NULL, 100); 
                GrUnregisterInput(rfbsock);
		        mvpw_set_fdinput(vnc_widget, NULL);
                video_play(NULL);
    			mvpw_show(root);
    			mvpw_expose(root);
    			mvpw_focus(root);
                output_pipe = open(MVP_NAMED_PIPE, O_WRONLY);
//                output_pipe = open("/music/mympeg.mpg",  O_CREAT|O_TRUNC|O_WRONLY);
                break;
            case 0x02: /* MP3 */
                av_init();
                audio_play(NULL);
                output_pipe = open(MVP_NAMED_PIPE, O_WRONLY);
                break; 
            }
           
            media_send_seek(stream,0);
        }
        break;
    }
    case MEDIA_BLOCK:
    {
        int32_t    blocklen;
//        u_int8_t *block;

        ptr = buf + 8;

        BUF_TO_INT32(blocklen,ptr);
		
        PRINTF("Media Block %d",blocklen);

        if ( blocklen != 0 ) {
            ptr = buf + 12;
            BUF_TO_INT32(stream->current_position,ptr);

            PRINTF(" Current %llx\n",stream->current_position);

//            int  n = 0;
            stream->inbuf = MALLOC(blocklen);

            if (!ReadExact(stream->sock,stream->inbuf,blocklen) ) {
                return False;
            }
            // stream->current_position 
            stream->inbuflen = blocklen;
            stream->inbufpos = 0;
            media_queue_data(&mystream);

        } else {
            PRINTF(" %lld %lld\n",stream->current_position,stream->length);
            if ( stream->current_position < stream->length ){
                ptr = buf + 12;
                BUF_TO_INT32(stream->inbuflen,ptr);
                stream->inbuflen -= stream->current_position;
                PRINTF("Reading remaining %d\n",stream->inbuflen);
                stream->current_position += stream->inbuflen;
                stream->length=stream->current_position;
                stream->inbuf = MALLOC(stream->inbuflen);
                ptr = buf + 8;
                INT32_TO_BUF(stream->inbuflen,ptr);

                if (!ReadExact(stream->sock,stream->inbuf,stream->inbuflen) ) {
                    return False;
                }
//                PRINTF(" %lld %lld\n",stream->current_position,stream->length);
//                PRINTF(" Remaining %lld\n",stream->length-stream->current_position);
                // how to write the rest
                stream->inbufpos = 0;
                media_queue_data(&mystream);
            } else {
                PRINTF("\n");
            }
            if (mystream.mediatype==TYPE_VIDEO && surface==NULL) {
                while ( av_empty()==0 ) {
                    // empty audio buffer 
                    usleep(10000);
                }
//                client_stop();
                video_clear();
                if ( (surface = osd_create_surface(720,surface_y) )  == NULL ) {
                    PRINTF("Couldn't create surface\n");
                }
		        mvpw_set_timer(vnc_widget, vnc_timer_callback, 1000); 
                GrRegisterInput(rfbsock);
		        mvpw_set_fdinput(vnc_widget, mvp_fdinput_callback);
    			mvpw_show(vnc_widget);
    			mvpw_expose(vnc_widget);
    			mvpw_focus(vnc_widget);
                osd_display_surface(surface);
                UpdateFinished();
                mystream.mediatype= 0;
//		        SendKeyEvent(0x20, -1);
            }
            RDCSendStop(rfbsock);
        }
        break;
    }

    case MEDIA_STEP:
    case MEDIA_SEEK:
    {
        PRINTF("Media Step/seek\n");
        ptr = buf + 12;
        stream->current_position = 0;
//        INT32_TO_BUF(stream->current_position,ptr);
        media_send_read(stream);
        break;
    }
    case MEDIA_STOP:
    {
        PRINTF("Media Stop\n");
        RDCSendRequestAck(rfbsock,RDC_STOP);
        if (mystream.mediatype==TYPE_VIDEO && surface==NULL) {
            video_clear();
		    av_move(0, 0, 0);
	        mvpw_set_bg(root, root_color);
            if ( (surface = osd_create_surface(720,surface_y) )  == NULL ) {
                printf("Couldn't create surface\n");
            }
            mvpw_set_timer(vnc_widget, vnc_timer_callback, 1000); 
            GrRegisterInput(rfbsock);
            mvpw_set_fdinput(vnc_widget, mvp_fdinput_callback);
            mvpw_show(vnc_widget);
            mvpw_expose(vnc_widget);
            mvpw_focus(vnc_widget);
            osd_display_surface(surface);
            UpdateFinished();            
            mystream.mediatype= 0;
//            RDCSendStop(rfbsock);
        } else if (mystream.mediatype==TYPE_AUDIO ) {
            audio_stop = 1;
			av_reset();
        }
        close(output_pipe);        
        break;
    }
	case MEDIA_8:
	{
		PRINTF("Media 8\n");
        media_send_request(&mystream,current);
		break;
	}
	default:
	{
        PRINTF("Media Unhandled %d\n",buf[0]);
		break;
    }
    }
    return True;
}

void media_queue_data(stream_t *stream)
{
    int      n;
    if (mystream.mediatype==TYPE_VIDEO && surface != NULL) {
        osd_destroy_surface(surface);
        surface = NULL;
    }
    n = write(output_pipe,stream->inbuf + stream->inbufpos, stream->inbuflen - stream->inbufpos);

    if ( n > 0 ) {
        stream->inbufpos += n;
        if ( stream->inbufpos == stream->inbuflen) {
            RDCSendProgress(stream);
            media_send_read(stream);
            stream->inbufpos = 0;
            stream->inbuflen = 0;
            FREENULL(stream->inbuf);

        }
    }

    return ;
}


/*
*   mvp.c
*   Native MVP viewer
*/


static int               visible = 1;



void RectangleUpdateYUV(int x0, int y0, int w, int h,  unsigned char *buf)
{
    int   x,y;
    unsigned char   y1,y2,u,v;
    unsigned char *ptr = buf;

    // printf("YUV update %d %d %d %d\n",x0,y0,w,h);

    for ( y = y0; y < y0 + h; y++ ) {
        for ( x = x0; x < x0 +w ; x+= 2 ) {           
            y1 = *ptr++;
            u = *ptr++;
            v = *ptr++;
            y2 = *ptr++;
            osd_draw_pixel_ayuv(surface2,x,y,255,y1,u,v);
            osd_draw_pixel_ayuv(surface2,x+1,y,255,y2,u,v);
        }
    }
}

void RectangleUpdateYUV2(int x0, int y0, int w, int h,  unsigned char *buf1, unsigned char *buf2)
{
    int   x,y;
    unsigned char   y1,y2,u,v;
    unsigned char *ptr1 = buf1;
    unsigned char *ptr2 = buf2;

    // printf("YUV2 update %d %d %d %d\n",x0,y0,w,h);

    for ( y = y0; y < y0 + h; y++ ) {
        for ( x = x0; x < x0 +w ; x+= 2 ) {           
            y1 = *ptr1++;
            y2 = *ptr1++;
            u = *ptr2++;
            v = *ptr2++;
            osd_draw_pixel_ayuv(surface2,x,y,255,y1,u,v);
            osd_draw_pixel_ayuv(surface2,x+1,y,255,y2,u,v);
        }
    }
}


void RectangleUpdateAYVU(int x0, int y0, int w, int h,  unsigned char *buf)
{
    int   x,y;
    unsigned char   a1,a2,y1,y2,u,v;
    unsigned char *ptr = buf;

    // printf("AYUV update %d %d %d %d\n",x0,y0,w,h);

    for ( y = y0; y < y0 + h; y++ ) {
        for ( x = x0; x < x0 +w ; x+= 2 ) {
            a1 = *ptr++;
            a2 = *ptr++;
            y1 = *ptr++;
            u = *ptr++;
            v = *ptr++;
            y2 = *ptr++;
            osd_draw_pixel_ayuv(surface2,x,y,255-a1,y1,u,v);
            osd_draw_pixel_ayuv(surface2,x+1,y,255-a2,y2,u,v);
        }
    }
}

void RectangleUpdateARGB(int x0, int y0, int w, int h,  unsigned char *buf)
{
    int   x,y;
    unsigned char   a,r,g,b;
    unsigned char *ptr = buf;

    // printf("ARGB update %d %d %d %d\n",x0,y0,w,h);

    for ( y = y0; y < y0 + h; y++ ) {
#if 1
        for ( x = x0; x < x0 +w ; x++ ) {
            a = *ptr++;
            r = *ptr++;
            g = *ptr++;
            b = *ptr++;   
            a = 0xff - a;
            osd_draw_pixel(surface2,x,y,osd_rgba(r,g,b,a));
        }
#endif 
    }
}

void UpdateFinished(void)
{
    if (surface==NULL) {
        printf("Update Finished\n");
        return;
    }
    if ( visible ) {
        osd_blit(surface,0,0,surface2,0,0,720,surface_y);
    } else {
        osd_blit(surface,0,0,surface_blank,0,0,720,surface_y);
    }
}

/** \brief Set the display to be visible/not
 */
void SetDisplayState(int state)
{
    if (state==1) {
    } else {
		video_functions = &mvp_functions;
        video_clear();
    }
    /*
    if ( visible ) {
        osd_blit(surface,0,0,surface2,0,0,720,surface_y);
    } else {
        osd_blit(surface,0,0,surface_blank,0,0,720,surface_y);
    }
    */
}

void ClearSurface(osd_surface_t *sfc)
{
    int      x,y;

    for ( x = 0; x < 720; x++ ) {
        for ( y = 0; y < surface_y; y++ ) {
            osd_draw_pixel(sfc,x,y,0x00000000);
        }
    }
}


/*
 *   $Id: direct.c,v 1.1.1.1 2005/02/28 15:19:06 dom Exp $
 *   Implementation of a direct socket connection
 */

Bool direct_init(stream_t *stream, char *hostname, int port)
{
    unsigned int host;

    if (!StringToIPAddr(hostname, &host)) {
        fprintf(stderr,"%s: couldn't convert '%s' to host address\n",
                programName,hostname);
        return False;
    }

    stream->directsock = ConnectToTcpAddr(host, port);

    //mvpav_setfd(stream->directsock);

    return stream->directsock != -1 ? True : False;
}


void PauseDisplayState(void)
{
    osd_blit(surface2,0,0,surface,0,0,720,surface_y);
}

long long mvp_file_size(void)
{
    PRINTF("Video size %lld\n",mystream.current_position);
    return mystream.current_position;
}


int mvp_file_read(char *buf, int len)
{
    int data, tslen = 0;
    tslen = len;
    do {
        data = read(fd, buf+(len-tslen), tslen);
        if (data <= 0) {
            if (tslen==len) {
                return data;
            }
            break;
        } else {
            tslen-=data;
        }
    } while (tslen > 0);
    return len-tslen;
}