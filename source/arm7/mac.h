// SPDX-License-Identifier: MIT
//
// Copyright (C) 2005-2006 Stephen Stair - sgstair@akkit.org - http://www.akkit.org
// Copyright (C) 2025 Antonio Niño Díaz

#ifndef DSWIFI_ARM7_MAC_H__
#define DSWIFI_ARM7_MAC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <nds/ndstypes.h>

void Wifi_MacInit(void);

u16 Wifi_MACReadHWord(u32 MAC_Base, u32 MAC_Offset);

void Wifi_MACRead(u16 *dest, u32 MAC_Base, u32 MAC_Offset, u32 length);

// This function writes data to MAC RAM, and it is meant to be used only to
// write data to the TX buffer.
//
// The TX process doesn't involve a circular buffer. DSWiFi writes everything to
// the start of the TX buffer and waits until it is sent to write new data and
// send it.
void Wifi_MACWrite(u16 *src, u32 MAC_Base, u32 length);

#ifdef __cplusplus
};
#endif

#endif // DSWIFI_ARM7_MAC_H__
