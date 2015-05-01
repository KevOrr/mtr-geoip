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
#include "geoip.h"

/* Defines */

#undef Debug

#undef CorruptCheck
#undef WipeFrees
#undef WipeMallocs



int use_geoip = 0;
char* freegeoip_address;
char freegeoip_host[] = "freegeoip.net";
char freegeoip_useragent[] = "mtr-geoip";

int monofd;			/* just look up the first one for the time being */
int tmpres;
struct sockaddr_in *monosock;

enum {
   STATE_FINISHED,
   STATE_FAILED,
   STATE_REQ1,
   STATE_REQ2,
   STATE_REQ3
};


double geoip_sweeptime;
extern int af;

dword geoip_mem = 0;

char tempstring[16384+1+1];

#define BashSize 8192			/* Size of hash tables */
#define BashModulo(x) ((x) & 8191)	/* Modulo for hash table size: */
#define HostnameLength 255		/* From RFC */
#define ResRetryDelay1 3
#define ResRetryDelay2 4
#define ResRetryDelay3 5

struct geoip_locate *geoip_idbash[BashSize];
struct geoip_locate *geoip_ipbash[BashSize];
struct geoip_locate *geoip_hostbash[BashSize];
struct geoip_locate *geoip_expireresolves = NULL;
struct geoip_locate *geoip_lastresolve = NULL;
struct logline *geoip_streamlog = NULL;
struct logline *geoip_lastlog = NULL;

#ifdef Debug
int geoip_debug = 1;
#else
int geoip_debug = 0;
#endif

long geoip_idseed = 0xdeadbeef;
long geoip_aseed;

/* Code */
#ifdef CorruptCheck
#define TOT_SLACK 2
#define HEAD_SLACK 1
/* Need an entry for sparc systems here too. 
   Don't try this on Sparc for now. */
#else
#ifdef sparc
#define TOT_SLACK 2
#define HEAD_SLACK 2
#else
#define TOT_SLACK 1
#define HEAD_SLACK 1
#endif
#endif



dword geoip_res_resend = 0;
dword geoip_res_timeout = 0;
dword geoip_resolvecount = 0;

void geoip_open(void)
{
	/* create TCP socket */
	if ((monofd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		fprintf(stderr, "geoip_open: %s\n", "Can't create TCP socket");
		exit(-1);
	}

	/* get IP of freegeoip.net */
	struct hostent *hent;
	int iplen = 15;	// XXX.XXX.XXX.XXX
	freegeoip_address = (char *)malloc(iplen+1);
	memset(freegeoip_address, 0, iplen+1);
	
	int found = 0;
	int i;
	int maxtries = 10;
	int secondsdelay = 5;
	fprintf(stderr, "Attempting to resolve %s", freegeoip_host);
	for (i=0; i<maxtries; i++) {
		if ((hent = gethostbyname(freegeoip_host)) == NULL) {
			fprintf(stderr, "geoip_open: Can't get IP of %s", freegeoip_host);
			exit(1);
		}
		//for (j=0; j<hent->h_length; j++) fprintf(stdout, "hent[%d]: %s\n", j, hent->h_addr_list[j]);
		char** addr = hent->h_addr_list;
		do {
			fprintf(stdout, "hent[]: %s (%s)\n", *addr, freegeoip_address);
			//if (inet_ntop(AF_INET, (void *)hent->h_addr_list[0], freegeoip_address, iplen) == NULL) { 
			if (inet_ntop(AF_INET, (void *)*addr, freegeoip_address, iplen) == NULL) { 
				found = 0; 
				sleep(secondsdelay); 
				switch (errno) {
						case EAFNOSUPPORT:
							fprintf(stderr, " Error: EAFNOSUPPORT (AF_INET not a supported family)\n");
							break;
						case ENOSPC:
							fprintf(stderr, " Error: ENOSPC (The converted string would exceed the size give, ie %d)\n", iplen);
							break;
						default:
							fprintf(stderr, " Error: unknown in geoip_open()\n");
					}
				fprintf(stderr, ".");
			}
			else { found = 1; break; }
		} while (*(++addr));
	}
	if (found == 0) {
			fprintf(stderr, "Error (tried %d times)\n", maxtries);
			switch (errno) {
				case EAFNOSUPPORT:
					fprintf(stderr, " Error: EAFNOSUPPORT (AF_INET not a supported family)\n");
					break;
				case ENOSPC:
					fprintf(stderr, " Error: ENOSPC (The converted string would exceed the size give, ie %d)\n", iplen);
					break;
				default:
					fprintf(stderr, " Error: unknown in geoip_open()\n");
			}
			exit(1);
	}
	else fprintf(stdout, " OK (%s)\n", freegeoip_address);
	monosock = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in *));
	monosock->sin_family = AF_INET;
	tmpres = inet_pton(AF_INET, freegeoip_address, (void *)(&(monosock->sin_addr.s_addr)));
	if (tmpres < 0) {
		fprintf(stderr, "geoip: %s is not a valid IP Address\n", freegeoip_address);
		exit(1);
	}
	monosock->sin_port = htons(80);
	if (connect(monofd, (struct sockaddr *) monosock, sizeof(struct sockaddr)) < 0) {
		perror("Could not connect");
		exit(1);
	}
	
}

