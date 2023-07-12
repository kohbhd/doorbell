/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.
https://community.platformio.org/t/esp32-ld-region-dram0-0-seg-overflowed-by-156768-bytes/18753
https://esp32.com/viewtopic.php?t=6699
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "TFMicroCamera.h"
#include <WiFi.h>
#include <PubSubClient.h>

#include "detection_responder.h"
#include "image_provider.h"
#include "model_settings.h"
#include "person_detect_model_data.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

// Globals, used for compatibility with Arduino-style sketches.
namespace {
tflite::ErrorReporter* error_reporter = nullptr;
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;

// An area of memory to use for input, output, and intermediate arrays.
//constexpr int kTensorArenaSize = 93 * 1024;
constexpr int kTensorArenaSize = 86 * 1024;
// KBH alignas(16) static uint8_t tensor_arena[kTensorArenaSize];
alignas(16) static uint8_t *tensor_arena;
const char* ssid = "SINGTEL-5PUR";
const char* password = "94ubbn885r";
IPAddress mqtt_server = IPAddress(192, 168, 1, 90); 
}  // namespace

WiFiClient espClient;
PubSubClient client(espClient);
void callback(char* topic, byte* payload, unsigned int length);
void reConnect();

void setupWifi() 
{
  delay(10);
  Serial.printf("Connecting to %s",ssid);

  WiFi.mode(WIFI_STA);  //Set the mode to WiFi station mode.
  WiFi.begin(ssid, password); //Start Wifi connection.

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nSuccess\n");
  Serial.print(WiFi.localIP());
}

// The name of this function is important for Arduino compatibility.
void setup() 
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  setupWifi();

  client.setServer(mqtt_server, 1883);  //Sets the server details.
  client.setCallback(callback); //Sets the message callback function.

  tensor_arena = (uint8_t *)malloc(kTensorArenaSize);
 
  // Set up logging. Google style is to avoid globals or statics because of
  // lifetime uncertainty, but since this has a trivial destructor it's okay.
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_person_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    TF_LITE_REPORT_ERROR(error_reporter,
                         "Model provided is schema version %d not equal "
                         "to supported version %d.",
                         model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  //
  // tflite::AllOpsResolver resolver;
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroMutableOpResolver<3> micro_op_resolver;
  micro_op_resolver.AddBuiltin(
      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_AVERAGE_POOL_2D,
                               tflite::ops::micro::Register_AVERAGE_POOL_2D());

  // Build an interpreter to run the model with.
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
    return;
  }

  // Get information about the memory area to use for the model's input.
  input = interpreter->input(0);
}

// The name of this function is important for Arduino compatibility.
void loop() {
  if (!client.connected()) {
    reConnect();
  }
  client.loop();  //This function is called periodically to allow clients to process incoming messages and maintain connections to the server.

  // Get image from provider.
  if (kTfLiteOk != GetImage(error_reporter, kNumCols, kNumRows, kNumChannels,
                            input->data.uint8)) {
    TF_LITE_REPORT_ERROR(error_reporter, "Image capture failed.");
  }

  // Run the model on this input and make sure it succeeds.
  if (kTfLiteOk != interpreter->Invoke()) {
    TF_LITE_REPORT_ERROR(error_reporter, "Invoke failed.");
  }

  TfLiteTensor* output = interpreter->output(0);

  // Process the inference results.
  uint8_t person_score = output->data.uint8[kPersonIndex];
  uint8_t no_person_score = output->data.uint8[kNotAPersonIndex];
  RespondToDetection(error_reporter, person_score, no_person_score);
}

void callback(char* topic, byte* payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void reConnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID.
    String clientId = "M5Stack-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect.
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement to the topic.
      client.publish("M5Atom", "Status: Connected");
      // ... and resubscribe.
      client.subscribe("M5Atom");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println("try again in 5 seconds");
      delay(2000);
    }
  }
}