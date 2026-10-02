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

#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/klog.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <iwlib.h>

#include "ralink-info.h"

#define websWrite fprintf

/******************************************************************************************************************************************/

/************************ CONSTANTS & MACROS ************************/

/*
 * Constants fof WE-9->15
 */
#define IW15_MAX_FREQUENCIES 16
#define IW15_MAX_BITRATES 8
#define IW15_MAX_TXPOWER 8
#define IW15_MAX_ENCODING_SIZES 8
#define IW15_MAX_SPY 8
#define IW15_MAX_AP 8

/****************************** TYPES ******************************/

/*
 *	Struct iw_range up to WE-15
 */
struct iw15_range {
	__u32 throughput;
	__u32 min_nwid;
	__u32 max_nwid;
	__u16 num_channels;
	__u8 num_frequency;
	struct iw_freq freq[IW15_MAX_FREQUENCIES];
	__s32 sensitivity;
	struct iw_quality max_qual;
	__u8 num_bitrates;
	__s32 bitrate[IW15_MAX_BITRATES];
	__s32 min_rts;
	__s32 max_rts;
	__s32 min_frag;
	__s32 max_frag;
	__s32 min_pmp;
	__s32 max_pmp;
	__s32 min_pmt;
	__s32 max_pmt;
	__u16 pmp_flags;
	__u16 pmt_flags;
	__u16 pm_capa;
	__u16 encoding_size[IW15_MAX_ENCODING_SIZES];
	__u8 num_encoding_sizes;
	__u8 max_encoding_tokens;
	__u16 txpower_capa;
	__u8 num_txpower;
	__s32 txpower[IW15_MAX_TXPOWER];
	__u8 we_version_compiled;
	__u8 we_version_source;
	__u16 retry_capa;
	__u16 retry_flags;
	__u16 r_time_flags;
	__s32 min_retry;
	__s32 max_retry;
	__s32 min_r_time;
	__s32 max_r_time;
	struct iw_quality avg_qual;
};

/*
 * Union for all the versions of iwrange.
 * Fortunately, I mostly only add fields at the end, and big-bang
 * reorganisations are few.
 */
union iw_range_raw {
	struct iw15_range range15; /* WE 9->15 */
	struct iw_range range;	 /* WE 16->current */
};

/*
 * Offsets in iw_range struct
 */
#define iwr15_off(f) (((char *)&(((struct iw15_range *)NULL)->f)) - (char *)NULL)
#define iwr_off(f) (((char *)&(((struct iw_range *)NULL)->f)) - (char *)NULL)

/* Disable runtime version warning in ralink_get_range_info() */
int iw_ignore_version_sp = 0;

/*------------------------------------------------------------------*/
/*
 * Get the range information out of the driver
 */
int ralink_get_range_info(iwrange *range, char *buffer, int length) {
	union iw_range_raw *range_raw;

	/* Point to the buffer */
	range_raw = (union iw_range_raw *)buffer;

	/* For new versions, we can check the version directly, for old versions
	 * we use magic. 300 bytes is a also magic number, don't touch... */
	if (length < 300) {
		/* That's v10 or earlier. Ouch ! Let's make a guess...*/
		range_raw->range.we_version_compiled = 9;
	}

	/* Check how it needs to be processed */
	if (range_raw->range.we_version_compiled > 15) {
		/* This is our native format, that's easy... */
		/* Copy stuff at the right place, ignore extra */
		memcpy((char *)range, buffer, sizeof(iwrange));
	} else {
		/* Zero unknown fields */
		bzero((char *)range, sizeof(struct iw_range));

		/* Initial part unmoved */
		memcpy((char *)range, buffer, iwr15_off(num_channels));
		/* Frequencies pushed futher down towards the end */
		memcpy((char *)range + iwr_off(num_channels), buffer + iwr15_off(num_channels), iwr15_off(sensitivity) - iwr15_off(num_channels));
		/* This one moved up */
		memcpy((char *)range + iwr_off(sensitivity), buffer + iwr15_off(sensitivity), iwr15_off(num_bitrates) - iwr15_off(sensitivity));
		/* This one goes after avg_qual */
		memcpy((char *)range + iwr_off(num_bitrates), buffer + iwr15_off(num_bitrates), iwr15_off(min_rts) - iwr15_off(num_bitrates));
		/* Number of bitrates has changed, put it after */
		memcpy((char *)range + iwr_off(min_rts), buffer + iwr15_off(min_rts), iwr15_off(txpower_capa) - iwr15_off(min_rts));
		/* Added encoding_login_index, put it after */
		memcpy((char *)range + iwr_off(txpower_capa), buffer + iwr15_off(txpower_capa), iwr15_off(txpower) - iwr15_off(txpower_capa));
		/* Hum... That's an unexpected glitch. Bummer. */
		memcpy((char *)range + iwr_off(txpower), buffer + iwr15_off(txpower), iwr15_off(avg_qual) - iwr15_off(txpower));
		/* Avg qual moved up next to max_qual */
		memcpy((char *)range + iwr_off(avg_qual), buffer + iwr15_off(avg_qual), sizeof(struct iw_quality));
	}

	/* We are now checking much less than we used to do, because we can
	 * accomodate more WE version. But, there are still cases where things
	 * will break... */
	if (!iw_ignore_version_sp) {
		/* We don't like very old version (unfortunately kernel 2.2.X) */
		if (range->we_version_compiled <= 10) {
			fprintf(stderr, "Warning: Driver for device %s has been compiled with an "
							"ancient version\n",
					"raxx");
			fprintf(stderr, "of Wireless Extension, while this program support "
							"version 11 and later.\n");
			fprintf(stderr, "Some things may be broken...\n\n");
		}

		/* We don't like future versions of WE, because we can't cope with
		 * the unknown */
		if (range->we_version_compiled > WE_MAX_VERSION) {
			fprintf(stderr, "Warning: Driver for device %s has been compiled with version %d\n", "raxx", range->we_version_compiled);
			fprintf(stderr, "of Wireless Extension, while this program supports up "
							"to version %d.\n",
					WE_VERSION);
			fprintf(stderr, "Some things may be broken...\n\n");
		}

		/* Driver version verification */
		if ((range->we_version_compiled > 10) && (range->we_version_compiled < range->we_version_source)) {
			fprintf(stderr, "Warning: Driver for device %s recommend version %d of "
							"Wireless Extension,\n",
					"raxx", range->we_version_source);
			fprintf(stderr, "but has been compiled with version %d, therefore some "
							"driver features\n",
					range->we_version_compiled);
			fprintf(stderr, "may not be available...\n\n");
		}
		/* Note : we are only trying to catch compile difference, not source.
		 * If the driver source has not been updated to the latest, it doesn't
		 * matter because the new fields are set to zero */
	}

	/* Don't complain twice.
	 * In theory, the test apply to each individual driver, but usually
	 * all drivers are compiled from the same kernel. */
	iw_ignore_version_sp = 1;

	return (0);
}

