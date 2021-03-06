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

// Details of the PIR
#define PIRPIN D6

// Which LEDs should flash. D0 is the LED on the NodeMCU. D4 is the LED on the ESP12 (the one that flashes during flashing). Undefine these to disable
// #define PERIODIC_DHT_LED D0
// #define UPDATE_DHT_LED D4
#define WIFI_SETUP_LED D0
#define PIR_LED D4

// Thresholds for deciding which changes should be sent. Set to 0.0 to send every change
#define MIN_TEMP_CHANGE 0.2
#define MIN_HUMIDITY_CHANGE 3 // The sensor offers a high degree of stated precision, but isn't really that accurate, so you might want to set this higher

void send_temp(float temperature, float humidity);
void send_autodiscovery(void);
void send_autodiscovery_temp(void);
void send_autodiscovery_hum(void);
#ifdef PIRPIN
void send_autodiscovery_motion(void);
#endif
void send_online(void);
void mqtt_reconnect(void);
void save_config_cb(void);
void tick_flash(uint8_t led);
void config_mode_cb(WiFiManager *myWiFiManager);
void get_state_topic(char *buf, size_t size);
void get_avail_topic(char *buf, size_t size);
#ifdef PIRPIN
void get_motion_topic(char *buf, size_t size);
void send_motion(bool motion);
#endif

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
#ifdef PIRPIN
bool last_motion = false;
#endif


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
#ifdef PIRPIN
void get_motion_topic(char *buf, size_t size)
{
	String motion_topic;
	motion_topic += MQTT_PREFIX;
	motion_topic += mqtt_topic_id;
	motion_topic += "/motion";
	strncpy(buf, motion_topic.c_str(), size);
}
#endif
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

	send_autodiscovery_temp();
	send_autodiscovery_hum();
#ifdef PIRPIN
	send_autodiscovery_motion();
#endif
}

void send_autodiscovery_temp(void)
{
	StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> jsonBuffer;
	JsonObject &root = jsonBuffer.createObject();

	char avail_topic[60];
	get_avail_topic(avail_topic, sizeof(avail_topic));
	char state_topic[60];
	get_state_topic(state_topic, sizeof(state_topic));

	root["name"] = (String)mqtt_topic_id + " Temperature";
	root["avty_t"] = (String)avail_topic;
	root["pl_avail"] = (String) "true";
	root["pl_not_avail"] = (String) "false";
	root["stat_t"] = (String)state_topic;
	root["unit_of_meas"] = (String) "°C";
	root["val_tpl"] = (String) "{{ value_json.temperature | round(1) }}";

	String autodiscovery_topic;
	autodiscovery_topic += MQTT_AUTODISCOVERY_PREFIX;
	autodiscovery_topic += "sensor/";
	autodiscovery_topic += mqtt_topic_id;
	autodiscovery_topic += "_temp";
	autodiscovery_topic += "/config";
	char buffer[root.measureLength() + 1];
	root.printTo(buffer, sizeof(buffer));
	client.publish(autodiscovery_topic.c_str(), buffer, true);
}

void send_autodiscovery_hum(void)
{
	StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> jsonBuffer;
	JsonObject &root = jsonBuffer.createObject();

	char avail_topic[60];
	get_avail_topic(avail_topic, sizeof(avail_topic));
	char state_topic[60];
	get_state_topic(state_topic, sizeof(state_topic));

	root["name"] = (String)mqtt_topic_id + " Humidity";
	root["avty_t"] = (String)avail_topic;
	root["pl_avail"] = (String) "true";
	root["pl_not_avail"] = (String) "false";
	root["stat_t"] = (String)state_topic;
	root["unit_of_meas"] = (String) "%";
	root["val_tpl"] = (String) "{{ value_json.humidity | round(0) }}";

	String autodiscovery_topic;
	autodiscovery_topic += MQTT_AUTODISCOVERY_PREFIX;
	autodiscovery_topic += "sensor/";
	autodiscovery_topic += mqtt_topic_id;
	autodiscovery_topic += "_hum";
	autodiscovery_topic += "/config";
	char buffer[root.measureLength() + 1];
	root.printTo(buffer, sizeof(buffer));
	client.publish(autodiscovery_topic.c_str(), buffer, true);
}

