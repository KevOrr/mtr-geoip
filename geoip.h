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

#include <config.h>
#include <netinet/in.h>
struct geoip_t {
        int            	date_requested; /* last time it was requested */
                                        /* for retry after timeout */
        unsigned short  is_available;   /* was it received */

        char            *country_code;  /* US */
        char            *country_name;  /* United States */
        char            *region_code;   /* CA */
        char            *region_name;   /* California */
        char            *city;          /* Mountain View */
        char            *zip_code;      /* 94040 */
        char            *time_zone;     /* America/Los Angeles */
        float           latitude;       /* 37.386 */
        float           longitude;      /* -122.084 */
        short int       metro_code;     /* 807 */
} geoip_t; 

void geoip_open(void);
void geoip_lookup(ip_t * address, struct geoip_t *pgeoip_t); /* start locating */

void geoip_events(double *sinterval);
int geoip_waitfd(void);

struct geoip_locate {
	struct geoip_locate *next;
	struct geoip_locate *previous;
	struct geoip_locate *nextid;
	struct geoip_locate *previousid;
	struct geoip_locate *nextip;
	struct geoip_locate *previousip;
	float  		    expiretime;
	struct geoip_t 	    *geoip;
	ip_t	            ip;
	word 	     	    id;
	byte	  	    state;
};

void geoip_unlinkresolve(struct geoip_locate *rp);
void geoip_unlinkresolveid(struct geoip_locate *rp);
void geoip_unlinkresolveip(struct geoip_locate *rp);
dword geoip_getidbash(word id);
dword geoip_getipbash(ip_t *ip);
int geoip_istime(double x, double *sinterval);
void geoip_passrp(struct geoip_locate *rp, long ttl);
void geoip_dorequest(char *s, word id);
void geoip_restell(char *s);
void geoip_untieresolve(struct geoip_locate *rp);
void geoip_failrp(struct geoip_locate *rp);
char *geoip_strlongip(ip_t *ip);
void geoip_sendrequest(struct geoip_locate *rp);
void geoip_resendrequest(struct geoip_locate *rp);
struct geoip_locate *geoip_findip(ip_t *ip);
void geoip_linkresolve(struct geoip_locate *rp);
struct geoip_locate *geoip_allocresolve(void);
void geoip_linkresolve(struct geoip_locate *rp);
void geoip_linkresolveid(struct geoip_locate *addrp);
void geoip_linkresolveip(struct geoip_locate *addrp);
void *geoip_statmalloc(size_t size);
struct geoip_locate *geoip_findip(ip_t * ip);
struct geoip_locate *geoip_findid(word id);

/*
Where is it called?
-------------------
mtr_curses_hosts(int startstat)
	if (r == geoip_t* net_geoip(at) == null)
		geoip_lookup(ip, r)
			.. we get an ip and a geoip_t pointing to the result to fill
			.. prepare, if not cached :
				.. a geoip_locate* w/ geoip_allocresolve()
				.. set its ->geoip 
				.. set its initial state to STATE_REQ1
				.. set its expire time
				.. addrcpy
				.. geoip_linkresolve()
				.. addrcpy
				.. geoip_linkresolveip()
				.. geoip_sendrequest(rp)
					.. loop geoip_findid(rp->id)
					.. geoip_linkresolveid(rp)
					.. geoip_resendrequest(rp)
						.. // geoip_dorequest(tempstring, rp->id)

					.. read socket to process the chain of geoip_locate
						.. in geoip_events (called by select.c) it will then receive the writ
*/

