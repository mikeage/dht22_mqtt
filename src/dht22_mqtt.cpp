#include <FS.h> // needs to be first; I have no idea why, but a bunch of links say this ;-)
#include "Arduino.h"
#include "ArduinoJson.h"
#include "DHT.h"
#include "DNSServer.h"
#include "ESP8266WebServer.h"
#include "ESP8266WiFi.h"
#include "PubSubClient.h"
#include "Ticker.h"
#include "WiFiManager.h"

///////////////////
// Configuration //
///////////////////

// Details of the temperature sensor
#define DHTPIN D7
#define DHTTYPE DHT22

// Which LEDs should flash. D0 is the LED on the NodeMCU. D4 is the LED on the ESP12 (the one that flashes during flashing). Undefine these to disable
#define PERIODIC_DHT_LED D0
#define UPDATE_DHT_LED D4
#define WIFI_SETUP_LED D0

// Thresholds for deciding which changes should be sent. Set to 0.0 to send every change
#define MIN_TEMP_CHANGE 0.1
#define MIN_HUMIDITY_CHANGE 3 // The sensor offers a high degree of stated precision, but isn't really that accurate, so you might want to set this higher

void send_temp(float temperature, float humidity);
void send_autodiscovery(void);
void send_online(void);
void mqtt_reconnect(void);
void save_config_cb(void);
void tick_flash(uint8_t led);
void config_mode_cb(WiFiManager *myWiFiManager);
void get_state_topic(char *buf, size_t size);
void get_avail_topic(char *buf, size_t size);

// Default values; these will be overridden in the WiFi setup
char mqtt_server[40] = "mqtt.local";
char mqtt_port[6] = "1883";
char mqtt_topic_id[40] = "sensor1";
#define MQTT_PREFIX "temps/"
#define MQTT_AUTODISCOVERY_PREFIX "homeassistant/"

#ifdef WIFI_SETUP_LED
Ticker wifi_ticker;
#endif
DHT dht(DHTPIN, DHTTYPE);

bool g_shouldSaveConfig = false;
void save_config_cb(void)
{
	Serial.println(F("Configuration save required"));
	g_shouldSaveConfig = true;
}

// Cache the last values globally, so we don't send every single value; only those with a significant change
boolean valid_temp = false;
float last_temperature = 0.0;
float last_humidity = 0.0;

// Flash the specified during WiFi setup
void tick_flash(uint8_t led)
{
	int state = digitalRead(led); // get the current state of GPIO1 pin
	digitalWrite(led, !state);    // set pin to the opposite state
}

void config_mode_cb(WiFiManager *myWiFiManager)
{
	Serial.print(F("Entered config mode. Listening at: "));
	Serial.print(myWiFiManager->getConfigPortalSSID());
	Serial.print(F(" and connect to http://"));
	Serial.println(WiFi.softAPIP());
#ifdef WIFI_SETUP_LED
	wifi_ticker.attach(0.2, tick_flash, WIFI_SETUP_LED);
#endif
}

WiFiClient espClient;
PubSubClient client(espClient);

void get_state_topic(char *buf, size_t size)
{
	String state_topic;
	state_topic += MQTT_PREFIX;
	state_topic += mqtt_topic_id;
	state_topic += "/status";
	strncpy(buf, state_topic.c_str(), size);
}
void get_avail_topic(char *buf, size_t size)
{
	String avail_topic;
	avail_topic += MQTT_PREFIX;
	avail_topic += mqtt_topic_id;
	avail_topic += "/online";
	strncpy(buf, avail_topic.c_str(), size);
}

