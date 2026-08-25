/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32x035_usbfs_device.c
 * Author             : WCH
 * Version            : V1.0.1
 * Date               : 2025/03/10
 * Description        : USBFS firmware + CDC direct communication.
 ********************************************************************************/

#include <ch32x035_usbfs_device.h>

const uint8_t  *pUSBFS_Descr;

volatile uint8_t  USBFS_SetupReqCode;
volatile uint8_t  USBFS_SetupReqType;
volatile uint16_t USBFS_SetupReqValue;
volatile uint16_t USBFS_SetupReqIndex;
volatile uint16_t USBFS_SetupReqLen;

volatile uint8_t  USBFS_DevConfig;
volatile uint8_t  USBFS_DevAddr;
volatile uint8_t  USBFS_DevSleepStatus;
volatile uint8_t  USBFS_DevEnumStatus;

__attribute__ ((aligned(4))) uint8_t USBFS_EP0_4Buf[DEF_USBD_UEP0_SIZE];
__attribute__ ((aligned(4))) uint8_t USBFS_EP1_Buf[DEF_USBD_ENDP1_SIZE];
__attribute__ ((aligned(4))) uint8_t USBFS_EP2_Buf[DEF_USBD_ENDP2_SIZE];
__attribute__ ((aligned(4))) uint8_t USBFS_EP3_Buf[DEF_USBD_ENDP3_SIZE];

volatile uint8_t  USBFS_Endp_Busy[DEF_UEP_NUM];

/* CDC control */
CDC_CTL cdc;
__attribute__ ((aligned(4))) uint8_t CDC_Rx_Buf[CDC_RX_BUF_LEN];

void USBFS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      CDC_Init
 */
void CDC_Init(void)
{
    uint8_t i;

    cdc.Rx_LoadPtr = 0;
    cdc.Rx_DealPtr = 0;
    cdc.Rx_RemainLen = 0;
    for(i = 0; i < CDC_RX_PACK_NUM; i++)
    {
        cdc.Rx_PackLen[i] = 0;
    }

    cdc.USB_Up_IngFlag = 0;
    cdc.USB_Up_TimeOut = 0;
    cdc.USB_Up_Pack0_Flag = 0;

    cdc.Com_Cfg[0] = (uint8_t)115200;
    cdc.Com_Cfg[1] = (uint8_t)(115200 >> 8);
    cdc.Com_Cfg[2] = (uint8_t)(115200 >> 16);
    cdc.Com_Cfg[3] = (uint8_t)(115200 >> 24);
    cdc.Com_Cfg[4] = 0;
    cdc.Com_Cfg[5] = 0;
    cdc.Com_Cfg[6] = 8;
    cdc.Com_Cfg[7] = 30;

    cdc.Rx_TimeOut = 0;
    cdc.Rx_TimeOutMax = 30;
}

/*********************************************************************
 * @fn      USBFS_RCC_Init
 */
void USBFS_RCC_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, ENABLE);
}

/*********************************************************************
 * @fn      USBFS_Device_Endp_Init
 */
void USBFS_Device_Endp_Init(void)
{
    USBFSD->UEP4_1_MOD = USBFS_UEP1_TX_EN;
    USBFSD->UEP2_3_MOD = USBFS_UEP2_RX_EN | USBFS_UEP3_TX_EN;

    USBFSD->UEP0_DMA = (uint32_t)USBFS_EP0_4Buf;
    USBFSD->UEP1_DMA = (uint32_t)USBFS_EP1_Buf;
    USBFSD->UEP2_DMA = (uint32_t)(uint8_t *)&CDC_Rx_Buf[0];
    USBFSD->UEP3_DMA = (uint32_t)(uint8_t *)&USBFS_EP3_Buf[0];

    USBFSD->UEP0_CTRL_H = USBFS_UEP_R_RES_ACK | USBFS_UEP_T_RES_NAK;
    USBFSD->UEP2_CTRL_H = USBFS_UEP_R_RES_ACK;

    USBFSD->UEP1_TX_LEN = 0;
    USBFSD->UEP3_TX_LEN = 0;

    USBFSD->UEP1_CTRL_H = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP3_CTRL_H = USBFS_UEP_T_RES_NAK;

    for(uint8_t i = 0; i < DEF_UEP_NUM; i++)
    {
        USBFS_Endp_Busy[i] = 0;
    }
}

