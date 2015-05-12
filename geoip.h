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

int geoip_enabled;  /* This can be disabled (0) with --no-geolocation or -N */
int geoip_is_ui_shown; //unsure yet 

/*
    GeoIP location for mtr --
    Copyright (C) 2015 by Alexandre van 't Westende <emugel@qq.com>
    Released under GPL, as above.
*/

const char*     geolocation_server_host;
const int       geolocation_server_port;
const char*     geolocation_server_useragent;

/*
    geo_location holds hosts and their responses (i.e. if they arrived yet), 
    and are organized as a chained list, the last pointer is called
    geo_tail (it points to NULL if empty).
*/
struct geo_location {
    char*       host;               // eg: "8.8.8.8", "chess.com", "dyndns.com"
    int         sent;               // 0 if request was not sent yet, 1 otherwise
    
    struct geo_location* prev;      // pointer to the prev. NULL otherwise

    char        ip[40];             // e.g. "68.71.245.5" or "fa42:52::1"
                                    // textual IPv6 uses 39bytes max, 15 for IPv4
    char        country_code[4];    // e.g. "US". ISO 3166 defines 2 to 3
    char        country_name[64];   // e.g. "United States"
    char        region_code[4];     // e.g. "CA"
    char        region_name[64];    // e.g. "California"
    char        city_name[64];      // e.g. "Beverly Hills"
    char        zip[10];            // e.g. "90211"
    char        tz[128];            // e.g. "America/Los_Angeles" (time zone)
    float       longitude;          // e.g. 34.04
    float       latitude;           // e.g. -118.38
    int         metro_code;         // e.g. 803
};


int geoip_open();
struct geo_location* geoip_locate(char* hostOrIp);
int geoip_waitfd();                 // return 0 if not connected, or fd for select
void geoip_read_socket();
void geoip_write_socket();
struct geo_location* geoip_next_request_to_write();
void geoip_print_location(struct geo_location* p) ;
