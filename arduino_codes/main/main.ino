#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==========================================
// 1. CONFIGURATION & WIRING
// ==========================================
// Wiring Guide:
// YF-S201 Signal -> Voltage Divider -> GPIO 15
// OLED SDA       -> GPIO 21
// OLED SCL       -> GPIO 22
// OLED VCC       -> 3.3V (Recommended)
// Sensor VCC     -> 5V (From LM2596)

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define FLOW_SENSOR_PIN 15

// Initialize OLED display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==========================================
// 2. GLOBAL VARIABLES
// ==========================================
volatile int pulseCount = 0;
float flowRates[5];      // Buffer for 5 seconds of flow data
int pulseCounts[5];      // Buffer for raw pulses
int secondIndex = 0;     // Track current second in window (0-4)
unsigned long oldTime = 0;
float totalSessionVolume = 0; // Tracks volume for the current "Active" session

// ==========================================
// 3. INTERRUPT SERVICE ROUTINE
// ==========================================
void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

// ==========================================
// 4. MACHINE LEARNING LOGIC (The Brain)
// ==========================================
String predictActivity(float mean_flow, float max_flow, float std_flow, int pulses, int peak_pulses, float vol) {
  
  // --- A. MANUAL OVERRIDES FOR AIR TESTING ---
  // When blowing air, the sensor spins VERY fast (>12 L/min).
  // Real water showers are usually 6-12 L/min.
  // We classify BOTH as "Shower".
  if (mean_flow > 12.0) {
    return "Shower";
  }

  // --- B. STANDARD LOGIC ---
  
  // 1. Idle & Leak Check
  // If flow is tiny (<= 0.8 L/min)
  if (mean_flow <= 0.8) {
    // If it's basically zero, it's Idle
    if (mean_flow < 0.2) return "Idle";
    // If it's small but persistent, it's a Leak
    return "Leak";
  }

  // 2. Strong Flow Check (> 5.0 L/min)
  if (mean_flow > 5.0) {
    // If flow is steady (Low Std Dev) OR Volume is high (long duration)
    // It's a Shower.
    if (std_flow < 2.5 || vol > 2.0) {
      return "Shower";
    }
    // If it's strong but very shaky, it's likely a Tap (filling a bucket/pot)
    return "Tap";
  }

  // 3. Toilet Check
  // Toilets are usually short bursts (Low Volume < 6L total)
  // They have a "Peak" (flush start) and then drop.
  if (vol < 6.0 && peak_pulses > 20) {
     return "Toilet";
  }

  // 4. Default Case
  // Hand washing, brushing teeth, doing dishes usually fall here.
  return "Tap";
}

// ==========================================
// 5. SETUP FUNCTION
// ==========================================
void setup() {
  Serial.begin(115200);

  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  
  // Show Start Screen
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 10);
  display.println("Smart Water Monitor");
  display.setCursor(30, 30);
  display.println("Starting...");
  display.display();
  delay(2000);

  // Initialize Sensor Pin
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);
  
  oldTime = millis();
}

// ==========================================
// 6. MAIN LOOP
// ==========================================
void loop() {
  // Execute logic every 1 second
  if ((millis() - oldTime) > 1000) {
    
    // --- Step 1: Read Sensor safely ---
    detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN));
    
    // Calculate Flow Rate (L/min) for this specific second
    // Formula: (Pulse frequency * 60) / 7.5
    float currentFlow = ((1000.0 / (millis() - oldTime)) * pulseCount) / 7.5;
    
    // Store in buffers
    flowRates[secondIndex] = currentFlow;
    pulseCounts[secondIndex] = pulseCount;
    
    // Accumulate Volume
    if (currentFlow > 0.1) {
      totalSessionVolume += currentFlow / 60.0; // Add Liters
    }

    // Reset counters for next second
    pulseCount = 0;
    oldTime = millis();
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);
    
    // Move to next slot in buffer
    secondIndex++;

    // --- Step 2: Process Window (Every 5 seconds) ---
    if (secondIndex >= 5) {
      processWindow();
      secondIndex = 0; // Reset window index
    }
  }
}

// ==========================================
// 7. DATA PROCESSING & DISPLAY
// ==========================================
void processWindow() {
  // A. Calculate Features
  float sumFlow = 0;
  float maxFlow = 0;
  int totalPulses = 0;
  int peakPulses = 0;
  float sumSq = 0;

  for (int i = 0; i < 5; i++) {
    sumFlow += flowRates[i];
    if (flowRates[i] > maxFlow) maxFlow = flowRates[i];
    
    totalPulses += pulseCounts[i];
    if (pulseCounts[i] > peakPulses) peakPulses = pulseCounts[i];
  }

  float meanFlow = sumFlow / 5.0;
  float volume = totalPulses / 450.0; // Approx Liters in this 5s window
  
  // Calculate Standard Deviation (Steadiness)
  for (int i = 0; i < 5; i++) {
    sumSq += pow(flowRates[i] - meanFlow, 2);
  }
  float stdFlow = sqrt(sumSq / 5.0);

  // B. Run Prediction
  String activity = predictActivity(meanFlow, maxFlow, stdFlow, totalPulses, peakPulses, volume);

  // Reset Session Volume if IDLE
  if (activity == "Idle") {
    totalSessionVolume = 0;
  }

  // C. Debug to Serial Monitor (Helps you see what's happening)
  Serial.print("Predicted: "); Serial.print(activity);
  Serial.print(" | Rate: "); Serial.print(meanFlow);
  Serial.print(" L/m | Vol: "); Serial.println(totalSessionVolume);

  // D. Update OLED
  updateDisplay(activity, meanFlow);
}

void updateDisplay(String activity, float currentRate) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  if (activity == "Idle") {
    // --- IDLE SCREEN ---
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Status: Monitoring");
    
    display.setTextSize(2);
    display.setCursor(40, 25);
    display.println("IDLE");
    
    display.setTextSize(1);
    display.setCursor(20, 50);
    display.println("No flow detected");
    
  } else if (activity == "Leak") {
    // --- LEAK ALERT SCREEN ---
    display.fillScreen(SSD1306_WHITE); // Flash White
    display.setTextColor(SSD1306_BLACK); // Black Text
    
    display.setTextSize(2);
    display.setCursor(30, 10);
    display.println("ALERT!");
    
    display.setCursor(10, 35);
    display.println("LEAK DETECTED");
    
  } else {
    // --- ACTIVE USAGE SCREEN (Shower, Tap, Toilet) ---
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Activity: "); 
    
    // Display Activity Name Big
    display.setTextSize(2);
    display.setCursor(0, 15);
    display.println(activity); 
    
    // Display Stats
    display.setTextSize(1);
    display.setCursor(0, 35);
    display.print("Rate: "); 
    display.print(currentRate); 
    display.println(" L/min");

    display.setCursor(0, 48);
    display.print("Total: "); 
    display.print(totalSessionVolume); 
    display.println(" L");
    
    // Progress Bar at bottom
    // Max bar width = 128 pixels. Maps 0-15 L/min to 0-128 width.
    int barWidth = map(constrain(currentRate, 0, 15), 0, 15, 0, 128);
    display.fillRect(0, 60, barWidth, 4, SSD1306_WHITE);
  }
  
  display.display();
}
