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

/*
    Non-blocking DNS portion --
    Copyright (C) 1998 by Simon Kirby <sim@neato.org>
    Released under GPL, as above.

    Modified by Av'tW for geoip location on 2015 April
*/

#include "config.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef __APPLE__
#define BIND_8_COMPAT
#endif
#include <arpa/nameser.h>
#ifdef HAVE_ARPA_NAMESER_COMPAT_H
#include <arpa/nameser_compat.h>
#endif
#include <netdb.h>
#include <resolv.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/syscall.h>

#include "mtr.h"
#include "net.h"

/* Defines */

#undef Debug

#undef CorruptCheck
#undef WipeFrees
#undef WipeMallocs

int use_geoip = 0;

void geoip_open(void)
{
    /* Check whether we have a tool called geoip, and whether it's secured
     * (only readable by root)
     */
    fprintf(stdout, "Tadaa\n");
}

char *geoip_lookup(ip_t * ip)
{
/*  struct resolve *rp;

  if ((rp = findip(ip))) {
    if ((rp->state == STATE_FINISHED) || (rp->state == STATE_FAILED)) {
      if ((rp->state == STATE_FINISHED) && (rp->hostname)) {
	if (debug) {
	  snprintf(tempstring, sizeof(tempstring), "Resolver: Used cached record: %s == \"%s\".\n",
		  strlongip(ip),rp->hostname);
	  restell(tempstring);
	}
	return rp->hostname;
      } else {
	if (debug) {
	  snprintf(tempstring, sizeof(tempstring), "Resolver: Used failed record: %s == ???\n",
		  strlongip(ip));
	  restell(tempstring);
	}
	return NULL;
      }
    }
    return NULL;
  }
  if (debug)
    fprintf(stderr,"Resolver: Added to new record.\n");
  rp = allocresolve();
  rp->state = STATE_PTRREQ1;
  rp->expiretime = sweeptime + ResRetryDelay1;
  addrcpy( (void *) &(rp->ip), (void *) ip, af );
  linkresolve(rp);
  addrcpy( (void *) &(rp->ip), (void *) ip, af );
  linkresolveip(rp);
  sendrequest(rp,T_PTR);
  */
  return NULL;
}

