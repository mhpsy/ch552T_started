#!/bin/bash

# 配置信息：使用你提供的特定 Commit 哈希值
COMMIT_HASH="6d7aa8f351877eb709484b30cac5af3f7a375b2d"
RAW_BASE_URL="https://raw.githubusercontent.com/Blinkinlabs/ch554_sdcc/$COMMIT_HASH/include"
TARGET_DIR="./include"

# 文件列表：根据你之前提供的库文件结构
FILES=(
    "adc.c" "adc.h" "bootloader.h" "ch554.h" "ch554_datatypes.h"
    "ch554_usb.h" "debug.c" "debug.h" "i2c.c" "i2c.h"
    "pwm.h" "spi.c" "spi.h" "touchkey.c" "touchkey.h"
)

# 创建目标文件夹
mkdir -p "$TARGET_DIR"

echo "------------------------------------------"
echo "🚀 开始从 GitHub 拉取 CH554/552 核心驱动文件..."
echo "------------------------------------------"

for FILE in "${FILES[@]}"; do
    echo "正在下载: $FILE ..."
    # -q: 静默模式  -O: 指定保存路径
    wget -q -O "$TARGET_DIR/$FILE" "$RAW_BASE_URL/$FILE"
    
    if [ $? -eq 0 ]; then
        echo "  ✅ [成功]"
    else
        echo "  ❌ [失败] 请检查网络连接"
    fi
done

echo "------------------------------------------"
echo "🎉 任务完成！文件已存入: $TARGET_DIR"
ls -F "$TARGET_DIR"