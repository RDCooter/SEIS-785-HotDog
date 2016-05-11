//============================================================================
//
// Name: HotDog.ino
//
// Author:  Robert Driesch  --  UST Graduate Program
//
// Description: A short sketch to try and track the temperature and humidity 
//              in order to calculate a heat index that can be used to warn 
//              of harmful situatiuons.  Also allows for external sources to 
//              warn of a heat advisory to send an identical warning through 
//              the device.
//
// Date:    May 11, 2016
//
//============================================================================

//////////////////////////////////////////////////////////////////////////////
// Common Include Files
//////////////////////////////////////////////////////////////////////////////
#include "HttpClient/HttpClient.h"      // For HTTP Post operations
#include "OneWire/OneWire.h"            // For TempSensor
#include "spark-dallas-temperature/spark-dallas-temperature.h"

#include "math.h"                       // For min(), map()


//////////////////////////////////////////////////////////////////////////////
// Constants - Enums and Defines
//////////////////////////////////////////////////////////////////////////////

//============================================================================
// Define the Pins from the Board that Wiil Be Used for Temperature Sensor:
//============================================================================
#define TEMP_SENSOR_PIN         D2

//============================================================================
// Define the Pins from the Board that Wiil Be Used for External RGB LED:
//============================================================================
#define RED_PIN                 A4
#define GREEN_PIN               A1
#define BLUE_PIN                A0

//============================================================================
// Define the HTTP VariableId & Tokens for the UbiDots API:
//============================================================================
#define VARIABLE_ID             "5732456f76254245147b6d98"
#define TOKEN                   "absgMsTmGMTONZWmmvi6m0jaRPNvKE"

//============================================================================
// Define the Cellular Connection Settings that Wiil Be Used:
//============================================================================
#define CELLULAR_APN            "m2m.com.attz"
#define CELLULAR_USERNAME       ""
#define CELLULAR_PASSWORD       ""

//============================================================================
// Define the Iteration Consdtants and Limit Values that Wiil Be Used:
//============================================================================
#define DEFAULT_LOOP_DELAY      1000
#define OVERRIDE_DURATION       1800000

//============================================================================
// Define the Particle Publish Function & Variables that Wiil Be Used:
//============================================================================
#define PUBLISH_PATH            "photon"

//////////////////////////////////////////////////////////////////////////////
// Global In-Line Class Definitions:
//////////////////////////////////////////////////////////////////////////////

/**
 * Create an External RGB ED class that can be used to automatically mirror
 * the onboard RGB LED to anExternal RGB LED.  Once the class is 
 * instantiated, then no additional code will be needed within either the 
 * setup() or loop() functions. 
 **/
class ExternalRGB {
  public:
    ExternalRGB(pin_t r, pin_t g, pin_t b) : pin_r(r), pin_g(g), pin_b(b) {
      pinMode(pin_r, OUTPUT);
      pinMode(pin_g, OUTPUT);
      pinMode(pin_b, OUTPUT);
      RGB.onChange(&ExternalRGB::handler, this);
    }

    void handler(uint8_t r, uint8_t g, uint8_t b) {
      analogWrite(pin_r, 255 - r);
      analogWrite(pin_g, 255 - g);
      analogWrite(pin_b, 255 - b);
    }

    private:
      pin_t pin_r;
      pin_t pin_g;
      pin_t pin_b;
};

//////////////////////////////////////////////////////////////////////////////
// Global Variables 
//////////////////////////////////////////////////////////////////////////////

//============================================================================
// Define Variables Used for the Temperature Sensor:
//============================================================================
DallasTemperature tempSensor(new OneWire(TEMP_SENSOR_PIN));
double currentTemperature   = 0.0;      // Current temperature sensor reading 
int maxHighTemp             = 90;       // Top value in range of temperatures
int minLowTemp              = 60;       // Bottom value in range of temps

//============================================================================
// Define Variables Used for the Humidity Sensor:
//============================================================================
double currentHumidity      = 0.0;      // Current humiditysensor reading 

