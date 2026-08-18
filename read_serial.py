import serial
import time
import sys

try:
    ser = serial.Serial('/dev/cu.usbmodem23201', 115200, timeout=1)
    print("Conectado. Leyendo por 10 segundos...")
    end_time = time.time() + 10
    with open('serial_log.txt', 'w') as f:
        while time.time() < end_time:
            line = ser.readline()
            if line:
                decoded = line.decode('utf-8', errors='replace').strip()
                print(decoded)
                f.write(decoded + '\n')
    ser.close()
    print("Fin lectura.")
except Exception as e:
    print("Error:", e)
