/**
 * ======================================================================================
 * CHƯƠNG TRÌNH MÔ PHỎNG GNSS / GPS ATGM336H (NMEA-0183 MOCK GENERATOR)
 * HỖ TRỢ TỐI ƯU CẢ ESP32 VÀ ARDUINO (UNO / NANO / PRO MINI)
 * ======================================================================================
 * 
 * Mục đích:
 *   - Mô phỏng tín hiệu UART của module định vị vệ tinh ATGM336H chuẩn NMEA-0183 ($GNRMC, $GNGGA).
 *   - Cung cấp tọa độ GPS thực tế lấy từ lộ trình xe buýt: Map/0.geojson (Hà Đông - Thanh Xuân, Hà Nội).
 *   - Tốc độ xe ngẫu nhiên trong dải từ 0 đến 60 km/h (có mô phỏng tăng/giảm tốc và dừng đỗ).
 *   - Tự động lặp lại lộ trình khi đi hết các điểm để hệ thống chạy liên tục khi test.
 * 
 * ======================================================================================
 * SƠ ĐỒ KẾT NỐI VỚI STM32 MASTER:
 * ======================================================================================
 * 
 *  KHI DÙNG ESP32 (KHUYÊN DÙNG - CÙNG MỨC ĐIỆN ÁP 3.3V CHUẨN VỚI STM32):
 *  -------------------------------------------------------------------------------------
 *    ESP32 Pin GPIO 17 (TX2)  --->  STM32 Pin PA3 (USART2_RX)
 *    ESP32 Pin GND            --->  STM32 Pin GND (BẮT BUỘC NỐI CHUNG MASS)
 *    ESP32 Cổng USB           --->  Cắm vào máy tính để nạp code & xem Serial Monitor (115200)
 * 
 *  KHI DÙNG ARDUINO UNO / NANO:
 *  -------------------------------------------------------------------------------------
 *    Arduino TX (Pin D1)      --->  STM32 Pin PA3 (USART2_RX)
 *    Arduino GND              --->  STM32 Pin GND (CHUNG MASS)
 * ======================================================================================
 */

#include <Arduino.h>

#if defined(__AVR__)
  #include <avr/pgmspace.h>
  #include <avr/dtostrf.h>
#else
  #include <pgmspace.h>
#endif

#ifndef DEG_TO_RAD
  #define DEG_TO_RAD 0.017453292519943295769236907684886f
#endif
#ifndef RAD_TO_DEG
  #define RAD_TO_DEG 57.295779513082320876798154814105f
#endif

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2  // LED tích hợp trên hầu hết board ESP32 DevKit
#endif

// ======================================================================================
// CẤU HÌNH CỔNG UART PHÁT DỮ LIỆU
// ======================================================================================
#define GPS_BAUDRATE        9600   // Tốc độ UART chuẩn của ATGM336H và STM32 USART2
#define SEND_INTERVAL_MS    1000   // Tần số phát GPS: 1Hz (1 giây / lần)

#if defined(ESP32)
  // Trên ESP32: Sử dụng Hardware Serial2 cực kỳ ổn định (TX2=GPIO17, RX2=GPIO16)
  // Cổng Serial (USB) dùng song song để xem log máy tính ở 115200 baud
  #define ESP32_TX2_PIN 17  // Nối vào STM32 PA3
  #define ESP32_RX2_PIN 16  // Để trống
  #define GPS_PORT Serial2
#else
  // Cấu hình dành riêng cho vi điều khiển AVR (Arduino Uno / Nano)
  #define USE_SOFTWARE_SERIAL 0   // 0: Dùng Hardware Serial (Pin D1), 1: Dùng SoftwareSerial (Pin D3)
  #if USE_SOFTWARE_SERIAL
    #include <SoftwareSerial.h>
    SoftwareSerial gpsSerial(2, 3); // RX=D2, TX=D3
    #define GPS_PORT gpsSerial
  #else
    #define GPS_PORT Serial
  #endif
