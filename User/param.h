#ifndef __PARAM_H__
#define __PARAM_H__

#include "debug.h"
#include <string.h>

/**
 * ============================================================
 *  Flash 参数存储模块
 *  用法：任意函数中直接操作 g_pb，改完后调用 Param_Save()
 *
 *    g_pb.i = 123;            // 改 RAM
 *    Param_Save();            // 写 Flash
 *    uint16_t v = *flash_i;   // 读 Flash 原始值（不经过 RAM）
 * ============================================================
 */

/*========== 配置 ==========*/
#define FLASH_PARAM_ADDR    0x0800F000           /* Flash 末页 */
#define PARAM_MAGIC         0x50415201           /* 魔数 "PAR1" */
#define PARAM_I_VAL_MAX     0xFFFF               /* 参数 i 上限（uint16） */

/* 霍尔矩阵运行参数默认值 */
#define PARAM_DEF_INTERVAL     300      /* 扫描发送周期 ms */
#define PARAM_DEF_ROW_DELAY    100      /* 行选通稳定延时 us */
#define PARAM_DEF_CLOSE_DELAY  50       /* 行关闭延时 us */
#define PARAM_DEF_SAMPLES      4        /* 每个点 ADC 采样平均次数 */
#define PARAM_DEF_FIELD_ZERO   2048     /* SS49E 零磁场 ADC 中点值 */
#define PARAM_DEF_FIELD_K      872      /* 换算系数: 每 ADC 对应 0.1G 的比例 */
                                        /* B(G)=(adc-zero)*K/1000, K=872≈SS49E 1.4mV/G@5V */
#define PARAM_DEF_TEMP_EN      0        /* 温漂补偿默认关闭 */
#define PARAM_DEF_TEMP_COEFF   0        /* 温漂补偿系数 (int16, 0.1G/温度ADC差) */

/* 参数取值范围 */
#define PARAM_INTERVAL_MIN     50
#define PARAM_INTERVAL_MAX     5000
#define PARAM_ROW_DELAY_MAX    5000
#define PARAM_CLOSE_DELAY_MAX  2000
#define PARAM_SAMPLES_MIN      1
#define PARAM_SAMPLES_MAX      16
#define PARAM_FIELD_ZERO_MAX   4095
#define PARAM_FIELD_K_MIN      1
#define PARAM_FIELD_K_MAX      10000
#define PARAM_TEMP_EN_MAX      1
#define PARAM_TEMP_COEFF_MIN   (-1000)
#define PARAM_TEMP_COEFF_MAX   1000

/*========== 参数结构体 ==========*/
/**
 * @brief 参数结构体
 *
 * ══════════════════════════════════════════════════════
 *  【新增参数步骤】
 * ══════════════════════════════════════════════════════
 *
 *  第 1 步：在结构体中添加字段
 *      uint16_t  my_var;    // 放在 reserved 前面
 *
 *  第 2 步：添加 Flash 地址宏（用于直接读 Flash 原始值）
 *      #define flash_my_var  ((uint16_t *)(FLASH_PARAM_ADDR + 14))
 *      注: 14 = 4(magic) + 2(i) + 1(mode) + 2(thresh) + 4(count)
 *
 *  第 3 步：在 PARAM_REG[] 中注册（param.c）
 *      { "my_var", 14, 2, 0 },
 *      格式: { "名字", 结构体偏移, 字节数, 默认值 }
 *
 *  第 4 步（可选）：添加内联存取函数
 *      static inline uint16_t Param_GetMyVar(void)       { return g_pb.my_var; }
 *      static inline void     Param_SetMyVar(uint16_t v) { g_pb.my_var = v; }
 *
 *  ⚠️ 注意：添加字段后务必更新 reserved 前面的总字节数，
 *     若总大小超过 256 字节，需要将 FLASH_PARAM_ADDR
 *     改为前一页 (0x0800EF00)。
 * ══════════════════════════════════════════════════════
 */
typedef struct {
    uint32_t magic;          /*  0: 魔数 */
    uint16_t i;              /*  4: 参数 i */
    uint8_t  mode;           /*  6: 模式 */
    uint16_t thresh;         /*  8: 阈值 */
    uint32_t count;          /* 12: 计数值 */
    uint16_t interval;       /* 16: 扫描发送周期 ms */
    uint16_t row_delay;      /* 18: 行选通稳定延时 us */
    uint16_t close_delay;    /* 20: 行关闭延时 us */
    uint8_t  samples;        /* 22: ADC 采样平均次数 */
    uint16_t field_zero;     /* 24: SS49E 零磁场 ADC 中点值 */
    uint16_t field_k;        /* 26: 磁场换算系数 (每 ADC 对应 0.1G 比例) */
    uint8_t  temp_en;        /* 28: 温漂补偿使能 */
    uint16_t temp_coeff;     /* 30: 温漂补偿系数 (int16, 0.1G/温度ADC差) */
    /* ↑ 新增字段加在这里 ↑ */
} ParamBlock_t;              /* 当前共 32 字节 */

