////////////////////////////////////////////////////////////////////////////////
// Firebase Webhook Config
////////////////////////////////////////////////////////////////////////////////
//
// The Firebase webhook
#define FIREBASE_WEBHOOK_ENABLED true;
//
#ifdef FIREBASE_WEBHOOK_ENABLED
// The interval (in milliseconds) to post to Firebase
const int FIREBASE_WEBHOOK_INTERVAL_MILLISECONDS = 30000;
// Last Firebase update timestamp
long lastFirebaseUpdate = 0;
// The webhook for posting data to Firebase
const char *FIREBASE_POST_WEBHOOK = "firebase-post-data";
#endif


////////////////////////////////////////////////////////////////////////////////
// Pin Constants
////////////////////////////////////////////////////////////////////////////////
//
// The LED pin
const int PIN_LED = D0;
// The button pin
const int PIN_BUTTON = D2;
// The pin for moisture sensor power
const int PIN_MOISTURE_SENSOR_POWER = D3;


////////////////////////////////////////////////////////////////////////////////
// String constants
////////////////////////////////////////////////////////////////////////////////
//
const char *NAME_MOISTURE_SENSOR_VALUE = "moisture";
const char *NAME_MOISTURE_SENSOR_ROLLING_AVERAGE = "moisture_rolling_average";
const char *NAME_MOISTURE_THRESHOLD = "moisture_threshold";
const char *NAME_MOISTURE_STATE = "state";
const char *VALUE_MOISTURE_DRY = "DRY";
const char *VALUE_MOISTURE_WET = "WET";


////////////////////////////////////////////////////////////////////////////////
// Misc constants
////////////////////////////////////////////////////////////////////////////////
//
// The version indicator
const int VERSION = 315;
// The default moisture threshold
const int DEFAULT_MOISTURE_THRESHOLD = 1500;
// The address of the settings in EEPROM
const int ADDRESS_SETTINGS = 10;
// The allowance for the sensor readings
const int SENSOR_ALLOWANCE = 6; // 6%
// The number of readings to use for the moisture rolling average
const int MOISTURE_ROLLING_AVERAGE_COUNT = 5;
// The number of milliseconds to wait after startup to allow the rolling average to settle
const int MOISTURE_ROLLING_AVERAGE_STARTUP_DEFER_MS = 5000;


////////////////////////////////////////////////////////////////////////////////
// Moisture state values
////////////////////////////////////////////////////////////////////////////////
//
const int STATE_MOISTURE_DRY = 0;
const int STATE_MOISTURE_WET = 1;


////////////////////////////////////////////////////////////////////////////////
// Structures
////////////////////////////////////////////////////////////////////////////////
//
// A structure to hold settings
struct Settings {
    int version;
    int moistureThreshold;
};


////////////////////////////////////////////////////////////////////////////////
// Global variables
////////////////////////////////////////////////////////////////////////////////
//
// Get the initial state
int state = -1; // Initial state (unset)
// The moisture sensor value
int moistureSensorValue;
// The rolling average
int moistureSensorRollingAverage = -1;
// The settings
Settings settings;


////////////////////////////////////////////////////////////////////////////////
// Public API
////////////////////////////////////////////////////////////////////////////////
/**
 * The setup loop. Configures the pins, gets settings and performs a reset if
 * requested. Also binds globals (moisture, threshold and state) for publish.
 *
 */
void setup() {
    // Setup the LED pin
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    // Setup the sensor power pin
    pinMode(PIN_MOISTURE_SENSOR_POWER, OUTPUT);
    digitalWrite(PIN_MOISTURE_SENSOR_POWER, LOW);

    // Setup the button pin
    pinMode(PIN_BUTTON, INPUT_PULLUP);


    // If the button is pressed during setup, reset the settings
    if (LOW == digitalRead(PIN_BUTTON)) {
        handleClearSettings();
    }


    // Get the settings from storage
    unpersistSettings();
    // Publish the threshold (for logging purposes)
    publishThreshold();


    // Bind variables for publishing
    //
    // Configure the moisture sensor variable to be published
    Particle.variable(NAME_MOISTURE_SENSOR_VALUE, &moistureSensorValue, INT);
    // Configure the moisture rolling threshold to be published
    Particle.variable(NAME_MOISTURE_SENSOR_ROLLING_AVERAGE, &moistureSensorRollingAverage, INT);
    // Configure the moisture threshold variable to be published
    Particle.variable(NAME_MOISTURE_THRESHOLD, &settings.moistureThreshold, INT);
    // Configure the state variable to be published
    Particle.variable(NAME_MOISTURE_STATE, &state, INT);
}

