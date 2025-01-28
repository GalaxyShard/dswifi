// SPDX-License-Identifier: MIT
//
// Copyright (C) 2005-2006 Stephen Stair - sgstair@akkit.org - http://www.akkit.org

// DS Wifi interface code
// ARM9 wifi support code

#include <netinet/in.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/socket.h>

#include <nds.h>

#include "arm9/access_point.h"
#include "arm9/ipc.h"
#include "arm9/rx_tx_queue.h"
#include "arm9/wifi_arm9.h"
#include "common/common_defs.h"
#include "common/ieee_defs.h"
#include "common/spinlock.h"

#ifdef WIFI_USE_TCP_SGIP

#    include "arm9/heap.h"
#    include "arm9/sgIP/sgIP.h"

sgIP_Hub_HWInterface *wifi_hw;

const char *ASSOCSTATUS_STRINGS[] = {
    [ASSOCSTATUS_DISCONNECTED] = "ASSOCSTATUS_DISCONNECTED",
    [ASSOCSTATUS_SEARCHING] = "ASSOCSTATUS_SEARCHING",
    [ASSOCSTATUS_AUTHENTICATING] = "ASSOCSTATUS_AUTHENTICATING",
    [ASSOCSTATUS_ASSOCIATING] = "ASSOCSTATUS_ASSOCIATING",
    [ASSOCSTATUS_ACQUIRINGDHCP] = "ASSOCSTATUS_ACQUIRINGDHCP",
    [ASSOCSTATUS_ASSOCIATED] = "ASSOCSTATUS_ASSOCIATED",
    [ASSOCSTATUS_CANNOTCONNECT] = "ASSOCSTATUS_CANNOTCONNECT",
};

// This function is used in socket handling code when the user has selected
// blocking mode. They are called after every retry to give interrupts a chance
// to happen (interrupts are disabled in critical sections).
void sgIP_IntrWaitEvent(void)
{
    swiDelay(20000);
}

#ifdef SGIP_DEBUG
// Function that prints an ethernet header (and prefixes it with the character
// passed in the first argument)
static void ethhdr_print(char f, void *d)
{
    char buffer[33];
    int i;
    int t, c;
    buffer[0]  = f;
    buffer[1]  = ':';
    buffer[14] = ' ';
    buffer[27] = ' ';
    buffer[32] = 0;
    for (i = 0; i < 6; i++)
    {
        t = ((u8 *)d)[i];
        c = t & 15;
        if (c > 9)
            c += 'A' - 10;
        else
            c += '0';
        buffer[3 + i * 2] = c;
        c                 = (t >> 4) & 15;
        if (c > 9)
            c += 'A' - 10;
        else
            c += '0';
        buffer[2 + i * 2] = c;

        t = ((u8 *)d)[i + 6];
        c = t & 15;
        if (c > 9)
            c += 'A' - 10;
        else
            c += '0';
        buffer[16 + i * 2] = c;
        c                  = (t >> 4) & 15;
        if (c > 9)
            c += 'A' - 10;
        else
            c += '0';
        buffer[15 + i * 2] = c;
    }
    for (i = 0; i < 2; i++)
    {
        t = ((u8 *)d)[i + 12];
        c = t & 15;
        if (c > 9)
            c += 'A' - 10;
        else
            c += '0';
        buffer[29 + i * 2] = c;
        c                  = (t >> 4) & 15;
        if (c > 9)
            c += 'A' - 10;
        else
            c += '0';
        buffer[28 + i * 2] = c;
    }
    SGIP_DEBUG_MESSAGE((buffer));
}
#endif // SGIP_DEBUG

#endif // WIFI_USE_TCP_SGIP

WifiPacketHandler packethandler = 0;

void Wifi_CopyMacAddr(volatile void *dest, volatile void *src)
{
    volatile u16 *d = dest;
    volatile u16 *s = src;

    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
}

void Wifi_RawSetPacketHandler(WifiPacketHandler wphfunc)
{
    packethandler = wphfunc;
}

void Wifi_DisableWifi(void)
{
    WifiData->reqMode = WIFIMODE_DISABLED;
    WifiData->reqReqFlags &= ~WFLAG_REQ_APCONNECT;
}

void Wifi_EnableWifi(void)
{
    WifiData->reqMode = WIFIMODE_NORMAL;
    WifiData->reqReqFlags &= ~WFLAG_REQ_APCONNECT;
}