#define RTPRIV_IOCTL_SHOW (SIOCIWFIRSTPRIV + 0x11)
#define RTPRIV_IOCTL_GET_MAC_TABLE (SIOCIWFIRSTPRIV + 0x0F)
#define RTPRIV_IOCTL_GET_MAC_TABLE_STRUCT (SIOCIWFIRSTPRIV + 0x1F)

int wl_ioctl(const char *ifname, int cmd, struct iwreq *pwrq) {
	int ret = 0;
	int s;

	/* open socket to kernel */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		return errno;
	}

	/* do it */
	strncpy(pwrq->ifr_name, ifname, IFNAMSIZ);
	if ((ret = ioctl(s, cmd, pwrq)) < 0)
		perror(pwrq->ifr_name);

	/* cleanup */
	close(s);
	return ret;
}

char *GetBW(int BW) {
	switch (BW) {
	case BW_10:
		return "10M";
	case BW_20:
		return "20M";
	case BW_40:
		return "40M";
	case BW_80:
		return "80M";
	default:
		return "N/A";
	}
}

char *GetPhyMode(int Mode) {
	switch (Mode) {
	case MODE_CCK:
		return "CCK";
	case MODE_OFDM:
		return "OFDM";
	case MODE_HTMIX:
		return "HTMIX";
	case MODE_HTGREENFIELD:
		return "HT_GF";
	case MODE_VHT:
		return "VHT";
	default:
		return "N/A";
	}
}

static int getMCS(MACHTTRANSMIT_SETTING HTSetting) {
	int mcs_1ss = (int)HTSetting.field.MCS;

	if (HTSetting.field.MODE >= MODE_VHT) {
		if (mcs_1ss > 9)
			mcs_1ss %= 16;
	}

	return mcs_1ss;
}

static const int MCSMappingRateTable[] = {
	2, 4, 11, 22, // CCK

	12, 18, 24, 36, 48, 72, 96, 108, // OFDM

	13, 26, 39, 52, 78, 104, 117, 130, 26, 52, 78, 104, 156, 208, 234,
	260,								  // 11n: 20MHz, 800ns GI, MCS: 0 ~ 15
	39, 78, 117, 156, 234, 312, 351, 390, // 11n: 20MHz, 800ns GI, MCS: 16 ~
										  // 23
	27, 54, 81, 108, 162, 216, 243, 270, 54, 108, 162, 216, 324, 432, 486,
	540,								   // 11n: 40MHz, 800ns GI, MCS: 0 ~ 15
	81, 162, 243, 324, 486, 648, 729, 810, // 11n: 40MHz, 800ns GI, MCS: 16 ~
										   // 23
	14, 29, 43, 57, 87, 115, 130, 144, 29, 59, 87, 115, 173, 230, 260,
	288,								  // 11n: 20MHz, 400ns GI, MCS: 0 ~ 15
	43, 87, 130, 173, 260, 317, 390, 433, // 11n: 20MHz, 400ns GI, MCS: 16 ~
										  // 23
	30, 60, 90, 120, 180, 240, 270, 300, 60, 120, 180, 240, 360, 480, 540,
	600, // 11n: 40MHz, 400ns GI, MCS: 0 ~ 15
	90, 180, 270, 360, 540, 720, 810, 900,

	13, 26, 39, 52, 78, 104, 117, 130, 156,			 // 11ac: 20Mhz, 800ns GI, MCS:
													 // 0~8
	27, 54, 81, 108, 162, 216, 243, 270, 324, 360,   // 11ac: 40Mhz, 800ns GI,
													 // MCS: 0~9
	59, 117, 176, 234, 351, 468, 527, 585, 702, 780, // 11ac: 80Mhz, 800ns GI,
													 // MCS: 0~9
	14, 29, 43, 57, 87, 115, 130, 144, 173,			 // 11ac: 20Mhz, 400ns GI, MCS:
													 // 0~8
	30, 60, 90, 120, 180, 240, 270, 300, 360, 400,   // 11ac: 40Mhz, 400ns GI,
													 // MCS: 0~9
	65, 130, 195, 260, 390, 520, 585, 650, 780, 867  // 11ac: 80Mhz, 400ns GI,
													 // MCS: 0~9
};

static int getRate(MACHTTRANSMIT_SETTING HTSetting) {
	int rate_count = sizeof(MCSMappingRateTable) / sizeof(int);
	int rate_index = 0;
	int num_ss_vht = 1;

	if (HTSetting.field.MODE >= MODE_VHT) {
		int mcs_1ss = (int)HTSetting.field.MCS;

		if (mcs_1ss > 9) {
			num_ss_vht = (mcs_1ss / 16) + 1;
			mcs_1ss %= 16;
		}
		if (HTSetting.field.BW == BW_20)
			rate_index = 108 + ((unsigned char)HTSetting.field.ShortGI * 29) + mcs_1ss;
		else if (HTSetting.field.BW == BW_40)
			rate_index = 117 + ((unsigned char)HTSetting.field.ShortGI * 29) + mcs_1ss;
		else if (HTSetting.field.BW == BW_80)
			rate_index = 127 + ((unsigned char)HTSetting.field.ShortGI * 29) + mcs_1ss;
	} else if (HTSetting.field.MODE >= MODE_HTMIX)
		rate_index = 12 + ((unsigned char)HTSetting.field.BW * 24) + ((unsigned char)HTSetting.field.ShortGI * 48) + ((unsigned char)HTSetting.field.MCS);
	else if (HTSetting.field.MODE == MODE_OFDM)
		rate_index = (unsigned char)(HTSetting.field.MCS) + 4;
	else if (HTSetting.field.MODE == MODE_CCK)
		rate_index = (unsigned char)(HTSetting.field.MCS);

	if (rate_index < 0)
		rate_index = 0;

	if (rate_index >= rate_count)
		rate_index = rate_count - 1;

	return (MCSMappingRateTable[rate_index] * num_ss_vht * 5) / 10;
}

