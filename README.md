# CH552T_started

1. 大概率会只用Vendor模式
   1. 协议全部自定义,包大小自定义,除了不免驱
   2. 不会收到HID这种数据包和轮训通讯的限制,拼接数据会很麻烦
2. 当前的构建中 build_src_filter 的方式去掉了 hid 的文件
   1. 需要的话注释掉就好
3. 高电平是5V


## 初始化和使用开发工具

### init
```bash
rm -rf .venv
python -m venv .venv
source .venv/bin/activate
pip install pyusb
```

### use
```bash
python ./tools/vendor_demo.py status
python ./tools/vendor_demo.py write p11 1
python ./tools/vendor_demo.py read p11 1

```

## 针脚的定义

不要操作这两个针脚

P36 is D-
P37 is D+

```c
/*
 * Pin ID encoding: high nibble = port number, low nibble = bit number
 * CH552T TSSOP-20 available GPIO pins:
 *
 *   Port 1 (8 pins)              Port 3 (6 pins, P3.6/P3.7 = USB)
 *   P1.0  pin 2   T2/CAP1        P3.0  pin 15  RXD/PWM1_
 *   P1.1  pin 3   T2EX/CAP2/AIN0 P3.1  pin 16  TXD/PWM2_
 *   P1.2  pin 4   RXD_            P3.2  pin 17  INT0/TXD1_/AIN3
 *   P1.3  pin 5   TXD_            P3.3  pin 18  INT1
 *   P1.4  pin 6   SCS/AIN1        P3.4  pin 19  PWM2/RXD1_/T0
 *   P1.5  pin 7   MOSI/PWM1/AIN2  P3.5  pin 20  T1
 *   P1.6  pin 8   MISO/RXD1
 *   P1.7  pin 9   SCK/TXD1
 */
#define PIN_P10  0x10
#define PIN_P11  0x11
#define PIN_P12  0x12
#define PIN_P13  0x13
#define PIN_P14  0x14
#define PIN_P15  0x15
#define PIN_P16  0x16
#define PIN_P17  0x17
#define PIN_P30  0x30
#define PIN_P31  0x31
#define PIN_P32  0x32
#define PIN_P33  0x33
#define PIN_P34  0x34
#define PIN_P35  0x35

#define VND_REQ_GPIO_WRITE    0x01  /* wValueL = pin_id, wIndexL = level */
#define VND_REQ_GPIO_READ     0x02  /* wValueL = pin_id, returns 1 byte  */
#define VND_REQ_GPIO_READ_ALL 0x03  /* returns 2 bytes: [P1, P3]         */
```
