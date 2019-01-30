#include "Arduino.h"
#include "DHT.h"

///////////////////
// Configuration //
///////////////////

// Details of the temperature sensor
#define DHTPIN D7
#define DHTTYPE DHT22

// Which LEDs should flash. D0 is the LED on the NodeMCU. D4 is the LED on the ESP12 (the one that flashes during flashing). Undefine this to disable
#define PERIODIC_DHT_LED D0

DHT dht(DHTPIN, DHTTYPE);

void setup(void)
{
	Serial.begin(115200);

#ifdef PERIODIC_DHT_LED
	pinMode(PERIODIC_DHT_LED, OUTPUT);
	digitalWrite(PERIODIC_DHT_LED, HIGH);
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