#endif

// ======================================================================================
// DỮ LIỆU TỌA ĐỘ TRÍCH XUẤT TỪ Map/0.geojson (53 ĐIỂM DỌC TUYẾN ĐƯỜNG)
// Lưu trong bộ nhớ Flash (PROGMEM) để tiết kiệm bộ nhớ RAM
// Cấu trúc mỗi phần tử: { Latitude (Vĩ độ N), Longitude (Kinh độ E) }
// ======================================================================================
const float WAYPOINTS[][2] PROGMEM = {
  {20.980780f, 105.796110f}, // #0  - Điểm khởi hành (Gần Học Viện Kỹ Thuật Mật Mã)
  {20.980340f, 105.795480f}, // #1
  {20.980220f, 105.795330f}, // #2
  {20.980490f, 105.795090f}, // #3
  {20.981140f, 105.794570f}, // #4
  {20.981400f, 105.794370f}, // #5
  {20.981630f, 105.794190f}, // #6
  {20.981720f, 105.794110f}, // #7
  {20.982240f, 105.793680f}, // #8
  {20.982720f, 105.793270f}, // #9
  {20.983630f, 105.792500f}, // #10
  {20.983670f, 105.792480f}, // #11
  {20.983770f, 105.792610f}, // #12
  {20.984120f, 105.793110f}, // #13
  {20.984310f, 105.793370f}, // #14
  {20.984660f, 105.793870f}, // #15
  {20.984950f, 105.794270f}, // #16
  {20.985380f, 105.794880f}, // #17
  {20.985450f, 105.794980f}, // #18
  {20.985690f, 105.794810f}, // #19
  {20.985620f, 105.794710f}, // #20
  {20.985360f, 105.794360f}, // #21
  {20.984810f, 105.793580f}, // #22
  {20.984500f, 105.793140f}, // #23
  {20.984490f, 105.793130f}, // #24
  {20.984310f, 105.792880f}, // #25
  {20.984000f, 105.792430f}, // #26
  {20.983910f, 105.792320f}, // #27
  {20.983680f, 105.792030f}, // #28
  {20.983500f, 105.791800f}, // #29
  {20.983250f, 105.791470f}, // #30
  {20.983000f, 105.791150f}, // #31
  {20.982880f, 105.791000f}, // #32
  {20.982730f, 105.790820f}, // #33
  {20.982170f, 105.790100f}, // #34
  {20.982010f, 105.789900f}, // #35
  {20.982080f, 105.789840f}, // #36
  {20.982480f, 105.789430f}, // #37
  {20.982560f, 105.789350f}, // #38
  {20.982850f, 105.789070f}, // #39
  {20.983190f, 105.788730f}, // #40
  {20.983390f, 105.788530f}, // #41
  {20.983430f, 105.788490f}, // #42
  {20.983460f, 105.788460f}, // #43
  {20.983680f, 105.788230f}, // #44
  {20.983860f, 105.788030f}, // #45
  {20.983900f, 105.787990f}, // #46
  {20.984350f, 105.787460f}, // #47
  {20.984640f, 105.786950f}, // #48
  {20.984950f, 105.786380f}, // #49
  {20.985400f, 105.785130f}, // #50
  {20.985550f, 105.784660f}, // #51
  {20.985790f, 105.784970f}  // #52 - Điểm cuối tuyến
};

const uint16_t TOTAL_WAYPOINTS = sizeof(WAYPOINTS) / sizeof(WAYPOINTS[0]);

// ======================================================================================
// BIẾN TRẠNG THÁI MÔ PHỎNG
// ======================================================================================
uint16_t currentWpIdx = 0;
uint32_t lastSendTime = 0;

// Giả lập đồng hồ UTC (bắt đầu từ 07:30:00 sáng)
uint8_t utcHour = 7;
uint8_t utcMin  = 30;
uint8_t utcSec  = 0;

