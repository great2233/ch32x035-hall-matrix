#ifndef __CH32X035_USBFS_DEVICE_H_
#define __CH32X035_USBFS_DEVICE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "debug.h"
#include "string.h"
#include "usb_desc.h"
#include <ch32x035_usb.h>

#define DEF_UEP_IN                    0x80
#define DEF_UEP_OUT                   0x00
#define DEF_UEP0                      0x00
#define DEF_UEP1                      0x01
#define DEF_UEP2                      0x02
#define DEF_UEP3                      0x03
#define DEF_UEP4                      0x04
#define DEF_UEP5                      0x05
#define DEF_UEP6                      0x06
#define DEF_UEP7                      0x07
#define DEF_UEP_NUM                   8

#define USBFSD_UEP_RX_EN            0x08
#define USBFSD_UEP_TX_EN            0x04
#define USBFSD_UEP_BUF_MOD          0x01
#define DEF_UEP_DMA_LOAD            0
#define DEF_UEP_CPY_LOAD            1

#define pUSBFS_SetupReqPak           ((PUSB_SETUP_REQ)USBFS_EP0_4Buf)

#define USB_IOEN                    0x00000080
#define USB_PHY_V33                 0x00000040
#define UDP_PUE_MASK                0x0000000C
#define UDP_PUE_DISABLE             0x00000000
#define UDP_PUE_35UA                0x00000004
#define UDP_PUE_10K                 0x00000008
#define UDP_PUE_1K5                 0x0000000C

#define UDM_PUE_MASK                0x00000003
#define UDM_PUE_DISABLE             0x00000000
#define UDM_PUE_35UA                0x00000001
#define UDM_PUE_10K                 0x00000002
#define UDM_PUE_1K5                 0x00000003

/* CDC buffer */
#define CDC_RX_PACK_NUM             8
#define CDC_RX_PACK_SIZE            64
#define CDC_RX_BUF_LEN              (CDC_RX_PACK_NUM * CDC_RX_PACK_SIZE)

typedef struct __attribute__((packed)) _CDC_CTL
{
    volatile uint16_t Rx_LoadPtr;
    volatile uint16_t Rx_DealPtr;
    volatile uint16_t Rx_RemainLen;
    volatile uint16_t Rx_PackLen[CDC_RX_PACK_NUM];

    uint8_t  USB_Up_IngFlag;
    uint16_t USB_Up_TimeOut;
    uint8_t  USB_Up_Pack0_Flag;

    uint8_t  Com_Cfg[8];
    uint8_t  Rx_TimeOut;
    uint8_t  Rx_TimeOutMax;
}CDC_CTL;

extern const uint8_t  *pUSBFS_Descr;
extern volatile uint8_t  USBFS_SetupReqCode;
extern volatile uint8_t  USBFS_SetupReqType;
extern volatile uint16_t USBFS_SetupReqValue;
extern volatile uint16_t USBFS_SetupReqIndex;
extern volatile uint16_t USBFS_SetupReqLen;

extern volatile uint8_t  USBFS_DevConfig;
extern volatile uint8_t  USBFS_DevAddr;
extern volatile uint8_t  USBFS_DevSleepStatus;
extern volatile uint8_t  USBFS_DevEnumStatus;

extern __attribute__ ((aligned(4))) uint8_t USBFS_EP0_4Buf[];
extern __attribute__ ((aligned(4))) uint8_t USBFS_EP1_Buf[];
extern __attribute__ ((aligned(4))) uint8_t USBFS_EP2_Buf[];
extern __attribute__ ((aligned(4))) uint8_t USBFS_EP3_Buf[];
extern volatile uint8_t  USBFS_Endp_Busy[];

extern CDC_CTL cdc;
extern __attribute__ ((aligned(4))) uint8_t CDC_Rx_Buf[CDC_RX_BUF_LEN];

extern void USBFS_Device_Init(FunctionalState sta, PWR_VDD VDD_Voltage);
extern void USBFS_Device_Endp_Init(void);
extern void USBFS_RCC_Init(void);
extern void USBFS_Sleep_Wakeup_Operate(void);
extern uint8_t USBFS_Endp_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len, uint8_t mod);
extern void CDC_Init(void);
extern int __debug_cdc_write(const char *buf, int size);

#ifdef __cplusplus
}
#endif

#endif
