// SPDX-License-Identifier: MIT
//
// Copyright (C) 2005-2006 Stephen Stair - sgstair@akkit.org - http://www.akkit.org
// Copyright (C) 2025 Antonio Niño Díaz

#include "arm7/wifi_arm7.h"

static void wifiAddressHandler(void *address, void *userdata)
{
    (void)userdata;

    irqEnable(IRQ_WIFI);
    Wifi_Init(address);
}

static void wifiValue32Handler(u32 value, void *data)
{
    (void)data;

    switch (value)
    {
        case WIFI_DISABLE:
            irqDisable(IRQ_WIFI);
            break;
        case WIFI_ENABLE:
            irqEnable(IRQ_WIFI);
            break;
        case WIFI_SYNC:
            Wifi_Update();
            break;
        default:
            break;
    }
}

// callback to allow wifi library to notify arm9
static void arm7_synctoarm9(void)
{
    fifoSendValue32(FIFO_DSWIFI, WIFI_SYNC);
}

void installWifiFIFO(void)
{
    irqSet(IRQ_WIFI, Wifi_Interrupt);     // set up wifi interrupt
    Wifi_SetSyncHandler(arm7_synctoarm9); // allow wifi lib to notify arm9
    fifoSetValue32Handler(FIFO_DSWIFI, wifiValue32Handler, 0);
    fifoSetAddressHandler(FIFO_DSWIFI, wifiAddressHandler, 0);
}
