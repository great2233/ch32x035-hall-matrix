/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_desc.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/04/06
 * Description        : usb device descriptor,configuration descriptor,
 *                      string descriptors and other descriptors.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "usb_desc.h"

const uint8_t  MyDevDescr[] =
{
    0x12, 0x01, 0x10, 0x01, 0x02, 0x00, 0x00, DEF_USBD_UEP0_SIZE,
    (uint8_t)DEF_USB_VID, (uint8_t)(DEF_USB_VID >> 8),
    (uint8_t)DEF_USB_PID, (uint8_t)(DEF_USB_PID >> 8),
    DEF_IC_PRG_VER, 0x00, 0x01, 0x02, 0x00, 0x01,
};

const uint8_t  MyCfgDescr[] =
{
    0x09, 0x02, 0x43, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,

    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,

    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x00, 0x01,

    0x07, 0x05, 0x81, 0x03, (uint8_t)DEF_USBD_ENDP1_SIZE, (uint8_t)(DEF_USBD_ENDP1_SIZE >> 8), 0x01,

    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,

    0x07, 0x05, 0x02, 0x02, (uint8_t)DEF_USBD_ENDP2_SIZE, (uint8_t)(DEF_USBD_ENDP2_SIZE >> 8), 0x00,
    0x07, 0x05, 0x83, 0x02, (uint8_t)DEF_USBD_ENDP3_SIZE, (uint8_t)(DEF_USBD_ENDP3_SIZE >> 8), 0x00,
};

const uint8_t  MyLangDescr[] = { 0x04, 0x03, 0x09, 0x04 };

const uint8_t  MyManuInfo[] = { 0x0E, 0x03, 'w', 0, 'c', 0, 'h', 0, '.', 0, 'c', 0, 'n', 0 };

const uint8_t  MyProdInfo[] =
{
    0x22, 0x03, 'C', 0x00, 'H', 0x00, '3', 0x00, '2', 0x00, 'X', 0x00,
    '0', 0x00, '3', 0x00, '5', 0x00, ' ', 0x00, 'U', 0x00, 'S', 0x00,
    'B', 0x00, ' ', 0x00, 'C', 0x00, 'D', 0x00, 'C', 0x00
};

const uint8_t  MySerNumInfo[] =
{
    0x16, 0x03, '0', 0, '1', 0, '2', 0, '3', 0, '4', 0,
    '5', 0, '6', 0, '7', 0, '8', 0, '9', 0
};