void send_autodiscovery(void)
{

	StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> jsonBuffer;
	JsonObject &root_temperature = jsonBuffer.createObject();
	JsonObject &root_humidity = jsonBuffer.createObject();

	char avail_topic[60];
	get_avail_topic(avail_topic, sizeof(avail_topic));
	char state_topic[60];
	get_state_topic(state_topic, sizeof(state_topic));

	root_temperature["name"] = (String)mqtt_topic_id + " Temperature";
	root_temperature["avty_t"] = (String)avail_topic;
	root_temperature["pl_avail"] = (String) "true";
	root_temperature["pl_not_avail"] = (String) "false";
	root_temperature["stat_t"] = (String)state_topic;
	root_temperature["unit_of_meas"] = (String) "°C";
	root_temperature["val_tpl"] = (String) "{{ value_json.temperature | round(1) }}";

	root_humidity["name"] = (String)mqtt_topic_id + " Humidity";
	root_humidity["avty_t"] = (String)avail_topic;
	root_humidity["pl_avail"] = (String) "true";
	root_humidity["pl_not_avail"] = (String) "false";
	root_humidity["stat_t"] = (String)state_topic;
	root_humidity["unit_of_meas"] = (String) "%";
	root_humidity["val_tpl"] = (String) "{{ value_json.humidity | round(0) }}";

	String autodiscovery_topic_temperature;
	autodiscovery_topic_temperature += MQTT_AUTODISCOVERY_PREFIX;
	autodiscovery_topic_temperature += "sensor/";
	autodiscovery_topic_temperature += mqtt_topic_id;
	autodiscovery_topic_temperature += "_temp";
	autodiscovery_topic_temperature += "/config";
	char buffer_temperature[root_temperature.measureLength() + 1];
	root_temperature.printTo(buffer_temperature, sizeof(buffer_temperature));
	client.publish(autodiscovery_topic_temperature.c_str(), buffer_temperature, true);

	String autodiscovery_topic_humidity;
	autodiscovery_topic_humidity += MQTT_AUTODISCOVERY_PREFIX;
	autodiscovery_topic_humidity += "sensor/";
	autodiscovery_topic_humidity += mqtt_topic_id;
	autodiscovery_topic_humidity += "_hum";
	autodiscovery_topic_humidity += "/config";
	char buffer_humidity[root_humidity.measureLength() + 1];
	root_humidity.printTo(buffer_humidity, sizeof(buffer_humidity));
	client.publish(autodiscovery_topic_humidity.c_str(), buffer_humidity, true);
}

void send_online(void)
{
	char avail_topic[60];
	get_avail_topic(avail_topic, sizeof(avail_topic));
	client.publish(avail_topic, "true");
}

void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
	Serial.print(F("Message received on "));
	Serial.print(topic);
	Serial.print(F(": "));

	for (unsigned int i = 0; i < length; i++) {
		Serial.print((char)payload[i]);
	}
	Serial.println();

	send_autodiscovery();
	send_online();
	send_temp(last_temperature, last_humidity);
}

void mqtt_reconnect(void)
{
	char avail_topic[60];
	get_avail_topic(avail_topic, sizeof(avail_topic));

	while (!client.connected()) {
		Serial.print(F("Attempting MQTT connection..."));
		String clientId = "temp-";
		clientId += mqtt_topic_id;
		if (client.connect(clientId.c_str(), avail_topic, 0, false, "false")) {
			Serial.println(F("connected"));

			send_autodiscovery();
			send_online();

			String listen_topic;
			listen_topic += MQTT_PREFIX;
			listen_topic += "command";
			client.subscribe(listen_topic.c_str());
		} else {
			Serial.print(F("failed, client state="));
			Serial.print(client.state());
			Serial.println(F(" try again in 5 seconds"));
			delay(5000);
		}
	}
}

void send_temp(float temperature, float humidity)
{
	if (valid_temp == false) {
		Serial.println(F("Not sending temp; no valid reading yet!"));
		return;
	}
	StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> jsonBuffer;
	JsonObject &root = jsonBuffer.createObject();

	root["humidity"] = (String)humidity;
	root["temperature"] = (String)temperature;

	char buffer[root.measureLength() + 1];
	root.printTo(buffer, sizeof(buffer));

	char state_topic[60];
	get_state_topic(state_topic, sizeof(state_topic));
	client.publish(state_topic, buffer, false);
}