/* 逐点零场校准表：紧接参数块之后存入 Flash（偏移 32 起，64 x u16） */
#define CAL_POINTS       64
#define CAL_TABLE_OFF    ((uint16_t)sizeof(ParamBlock_t))

/* Flash 直接地址宏（只读时用，绕过 RAM 缓存） */
#define flash_i       ((uint16_t *)(FLASH_PARAM_ADDR + 4))
#define flash_mode    ((uint8_t  *)(FLASH_PARAM_ADDR + 6))
#define flash_thresh  ((uint16_t *)(FLASH_PARAM_ADDR + 8))
#define flash_count   ((uint32_t *)(FLASH_PARAM_ADDR + 12))
#define flash_interval    ((uint16_t *)(FLASH_PARAM_ADDR + 16))
#define flash_row_delay   ((uint16_t *)(FLASH_PARAM_ADDR + 18))
#define flash_close_delay ((uint16_t *)(FLASH_PARAM_ADDR + 20))
#define flash_samples     ((uint8_t  *)(FLASH_PARAM_ADDR + 22))
#define flash_field_zero  ((uint16_t *)(FLASH_PARAM_ADDR + 24))
#define flash_field_k     ((uint16_t *)(FLASH_PARAM_ADDR + 26))
#define flash_temp_en     ((uint8_t  *)(FLASH_PARAM_ADDR + 28))
#define flash_temp_coeff  ((uint16_t *)(FLASH_PARAM_ADDR + 30))
#define flash_cal(i)      ((uint16_t *)(FLASH_PARAM_ADDR + CAL_TABLE_OFF + 2 * (i)))
/* ↑ 新增字段的 Flash 宏加在这里 ↑ */

/*========== 注册表 ==========*/
typedef struct {
    const char *name;        /* 命令名字，如 "i"、"mode" */
    uint16_t offset;         /* 在结构体中的字节偏移 */
    uint8_t  size;           /* 字节数：1/2/4 */
    uint32_t def;            /* 默认值 */
} ParamReg_t;

#define PARAM_CNT  12
extern const ParamReg_t PARAM_REG[PARAM_CNT];

extern uint16_t g_cal_off[CAL_POINTS];  /* 逐点零场校准偏移 */

extern ParamBlock_t g_pb;

/*========== API ==========*/
void     Param_Init(void);              /* 上电调用，从 Flash 加载 */
void     Param_Save(void);              /* 全部写入 Flash（整页 256B 擦写）*/
void     Param_Show(void);              /* 打印所有参数 */
void     Param_Default(void);           /* 恢复默认值 */
int8_t   Param_Find(const char *name);  /* 按名查找，返回 ID */
uint32_t Param_Get(int8_t id);          /* 按 ID 取值 */
void     Param_Set(int8_t id, uint32_t val); /* 按 ID 设值（不存 Flash）*/

/*========== 内联存取函数 ==========*/
/* 任意函数可直接操作 g_pb.xxx，或用以下内联函数 */
static inline uint16_t Param_GetI(void)       { return g_pb.i; }
static inline void     Param_SetI(uint16_t v) { g_pb.i = v; }
static inline uint8_t  Param_GetMode(void)       { return g_pb.mode; }
static inline void     Param_SetMode(uint8_t v)  { g_pb.mode = v; }
static inline uint16_t Param_GetThresh(void)       { return g_pb.thresh; }
static inline void     Param_SetThresh(uint16_t v) { g_pb.thresh = v; }
static inline uint32_t Param_GetCount(void)       { return g_pb.count; }
static inline void     Param_SetCount(uint32_t v) { g_pb.count = v; }
static inline uint16_t Param_GetInterval(void)       { return g_pb.interval; }
static inline void     Param_SetInterval(uint16_t v) { g_pb.interval = v; }
static inline uint16_t Param_GetRowDelay(void)       { return g_pb.row_delay; }
static inline void     Param_SetRowDelay(uint16_t v) { g_pb.row_delay = v; }
static inline uint16_t Param_GetCloseDelay(void)       { return g_pb.close_delay; }
static inline void     Param_SetCloseDelay(uint16_t v) { g_pb.close_delay = v; }
static inline uint8_t  Param_GetSamples(void)       { return g_pb.samples; }
static inline void     Param_SetSamples(uint8_t v)  { g_pb.samples = v; }
static inline uint16_t Param_GetFieldZero(void)       { return g_pb.field_zero; }
static inline void     Param_SetFieldZero(uint16_t v) { g_pb.field_zero = v; }
static inline uint16_t Param_GetFieldK(void)       { return g_pb.field_k; }
static inline void     Param_SetFieldK(uint16_t v) { g_pb.field_k = v; }
static inline uint8_t  Param_GetTempEn(void)       { return g_pb.temp_en; }
static inline void     Param_SetTempEn(uint8_t v)  { g_pb.temp_en = v; }
static inline int16_t  Param_GetTempCoeff(void)    { return (int16_t)g_pb.temp_coeff; }
static inline void     Param_SetTempCoeff(int16_t v){ g_pb.temp_coeff = (uint16_t)v; }
/* ↑ 新增字段的内联函数加在这里 ↑ */

#endif
