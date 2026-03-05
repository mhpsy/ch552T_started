# CH552T 在 Arch Linux 下烧录与调试记录（PlatformIO）

本文档记录本项目从“无法烧录”到“成功烧录”的完整排查过程，并给出可重复操作步骤。

## 1. 当前工程上传方式（已改为单次执行）

`platformio.ini` 已配置为一次性直刷，不再每次等待轮询：

```ini
upload_protocol = custom
upload_command = wchisp -u flash $SOURCE
```

说明：
- 该命令会直接调用 `wchisp` 烧录当前固件。
- 如果设备未处于 ISP 模式（55e0），会直接失败并退出（这是预期行为）。

---

## 2. 一次完整烧录流程

### 步骤 A：编译

```bash
pioact
platformio run -e ch552_raw
```

### 步骤 B：让板子进入 ISP 模式

按你的板卡方式操作（你已验证可行）：
- 按住 `P3.6` 条件
- 上电/插 USB
- 必要时触发复位

### 步骤 C：确认设备枚举为 ISP

```bash
lsusb | grep -Ei '4348:55e0|1a86:55e0'
```

看到类似输出说明已进入 ISP：
- `ID 4348:55e0 WinChipHead`

### 步骤 D：烧录

```bash
pioact
platformio run -e ch552_raw -t upload
```

成功关键日志示例：
- `Chip: CH552...`
- `Code flash xxxx bytes written`
- `SUCCESS`

---

## 3. 本次问题的根因与结论

### 3.1 不是编译问题

你已经可以稳定编译成功：
- `platformio run -e ch552_raw` 成功

### 3.2 早期失败点 1：上传协议不匹配

- 之前使用 `upload_protocol = ch55x`（`vnproch55x`）时，经常出现：
  - `Found no CH55x USB`
  - 或工具崩溃（segfault）
- 当前改为 `wchisp -u flash` 后更稳定。

### 3.3 早期失败点 2：权限问题（已解决）

现象：
- `wchisp -u info` 报 `no enough permission`

修复方式：

```bash
sudo cp ~/.platformio/packages/tool-vnproch55x/99-ch55xbl.rules /etc/udev/rules.d/99-ch55xbl.rules
sudo chmod 644 /etc/udev/rules.d/99-ch55xbl.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

然后重新插拔设备，再测：

```bash
wchisp -u info
```

如果能看到 `Chip: CH552 ... BTVER ...`，说明权限已修好。

---

## 4. 常见错误对照

### 错误：`Found no CH55x USB`

含义：上传器找不到目标设备。
排查：
1. 先确认是否是 `55e0`（不是 `1209:c550`）
2. 确认 USB 线是数据线
3. 进入 ISP 后再立即上传

### 错误：`no enough permission`

含义：设备存在，但当前用户无访问权限。
处理：按上面 udev 规则步骤执行，并重新插拔。

### 错误：`sudo: wchisp: command not found`

含义：`sudo` 环境没有继承用户 PATH。
可用方式：

```bash
sudo $(command -v wchisp) -u info
```

---

## 5. 建议的日常使用习惯

1. 先编译，再进 ISP，再上传（固定顺序）
2. 上传前先看 `lsusb` 是否为 `55e0`
3. 上传成功后再复位进应用模式测试
4. 若使用 Hub 不稳定，优先直连主板 USB 口

---

## 6. 参考命令速查

```bash
# 编译
pioact && platformio run -e ch552_raw

# 查看是否进入 ISP
lsusb | grep -Ei '4348:55e0|1a86:55e0'

# 查看芯片信息（验证权限+通信）
wchisp -u info

# 烧录
pioact && platformio run -e ch552_raw -t upload
```
