/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/12/26
 * Description        : 8x8 Hall matrix scan + USB CDC virtual serial output.
 ********************************************************************************/

#include "debug.h"
#include <ch32x035_usbfs_device.h>
#include "param.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Global define */
#define CMD_BUF_SIZE    128
#define CMD_PROMPT      "\r\n# "

#define HALL_ROWS       8
#define HALL_COLS       8

/* Global Variable */
static char line_buf[CMD_BUF_SIZE];
static uint16_t line_idx = 0;

volatile uint32_t g_log_tick = 0;
static uint32_t g_scan_cnt = 0;
static uint16_t g_hall[HALL_ROWS][HALL_COLS];

#define TS_ADC_CH   ((uint8_t)0x10)   /* 内部温度传感器 ADC 通道 16 */
static uint16_t g_ts_ref = 0;         /* 校准时温度传感器 ADC 基准 */
static uint16_t g_ts_now = 0;         /* 当前温度传感器 ADC */

static uint16_t TS_Read(void);

/* 行控制输出引脚映射（推挽输出，低电平选通 SS8550 PNP，为对应行霍尔供电） */
static const uint16_t ROW_GPIO_PIN[HALL_ROWS] = {
    GPIO_Pin_9,   /* 行1 A1 = PB9 */
    GPIO_Pin_8,   /* 行2 A2 = PB8 */
    GPIO_Pin_7,   /* 行3 A3 = PB7 */
    GPIO_Pin_6,   /* 行4 A4 = PB6 */
    GPIO_Pin_1,   /* 行5 A5 = PB1 */
    GPIO_Pin_4,   /* 行6 A6 = PB4 */
    GPIO_Pin_3,   /* 行7 A7 = PB3 */
    GPIO_Pin_0    /* 行8 A8 = PB0 */
};

#define ALL_ROW_PINS (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_3 | GPIO_Pin_4 | \
                      GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9)

/* 列 ADC 输入引脚（PA0-PA7，对应 ADC1 通道 0-7） */
#define ALL_COL_PINS  (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | \
                       GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7)

/*********************************************************************
 * @fn      Hall_Matrix_GPIO_Init
 *
 * @brief   初始化霍尔矩阵 GPIO：列 PA0-PA7 模拟输入，行 PB 推挽输出
 *
 * @return  none
 */
static void Hall_Matrix_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* 列输入：PA0-PA7 模拟输入，读取霍尔模拟输出 */
    GPIO_InitStructure.GPIO_Pin = ALL_COL_PINS;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 行输出：先置高再配推挽，避免初始化瞬间导通 */
    GPIO_SetBits(GPIOB, ALL_ROW_PINS);
    GPIO_InitStructure.GPIO_Pin = ALL_ROW_PINS;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_SetBits(GPIOB, ALL_ROW_PINS);
}

/*********************************************************************
 * @fn      Hall_Matrix_ADC_Init
 *
 * @brief   初始化 ADC1，单通道软件触发模式
 *
 * @return  none
 */
static void Hall_Matrix_ADC_Init(void)
{
    ADC_InitTypeDef ADC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    ADC_DeInit(ADC1);
    ADC_CLKConfig(ADC1, ADC_CLK_Div6);

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_Cmd(ADC1, ENABLE);
}

/*********************************************************************
 * @fn      Hall_Read_Column
 *
 * @brief   读取指定列（ADC 通道）的霍尔电压，多次采样取平均
 *
 * @param   col - 列序号 0-7，对应 PA0-PA7
 *
 * @return  ADC 转换平均值
 */
static uint16_t Hall_Read_Column(uint8_t col)
{
    uint16_t val = 0;
    uint8_t i, n;
    uint32_t tmo;

    n = Param_GetSamples();
    if(n < PARAM_SAMPLES_MIN) n = PARAM_SAMPLES_MIN;
    if(n > PARAM_SAMPLES_MAX) n = PARAM_SAMPLES_MAX;

    ADC_RegularChannelConfig(ADC1, (uint8_t)(ADC_Channel_0 + col), 1, ADC_SampleTime_11Cycles);
    for(i = 0; i < n; i++)
    {
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        tmo = 100000;                     /* 超时保护：避免 ADC 异常卡死主循环 */
        while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) && --tmo) { }
        ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
        if(tmo)
        {
            val += ADC_GetConversionValue(ADC1);
        }
    }

    return (uint16_t)((val + n / 2) / n);
}