int get_apcli_peer_connected(const char *ifname, struct iwreq *p_wrq) {
	if (wl_ioctl(ifname, SIOCGIWAP, p_wrq) >= 0) {
		p_wrq->u.ap_addr.sa_family = ARPHRD_ETHER;
		if (p_wrq->u.ap_addr.sa_data[0] || p_wrq->u.ap_addr.sa_data[1] || p_wrq->u.ap_addr.sa_data[2] || p_wrq->u.ap_addr.sa_data[3] || p_wrq->u.ap_addr.sa_data[4] || p_wrq->u.ap_addr.sa_data[5]) {
			return 1;
		}
	}

	return 0;
}

int get_apcli_wds_entry(const char *ifname, RT_802_11_MAC_ENTRY *pme) {
	struct iwreq wrq;

	bzero(pme, sizeof(RT_802_11_MAC_ENTRY));
	wrq.u.data.pointer = pme;
	wrq.u.data.length = sizeof(RT_802_11_MAC_ENTRY);
	wrq.u.data.flags = 0;

	if (wl_ioctl(ifname, RTPRIV_IOCTL_GET_MAC_TABLE_STRUCT, &wrq) >= 0 && wrq.u.data.length == sizeof(RT_802_11_MAC_ENTRY)) {
		return 1;
	}

	return 0;
}

int is_mac_in_sta_list(const unsigned char *p_mac) {
	int i;
	struct iwreq wrq;
	char mac_table_data[4096];

#if BOARD_HAS_5G_RADIO
	/* query wl for authenticated sta list */
	memset(mac_table_data, 0, sizeof(mac_table_data));
	wrq.u.data.pointer = mac_table_data;
	wrq.u.data.length = sizeof(mac_table_data);
	wrq.u.data.flags = 0;
	if (wl_ioctl(IFNAME_5G_MAIN, RTPRIV_IOCTL_GET_MAC_TABLE_STRUCT, &wrq) >= 0) {
		RT_802_11_MAC_TABLE *mp = (RT_802_11_MAC_TABLE *)wrq.u.data.pointer;
		for (i = 0; i < mp->Num; i++) {
			if (memcmp(mp->Entry[i].Addr, p_mac, ETHER_ADDR_LEN) == 0)
				return (mp->Entry[i].ApIdx == 0) ? 3 : 4;
		}
	}
#endif

#if defined(USE_RT3352_MII)
	if (nvram_get_int("inic_disable") == 1)
		return 0;

	if (nvram_get_int("mlme_radio_rt") == 0)
		return 0;

	/* query rt for authenticated sta list */
	memset(mac_table_data, 0, sizeof(mac_table_data));
	wrq.u.data.pointer = mac_table_data;
	wrq.u.data.length = sizeof(mac_table_data);
	wrq.u.data.flags = 0;
	if (wl_ioctl(IFNAME_2G_MAIN, RTPRIV_IOCTL_GET_MAC_TABLE, &wrq) >= 0) {
		RT_802_11_MAC_TABLE_INIC *mp = (RT_802_11_MAC_TABLE_INIC *)wrq.u.data.pointer;
		for (i = 0; i < mp->Num; i++) {
			if (memcmp(mp->Entry[i].Addr, p_mac, ETHER_ADDR_LEN) == 0)
				return (mp->Entry[i].ApIdx == 0) ? 1 : 2;
		}
	}
#else
	/* query rt for authenticated sta list */
	memset(mac_table_data, 0, sizeof(mac_table_data));
	wrq.u.data.pointer = mac_table_data;
	wrq.u.data.length = sizeof(mac_table_data);
	wrq.u.data.flags = 0;
	if (wl_ioctl(IFNAME_2G_MAIN, RTPRIV_IOCTL_GET_MAC_TABLE_STRUCT, &wrq) >= 0) {
		RT_802_11_MAC_TABLE *mp = (RT_802_11_MAC_TABLE *)wrq.u.data.pointer;
		for (i = 0; i < mp->Num; i++) {
			if (memcmp(mp->Entry[i].Addr, p_mac, ETHER_ADDR_LEN) == 0)
				return (mp->Entry[i].ApIdx == 0) ? 1 : 2;
		}
	}
#endif

	return 0;
}

static int print_apcli_wds_header(webs_t wp, const char *caption) {
	int ret = 0;

	ret += websWrite(wp, caption);
	ret += websWrite(wp, "----------------------------------------\n");
	ret += websWrite(wp, "%-19s%-8s%-4s%-4s%-4s%-5s%-5s%-6s%-5s\n", "BSSID", "PhyMode", " BW", "MCS", "SGI", "LDPC", "STBC", "TRate", "RSSI");

	return ret;
}

static int print_apcli_wds_entry(webs_t wp, RT_802_11_MAC_ENTRY *me, int num_ss_rx) {
	int ret, rssi;

	ret = 0;

	rssi = -127;
	if ((int)me->AvgRssi0 > rssi && me->AvgRssi0 != 0)
		rssi = (int)me->AvgRssi0;
	if (num_ss_rx > 1) {
		if ((int)me->AvgRssi1 > rssi && me->AvgRssi1 != 0)
			rssi = (int)me->AvgRssi1;
	}
	if (num_ss_rx > 2) {
		if ((int)me->AvgRssi2 > rssi && me->AvgRssi2 != 0)
			rssi = (int)me->AvgRssi2;
	}

	ret += websWrite(wp, "%02X:%02X:%02X:%02X:%02X:%02X  %-7s %3s %3d %3s %4s %4s %4dM %4d\n", me->Addr[0], me->Addr[1], me->Addr[2], me->Addr[3], me->Addr[4], me->Addr[5], GetPhyMode(me->TxRate.field.MODE), GetBW(me->TxRate.field.BW), getMCS(me->TxRate), me->TxRate.field.ShortGI ? "YES" : "NO", me->TxRate.field.ldpc ? "YES" : "NO", me->TxRate.field.STBC ? "YES" : "NO", getRate(me->TxRate), rssi);

	return ret;
}