//============================================================================
// Define Variables Used for the HTTP Client:
//============================================================================
HttpClient http;                        // HTTP object for POST operaation
http_request_t httpRequest;             // HTTP Body for POST operation
http_response_t httpResponse;           // HTTP response to POST operation
http_header_t httpHeaders[] = {
    { "Content-Type", "application/json" },
    { "X-Auth-Token" , TOKEN },
    { NULL, NULL }                      // Always terminate headers will NULL
};
 
//============================================================================
// Define Misc. Global Variables:
//============================================================================
int lastPublish     = 0;                // Keep track of the last POST op
double currentHeatIndex = 0.0;          // Current heat index value
int alertCondition  = 0;                // Indicates an alert condition exists
int overrideAlert   = 0;                // Indicates an alert override exists

//============================================================================
// Define Variables Used for the External RGB LED:
//============================================================================
//ExternalRGB myRGB(RED_PIN, GREEN_PIN, BLUE_PIN);

//////////////////////////////////////////////////////////////////////////////
// Forward Declare Function Definitions
//////////////////////////////////////////////////////////////////////////////
int getPublishRate(double &);
double getSensorTemp();
double getSensorHumidity();
double calculateHeatIndex(double &, double &);
int evaluateHeatIndex(double &);
int updateRedLEDValue(double &);
int updateBlueLEDValue(double &);
int publishTemp(double&, double&);
void publishHttp(double&, double&);
void myAlertHandler(const char *, const char *); 

// ///////////////////////////////////////////////////////////////////////////
// ///////////////////////////////////////////////////////////////////////////
//  Main Setup() Routine: 
// ///////////////////////////////////////////////////////////////////////////
// ///////////////////////////////////////////////////////////////////////////
void setup() {
    /**
     * Setup the Particle Variables, Functions and Handlers that can be 
     * accessed through the Particle cloud interface.
     **/
    Particle.variable("tempF", &currentTemperature, DOUBLE);
    Particle.variable("humidity", &currentHumidity, DOUBLE);
    Particle.variable("heatIndexF", &currentHeatIndex, DOUBLE);
    Particle.variable("alertExists", &alertCondition, INT);
    
    Particle.subscribe("hotdog/alert", myAlertHandler);

    /**
     * Setup the static HTTP information needed to interface to the UbiDots 
     * API to collect and store the sensor data that we will be collecting.
     **/
    httpRequest.port = 80;
    httpRequest.hostname = "things.ubidots.com";
    httpRequest.path = "/api/v1.6/variables/"VARIABLE_ID"/values";
    
    /**
     * Setup and Start the Temperatoure Sensor to gather the information 
     * from the OneWire Sensor.
     **/
    tempSensor.begin();

    /**
     * Setup the Internal (and External) LED so we can control them as we 
     * read and react to the temperature sensor values.
     **/
    RGB.control(true);                  // So we can control the internal LED

    /**
     * Setup the Serial Interface to allow for USB Debugging.
     **/
    Serial.begin(9600);
    Serial.println("Hello World... I am HotDog!");
}
 