// Tốc độ hiện tại (km/h)
float currentSpeedKmh = 0.0f;
float targetSpeedKmh  = 25.0f;
uint8_t stopCountdown = 0; // Đếm ngược số giây xe dừng đón/trả học sinh hoặc đèn đỏ

// ======================================================================================
// HÀM TÍNH CHECKSUM NMEA (XOR từ sau '$' đến trước '*')
// ======================================================================================
uint8_t calculateNmeaChecksum(const char* sentence) {
  uint8_t checksum = 0;
  if (*sentence == '$') sentence++;
  while (*sentence && *sentence != '*') {
    checksum ^= (uint8_t)(*sentence);
    sentence++;
  }
  return checksum;
}

// ======================================================================================
// HÀM TÍNH HƯỚNG DI CHUYỂN (BEARING/COURSE 0 - 360 ĐỘ)
// ======================================================================================
float calculateBearing(float lat1, float lon1, float lat2, float lon2) {
  float dLon = (lon2 - lon1) * DEG_TO_RAD;
  float y = sin(dLon) * cos(lat2 * DEG_TO_RAD);
  float x = cos(lat1 * DEG_TO_RAD) * sin(lat2 * DEG_TO_RAD) -
            sin(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * cos(dLon);
  float bearing = atan2(y, x) * RAD_TO_DEG;
  if (bearing < 0.0f) bearing += 360.0f;
  return bearing;
}

// ======================================================================================
// CẬP NHẬT TỐC ĐỘ XE MÔ PHỎNG (TỪ 0 ĐẾN 60 KM/H)
// ======================================================================================
void updateSimulatedSpeed() {
  // Mô phỏng dừng xe đón/trả học sinh hoặc chờ đèn đỏ
  if (stopCountdown > 0) {
    stopCountdown--;
    currentSpeedKmh = 0.0f;
    return;
  }

  // Xác suất ngẫu nhiên dừng xe (~6% mỗi giây)
  if (random(0, 100) < 6) {
    stopCountdown = random(3, 7);
    currentSpeedKmh = 0.0f;
    targetSpeedKmh = 0.0f;
    return;
  }

  // Chọn tốc độ mục tiêu ngẫu nhiên từ 15 đến 58 km/h (trong giới hạn 0-60 km/h)
  if (random(0, 10) < 3 || abs(targetSpeedKmh - currentSpeedKmh) < 2.0f) {
    targetSpeedKmh = (float)random(15, 58) + ((float)random(0, 10) / 10.0f);
  }

  // Gia tốc tăng/giảm mượt mà thực tế (xe buýt ~ 2-4 km/h mỗi giây)
  if (currentSpeedKmh < targetSpeedKmh) {
    currentSpeedKmh += (float)random(20, 45) / 10.0f;
    if (currentSpeedKmh > targetSpeedKmh) currentSpeedKmh = targetSpeedKmh;
  } else if (currentSpeedKmh > targetSpeedKmh) {
    currentSpeedKmh -= (float)random(25, 50) / 10.0f;
    if (currentSpeedKmh < targetSpeedKmh) currentSpeedKmh = targetSpeedKmh;
  }

  // Giới hạn tuyệt đối từ 0.0 đến 60.0 km/h
  if (currentSpeedKmh < 0.0f) currentSpeedKmh = 0.0f;
  if (currentSpeedKmh > 60.0f) currentSpeedKmh = 60.0f;
}

// ======================================================================================
// TẠO VÀ PHÁT BẢN TIN NMEA $GNRMC (CHUẨN ATGM336H KHỚP VỚI STM32 PARSER)
// ======================================================================================
void sendGnrmcSentence(float lat, float lon, float speedKmh, float courseDeg) {
  // 1. Chuyển đổi Vĩ độ (Latitude) sang định dạng DDMM.MMMM
  int latDeg = (int)lat;
  float latMin = (lat - latDeg) * 60.0f;
  char latStr[12];
  snprintf(latStr, sizeof(latStr), "%02d%07.4f", latDeg, latMin);

  // 2. Chuyển đổi Kinh độ (Longitude) sang định dạng DDDMM.MMMM
  int lonDeg = (int)lon;
  float lonMin = (lon - lonDeg) * 60.0f;
  char lonStr[13];
  snprintf(lonStr, sizeof(lonStr), "%03d%07.4f", lonDeg, lonMin);

  // 3. Chuyển đổi tốc độ km/h sang Knots (1 Knot = 1.852 km/h)
  float speedKnots = speedKmh / 1.852f;
  char speedStr[10];
  dtostrf(speedKnots, 1, 2, speedStr);

  char courseStr[10];
  dtostrf(courseDeg, 1, 1, courseStr);

  // 4. Định dạng thời gian UTC và ngày tháng (23/09/2026)
  char timeStr[12];
  snprintf(timeStr, sizeof(timeStr), "%02d%02d%02d.00", utcHour, utcMin, utcSec);
  const char* dateStr = "230926";

  // 5. Ghép nội dung bản tin NMEA RMC
  // Định dạng chuẩn: $GNRMC,hhmmss.ss,A,ddmm.mmmm,N,dddmm.mmmm,E,speed,course,ddmmyy,,,A*CS
  char payload[100];
  snprintf(payload, sizeof(payload),
           "GNRMC,%s,A,%s,N,%s,E,%s,%s,%s,,,A",
           timeStr, latStr, lonStr, speedStr, courseStr, dateStr);

  // 6. Tính Checksum và phát ra UART nối STM32
  uint8_t cs = calculateNmeaChecksum(payload);
  char nmeaSentence[120];
  snprintf(nmeaSentence, sizeof(nmeaSentence), "$%s*%02X\r\n", payload, cs);

  GPS_PORT.print(nmeaSentence);

#if defined(ESP32) || USE_SOFTWARE_SERIAL
  // In ra cổng USB Serial để người dùng quan sát trên máy tính
  Serial.print(F("[MOCK -> STM32] "));
  Serial.print(nmeaSentence);
  Serial.print(F("        --> Lat: ")); Serial.print(lat, 6);
  Serial.print(F(" | Lon: ")); Serial.print(lon, 6);
  Serial.print(F(" | Speed: ")); Serial.print(speedKmh, 1); Serial.println(F(" km/h"));
#endif
}

// ======================================================================================
// TẠO BẢN TIN BỔ TRỢ $GNGGA (MÔ PHỎNG 8 VỆ TINH, CỐ ĐỊNH 3D-FIX)
// ======================================================================================
void sendGnggaSentence(float lat, float lon) {
  int latDeg = (int)lat;
  float latMin = (lat - latDeg) * 60.0f;
  char latStr[12];
  snprintf(latStr, sizeof(latStr), "%02d%07.4f", latDeg, latMin);

  int lonDeg = (int)lon;
  float lonMin = (lon - lonDeg) * 60.0f;
  char lonStr[13];
  snprintf(lonStr, sizeof(lonStr), "%03d%07.4f", lonDeg, lonMin);

  char timeStr[12];
  snprintf(timeStr, sizeof(timeStr), "%02d%02d%02d.00", utcHour, utcMin, utcSec);

  // $GNGGA,hhmmss.ss,lat,N,lon,E,fixQuality,numSats,HDOP,altitude,M,geoidHeight,M,,*CS
  char payload[100];
  snprintf(payload, sizeof(payload),
           "GNGGA,%s,%s,N,%s,E,1,08,1.2,16.5,M,-28.0,M,,",
           timeStr, latStr, lonStr);

  uint8_t cs = calculateNmeaChecksum(payload);
  char nmeaSentence[120];
  snprintf(nmeaSentence, sizeof(nmeaSentence), "$%s*%02X\r\n", payload, cs);

  GPS_PORT.print(nmeaSentence);
}

// ======================================================================================
// SETUP
// ======================================================================================
void setup() {
#if defined(ESP32)
  // Khởi tạo bộ sinh số ngẫu nhiên phần cứng của ESP32
  randomSeed(esp_random());

  // Serial: Cổng USB để xem log debug trên máy tính (115200 baud)
  Serial.begin(115200);

  // Serial2: Cổng phần cứng UART2 phát tín hiệu NMEA sang STM32 Master (9600 baud)
  Serial2.begin(GPS_BAUDRATE, SERIAL_8N1, ESP32_RX2_PIN, ESP32_TX2_PIN);

  Serial.println(F("\n=================================================="));
  Serial.println(F("  ATGM336H GNSS MOCK FOR STM32 MASTER (ESP32)     "));
  Serial.println(F("  Route: Map/0.geojson (Ha Noi) | Speed: 0-60 km/h "));
  Serial.println(F("  Wiring:                                         "));
  Serial.println(F("    ESP32 GPIO 17 (TX2) ---> STM32 PA3 (USART2_RX)"));
  Serial.println(F("    ESP32 GND           ---> STM32 GND            "));
  Serial.println(F("=================================================="));
#else
  // Cấu hình khi chạy trên Arduino Uno/Nano
  randomSeed(analogRead(A0));
  #if USE_SOFTWARE_SERIAL
    Serial.begin(115200);
    gpsSerial.begin(GPS_BAUDRATE);
  #else
    Serial.begin(GPS_BAUDRATE);
  #endif
#endif

  // Đèn LED tích hợp nhấp nháy báo hiệu phát dữ liệu
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

// ======================================================================================
// LOOP (CHU KỲ 1000ms / 1Hz)
// ======================================================================================
void loop() {
  uint32_t currentMillis = millis();

  if (currentMillis - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = currentMillis;

    // Bật nhấp nháy LED khi phát gói tin
    digitalWrite(LED_BUILTIN, HIGH);

    // 1. Đọc tọa độ điểm hiện tại từ Flash (PROGMEM)
    float curLat = pgm_read_float(&(WAYPOINTS[currentWpIdx][0]));
    float curLon = pgm_read_float(&(WAYPOINTS[currentWpIdx][1]));

    // 2. Điểm kế tiếp để tính góc hướng di chuyển (Bearing)
    uint16_t nextWpIdx = (currentWpIdx + 1) % TOTAL_WAYPOINTS;
    float nextLat = pgm_read_float(&(WAYPOINTS[nextWpIdx][0]));
    float nextLon = pgm_read_float(&(WAYPOINTS[nextWpIdx][1]));
    float bearing = calculateBearing(curLat, curLon, nextLat, nextLon);

    // 3. Cập nhật tốc độ xe mô phỏng ngẫu nhiên từ 0 đến 60 km/h
    updateSimulatedSpeed();

    // 4. Phát các bản tin NMEA chuẩn của ATGM336H
    // Lưu ý: STM32 parser (gps_parser.c) lọc và giải mã bản tin $GNRMC
    sendGnggaSentence(curLat, curLon);
    sendGnrmcSentence(curLat, curLon, currentSpeedKmh, bearing);

    // 5. Tăng thời gian UTC
    utcSec++;
    if (utcSec >= 60) {
      utcSec = 0;
      utcMin++;
      if (utcMin >= 60) {
        utcMin = 0;
        utcHour = (utcHour + 1) % 24;
      }
    }

    // 6. Chuyển sang điểm tọa độ tiếp theo trên lộ trình
    // Nếu xe đang dừng (speed = 0) thì giữ nguyên vị trí, xe chạy thì tiến tới
    if (currentSpeedKmh > 0.5f) {
      currentWpIdx = nextWpIdx;
    }

    digitalWrite(LED_BUILTIN, LOW);
  }
}
