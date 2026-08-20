#include "ota.h"
#include "file_sys.h"
#include "usart.h"
#include "log_store.h"
#include <string.h>

/* ============================================================
 * OTA 保护实现 (伪 OTA: SD 卡镜像 + CRC32 校验)
 *
 * SD 卡块布局:
 *   Block OTA_HDR_BLOCK:        镜像头 (ota_header_t + padding 到 512B)
 *   Block OTA_HDR_BLOCK+1 ... : 镜像数据
 *
 * ota_header_t:
 *   magic       = 0x4F544131 ("OTA1")
 *   length      = 镜像数据字节数 (<= OTA_MAX_BLOCKS * 512)
 *   crc32       = 镜像数据 CRC32 (zlib 多项式 0xEDB88320, 与 settings/file_sys 一致)
 *   target_addr = 烧写目标 Flash 地址 (例如 0x08080000=Sector 10, 仅参考)
 * ============================================================ */

#define OTA_HDR_BLOCK      200      /* 镜像头块 */
#define OTA_MAX_BLOCKS     100      /* 最多 100*512 = 50KB */
#define OTA_MAGIC          0x4F544131u  /* "OTA1" */

typedef struct {
    uint32_t magic;        /* OTA_MAGIC = "OTA1" */
    uint32_t length;       /* 镜像数据字节长度 */
    uint32_t crc32;        /* 镜像数据 CRC32 */
    uint32_t target_addr;  /* 烧写目标 Flash 地址 (仅参考) */
} ota_header_t;

/* CRC32 (zlib/zip 多项式 0xEDB88320, 与 settings.c/file_sys.c 一致) */
static uint32_t ota_crc32(const uint8_t *data, int len)
{
    uint32_t crc = 0xFFFFFFFFu;
    int i;
    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        int b;
        for (b = 0; b < 8; b++)
        {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc = (crc >> 1);
        }
    }
    return ~crc;
}

/* 清除 OTA 头块 (magic=0), 防止重启循环 */
static void ota_clear_header(void)
{
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    FileSys_WriteRawBlock(OTA_HDR_BLOCK, buf);
}

int OTA_CheckPendingUpdate(void)
{
    uint8_t buf[512];
    ota_header_t hdr;
    uint32_t crc;
    int blocks, i;
    uint32_t remaining;

    /* 1. 读头块 */
    if (FileSys_ReadRawBlock(OTA_HDR_BLOCK, buf) != 0)
    {
        Log_Printf("[OTA] read header block failed\r\n");
        return -1;
    }
    memcpy(&hdr, buf, sizeof(hdr));

    /* 2. 校验 magic (无镜像则返回 0, 不算错误) */
    if (hdr.magic != OTA_MAGIC)
    {
        Log_Printf("[OTA] no pending update (magic=0x%08x)\r\n",
                   (unsigned)hdr.magic);
        return 0;
    }

    /* 3. 校验长度合法性 */
    if (hdr.length == 0 || hdr.length > OTA_MAX_BLOCKS * 512)
    {
        Log_Printf("[OTA] invalid length %u, clearing\r\n",
                   (unsigned)hdr.length);
        ota_clear_header();
        return -1;
    }

    /* 4. 计算镜像数据 CRC32 */
    blocks = (hdr.length + 511) / 512;
    remaining = hdr.length;
    crc = 0xFFFFFFFFu;
    for (i = 0; i < blocks; i++)
    {
        int len;
        int j;
        if (FileSys_ReadRawBlock(OTA_HDR_BLOCK + 1 + i, buf) != 0)
        {
            Log_Printf("[OTA] read data block %d failed, clearing\r\n", i);
            ota_clear_header();
            return -1;
        }
        /* 最后一块只算剩余字节 */
        len = (remaining > 512) ? 512 : (int)remaining;
        for (j = 0; j < len; j++)
        {
            crc ^= buf[j];
            int b;
            for (b = 0; b < 8; b++)
            {
                if (crc & 1u)
                    crc = (crc >> 1) ^ 0xEDB88320u;
                else
                    crc = (crc >> 1);
            }
        }
        remaining -= (uint32_t)len;
    }
    crc = ~crc;

    /* 5. 比对 CRC */
    if (crc != hdr.crc32)
    {
        Log_Printf("[OTA] CRC mismatch: stored=0x%08x computed=0x%08x, clearing\r\n",
                   (unsigned)hdr.crc32, (unsigned)crc);
        ota_clear_header();
        return -1;
    }

    /* 6. 镜像验证通过 */
    Log_Printf("[OTA] update verified: length=%u target=0x%08x\r\n",
               (unsigned)hdr.length, (unsigned)hdr.target_addr);
    LogStore_Info("[OTA] update verified");

    /* 7. 伪跳转: 实际不烧写到 Flash (无 bootloader, 双 bank OTA 需独立工程)
     *    真实 OTA 需要 bootloader 在 Sector 0 + 链接器双套镜像布局,
     *    此处只标记验证通过, 不进行 Flash 烧写.
     *    如未来加 bootloader, 在此处加 HAL_FLASH_Program 烧写到 target_addr
     *    并设置 VTOR 跳转. */
    Log_Printf("[OTA] pseudo-jump: actual flash write skipped (no bootloader)\r\n");

    /* 8. 清除头块, 防止下次启动重复检查 */
    ota_clear_header();
    Log_Printf("[OTA] header cleared, resume normal boot\r\n");

    return 1;
}