static int print_sta_list(webs_t wp, RT_802_11_MAC_TABLE *mp, int num_ss_rx, int ap_idx) {
	int ret, i, hr, min, sec, rssi;

	ret = 0;

	ret += websWrite(wp, "\nAP %s Stations List\n", (ap_idx == 0) ? "Main" : "Guest");
	ret += websWrite(wp, "----------------------------------------\n");
	ret += websWrite(wp, "        %-19s%-8s%-4s%-4s%-4s%-5s%-5s%-6s%-5s%-4s%-12s\n", "MAC", "PhyMode", " BW", "MCS", "SGI", "LDPC", "STBC", "TRate", "RSSI", "PSM", "Connect Time");

	for (i = 0; i < mp->Num; i++) {
		if ((int)mp->Entry[i].ApIdx != ap_idx)
			continue;

		hr = mp->Entry[i].ConnectedTime / 3600;
		min = (mp->Entry[i].ConnectedTime % 3600) / 60;
		sec = mp->Entry[i].ConnectedTime - hr * 3600 - min * 60;
		rssi = -127;
		if ((int)mp->Entry[i].AvgRssi0 > rssi && mp->Entry[i].AvgRssi0 != 0)
			rssi = (int)mp->Entry[i].AvgRssi0;
		if (num_ss_rx > 1) {
			if ((int)mp->Entry[i].AvgRssi1 > rssi && mp->Entry[i].AvgRssi1 != 0)
				rssi = (int)mp->Entry[i].AvgRssi1;
		}
		if (num_ss_rx > 2) {
			if ((int)mp->Entry[i].AvgRssi2 > rssi && mp->Entry[i].AvgRssi2 != 0)
				rssi = (int)mp->Entry[i].AvgRssi2;
		}

		ret += websWrite(wp, "CLIENT: %02X:%02X:%02X:%02X:%02X:%02X  %-7s %3s %3d %3s %4s %4s %4dM %4d "
							 "%3s %02d:%02d:%02d\n",
						 mp->Entry[i].Addr[0], mp->Entry[i].Addr[1], mp->Entry[i].Addr[2], mp->Entry[i].Addr[3], mp->Entry[i].Addr[4], mp->Entry[i].Addr[5], GetPhyMode(mp->Entry[i].TxRate.field.MODE), GetBW(mp->Entry[i].TxRate.field.BW), getMCS(mp->Entry[i].TxRate), mp->Entry[i].TxRate.field.ShortGI ? "YES" : "NO", mp->Entry[i].TxRate.field.ldpc ? "YES" : "NO", mp->Entry[i].TxRate.field.STBC ? "YES" : "NO", getRate(mp->Entry[i].TxRate), rssi, mp->Entry[i].Psm ? "YES" : "NO", hr, min, sec);
	}

	ret += websWrite(wp, "----------------------------------------\n");

	return ret;
}

#if defined(USE_RT3352_MII)
static int getRate_inic(MACHTTRANSMIT_SETTING_INIC HTSetting) {
	int rate_count = sizeof(MCSMappingRateTable) / sizeof(int);
	int rate_index = 0;

	if (HTSetting.field.MODE >= MODE_HTMIX)
		rate_index = 12 + ((unsigned char)HTSetting.field.BW * 24) + ((unsigned char)HTSetting.field.ShortGI * 48) + ((unsigned char)HTSetting.field.MCS);
	else if (HTSetting.field.MODE == MODE_OFDM)
		rate_index = (unsigned char)(HTSetting.field.MCS) + 4;
	else if (HTSetting.field.MODE == MODE_CCK)
		rate_index = (unsigned char)(HTSetting.field.MCS);

	if (rate_index < 0)
		rate_index = 0;

	if (rate_index >= rate_count)
		rate_index = rate_count - 1;

	return (MCSMappingRateTable[rate_index] * 5) / 10;
}

static int print_sta_list_inic(webs_t wp, RT_802_11_MAC_TABLE_INIC *mp, int num_ss_rx, int ap_idx) {
	int ret, i, hr, min, sec, rssi;

	ret = 0;

	ret += websWrite(wp, "\nAP %s Stations List\n", (ap_idx == 0) ? "Main" : "Guest");
	ret += websWrite(wp, "----------------------------------------\n");
	ret += websWrite(wp, "        %-19s%-8s%-4s%-4s%-4s%-5s%-5s%-6s%-5s%-4s%-12s\n", "MAC", "PhyMode", " BW", "MCS", "SGI", "LDPC", "STBC", "TRate", "RSSI", "PSM", "Connect Time");

	for (i = 0; i < mp->Num; i++) {
		if ((int)mp->Entry[i].ApIdx != ap_idx)
			continue;

		hr = mp->Entry[i].ConnectedTime / 3600;
		min = (mp->Entry[i].ConnectedTime % 3600) / 60;
		sec = mp->Entry[i].ConnectedTime - hr * 3600 - min * 60;
		rssi = -127;
		if ((int)mp->Entry[i].AvgRssi0 > rssi && mp->Entry[i].AvgRssi0 != 0)
			rssi = (int)mp->Entry[i].AvgRssi0;
		if (num_ss_rx > 1) {
			if ((int)mp->Entry[i].AvgRssi1 > rssi && mp->Entry[i].AvgRssi1 != 0)
				rssi = (int)mp->Entry[i].AvgRssi1;
		}

		ret += websWrite(wp, "CLIENT: %02X:%02X:%02X:%02X:%02X:%02X  %-7s %3s %3d %3s %4s %4s %4dM %4d "
							 "%3s %02d:%02d:%02d\n",
						 mp->Entry[i].Addr[0], mp->Entry[i].Addr[1], mp->Entry[i].Addr[2], mp->Entry[i].Addr[3], mp->Entry[i].Addr[4], mp->Entry[i].Addr[5], GetPhyMode(mp->Entry[i].TxRate.field.MODE), GetBW(mp->Entry[i].TxRate.field.BW), mp->Entry[i].TxRate.field.MCS, mp->Entry[i].TxRate.field.ShortGI ? "YES" : "NO", "NO", mp->Entry[i].TxRate.field.STBC ? "YES" : "NO", getRate_inic(mp->Entry[i].TxRate), rssi, mp->Entry[i].Psm ? "YES" : "NO", hr, min, sec);
	}

	return ret;
}

static int print_mac_table_inic(webs_t wp, const char *wif_name, int num_ss_rx, int is_guest_on) {
	char mac_table_data[4096];
	struct iwreq wrq;
	RT_802_11_MAC_TABLE_INIC *mp;
	int ret = 0;

	bzero(mac_table_data, sizeof(mac_table_data));
	wrq.u.data.pointer = mac_table_data;
	wrq.u.data.length = sizeof(mac_table_data);
	wrq.u.data.flags = 0;

	if (wl_ioctl(wif_name, RTPRIV_IOCTL_GET_MAC_TABLE, &wrq) >= 0) {
		mp = (RT_802_11_MAC_TABLE_INIC *)wrq.u.data.pointer;

		ret += print_sta_list_inic(wp, mp, num_ss_rx, 0);
		if (is_guest_on)
			ret += print_sta_list_inic(wp, mp, num_ss_rx, 1);
	}

	return ret;
}
#endif

