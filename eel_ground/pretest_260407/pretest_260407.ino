
#include <ESP32Servo.h>

// ===== Servo objects =====
Servo servoA;  // Head
Servo servoB;  // Pivot Front
Servo servoC;  // Pivot Rear

// ===== GPIO pins =====
const int pinA = 2;
const int pinB = 4;
const int pinC = 7;

// ===== Movement tables =====
int phase1Moves[][3] = {
  { 35, 145, 145},   // Move 1
  { 90, 90, 145},   // Move 2
  { 145, 35, 90},    // Move 3
  { 145, 35, 35},   // Move 4
  { 90, 90, 35},   // Move 5
  { 35, 145, 90}   // Move 6
};

// ===== Current servo positions =====
int currentA = 90;
int currentB = 90;
int currentC = 90;

// ===== Smooth motion parameters =====
int stepDelay = 1;   // milliseconds per step  (290 deg./s)
int stepSize  = 1;   // degrees per step

// ===== Utility: smooth move to target =====
void moveTo(int targetA, int targetB, int targetC) {
  while (currentA != targetA || currentB != targetB || currentC != targetC) {

    // --- A ---
    if (currentA < targetA) {
      currentA += stepSize;
      if (currentA > targetA) currentA = targetA;
    } else if (currentA > targetA) {
      currentA -= stepSize;
      if (currentA < targetA) currentA = targetA;
    }

    // --- B ---
    if (currentB < targetB) {
      currentB += stepSize;
      if (currentB > targetB) currentB = targetB;
    } else if (currentB > targetB) {
      currentB -= stepSize;
      if (currentB < targetB) currentB = targetB;
    }

    // --- C ---
    if (currentC < targetC) {
      currentC += stepSize;
      if (currentC > targetC) currentC = targetC;
    } else if (currentC > targetC) {
      currentC -= stepSize;
      if (currentC < targetC) currentC = targetC;
    }

    // Write positions to servos
    servoA.write(currentA);
    servoB.write(currentB);
    servoC.write(currentC);

//    delay(stepDelay);
  }
}

void setup() {
  servoA.attach(pinA);
  servoB.attach(pinB);
  servoC.attach(pinC);

  // Initialize all servos to 90 degrees
  servoA.write(90);
  servoB.write(90);
  servoC.write(90);
  delay(2000);
}

void loop() {

  // ===== Phase 1 =====
  for (int i = 0; i < sizeof(phase1Moves)/sizeof(phase1Moves[0]); i++) {
    moveTo(
      phase1Moves[i][0],
      phase1Moves[i][1],
      phase1Moves[i][2]
    );
    delay(300);
  }

}
