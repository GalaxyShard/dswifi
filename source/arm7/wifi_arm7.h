// SPDX-License-Identifier: MIT
//
// Copyright (C) 2005-2006 Stephen Stair - sgstair@akkit.org - http://www.akkit.org
// Copyright (C) 2025 Antonio Niño Díaz

// ARM7 wifi interface header

#ifndef DSWIFI_ARM7_WIFI_ARM7_H__
#define DSWIFI_ARM7_WIFI_ARM7_H__

#ifndef ARM7
#    error Wifi is only accessible from the ARM7
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <nds.h>

#include "arm7/wifi_registers.h"
#include "common/wifi_shared.h"

extern volatile Wifi_MainStruct *WifiData;

// keepalive updated in the update handler, which should be called in vblank
// keepalive set for 2 minutes.
#define WIFI_KEEPALIVE_COUNT (60 * 60 * 2)

// Wifi Sync Handler function: Callback function that is called when the arm9 needs to be told to
// synchronize with new fifo data. If this callback is used (see Wifi_SetSyncHandler()), it should
// send a message via the fifo to the arm9, which will call Wifi_Sync() on arm9.
typedef void (*WifiSyncHandler)(void);

void Wifi_WakeUp(void);
void Wifi_Shutdown(void);
void Wifi_Interrupt(void);
void Wifi_Update(void);

void Wifi_CopyMacAddr(volatile void *dest, volatile void *src);
int Wifi_CmpMacAddr(volatile void *mac1, volatile void *mac2);

void Wifi_Init(void *WifiData);
void Wifi_Deinit(void);
void Wifi_Start(void);
void Wifi_Stop(void);
void Wifi_SetWepKey(void *wepkey);
void Wifi_SetWepMode(int wepmode);
void Wifi_SetMode(int wifimode);
void Wifi_SetPreambleType(int preamble_type);
void Wifi_TxSetup(void);
void Wifi_RxSetup(void);
void Wifi_DisableTempPowerSave(void);

int Wifi_SendOpenSystemAuthPacket(void);
int Wifi_SendAssocPacket(void);
int Wifi_SendNullFrame(void);
int Wifi_SendPSPollFrame(void);
int Wifi_ProcessReceivedFrame(int macbase, int framelen);

void Wifi_SetSyncHandler(WifiSyncHandler sh);

#ifdef __cplusplus
};
#endif

#endif // DSWIFI_ARM7_WIFI_ARM7_H__
