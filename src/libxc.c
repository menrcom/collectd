/**
 * collectd - src/libxc.c
 * Copyright (C) 2011       Stepan G. Fedorov
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Stepan G. Fedorov <sf at clodo.ru>
 **/

/* #define _BSD_SOURCE */

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#define ND 512

static char *unknown_domain = "unknown";

struct xclist {
	struct xclist *next;
	int domain;
	xc_domaininfo_t di;
	unsigned int vnc_port;
	char *name;
};

static long ps, pagesize;
static int xenhandle;

static struct xclist *dl = NULL;

struct xclist *new_domain(int domain) {
	struct xclist dlp = malloc(sizeof(struct xclist));
	dlp->domain = domain;
	dlp->next = NULL;
	dlp->name = unknown_domain;

	return tdlp;
}

/* Find existing record, or create new one: */
struct xclist *find_domain(struct xclist *dlp, int domain) {

	for(;dlp && dlp->next; dlp=dlp->next) if(dlp->domain == domain) return dlp;

	if(dlp) {
		dlp->next = new_domain(domain);
		return dlp->next;
	}

	return NULL;
}

void freelist(char **p, int n) {
	int in;
	for(in = 0; in < n; in++) free(p[in]);
	free(p);
}


static void xenmon_submit (char * vmname, int domain, int state, short vncport, gauge_t cpunum, gauge_t curmem, gauge_t maxmem)
{
	value_t values[3];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = snum;
	values[1].gauge = mnum;
	values[2].gauge = lnum;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "load", sizeof (vl.plugin));
	sstrncpy (vl.type, "load", sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

struct xclist *update_domain_list(struct xclist *dlp, struct xc_domaininfo_t * dip, int nd) {
	int i;

	while( dlp || i < nd )
		if( dlp->domain == dip[i].domain ) {
			dlp->di = dip[i];
			dlp = dlp->next;
			i++;
		} else if(dlp && i==nd) {
			dlp = dlp->next;
			i = 0;
		}
	}

	return dlp;
}

int xenmon_merge_data_from_hypervisor(void) {

	struct xc_domainlist_t di[ND];
	int nd = 0, total_nd = 0;
	int sd = 1; /* Start domain */
	struct xclist *dlp = dl;

	do {

		nd = xc_domain_getinfolist(xenhandle, sd, ND, di);

		if( nd != -1 ) {
			dlp = update_domain_list(dlp, di, nd);
			sd = di[nd-1].domain + 1;
			total_nd += nd;
		}

	} while (nd == ND);

	return total_nd;

}

int xenmon_get_data_from_xenstore(void) {

	struct xs_handle *xs = xs_daemon_open();
	if(xs == NULL) {
		printf("Cannot connect to Xenstore.\n");
		return;
	}

	int fd = xs_fileno(xs);

	fcntl(fd, F_SETFL, O_NONBLOCK);

	int num;
	char **dir = xs_directory(xs, XBT_NULL, "/local/domain", &num);

	if( !dir ) return 0;

	if( !dl ) dl = new_domain(atoi(dir[0]));

	struct xclist *dlp = dl;
	int i;

	for(i=0; i < num; i++) {

		struct xclist *tdlp = find_domain(dlp, atoi(dir[i]));

		if (!tdlp) continue;

		char xspath[255];

		/* Initialize domain name: */
		if ( tdlp->name == unknown_domain ) {
			snprintf(xspath, 255, "/local/domain/%s/name", dir[i]);
			int len = 0;
			tdlp->name = xs_read(xs, XBT_NULL, xspath, &len);
			if( !tdlp->name ) tdlp->name = unknown_domain;				
		}

		/* Initialize vnc-port: */
		if (tdlp->vnc_port == 0) {
			snprintf(xspath, 255,"/local/domain/%s/console/vnc-port", dir[i]);
			char *buf = xs_read(xs, XBT_NULL, xspath, &len);

			if ( buf ) {
				tdlp->vnc_port = atoi(buf);
				free(buf);
			} else dlp->vnc_port = 0;
		}
			
		dlp = tdlp;

	} /* for (i=0;i<num;i++)*/

	/* TODO: close connextion to xenstore here. */
	return num;

} /* xenmon_merge_data_from_xenstore() */


static int xenmon_read (void) {

	if(xenmon_get_data_from_xenstore() && xenmon_merge_data_from_hypervisor())
		return xenmon_submit_data();

}

static int xenmon_init(void) {
	pagesize = sysconf(_SC_PAGESIZE);
	if(pagesize == -1) pagesize = 4096;
	ps = 1048576 / pagesize; /* Number of pages in one megabyte. */

	xenhandle = xc_interface_open();

	if(xenhandle == -1) return -1; /* fail(-1, "Cannot get xc handle."); */
}

void module_register (void)
{
	plugin_register_init ("xenmon", xenmon_init);
	plugin_register_read ("xenmon", xenmon_read);
} /* void module_register */

/*
 * Local variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