/*********************************************************************
 * @fn      Hall_Matrix_Scan
 *
 * @brief   逐行选通并读取 8 列霍尔电压，结果存入 g_hall
 *
 * @return  none
 */
static void Hall_Matrix_Scan(void)
{
    uint8_t row, col;

    for(row = 0; row < HALL_ROWS; row++)
    {
        /* 选通第 row 行（输出低电平） */
        GPIO_ResetBits(GPIOB, ROW_GPIO_PIN[row]);
        Delay_Us(Param_GetRowDelay());

        /* 读取 8 列 ADC */
        for(col = 0; col < HALL_COLS; col++)
        {
            g_hall[row][col] = Hall_Read_Column(col);
        }

        /* 关闭该行 */
        GPIO_SetBits(GPIOB, ROW_GPIO_PIN[row]);
        Delay_Us(Param_GetCloseDelay());
    }

    /* 扫描结束后读取内部温度传感器（仅温漂补偿开启时，用于温漂补偿） */
    if(Param_GetTempEn())
    {
        g_ts_now = TS_Read();
    }
}

/*********************************************************************
 * @fn      TS_Read
 *
 * @brief   读取 CH32 内部温度传感器 ADC（通道 16），8 次平均
 *          带超时保护：内部通道无效时不阻塞主循环
 *
 * @return  温度 ADC 值（读取失败时返回上次有效值）
 */
static uint16_t TS_Read(void)
{
    uint16_t val = 0;
    uint8_t i, cnt = 0;

    ADC_RegularChannelConfig(ADC1, TS_ADC_CH, 1, ADC_SampleTime_11Cycles);
    for(i = 0; i < 8; i++)
    {
        uint32_t tmo = 1000;
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) && --tmo) { }
        if(tmo == 0)
        {
            break;   /* 转换超时：内部通道可能不可用，放弃本次 */
        }
        ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
        val += ADC_GetConversionValue(ADC1);
        cnt++;
    }

    if(cnt == 0)
    {
        return g_ts_now;   /* 读取失败：保持上次值 */
    }
    return (uint16_t)(val / cnt);
}

/*********************************************************************
 * @fn      TIM3_CDC_Init
 *
 * @brief   Initialize TIM3 for CDC receive timeout counting.
 *
 * @return  none
 */
static void TIM3_CDC_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_DeInit(TIM3);
    TIM_TimeBaseStructure.TIM_Period = 100 - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = SystemCoreClock / 1000000 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    NVIC_EnableIRQ(TIM3_IRQn);
    TIM_Cmd(TIM3, ENABLE);
}

static void CDC_Send(const char *str)
{
    __debug_cdc_write(str, strlen(str));
}

static void Print_SystemInfo(void)
{
    char buf[32];
    CDC_Send("\r\n========== System Info ==========\r\n");
    CDC_Send("Chip       : CH32X035\r\n");
    CDC_Send("ChipID     : 0x");
    sprintf(buf, "%08X\r\n", (unsigned int)DBGMCU_GetCHIPID());
    CDC_Send(buf);
    CDC_Send("Clock      : ");
    sprintf(buf, "%lu Hz\r\n", (unsigned long)SystemCoreClock);
    CDC_Send(buf);
    CDC_Send("USB Status : ");
    CDC_Send(USBFS_DevEnumStatus ? "Enumerated\r\n" : "Not enumerated\r\n");
    CDC_Send("==================================\r\n");
}

static void Print_Status(void)
{
    char buf[32];
    CDC_Send("\r\n========== Status ==========\r\n");
    sprintf(buf, "Log Tick   : %lu\r\n", (unsigned long)g_log_tick);
    CDC_Send(buf);
    sprintf(buf, "Scan Count : %lu\r\n", (unsigned long)g_scan_cnt);
    CDC_Send(buf);
    sprintf(buf, "Interval   : %u ms\r\n", (unsigned)Param_GetInterval());
    CDC_Send(buf);
    sprintf(buf, "Samples    : %u\r\n", (unsigned)Param_GetSamples());
    CDC_Send(buf);
    CDC_Send("=============================\r\n");
}

