/*********
  Rui Santos
  Complete instructions at: https://RandomNerdTutorials.com/esp32-cam-save-picture-firebase-storage/
  
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

  Based on the example provided by the ESP Firebase Client Library
*********/

#include "WiFi.h"
#include "esp_camera.h"
#include "Arduino.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems
#include "driver/rtc_io.h"
#include <SPIFFS.h>
#include <FS.h>
#include <Firebase_ESP_Client.h>
//Provide the token generation process info.
#include <addons/TokenHelper.h>
#include <ArduinoJson.h>

//Replace with your network credentials
// const char* ssid = "Brawijaya";
// const char* password = "ujungberung";
const char* ssid = "Sandy Asmara";
const char* password = "sandydimas17";

#define FIREBASE_PROJECT_ID "apps-2ee38"

// Insert Firebase project API Key
#define API_KEY "AIzaSyBbOZqg19S67nkfzis-7SXGvaQzN_GPGks"

// Insert Authorized Email and Corresponding Password
#define USER_EMAIL "ghesa@gmail.com"
#define USER_PASSWORD "123qwe"

// Insert Firebase storage bucket ID e.g bucket-name.appspot.com
#define STORAGE_BUCKET_ID "apps-2ee38.appspot.com"
//Define Firebase Data objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configF;

#pragma region Global Variable
void(* resetFunc) (void) = 0;
bool qcMode = true;
int currentTime = 0; // can be minute or hour or day
//"2023-02-26T17:00:00.00000Z"; // ISO 8601/RFC3339 UTC "Zulu" format
int itemCode = 0;
String currentDate = "2023-03-01T00:00:00.00000Z";
int errorCount = 0;
#pragma endregion

String getTimeStampNow() {
  unsigned long randTime = millis();
  String serverTimePath = qcMode ? "SERVERTIMEQC/ServerTimeWatcher" : "SERVERTIME/ServerTimeWatcher";
  FirebaseJson content;
  content.set("fields/CreateBy/stringValue", String(randTime).c_str());
  content.set("fields/LastGet/timestampValue", String(currentDate).c_str());
  if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", serverTimePath.c_str(), content.raw(), "CreateBy,LastGet")) {
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", serverTimePath.c_str())) {
      // Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
      DynamicJsonDocument doc(500);
      DeserializationError error = deserializeJson(doc, fbdo.payload().c_str());
      if (error) {
        Serial.println("[error] deserializeJson() failed: ");
        Serial.println(error.f_str());
        resetIfOverfailed();
        return "";
      }
      const char* timeResult = doc["updateTime"];
      String date = convertDateTime(timeResult);
      return date;
    } else {
      Serial.println("[error] get server time failed!");
      Serial.println(fbdo.errorReason());
      resetIfOverfailed();
      return "";
    }
  } else {
    Serial.println("[error] set server time failed!");
    Serial.println(fbdo.errorReason());
    Serial.printf("[error] input used: ");
    Serial.printf(String(randTime).c_str());
    Serial.printf("\n");
    Serial.printf(String(currentDate).c_str());
    Serial.printf("\n");
    resetIfOverfailed();
    return "";
  }
}

String convertDateTime(const char* date) {
  //https://arduino.stackexchange.com/questions/83860/esp8266-iso-8601-string-to-tm-struct
  struct tm tm = {0};
  char buf[100];
  // Convert to tm struct
  strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);
  // Can convert to any other format
  // strftime(buf, sizeof(buf), "%d %b %Y %H:%M", &tm); //original
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
  // Serial.printf("%s", buf);
  return String(buf);
}

int getSpecificTime(const char* date, char type) {
  struct tm tm = {0};
  char buf[100];
  strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);
  switch (type) {
    case 'y':
      return tm.tm_year;
    case 'm':
      return tm.tm_mon;
    case 'd':
      return tm.tm_mday;
    case 'h':
      return tm.tm_hour;
    case 'M':
      return tm.tm_min;
    default:
      return 0;
  }
}

void resetIfOverfailed() {
  errorCount++;
  if (errorCount >= 4) {
    Serial.println("[info] resetting device ...");
    resetFunc();
  }
}

