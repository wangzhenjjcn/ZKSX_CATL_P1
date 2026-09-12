# ZKSX_CATL_P1

XIAO ESP32-S3 Sense 固件：压力传感器 UART 透传、PDM 麦克风、两个触点开关，三路数据复用 USB CDC。

## 接线

- 传感器 TX → D0 (GPIO1)，传感器 RX → D1 (GPIO2)，波特率 460800 8N1
- 麦克风：Sense 扩展板 GPIO42 CLK / GPIO41 DATA（不要剪 J1/J2）
- SW1 → D3 (GPIO4) 与 GND，SW2 → D4 (GPIO5) 与 GND，内部上拉，闭合为低电平
- UART 必须是 3.3V；5V TX 需要电平转换

## 编译烧录

```text
python -m pip install platformio
python -m platformio run -t upload
```

首次会下载 ESP32-S3 工具链，耗时较长。不要同时打开串口监视器。

## 主机解包

```text
pip install -r tools/requirements.txt
python tools/usb_mux_dump.py
python tools/usb_mux_dump.py COM5
python tools/usb_mux_dump.py COM5 --wav capture.wav --quiet-audio
```

完整对接说明见 [doc/串口数据协议.md](doc/串口数据协议.md)。USB 包：`A5 5A | type | len_le | payload | crc16_ccitt_le`（CRC 覆盖 type+len+payload）。

- `0x01` 传感器原始 70 字节（`FF 84 ...`）
- `0x02` 48 kHz / 16-bit 单声道 PCM，每块 960 sample（20 ms）
- `0x03` 开关，bit0=SW1，bit1=SW2，1=闭合；变化立即上报，200 ms 心跳
- `0x04` 计数：sensor_ok / checksum_fail / audio_dropped / usb_incomplete