static void Send_Hall_Matrix(void)
{
    char buf[24];
    uint8_t row, col;

    CDC_Send("\r\n=== Hall Matrix (8x8) ===\r\n");
    for(row = 0; row < HALL_ROWS; row++)
    {
        for(col = 0; col < HALL_COLS; col++)
        {
            sprintf(buf, "%04d ", g_hall[row][col]);
            CDC_Send(buf);
        }
        CDC_Send("\r\n");
    }
    CDC_Send("==========================\r\n");
}

/*********************************************************************
 * @fn      Adc_To_Field
 *
 * @brief   SS49E 线性霍尔 ADC 值换算为磁场强度（含逐点校准与温漂补偿）
 *
 *          1) 逐点零场校准: adc' = adc - cal_off[row][col]
 *          2) 磁场换算:     B(G) = adc' * field_k / 1000
 *          3) 温漂补偿:     B += (ts_now - ts_ref) * temp_coeff / 1000
 *
 * @param   adc - 单点 ADC 值
 * @param   row, col - 点坐标
 *
 * @return  磁场强度, 单位 0.1G
 */
static int32_t Adc_To_Field(uint16_t adc, uint8_t row, uint8_t col)
{
    int32_t v;
    uint8_t idx = (uint8_t)(row * HALL_COLS + col);

    /* 1) 逐点零场校准 */
    v = (int32_t)adc - (int32_t)g_cal_off[idx];

    /* 2) 磁场换算为 0.1G */
    v = v * (int32_t)Param_GetFieldK();
    v = v / 100;

    /* 3) 温漂补偿（温度升高 ts_now 增大，方向由系数符号决定） */
    if(Param_GetTempEn())
    {
        int32_t dts = (int32_t)g_ts_now - (int32_t)g_ts_ref;
        int32_t coeff = (int32_t)Param_GetTempCoeff();
        v += dts * coeff / 100;
    }

    return v;
}

static void Send_Field_Matrix(void)
{
    char buf[24];
    uint8_t row, col;

    CDC_Send("\r\n=== Field Matrix (G) ===\r\n");
    for(row = 0; row < HALL_ROWS; row++)
    {
        for(col = 0; col < HALL_COLS; col++)
        {
            int32_t b10 = Adc_To_Field(g_hall[row][col], row, col);
            int32_t b_abs = b10 < 0 ? -b10 : b10;
            sprintf(buf, "%+5ld.%d ", (long)(b10 / 10), (int)(b_abs % 10));
            CDC_Send(buf);
        }
        CDC_Send("\r\n");
    }
    CDC_Send("============================\r\n");
}

static void Send_Cal_Table(void)
{
    char buf[24];
    uint8_t row, col;

    CDC_Send("\r\n=== Cal Table ===\r\n");
    for(row = 0; row < HALL_ROWS; row++)
    {
        for(col = 0; col < HALL_COLS; col++)
        {
            sprintf(buf, "%04d ", g_cal_off[row * HALL_COLS + col]);
            CDC_Send(buf);
        }
        CDC_Send("\r\n");
    }
    CDC_Send("====================\r\n");
}

static void Print_Help(void)
{
    CDC_Send("\r\nAvailable Commands:\r\n");
    CDC_Send("  sysinfo      - Show chip and clock information\r\n");
    CDC_Send("  status       - Show current device status\r\n");
    CDC_Send("  hall         - Scan hall matrix and send once\r\n");
    CDC_Send("  field        - Scan and send field strength (Gauss)\r\n");
    CDC_Send("  cal_zero     - Calibrate zero field (save 64 offsets + temp ref)\r\n");
    CDC_Send("  cal_reset    - Reset calibration table\r\n");
    CDC_Send("  cal          - Show calibration table\r\n");
    CDC_Send("  temp         - Show temperature sensor info\r\n");
    CDC_Send("  i [val]      - Get/set param i (uint16)\r\n");
    CDC_Send("  param        - List all params\r\n");
    CDC_Send("  param save   - Save params to flash\r\n");
    CDC_Send("  param def    - Restore defaults\r\n");
    CDC_Send("  param set <name> <val>  - Set param by name\r\n");
    CDC_Send("  Run params: interval(row period ms) row_delay(us)\r\n");
    CDC_Send("             close_delay(us) samples(avg times)\r\n");
    CDC_Send("  Field(SS49E): field_zero(mid adc) field_k(cal)\r\n");
    CDC_Send("  Temp drift: temp_en(0/1) temp_coeff(int16)\r\n");
    CDC_Send("  reset        - Software reset MCU\r\n");
    CDC_Send("  help         - Show this help\r\n");
}