void Wifi_SetPromiscuousMode(int enable)
{
    if (enable)
        WifiData->reqReqFlags |= WFLAG_REQ_PROMISC;
    else
        WifiData->reqReqFlags &= ~WFLAG_REQ_PROMISC;
}

void Wifi_ScanMode(void)
{
    WifiData->reqMode = WIFIMODE_SCAN;
    WifiData->reqReqFlags &= ~WFLAG_REQ_APCONNECT;
}

void Wifi_SetChannel(int channel)
{
    if (channel < 1 || channel > 13)
        return;
    if (WifiData->reqMode == WIFIMODE_NORMAL || WifiData->reqMode == WIFIMODE_SCAN)
    {
        WifiData->reqChannel = channel;
    }
}

#ifdef WIFI_USE_TCP_SGIP

int Wifi_TransmitFunction(sgIP_Hub_HWInterface *hw, sgIP_memblock *mb)
{
    (void)hw;

    // Convert ethernet frame into wireless frame and transmit it.
    //
    // Ethernet header: 6 byte dest, 6 byte src, 2 byte protocol ID
    //
    // This function assumes individual pbuf len is >=14 bytes, it's pretty
    // likely ;) - also hopes pbuf len is a multiple of 2 :|

    sgIP_Header_Ethernet *eth = (void *)mb->datastart;

    // Max size for the combined headers plus the WEP IV
    u16 framehdr[(HDR_TX_SIZE + HDR_DATA_MAC_SIZE + 4) / sizeof(u16)];

    // Actual size that we are going to use of framehdr
    int framelen = mb->totallength - sizeof(sgIP_Header_Ethernet) + 8
                 + (WifiData->wepmode7 ? 4 : 0); // WEP IV

    if (!(WifiData->flags9 & WFLAG_ARM9_NETUP))
    {
        SGIP_DEBUG_MESSAGE(("Transmit:err_netdown"));
        sgIP_memblock_free(mb);
        return 0; // ?
    }
    if ((framelen + 40) > Wifi_TxBufferBytesAvailable())
    {
        // error, can't send this much!
        SGIP_DEBUG_MESSAGE(("Transmit:err_space"));
        sgIP_memblock_free(mb);
        return 0; //?
    }

#ifdef SGIP_DEBUG
    ethhdr_print('T', mb->datastart);
#endif

    // Copy hardware TX and IEEE headers
    // =================================

    Wifi_TxHeader *tx = (void *)framehdr;
    IEEE_DataFrameHeader *ieee = (void *)(((u8 *)framehdr) + sizeof(Wifi_TxHeader));

    // Hardware TX header
    // ------------------

    // Let the ARM7 fill in the data transfer rate. We will only fill the IEEE
    // frame length later.
    memset(tx, 0, sizeof(Wifi_TxHeader));

    // IEEE 802.11 header
    // ------------------

    int hdrlen = sizeof(Wifi_TxHeader) + sizeof(IEEE_DataFrameHeader);

    if (WifiData->curReqFlags & WFLAG_REQ_APADHOC) // adhoc mode
    {
        ieee->frame_control = TYPE_DATA;
        ieee->duration = 0;
        Wifi_CopyMacAddr(ieee->addr_1, eth->dest_mac);
        Wifi_CopyMacAddr(ieee->addr_2, WifiData->MacAddr);
        Wifi_CopyMacAddr(ieee->addr_3, WifiData->bssid7);
        ieee->seq_ctl = 0;
    }
    else
    {
        ieee->frame_control = FC_TO_DS | TYPE_DATA;
        ieee->duration = 0;
        Wifi_CopyMacAddr(ieee->addr_1, WifiData->bssid7);
        Wifi_CopyMacAddr(ieee->addr_2, WifiData->MacAddr);
        Wifi_CopyMacAddr(ieee->addr_3, eth->dest_mac);
        ieee->seq_ctl = 0;
    }

    if (WifiData->wepmode7)
    {
        ieee->frame_control |= FC_PROTECTED_FRAME;

        // WEP IV, will be filled in if needed on the ARM7 side.
        ((u16 *)ieee->body)[0] = 0;
        ((u16 *)ieee->body)[1] = 0;

        hdrlen += 4;
    }

    tx->tx_length = framelen + hdrlen - HDR_TX_SIZE + 4; // Checksum

    WifiData->stats[WSTAT_TXQUEUEDPACKETS]++;
    WifiData->stats[WSTAT_TXQUEUEDBYTES] += framelen + hdrlen;

    int base = WifiData->txbufOut;

    Wifi_TxBufferWrite(base * 2, hdrlen, framehdr);
    base += hdrlen / 2;
    if (base >= (WIFI_TXBUFFER_SIZE / 2))
        base -= WIFI_TXBUFFER_SIZE / 2;

    // Copy LLC header
    // ===============

    // Re-use the previous struct to generate LLC header
    framehdr[0] = 0xAAAA;
    framehdr[1] = 0x0003;
    framehdr[2] = 0x0000;
    framehdr[3] = eth->protocol; // Frame type

    Wifi_TxBufferWrite(base * 2, 8, framehdr);
    base += 8 / 2;
    if (base >= (WIFI_TXBUFFER_SIZE / 2))
        base -= WIFI_TXBUFFER_SIZE / 2;

    // Copy data
    // =========

    // Save the pointer to the initial block in the list so that we can free it
    // later.
    sgIP_memblock *t = mb;

    // The data may be split into multiple blocks. Only the first one will have
    // an ethernet header. Skip it.
    int writelen = mb->thislength - sizeof(sgIP_Header_Ethernet);
    if (writelen > 0)
    {
        Wifi_TxBufferWrite(base * 2, writelen,
                           mb->datastart + sizeof(sgIP_Header_Ethernet));
        base += (writelen + 1) / 2;
        if (base >= (WIFI_TXBUFFER_SIZE / 2))
            base -= WIFI_TXBUFFER_SIZE / 2;
    }

    // All other blocks have to be copied without any modification.
    while (mb->next)
    {
        mb = mb->next;

        writelen = mb->thislength;
        Wifi_TxBufferWrite(base * 2, writelen, mb->datastart);
        base += (writelen + 1) / 2;
        if (base >= (WIFI_TXBUFFER_SIZE / 2))
            base -= WIFI_TXBUFFER_SIZE / 2;
    }

    if (WifiData->wepmode7)
    {
        // Allocate 4 more bytes for the WEP ICV in the TX buffer. However,
        // don't write anything. We just need to remember to not fill it and to
        // reserve that space so that the hardware can fill it.
        base += 4 / 2;
        if (base >= (WIFI_TXBUFFER_SIZE / 2))
            base -= WIFI_TXBUFFER_SIZE / 2;
    }

    WifiData->txbufOut = base; // Update FIFO out pos, done sending packet.

    sgIP_memblock_free(t); // free packet, as we're the last stop on this chain.

    Wifi_CallSyncHandler();

    return 0;
}

