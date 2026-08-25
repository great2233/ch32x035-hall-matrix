#include "param.h"
#include <stdio.h>

ParamBlock_t g_pb;
uint16_t g_cal_off[CAL_POINTS];

/**
 * 注册表：新增参数在这里加一行
 * 格式：{ "名字", 结构体偏移, 字节数(1/2/4), 默认值 }
 */
const ParamReg_t PARAM_REG[PARAM_CNT] = {
    { "i",          4,  2, 0        },
    { "mode",       6,  1, 0        },
    { "thresh",     8,  2, 100      },
    { "count",      12, 4, 0        },
    { "interval",   16, 2, PARAM_DEF_INTERVAL   },
    { "row_delay",  18, 2, PARAM_DEF_ROW_DELAY  },
    { "close_delay",20, 2, PARAM_DEF_CLOSE_DELAY },
    { "samples",    22, 1, PARAM_DEF_SAMPLES    },
    { "field_zero", 24, 2, PARAM_DEF_FIELD_ZERO },
    { "field_k",    26, 2, PARAM_DEF_FIELD_K    },
    { "temp_en",    28, 1, PARAM_DEF_TEMP_EN    },
    { "temp_coeff", 30, 2, (uint32_t)(int16_t)PARAM_DEF_TEMP_COEFF },
    /* ↑ 新增参数加在这里 ↑ */
};

static void set_defaults(void)
{
    memset(&g_pb, 0, sizeof(g_pb));
    g_pb.magic = PARAM_MAGIC;
    for(int i = 0; i < PARAM_CNT; i++)
    {
        uint32_t v = PARAM_REG[i].def;
        uint8_t *p = (uint8_t *)&g_pb + PARAM_REG[i].offset;
        if(PARAM_REG[i].size == 1)      *(uint8_t  *)p = (uint8_t)v;
        else if(PARAM_REG[i].size == 2) *(uint16_t *)p = (uint16_t)v;
        else                             *(uint32_t *)p = v;
    }
    memset(g_cal_off, 0, sizeof(g_cal_off));
}

/*========== API ==========*/

void Param_Init(void)
{
    ParamBlock_t *p = (ParamBlock_t *)FLASH_PARAM_ADDR;
    if(p->magic == PARAM_MAGIC)
    {
        memcpy(&g_pb, p, sizeof(g_pb));
        memcpy(g_cal_off, (uint8_t *)FLASH_PARAM_ADDR + CAL_TABLE_OFF, sizeof(g_cal_off));
        printf("Param loaded\r\n");
        /* 新字段合法性校验：非法则恢复默认 */
        if(g_pb.interval < PARAM_INTERVAL_MIN || g_pb.interval > PARAM_INTERVAL_MAX ||
           g_pb.row_delay == 0 || g_pb.row_delay > PARAM_ROW_DELAY_MAX ||
           g_pb.close_delay > PARAM_CLOSE_DELAY_MAX ||
           g_pb.samples < PARAM_SAMPLES_MIN || g_pb.samples > PARAM_SAMPLES_MAX ||
           g_pb.field_zero > PARAM_FIELD_ZERO_MAX ||
           g_pb.field_k < PARAM_FIELD_K_MIN || g_pb.field_k > PARAM_FIELD_K_MAX ||
           g_pb.temp_en > PARAM_TEMP_EN_MAX ||
           (int16_t)g_pb.temp_coeff < PARAM_TEMP_COEFF_MIN ||
           (int16_t)g_pb.temp_coeff > PARAM_TEMP_COEFF_MAX)
        {
            printf("Param invalid, reset to default\r\n");
            set_defaults();
        }
    }
    else
    {
        set_defaults();
        printf("Param init (magic=0x%08X)\r\n", p->magic);
    }
    Param_Show();
}

void Param_Save(void)
{
    uint8_t buf[256];

    memset(buf, 0, sizeof(buf));
    memcpy(buf, &g_pb, sizeof(g_pb));
    memcpy(buf + CAL_TABLE_OFF, g_cal_off, sizeof(g_cal_off));

    FLASH_ROM_ERASE(FLASH_PARAM_ADDR, 256);
    FLASH_ROM_WRITE(FLASH_PARAM_ADDR, (uint32_t *)buf, 256);
    printf("Param saved\r\n");
}

void Param_Show(void)
{
    printf("--- Params ---\r\n");
    for(int i = 0; i < PARAM_CNT; i++)
    {
        uint32_t v = Param_Get(i);
        if(PARAM_REG[i].size <= 2)
            printf("  %s = %u\r\n", PARAM_REG[i].name, (unsigned)v);
        else
            printf("  %s = %lu\r\n", PARAM_REG[i].name, (unsigned long)v);
    }
}

void Param_Default(void)
{
    set_defaults();
    printf("Params set to defaults\r\n");
}

int8_t Param_Find(const char *name)
{
    for(int i = 0; i < PARAM_CNT; i++)
        if(strcmp(name, PARAM_REG[i].name) == 0) return i;
    return -1;
}

uint32_t Param_Get(int8_t id)
{
    if(id < 0 || id >= PARAM_CNT) return 0;
    uint8_t *p = (uint8_t *)&g_pb + PARAM_REG[id].offset;
    if(PARAM_REG[id].size == 1)      return *(uint8_t  *)p;
    else if(PARAM_REG[id].size == 2) return *(uint16_t *)p;
    else                             return *(uint32_t *)p;
}

void Param_Set(int8_t id, uint32_t val)
{
    if(id < 0 || id >= PARAM_CNT) return;
    uint8_t *p = (uint8_t *)&g_pb + PARAM_REG[id].offset;
    if(PARAM_REG[id].size == 1)      *(uint8_t  *)p = (uint8_t)val;
    else if(PARAM_REG[id].size == 2) *(uint16_t *)p = (uint16_t)val;
    else                             *(uint32_t *)p = val;
}
