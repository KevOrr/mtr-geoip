/*
    mtr  --  a network diagnostic tool
    Copyright (C) 1997,1998  Matt Kimball

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as 
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

int geoip_enabled = 1;      // This can be disabled (0) with --no-geolocation or -N
int geoip_is_ui_shown = 0;  // Whether it is currently shown in UI (curses)
int geoip_was_init = 0;

/*
    GeoIP location for mtr --
    Copyright (C) 2015 by Alexandre van 't Westende <emugel@qq.com>
    Released under GPL, as above.
*/

const char*     geolocation_server_host = "freegeoip.net";
const int       geolocation_server_port = 80;
const char*     geolocation_server_useragent = "mtr+geoip";

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include "geoip.h"
extern int errno;

// [Debugging]
// Set GEOIP_DEBUG to 1 and recompile to enable logs to syslog 
// (if 0, messages to syslog are disabled).
//
// In your /etc/syslog.conf, you may want to add a line 
//      user.*                  -/var/log/ALLUSERMESSAGES
// Then send SIGHUP to syslogd so as to reload (ie: pkill -1 syslogd)
// (this is so all messages get stored)
#define GEOIP_DEBUG 0 

#ifdef GEOIP_DEBUG
    #include <syslog.h>
    #define SYSLOG_DEBUG(fmt, args...) syslog(LOG_DEBUG, fmt, ##args)
    #define SYSLOG_INFO(fmt, args...) syslog(LOG_INFO, fmt, ##args)
    #define SYSLOG_WARN(fmt, args...) syslog(LOG_WARNING, fmt, ##args)
    #define SYSLOG_ERR(fmt, args...) syslog(LOG_ERR, fmt, ##args)
#else
    #define SYSLOG_INFO(fmt, args...)
    #define SYSLOG_WARN(fmt, args...)
    #define SYSLOG_ERR(fmt, args...)
#endif

#define geomax(a,b) \
  ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define geomin(a,b) \
  ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b; })                           

struct geo_location* geo_tail = NULL;       // tail of the chained list
struct geo_location* geo_lastLocationRequested = NULL;
int geoip_responses_awaited = 0;    // request sent: ++, response received: --
struct sockaddr_in remote;
int geoip_fd;                      // fd for persistent connection to "freegeoip.net"
char buf[BUFSIZ+1];

/**
 * Toggle display in curses.
 * Will also initialize if called the first time
 */
void geoip_toggle_display() {
    if (!geoip_was_init) {
        geoip_was_init = 1;
        if (geoip_open() != 0) geoip_enabled = 0;
    }
    geoip_is_ui_shown = geoip_is_ui_shown == 0 ? 1 : 0;
}

void geoip_open_syslog() {
    if (geoip_enabled) {
        #ifdef GEOIP_DEBUG
        openlog("mtr_geoip", 0, 0);
        SYSLOG_INFO("Opening logs for mtr::geoip");
        #endif
    }
}

/**
 * Establish a persistent HTTP connection to geolocation server.
 *
 * Return 0 if success, 1 upon failure. geoip_enabled will be set to 
 *  0 in the later case.
 */