static int print_wmode(webs_t wp, unsigned int wmode, unsigned int phy_mode) {
	char buf[16] = {0};
	int ret = 0;

	if (wmode) {
		char *p = buf;
		if (wmode & WMODE_A)
			p += sprintf(p, "/a");
		if (wmode & WMODE_B)
			p += sprintf(p, "/b");
		if (wmode & WMODE_G)
			p += sprintf(p, "/g");
		if (wmode & WMODE_GN)
			p += sprintf(p, "/n");
		if (wmode & WMODE_AN)
			p += sprintf(p, "/n");
		if (wmode & WMODE_AC)
			p += sprintf(p, "/ac");
		if (p != buf)
			ret += websWrite(wp, "WPHY Mode\t: 11%s\n", buf + 1);
	} else {
		switch (phy_mode) {
		case PHY_11BG_MIXED:
			strcpy(buf, "b/g");
			break;
		case PHY_11B:
			strcpy(buf, "b");
			break;
		case PHY_11A:
			strcpy(buf, "a");
			break;
		case PHY_11ABG_MIXED:
			strcpy(buf, "a/b/g");
			break;
		case PHY_11G:
			strcpy(buf, "g");
			break;
		case PHY_11ABGN_MIXED:
			strcpy(buf, "a/b/g/n");
			break;
		case PHY_11N:
		case PHY_11N_5G:
			strcpy(buf, "n");
			break;
		case PHY_11GN_MIXED:
			strcpy(buf, "g/n");
			break;
		case PHY_11AN_MIXED:
			strcpy(buf, "a/n");
			break;
		case PHY_11BGN_MIXED:
			strcpy(buf, "b/g/n");
			break;
		case PHY_11AGN_MIXED:
			strcpy(buf, "a/g/n");
			break;
		}
		if (buf[0])
			ret += websWrite(wp, "WPHY Mode\t: 11%s\n", buf);
	}

	return ret;
}

static int print_mac_table(webs_t wp, const char *wif_name, int num_ss_rx, int is_guest_on) {
	char mac_table_data[4096];
	struct iwreq wrq;
	RT_802_11_MAC_TABLE *mp;
	int ret = 0;

	bzero(mac_table_data, sizeof(mac_table_data));
	wrq.u.data.pointer = mac_table_data;
	wrq.u.data.length = sizeof(mac_table_data);
	wrq.u.data.flags = 0;

	if (wl_ioctl(wif_name, RTPRIV_IOCTL_GET_MAC_TABLE_STRUCT, &wrq) >= 0) {
		mp = (RT_802_11_MAC_TABLE *)wrq.u.data.pointer;

		ret += print_sta_list(wp, mp, num_ss_rx, 0);
		if (is_guest_on)
			ret += print_sta_list(wp, mp, num_ss_rx, 1);
	}

	return ret;
}

