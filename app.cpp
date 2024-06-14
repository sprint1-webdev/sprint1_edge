#include <DHT.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <RTClib.h>
#include <math.h>

// Define os pinos que serão utilizados para ligação ao display
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// Declaração das constantes
#define LOG_OPTION 1     // Opção para ativar a leitura do log
#define SERIAL_OPTION 1  // Opção de comunicação serial: 0 para desligado, 1 para ligado
#define UTC_OFFSET -3    // Ajuste de fuso horário para UTC-3
#define DHTPIN 6 // Pino do sensor DHT
#define DHTTYPE DHT22 // Tipo de sensor DHT
DHT dht(DHTPIN, DHTTYPE); // Inicializa o sensor DHT

RTC_DS1307 RTC;

// Configurações da EEPROM
const int maxRecords = 100;
const int recordSize = 16; // Aumentado para armazenar todos os dados necessários
int startAddress = 0;
int endAddress = maxRecords * recordSize;
int currentAddress = 0;

// Pinos dos LEDs
int ledVermelho = 8;  
int ledVerde = 9;    

// Pinos dos sensores
const int ldr = A0;
const int pinTermistor = A1;

// Variáveis para os valores lidos dos sensores
int valorLdr = 0; 
float tempAmb;
float tempPista;
float umidadeAmb;

// Caracteres personalizados para o LCD
byte name0x8[] = { B10000, B10000, B00000, B00000, B11111, B00001, B11101, B01101 };
byte name0x6[] = { B00001, B00000, B00000, B00000, B00111, B01100, B10101, B10101 };
byte name0x7[] = { B00000, B11001, B01010, B00100, B11111, B00000, B11111, B10111 };
byte name1x6[] = { B10101, B10101, B10101, B10101, B10100, B11111, B00110, B00110 };
byte name1x7[] = { B10111, B01111, B10000, B11111, B00000, B11111, B00000, B00000 };
byte name1x8[] = { B01101, B10101, B01101, B11101, B00001, B11111, B00110, B00110 };

// Array símbolo de grau
byte grau[8] = { 
    B00001100,
    B00010010,
    B00010010,
    B00001100,
    B00000000,
    B00000000,
    B00000000,
    B00000000,
};

void setup() {
  Serial.begin(9600); 
  pinMode(ledVermelho, OUTPUT);
  pinMode(ledVerde, OUTPUT);
  dht.begin(); // Inicia o sensor DHT
  lcd.begin(16, 2);
  lcd.createChar(0, grau); // Cria o caractere personalizado para o símbolo de grau
  RTC.begin();
  RTC.adjust(DateTime(F(__DATE__), F(__TIME__))); // Ajustar a data e hora apropriadas uma vez inicialmente, depois comentar
  inicializacao();
}

void loop() {
  // Variáveis para acumular as leituras dos sensores
  float somaLux = 0;
  float somaTempAmb = 0;
  float somaUmidade = 0;
  float somaTempPista = 0;

  // Realiza 10 leituras para obter uma média
  for (int i = 0; i < 10; ++i) {
    int valorLdr = analogRead(ldr); // Ler o valor analógico do pino A0
    int valorLdrConvert = map(valorLdr, 8, 1015, 1015, 8); // Converte o valor lido para uma escala ajustada
    float Vout = valorLdrConvert * (5.0 / 1023.0); // Converte a leitura analógica para tensão

    // Garantir que Vout não seja zero para evitar divisão por zero
    if (Vout != 0) {
      int R1 = 10000; // Resistor fixo de 10k ohms
      float R_LDR = R1 * (5.0 / Vout - 1.0); // Calcular resistência do LDR

      // Assumindo que a relação resistência-lux é inversa e ajustando os coeficientes
      if (R_LDR != 0) {
        float lux = pow((R1 / R_LDR), 1.25) * 100; // Exemplo de ajuste, pode variar conforme seu LDR
        somaLux += lux; // Soma o valor de lux
      }
    }

    tempAmb = dht.readTemperature();
    if (isnan(tempAmb)) {
      Serial.println("Falha ao ler a temperatura!");
    } else {
      somaTempAmb += tempAmb;
    }

    umidadeAmb = dht.readHumidity();
    if (isnan(umidadeAmb)) {
      Serial.println("Falha ao ler a umidade!");
    } else {
      somaUmidade += umidadeAmb;
    }

    int tempPistaRaw = analogRead(pinTermistor);
    float tempPistaK = 1 / (log(1 / (1023.0 / tempPistaRaw - 1)) / 3950 + 1.0 / 298.15);
    tempPista = tempPistaK - 273.15; // Converte para Celsius
    somaTempPista += tempPista;

    delay(200);
  }

  // Calcular as médias
  float mediaLux = somaLux / 10;
  float mediaTempAmb = somaTempAmb / 10;
  float mediaUmidade = somaUmidade / 10;
  float mediaTempPista = somaTempPista / 10;

  verificarLuminosidade(mediaLux);
  verificarTemperatura(mediaTempAmb);
  verificarUmidade(mediaUmidade);
  verificarTempPista(mediaTempPista);

  // Verifica se algum valor está fora dos intervalos desejados
  if (isOutOfScope(mediaTempAmb, mediaUmidade, mediaLux, mediaTempPista)) {
    salvarDadosEEPROM(mediaTempAmb, mediaUmidade, mediaLux, mediaTempPista);
    exibirSerial(mediaLux, mediaTempAmb, mediaUmidade, mediaTempPista); // Exibir na serial
  }

  exibirInfo(mediaLux, mediaTempAmb, mediaUmidade, mediaTempPista);
}