/**
 * The main loop. Reads the moisture level, takes action on the level and
 * handles calibration requests.
 *
 */
void loop() {
    // Read the moisture value
    readMoistureValue();

    // Process the moisture value
    handleMoistureReading();


    // Set the LED status
    if (STATE_MOISTURE_DRY == state) {
        digitalWrite(PIN_LED, HIGH);
    } else {
        digitalWrite(PIN_LED, LOW);
    }


    // If the calibrate button is pressed, then recalibrate the moisture threshold
    if(LOW == digitalRead(PIN_BUTTON)) {
        // Remember to debounce (if not debounced at the end of the loop)
        handleSetMoistureThreshold();
    }

    // Sleep for one second
    //
    // This debounces the calibration button press and also is used to
    // give the moisture sensor a break
    delay(1000);
}


////////////////////////////////////////////////////////////////////////////////
// Private API / Helper functions
////////////////////////////////////////////////////////////////////////////////
//
/**
 * Blinks the LED.
 *
 * @param number the number of times to blink
 * @param interval the number of milliseconds to pause after each blink
 */
void blinkLED(int number, int interval) {
    for (int i=0; i<number; i++) {
        digitalWrite(PIN_LED, LOW);
        delay(interval);
        digitalWrite(PIN_LED, HIGH);
        delay(interval);
    }
}


////////////////////////////////////////////////////////////////////////////////
// Handler functions
//
/**
 * Clears the settings from storage.
 *
 */
void handleClearSettings() {
    // Blink the LED 5 times to indicate that settings are being cleared
    blinkLED(5, 250);
    // Clear the settings
    clearSettings();
    // Keep the light on as an indicator to the user
    delay(1500);
    // Blink the LED 10 times to indicate successful reset
    blinkLED(10, 250);
}

/**
 * Sets the moisture threshold. Blinks the LED 3 times to indicate to the user
 * that calibration is starting. Sets, saves and publishes the new value.
 *
 */
void handleSetMoistureThreshold() {
    // Blink the LED three times to indicate that the recalibrate request has
    // been recognized
    blinkLED(3, 250);

    // Set the current moisture value as the threshold
    settings.moistureThreshold = moistureSensorValue;

    // Save the new threshold
    persistSettings();

    // Publish the new threshold
    publishThreshold();

    // Finish by turning off the LED
    digitalWrite(PIN_LED, LOW);

    // Wait before resuming
    delay(500);
}

/**
 * Changes moisture state (if needed); doing so will publish a state change and
 * alter the LED status (on or off).
 *
 */
void handleMoistureReading() {

    if (millis() <= MOISTURE_ROLLING_AVERAGE_STARTUP_DEFER_MS) {
        return;
    }

    int moistureValue = moistureSensorRollingAverage;
    // int moistureValue = moistureSensorValue;

    // TODO: Use the rolling average!!!
    if (-1 == state) {
        // There is no state; set the state to the current state
        state = moistureValue < settings.moistureThreshold ? STATE_MOISTURE_DRY : STATE_MOISTURE_WET;

        // Invoke a state change
        publishCurrentState();
    }

    // If the previous state is DRY AND the moisture sensor value is above the threshold + allowance
    else if (state != STATE_MOISTURE_WET && moistureValue > settings.moistureThreshold + SENSOR_ALLOWANCE) {
        // Change state to WET
        state = STATE_MOISTURE_WET;

        // Invoke a state change
        publishCurrentState();
    }

    // If the previous state is WET AND the moisture sensor value is below the threshold - allowance
    else if (state != STATE_MOISTURE_DRY && moistureValue < settings.moistureThreshold - SENSOR_ALLOWANCE) {
        // Change state to dry
        state = STATE_MOISTURE_DRY;

        // Invoke a state change
        publishCurrentState();
    }

    #ifdef FIREBASE_WEBHOOK_ENABLED
    // Update Firebase every 30 seconds
    if (millis() - lastFirebaseUpdate > FIREBASE_WEBHOOK_INTERVAL_MILLISECONDS) {
        invokeFirebaseWebhook();
        lastFirebaseUpdate = millis();
    }
    #endif
}


