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
bool pirAnterior  = false;
bool cicloActivo  = false;   // true mientras se procesa una detección, hasta que el PIR baje a LOW

// Umbrales ultrasónico (ajusta según tu tolva)
const int DIST_LLENO = 5;    // cm — sensor detecta comida cerca
const int DIST_VACIO = 15;   // cm — sensor no detecta comida

// Control manual desde el dashboard
bool ledManual  = false;   // true = encendido manualmente desde dashboard
bool buzzManual = false;

// Última lectura de los sensores (evita medir 2 veces por ciclo)
long ultDistAlto = 999;
long ultDistBajo = 999;

// Estado estable del PIR para mostrar en LCD (no lee el pin crudo)
bool pirEstable = false;
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

  // El servo se conecta (attach) solo cuando se va a mover,
  // y se desconecta (detach) después — así no interfiere con el I2C del LCD

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
  // ── Escucha comandos del dashboard (Python) ──
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    procesarComando(cmd);
  }

  bool pirActual = digitalRead(PIN_PIR) == HIGH;
  long ahora     = millis();

  // ── Movimiento detectado: solo si venía de LOW y no estamos ya procesando ──
  if (pirActual && !pirAnterior && !cicloActivo) {
    cicloActivo  = true;     // bloquea cualquier otro disparo hasta que el PIR baje a LOW
    pirEstable   = true;
    Serial.println("PIR:1");

    // Verifica nivel de comida antes de dispensar
    long distBajo  = medirDistancia(TRIG_BAJO, ECHO_BAJO);
    bool hayComida = (distBajo > 0 && distBajo < DIST_VACIO);

    if (!hayComida) {
      // Sin comida — avisa pero no dispensa
      Serial.println("ALERTA:sincomida");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Sin comida!");
      lcd.setCursor(0, 1);
      lcd.print("Recarga pronto");
      tone(PIN_BUZZ, 500, 200); delay(300);
      tone(PIN_BUZZ, 500, 200); delay(300);
      noTone(PIN_BUZZ);
      delay(2000);
      actualizarLCD();
    } else {
      // Hay comida — procede normal
      alertaLED();
      pitido();
      dispensar();
      Serial.println("LED:0");
      Serial.println("BUZZ:0");
    }
  }

  // ── El PIR bajó a LOW: recién aquí se libera el bloqueo ──
  if (!pirActual && pirAnterior) {
    Serial.println("PIR:0");
    pirEstable  = false;
    cicloActivo = false;     // ya puede volver a detectar
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

// ── Procesa comandos manuales del dashboard ──
void procesarComando(String cmd) {
  if (cmd == "LED_ON") {
    ledManual = true;
    digitalWrite(PIN_LED, HIGH);
    Serial.println("LED:1");
  }
  else if (cmd == "LED_OFF") {
    ledManual = false;
    digitalWrite(PIN_LED, LOW);
    Serial.println("LED:0");
  }
  else if (cmd == "BUZZ_ON") {
    buzzManual = true;
    tone(PIN_BUZZ, 1000);   // tono continuo
    Serial.println("BUZZ:1");
  }
  else if (cmd == "BUZZ_OFF") {
    buzzManual = false;
    noTone(PIN_BUZZ);
    Serial.println("BUZZ:0");
  }
}

// ── Mide distancia de un HC-SR04 ──
long medirDistancia(int trig, int echo) {
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duracion = pulseIn(echo, HIGH, 30000);  // timeout 30ms
  return duracion * 0.034 / 2;  // convierte a cm
}

// ── Lee ambos sensores, guarda el resultado y manda estado por Serial ──
void leerNivel() {
  ultDistAlto = medirDistancia(TRIG_ALTO, ECHO_ALTO);
  ultDistBajo = medirDistancia(TRIG_BAJO, ECHO_BAJO);

  bool altoDetecta = (ultDistAlto > 0 && ultDistAlto < DIST_LLENO);
  bool bajoDetecta = (ultDistBajo > 0 && ultDistBajo < DIST_VACIO);

  if (altoDetecta && bajoDetecta) {
    Serial.println("NIVEL:lleno");
  } else if (!altoDetecta && bajoDetecta) {
    Serial.println("NIVEL:medio");
  } else {
    Serial.println("NIVEL:vacio");
  }
}

// ── Abre y cierra el servo (attach/detach para no interferir con I2C) ──
void dispensar() {
  Serial.println("ALERTA:dispensando");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Dispensando...");
  lcd.setCursor(0, 1);
  lcd.print("Espera...");

  servo.attach(PIN_SERVO);
  servo.write(90);
  delay(2000);
  servo.write(0);
  delay(500);          // tiempo para que el servo termine de moverse
  servo.detach();      // libera Timer1 — el LCD vuelve a la normalidad

  Serial.println("SERVO:cerrado");
  actualizarLCD();
}

// ── Parpadeo LED x3 ──
void alertaLED() {
  if (ledManual) return;   // no interrumpe si está encendido manualmente
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
  if (buzzManual) return;   // no interrumpe si está sonando manualmente
  tone(PIN_BUZZ, 1000, 150); delay(200);
  tone(PIN_BUZZ, 1500, 150); delay(200);
  tone(PIN_BUZZ, 2000, 150); delay(200);
  noTone(PIN_BUZZ);
  Serial.println("BUZZ:1");
  delay(50);
  Serial.println("BUZZ:0");
}

// ── Actualiza el LCD usando las últimas lecturas (sin medir de nuevo) ──
void actualizarLCD() {
  bool altoDetecta = (ultDistAlto > 0 && ultDistAlto < DIST_LLENO);
  bool bajoDetecta = (ultDistBajo > 0 && ultDistBajo < DIST_VACIO);

  String nivel;
  if (altoDetecta && bajoDetecta)       nivel = "LLENO ";
  else if (!altoDetecta && bajoDetecta) nivel = "MEDIO ";
  else                                   nivel = "VACIO!";

  lcd.setCursor(0, 0);
  lcd.print("Mascota:");
  lcd.print(pirEstable ? "SI  " : "NO  ");

  lcd.setCursor(0, 1);
  lcd.print("Comida:");
  lcd.print(nivel);
  lcd.print("  ");
}
