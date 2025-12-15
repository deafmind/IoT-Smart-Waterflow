#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>

// ==========================================
// 1. CONFIGURATION
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define FLOW_SENSOR_PIN 15

// --- GSM SETTINGS ---

const String PHONE_NUMBER = "+989137999526"; 

// Wiring: SIM800C TX -> GPIO 16, SIM800C RX -> GPIO 17
#define RX_PIN 16
#define TX_PIN 17

// Initialization
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
HardwareSerial gsmSerial(2); // Use UART2 for GSM

// ==========================================
// 2. GLOBAL VARIABLES
// ==========================================
volatile int pulseCount = 0;
float flowRates[5];      
int pulseCounts[5];      
int secondIndex = 0;
unsigned long oldTime = 0;
float totalSessionVolume = 0;

// SMS State Machine
bool leakNotificationSent = false;
unsigned long lastSmsTime = 0;

// ==========================================
// 3. INTERRUPT SERVICE ROUTINE
// ==========================================
void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

// ==========================================
// 4. GSM FUNCTIONS
// ==========================================
void sendSMS(String message) {
  Serial.println("Sending SMS...");
  
  gsmSerial.println("AT+CMGF=1"); // Set SMS mode to Text
  delay(1000);
  
  gsmSerial.print("AT+CMGS=\"");
  gsmSerial.print(PHONE_NUMBER);
  gsmSerial.println("\"");
  delay(1000);
  
  gsmSerial.print(message);
  delay(100);
  
  gsmSerial.write(26); // ASCII code of CTRL+Z to send
  delay(1000);
  
  Serial.println("SMS Sent Request Completed.");
}

void initGSM() {
  gsmSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(1000);
  Serial.println("Initializing SIM800C...");
  gsmSerial.println("AT"); // Handshake
  delay(1000);
  gsmSerial.println("AT+CMGF=1"); // Text Mode
  delay(1000);
  gsmSerial.println("AT+CNMI=2,2,0,0,0"); // Receive handling
  delay(1000);
  Serial.println("GSM Ready.");
}

// ==========================================
// 5. MACHINE LEARNING LOGIC
// ==========================================
String predictActivity(float mean_flow, float max_flow, float std_flow, int pulses, int peak_pulses, float vol) {
  
  // A. MANUAL OVERRIDES FOR AIR TESTING (HIGH SPEED)
  if (mean_flow > 12.0) return "Shower";

  // B. STANDARD LOGIC
  
  // 1. Idle & Leak Check
  if (mean_flow <= 0.8) {
    if (mean_flow < 0.2) return "Idle";
    return "Leak";
  }

  // 2. Strong Flow Check
  if (mean_flow > 5.0) {
    if (std_flow < 2.5 || vol > 2.0) return "Shower";
    return "Tap";
  }

  // 3. Toilet Check
  if (vol < 6.0 && peak_pulses > 20) return "Toilet";

  // 4. Default
  return "Tap";
}

// ==========================================
// 6. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  // Init GSM
  initGSM();

  // Init OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("System Initializing...");
  display.setCursor(0, 30);
  display.println("GSM: Connected");
  display.display();
  delay(2000);

  // Init Sensor
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);
  
  oldTime = millis();
}

// ==========================================
// 7. MAIN LOOP
// ==========================================
void loop() {
  if ((millis() - oldTime) > 1000) {
    
    // --- Step 1: Read Sensor ---
    detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN));
    
    float currentFlow = ((1000.0 / (millis() - oldTime)) * pulseCount) / 7.5;
    
    flowRates[secondIndex] = currentFlow;
    pulseCounts[secondIndex] = pulseCount;
    
    if (currentFlow > 0.1) {
      totalSessionVolume += currentFlow / 60.0; 
    }

    pulseCount = 0;
    oldTime = millis();
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);
    
    secondIndex++;

    // --- Step 2: Process Window (Every 5s) ---
    if (secondIndex >= 5) {
      processWindow();
      secondIndex = 0; 
    }
  }
  
  // Optional: Read GSM responses for debugging
  while(gsmSerial.available()) {
    Serial.write(gsmSerial.read());
  }
}

// ==========================================
// 8. DATA PROCESSING & DISPLAY
// ==========================================
void processWindow() {
  // A. Features
  float sumFlow = 0; float maxFlow = 0; int totalPulses = 0; int peakPulses = 0; float sumSq = 0;

  for (int i = 0; i < 5; i++) {
    sumFlow += flowRates[i];
    if (flowRates[i] > maxFlow) maxFlow = flowRates[i];
    totalPulses += pulseCounts[i];
    if (pulseCounts[i] > peakPulses) peakPulses = pulseCounts[i];
  }

  float meanFlow = sumFlow / 5.0;
  float volume = totalPulses / 450.0;
  
  for (int i = 0; i < 5; i++) sumSq += pow(flowRates[i] - meanFlow, 2);
  float stdFlow = sqrt(sumSq / 5.0);

  // B. Prediction
  String activity = predictActivity(meanFlow, maxFlow, stdFlow, totalPulses, peakPulses, volume);

  // C. SMS Logic (Leak Detection)
  if (activity == "Leak") {
    // Only send if we haven't sent one recently (e.g., in this session)
    if (!leakNotificationSent) {
      String msg = "نشتی اب";
      sendSMS(msg);
      leakNotificationSent = true; // Block future SMS until system resets
    }
  } else if (activity == "Idle") {
    // Reset the flag only when the leak stops and system goes Idle
    leakNotificationSent = false;
    totalSessionVolume = 0;
  }

  // D. Update OLED
  updateDisplay(activity, meanFlow);
  
  // Debug
  Serial.print("Class: "); Serial.println(activity);
}

void updateDisplay(String activity, float currentRate) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  if (activity == "Idle") {
    display.setTextSize(1);
    display.setCursor(0, 0); display.println("System Active");
    display.setTextSize(2);
    display.setCursor(40, 25); display.println("IDLE");
    
  } else if (activity == "Leak") {
    display.fillScreen(SSD1306_WHITE); 
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(2);
    display.setCursor(30, 10); display.println("ALERT!");
    display.setCursor(10, 35); display.println("LEAK DETECTED");
    if (leakNotificationSent) {
       display.setTextSize(1);
       display.setCursor(20, 55); display.println("SMS SENT");
    }
    
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0); display.print("Activity: "); 
    display.setTextSize(2);
    display.setCursor(0, 15); display.println(activity); 
    
    display.setTextSize(1);
    display.setCursor(0, 35);
    display.print("Rate: "); display.print(currentRate); display.println(" L/min");

    display.setCursor(0, 48);
    display.print("Total: "); display.print(totalSessionVolume); display.println(" L");
    
    int barWidth = map(constrain(currentRate, 0, 15), 0, 15, 0, 128);
    display.fillRect(0, 60, barWidth, 4, SSD1306_WHITE);
  }
  display.display();
}