// Verifica os níveis de luminosidade e aciona os LEDs de acordo
void verificarLuminosidade(float mediaLux) {
  Serial.print("Índice de luminosidade (em lux): ");
  Serial.print(mediaLux);

  if (mediaLux >= 20000 && mediaLux <= 50000) {
    apagarLeds();
    digitalWrite(ledVerde, HIGH);
    Serial.println("\t Luminosidade Ideal");
  } else if (mediaLux < 20000) {
    apagarLeds();
    digitalWrite(ledVermelho, HIGH);
    Serial.println("\t Luminosidade Baixa");
  } else {
    apagarLeds();
    digitalWrite(ledVerde, HIGH); // Corrigido para ligar o LED verde para luminosidade alta
    Serial.println("\t Luminosidade Alta");
  }
}

// Verifica a temperatura ambiente e imprime o estado no Serial Monitor
void verificarTemperatura(float mediaTempAmb) {
  Serial.print("Temperatura Ambiente: ");
  Serial.print(mediaTempAmb);
  Serial.print("°C");

  if (mediaTempAmb > 25) {
    Serial.println("\t Temperatura Ambiente Alta");
  } else if (mediaTempAmb < 15) {
    Serial.println("\t Temperatura Ambiente Baixa");
  } else {
    Serial.println("\t Temperatura Ambiente Ideal");
  }
}

// Verifica a umidade ambiente e imprime o estado no Serial Monitor
void verificarUmidade(float mediaUmidade) {
  Serial.print("Umidade: ");
  Serial.print(mediaUmidade);
  Serial.print("%");

  if (mediaUmidade > 60) {
    Serial.println("\t Umidade alta");
  } else if (mediaUmidade < 40) {
    Serial.println("\t Umidade Baixa");
  } else {
    Serial.println("\t Umidade Ideal");
  }
}

// Verifica a temperatura da pista e imprime o estado no Serial Monitor
void verificarTempPista(float mediaTempPista) {
  Serial.print("Temperatura da pista: ");
  Serial.print(mediaTempPista);
  
  if (mediaTempPista >= 20 && mediaTempPista <= 30) {
    Serial.println("\t Ideal");
  } else if (mediaTempPista < 20) {
    Serial.println("\t Muito Baixa");
  } else {
    Serial.println("\t Muito Alta");
  }
}

// Verifica se algum valor está fora dos intervalos desejados
bool isOutOfScope(float tempAmb, float umidade, float lux, float tempPista) {
  return (lux < 20000 || lux > 50000 || tempAmb < 15 || tempAmb > 25 || umidade < 40 || umidade > 60 || tempPista < 20 || tempPista > 30);
}

// Salva os dados na EEPROM
void salvarDadosEEPROM(float tempAmb, float umidade, float lux, float tempPista) {
  DateTime now = RTC.now();
  int tempAmbInt = (int)(tempAmb * 100);
  int umidadeInt = (int)(umidade * 100);
  int luxInt = (int)lux;
  int tempPistaInt = (int)(tempPista * 100);

  EEPROM.put(currentAddress, now.unixtime());
  EEPROM.put(currentAddress + 4, luxInt);
  EEPROM.put(currentAddress + 6, tempAmbInt);
  EEPROM.put(currentAddress + 8, umidadeInt);
  EEPROM.put(currentAddress + 10, tempPistaInt);

  getNextAddress();
}