int geoip_open() {
    const char* host = geolocation_server_host;
    const int   port = geolocation_server_port;
    int*        fd   = &geoip_fd;

    if (geoip_enabled) {
        struct hostent* hent;
        if ((hent = gethostbyname(host)) == NULL) {
            SYSLOG_ERR("gethostbyname(%s) error", host);
            geoip_enabled = 0;
            return 1;
        }
        else SYSLOG_INFO("gethostbyname(%s) is OK...\n", host);

        char tmpip[16];
        if (inet_ntop(AF_INET, (void*) hent->h_addr_list[0], tmpip, 16) == NULL) {
            SYSLOG_ERR("Can't resolve host %s", host); 
            geoip_enabled = 0;
            return 1;
        } else SYSLOG_INFO("IP for %s: %s\n", host, tmpip);

        remote.sin_family = AF_INET;
        remote.sin_port = htons(port);
        remote.sin_addr = *((struct in_addr*)hent->h_addr);
        memset(&(remote.sin_zero), '\0', 8);

        SYSLOG_INFO("Using IP %s for %s.\n", inet_ntoa(remote.sin_addr), host);

        if ((*fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { 
            SYSLOG_ERR("Can't create TCP socket"); 
            geoip_enabled = 0;
            return 1; 
        }
        else SYSLOG_INFO("Socket created\n");

        // Look into the host entries until one accepts us
        int nEntry;
        for (nEntry=0; hent->h_addr_list[nEntry] != NULL; nEntry++) {
            remote.sin_addr = *(struct in_addr*) hent->h_addr_list[nEntry];
            SYSLOG_INFO("Attempting to connect to %s...\n", inet_ntoa(remote.sin_addr));
            if (connect( *fd, (struct sockaddr*) &remote, 
                            sizeof(struct sockaddr)) < 0) {
                SYSLOG_WARN("  failed");
            }
            else {
                SYSLOG_INFO(" Success. filedesc is #%d!\n", *fd);
                return 0;
            }
        }
        SYSLOG_ERR("All host entries for %s failed.\n", host);
        geoip_enabled = 0;
    }
    return 1;
}

/**
 * return the location of the hostname/ip if it's known,
 * or returns an incomplete location pointer, that is going to be updated
 * ASAP. In the latter case, geoip_request() will be called.
 *
 * [DETAILS in case the host location is not known]
 * Allocate a geo_location for the request and its result. The result will be 
 * on geo_tail, and will be returned. The chained list pointed to by geo_tail
 * will grow by one element.
 *
 * Request will be sent in geoip_events(), whenever select() says the
 * socket is writable.
 *
 * @return NULL in case of error, or the pointer otherwise.
 */
struct geo_location* geoip_locate(char* hostOrIp) {
    struct geo_location* p;

    // see if it's in the cache already
    for (p = geo_tail; p != NULL; p = p->prev) {
        if (p != NULL && strcmp(p->host, hostOrIp) == 0) {
            // warning: this will generate a lot of logs if uncommented
            //SYSLOG_DEBUG("locate '%s' (size is %u):\n", hostOrIp, strlen(hostOrIp));
            //SYSLOG_DEBUG(" FOUND in cache\n");
            return p;
        }
    }
    SYSLOG_DEBUG("locate '%s' (size is %u):\n", hostOrIp, strlen(hostOrIp));
    SYSLOG_INFO(" Not found in cache\n");

    // unknown. allocate a partially filled struct.
    // it will be auto-detected in geoip_events() and server will be queried.
    SYSLOG_DEBUG("log malloc() of %u bytes in geoip_locate\n",
                (unsigned int)sizeof(struct geo_location));

    p = (struct geo_location*) malloc(sizeof(struct geo_location));
    if (p == NULL) {
        SYSLOG_WARN("malloc() of %u bytes failed in geoip_locate (1/2): %s\n",
                (unsigned int)sizeof(struct geo_location), strerror(errno));
        return NULL;
    }

    p->sent = 0;

    SYSLOG_DEBUG("log malloc() of %u bytes in geoip_locate (2/2)\n",
            (unsigned int) strlen(hostOrIp));

    p->host = (char*) malloc(strlen(hostOrIp)+1);
    if (p->host == NULL) {
        SYSLOG_WARN("malloc() of %u bytes failed in geoip_locate (2/2): %s\n",
                (unsigned int) strlen(hostOrIp), strerror(errno));
        free(p);
        return NULL;
    }
    p->prev = geo_tail;
    geo_tail = p;
    
    strcpy(p->host, hostOrIp);
    strcpy(p->ip, "?");
    strcpy(p->country_code, "?");
    strcpy(p->country_name, "?");
    strcpy(p->region_code, "?");
    strcpy(p->region_name, "?");
    strcpy(p->city_name, "?");
    strcpy(p->zip, "?");
    strcpy(p->tz, "?");
    p->longitude = 0.;
    p->latitude = 0.;
    p->metro_code = 0;

    return p;
}

// return 0 if not connected, or fd for select
int geoip_waitfd() {
    if (geoip_fd) return geoip_fd;
    else return 0;
}

// return NULL if nothing to write, or a pointer otherwise
struct geo_location* geoip_next_request_to_write() {
    // Do we have anything to write? -> set write_fds
    struct geo_location* pFirstToWrite = geo_tail;
    if (pFirstToWrite) {
        for (;pFirstToWrite != NULL; pFirstToWrite = pFirstToWrite->prev) {
            if (pFirstToWrite->sent == 0) {
                return pFirstToWrite;
            }
        }
    }
    return NULL;
}

/**
 * Socket is ready to write.
 */
void geoip_write_socket() {
    if (geoip_enabled == 0) return;
    if (geoip_responses_awaited > 0) return;

    // In principle, guaranteed to have something to write,
    //  otherwise the socket wouldn't have been put in select writefds.
    struct geo_location* pFirstToWrite = geoip_next_request_to_write();
    if (pFirstToWrite == NULL) {
        SYSLOG_WARN("Warn: why nothing to write? geoip_write_socket\n");
        return;
    }

    // socket is write-ready
    // FD_CLR(nFd, &write_fds);
    memset(buf, '\0', BUFSIZ);
    sprintf(buf, "GET /csv/%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n",
            pFirstToWrite->host,
            geolocation_server_host,
            geolocation_server_useragent
           );
    pFirstToWrite->sent = 1;
    geo_lastLocationRequested = pFirstToWrite;
    //SYSLOG_DEBUG("Query is:\n<<START>>\n%s<<END>>\n", query);

    int sent = send(geoip_fd, buf, strlen(buf), 0);
    if (sent == -1) { 
        perror("Can't send query. Why couldn't it be send in one pass?"); 
        exit(1); 
    }
    //else SYSLOG_DEBUG(" %d bytes sent.\n", sent);
    geoip_responses_awaited++;
    return;
}

void geoip_print_location(struct geo_location* p) {
    SYSLOG_DEBUG("\
Location of %s:\n\
 ip address: \t%s\n\
 country: \t%s %s\n\
 region: \t%s (%s)\n\
 city: \t%s %s\n\
 timezone: \t%s\n\
 coordinates: \t%f, %f\n\
 metro code: \t%d\n",
        p->host,
        p->ip,
        p->country_name, p->country_code,
        p->region_name, p->region_code,
        p->city_name, p->zip,
        p->tz, 
        p->longitude, p->latitude,
        p->metro_code
    );
}

/*
 * Called in select.c when the fd is ready to read
 * The structure for which we received the response will have its field
 * updated, nothing more to do than extracting them (display done in curses.c)
 */
void geoip_read_socket() {
    // socket has stuffs to read
    // FD_CLR(nFd, &read_fds);      // done in select.c?
    memset(buf, '\0', BUFSIZ);
    SYSLOG_DEBUG("Received answer for geoip (unique fd #%d)\n", geoip_fd);
    char buf[BUFSIZ+1];
    int sent = recv(geoip_fd, buf, BUFSIZ, 0);

    geoip_responses_awaited--;
    if (geoip_responses_awaited < 0) geoip_responses_awaited = 0;

    if (sent == 0) { // FIN
        SYSLOG_WARN("Connection closed by the server!\n");
        SYSLOG_WARN("We would normally need to reconnect\n");
        geoip_fd = 0;
        // geoip_enabled = 0;
        return;
    }
    switch (errno) {
        case EWOULDBLOCK : 
        case EBADF :
        case ECONNREFUSED :
        case EFAULT :
        case EINTR :
        case EINVAL:
        case ENOMEM :
        case ENOTCONN :
        case ENOTSOCK :
            SYSLOG_WARN("Ooops");
            break;
        default: 
            SYSLOG_DEBUG("Response in geoip_read_socket has %d bytes\n", sent);
    }
    char* start = strstr(buf, "\r\n\r\n") + 4;
    if (start == NULL) SYSLOG_WARN("Unable to find start of body: %s\n", buf);
    else {
        // Find requested query
        // Since we have only one socket connected to the server, this should do
        struct geo_location* p = geo_lastLocationRequested;

        // Extract
        SYSLOG_DEBUG("Response body:\n%s\n", start);

        int field = 0;
        int bLoop = 1;
        start--;
        while (bLoop == 1 && start != NULL) {
            start++;
            char* end = strstr(start, ",");
            if (end == NULL) {
                end = start + strlen(start+1) + 1;
                bLoop = 0;      // last iteration
            }

            switch (field) {
                case 0: strncpy(p->ip, start, end - start); break;
                case 1: strncpy(p->country_code, start, end - start); break;
                case 2: strncpy(p->country_name, start, end - start); break;
                case 3: strncpy(p->region_code, start, end - start); break;
                case 4: strncpy(p->region_name, start, end - start); break;
                case 5: strncpy(p->city_name, start, end - start); break;
                case 6: strncpy(p->zip, start, end - start); break;
                case 7: strncpy(p->tz, start, end - start); break;
                case 8: p->longitude = strtof(start, NULL); break;
                case 9: p->latitude = strtof(start, NULL); break;
                case 10: p->metro_code = strtod(start, NULL); break;
                default: 
                         break;
            }
            field++;
            start = strstr(start, ",");
        }
        //geoip_print_location(p);
    }
}