// ///////////////////////////////////////////////////////////////////////////
// \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\
//  Main Loop() Routine: 
// \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\
// ///////////////////////////////////////////////////////////////////////////
void loop() {

    /**
     * Extract the current temperature and humidity sensor readings from 
     * the various sensors.
     **/
    currentTemperature = getSensorTemp();
    currentHumidity = getSensorHumidity();

    /**
     * Calculate the heat index from the current temperature and humidy 
     * values to get the equilivant "feels like" temperature that will 
     * then be used to determine the adjusted RGB LED values.
     **/
    currentHeatIndex = calculateHeatIndex(currentTemperature, currentHumidity); 

    /**
     * Update the Internal RGB LED with the adjusted LED values based upon 
     * the current heat index calculated from the current temperature and 
     * humidity sensor readings.
     **/
     if ( currentTemperature > 0.0 ) {  // Ignore anomalous sensor readings
         evaluateHeatIndex(currentHeatIndex);

        /**
         * Calculate the adjusted RGB LED values for the red and blue LEDs 
         * so we can set the RGB LED to give a visual indication of the 
         * heat index.
         **/
        int adjustedRed = updateRedLEDValue(currentHeatIndex);
        int adjustedBlue = updateBlueLEDValue(currentHeatIndex);
        RGB.color(adjustedRed, 0, adjustedBlue); // Ignore Green LED

        /**
         * Set the RGB LED brightness proportional to the adjusted red/blue 
         * LED values calculated from the current temperature sensor readings. 
         * A value of zero indicates that color is fully engaged, so we will 
         * use the map() function to reverse that value so a corresponding 
         * brightness percentage is generated regardless of whether or not 
         * we are dealing with extreme HOT or COLD temperatures.
         **/
        //int brightVal = map(min(adjustedRed, adjustedBlue), 0, 255, 255, 0);
        int brightVal = max(adjustedRed, adjustedBlue);
        RGB.brightness(brightVal);

        // Output Serial Debug statements for the red and blue adjusted values
        Serial.println("Temperature=[" + String(currentTemperature) + "];" 
                        + " Humidity=[" + String(currentHumidity) + "];" 
                        + " HeatIndex=[" + String(currentHeatIndex) + "];" 
                        + " RedLedValue=[" + String(adjustedRed) + "];" 
                        + " BlueLedValue=[" + String(adjustedBlue) + "];" 
                        + " BrightnessValue=[" + String(brightVal) + "]");
     }

    /**
     * Determine if it is OK to publish the current reading or if we should 
     * defer/delay publishing the results so we do not flood the system with 
     * extranous or unimportant readings.
     **/
    if ( (millis()-lastPublish) > getPublishRate(currentHeatIndex) ) {
        lastPublish = publishTemp(currentTemperature, currentHeatIndex);
    }
    
    /**
     * Determine if an override ALERT condition exists and how long that 
     * override has been in effect.  At some point we need to remove the 
     * override and allow the normal loop prpocessing to regain control 
     * of the RGB LED.
     **/
    if ( (millis()-overrideAlert) > OVERRIDE_DURATION ) {
        overrideAlert = 0;              // Reset the override ALERT value
    }
     
    /**
     * Pause between sensor readings so we do not flood the sensor and the 
     * micro-board with requests and work where the results will be become 
     * unreliable.
     **/
    delay(DEFAULT_LOOP_DELAY);
}

/*****************************************************************************
 * Name: getPublishRate
 * 
 * Description: Retrieve the current fahrenheit temperature reading from the
 *              sensor and return that value back to the caller.
 ****************************************************************************/
int getPublishRate(double& pTemperature) {
    int tempInt = (int)pTemperature;    // Local int for easier comparisons
    int publishRate = 5 * 1000;         // Default to the Danger Zone (5 seconds)

    /** 
     * Perform a quick check if any override ALERT condition exists and we 
     * should early exit and keep the publish rate set to the "Danger Zone" 
     * with quicker updates.
     **/
     if ( !overrideAlert ) {            // Only calculate when not overridden
        /**
         * Determine based upon the current temperature reading, how often we 
         * should be concerned enough about the temperature that we publish its 
         * value.  This is done primarily to conserve battery and data bandwidth 
         * during the POSTing operations.
         **/
        if ( tempInt < 60 ) { 
            publishRate = 10 * 60 * 1000; // Sleepy time... (10 minutes)
        } else if ( tempInt < 80 ) {
            publishRate = 1 * 60 * 1000; // Moderate temperatures... (1 minute)
        } else if ( tempInt < 90 ) {        
            publishRate = 30 * 1000;    // Starting to be concerned... (30 seconds)
        } 
    }
    return publishRate;                 // Return the calculated rate
}

/*****************************************************************************
 * Name: getSensorTemp
 * 
 * Description: Retrieve the current fahrenheit temperature reading from the
 *              sensor and return that value back to the caller.
 ****************************************************************************/
double getSensorTemp() {
    tempSensor.requestTemperatures();   // Get the current OneWire reading
    return tempSensor.getTempFByIndex( 0 ); // Return back the first (only) val
}

/*****************************************************************************
 * Name: getSensorHumidity
 * 
 * Description: Retrieve the current humidity reading from the sensor and 
 *              return that value back to the caller.
 ****************************************************************************/