static void Cmd_Param(const char *args)
{
    char buf[64], name[32], val_str[32];
    long val;

    while(*args == ' ') args++;
    strcpy(buf, args);

    if(buf[0] == '\0' || strcmp(buf, "list") == 0)
    {
        char tmp[48];
        CDC_Send("\r\n");
        for(int i = 0; i < PARAM_CNT; i++)
        {
            uint32_t v = Param_Get(i);
            if(PARAM_REG[i].size <= 2)
                sprintf(tmp, "  %s = %u\r\n", PARAM_REG[i].name, (unsigned)v);
            else
                sprintf(tmp, "  %s = %lu\r\n", PARAM_REG[i].name, (unsigned long)v);
            CDC_Send(tmp);
        }
        return;
    }
    if(strcmp(buf, "save") == 0)   { Param_Save(); CDC_Send("\r\nSaved\r\n"); return; }
    if(strcmp(buf, "def") == 0)    { Param_Default(); CDC_Send("\r\nDefaults restored\r\n"); return; }

    if(sscanf(buf, "set %31s %31s", name, val_str) == 2)
    {
        int8_t id = Param_Find(name);
        if(id < 0) { CDC_Send("\r\nUnknown param\r\n"); return; }
        val = strtol(val_str, NULL, 0);
        Param_Set(id, (uint32_t)val);
        Param_Save();
        {
            char tmp[48];
            sprintf(tmp, "\r\n%s = %lu\r\n", name, (unsigned long)Param_Get(id));
            CDC_Send(tmp);
        }
        return;
    }
    CDC_Send("\r\nUsage: param [list|save|def|set <name> <val>]\r\n");
}

static void Execute_Command(const char *cmd)
{
    char buf[CMD_BUF_SIZE];

    while(*cmd == ' ' || *cmd == '\t') cmd++;
    strcpy(buf, cmd);

    if(strcmp(buf, "sysinfo") == 0)
        Print_SystemInfo();
    else if(strcmp(buf, "status") == 0)
        Print_Status();
    else if(strcmp(buf, "hall") == 0)
        { Hall_Matrix_Scan(); Send_Hall_Matrix(); }
    else if(strcmp(buf, "field") == 0)
        { Hall_Matrix_Scan(); Send_Field_Matrix(); }
    else if(strcmp(buf, "cal") == 0)
        { Send_Cal_Table(); }
    else if(strcmp(buf, "cal_zero") == 0)
    {
        uint8_t i;
        Hall_Matrix_Scan();           /* 更新 g_hall 与 g_ts_now */
        for(i = 0; i < CAL_POINTS; i++)
            g_cal_off[i] = g_hall[i / HALL_COLS][i % HALL_COLS];
        g_ts_ref = g_ts_now;
        Param_Save();
        CDC_Send("\r\nZero-field calibration saved (64 points, temp ref captured)\r\n");
    }
    else if(strcmp(buf, "cal_reset") == 0)
    {
        uint8_t i;
        for(i = 0; i < CAL_POINTS; i++)
            g_cal_off[i] = 0;
        g_ts_ref = 0;
        Param_Save();
        CDC_Send("\r\nCalibration reset\r\n");
    }
    else if(strcmp(buf, "temp") == 0)
    {
        char b[24];
        CDC_Send("\r\nTS now=");
        sprintf(b, "%u", g_ts_now); CDC_Send(b);
        CDC_Send(" ref=");
        sprintf(b, "%u", g_ts_ref); CDC_Send(b);
        CDC_Send(" en=");
        sprintf(b, "%u", (unsigned)Param_GetTempEn()); CDC_Send(b);
        CDC_Send(" coeff=");
        sprintf(b, "%d", (int)Param_GetTempCoeff()); CDC_Send(b);
        CDC_Send("\r\n");
    }
    else if(strcmp(buf, "help") == 0)
        Print_Help();
    else if(strcmp(buf, "reset") == 0)
        { CDC_Send("\r\nSystem reset...\r\n"); Delay_Ms(10); NVIC_SystemReset(); }
    else if(strncmp(buf, "param", 5) == 0)
        { Cmd_Param(buf + 5); }
    else if(strncmp(buf, "i", 1) == 0)
    {
        char *arg = buf + 1;
        while(*arg == ' ') arg++;
        if(*arg == '\0')
        {
            char tmp[32];
            CDC_Send("\r\ni = ");
            sprintf(tmp, "%u (0x%04X)\r\n", (unsigned)Param_GetI(), (unsigned)Param_GetI());
            CDC_Send(tmp);
        }
        else
        {
            long v = strtol(arg, NULL, 0);
            if(v >= 0 && v <= PARAM_I_VAL_MAX)
            {
                Param_SetI((uint16_t)v);
                Param_Save();
                { char tmp[32]; sprintf(tmp, "%u\r\n", (unsigned)v); CDC_Send("\r\ni set to "); CDC_Send(tmp); }
            }
            else CDC_Send("\r\nInvalid (0-65535)\r\n");
        }
    }
    else if(strlen(buf) > 0)
    {
        CDC_Send("\r\nUnknown command: ");
        CDC_Send(buf);
        CDC_Send("\r\nType 'help' for available commands\r\n");
    }
}