int Wifi_Interface_Init(sgIP_Hub_HWInterface *hw)
{
    hw->MTU       = 2300;
    hw->ipaddr    = (192) | (168 << 8) | (1 << 16) | (151 << 24);
    hw->snmask    = 0x00FFFFFF;
    hw->gateway   = (192) | (168 << 8) | (1 << 16) | (1 << 24);
    hw->dns[0]    = (192) | (168 << 8) | (1 << 16) | (1 << 24);
    hw->hwaddrlen = 6;
    Wifi_CopyMacAddr(hw->hwaddr, WifiData->MacAddr);
    hw->userdata = 0;
    return 0;
}

#endif

void Wifi_Timer(int num_ms)
{
    Wifi_Update();
#ifdef WIFI_USE_TCP_SGIP
    sgIP_Timer(num_ms);
#endif
}

#ifdef WIFI_USE_TCP_SGIP
static void Wifi_sgIpHandlePackage(int base, int len)
{
    // Do sgIP interfacing for RX packets here.

    int hdr_rx_base = base;
    int hdr_ieee_base = hdr_rx_base + HDR_RX_SIZE / 2;

    // Only check packets if they are of non-null data type, and if they are
    // coming from the AP (toDS=0).
    u16 frame_control = Wifi_RxReadHWordOffset(hdr_ieee_base * 2, HDR_MGT_FRAME_CONTROL);
    if (!((frame_control & (FC_TO_DS | FC_TYPE_SUBTYPE_MASK)) == TYPE_DATA))
        return;

    u16 framehdr[(HDR_RX_SIZE + HDR_DATA_MAC_SIZE + 8 + 4) / sizeof(u16)];
    IEEE_DataFrameHeader *ieee = (void *)(((u8 *)framehdr) + sizeof(Wifi_TxHeader));

    Wifi_RxRawReadPacket(base * 2, sizeof(framehdr), framehdr);

    // With toDS=0, regardless of the value of fromDS, Address 1 is RA/DA
    // (Receiver Address / Destination Address), which is the final recipient of
    // the frame. Only accept messages addressed to our MAC address or to all
    // devices.

    const u16 broadcast_address[3] = { 0xFFFF, 0xFFFF, 0xFFFF };

    // ethhdr_print('!', ieee->addr_1);
    if (!(Wifi_CmpMacAddr(ieee->addr_1, WifiData->MacAddr) ||
          Wifi_CmpMacAddr(ieee->addr_1, (void *)&broadcast_address)))
        return;

    // Okay, the frame is addressed to us (or to everyone). Let's parse it.

    int hdrlen;
    int base2 = base; // Index in the circular buffer to the RX header

    // TODO: Delete this code, and leave the comment?
    // if(ieee->frame_control & FC_PROTECTED_FRAME)
    // {
    //     // WEP enabled. When receiving WEP packets, the IV is stripped for us!
    //     // How nice :|
    //     // Add the LLC header size (8) and the WEP IV (4)
    //     base2 += (HDR_RX_SIZE + HDR_DATA_MAC_SIZE + 8 + 4) / 2;
    //     hdrlen = HDR_DATA_MAC_SIZE + 8 + 4;
    //     base2 += [wifi hdr 12byte] + [802 header hdrlen] + [slip hdr 8byte]
    // }
    // else
    // {

    // Index to the start of the data after the LLC header
    base2 += (HDR_RX_SIZE + HDR_DATA_MAC_SIZE + 8) / 2; // LLC header is 8 bit
    hdrlen = HDR_DATA_MAC_SIZE + 8;

    // }

    int llc_base = base + ((HDR_RX_SIZE + HDR_DATA_MAC_SIZE) / 2);

    // Check for LLC/SLIP header...
    if (!((Wifi_RxReadHWordOffset(llc_base * 2, 0) == 0xAAAA)
        && (Wifi_RxReadHWordOffset(llc_base * 2, 2) == 0x0003)
        && (Wifi_RxReadHWordOffset(llc_base * 2, 4) == 0)))
        return;

    // Size in bytes of the actual contents received excluding the IEEE 802.11
    // header and the LLC header.
    size_t datalen = len - hdrlen;

    // The sgIP block will need to contain the ethernet header and the data.
    sgIP_memblock *mb = sgIP_memblock_allocHW(sizeof(sgIP_Header_Ethernet),
                                              datalen);
    if (mb == NULL)
        return;

    sgIP_Header_Ethernet *eth = (void *)mb->datastart;

    if (base2 >= WIFI_RXBUFFER_SIZE / 2)
        base2 -= WIFI_RXBUFFER_SIZE / 2;

    // TODO: Improve this to read correctly in the case that the packet buffer
    // is fragmented

    // This will read all data into the memory block. It's done in halfwords,
    // so it will skip the last byte if the size isn't a multiple of 16 bits.
    Wifi_RxRawReadPacket(base2 * 2, datalen & ~1,
                         mb->datastart + sizeof(sgIP_Header_Ethernet));
    if (len & 1)
    {
        // Read the last byte
        char *p = mb->datastart + sizeof(sgIP_Header_Ethernet) + datalen - 1;
        *p = Wifi_RxReadHWordOffset(base2 * 2, datalen & ~1) & 255;
    }

    Wifi_CopyMacAddr(eth->dest_mac, ieee->addr_1); // Copy destination
    if (Wifi_RxReadHWordOffset(hdr_ieee_base * 2, 0) & FC_FROM_DS)
    {
        // From DS set? Copy src from addr 3.
        Wifi_CopyMacAddr(eth->src_mac, ieee->addr_3);
    }
    else
    {
        // From DS not set? Copy src from addr 2.
        Wifi_CopyMacAddr(eth->src_mac, ieee->addr_2);
    }

    // Assume LLC exists and is 8 bytes. It goes right after the hardware RX
    // header and the IEEE data frame header. We want to read the last halfword.
    eth->protocol = framehdr[(HDR_RX_SIZE + HDR_DATA_MAC_SIZE + 6) / 2];

#ifdef SGIP_DEBUG
    ethhdr_print('R', mb->datastart);
#endif

    // Done generating recieved data packet... now distribute it.
    sgIP_Hub_ReceiveHardwarePacket(wifi_hw, mb);
}
#endif