double getSensorHumidity() {
    return (double)0.80;                // Just return 80% for now.
}

/*****************************************************************************
 * Name: calculateHeatIndex
 * 
 * Description: Calculate the current heat index based upon the current 
 *              temperature and humidity sensor readings.
 * 
 *              http://www.srh.noaa.gov/oun/?n=safety-summer-heatindex
 ****************************************************************************/
double calculateHeatIndex(double& pTemperature, double& pHumidity) {
    int T = max((int)pTemperature, 0);  // Local for easier calculations
    double T2 = T * T;                  // Square the temperature value
    double R = max(pHumidity, 0.0);     // Local for easier calculations
    double R2 = R * R;                  // Square the humidity value
    double heatIndex;                   // Store the Heat Index result

    // Define constants to complete the Heat Index function calculations.
    double C1 = -42.379;
    double C2 = 2.04901523;
    double C3 = 10.14333127;
    double C4 = -0.22475541;
    double C5 = -.00683783;
    double C6 = -5.481717E-2;
    double C7 = 1.22874E-3;
    double C8 = 8.5282E-4;
    double C9 = -1.99E-6;

    // Actual function of Calculating Heat Index
    heatIndex = C1 + (C2 * T) + (C3 * R) + (C4 * T * R) + (C5 * T2) + (C6 * R2) + (C7 * T2 * R) + (C8 * T * R2) + (C9 * T2 * R2);
    return heatIndex;
}

/*****************************************************************************
 * Name: evaluateHeatIndex
 * 
 * Description: Evaluate the current heat index to determine if we have an 
 *              ALERT condition where we need to signal/publish an ALERT 
 *              event.
 * 
 *              http://www.srh.noaa.gov/oun/?n=safety-summer-heatindex
 ****************************************************************************/
int evaluateHeatIndex(double& pHeatIndex) {
    char alertClassification[64];       // Used to build the ALERT type
    int savAlertCondition = alertCondition; // Remember its value upon entering
    
    /**
     * Determine based upon the heat index what category the temperature 
     * falls into and set the alertCondition indicator approiately.  
     **/
    if ( pHeatIndex >= (double)130.0 ) { // HeatStroke = Highly likely
        alertCondition = 1;             // Send the ALERT warning.
        sprintf(alertClassification, "ExtremelyHot[%.4f]", pHeatIndex); 
    } else if ( pHeatIndex >= (double)105.0 ) { // HeatStroke = Likely/Possible
        alertCondition = 1;             // Send the ALERT warning.
        sprintf(alertClassification, "VeryHot[%.4f]", pHeatIndex); 
    } else if ( pHeatIndex >= (double)90.0 ) { // HeatExhaustion = Possible
        alertCondition = 1;             // Send the ALERT warning.
        sprintf(alertClassification, "Hot[%.4f]", pHeatIndex); 
    } else if ( pHeatIndex >= (double)80.0 ) { // Fatigue = Possible
        alertCondition = 0;             // Reset the ALERT condition.
        sprintf(alertClassification, "VeryWarm[%.4f]", pHeatIndex); 
    } else {
        alertCondition = 0;             // Reset the ALERT condition.
        sprintf(alertClassification, "Moderate[%.4f]", pHeatIndex); 
    }
    
    /**
     * Check if we have identified an *new* ALERT condition from the heat 
     * index value that we need to publish. 
     **/
    if ( alertCondition==1 && alertCondition!=savAlertCondition ) {
        Particle.publish("hotdog/"PUBLISH_PATH"/alert", String(alertClassification));
        return 1;                       // Indicate an ALERT was issued.
    }
    return 0;                           // Did not issue an ALERT.
}

/*****************************************************************************
 * Name: updateRedLEDValue
 * 
 * Description: This method will take the current temperature (fahrenheit) 
 *              reading and adjust it into an inverted red LED value from 0 
 *              to 255. 
 * 
 *              Any temperature value greater than 80 corresponds to an 
 *              inverted LED value of 255 while a value less than 60 
 *              corresponds to an inverted LED value of 0.
 ****************************************************************************/
