#include "Arduino.h"
#include "DHT.h"
#include "DNSServer.h"
#include "ESP8266WebServer.h"
#include "ESP8266WiFi.h"
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
#define WIFI_SETUP_LED D0

void tick_flash(uint8_t led);
void config_mode_cb(WiFiManager *myWiFiManager);

#ifdef WIFI_SETUP_LED
Ticker wifi_ticker;
#endif
DHT dht(DHTPIN, DHTTYPE);

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

	wifiManager.setAPCallback(config_mode_cb);

	if (!wifiManager.autoConnect()) {
		Serial.println(F("Failed to connect; resetting..."));
		ESP.reset();
		delay(5000);
	}

	Serial.print(F("Connected. IP is "));
	Serial.println(WiFi.localIP());

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

	Serial.print(F("Humidity: "));
	Serial.print(humidity);
	Serial.print(F("% Temperature: "));
	Serial.print(temperature);
	Serial.println(F("°C "));

	delay(2000);
}
