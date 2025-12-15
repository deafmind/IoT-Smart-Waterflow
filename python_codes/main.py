import csv
import serial
import time
import os

def main():
    arduino_port = "COM8" 
    baud = 115200 
    
    # This is the base name we want
    base_filename = "esp32-data.csv"
    
    sensor_data = [] 
    
    # --- CONNECTION BLOCK ---
    try:
        ser = serial.Serial(arduino_port, baud, timeout=1)
        print(f"Connected to ESP32 on {arduino_port}")
        time.sleep(2) 
    except serial.SerialException:
        print(f"Could not connect to {arduino_port}. Check your USB cable and drivers.")
        return

    # --- DATA COLLECTION BLOCK ---
    samples = 5
    line = 0
    print("Starting data collection...")

    while line < samples:
        if ser.in_waiting > 0:
            try:
                getData = ser.readline()
                dataString = getData.decode('utf-8').strip()
                
                if dataString:
                    readings = dataString.split(",")
                    print(f"Reading {line+1}: {readings}")
                    sensor_data.append(readings)
                    line += 1
            except UnicodeDecodeError:
                print("Decode error - skipping line")
            except KeyboardInterrupt:
                print("Stopping early...")
                break

    ser.close() # Close connection now that we have the data

    # --- UNIQUE FILENAME LOGIC (The part you asked for) ---
    final_filename = base_filename
    counter = 1

    # Check if file exists. If yes, keep adding numbers until we find a free one.
    while os.path.exists(final_filename):
        # Splits "esp32-data.csv" into "esp32-data" and ".csv"
        name, ext = os.path.splitext(base_filename)
        # Creates "esp32-data-1.csv", then "esp32-data-2.csv", etc.
        final_filename = f"{name}-{counter}{ext}"
        counter += 1

    # --- SAVE TO FILE ---
    print(f"Saving data to: {final_filename}")
    
    try:
        with open(final_filename, 'w', encoding='UTF8', newline='') as f:
            writer = csv.writer(f)
            writer.writerows(sensor_data)
        print("File saved successfully!")
    except Exception as e:
        print(f"Error saving file: {e}")

if __name__ == "__main__":
    main()
