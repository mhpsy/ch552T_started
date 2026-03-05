# CH552T 当前问题总结（2026-03-05）

## 1. 目标

在 Arch Linux + PlatformIO 环境下，完成 CH552 固件烧录，并让 USB Vendor 固件稳定枚举，随后用 `tools/vendor_demo.py` 控制 GPIO（pin10/pin11）。

---

## 2. 当前现象

### 2.1 烧录链路

- 已可稳定进入 ISP 模式（`4348:55e0`）。
- `wchisp -u info` 可以正常读取芯片信息（CH552、BTVER 2.40）。
- `platformio run -t upload` 已可成功写入 flash。

结论：**烧录链路已打通**。

### 2.2 运行态 USB 枚举

烧录后进入应用模式时，系统日志出现：
- `device descriptor read/64, error -110`
- `device not accepting address ...`
- `unable to enumerate USB device`

同时 `lsusb` 常为空，或未出现预期运行态 VID/PID。

结论：**当前主要问题是“应用固件 USB 枚举失败/不稳定”，不是烧录失败**。

---

## 3. 已完成的排查与修复

### 3.1 工程与上传配置

- 自定义板卡已补齐（`boards/ch552t.json`）。
- 上传方式已改为一次性直刷：
  - `platformio.ini`
  - `upload_protocol = custom`
  - `upload_command = wchisp -u flash $SOURCE`

### 3.2 Linux 权限

- 已确认之前确实存在 udev 权限问题（`no enough permission`）。
- 修复后 `wchisp -u info` 可正常访问设备。

### 3.3 USB 固件修正（已做）

在 `src/usb_vendor.c` 已做以下修复：
1. 修正产品字符串描述符长度（从 `0x1E` 改为 `0x20`）。
2. 增加 USB 控制器复位/清 FIFO 初始化序列。
3. 将 EP0 DMA 缓冲区放到固定 xRAM 地址（`__xdata __at (0x0000)`）。

这些改动均已编译通过，但运行态枚举问题仍存在。

---

## 4. 当前结论（最重要）

1. **烧录成功 != USB 运行正常**。
2. 目前失败点在应用固件 USB 初始化/枚举阶段。
3. 板子与线材并非完全不可用（因为 ISP 模式可稳定识别）。
4. 仍需继续缩小问题：
   - 固件 USB 逻辑问题（优先）
   - 或运行态供电/Hub/线材时序敏感（次优先）

---

## 5. 建议下一步（按优先级）

### P1. 做“最小可枚举固件”

先去掉 Vendor 命令逻辑，仅保留最简 Device Descriptor + EP0 标准请求，目标是先稳定枚举。

若最小固件能稳定枚举，再逐步加回：
1. Vendor request 处理
2. GPIO 控制
3. 读状态接口

### P2. 物理层隔离验证

- 直连主板 USB 口（不经过 Hub）
- 更换短数据线
- 保持同一 USB 端口反复测试

### P3. 日志验证标准

- 成功标准：`lsusb` 持续看到运行态 VID/PID（非 `55e0`），无 `-110/-71`。
- 失败标准：频繁重新枚举、descriptor timeout、address reject。

---

## 6. 可复现命令记录

```bash
# 编译
pioact && platformio run -e ch552_raw

# 查看 ISP 是否可访问
wchisp -u info

# 烧录
pioact && platformio run -e ch552_raw -t upload

# 查看 USB 枚举状态
lsusb | grep -Ei '1234:5678|1209:c550|4348:55e0|1a86:55e0'
```

---

## 7. 当前状态标签

- [x] 编译可用
- [x] 烧录可用
- [ ] 运行态 USB 稳定枚举
- [ ] `vendor_demo.py` 端到端 GPIO 控制成功
