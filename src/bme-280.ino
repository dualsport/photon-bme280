/*
 * Project bme-280-oled
 * Description:
 * Author:
 * Date:
 */

#include "Particle.h"
#include "Adafruit_BME280_RK.h"
#include "Adafruit_Sensor.h"
#include <math.h> // isnan()


// Temp, pressure, humidity sensor
Adafruit_BME280 bme;

// I2C wiring 
#define BME_MOSI D0 // = SDA 
#define BME_SCK D1 // = SCL 

 int led1 = D7;  //onboard led
 int bme_pwr = D4; //bme power

// Repeat time for Publish in seconds
// Example 900 will repeat every 15 minutes at :00, :15, :30, :45
const int period = 1800;

// time sync interval in seconds
// simple interval, repeat every n seconds
// 12 hours = 43200
const int sync_interval = 43200;
time_t next_sync;

time_t current_time;
time_t next_pub;

struct weather{
  float temp_c;
  float temp_f;
  float humidity;
  float pressure;
  float dewpt_c;
  float dewpt_f;
} ;

struct weather get_weather(void);

void setup() {
	Serial.begin(9600);
    Particle.publish("status", "start", PRIVATE);
    Particle.function("current_conditions", current);

    pinMode(led1, OUTPUT);
    digitalWrite(led1, LOW);
    pinMode(bme_pwr, OUTPUT);
    digitalWrite(bme_pwr, LOW);

    current_time = Time.now();
    next_pub = current_time - (current_time % period) + period;
    next_sync = current_time + sync_interval;
    delay(2000);
}

void loop() { 
  current_time = Time.now();
  // Update readings
	if (millis() - lastUpdate >= updatePeriod * 1000) {
    digitalWrite(led1, HIGH);
		lastUpdate = millis();
		
    struct weather w = get_weather();

		display.clearDisplay();

		// if we have good readings update screen & publish readings
		if (!isnan(w.temp_c) && !isnan(w.humidity) && !isnan(w.pressure)) {

      // display results
			display.setTextSize(2);
			display.setTextColor(WHITE);
			
			snprintf(buf, sizeof(buf), "%.1f Deg F", w.temp_f);
			display.setCursor(0,0);
			display.println(buf);

			// snprintf(buf, sizeof(buf), "%.1f DwPtF", w.dewpt_f * 9.0 / 5.0 + 32.0);
      // display.setCursor(0,24);
			// display.println(buf);

			snprintf(buf, sizeof(buf), "%.1f %% RH", w.humidity);
      display.setCursor(0,24);
			display.println(buf);

			snprintf(buf, sizeof(buf), "%.1f InHg", w.pressure);
      display.setCursor(0,48);
			display.println(buf);

      display.display();

      // Publish results
      if(current_time >= next_pub) {
        String result = String::format("{\"Temp_C\": %4.2f, \"Dewpoint_C\": %4.2f, \"RelHum\": %4.2f, \"Press_InHg\": %4.2f}", w.temp_c, w.dewpt_c, w.humidity, w.pressure);
        Particle.publish("readings", result, PRIVATE);
        next_pub = current_time - (current_time % period) + period;
      }
  	}
    else {
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(WHITE);
      display.setCursor(0,0);
      display.println("Error");
      display.setCursor(0,24);
      display.println("Reading");
      display.setCursor(0,48);
      display.println("BME280");
      display.display();
    }
   digitalWrite(led1, LOW);
  }

  // sync time
  if(current_time >= next_sync) {
      Particle.syncTime();
      delay(5000);
      if (Particle.syncTimePending()) {
          Particle.publish("status", "time sync failed", PRIVATE);
      }
      else {
          current_time = Time.now();
          next_sync = current_time + sync_interval;
          Particle.publish("status", "time sync success", PRIVATE);
      }
  }
}

int current(String unit) {
    digitalWrite(led1, HIGH);
    String result = "Invalid unit given. Allowed units are 'C' or 'F' for celsius or fahrenheit.";
    if (unit.toUpperCase() == "C" || unit.toUpperCase() == "F") {
      struct weather w = get_weather();
      if (unit.toUpperCase() == "C") {
        result = String::format("{\"Temp_C\": %4.2f, \"Dewpoint_C\": %4.2f, \"RelHum\": %4.2f, \"Press_InHg\": %4.2f}", w.temp_c, w.dewpt_c, w.humidity, w.pressure);
      }
      else {
        result = String::format("{\"Temp_F\": %4.2f, \"Dewpoint_F\": %4.2f, \"RelHum\": %4.2f, \"Press_InHg\": %4.2f}", w.temp_f, w.dewpt_f, w.humidity, w.pressure);
      }
    }
    Particle.publish("current_conditions", result, PRIVATE);
    digitalWrite(led1, LOW);
    return 1;
}

float calcDewpoint(float temp, float humidity) {
  // calculate dewpoint from temp & relative humidity
  // temp in C, rel humidity as decimal, i.e. .715 for 71.5%
  // using Magnus formula
  // source: https://www.azosensors.com/article.aspx?ArticleID=23

  // table of Magnus coefficients for dew point calculation
  // [minTempC, maxTempC, C1(mbar), C2(-), C3(C)]
  float mgn[2][5] = {{-50.9,0,6.1078,17.84362,254.425},{0,100,6.1078,17.08085,234.175}};

  int ms = -1;
  float dewpoint = 999;
  for (int i = 0; i < 2; i++) {
    if (temp > mgn[i][0] && temp < mgn[i][1]) {
      ms = i;
    }
  }
  if (ms >= 0) {
    // Magnus formula dewpoint calculation
    float ps = mgn[ms][2] * exp((mgn[ms][3] * temp)/(mgn[ms][4] + temp));
    float pd = ps * (humidity / 100);
    dewpoint = ( (-log(pd / mgn[ms][2]) * mgn[ms][4]) / (log(pd / mgn[ms][2]) - mgn[ms][3]) );
  }
  else {
    String result = String::format("Temp %4.2f out of range for dew point calculation.", temp);
    Particle.publish("error", result, PRIVATE);
  }
  return dewpoint;
}

struct weather get_weather(void) {
  bool bmeStarted = false;
  digitalWrite(bme_pwr, HIGH);
  delay(75);
  do {
    if (!bme.begin(0x76)) {
      // cycle power if BME280 doesn't initialize
      digitalWrite(bme_pwr, LOW);
      delay(50);
      digitalWrite(bme_pwr, HIGH);
      delay(100);
    }
    else
    {
      bmeStarted = true;
      delay(75); // allow to stabilize
    }
  } while (!bmeStarted);

  struct weather w;
  w.temp_c = bme.readTemperature(); // degrees C
  w.temp_f = (w.temp_c * 9 / 5) + 32; // degrees F
  w.humidity = bme.readHumidity(); // % 
  w.pressure = bme.readPressure() / 3386.39F; // inches-Hg
  w.dewpt_c = calcDewpoint(w.temp_c, w.humidity); // dew point C
  w.dewpt_f = (w.dewpt_c * 9 / 5) + 32; // dew point F
  digitalWrite(bme_pwr, LOW);
  return w;
} 
