// Enabling debug strings over serial increases program size
#define DEBUG_STRINGS false

// Wifi connection details
const char* ssid            = "Starbucks Wifi";
const char* password        = "psk here";
const char* this_hostname   = "PurpleAir Display";

// PurpleAir air quality monitor accessible from your network
const char* api_server      = "192.168.10.10";
const int   api_port        = 80;		// No need to change this
const char* api_url         = "/json";		// No need to change this

// How often to update the displayed information
const uint32_t poll_api_seconds = 40;

// OTA update security
#define OTA_IS_PASSWORD_PROTECTED true
// You can set ota_password to an MD5 hash of the password, which is marginally
// more secure than leaving it as the literal string in case it leaks, but MD5
// isn't very hard to break
#define PASSWORD_IS_MD5_HASH false
// Be good and change this to something more secure, not "12345" or "admin"
const char* ota_password    = "hackme";
