#ifndef OTA_H
#define OTA_H

/* ============================================================
 * OTA 保护 (伪 OTA: SD 卡镜像校验 + 跳转标志)
 *
 * 设计原理:
 *   - 用户从电脑把 UPDATE.BIN 镜像拷贝到 SD 卡专用块区
 *   - 启动时检查镜像头部 (magic + length + crc32)
 *   - 校验镜像数据 CRC32 完整性
 *   - 验证通过: 标记"已验证", 清除头块防止重启循环
 *   - 验证失败: 告警并清除头块, 防止重启卡在 OTA 检查
 *
 * SD 卡块布局 (每块 512 字节):
 *   Block 200:         OTA 镜像头 (ota_header_t + padding)
 *   Block 201..300:    OTA 镜像数据 (最多 100*512=50KB)
 *
 * 关于真实 OTA 的局限:
 *   STM32F407ZGT6 1MB Flash 无 bootloader, 应用从 0x08000000 启动
 *   无独立 bank 切换硬件, 实现"真 OTA" 需要:
 *     1. 自定义 bootloader (放在 Sector 0)
 *     2. 双 bank Flash 布局 (Sector 1~5 + Sector 6~11)
 *     3. 链接器脚本配置双套镜像地址
 *   此处只做"伪 OTA" 流程: 校验 + 标记, 不实际烧写到 Flash
 * ============================================================ */

/* 启动时检查 OTA 镜像
 * 返回:
 *    1 = 镜像存在且校验通过 (已标记, 已清除头块)
 *    0 = 无 OTA 镜像 (头块 magic 不匹配)
 *   -1 = 校验失败 (长度/CRC 错误, 已清除头块防止重启循环)
 */
int OTA_CheckPendingUpdate(void);

#endif /* OTA_H */