void geoip_events(double *sinterval)
{
  struct geoip_locate *rp, *nextrp;
/*	geoip_restell("BLABLABLA");
 */
//	if (!geoip_expireresolves) fprintf(stderr, "NOTHNG TO expiresolves");
//	else {
//		rp = geoip_expireresolves;
//		fprintf(stderr, "TATATA sweep:%f exp:%f", geoip_sweeptime, rp->expiretime);
//		exit(1);
//	}
 for (rp = geoip_expireresolves;(rp) && (geoip_sweeptime >= rp->expiretime);rp = nextrp) {
fprintf(stderr, "state:%d\n", rp->state);
    nextrp = rp->next;
    switch (rp->state) {
    case STATE_FINISHED:	/* TTL has expired */
fprintf(stderr, "finished");
exit(1);
    case STATE_FAILED:	/* Fake TTL has expired */
fprintf(stderr, "failed");
exit(1);
/*      if (geoip_debug) {
	snprintf(tempstring, sizeof(tempstring), "geoip_locator: Cache record for \"%s\" (%s) has expired. (state: %u)  Marked for expire at: %g, time: %g.",
                nonull(rp->hostname), geoip_strlongip( &(rp->ip) ), 
		rp->state, rp->expiretime, geoip_sweeptime);
	geoip_restell(tempstring);
      }
*/
      geoip_unlinkresolve(rp);
      break;
    case STATE_REQ1:	/* First T_PTR send timed out */
			geoip_dorequest(rp);
      geoip_restell("geoip locator: Send #2 for \"PTR\" query...");
      fprintf(stderr, "geoip locator: Send #2 for \"PTR\" query...");
      rp->state++;
      rp->expiretime = geoip_sweeptime + ResRetryDelay2;
      (void)geoip_istime(rp->expiretime,sinterval);
      geoip_res_resend++;
      break;
    case STATE_REQ2:	/* Second T_PTR send timed out */
			geoip_dorequest(rp);
      geoip_restell("geoip locator: Send #3 for \"PTR\" query...");
      rp->state++;
      rp->expiretime = geoip_sweeptime + ResRetryDelay3;
      (void)geoip_istime(rp->expiretime,sinterval);
      geoip_res_resend++;
      break;
    case STATE_REQ3:	/* Third T_PTR timed out */
      geoip_restell("geoip locator: \"PTR\" query timed out.");
      geoip_failrp(rp);
      (void)geoip_istime(rp->expiretime,sinterval);
      geoip_res_timeout++;
      break;
		default:	
			fprintf(stderr, "AFAWFWEF");
			geoip_restell("GEOIP DEFAULT");
    }
  }
  if (geoip_expireresolves)
    (void)geoip_istime(geoip_expireresolves->expiretime,sinterval);
}