void Wifi_Update(void)
{
    if (!WifiData)
        return;

#ifdef WIFI_USE_TCP_SGIP

    if (!(WifiData->flags9 & WFLAG_ARM9_ARM7READY))
    {
        if (WifiData->flags7 & WFLAG_ARM7_ACTIVE)
        {
            WifiData->flags9 |= WFLAG_ARM9_ARM7READY;
            // add network interface.
            wifi_hw = sgIP_Hub_AddHardwareInterface(&Wifi_TransmitFunction, &Wifi_Interface_Init);
            sgIP_timems = WifiData->random; // hacky! but it should work just fine :)
        }
    }
    if (WifiData->authlevel != WIFI_AUTHLEVEL_ASSOCIATED && WifiData->flags9 & WFLAG_ARM9_NETUP)
    {
        WifiData->flags9 &= ~WFLAG_ARM9_NETUP;
    }
    else if (WifiData->authlevel == WIFI_AUTHLEVEL_ASSOCIATED
             && !(WifiData->flags9 & WFLAG_ARM9_NETUP))
    {
        WifiData->flags9 |= WFLAG_ARM9_NETUP;
    }

#endif

    // check for received packets, forward to whatever wants them.
    int cnt = 0;
    while (WifiData->rxbufIn != WifiData->rxbufOut)
    {
        int base    = WifiData->rxbufIn;

        int len     = Wifi_RxReadHWordOffset(base * 2, HDR_RX_IEEE_FRAME_SIZE);
        int fulllen = ((len + 3) & (~3)) + HDR_RX_SIZE;

#ifdef WIFI_USE_TCP_SGIP
        Wifi_sgIpHandlePackage(base, len);
#endif

        // check if we have a handler
        if (packethandler)
        {
            int base2 = base + HDR_RX_SIZE / 2;
            if (base2 >= (WIFI_RXBUFFER_SIZE / 2))
                base2 -= (WIFI_RXBUFFER_SIZE / 2);
            (*packethandler)(base2 * 2, len);
        }

        base += fulllen / 2;
        if (base >= (WIFI_RXBUFFER_SIZE / 2))
            base -= (WIFI_RXBUFFER_SIZE / 2);
        WifiData->rxbufIn = base;

        // Exit if we have already handled a lot of packets
        if (cnt++ > 80)
            break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Ip addr get/set functions
#ifdef WIFI_USE_TCP_SGIP

u32 Wifi_GetIP(void)
{
    if (wifi_hw)
        return wifi_hw->ipaddr;
    return 0;
}

struct in_addr Wifi_GetIPInfo(struct in_addr *pGateway, struct in_addr *pSnmask,
                              struct in_addr *pDns1, struct in_addr *pDns2)
{
    struct in_addr ip = { INADDR_NONE };
    if (wifi_hw)
    {
        if (pGateway)
            pGateway->s_addr = wifi_hw->gateway;
        if (pSnmask)
            pSnmask->s_addr = wifi_hw->snmask;
        if (pDns1)
            pDns1->s_addr = wifi_hw->dns[0];
        if (pDns2)
            pDns2->s_addr = wifi_hw->dns[1];

        ip.s_addr = wifi_hw->ipaddr;
    }
    return ip;
}

void Wifi_SetIP(u32 IPaddr, u32 gateway, u32 subnetmask, u32 dns1, u32 dns2)
{
    if (wifi_hw)
    {
        SGIP_DEBUG_MESSAGE(("SetIP%08X %08X %08X", IPaddr, gateway, subnetmask));
        wifi_hw->ipaddr  = IPaddr;
        wifi_hw->gateway = gateway;
        wifi_hw->snmask  = subnetmask;
        wifi_hw->dns[0]  = dns1;
        wifi_hw->dns[1]  = dns2;
        // reset arp cache...
        sgIP_ARP_FlushInterface(wifi_hw);
    }
}

void Wifi_SetDHCP(void)
{
}

#endif
