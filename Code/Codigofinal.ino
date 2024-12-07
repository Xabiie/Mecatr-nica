#include <SPI.h>
#include <MFRC522.h>
#include <Stepper.h>

// Configuración del motor paso a paso
const int stepsPerRevolution = 2048; // Pasos por revolución del motor 28BYJ-48
const int stepsPerQuarterTurn = 512; // Pasos necesarios para girar 90° (1/4 de revolución)
Stepper myStepper(stepsPerRevolution, 2, 3, 4, 5); // Pines IN1, IN2, IN3, IN4

// Configuración del sensor RFID
#define SS_PIN 10
#define RST_PIN 9
MFRC522 rfid(SS_PIN, RST_PIN); // Crear una instancia para el lector RFID

// UID de la tarjeta permitida (1D 58 03 32)
byte allowedUID[] = {0x1D, 0x58, 0x03, 0x32};

// Configuración del botón
const int buttonPin = 6;  // El botón está conectado al pin D6
int buttonState = 0;       // Variable para almacenar el estado del botón
int lastButtonState = 0;   // Variable para almacenar el último estado del botón

// Variables de control del tiempo
unsigned long startTime = 0; // Tiempo de inicio del ciclo
int currentSection = 0; // Sección actual (0 = 0°, 1 = 90°, 2 = 180°, 3 = 270°)
int lastSection = 0; // Almacena la sección antes de la retirada de la tarjeta
bool cardDetected = false; // Estado de la tarjeta (cerca o no)
bool cardJustRemoved = false; // Indica si la tarjeta fue retirada completamente
bool buttonPressed = false; // Indica si el botón fue presionado

// Temporizador para confirmar la retirada de la tarjeta
unsigned long cardRemoveStartTime = 0;
const unsigned long cardRemoveDelay = 4000; // 7 segundos para confirmar la ausencia de la tarjeta

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init(); // Inicializar el lector RFID
  myStepper.setSpeed(10); // Configura la velocidad del motor

  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH); // Desactiva el sensor RFID al inicio

  pinMode(buttonPin, INPUT);  // Configura el botón como entrada con pull-up

  Serial.println("Sistema de dispensador de comida listo.");
  startTime = millis(); // Inicia el contador de tiempo
}

void loop() {
  // Verificar el estado de la tarjeta RFID
  activarRFID();
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    if (isAllowedCard()) {
      if (!cardDetected) {
        Serial.println("Tarjeta permitida detectada. Moviendo a la primera sección (0°).");
        moveToSection(0); // Mover a la primera sección (0°)
        lastSection = currentSection; // Guardamos la sección antes de la tarjeta
        cardDetected = true; // Marca que la tarjeta está cerca
        cardJustRemoved = false; // Resetea el estado de retirada
      }
      cardRemoveStartTime = 0; // Reinicia el temporizador de retirada porque la tarjeta está cerca
    }
    rfid.PICC_HaltA(); // Detener la comunicación con la tarjeta
  } else {
    // Si no detecta la tarjeta y estaba previamente detectada
    if (cardDetected && cardRemoveStartTime == 0) {
      cardRemoveStartTime = millis(); // Inicia el temporizador para confirmar retirada
      Serial.println("Iniciando temporizador para confirmar la retirada de la tarjeta...");
    }

    // Verificar si han pasado los 7 segundos de confirmación sin detectar la tarjeta
    if (cardDetected && cardRemoveStartTime > 0 && (millis() - cardRemoveStartTime >= cardRemoveDelay)) {
      cardDetected = false; // Confirma que la tarjeta fue retirada
      cardJustRemoved = true;
      Serial.println("Tarjeta retirada confirmada. Volviendo a la posición correspondiente.");
      moveToSection(lastSection); // Regresa a la sección donde estaba antes de retirar la tarjeta
      cardRemoveStartTime = 0; // Reinicia el temporizador
    }
  }
  desactivarRFID();

  // Solo actualiza la posición basada en el tiempo si la tarjeta ha sido retirada
  if (cardJustRemoved && !cardDetected) {
    updateSectionByTime(); // Actualiza la sección basada en el tiempo
    cardJustRemoved = false; // Reinicia el estado de retirada para futuras detecciones
  }

  // Si no hay tarjeta detectada ni recientemente retirada, ajusta la posición con el tiempo
  if (!cardDetected && !cardJustRemoved) {
    updateSectionByTime(); // Mueve el motor según el ciclo de tiempo
  }

  // Detectar cuando el botón es presionado
  buttonState = digitalRead(buttonPin);

  // Si el botón es presionado, mover el motor físicamente sin actualizar la sección lógica
  if (buttonState == LOW && lastButtonState == HIGH) {  // El botón se presionó
    myStepper.step(stepsPerQuarterTurn); // Mover físicamente el motor 90°
    Serial.println("Botón presionado. Moviendo 90° sin cambiar la sección lógica.");
    buttonPressed = true; // Indica que el motor fue movido manualmente
  }
  
  // Guardar el último estado del botón
  lastButtonState = buttonState;

  delay(100); // Pausa para evitar actualizaciones rápidas innecesarias
}

// Función para activar el sensor RFID
void activarRFID() {
  digitalWrite(SS_PIN, LOW); // Activa el sensor RFID
}

// Función para desactivar el sensor RFID
void desactivarRFID() {
  digitalWrite(SS_PIN, HIGH); // Desactiva el sensor RFID
}

// Función para verificar si el UID de la tarjeta coincide con el permitido
bool isAllowedCard() {
  if (rfid.uid.size != sizeof(allowedUID)) return false;
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] != allowedUID[i]) return false;
  }
  return true;
}

// Función para mover el motor a la sección indicada
void moveToSection(int section) {
  int stepsToMove = (section - currentSection) * stepsPerQuarterTurn;

  // Ajustar el número de pasos para siempre mover en la dirección correcta
  if (stepsToMove < 0) stepsToMove += stepsPerRevolution;

  // Mover el motor la cantidad calculada de pasos
  myStepper.step(stepsToMove);
  
  // Actualiza la sección actual
  currentSection = section;
  
  Serial.print("Motor movido a sección ");
  Serial.print(section);
  Serial.print(" (");
  Serial.print(stepsToMove);
  Serial.println(" pasos).");
}

// Función para actualizar la sección del motor según el tiempo transcurrido
void updateSectionByTime() {
  unsigned long elapsedTime = millis() - startTime; // Tiempo transcurrido en milisegundos
  int cyclePosition = (elapsedTime / 1000) % 120; // Posición en el ciclo de 120 segundos

  // Determina la sección en función del tiempo transcurrido
  int newSection;
  if (cyclePosition < 30) {
    newSection = 0; // Primera sección (0°)
  } else if (cyclePosition < 60) {
    newSection = 1; // Segunda sección (90°)
  } else if (cyclePosition < 90) {
    newSection = 2; // Tercera sección (180°)
  } else {
    newSection = 3; // Cuarta sección (270°)
  }

  // Mueve el motor si la nueva sección es diferente a la actual
  if (newSection != currentSection) {
    Serial.print("Actualizando posición por tiempo. Moviendo a la sección: ");
    Serial.println(newSection + 1);
    moveToSection(newSection);
  }
}
