const int IN1 = 8;
const int IN2 = 9;
const int IN3 = 10;
const int IN4 = 11;

int stepDelay = 1;

void setStep(int a, int b, int c, int d) {
  digitalWrite(IN1, a);
  digitalWrite(IN2, b);
  digitalWrite(IN3, c);
  digitalWrite(IN4, d);
}

void stepForward() {
  setStep(1,0,1,0); delay(stepDelay);
  setStep(0,1,1,0); delay(stepDelay);
  setStep(0,1,0,1); delay(stepDelay);
  setStep(1,0,0,1); delay(stepDelay);
}
void stepBackward() {
  setStep(1,0,0,1); delay(stepDelay);
  setStep(0,1,0,1); delay(stepDelay);
  setStep(0,1,1,0); delay(stepDelay);
  setStep(1,0,1,0); delay(stepDelay);
}

void stopMotor() {
  setStep(0,0,0,0);
}

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
}
void loop() {
  // each stepForward/Backward call = 4 delays x 1ms = 4ms
  // 10000ms / 4ms = 2500 iterations
  for (int i = 0; i < 2500; i++) stepForward();
  stopMotor();
  delay(1000);

  for (int i = 0; i < 2500; i++) stepBackward();
  stopMotor();
  delay(1000);
}