#ifdef PIRPIN
void send_autodiscovery_motion(void)
{
	StaticJsonBuffer<MQTT_MAX_PACKET_SIZE> jsonBuffer;
	JsonObject &root = jsonBuffer.createObject();

	char avail_topic[60];
	get_avail_topic(avail_topic, sizeof(avail_topic));
	char motion_topic[60];
	get_motion_topic(motion_topic, sizeof(motion_topic));

	root["name"] = (String)mqtt_topic_id + " Motion";
	root["avty_t"] = (String)avail_topic;
	root["pl_avail"] = (String) "true";
	root["pl_not_avail"] = (String) "false";
	root["stat_t"] = (String)motion_topic;
	root["dev_cla"] = (String) "motion";

	String autodiscovery_topic;
	autodiscovery_topic += MQTT_AUTODISCOVERY_PREFIX;
	autodiscovery_topic += "binary_sensor/";
	autodiscovery_topic += mqtt_topic_id;
	autodiscovery_topic += "_motion";
	autodiscovery_topic += "/config";
	char buffer[root.measureLength() + 1];
	root.printTo(buffer, sizeof(buffer));
	client.publish(autodiscovery_topic.c_str(), buffer, true);
}
#endif

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

#ifdef PIRPIN
void send_motion(bool motion)
{
	char motion_topic[60];
	get_motion_topic(motion_topic, sizeof(motion_topic));
	client.publish(motion_topic, motion ? "ON" : "OFF", false);
}
#endif

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
#ifdef PIRPIN
	pinMode(PIRPIN, INPUT);
#ifdef PIR_LED
	pinMode(PIR_LED, OUTPUT);
	digitalWrite(PIR_LED, HIGH);
#endif
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

unsigned long dht22_millis = 0;
#ifdef PIRPIN
unsigned long pir_millis = 0;
#endif
unsigned long network_millis = 0;

void loop(void)
{
	bool do_dht = false;
#ifdef PIRPIN
	bool do_pir = false;
#endif
	bool do_network = false;

	unsigned long current_millis = millis();

	if (current_millis - dht22_millis >= 2000) {
		dht22_millis = current_millis;
		do_dht = true;
	}
#ifdef PIRPIN
	if (current_millis - pir_millis >= 100) {
		pir_millis = current_millis;
		do_pir = true;
	}
#endif

	if (current_millis - network_millis >= 100) {
		network_millis = current_millis;
		do_network = true;
	}

#ifdef PERIODIC_DHT_LED
	// Flash LED once each cycle
	digitalWrite(PERIODIC_DHT_LED, LOW);
	delay(100);
	digitalWrite(PERIODIC_DHT_LED, HIGH);
#endif

	if (do_network) {
		if (!client.connected()) {
			mqtt_reconnect();
		}
		client.loop();
	}

#ifdef PIRPIN
	if (do_pir) {
		int pir;
		pir = digitalRead(PIRPIN);
		//if (pir) {
		//Serial.println("PIR HIGH");
		//} else {
		//Serial.println("PIR LOW");
		//}
#ifdef PIR_LED
		digitalWrite(PIR_LED, !pir);
#endif
		bool motion = (pir == HIGH);
		if (motion != last_motion) {
			last_motion = motion;
			Serial.print(F("Motion is now "));
			Serial.println(motion ? F("ON"): F("OFF"));
			send_motion(pir == HIGH);
		}
	}
#endif

	if (do_dht) {
		float humidity = dht.readHumidity();
		float temperature = dht.readTemperature();

		if (isnan(humidity) || isnan(temperature) || (humidity > 100) || (humidity < 0)) {
			Serial.println(F("Failed to read from DHT sensor!"));
			return;
		}

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
	}
}