/*********************************************************************
 * @fn      GPIO_USB_INIT
 */
void GPIO_USB_INIT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_16;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_17;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

/*********************************************************************
 * @fn      USBFS_Device_Init
 */
void USBFS_Device_Init(FunctionalState sta, PWR_VDD VDD_Voltage)
{
    if(sta)
    {
        GPIO_USB_INIT();
        if(VDD_Voltage == PWR_VDD_5V)
        {
            AFIO->CTLR = (AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK | USB_PHY_V33)) | UDP_PUE_10K | USB_IOEN;
        }
        else
        {
            AFIO->CTLR = (AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK)) | USB_PHY_V33 | UDP_PUE_1K5 | USB_IOEN;
        }
        USBFSD->BASE_CTRL = 0x00;
        USBFS_Device_Endp_Init();
        USBFSD->DEV_ADDR = 0x00;
        USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
        USBFSD->INT_FG = 0xff;
        USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
        USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
        NVIC_EnableIRQ(USBFS_IRQn);
    }
    else
    {
        AFIO->CTLR = AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK | USB_IOEN);
        USBFSD->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
        Delay_Us(10);
        USBFSD->BASE_CTRL = 0x00;
        NVIC_DisableIRQ(USBFS_IRQn);
    }
}

/*********************************************************************
 * @fn      USBFS_Endp_DataUp
 */