void geoip_lookup(ip_t * ip, struct geoip_t *pgeoip_t) 
{
  struct geoip_locate *rp;
//	geoip_restell("IMPL geoip_lookup");

  if ((rp = geoip_findip(ip))) {
    if ((rp->state == STATE_FINISHED) || (rp->state == STATE_FAILED)) {
      if ((rp->state == STATE_FINISHED) && (rp->geoip)) {
					if (geoip_debug) {
						snprintf(tempstring, sizeof(tempstring), "geoip: Used cached record: %s\n",
							geoip_strlongip(ip));
						geoip_restell(tempstring);
					}
					return;
      } else {
					if (geoip_debug) {
						snprintf(tempstring, sizeof(tempstring), "geoip: Used failed record: %s == ???\n",
							geoip_strlongip(ip));
						geoip_restell(tempstring);
					}
					return;
      }
    }
    return;
  }
  if (geoip_debug) fprintf(stderr,"geoip: Added to new record.\n");
  rp = geoip_allocresolve();
  rp->state = STATE_REQ1;
	rp->geoip = pgeoip_t;
  rp->expiretime = geoip_sweeptime + ResRetryDelay1;
  //rp->expiretime = geoip_sweeptime + ResRetryDelay1;
  addrcpy( (void *) &(rp->ip), (void *) ip, af );
  geoip_linkresolve(rp);
  addrcpy( (void *) &(rp->ip), (void *) ip, af );
  geoip_linkresolveip(rp);
  geoip_sendrequest(rp);
  return;
}

void geoip_dorequest(struct geoip_locate* rp)
{
	// ALEX PLEASE CHANGE ALL OF THIS
/*
  packetheader *hp;
  int r,i;
  unsigned char buf[MaxPacketsize];

  r = RES_MKQUERY(QUERY,s,C_IN,type,NULL,0,NULL,(unsigned char*)buf,MaxPacketsize);
  if (r == -1) {
    geoip_restell("Resolver error: Query too large.");
    return;
  }
  hp = (packetheader *)buf;
  hp->id = id;
  for (i = 0;i < myres.nscount;i++)
    if (myres.nsaddr_list[i].sin_family == AF_INET)
      (void)sendto(resfd,buf,r,0,(struct sockaddr *)&myres.nsaddr_list[i],
		   sizeof(struct sockaddr));
#ifdef ENABLE_IPV6
    else if (resfd6 > 0) {
      if (!NSSOCKADDR6(i))
	continue;
      if (NSSOCKADDR6(i)->sin6_family == AF_INET6)
	(void)sendto(resfd6,buf,r,0,(struct sockaddr *) NSSOCKADDR6(i),
		     sizeof(struct sockaddr_in6));
    }
#endif
*/
	// Build query
	char query[256];
	char page[100];
  sprintf(tempstring,"/csv/%u.%u.%u.%u",
	    ((byte *)&rp->ip)[0],
	    ((byte *)&rp->ip)[1],
	    ((byte *)&rp->ip)[2],
	    ((byte *)&rp->ip)[3]);
	strcpy(page, tempstring);
	char *tpl = "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n";
	// -5 to consider the %s %s %s in tpl and the ending \0
	sprintf(query, tpl, page, freegeoip_host, freegeoip_useragent);
	fprintf(stderr, "Query is:\n<<START>>\n%s<<END>>\n", query);

	// Send the query to the server
	int sent = 0;
	while(sent < strlen(query)) {
		int howmany = send(monofd, query+sent, strlen(query)-sent, 0);
		if(howmany == -1) {
			fprintf(stderr, "Can't send query\n");
			exit(1);
		}
		sent += howmany;
	}
	// Now it is time to receive, we will select	
}

void geoip_restell(char *s)
{
  fputs(s,stderr);
  fputs("\r",stderr);
}


void geoip_failrp(struct geoip_locate *rp)
{
  if (rp->state == STATE_FINISHED)
    return;
  rp->state = STATE_FAILED;
  geoip_untieresolve(rp);
  if (geoip_debug)
    geoip_restell("geoip: Locate failed.\n");
}

char *geoip_strlongip(ip_t * ip)
{
#ifdef ENABLE_IPV6
  static char addrstr[INET6_ADDRSTRLEN];

  return (char *) inet_ntop( af, ip, addrstr, sizeof addrstr );
#else
  return inet_ntoa( *ip );
#endif
}

void geoip_unlinkresolve(struct geoip_locate *rp)
{
  geoip_untieresolve(rp);
  geoip_unlinkresolveid(rp);
  geoip_unlinkresolveip(rp);
}

int geoip_waitfd(void)
{		
  return monofd;
}

void geoip_untieresolve(struct geoip_locate *rp)
{
  if (rp->previous)
    rp->previous->next = rp->next;
  else
    geoip_expireresolves = rp->next;
  if (rp->next)
    rp->next->previous = rp->previous;
  else
    geoip_lastresolve = rp->previous;
  geoip_resolvecount--;
}



dword geoip_getidbash(word id)
{
	return (dword) BashModulo(id);
}