static int print_radio_status(webs_t wp, int is_aband) {
	int ret, i, channel, op_mode, ht_mode, is_guest_on, num_ss_rx;
	unsigned int wmode, phy_mode;
	const char *caption, *wif_ap[2], *wif_wds[4], *wif_apcli;
	struct iw_range range;
	double freq;
	char buffer[sizeof(iwrange) * 2];
	unsigned char mac[8];
	struct iwreq wrq0;
	struct iwreq wrq1;
	struct iwreq wrq2;

	if (is_aband)
	{
		websWrite(wp, "PRINT: 5Ghz-Band\n");
#if !BOARD_HAS_5G_RADIO
		websWrite(wp, "Error: board does not support 5G band\n");
#endif
	} else {
		websWrite(wp, "PRINT: 2Ghz-Band\n");
	}

	websWrite(wp, "----------------------------------------\n");

	ret = 0;

#if BOARD_HAS_5G_RADIO
	if (is_aband) {
		if (nvram_match("wl_radio_x", "0")
#if defined(USE_IWPRIV_RADIO_5G)
			|| nvram_match("mlme_radio_wl", "0")
#endif
				) {
			ret += websWrite(wp, "Radio %s is disabled\n", "5GHz");
			return ret;
		}

		wif_ap[0] = IFNAME_5G_MAIN;
		wif_ap[1] = IFNAME_5G_GUEST;
		wif_wds[0] = IFNAME_5G_WDS0;
		wif_wds[1] = IFNAME_5G_WDS1;
		wif_wds[2] = IFNAME_5G_WDS2;
		wif_wds[3] = IFNAME_5G_WDS3;
		wif_apcli = IFNAME_5G_APCLI;
	} else
#endif
	{
		if (nvram_match("rt_radio_x", "0")
#if defined(USE_IWPRIV_RADIO_2G) || defined(USE_RT3352_MII)
			|| nvram_match("mlme_radio_rt", "0")
#endif
				) {
			ret += websWrite(wp, "Radio %s is disabled\n", "2.4GHz");
			return ret;
		}

		wif_ap[0] = IFNAME_2G_MAIN;
		wif_ap[1] = IFNAME_2G_GUEST;
		wif_wds[0] = IFNAME_2G_WDS0;
		wif_wds[1] = IFNAME_2G_WDS1;
		wif_wds[2] = IFNAME_2G_WDS2;
		wif_wds[3] = IFNAME_2G_WDS3;
		wif_apcli = IFNAME_2G_APCLI;
	}

	op_mode = nvram_wlan_get_int(is_aband, "mode_x");
	ht_mode = nvram_wlan_get_int(is_aband, "gmode");
	num_ss_rx = nvram_wlan_get_int(is_aband, "stream_rx");
	is_guest_on = (nvram_wlan_get_int(is_aband, "guest_enable") == 1);

	caption = (op_mode == 1) ? "WDS" : "AP Main";

	if (op_mode != 3) {
		if (wl_ioctl(wif_ap[0], SIOCGIWAP, &wrq0) < 0) {
			ret += websWrite(wp, "Radio is disabled\n");
			return ret;
		}
		wrq0.u.ap_addr.sa_family = ARPHRD_ETHER;
		ret += websWrite(wp, "MAC (%s)\t: %02X:%02X:%02X:%02X:%02X:%02X\n", caption, (unsigned char)wrq0.u.ap_addr.sa_data[0], (unsigned char)wrq0.u.ap_addr.sa_data[1], (unsigned char)wrq0.u.ap_addr.sa_data[2], (unsigned char)wrq0.u.ap_addr.sa_data[3], (unsigned char)wrq0.u.ap_addr.sa_data[4], (unsigned char)wrq0.u.ap_addr.sa_data[5]);
	}

	if (op_mode != 1 && op_mode != 3 && is_guest_on) {
		if (wl_ioctl(wif_ap[1], SIOCGIWAP, &wrq0) >= 0) {
			wrq0.u.ap_addr.sa_family = ARPHRD_ETHER;
			ret += websWrite(wp, "MAC (%s)\t: %02X:%02X:%02X:%02X:%02X:%02X\n", "AP Guest", (unsigned char)wrq0.u.ap_addr.sa_data[0], (unsigned char)wrq0.u.ap_addr.sa_data[1], (unsigned char)wrq0.u.ap_addr.sa_data[2], (unsigned char)wrq0.u.ap_addr.sa_data[3], (unsigned char)wrq0.u.ap_addr.sa_data[4], (unsigned char)wrq0.u.ap_addr.sa_data[5]);
		}
	}

	if (op_mode == 3 || op_mode == 4) {
		if (get_interface_hwaddr(wif_apcli, mac) == 0) {
			ret += websWrite(wp, "MAC (%s)\t: %02X:%02X:%02X:%02X:%02X:%02X\n", "AP-Client", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		}
	}

	if (wl_ioctl(wif_ap[0], SIOCGIWFREQ, &wrq1) < 0)
		return ret;

	bzero(buffer, sizeof(buffer));
	wrq2.u.data.pointer = (caddr_t)buffer;
	wrq2.u.data.length = sizeof(buffer);
	wrq2.u.data.flags = 0;

	if (wl_ioctl(wif_ap[0], SIOCGIWRANGE, &wrq2) < 0)
		return ret;

	if (ralink_get_range_info(&range, buffer, wrq2.u.data.length) < 0)
		return ret;

	wmode = 0;
	phy_mode = 0;

	bzero(buffer, sizeof(unsigned long));
	wrq2.u.data.length = sizeof(unsigned long);
	wrq2.u.data.pointer = (caddr_t)buffer;
	wrq2.u.data.flags = RT_OID_802_11_PHY_MODE;

	if (wl_ioctl(wif_ap[0], RT_PRIV_IOCTL, &wrq2) < 0) {
		wrq2.u.data.flags = RT_OID_GET_PHY_MODE;
		if (wl_ioctl(wif_ap[0], RT_PRIV_IOCTL, &wrq2) < 0) {
			phy_mode = calc_phy_mode(ht_mode, is_aband);
		} else {
			if (wrq2.u.data.length == 1)
				memcpy(&phy_mode, wrq2.u.data.pointer, wrq2.u.data.length);
			else
				phy_mode = wrq2.u.mode;
		}
	} else {
		memcpy(&wmode, wrq2.u.data.pointer, wrq2.u.data.length);
	}

	freq = iw_freq2float(&(wrq1.u.freq));
	if (freq < KILO) {
		channel = (int)freq;
	} else {
		channel = iw_freq_to_channel(freq, &range);
		if (channel < 0)
			channel = 0;
	}

	caption = "Operation Mode";

	if (op_mode == 1)
		ret += websWrite(wp, "%s\t: WDS bridge\n", caption);
	else if (op_mode == 2)
		ret += websWrite(wp, "%s\t: WDS repeater (bridge + AP)\n", caption);
	else if (op_mode == 3)
		ret += websWrite(wp, "%s\t: AP-Client\n", caption);
	else if (op_mode == 4)
		ret += websWrite(wp, "%s\t: AP-Client + AP\n", caption);
	else
		ret += websWrite(wp, "%s\t: AP\n", caption);

	ret += print_wmode(wp, wmode, phy_mode);

	ret += websWrite(wp, "Channel Main\t: %d\n", channel);

	if (op_mode == 3 || op_mode == 4) {
		struct iwreq wrq;

		if (get_apcli_peer_connected(wif_apcli, &wrq)) {
#if defined(USE_RT3352_MII)
			if (!is_aband) {
				ret += websWrite(wp, "STA Connected\t: YES -> [%02X:%02X:%02X:%02X:%02X:%02X]\n", (unsigned char)wrq.u.ap_addr.sa_data[0], (unsigned char)wrq.u.ap_addr.sa_data[1], (unsigned char)wrq.u.ap_addr.sa_data[2], (unsigned char)wrq.u.ap_addr.sa_data[3], (unsigned char)wrq.u.ap_addr.sa_data[4], (unsigned char)wrq.u.ap_addr.sa_data[5]);
			} else
#endif
			{
				RT_802_11_MAC_ENTRY me;

				if (get_apcli_wds_entry(wif_apcli, &me)) {
					ret += print_apcli_wds_header(wp, "\nAP-Client Connection\n");
					ret += print_apcli_wds_entry(wp, &me, num_ss_rx);
				}
			}
		} else {
			ret += websWrite(wp, "STA Connected\t: %s\n", "NO");
		}
	}

	if ((op_mode == 1 || op_mode == 2)
#if defined(USE_RT3352_MII)
		&& (is_aband)
#endif
			) {
		RT_802_11_MAC_ENTRY me;

		ret += print_apcli_wds_header(wp, "\nWDS Peers\n");

		for (i = 0; i < 4; i++) {
			if (get_apcli_wds_entry(wif_wds[i], &me))
				ret += print_apcli_wds_entry(wp, &me, num_ss_rx);
		}
	}

	if (op_mode != 1 && op_mode != 3) {
#if defined(USE_RT3352_MII)
		if (!is_aband)
			ret += print_mac_table_inic(wp, wif_ap[0], num_ss_rx, is_guest_on);
		else
#endif
			ret += print_mac_table(wp, wif_ap[0], num_ss_rx, is_guest_on);
	}

	return ret;
}

#if BOARD_HAS_5G_RADIO
int ej_wl_status_5g(int eid, webs_t wp, int argc, char **argv) { return print_radio_status(wp, 1); }
#endif

int ej_wl_status_2g(int eid, webs_t wp, int argc, char **argv) { return print_radio_status(wp, 0); }

int ej_wl_auth_list(int eid, webs_t wp, int argc, char **argv) {
	struct iwreq wrq;
	int i, firstRow = 1, ret = 0;
	char mac_table_data[4096];
	char mac[18];

#if BOARD_HAS_5G_RADIO
	/* query wl for authenticated sta list */
	memset(mac_table_data, 0, sizeof(mac_table_data));
	wrq.u.data.pointer = mac_table_data;
	wrq.u.data.length = sizeof(mac_table_data);
	wrq.u.data.flags = 0;
	if (wl_ioctl(IFNAME_5G_MAIN, RTPRIV_IOCTL_GET_MAC_TABLE_STRUCT, &wrq) >= 0) {
		RT_802_11_MAC_TABLE *mp = (RT_802_11_MAC_TABLE *)wrq.u.data.pointer;
		for (i = 0; i < mp->Num; i++) {
			sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", mp->Entry[i].Addr[0], mp->Entry[i].Addr[1], mp->Entry[i].Addr[2], mp->Entry[i].Addr[3], mp->Entry[i].Addr[4], mp->Entry[i].Addr[5]);

			if (firstRow)
				firstRow = 0;
			else
				ret += websWrite(wp, ", ");

			ret += websWrite(wp, "\"%s\"", mac);
		}
	}
#endif

#if defined(USE_RT3352_MII)
	if (nvram_get_int("inic_disable") == 1)
		return ret;

	/* query rt for authenticated sta list */
	memset(mac_table_data, 0, sizeof(mac_table_data));
	wrq.u.data.pointer = mac_table_data;
	wrq.u.data.length = sizeof(mac_table_data);
	wrq.u.data.flags = 0;
	if (wl_ioctl(IFNAME_2G_MAIN, RTPRIV_IOCTL_GET_MAC_TABLE, &wrq) >= 0) {
		RT_802_11_MAC_TABLE_INIC *mp = (RT_802_11_MAC_TABLE_INIC *)wrq.u.data.pointer;
		for (i = 0; i < mp->Num; i++) {
			sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", mp->Entry[i].Addr[0], mp->Entry[i].Addr[1], mp->Entry[i].Addr[2], mp->Entry[i].Addr[3], mp->Entry[i].Addr[4], mp->Entry[i].Addr[5]);

			if (firstRow)
				firstRow = 0;
			else
				ret += websWrite(wp, ", ");

			ret += websWrite(wp, "\"%s\"", mac);
		}
	}
#else
	/* query rt for authenticated sta list */
	memset(mac_table_data, 0, sizeof(mac_table_data));
	wrq.u.data.pointer = mac_table_data;
	wrq.u.data.length = sizeof(mac_table_data);
	wrq.u.data.flags = 0;
	if (wl_ioctl(IFNAME_2G_MAIN, RTPRIV_IOCTL_GET_MAC_TABLE_STRUCT, &wrq) >= 0) {
		RT_802_11_MAC_TABLE *mp = (RT_802_11_MAC_TABLE *)wrq.u.data.pointer;
		for (i = 0; i < mp->Num; i++) {
			sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", mp->Entry[i].Addr[0], mp->Entry[i].Addr[1], mp->Entry[i].Addr[2], mp->Entry[i].Addr[3], mp->Entry[i].Addr[4], mp->Entry[i].Addr[5]);

			if (firstRow)
				firstRow = 0;
			else
				ret += websWrite(wp, ", ");

			ret += websWrite(wp, "\"%s\"", mac);
		}
	}
#endif

	return ret;
}

#define SSURV_LINE_LEN (4 + 33 + 20 + 23 + 9 + 7 + 7 + 3)			  // Channel+SSID+Bssid+Security+Signal+WiressMode+ExtCh+NetworkType
#define SSURV_LINE_LEN_WPS (4 + 33 + 20 + 23 + 9 + 7 + 7 + 3 + 4 + 5) // Channel+SSID+Bssid+Security+Signal+WiressMode+ExtCh+NetworkType+WPS+PIN

#if BOARD_HAS_5G_RADIO
int ej_wl_scan_5g(int eid, webs_t wp, int argc, char **argv) {
	int retval = 0;
	int apCount = 0;
	char data[8192];
	char ssid_str[128];
#if defined(USE_WSC_WPS)
	char site_line[SSURV_LINE_LEN_WPS + 1];
#else
	char site_line[SSURV_LINE_LEN + 1];
#endif
	char site_chnl[4];
	char site_ssid[34];
	char site_bssid[24];
	char site_signal[10];
	struct iwreq wrq;
	char *sp, *op, *empty;
	int len, line_len;

	empty = "[\"\", \"\", \"\", \"\"]";

	memset(data, 0, 32);
	strcpy(data, "SiteSurvey=1");
	wrq.u.data.length = strlen(data) + 1;
	wrq.u.data.pointer = data;
	wrq.u.data.flags = 0;

	if (wl_ioctl(IFNAME_5G_MAIN, RTPRIV_IOCTL_SET, &wrq) < 0) {
		dbg("Site Survey fails\n");
		return websWrite(wp, "[%s]", empty);
	}

	sleep(5);

	memset(data, 0, sizeof(data));
	wrq.u.data.length = sizeof(data);
	wrq.u.data.pointer = data;
	wrq.u.data.flags = 0;
	if (wl_ioctl(IFNAME_5G_MAIN, RTPRIV_IOCTL_GSITESURVEY, &wrq) < 0) {
		dbg("errors in getting site survey result\n");
		return websWrite(wp, "[%s]", empty);
	}

#if defined(USE_WSC_WPS)
	line_len = SSURV_LINE_LEN_WPS;
//	dbg("%-4s%-33s%-20s%-23s%-9s%-7s%-7s%-3s%-4s%-5s\n", "Ch", "SSID",
//"BSSID", "Security", "Signal(%)", "W-Mode", " ExtCH", "NT", "WPS", "DPID");
#else
	line_len = SSURV_LINE_LEN;
//	dbg("%-4s%-33s%-20s%-23s%-9s%-7s%-7s%-3s\n", "Ch", "SSID", "BSSID",
//"Security", "Signal(%)", "W-Mode", " ExtCH", "NT");
#endif

	retval += websWrite(wp, "[");
	if (wrq.u.data.length > 0) {
		op = sp = wrq.u.data.pointer + line_len + 2; // skip \n+\n
		len = strlen(op);

		while (*sp && ((len - (sp - op)) >= 0)) {
			memcpy(site_line, sp, line_len);

			memcpy(site_chnl, sp, 3);
			memcpy(site_ssid, sp + 4, 33);
			memcpy(site_bssid, sp + 37, 20);
			memcpy(site_signal, sp + 80, 9);

			site_line[line_len] = '\0';
			site_chnl[3] = '\0';
			site_ssid[33] = '\0';
			site_bssid[20] = '\0';
			site_signal[9] = '\0';

			memset(ssid_str, 0, sizeof(ssid_str));
			char_to_ascii(ssid_str, trim_r(site_ssid));

			if (!strlen(ssid_str))
				strcpy(ssid_str, "???");

			if (apCount)
				retval += websWrite(wp, "%s ", ",");

			retval += websWrite(wp, "[\"%s\", \"%s\", \"%s\", \"%s\"]", ssid_str, trim_r(site_bssid), trim_r(site_chnl), trim_r(site_signal));

			//			dbg("%s\n", site_line);

			sp += line_len + 1; // skip \n
			apCount++;
		}
	}

	if (apCount < 1) {
		retval += websWrite(wp, empty);
	}

	retval += websWrite(wp, "]");

	return retval;
}
#endif

int ej_wl_scan_2g(int eid, webs_t wp, int argc, char **argv) {
	int retval = 0, apCount = 0;
	char data[8192];
	char ssid_str[128];
#if defined(USE_WSC_WPS) || defined(USE_RT3352_MII)
	char site_line[SSURV_LINE_LEN_WPS + 1];
#else
	char site_line[SSURV_LINE_LEN + 1];
#endif
	char site_chnl[4];
	char site_ssid[34];
	char site_bssid[24];
	char site_signal[10];
	struct iwreq wrq;
	char *sp, *op, *empty;
	int len, line_len;

	empty = "[\"\", \"\", \"\", \"\"]";

	memset(data, 0, 32);
	strcpy(data, "SiteSurvey=1");
	wrq.u.data.length = strlen(data) + 1;
	wrq.u.data.pointer = data;
	wrq.u.data.flags = 0;

	if (wl_ioctl(IFNAME_2G_MAIN, RTPRIV_IOCTL_SET, &wrq) < 0) {
		dbg("Site Survey fails\n");
		return websWrite(wp, "[%s]", empty);
	}

	sleep(5);

	memset(data, 0, sizeof(data));
	wrq.u.data.length = sizeof(data);
	wrq.u.data.pointer = data;
	wrq.u.data.flags = 0;
	if (wl_ioctl(IFNAME_2G_MAIN, RTPRIV_IOCTL_GSITESURVEY, &wrq) < 0) {
		dbg("errors in getting site survey result\n");
		return websWrite(wp, "[%s]", empty);
	}

#if defined(USE_WSC_WPS) || defined(USE_RT3352_MII)
	line_len = SSURV_LINE_LEN_WPS;
//	dbg("%-4s%-33s%-20s%-23s%-9s%-7s%-7s%-3s%-4s%-5s\n", "Ch", "SSID",
//"BSSID", "Security", "Signal(%)", "W-Mode", " ExtCH", "NT", "WPS", "DPID");
#else
	line_len = SSURV_LINE_LEN;
//	dbg("%-4s%-33s%-20s%-23s%-9s%-7s%-7s%-3s\n", "Ch", "SSID", "BSSID",
//"Security", "Signal(%)", "W-Mode", " ExtCH", "NT");
#endif
	retval += websWrite(wp, "[");
	if (wrq.u.data.length > 0) {
		op = sp = wrq.u.data.pointer + line_len + 2; // skip \n+\n
		len = strlen(op);

		while (*sp && ((len - (sp - op)) >= 0)) {
			memcpy(site_line, sp, line_len);

			memcpy(site_chnl, sp, 3);
			memcpy(site_ssid, sp + 4, 33);
			memcpy(site_bssid, sp + 37, 20);
			memcpy(site_signal, sp + 80, 9);

			site_line[line_len] = '\0';
			site_chnl[3] = '\0';
			site_ssid[33] = '\0';
			site_bssid[20] = '\0';
			site_signal[9] = '\0';

			memset(ssid_str, 0, sizeof(ssid_str));
			char_to_ascii(ssid_str, trim_r(site_ssid));

			if (!strlen(ssid_str))
				strcpy(ssid_str, "???");

			if (apCount)
				retval += websWrite(wp, "%s ", ",");

			retval += websWrite(wp, "[\"%s\", \"%s\", \"%s\", \"%s\"]", ssid_str, trim_r(site_bssid), trim_r(site_chnl), trim_r(site_signal));

			//			dbg("%s\n", site_line);

			sp += line_len + 1; // skip \n
			apCount++;
		}
	}

	if (apCount < 1) {
		retval += websWrite(wp, empty);
	}

	retval += websWrite(wp, "]");

	return retval;
}

#if BOARD_HAS_5G_RADIO
int ej_wl_bssid_5g(int eid, webs_t wp, int argc, char **argv) {
	char bssid[32] = {0};
	unsigned char mac[8];

	snprintf(bssid, sizeof(bssid), "%s", nvram_safe_get("wl_macaddr"));
	if (get_interface_hwaddr(IFNAME_5G_MAIN, mac) == 0) {
		sprintf(bssid, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}

	websWrite(wp, "function get_bssid_ra0() { return '%s';}\n", bssid);

	return 0;
}
#endif

int ej_wl_bssid_2g(int eid, webs_t wp, int argc, char **argv) {
	char bssid[32] = {0};
	unsigned char mac[8];

	snprintf(bssid, sizeof(bssid), "%s", nvram_safe_get("rt_macaddr"));

#if defined(USE_RT3352_MII)
	if (nvram_get_int("inic_disable") != 1) {
#endif
		if (get_interface_hwaddr(IFNAME_2G_MAIN, mac) == 0) {
			sprintf(bssid, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		}
#if defined(USE_RT3352_MII)
	}
#endif

	websWrite(wp, "function get_bssid_rai0() { return '%s';}\n", bssid);

	return 0;
}

void char_to_ascii(char *output, char *input) {
	int i;
	char tmp[10];
	char *ptr;

	ptr = output;

	for (i = 0; i < strlen(input); i++) {
		if ((input[i] >= '0' && input[i] <= '9') || (input[i] >= 'A' && input[i] <= 'Z') || (input[i] >= 'a' && input[i] <= 'z') || input[i] == '!' || input[i] == '*' || input[i] == '(' || input[i] == ')' || input[i] == '_' || input[i] == '-' || input[i] == '\'' || input[i] == '.') {
			*ptr = input[i];
			ptr++;
		} else {
			sprintf(tmp, "%%%.02X", input[i]);
			strcpy(ptr, tmp);
			ptr += 3;
		}
	}

	*ptr = '\0';
}

void help() {
	fprintf(stderr, "Usage: \n");
	fprintf(stderr, "-h : this help\n");
	fprintf(stderr, "-a : print 5Ghz-band radio only\n");
	fprintf(stderr, "-n : print 2Ghz-band radio only\n");
	fprintf(stderr, "-x : print both bands\n");
	exit(0);
}

int main(int argc, char **argv) {
	int c;

	while ((c = getopt(argc, argv, "hanx")) != -1) {
		switch (c) {
		case 'h':
			// Print help
			help();
			return 0;
		case 'n':
			print_radio_status(stdout, 0);
			break;
		case 'a':
			print_radio_status(stdout, 1);
			break;
		case 'x':
			print_radio_status(stdout, 0);
			print_radio_status(stdout, 1);
			break;

		default:
			help();
			abort();
		}
	}
	return 0;
}
