/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef _ralink_info_h_
#define _ralink_info_h_

#include <ralink_boards.h>
#include <ralink_priv.h>
#include <netutils.h>
#include <rtutils.h>
#include <shutils.h>
#include <nvram_linux.h>

typedef FILE * webs_t;



// ralink.c
struct ifreq;
struct iwreq;
int get_apcli_peer_connected(const char *ifname, struct iwreq *p_wrq);
int get_apcli_wds_entry(const char *ifname, RT_802_11_MAC_ENTRY *pme);
int is_mac_in_sta_list(const unsigned char* p_mac);
int ej_lan_leases(int eid, webs_t wp, int argc, char **argv);
int ej_vpns_leases(int eid, webs_t wp, int argc, char **argv);
int ej_nat_table(int eid, webs_t wp, int argc, char **argv);
int ej_route_table(int eid, webs_t wp, int argc, char **argv);
int ej_conntrack_table(int eid, webs_t wp, int argc, char **argv);
int wl_ioctl(const char *ifname, int cmd, struct iwreq *pwrq);
int ej_wl_auth_list(int eid, webs_t wp, int argc, char **argv);
#if BOARD_HAS_5G_RADIO
int ej_wl_status_5g(int eid, webs_t wp, int argc, char **argv);
int ej_wl_scan_5g(int eid, webs_t wp, int argc, char **argv);
int ej_wl_bssid_5g(int eid, webs_t wp, int argc, char **argv);
#endif
int ej_wl_status_2g(int eid, webs_t wp, int argc, char **argv);
int ej_wl_scan_2g(int eid, webs_t wp, int argc, char **argv);
int ej_wl_bssid_2g(int eid, webs_t wp, int argc, char **argv);

void char_to_ascii(char *output, char *input);

void help();
int main(int argc, char **argv);

#endif /* _ralink_info_h_ */
