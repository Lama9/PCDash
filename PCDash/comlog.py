import serial
import serial.tools.list_ports
import datetime
import sys

def list_available_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("未检测到任何可用串口。")
        return []
    print("当前可用串口列表:")
    for p in ports:
        print(f"  - {p.device}: {p.description}")
    return [p.device for p in ports]

port = 'COM9'
baudrate = 115200

try:
    ser = serial.Serial(port, baudrate)
except serial.SerialException as e:
    print(f"错误：无法打开串口 {port}。原因: {e}\n")
    list_available_ports()
    sys.exit(1)

log_filename = f'esp_diag_{datetime.datetime.now():%Y%m%d_%H%M}.log'
print(f"成功打开串口 {port}，波特率 {baudrate}。数据将同时输出并保存至 {log_filename}")

with open(log_filename, 'w', encoding='utf-8') as f:
    try:
        while True:
            line = ser.readline().decode('utf-8', errors='replace')
            print(line, end='')
            f.write(line)
            f.flush()
    except KeyboardInterrupt:
        print("\n已手动停止监听。")
    finally:
        ser.close()