////////////////////////////////////////////////////////////////////////////////
// Publish functions
//
/**
 * Publishes the threshold.
 *
 */
void publishThreshold() {
    char stringValue[40];
    sprintf(stringValue, "%d", settings.moistureThreshold);
    Particle.publish(NAME_MOISTURE_THRESHOLD, stringValue);

    #ifdef FIREBASE_WEBHOOK_ENABLED
    // Invoke the Firebase Webhook
    invokeFirebaseWebhook();
    #endif
}

/**
 * Publishes the current state.
 *
 */
void publishCurrentState() {
    if (STATE_MOISTURE_DRY == state) {
        Particle.publish(NAME_MOISTURE_STATE, VALUE_MOISTURE_DRY);
    } else if (STATE_MOISTURE_WET == state) {
        Particle.publish(NAME_MOISTURE_STATE, VALUE_MOISTURE_WET);
    }

    #ifdef FIREBASE_WEBHOOK_ENABLED
    // Invoke the Firebase Webhook
    invokeFirebaseWebhook();
    #endif
}


#ifdef FIREBASE_WEBHOOK_ENABLED
/**
 * Invokes the Firebase Webhook.
 *
 */
void invokeFirebaseWebhook() {
    // Invoke the web hook
    //
    // The payload
    char payload[128];
    // Format the payload
    sprintf(
            payload,
            "{\"moisture\": %d, \"state\": \"%s\", \"threshold\": %d, \"rollingAverage\": %d}",
            moistureSensorValue,
            STATE_MOISTURE_DRY == state ? VALUE_MOISTURE_DRY : VALUE_MOISTURE_WET,
            settings.moistureThreshold,
            moistureSensorRollingAverage
        );
    // Send the payload
    Particle.publish(FIREBASE_POST_WEBHOOK, payload);
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Setting functions
////////////////////////////////////////////////////////////////////////////////
//
/**
 * Clears settings from storage.
 *
 */
void clearSettings() {
    EEPROM.clear();
}

/**
 * Saves settings to storage.
 *
 */
void persistSettings() {
    EEPROM.put(ADDRESS_SETTINGS, settings);
}

/**
 * Gets settings from storage. The settings will be copied to "settings".
 *
 */
void unpersistSettings() {
    EEPROM.get(ADDRESS_SETTINGS, settings);

    // If the version is -1, storage has not been set; save default values
    if (-1 == settings.version) {
        // There are no settings; set the default values
        Particle.publish("no settings found", "persisting default settings");
        // Create the settings
        settings = { VERSION, DEFAULT_MOISTURE_THRESHOLD };
        // Save the settings
        persistSettings();
    } else if (VERSION != settings.version) {
        // Add upgrade tasks here
        char title[75]; sprintf(title, "old version detected (%d)", settings.version);
        char message[75]; sprintf(message, "upgrading to new version (%d)", VERSION);

        // Publish an upgrade message
        Particle.publish(title, message);

        // Set the new version
        settings.version = VERSION;
        // Persist the new version
        persistSettings();
    }
}


////////////////////////////////////////////////////////////////////////////////
// Sensor functions
//
/**
 * Reads the moisture value. The value will be read into "moistureSensorValue".
 *
 */
void readMoistureValue() {
    // Turn on the sensor
    digitalWrite(PIN_MOISTURE_SENSOR_POWER, HIGH);

    // Give the sensor some time to turn on
    delay(50);

    // Read the value and store in the sensorValue global
    moistureSensorValue = map(analogRead(A0), 0, 4095, 0, 100);

    // If the rolling average has not been set, the initial value should be the first reading
    if (-1 == moistureSensorRollingAverage) {
        moistureSensorRollingAverage = moistureSensorValue;
    }

    //long unfolded = MOISTURE_ROLLING_AVERAGE_COUNT * moistureSensorRollingAverage;
    // The rolling average is:
    //   unfolded valuie = the rolling average * the count
    //   rolling average = (unfolded - current rolling average + current reading)/the count
    moistureSensorRollingAverage = ((MOISTURE_ROLLING_AVERAGE_COUNT * moistureSensorRollingAverage) - moistureSensorRollingAverage + moistureSensorValue)/MOISTURE_ROLLING_AVERAGE_COUNT;

    // Turn off the sensor
    digitalWrite(PIN_MOISTURE_SENSOR_POWER, LOW);
}