int updateRedLEDValue(double& pTemperature) {
    int tempInt = (int)pTemperature;    // Local int for easier comparisons

    if ( overrideAlert || tempInt >= maxHighTemp ) { // Temp exceeds threshold
        return 255;                     // Return max inverted LED value
    } else if ( tempInt <= minLowTemp ) { // Temp exceeds min threshold
        return 0;                       // Return min inverted LED value
    } 

    /** 
     * Since we have already checked both extremes, now we can simply 
     * determine how much each degree from the temperature sensor (above
     * the min threshold) is prorated in the range of LED values.
     **/
    return map(tempInt, minLowTemp, maxHighTemp, 0, 255);
}

/*****************************************************************************
 * Name: updateBlueLEDValue
 * 
 * Description: This method will take the current temperature (fahrenheit) 
 *              reading and adjust it into an inverted blue LED value from
 *              0 to 255. 
 * 
 *              Any temperature value greater than 80 corresponds to an 
 *              inverted LED value of 255 while a value less than 60 
 *              corresponds to an inverted LED value of 0.
 ****************************************************************************/
int updateBlueLEDValue(double& pTemperature) {
    /**
     * In order to determine the inverted value of the Blue LED, we will first 
     * get the inverted value of the Red LED and then negate its value.  In 
     * other words we are relying upon the formula:
     *      RedLEDValue + BlueLEDValue = 255
     **/
    return 255 - updateRedLEDValue(pTemperature);
}

/*****************************************************************************
 * Name: publishTemp
 * 
 * Description: Publish the current temperature reading (pTemperature) and 
 *              the calcualted heat index (pHeatIndex) to all of the clients 
 *              that we are POSTing the information to.
 ****************************************************************************/
int publishTemp(double& pTemperature, double& pHeatIndex) {
    
    /**
     * Only process and publish the temperature sensor values if we have 
     * registered a valid non-zero value.
     **/
    if ( (int)pTemperature > 0 ) {
    	Particle.publish("tempF", String(pTemperature));
        Particle.publish("heatIndexF", String(pHeatIndex));
        publishHttp(pTemperature, pHeatIndex); // Publish sensor to Http Client
    }
    
    return millis();                    // Current time of the last POST
}

/*****************************************************************************
 * Name: publishHttp
 * 
 * Description: Publish the current temperature reading (pTemperature) and 
 *              the calculated heat index (pHeatIndex) to all of the clients 
 *              that we are POSTing the information to.
 ****************************************************************************/
void publishHttp(double& pTemperature, double& pHeatIndex) {
    char httpBody[64];                  // Used to build the Http Body
    
    sprintf(httpBody, "{\"value\":%.4f}", pTemperature); 
    httpRequest.body = httpBody;        // Sending presence to Ubidots
    http.post(httpRequest, httpResponse, httpHeaders);

    /**
     * Output any Serial Debuuging information that has been collected 
     * during this past loop iteration.
     **/
    Serial.print("HTTP Status: " + String(httpResponse.status) + ";");
    Serial.println(" HTTP Data: " + String(httpResponse.body));
}

/*****************************************************************************
 * Name: myAlertHandler
 * 
 * Description: Handle subscribed events to try and determine when external 
 *              conditions exist that would cause us to override the normal 
 *              loop processing and fall into the ALERT path.  
 * 
 *              Examples of cases like this would be when a heat advisory 
 *              warning is issued by the National Weather Service and the 
 *              current heat index does not justify a warning indicator.
 ****************************************************************************/
void myAlertHandler(const char *eventName, const char *eventData) {
    String sData = String(eventData);   // Copy into String object
    String sHeat = "heat";              // Search terms for eventData
    String sAdvisory = "advisory";
    if ( sData.indexOf(sHeat)>=0 && sData.indexOf(sAdvisory)>=0 ) {
        alertCondition = 1;             // Send the ALERT warning.
        
    }

    /**
     * Output any Serial Debuuging information that has been collected 
     * from this Event.
     **/
    Serial.print("Event Name: " + String(eventName) + ";");
    Serial.print("Alert Condition: " + String(alertCondition) + ";");
    Serial.println(" Event Data: " + String(eventData));
}
