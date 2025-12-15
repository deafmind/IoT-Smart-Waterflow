#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==========================================
// 1. CONFIGURATION
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define FLOW_SENSOR_PIN 15  // Ensure this matches your Yellow Wire!

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==========================================
// 2. GLOBAL VARIABLES
// ==========================================
volatile int pulseCount = 0;
volatile unsigned long lastPulseTime = 0; // For noise filtering

float flowRates[5]; 
int pulseCounts[5]; 
int secondIndex = 0;
unsigned long oldTime = 0;
float totalVolume = 0.0; // Cumulative volume for display

// ==========================================
// 3. NOISE-FILTERED INTERRUPT
// ==========================================
void IRAM_ATTR pulseCounter() {
  unsigned long currentTime = micros();
  // Debounce/Filter: Ignore pulses that happen closer than 2ms apart (500Hz limit)
  // Real water flow won't pulse faster than this.
  if ((currentTime - lastPulseTime) > 2000) { 
    pulseCount++;
    lastPulseTime = currentTime;
  }
}

// ==========================================
// 4. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  // Init OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 failed"));
    for(;;);
  }
  display.clearDisplay();
  
  // Show "Ready" Screen
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Data Gathering Mode");
  display.setCursor(0,25);
  display.println("Waiting for flow...");
  display.display();
  delay(1000);

  // Init Sensor
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);

  // Print CSV Header
  Serial.println("mean_flow,max_flow,std_flow,total_pulses,peak_pulses,duty_cycle,volume,slope");
}

// ==========================================
// 5. MAIN LOOP
// ==========================================
void loop() {
  if ((millis() - oldTime) > 1000) {
    detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN));
    
    // --- CALCULATION ---
    // Flow Rate (L/min) = (Pulses * 60) / 7.5
    float currentFlow = ((1000.0 / (millis() - oldTime)) * pulseCount) / 7.5;
    
    // Volume (Liters) this second
    float volumeThisSecond = currentFlow / 60.0;
    
    // Threshold filter: Ignore tiny floating values < 0.1
    if (currentFlow > 0.1) {
       totalVolume += volumeThisSecond;
    } else {
       currentFlow = 0.0; // Force to 0 for display cleaner look
    }

    // --- OLED DISPLAY ---
    display.clearDisplay();
    
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("DATA COLLECTION");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    
    // Flow Rate
    display.setCursor(0, 20);
    display.print("Flow: ");
    display.setTextSize(2);
    display.print(currentFlow);
    display.setTextSize(1);
    display.println(" L/m");
    
    // Total Volume
    display.setCursor(0, 45);
    display.print("Total: "); 
    display.print(totalVolume); 
    display.println(" L");
    
    // Progress Indicator (Window 1-5)
    display.drawRect(0, 60, (secondIndex+1)*25, 3, SSD1306_WHITE);
    
    display.display();

    // --- DATA LOGGING ---
    flowRates[secondIndex] = currentFlow;
    pulseCounts[secondIndex] = pulseCount;
    
    // Reset
    pulseCount = 0;
    oldTime = millis();
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);
    
    secondIndex++;

    // Process Window every 5 seconds
    if (secondIndex >= 5) {
      processWindow();
      secondIndex = 0; 
    }
  }
}

// ==========================================
// 6. CSV FEATURE EXTRACTION
// ==========================================
void processWindow() {
  float sumFlow = 0;
  float maxFlow = 0;
  float sumSqDiff = 0;
  int totalPulses = 0;
  int peakPulses = 0;
  int activeSeconds = 0;

  for (int i = 0; i < 5; i++) {
    sumFlow += flowRates[i];
    if (flowRates[i] > maxFlow) maxFlow = flowRates[i];
    totalPulses += pulseCounts[i];
    if (pulseCounts[i] > peakPulses) peakPulses = pulseCounts[i];
    if (flowRates[i] > 0.1) activeSeconds++;
  }
  
  float meanFlow = sumFlow / 5.0;
  float dutyCycle = activeSeconds / 5.0;
  float volume = totalPulses / 450.0; 
  
  for (int i = 0; i < 5; i++) {
    sumSqDiff += pow(flowRates[i] - meanFlow, 2);
  }
  float stdFlow = sqrt(sumSqDiff / 5.0);
  
  float slope = (flowRates[4] - flowRates[0]) / 5.0;

  // Print CSV Line
  Serial.print(meanFlow); Serial.print(",");
  Serial.print(maxFlow); Serial.print(",");
  Serial.print(stdFlow); Serial.print(",");
  Serial.print(totalPulses); Serial.print(",");
  Serial.print(peakPulses); Serial.print(",");
  Serial.print(dutyCycle); Serial.print(",");
  Serial.print(volume, 4); Serial.print(",");
  Serial.println(slope);
}