#pragma region Image capture
// Photo File Name to save in SPIFFS
int i = 1;
#define FILE_PHOTO "/capture.jpg"
#define FILE_PHOTO1 "/capture1.jpg"
#define FILE_PHOTO2 "/capture2.jpg"
#define FILE_PHOTO3 "/capture3.jpg"
#define FILE_PHOTO4 "/capture4.jpg"
#define FILE_PHOTO5 "/capture5.jpg"

// OV2640 camera module pins (CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

boolean takeNewPhoto = true;
bool taskCompleted = false;

String fileName() {
  String file_photo = "";
  switch (i) {
    default:
    case 1:
      file_photo = FILE_PHOTO1;
      break;
    case 2:
      file_photo = FILE_PHOTO2;
      break;
    case 3:
      file_photo = FILE_PHOTO3;
      break;
    case 4:
      file_photo = FILE_PHOTO4;
      break;
    case 5:
      file_photo = FILE_PHOTO5;
      break;
  }
  return file_photo;
}

// Check if photo capture was successful
bool checkPhoto( fs::FS &fs ) {
  File f_pic = fs.open( fileName() );
  unsigned int pic_sz = f_pic.size();
  return ( pic_sz > 100 );
}

// Capture Photo and Save it to SPIFFS
void capturePhotoSaveSpiffs( void ) {
  camera_fb_t * fb = NULL; // pointer
  bool ok = 0; // Boolean indicating if the picture has been taken correctly
  do {
    // Take a photo with the camera
    Serial.println("Taking a photo...");

    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      return;
    }
    // Photo file name
    Serial.printf("Picture file name: ");
    Serial.println(fileName());
    File file = SPIFFS.open(fileName(), FILE_WRITE);
    // Insert the data in the photo file
    if (!file) {
      Serial.println("Failed to open file in writing mode");
    }
    else {
      file.write(fb->buf, fb->len); // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.print(fileName());
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    // Close the file
    file.close();
    esp_camera_fb_return(fb);

    // check if file has been correctly saved in SPIFFS
    ok = checkPhoto(SPIFFS);
  } while ( !ok );
}

void initWiFi(){
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
}

void initSPIFFS(){
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    ESP.restart();
  }
  else {
    delay(500);
    Serial.println("SPIFFS mounted successfully");
  }
}

void initCamera(){
 // OV2640 camera module
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  } 
}
#pragma endregion

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);
  initWiFi();
  initSPIFFS();
  // Turn-off the 'brownout detector'
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  initCamera();

  //Firebase
  // Assign the api key
  configF.api_key = API_KEY;
  //Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  //Assign the callback function for the long running token generation task
  configF.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  Firebase.begin(&configF, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  if (takeNewPhoto) {
    capturePhotoSaveSpiffs();
    if (i == 2) {
      takeNewPhoto = false;
    }
    // takeNewPhoto = false;
  }
  delay(1);
  if (Firebase.ready() && !taskCompleted){
    String _currentDate = getTimeStampNow();
    Serial.println(_currentDate);
    // if (_currentDate != "") {
      currentDate = _currentDate;
      currentDate += currentDate.indexOf("Z") <= 0 ? "Z" : "";
      String _directory = qcMode ? "/camqc" : "/cam";
      _directory += "/" + String(getSpecificTime(currentDate.c_str(), 'y'));
      _directory += "/" + String(getSpecificTime(currentDate.c_str(), 'm'));
      _directory += "/" + String(getSpecificTime(currentDate.c_str(), 'd'));
      _directory += "/" + String(getSpecificTime(currentDate.c_str(), 'h') + 7) + '.' + String(getSpecificTime(currentDate.c_str(), 'M')); 
      _directory += fileName();
      // taskCompleted = true;
      Serial.println(_directory);
      Serial.print("Uploading picture... ");

      //MIME type should be valid to avoid the download problem.
      //The file systems for flash and SD/SDMMC can be changed in FirebaseFS.h.
      if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID /* Firebase Storage bucket id */, fileName() /* path to local file */, mem_storage_type_flash /* memory storage type, mem_storage_type_flash and mem_storage_type_sd */, _directory /* path of remote file stored in the bucket */, "image/jpeg" /* mime type */)){
        Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
        if (i == 2) {
          taskCompleted = true;
          i = 1;
        } else {
          i++;
        }
      }
      else{
        Serial.println(fbdo.errorReason());
      }
    // }
  }
  delay(1000);
}