void geoip_unlinkresolveid(struct geoip_locate *rp)
{
  dword bashnum;

  bashnum = geoip_getidbash(rp->id);
  if (geoip_idbash[bashnum] == rp) 
    geoip_idbash[bashnum] = (rp->previousid)? rp->previousid : rp->nextid;
  if (rp->nextid)
    rp->nextid->previousid = rp->previousid;
  if (rp->previousid)
    rp->previousid->nextid = rp->nextid;
}

dword geoip_getipbash(ip_t * ip)
{
  char *p = (char *) ip;
  int i, len = 0;
  dword bashvalue = 0;

  switch ( af ) {
  case AF_INET:
    len = sizeof (struct in_addr);
    break;
#ifdef ENABLE_IPV6
  case AF_INET6:
    len = sizeof (struct in6_addr);
    break;
#endif
  }
  for (i = 0; i < len; i++, p++) {
    bashvalue^= *p;
    bashvalue+= (*p >> 1) + (bashvalue >> 1);
  }
  return BashModulo(bashvalue);
}

void geoip_unlinkresolveip(struct geoip_locate *rp)
{
  dword bashnum;

  bashnum = geoip_getipbash( &(rp->ip) );
  if (geoip_ipbash[bashnum] == rp)
    geoip_ipbash[bashnum] = (rp->previousip)? rp->previousip : rp->nextip;
  if (rp->nextip)
    rp->nextip->previousip = rp->previousip;
  if (rp->previousip)
    rp->previousip->nextip = rp->nextip;
}

int geoip_istime(double x,double *sinterval)
{
  if (x) {
    if (x > geoip_sweeptime) {
      if (*sinterval > x - geoip_sweeptime)
	*sinterval = x - geoip_sweeptime;
    } else
      return 1;
  }
  return 0;
}

void geoip_passrp(struct geoip_locate *rp,long ttl)
{
  rp->state = STATE_FINISHED;
  rp->expiretime = geoip_sweeptime + (double)ttl;
  geoip_untieresolve(rp);
  if (geoip_debug) {
    snprintf(tempstring, sizeof(tempstring), "geoip: Locate successful: %s, %s\n",rp->geoip->country_name, rp->geoip->city);
    geoip_restell(tempstring);
  }
}

struct geoip_locate *geoip_findip(ip_t * ip)
{
  struct geoip_locate *rp;
  dword bashnum;

  bashnum = geoip_getipbash(ip);
  rp = geoip_ipbash[bashnum];
  if (rp) {
    while ((rp->nextip) &&
	   ( addrcmp( (void *) ip, (void *) &(rp->nextip->ip), af ) >= 0 ))
      rp = rp->nextip;
    while ((rp->previousip) &&
           ( addrcmp( (void *) ip, (void *) &(rp->previousip->ip), af ) <= 0 ))
      rp = rp->previousip;
    if ( addrcmp( (void *) ip, (void *) &(rp->ip), af ) == 0 ) {
      geoip_ipbash[bashnum] = rp;
      return rp;
    } else
      return NULL;
  }
  return rp; /* NULL */
}

struct geoip_locate *geoip_allocresolve(void)
{
  struct geoip_locate *rp;

  rp = (struct geoip_locate*)geoip_statmalloc(sizeof(struct geoip_locate));
  if (!rp) {
    fprintf(stderr,"statmalloc() failed: %s\n",strerror(errno));
    exit(-1);
  }
  memset(rp,0, sizeof(struct geoip_locate));
  return rp;
}


void geoip_linkresolve(struct geoip_locate *rp)
{
  struct geoip_locate *irp;

  if (geoip_expireresolves) {
    irp = geoip_expireresolves;
    while ((irp->next) && (rp->expiretime >= irp->expiretime)) irp = irp->next;
    if (rp->expiretime >= irp->expiretime) {
      rp->next = NULL;
      rp->previous = irp;
      irp->next = rp;
      geoip_lastresolve = rp;
    } else {
      rp->previous = irp->previous;
      rp->next = irp;
      if (irp->previous)
	irp->previous->next = rp;
      else
	geoip_expireresolves = rp;
      irp->previous = rp;
    }
  } else {
    rp->next = NULL;
    rp->previous = NULL;
    geoip_expireresolves = geoip_lastresolve = rp;
  }
  geoip_resolvecount++;
}

