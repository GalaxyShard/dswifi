// SPDX-License-Identifier: MIT
//
// Copyright (C) 2025 Antonio Niño Díaz

#include <nds.h>

#include "arm9/ipc.h"
#include "arm9/rx_tx_queue.h"
#include "arm9/wifi_arm9.h"
#include "common/common_defs.h"
#include "common/ieee_defs.h"

// Struct with information specific to DSWifi
typedef struct {
    u8 players_max;
    u8 players_current;
} DSWifiExtraData;

// Vendor beacon info (Nintendo Co., Ltd.)
typedef struct {
    u8 oui[3]; // 0x00, 0x09, 0xBF
    u8 oui_type; // 0x00
    u8 stepping_offset[2];
    u8 lcd_video_sync[2];
    u8 fixed_id[4]; // 0x00400001
    u8 game_id[4];
    u8 stream_code[2];
    u8 extra_data_size;
    u8 beacon_type; // 1 = Multicart
    u8 cmd_data_size[2]; // size in halfwords
    u8 reply_data_size[2]; // size in halfwords
    DSWifiExtraData extra_data;
} FieVendorNintendo;

static_assert(sizeof(FieVendorNintendo) == 26);

// Access points created by official games acting as multiplayer hosts have no
// encryption and no BSSID.

int Wifi_BeaconStart(const char *ssid, u32 game_id)
{
    u8 data[512];

    size_t ssid_len = 0;
    if (ssid != NULL)
        ssid_len = strlen(ssid);
    if (ssid_len > 32)
        return -1;

    for (int i = 0; i < sizeof(WifiData->ssid9); i++)
        WifiData->ssid9[i] = ssid[i];
    WifiData->ssid9[0] = ssid_len;
    for (int i = 0; i < ssid_len; i++)
        WifiData->ssid9[1 + i] = ssid[i];

    // Copy hardware TX and IEEE headers
    // =================================

    Wifi_TxHeader *tx = (void *)data;
    IEEE_MgtFrameHeader *ieee = (void *)(data + sizeof(Wifi_TxHeader));
    u8 *body = (void *)(((u8 *)ieee) + sizeof(IEEE_MgtFrameHeader));

    // Hardware TX header
    // ------------------

    memset(tx, 0, sizeof(Wifi_TxHeader));
    tx->tx_rate = WIFI_TRANSFER_RATE_2MBPS; // This is always 2 Mbit/s

    // IEEE 802.11 header
    // ------------------

    ieee->frame_control = TYPE_BEACON;
    ieee->duration = 0;
    u16 broadcast_address[3] = { 0xFFFF, 0xFFFF, 0xFFFF };
    Wifi_CopyMacAddr(ieee->da, broadcast_address);
    Wifi_CopyMacAddr(ieee->sa, WifiData->MacAddr); // SA and BSSID are the same
    Wifi_CopyMacAddr(ieee->bssid, WifiData->MacAddr);
    ieee->seq_ctl = 0;

    // Frame body
    // ----------

    size_t hdr_size = sizeof(Wifi_TxHeader) + sizeof(IEEE_MgtFrameHeader);
    size_t body_size = 0;

    // Timestamp
    for (int i = 0; i < 8; i++)
        *body++ = 0;
    body_size += 8;

    // Beacon interval

    *(u16 *)body = 100; // It is common for the Beacon interval to be set to 100
                        // time units (interval between Beacon transmissions of
                        // approximately 100 ms).
    body += 2;
    body_size += 2;

    // Capability info

    *(u16 *)body = CAPS_ESS; // Official DS games also set CAPS_SHORT_PREAMBLE
    body += 2;
    body_size += 2;

    // SSID

    if (ssid_len > 0)
    {
        *body++ = MGT_FIE_ID_SSID;
        *body++ = ssid_len;
        for (size_t i = 0; i < ssid_len; i++)
            *body++ = ssid[i];
        body_size += 2 + ssid_len;
    }

    // Supported rates

    *body++ = MGT_FIE_ID_SUPPORTED_RATES;
    *body++ = 2;
    *body++ = RATE_MANDATORY | RATE_2_MBPS;
    *body++ = RATE_MANDATORY | RATE_1_MBPS;
    body_size += 4;

    // DS parameter set (WiFi channel)

    *body++ = MGT_FIE_ID_DS_PARAM_SET;
    *body++ = 1;
    *body++ = WifiData->curChannel; // This will be modified by the ARM7
    body_size += 3;

    // TIM

    *body++ = MGT_FIE_ID_TIM;
    *body++ = 6;
    for (size_t i = 0; i < 6; i++)
        *body++ = 0; // This will be filled by the ARM7
    body_size += 8;

    // Vendor (Nintendo)

    *body++ = MGT_FIE_ID_VENDOR;
    *body++ = sizeof(FieVendorNintendo);
    body_size += 2;

    FieVendorNintendo *fie = (void *)body;
    memset(fie, 0, sizeof(FieVendorNintendo));

    fie->oui[0] = 0x00;
    fie->oui[1] = 0x09;
    fie->oui[2] = 0xBF;
    fie->oui_type = 0x00;
    fie->game_id[0] = (game_id >> 24) & 0xFF;
    fie->game_id[1] = (game_id >> 16) & 0xFF;
    fie->game_id[2] = (game_id >> 8) & 0xFF;
    fie->game_id[3] = (game_id >> 0) & 0xFF;
    fie->extra_data_size = sizeof(DSWifiExtraData);
    fie->beacon_type = 1;
    fie->extra_data.players_max = WifiData->curMaxGuests + 1; // Add host
    fie->extra_data.players_current = 1; // Add host. Updated from the ARM7

    body_size += sizeof(FieVendorNintendo);

    // Send frame to the ARM7

    tx->tx_length = sizeof(IEEE_MgtFrameHeader) + body_size + 4; // Checksum

    int base = WifiData->txbufOut;

    Wifi_TxBufferWrite(base * 2, hdr_size + body_size, data);
    base += (hdr_size + body_size + 1) / 2; // Round up to a halfword
    if (base >= (WIFI_TXBUFFER_SIZE / 2))
        base -= WIFI_TXBUFFER_SIZE / 2;

    WifiData->txbufOut = base; // Update FIFO out pos, done sending packet.

    // That's all!

    WifiData->stats[WSTAT_TXQUEUEDPACKETS]++;
    WifiData->stats[WSTAT_TXQUEUEDBYTES] += hdr_size + body_size;

    Wifi_CallSyncHandler();

    return 0;
}
