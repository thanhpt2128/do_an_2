# Battery Management System - ESP32

Hệ thống quản lý và giám sát pin lithium-ion với kết nối IoT qua ThingsBoard.

## Tính năng chính

- **Giám sát thông số pin**: Điện áp, dòng điện, nhiệt độ
- **Tính toán trạng thái**: SOC (State of Charge), SOH (State of Health)
- **Điều khiển sạc/xả**: Tự động hoặc thủ công qua relay
- **Kết nối IoT**: Gửi dữ liệu lên ThingsBoard qua MQTT
- **Giao tiếp UART**: Nhận lệnh điều khiển

## Phần cứng

- **ESP32 DevKit**
- **INA219**: Đo điện áp và dòng điện 
- **DS18B20**: Cảm biến nhiệt độ 
- **Relay**: Điều khiển sạc (GPIO26) và xả (GPIO27)

## Cài đặt

1. Clone project và cài đặt ESP-IDF
2. Cấu hình WiFi và ThingsBoard trong [app_config.h](main/app_config.h):
   ```c
   #define WIFI_SSID           "your_wifi"
   #define WIFI_PASSWORD       "your_password"
   #define THINGSBOARD_SERVER  "your_server_ip"
   #define THINGSBOARD_TOKEN   "your_device_token"
   ```
3. Build và flash:
   ```bash
   idf.py build
   idf.py flash monitor
   ```

## Cấu trúc

- `main/`: Code chương trình chính (tasks, MQTT, cảm biến)
- `batlibs/`: Thư viện quản lý pin (SOC/SOH, điều khiển chế độ)
- `managed_components/`: Thư viện phụ thuộc (INA219, DS18B20, ThingsBoard, ArduinoJSON)

## Tài liệu
- [📄 Báo cáo đồ án](./PhamTruongThanh_20222675_doan2.pdf)