static void Process_CDC_Data(void)
{
    NVIC_DisableIRQ(USBFS_IRQn);

    while(cdc.Rx_RemainLen > 0)
    {
        uint16_t pkt_len = cdc.Rx_PackLen[cdc.Rx_DealPtr];
        uint8_t *pkt_data = &CDC_Rx_Buf[cdc.Rx_DealPtr * CDC_RX_PACK_SIZE];

        for(uint16_t i = 0; i < pkt_len; i++)
        {
            char c = (char)pkt_data[i];
            if(c == '\r' || c == '\n')
            {
                if(line_idx > 0)
                {
                    NVIC_EnableIRQ(USBFS_IRQn);
                    line_buf[line_idx] = '\0';
                    CDC_Send("\r\n");
                    CDC_Send(line_buf);
                    Execute_Command(line_buf);
                    memset(line_buf, 0, line_idx);
                    line_idx = 0;
                    CDC_Send(CMD_PROMPT);
                    NVIC_DisableIRQ(USBFS_IRQn);
                }
            }
            else if(c >= 0x20 && c < 0x7F)
            {
                if(line_idx < CMD_BUF_SIZE - 1)
                    line_buf[line_idx++] = c;
            }
        }

        cdc.Rx_PackLen[cdc.Rx_DealPtr] = 0;
        cdc.Rx_DealPtr++;
        if(cdc.Rx_DealPtr >= CDC_RX_PACK_NUM) cdc.Rx_DealPtr = 0;
        cdc.Rx_RemainLen--;
    }

    if(cdc.Rx_RemainLen == 0)
    {
        cdc.Rx_LoadPtr = 0;
        cdc.Rx_DealPtr = 0;
        USBFSD->UEP2_DMA = (uint32_t)(uint8_t *)&CDC_Rx_Buf[0];
        USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
    }

    NVIC_EnableIRQ(USBFS_IRQn);
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    printf("SystemClk:%d\r\n", SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());

    Hall_Matrix_GPIO_Init();
    Hall_Matrix_ADC_Init();

    TIM3_CDC_Init();
    CDC_Init();

    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE, PWR_VDD_SupplyVoltage());

    Param_Init();

    printf("USB CDC ready\r\n");

    while(1)
    {
        uint16_t itv;

        Process_CDC_Data();
        Hall_Matrix_Scan();
        Send_Hall_Matrix();
        g_scan_cnt++;
        g_log_tick++;

        itv = Param_GetInterval();
        if(itv < PARAM_INTERVAL_MIN) itv = PARAM_INTERVAL_MIN;
        if(itv > PARAM_INTERVAL_MAX) itv = PARAM_INTERVAL_MAX;
        Delay_Ms(itv);
    }
}