void geoip_sendrequest(struct geoip_locate *rp)
{
  do {
    geoip_idseed = (((geoip_idseed + geoip_idseed) | (long)time(NULL)) + geoip_idseed - 0x54bad4a) ^ geoip_aseed;
    geoip_aseed^= geoip_idseed;
    rp->id = (word)geoip_idseed;
  } while (geoip_findid(rp->id));
  geoip_linkresolveid(rp);
	geoip_dorequest(rp);
}

void geoip_linkresolveid(struct geoip_locate *addrp)
{
  struct geoip_locate *rp;
  dword bashnum;

  bashnum = geoip_getidbash(addrp->id);
  rp = geoip_idbash[bashnum];
  if (rp) {
    while ((rp->nextid) && (addrp->id > rp->nextid->id))
      rp = rp->nextid;
    while ((rp->previousid) && (addrp->id < rp->previousid->id))
      rp = rp->previousid;
    if (rp->id < addrp->id) {
      addrp->previousid = rp;
      addrp->nextid = rp->nextid;
      if (rp->nextid)
	rp->nextid->previousid = addrp;
      rp->nextid = addrp;
    } else {
      addrp->previousid = rp->previousid;
      addrp->nextid = rp;
      if (rp->previousid)
	rp->previousid->nextid = addrp;
      rp->previousid = addrp;
    }
  } else
    addrp->nextid = addrp->previousid = NULL;
    geoip_idbash[bashnum] = addrp;
}

void geoip_linkresolveip(struct geoip_locate *addrp)
{
  struct geoip_locate *rp;
  dword bashnum;

  bashnum = geoip_getipbash( &(addrp->ip) );
  rp = geoip_ipbash[bashnum];
  if (rp) {
    while ((rp->nextip) &&
	   ( addrcmp( (void *) &(addrp->ip),
		      (void *) &(rp->nextip->ip), af ) > 0 ))
      rp = rp->nextip;
    while ((rp->previousip) &&
	   ( addrcmp( (void *) &(addrp->ip),
		      (void *) &(rp->previousip->ip), af ) < 0 ))
      rp = rp->previousip;
    if ( addrcmp( (void *) &(rp->ip), (void *) &(addrp->ip), af ) < 0 ) {
      addrp->previousip = rp;
      addrp->nextip = rp->nextip;
      if (rp->nextip)
	rp->nextip->previousip = addrp;
      rp->nextip = addrp;
    } else {
      addrp->previousip = rp->previousip;
      addrp->nextip = rp;
      if (rp->previousip)
	rp->previousip->nextip = addrp;
      rp->previousip = addrp;
    }
  } else
    addrp->nextip = addrp->previousip = NULL;
  geoip_ipbash[bashnum] = addrp;
}

void *geoip_statmalloc(size_t size)
{
  void *p;
  size_t mallocsize;

  geoip_mem+= size;
  mallocsize = size + TOT_SLACK * sizeof(dword);

  p = malloc(mallocsize);
  if (!p) {
    fprintf(stderr,"malloc() of %u bytes failed: %s\n", (unsigned int)size, strerror(errno));
    exit(-1);
  }
  *((dword *)p) = (dword)size;
#ifdef CorruptCheck
  *(byte *)((char *)p + size + sizeof(dword) + sizeof(byte) * 0) = 0xde;
  *(byte *)((char *)p + size + sizeof(dword) + sizeof(byte) * 1) = 0xad;
  *(byte *)((char *)p + size + sizeof(dword) + sizeof(byte) * 2) = 0xbe;
  *(byte *)((char *)p + size + sizeof(dword) + sizeof(byte) * 3) = 0xef;
#endif
  p = (void *)((dword *)p + HEAD_SLACK);
#ifdef WipeMallocs
  memset(p,0xf0,size);
#endif
  return p;
}


struct geoip_locate *geoip_findid(word id)
{
  struct geoip_locate *rp;
  int bashnum;

  bashnum = geoip_getidbash(id);
  rp = geoip_idbash[bashnum];
  if (rp) {
    while ((rp->nextid) && (id >= rp->nextid->id))
      rp = rp->nextid;
    while ((rp->previousid) && (id <= rp->previousid->id))
      rp = rp->previousid;
    if (id == rp->id) {
      geoip_idbash[bashnum] = rp;
      return rp;
    } else
      return NULL;
  }
  return rp; /* NULL */
}