// Exibe os dados no Serial Monitor
void exibirSerial(float lux, float tempAmb, float umidade, float tempPista) {
  DateTime now = RTC.now();
  Serial.println("======================================== ALERTA =======================================");
  Serial.print(now.day());
  Serial.print("/");
  Serial.print(now.month());
  Serial.print("/");
  Serial.print(now.year());
  Serial.print(" ");
  Serial.print(now.hour() < 10 ? "0" : ""); // Adiciona zero à esquerda se hora for menor que 10
  Serial.print(now.hour());
  Serial.print(":");
  Serial.print(now.minute() < 10 ? "0" : ""); // Adiciona zero à esquerda se minuto for menor que 10
  Serial.print(now.minute());
  Serial.print(":");
  Serial.print(now.second() < 10 ? "0" : ""); // Adiciona zero à esquerda se segundo for menor que 10
  Serial.print(now.second());
  Serial.print(" - ");
  Serial.print("Lux: ");
  Serial.print(lux);
  Serial.print(" Temp. Ambiente: ");
  Serial.print(tempAmb);
  Serial.print(" Umidade: ");
  Serial.print(umidade);
  Serial.print(" Temp. Pista: ");
  Serial.println(tempPista);
  Serial.println("========================================================================================");
}

// Desliga todos os LEDs
void apagarLeds() {
  digitalWrite(ledVermelho, LOW);
  digitalWrite(ledVerde, LOW);
}

// Função de inicialização do display LCD
void inicializacao() {
  // Limpa a tela
  lcd.clear();

  // Cria caracteres personalizados
  lcd.createChar(1, name0x7);
  lcd.setCursor(7, 0);
  lcd.write(1);
  
  lcd.createChar(2, name0x6);
  lcd.setCursor(6, 0);
  lcd.write(2);
  
  lcd.createChar(3, name0x8);
  lcd.setCursor(8, 0);
  lcd.write(3);
  
  lcd.createChar(4, name1x6);
  lcd.setCursor(6, 1);
  lcd.write(4);
  
  lcd.createChar(5, name1x7);
  lcd.setCursor(7, 1);
  lcd.write(5);
  
  lcd.createChar(6, name1x8);
  lcd.setCursor(8, 1);
  lcd.write(6);

  delay(500);

  // Rolagem para a esquerda
  for (int posicao = 0; posicao < 3; posicao++) {
    lcd.scrollDisplayLeft();
    delay(300);
  }
    
  // Rolagem para a direita
  for (int posicao = 0; posicao < 6; posicao++) {
    lcd.scrollDisplayRight();
    delay(300);
  }

  lcd.clear();
  lcd.setCursor(6,0);
  lcd.print("Edge");
  lcd.setCursor(4,1);
  lcd.print("Solution!");

  delay(2000);
  lcd.clear();

  delay(5000);
}

// Exibe as informações no display LCD
void exibirInfo(float mediaLux, float mediaTempAmb, float mediaUmidade, float mediaTempPista) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Lux: ");
  lcd.setCursor(5, 0);
  lcd.print(mediaLux, 1);

  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp. Ambiente: ");
  lcd.setCursor(0, 1);
  lcd.print(mediaTempAmb, 1);
  lcd.setCursor(4, 1);
  lcd.write((byte)0);

  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Umidade: ");
  lcd.setCursor(9, 0);
  lcd.print(mediaUmidade, 1);
  lcd.setCursor(14, 0);
  lcd.print("%");

  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp. Pista: ");
  lcd.setCursor(0, 1);
  lcd.print(mediaTempPista, 1);
  lcd.write((byte)0);
}

// Função para obter o próximo endereço na EEPROM
void getNextAddress() {
  currentAddress += recordSize;
  if (currentAddress >= endAddress) {
    currentAddress = 0; // Volta para o começo se atingir o limite
  }
}

// Função para obter o log armazenado na EEPROM
void get_log() {
  Serial.println("Data stored in EEPROM:");
  Serial.println("Timestamp\tLux\tTemperature\tHumidity\tTrack Temp");

  // Loop para ler os dados armazenados na EEPROM
  for (int address = startAddress; address < endAddress; address += recordSize) {
    long timeStamp;
    int luxInt, tempAmbInt, umidadeInt, tempPistaInt;

    // Ler dados da EEPROM
    EEPROM.get(address, timeStamp);
    EEPROM.get(address + 4, luxInt);
    EEPROM.get(address + 6, tempAmbInt);
    EEPROM.get(address + 8, umidadeInt);
    EEPROM.get(address + 10, tempPistaInt);

    // Converter valores
    float lux = luxInt;
    float tempAmb = tempAmbInt / 100.0;
    float umidade = umidadeInt / 100.0;
    float tempPista = tempPistaInt / 100.0;

    // Verificar se os dados são válidos antes de imprimir
    if (timeStamp != 0xFFFFFFFF) { // 0xFFFFFFFF é o valor padrão de uma EEPROM não inicializada
      Serial.print(timeStamp);
      Serial.print("\t");
      Serial.print(lux);
      Serial.print("\t");
      Serial.print(tempAmb);
      Serial.print("\t\t");
      Serial.print(umidade);
      Serial.print("\t\t");
      Serial.println(tempPista);
    }
  }
}