uint8_t USBFS_Endp_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len, uint8_t mod)
{
    uint8_t endp_mode;
    uint8_t buf_load_offset;
    uint16_t *uep_tx_len;
    uint16_t *uep_ctrl;
    uint8_t *uep_dma;

    switch(endp)
    {
        case 1: uep_tx_len = (uint16_t *)&USBFSD->UEP1_TX_LEN;
                uep_ctrl = (uint16_t *)&USBFSD->UEP1_CTRL_H;
                uep_dma = (uint8_t *)&USBFSD->UEP1_DMA; break;
        case 2: uep_tx_len = (uint16_t *)&USBFSD->UEP2_TX_LEN;
                uep_ctrl = (uint16_t *)&USBFSD->UEP2_CTRL_H;
                uep_dma = (uint8_t *)&USBFSD->UEP2_DMA; break;
        case 3: uep_tx_len = (uint16_t *)&USBFSD->UEP3_TX_LEN;
                uep_ctrl = (uint16_t *)&USBFSD->UEP3_CTRL_H;
                uep_dma = (uint8_t *)&USBFSD->UEP3_DMA; break;
        case 4: uep_tx_len = (uint16_t *)&USBFSD->UEP4_TX_LEN;
                uep_ctrl = (uint16_t *)&USBFSD->UEP4_CTRL_H;
                uep_dma = (uint8_t *)&USBFSD->UEP0_DMA; break;
        case 5: uep_tx_len = (uint16_t *)&USBFSD->UEP5_TX_LEN;
                uep_ctrl = (uint16_t *)&USBFSD->UEP5_CTRL_H;
                uep_dma = (uint8_t *)&USBFSD->UEP5_DMA; break;
        case 6: uep_tx_len = (uint16_t *)&USBFSD->UEP6_TX_LEN;
                uep_ctrl = (uint16_t *)&USBFSD->UEP6_CTRL_H;
                uep_dma = (uint8_t *)&USBFSD->UEP6_DMA; break;
        case 7: uep_tx_len = (uint16_t *)&USBFSD->UEP7_TX_LEN;
                uep_ctrl = (uint16_t *)&USBFSD->UEP7_CTRL_H;
                uep_dma = (uint8_t *)&USBFSD->UEP7_DMA; break;
        default: break;
    }

    if((endp >= DEF_UEP1) && (endp <= DEF_UEP7))
    {
        if(USBFS_Endp_Busy[endp] == 0)
        {
            if((endp == DEF_UEP1) || (endp == DEF_UEP4))
            {
                endp_mode = USBFSD->UEP4_1_MOD;
                if(endp == DEF_UEP1) endp_mode = (uint8_t)(endp_mode >> 4);
            }
            else if((endp == DEF_UEP2) || (endp == DEF_UEP3))
            {
                endp_mode = USBFSD->UEP2_3_MOD;
                if(endp == DEF_UEP3) endp_mode = (uint8_t)(endp_mode >> 4);
            }
            else
            {
                endp_mode = USBFSD->UEP567_MOD;
                if(endp == DEF_UEP5) endp_mode = (uint8_t)(endp_mode << 2);
                else if(endp == DEF_UEP7) endp_mode = (uint8_t)(endp_mode >> 2);
                endp_mode &= 0xfe;
            }

            if(endp_mode & USBFSD_UEP_TX_EN)
            {
                if(endp_mode & USBFSD_UEP_RX_EN)
                {
                    buf_load_offset = (endp_mode & USBFSD_UEP_BUF_MOD) ?
                        ((*uep_ctrl & USBFS_UEP_T_TOG) ? 192 : 128) : 64;
                }
                else
                {
                    if(endp_mode & USBFSD_UEP_BUF_MOD)
                        buf_load_offset = (*uep_ctrl & USBFS_UEP_T_TOG) ? 64 : 0;
                    else
                        buf_load_offset = 0;
                }
                if(endp == DEF_UEP4) buf_load_offset += 64;

                if(buf_load_offset == 0)
                {
                    if(mod == DEF_UEP_DMA_LOAD)
                        *uep_dma = (uint16_t)(uint32_t)pbuf;
                    else
                        memcpy(((uint8_t *)(*((volatile uint32_t *)(uep_dma))) + 0x20000000), pbuf, len);
                }
                else
                {
                    memcpy(((uint8_t *)(*((volatile uint32_t *)(uep_dma))) + 0x20000000) + buf_load_offset, pbuf, len);
                }
                *uep_tx_len = len;
                *uep_ctrl = (*uep_ctrl & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
                USBFS_Endp_Busy[endp] = 0x01;
            }
            else return 1;
        }
        else return 1;
    }
    else return 1;
    return 0;
}

/*********************************************************************
 * @fn      USBFS_IRQHandler
 */
void USBFS_IRQHandler(void)
{
    uint8_t intflag, intst, errflag;
    uint16_t len;

    intflag = USBFSD->INT_FG;
    intst = USBFSD->INT_ST;

    if(intflag & USBFS_UIF_TRANSFER)
    {
        switch(intst & USBFS_UIS_TOKEN_MASK)
        {
        case USBFS_UIS_TOKEN_IN:
            switch(intst & (USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK))
            {
            case USBFS_UIS_TOKEN_IN | DEF_UEP0:
                if(USBFS_SetupReqLen == 0)
                    USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                if((USBFS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
                { }
                else
                {
                    switch(USBFS_SetupReqCode)
                    {
                    case USB_GET_DESCRIPTOR:
                        len = USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                        memcpy(USBFS_EP0_4Buf, pUSBFS_Descr, len);
                        USBFS_SetupReqLen -= len;
                        pUSBFS_Descr += len;
                        USBFSD->UEP0_TX_LEN = len;
                        USBFSD->UEP0_CTRL_H ^= USBFS_UEP_T_TOG;
                        break;
                    case USB_SET_ADDRESS:
                        USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | USBFS_DevAddr;
                        break;
                    default: break;
                    }
                }
                break;

            case (USBFS_UIS_TOKEN_IN | DEF_UEP1):
                USBFSD->UEP1_CTRL_H ^= USBFS_UEP_T_TOG;
                USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
                USBFS_Endp_Busy[DEF_UEP1] = 0;
                break;

            case (USBFS_UIS_TOKEN_IN | DEF_UEP3):
                USBFSD->UEP3_CTRL_H ^= USBFS_UEP_T_TOG;
                USBFSD->UEP3_CTRL_H = (USBFSD->UEP3_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
                USBFS_Endp_Busy[DEF_UEP3] = 0;
                cdc.USB_Up_IngFlag = 0;
                break;

            default: break;
            }
            break;

        case USBFS_UIS_TOKEN_OUT:
            switch(intst & (USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK))
            {
            case USBFS_UIS_TOKEN_OUT | DEF_UEP0:
                len = USBFSD->RX_LEN;
                if(intst & USBFS_UIS_TOG_OK)
                {
                    if((USBFS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
                    {
                        USBFS_SetupReqLen = 0;
                        if(USBFS_SetupReqCode == CDC_SET_LINE_CODING)
                        {
                            cdc.Com_Cfg[0] = USBFS_EP0_4Buf[0];
                            cdc.Com_Cfg[1] = USBFS_EP0_4Buf[1];
                            cdc.Com_Cfg[2] = USBFS_EP0_4Buf[2];
                            cdc.Com_Cfg[3] = USBFS_EP0_4Buf[3];
                            cdc.Com_Cfg[4] = USBFS_EP0_4Buf[4];
                            cdc.Com_Cfg[5] = USBFS_EP0_4Buf[5];
                            cdc.Com_Cfg[6] = USBFS_EP0_4Buf[6];
                            cdc.Com_Cfg[7] = cdc.Rx_TimeOutMax;
                        }
                    }
                    if(USBFS_SetupReqLen == 0)
                    {
                        USBFSD->UEP0_TX_LEN = 0;
                        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    }
                }
                break;

            case USBFS_UIS_TOKEN_OUT | DEF_UEP2:
                USBFSD->UEP2_CTRL_H ^= USBFS_UEP_R_TOG;
                cdc.Rx_PackLen[cdc.Rx_LoadPtr] = USBFSD->RX_LEN;
                cdc.Rx_LoadPtr++;
                if(cdc.Rx_LoadPtr >= CDC_RX_PACK_NUM)
                    cdc.Rx_LoadPtr = 0;
                USBFSD->UEP2_DMA = (uint32_t)(uint8_t *)&CDC_Rx_Buf[cdc.Rx_LoadPtr * CDC_RX_PACK_SIZE];
                cdc.Rx_RemainLen++;
                if(cdc.Rx_RemainLen >= (CDC_RX_PACK_NUM - 2))
                {
                    USBFSD->UEP2_CTRL_H &= ~USBFS_UEP_R_RES_MASK;
                    USBFSD->UEP2_CTRL_H |= USBFS_UEP_R_RES_NAK;
                }
                break;

            default: break;
            }
            break;

        case USBFS_UIS_TOKEN_SETUP:
            USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK;

            USBFS_SetupReqType = pUSBFS_SetupReqPak->bRequestType;
            USBFS_SetupReqCode = pUSBFS_SetupReqPak->bRequest;
            USBFS_SetupReqLen = pUSBFS_SetupReqPak->wLength;
            USBFS_SetupReqValue = pUSBFS_SetupReqPak->wValue;
            USBFS_SetupReqIndex = pUSBFS_SetupReqPak->wIndex;
            len = 0;
            errflag = 0;

            if((USBFS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
            {
                if(USBFS_SetupReqType & USB_REQ_TYP_CLASS)
                {
                    switch(USBFS_SetupReqCode)
                    {
                    case CDC_GET_LINE_CODING:
                        pUSBFS_Descr = (uint8_t *)&cdc.Com_Cfg[0];
                        len = 7;
                        break;
                    case CDC_SET_LINE_CODING: break;
                    case CDC_SET_LINE_CTLSTE: break;
                    case CDC_SEND_BREAK: break;
                    default: errflag = 0xff; break;
                    }
                }
                else if(USBFS_SetupReqType & USB_REQ_TYP_VENDOR)
                { }
                else errflag = 0xFF;

                len = (USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                memcpy(USBFS_EP0_4Buf, pUSBFS_Descr, len);
                pUSBFS_Descr += len;
            }
            else
            {
                switch(USBFS_SetupReqCode)
                {
                case USB_GET_DESCRIPTOR:
                    switch((uint8_t)(USBFS_SetupReqValue >> 8))
                    {
                    case USB_DESCR_TYP_DEVICE:
                        pUSBFS_Descr = MyDevDescr;
                        len = DEF_USBD_DEVICE_DESC_LEN;
                        break;
                    case USB_DESCR_TYP_CONFIG:
                        pUSBFS_Descr = MyCfgDescr;
                        len = DEF_USBD_CONFIG_DESC_LEN;
                        break;
                    case USB_DESCR_TYP_STRING:
                        switch((uint8_t)(USBFS_SetupReqValue & 0xFF))
                        {
                        case DEF_STRING_DESC_LANG: pUSBFS_Descr = MyLangDescr; len = DEF_USBD_LANG_DESC_LEN; break;
                        case DEF_STRING_DESC_MANU: pUSBFS_Descr = MyManuInfo; len = DEF_USBD_MANU_DESC_LEN; break;
                        case DEF_STRING_DESC_PROD: pUSBFS_Descr = MyProdInfo; len = DEF_USBD_PROD_DESC_LEN; break;
                        case DEF_STRING_DESC_SERN: pUSBFS_Descr = MySerNumInfo; len = DEF_USBD_SN_DESC_LEN; break;
                        default: errflag = 0xFF; break;
                        }
                        break;
                    default: errflag = 0xFF; break;
                    }
                    if(USBFS_SetupReqLen > len) USBFS_SetupReqLen = len;
                    len = (USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                    memcpy(USBFS_EP0_4Buf, pUSBFS_Descr, len);
                    pUSBFS_Descr += len;
                    break;

                case USB_SET_ADDRESS:
                    USBFS_DevAddr = (uint8_t)(USBFS_SetupReqValue & 0xFF);
                    break;

                case USB_GET_CONFIGURATION:
                    USBFS_EP0_4Buf[0] = USBFS_DevConfig;
                    if(USBFS_SetupReqLen > 1) USBFS_SetupReqLen = 1;
                    break;

                case USB_SET_CONFIGURATION:
                    USBFS_DevConfig = (uint8_t)(USBFS_SetupReqValue & 0xFF);
                    USBFS_DevEnumStatus = 0x01;
                    break;

                case USB_CLEAR_FEATURE:
                    if((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                    {
                        if((uint8_t)(USBFS_SetupReqValue & 0xFF) == USB_REQ_FEAT_REMOTE_WAKEUP)
                            USBFS_DevSleepStatus &= ~0x01;
                    }
                    else if((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                    {
                        if((uint8_t)(USBFS_SetupReqValue & 0xFF) == USB_REQ_FEAT_ENDP_HALT)
                        {
                            switch((uint8_t)(USBFS_SetupReqIndex & 0xFF))
                            {
                            case (DEF_UEP_IN | DEF_UEP1): USBFSD->UEP1_CTRL_H = USBFS_UEP_T_RES_NAK; break;
                            case (DEF_UEP_OUT | DEF_UEP2): USBFSD->UEP2_CTRL_H = USBFS_UEP_R_RES_ACK; break;
                            case (DEF_UEP_IN | DEF_UEP3): USBFSD->UEP3_CTRL_H = USBFS_UEP_T_RES_NAK; break;
                            default: errflag = 0xFF; break;
                            }
                        }
                        else errflag = 0xFF;
                    }
                    else errflag = 0xFF;
                    break;

                case USB_SET_FEATURE:
                    if((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                    {
                        if((uint8_t)(USBFS_SetupReqValue & 0xFF) == USB_REQ_FEAT_REMOTE_WAKEUP)
                        {
                            if(MyCfgDescr[7] & 0x20) USBFS_DevSleepStatus |= 0x01;
                            else errflag = 0xFF;
                        }
                        else errflag = 0xFF;
                    }
                    else if((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                    {
                        if((uint8_t)(USBFS_SetupReqValue & 0xFF) == USB_REQ_FEAT_ENDP_HALT)
                        {
                            switch((uint8_t)(USBFS_SetupReqIndex & 0xFF))
                            {
                            case (DEF_UEP_IN | DEF_UEP1): USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_STALL; break;
                            case (DEF_UEP_OUT | DEF_UEP2): USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_STALL; break;
                            case (DEF_UEP_IN | DEF_UEP3): USBFSD->UEP3_CTRL_H = (USBFSD->UEP3_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_STALL; break;
                            default: errflag = 0xFF; break;
                            }
                        }
                        else errflag = 0xFF;
                    }
                    else errflag = 0xFF;
                    break;

                case USB_GET_INTERFACE:
                    USBFS_EP0_4Buf[0] = 0x00;
                    if(USBFS_SetupReqLen > 1) USBFS_SetupReqLen = 1;
                    break;

                case USB_SET_INTERFACE: break;

                case USB_GET_STATUS:
                    USBFS_EP0_4Buf[0] = 0x00;
                    USBFS_EP0_4Buf[1] = 0x00;
                    if((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                    {
                        if(USBFS_DevSleepStatus & 0x01) USBFS_EP0_4Buf[0] = 0x02;
                    }
                    else if((USBFS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                    {
                        switch((uint8_t)(USBFS_SetupReqIndex & 0xFF))
                        {
                        case (DEF_UEP_IN | DEF_UEP1):
                            if(((USBFSD->UEP1_CTRL_H) & USBFS_UEP_T_RES_MASK) == USBFS_UEP_T_RES_STALL) USBFS_EP0_4Buf[0] = 0x01;
                            break;
                        case (DEF_UEP_OUT | DEF_UEP2):
                            if(((USBFSD->UEP2_CTRL_H) & USBFS_UEP_R_RES_MASK) == USBFS_UEP_R_RES_STALL) USBFS_EP0_4Buf[0] = 0x01;
                            break;
                        case (DEF_UEP_IN | DEF_UEP3):
                            if(((USBFSD->UEP3_CTRL_H) & USBFS_UEP_T_RES_MASK) == USBFS_UEP_T_RES_STALL) USBFS_EP0_4Buf[0] = 0x01;
                            break;
                        default: errflag = 0xFF; break;
                        }
                    }
                    else errflag = 0xFF;
                    if(USBFS_SetupReqLen > 2) USBFS_SetupReqLen = 2;
                    break;

                default: errflag = 0xFF; break;
                }
            }

            if(errflag == 0xff)
            {
                USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
            }
            else
            {
                if(USBFS_SetupReqType & DEF_UEP_IN)
                {
                    len = (USBFS_SetupReqLen > DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                    USBFS_SetupReqLen -= len;
                    USBFSD->UEP0_TX_LEN = len;
                    USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                }
                else
                {
                    if(USBFS_SetupReqLen == 0)
                    {
                        USBFSD->UEP0_TX_LEN = 0;
                        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    }
                    else
                    {
                        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                    }
                }
            }
            break;

        case USBFS_UIS_TOKEN_SOF: break;
        default: break;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
    }
    else if(intflag & USBFS_UIF_BUS_RST)
    {
        USBFS_DevConfig = 0;
        USBFS_DevAddr = 0;
        USBFS_DevSleepStatus = 0;
        USBFS_DevEnumStatus = 0;
        USBFSD->DEV_ADDR = 0;
        USBFS_Device_Endp_Init();
        CDC_Init();
        USBFSD->INT_FG = USBFS_UIF_BUS_RST;
    }
    else if(intflag & USBFS_UIF_SUSPEND)
    {
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
        Delay_Us(10);
        if(USBFSD->MIS_ST & USBFS_UMS_SUSPEND)
            USBFS_DevSleepStatus |= 0x02;
        else
            USBFS_DevSleepStatus &= ~0x02;
    }
    else
    {
        USBFSD->INT_FG = intflag;
    }
}

/*********************************************************************
 * @fn      __debug_cdc_write
 *
 * @brief   Send data via USB CDC EP3 IN.
 */
int __debug_cdc_write(const char *buf, int size)
{
    uint16_t remain = size;
    const char *ptr = buf;

    if(USBFS_DevEnumStatus == 0) return 0;

    while(remain)
    {
        uint16_t packlen = (remain > DEF_USBD_FS_PACK_SIZE) ? DEF_USBD_FS_PACK_SIZE : remain;
        uint32_t timeout = 100000;
        while(USBFS_Endp_Busy[DEF_UEP3] && timeout--);
        if(timeout == 0) break;

        USBFS_Endp_DataUp(DEF_UEP3, (uint8_t *)ptr, packlen, DEF_UEP_CPY_LOAD);
        ptr += packlen;
        remain -= packlen;
    }

    if(size % DEF_USBD_FS_PACK_SIZE == 0)
    {
        uint32_t timeout = 100000;
        while(USBFS_Endp_Busy[DEF_UEP3] && timeout--);
        if(timeout == 0) return 0;
        USBFS_Endp_DataUp(DEF_UEP3, (uint8_t *)ptr, 0, DEF_UEP_CPY_LOAD);
    }

    return 0;
}