void setup(void)
{
	Serial.begin(115200);

#ifdef WIFI_SETUP_LED
	pinMode(WIFI_SETUP_LED, OUTPUT);
	digitalWrite(WIFI_SETUP_LED, HIGH);
#endif
#ifdef UPDATE_DHT_LED
	pinMode(UPDATE_DHT_LED, OUTPUT);
	digitalWrite(UPDATE_DHT_LED, HIGH);
#endif
#ifdef PERIODIC_DHT_LED
	pinMode(PERIODIC_DHT_LED, OUTPUT);
	digitalWrite(PERIODIC_DHT_LED, HIGH);
#endif

#ifdef WIFI_SETUP_LED
	wifi_ticker.attach(0.2, tick_flash, WIFI_SETUP_LED);
#endif

	WiFiManager wifiManager;

	// For testing, uncomment these lines. Ideally, it'd be better to just trigger this with a button
	// wifiManager.resetSettings();
	// SPIFFS.format();

	//read configuration from FS json
	Serial.println(F("Mounting FS. This may take a while"));
	if (SPIFFS.begin()) {
		Serial.println(F("Mounted file system"));
		if (SPIFFS.exists("/config.json")) {
			Serial.println(F("Reading config file"));
			File config_file = SPIFFS.open("/config.json", "r");
			if (config_file) {
				Serial.println(F("Opened config file"));
				size_t size = config_file.size();
				std::unique_ptr<char[]> buf(new char[size]);
				config_file.readBytes(buf.get(), size);
				DynamicJsonBuffer jsonBuffer;
				JsonObject &json = jsonBuffer.parseObject(buf.get());
				json.printTo(Serial);
				if (json.success()) {
					strcpy(mqtt_server, json["mqtt_server"]);
					strcpy(mqtt_port, json["mqtt_port"]);
					strcpy(mqtt_topic_id, json["mqtt_topic_id"]);
				} else {
					Serial.println(F("Failed to load json config from file"));
				}
				config_file.close();
			}
		} else {
			Serial.println(F("No config file found"));
		}
	} else {
		Serial.println(F("Failed to mount"));
	}

	WiFiManagerParameter custom_mqtt_server("server", "MQTT server address", mqtt_server, 40);
	WiFiManagerParameter custom_mqtt_port("port", "MQTT port", mqtt_port, 6);
	WiFiManagerParameter custom_mqtt_topic_id("topic", "MQTT device identifier", mqtt_topic_id, 40);

	wifiManager.addParameter(&custom_mqtt_server);
	wifiManager.addParameter(&custom_mqtt_port);
	wifiManager.addParameter(&custom_mqtt_topic_id);

	wifiManager.setAPCallback(config_mode_cb);
	wifiManager.setSaveConfigCallback(save_config_cb);

	if (!wifiManager.autoConnect()) {
		Serial.println(F("Failed to connect; resetting..."));
		ESP.reset();
		delay(5000);
	}

	Serial.print(F("Connected. IP is "));
	Serial.println(WiFi.localIP());

	strcpy(mqtt_server, custom_mqtt_server.getValue());
	strcpy(mqtt_port, custom_mqtt_port.getValue());
	strcpy(mqtt_topic_id, custom_mqtt_topic_id.getValue());

	if (g_shouldSaveConfig) {
		Serial.println(F("Saving config"));
		DynamicJsonBuffer jsonBuffer;
		JsonObject &json = jsonBuffer.createObject();
		json["mqtt_server"] = mqtt_server;
		json["mqtt_port"] = mqtt_port;
		json["mqtt_topic_id"] = mqtt_topic_id;

		File config_file = SPIFFS.open("/config.json", "w");
		if (config_file) {
			json.printTo(config_file);
			config_file.close();
		} else {
			Serial.println(F("Failed to open config file for writing"));
		}
	}

	client.setServer(mqtt_server, atoi(mqtt_port));
	client.setCallback(mqtt_callback);

#ifdef WIFI_SETUP_LED
	wifi_ticker.detach();
	digitalWrite(WIFI_SETUP_LED, HIGH);
#endif
}

void loop(void)
{
#ifdef PERIODIC_DHT_LED
	// Flash LED once each cycle
	digitalWrite(PERIODIC_DHT_LED, LOW);
	delay(100);
	digitalWrite(PERIODIC_DHT_LED, HIGH);
#endif

	float humidity = dht.readHumidity();
	float temperature = dht.readTemperature();

	if (isnan(humidity) || isnan(temperature) || (humidity > 100) || (humidity < 0)) {
		Serial.println(F("Failed to read from DHT sensor!"));
		return;
	}

	if (!client.connected()) {
		mqtt_reconnect();
	}
	client.loop();

	if ((fabs(last_temperature - temperature) > MIN_TEMP_CHANGE) || (fabs(last_humidity - humidity) > MIN_HUMIDITY_CHANGE)) {
		valid_temp = true;
		Serial.println(F("New temperature or humidity detected"));
		last_humidity = humidity;
		last_temperature = temperature;
#ifdef UPDATE_DHT_LED
		for (int i = 0; i < 3; i++) {
			digitalWrite(UPDATE_DHT_LED, LOW);
			delay(100);
			digitalWrite(UPDATE_DHT_LED, HIGH);
			delay(50);
		}
#endif
		send_temp(temperature, humidity);
	}

	Serial.print(F("Humidity: "));
	Serial.print(humidity);
	Serial.print(F("% Temperature: "));
	Serial.print(temperature);
	Serial.println(F("°C "));

	delay(2000);
}
