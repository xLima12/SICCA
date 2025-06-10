#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);

const int sensorPin = 35;  // Botão/sensor
const int ledPin = 32;     // LED

bool estadoAnterior = HIGH;
unsigned long ultimaLeitura = 0;
unsigned long ultimoReconnect = 0;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando ao WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("Falha na conexão WiFi!");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensagem recebida no tópico [");
  Serial.print(topic);
  Serial.print("]: ");
  
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.println(msg);
  
  // Remove espaços em branco e quebras de linha
  msg.trim();
  
  // Verifica se é JSON e extrai o valor
  if (msg.startsWith("{") && msg.endsWith("}")) {
    // É um JSON, procura pelo valor da chave "msg"
    int startPos = msg.indexOf("\"msg\":");
    if (startPos != -1) {
      startPos = msg.indexOf("\"", startPos + 6); // Procura a primeira aspas após "msg":
      int endPos = msg.indexOf("\"", startPos + 1); // Procura a aspas de fechamento
      if (startPos != -1 && endPos != -1) {
        msg = msg.substring(startPos + 1, endPos);
        Serial.print("Comando extraído do JSON: ");
        Serial.println(msg);
      }
    }
  }
  
  // Controla o LED baseado na mensagem recebida
  if (msg == "ligar" || msg == "LIGAR") {
    digitalWrite(ledPin, HIGH);
    Serial.println("LED ligado via MQTT");
    // Confirma o estado
    client.publish("agua/status", "LED_LIGADO");
  } 
  else if (msg == "desligar" || msg == "DESLIGAR") {
    digitalWrite(ledPin, LOW);
    Serial.println("LED desligado via MQTT");
    // Confirma o estado
    client.publish("agua/status", "LED_DESLIGADO");
  }
  else if (msg == "status" || msg == "STATUS") {
    // Comando para consultar status atual
    Serial.println("Solicitação de status recebida");
    if (digitalRead(ledPin) == HIGH) {
      client.publish("agua/status", "LED_LIGADO");
      Serial.println("Status enviado: LED_LIGADO");
    } else {
      client.publish("agua/status", "LED_DESLIGADO");
      Serial.println("Status enviado: LED_DESLIGADO");
    }
    
    // Envia informações adicionais
    String statusCompleto = "LED: ";
    statusCompleto += (digitalRead(ledPin) == HIGH) ? "LIGADO" : "DESLIGADO";
    statusCompleto += ", WiFi: CONECTADO, MQTT: CONECTADO";
    client.publish("agua/info", statusCompleto.c_str());
  }
  else {
    Serial.println("Comando não reconhecido: " + msg);
    // Lista comandos disponíveis
    client.publish("agua/status", "COMANDO_INVALIDO");
    client.publish("agua/info", "Comandos: ligar, desligar, status");
  }
}

void reconnect() {
  // Tenta reconectar apenas a cada 5 segundos
  if (millis() - ultimoReconnect > 5000) {
    ultimoReconnect = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi desconectado, reconectando...");
      setup_wifi();
      return;
    }
    
    Serial.print("Tentando conexão MQTT...");
    
    // Cria um ID único para o cliente
    String clientId = "ESP8266Water-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println(" conectado!");
      
      // Subscreve ao tópico de controle
      client.subscribe("agua/controle");
      Serial.println("Subscrito ao tópico: agua/controle");
      
      // Publica status de conexão
      client.publish("agua/status", "CONECTADO");
      
      // Envia estado atual do LED
      if (digitalRead(ledPin) == HIGH) {
        client.publish("agua/status", "LED_LIGADO");
      } else {
        client.publish("agua/status", "LED_DESLIGADO");
      }
      
    } else {
      Serial.print(" falhou, rc=");
      Serial.print(client.state());
      Serial.println(" tentando novamente em 5 segundos");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Monitor de Água ESP8266 ===");
  
  // Configura os pinos
  pinMode(sensorPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  
  // Conecta ao WiFi
  setup_wifi();
  
  // Configura MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  // Seed para números aleatórios
  randomSeed(micros());
  
  Serial.println("Setup concluído!");
  Serial.println("Comandos MQTT:");
  Serial.println("- Tópico: agua/controle");
  Serial.println("- Ligar LED: 'ligar'");
  Serial.println("- Desligar LED: 'desligar'");
  Serial.println("- Consultar status: 'status'");
  Serial.println("- Respostas: agua/status e agua/info");
}

void loop() {
  // Verifica conexão MQTT
  if (!client.connected()) {
    reconnect();
  } else {
    client.loop();
  }
  
  // Lê o estado atual do botão/sensor
  bool estadoAtual = digitalRead(sensorPin);
  
  // Detecta quando o botão é pressionado (transição HIGH -> LOW)
  if (estadoAnterior == HIGH && estadoAtual == LOW) {
    // Debounce - evita leituras múltiplas
    if (millis() - ultimaLeitura > 200) {
      Serial.println("Botão pressionado!");
      
      // Alterna o estado do LED
      bool ledAtual = digitalRead(ledPin);
      digitalWrite(ledPin, !ledAtual);
      
      // Publica o novo estado via MQTT
      if (client.connected()) {
        if (!ledAtual) { // LED foi ligado
          client.publish("agua/monitoramento", "BOTAO_PRESSIONADO_LED_LIGADO");
          client.publish("agua/status", "LED_LIGADO");
          Serial.println("LED ligado e publicado via MQTT");
        } else { // LED foi desligado
          client.publish("agua/monitoramento", "BOTAO_PRESSIONADO_LED_DESLIGADO");
          client.publish("agua/status", "LED_DESLIGADO");
          Serial.println("LED desligado e publicado via MQTT");
        }
      } else {
        Serial.println("MQTT desconectado - não foi possível publicar");
      }
      
      ultimaLeitura = millis();
    }
  }
  
  estadoAnterior = estadoAtual;
  
  // Status de conexão a cada 30 segundos
  static unsigned long ultimoStatus = 0;
  if (millis() - ultimoStatus > 30000) {
    ultimoStatus = millis();
    if (client.connected()) {
      client.publish("agua/heartbeat", "ONLINE");
      Serial.println("Heartbeat enviado");
    }
  }
  
  delay(50); // Pequeno delay para estabilidade
}