// ============================================
// ALIMENTADOR DE MASCOTAS - Código completo
// PIR + LED + Buzzer + Servo + HC-SR04 + LCD
// ============================================

#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ── Pines ──────────────────────────────────
const int PIN_PIR       = 2;
const int TRIG_ALTO     = 3;
const int ECHO_ALTO     = 4;
const int TRIG_BAJO     = 5;
const int ECHO_BAJO     = 6;
const int PIN_BUZZ      = 7;
const int PIN_LED       = 8;
const int PIN_SERVO     = 9;
// LCD → A4 (SDA) · A5 (SCL) automático
// ───────────────────────────────────────────

Servo servo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── Variables ──────────────────────────────
bool pirAnterior    = false;
bool yaDispensado   = false;
long ultimaDeteccion = 0;
const int COOLDOWN  = 5000;   // ms entre detecciones

// Umbrales ultrasónico (ajusta según tu tolva)
const int DIST_LLENO = 5;    // cm — sensor detecta comida cerca
const int DIST_VACIO = 15;   // cm — sensor no detecta comida
// ───────────────────────────────────────────

void setup() {
  Serial.begin(9600);

  pinMode(PIN_PIR,   INPUT);
  pinMode(PIN_LED,   OUTPUT);
  pinMode(PIN_BUZZ,  OUTPUT);
  pinMode(TRIG_ALTO, OUTPUT);
  pinMode(ECHO_ALTO, INPUT);
  pinMode(TRIG_BAJO, OUTPUT);
  pinMode(ECHO_BAJO, INPUT);

  servo.attach(PIN_SERVO);
  servo.write(0);  // cerrado

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("PetFeeder v1.0");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...");
  delay(2000);
  lcd.clear();

  // Señal de arranque
  digitalWrite(PIN_LED, HIGH); delay(200);
  digitalWrite(PIN_LED, LOW);

  Serial.println("STATUS:listo");
  actualizarLCD();
}

void loop() {
  bool pirActual = digitalRead(PIN_PIR) == HIGH;
  long ahora     = millis();

  // ── Movimiento detectado ──
  if (pirActual && !pirAnterior && (ahora - ultimaDeteccion > COOLDOWN)) {
    ultimaDeteccion = ahora;
    yaDispensado    = false;
    Serial.println("PIR:1");
    alertaLED();
    pitido();

    if (!yaDispensado) {
      yaDispensado = true;
      dispensar();
    }

    Serial.println("LED:0");
    Serial.println("BUZZ:0");
  }

  // ── Movimiento terminó ──
  if (!pirActual && pirAnterior) {
    Serial.println("PIR:0");
    yaDispensado = false;
  }

  pirAnterior = pirActual;

  // ── Leer nivel de comida cada 2 seg ──
  static long ultimaLectura = 0;
  if (ahora - ultimaLectura > 2000) {
    ultimaLectura = ahora;
    leerNivel();
    actualizarLCD();
  }

  delay(100);
}

// ── Mide distancia de un HC-SR04 ──
long medirDistancia(int trig, int echo) {
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duracion = pulseIn(echo, HIGH, 30000);  // timeout 30ms
  return duracion * 0.034 / 2;  // convierte a cm
}

// ── Lee ambos sensores y manda estado ──
void leerNivel() {
  long distAlto = medirDistancia(TRIG_ALTO, ECHO_ALTO);
  long distBajo = medirDistancia(TRIG_BAJO, ECHO_BAJO);

  bool altoDetecta = (distAlto > 0 && distAlto < DIST_LLENO);
  bool bajoDetecta = (distBajo > 0 && distBajo < DIST_VACIO);

  if (altoDetecta && bajoDetecta) {
    Serial.println("NIVEL:lleno");
  } else if (!altoDetecta && bajoDetecta) {
    Serial.println("NIVEL:medio");
  } else {
    Serial.println("NIVEL:vacio");
    // Alerta sonora si no hay comida
    tone(PIN_BUZZ, 500, 100);
  }
}

// ── Abre y cierra el servo ──
void dispensar() {
  Serial.println("ALERTA:dispensando");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Dispensando...");

  servo.write(90);
  delay(2000);
  servo.write(0);

  Serial.println("SERVO:cerrado");
  actualizarLCD();
}

// ── Parpadeo LED x3 ──
void alertaLED() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED, HIGH);
    Serial.println("LED:1");
    delay(200);
    digitalWrite(PIN_LED, LOW);
    Serial.println("LED:0");
    delay(150);
  }
}

// ── Pitido con buzzer pasivo ──
void pitido() {
  tone(PIN_BUZZ, 1000, 150); delay(200);
  tone(PIN_BUZZ, 1500, 150); delay(200);
  tone(PIN_BUZZ, 2000, 150); delay(200);
  noTone(PIN_BUZZ);
  Serial.println("BUZZ:1");
  delay(50);
  Serial.println("BUZZ:0");
}

// ── Actualiza el LCD con estado actual ──
void actualizarLCD() {
  long distAlto = medirDistancia(TRIG_ALTO, ECHO_ALTO);
  long distBajo = medirDistancia(TRIG_BAJO, ECHO_BAJO);

  bool altoDetecta = (distAlto > 0 && distAlto < DIST_LLENO);
  bool bajoDetecta = (distBajo > 0 && distBajo < DIST_VACIO);

  String nivel;
  if (altoDetecta && bajoDetecta)       nivel = "LLENO ";
  else if (!altoDetecta && bajoDetecta) nivel = "MEDIO ";
  else                                   nivel = "VACIO!";

  bool hayMascota = digitalRead(PIN_PIR) == HIGH;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mascota:");
  lcd.print(hayMascota ? "SI " : "NO ");

  lcd.setCursor(0, 1);
  lcd.print("Comida:");
  lcd.print(nivel);
}
