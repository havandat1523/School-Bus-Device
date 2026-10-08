/**
 * ======================================================================================
 * CHƯƠNG TRÌNH ESP32 MÔ PHỎNG RASPBERRY PI (RASPBERRY PI SIMULATOR / SNIFFER)
 * ======================================================================================
 * 
 * Mục đích:
 *   - Thay thế Raspberry Pi trong quá trình thử nghiệm phần cứng và firmware STM32 Master.
 *   - NHẬN và GIẢI MÃ toàn bộ gói tin từ STM32 Master (GPS, Trạng thái 16 ghế, Quẹt thẻ RFID,
 *     Nút bấm SOS, Cảm biến nhiệt độ/độ ẩm DHT11) -> Hiển thị trực quan lên Serial Terminal.
 *   - GỬI LỆNH ĐIỀU KHIỂN từ Pi xuống Master thông qua Menu tương tác trên bàn phím máy tính
 *     (Phát âm thanh loa DFPlayer, Kích hoạt cảnh báo nguy hiểm 08/001, Hủy cảnh báo 0xF4...).
 * 
 * SƠ ĐỒ NỐI DÂY GIỮA ESP32 VÀ STM32 MASTER:
 * --------------------------------------------------------------------------------------
 *   ESP32 Pin GPIO 16 (RX2)  <---  STM32 Pin PA9 (USART1_TX)   [Nhận dữ liệu từ Master]
 *   ESP32 Pin GPIO 17 (TX2)  --->  STM32 Pin PA10 (USART1_RX)  [Gửi lệnh xuống Master]
 *   ESP32 Pin GND            <-->  STM32 Pin GND               [BẮT BUỘC CHUNG MASS]
 *   ESP32 Cổng USB           --->  Cắm vào máy tính (Baud 115200) để xem Terminal & gõ phím
 * ======================================================================================
 */

#include <Arduino.h>

// ======================================================================================
// ĐỊNH NGHĨA GIAO THỨC UART MASTER <-> PI
// ======================================================================================
#define PI_UART_BAUDRATE 115200   // Tốc độ UART chuẩn giữa STM32 Master và Pi
#define PI_FRAME_STX     0xAA     // Byte mở đầu khung truyền (170)
#define PI_FRAME_ETX     0x55     // Byte kết thúc khung truyền (85)

// Chân UART phần cứng Serial2 trên ESP32
#define PIN_RX2 16  // Nối với PA9 của STM32
#define PIN_TX2 17  // Nối với PA10 của STM32

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

// Trạng thái máy giải mã khung truyền
enum RxState {
  STATE_STX,
  STATE_MAIN,
  STATE_SUB,
  STATE_LEN,
  STATE_DATA,
  STATE_CHECKSUM,
  STATE_ETX
};

RxState rxState = STATE_STX;
uint8_t rxMain = 0;
uint8_t rxSub = 0;
uint8_t rxLen = 0;
uint8_t rxData[150];
uint8_t rxDataIdx = 0;
uint8_t rxExpectedChk = 0;

// Thống kê số lượng gói tin
uint32_t totalPacketsRecv = 0;
uint32_t gpsPacketsRecv   = 0;
uint32_t rfidPacketsRecv  = 0;
uint32_t seatPacketsRecv  = 0;
uint32_t sosPacketsRecv   = 0;

// ======================================================================================
// HÀM GỬI KHUNG TRUYỀN TỪ PI (ESP32) XUỐNG STM32 MASTER
// Format chuẩn: [0xAA] [MAIN] [SUB] [LEN] [DATA...] [XOR_CHK] [0x55]
// ======================================================================================
void sendFrameToMaster(uint8_t main_evt, uint8_t sub_evt, const uint8_t *data = NULL, uint8_t len = 0) {
  uint8_t tx_buf[160];
  tx_buf[0] = PI_FRAME_STX;
  tx_buf[1] = main_evt;
  tx_buf[2] = sub_evt;
  tx_buf[3] = len;

  uint8_t chk = main_evt ^ sub_evt ^ len;
  for (uint8_t i = 0; i < len; i++) {
    tx_buf[4 + i] = data[i];
    chk ^= data[i];
  }

  tx_buf[4 + len] = chk;
  tx_buf[5 + len] = PI_FRAME_ETX;

  // Gửi ra Serial2 tới STM32 Master
  Serial2.write(tx_buf, 6 + len);

  // In thông báo phản hồi lên màn hình USB Serial
  Serial.println(F("\n------------------------------------------------------------"));
  Serial.print(F(">>> [PI -> MASTER SENT] Main=0x"));
  if (main_evt < 0x10) Serial.print(F("0"));
  Serial.print(main_evt, HEX);
  Serial.print(F(", Sub=0x"));
  if (sub_evt < 0x10) Serial.print(F("0"));
  Serial.print(sub_evt, HEX);
  Serial.print(F(", Len="));
  Serial.print(len);
  Serial.print(F(" | Raw: "));
  for (uint8_t i = 0; i < 6 + len; i++) {
    if (tx_buf[i] < 0x10) Serial.print(F("0"));
    Serial.print(tx_buf[i], HEX);
    Serial.print(F(" "));
  }
  Serial.println();
  Serial.println(F("------------------------------------------------------------"));
}

// ======================================================================================
// BẢNG SỰ KIỆN CHUẨN ĐẶC TẢ V1.3.0 (MỤC 3.2)
// EVENT_MAIN / EVENT_SUB tương ứng chính xác với Track âm thanh trên DFPlayer
// ======================================================================================
struct EventDef {
  uint8_t main_evt;
  uint8_t sub_evt;
  const char* codeStr;
  const char* desc;
  const char* defaultPayload;
};

const EventDef EVENT_LIST[] = {
  { 0x01, 0x01, "01/001", "Tai xe login thanh cong", "GV0012" },
  { 0x01, 0x02, "01/002", "Tai xe login that bai", NULL },
  { 0x02, 0x01, "02/001", "Tai xe logout thanh cong", NULL },
  { 0x02, 0x02, "02/002", "Tai xe logout that bai", NULL },
  { 0x03, 0x01, "03/001", "Phu xe login thanh cong", "PX0003" },
  { 0x03, 0x02, "03/002", "Phu xe login that bai", NULL },
  { 0x04, 0x01, "04/001", "Phu xe logout thanh cong", NULL },
  { 0x04, 0x02, "04/002", "Phu xe logout that bai", NULL },
  { 0x05, 0x01, "05/001", "Hoc sinh quet the thanh cong", "639E74E4" },
  { 0x05, 0x02, "05/002", "Ma RFID khong hop le / loi quet the", "UNKNOWN" },
  { 0x05, 0x03, "05/003", "Hoc sinh cuoi cung da len xe (chieu don)", NULL },
  { 0x05, 0x04, "05/004", "Hoc sinh cuoi cung da xuong xe tai nha (chieu tra - nhac kiem tra xe)", NULL },
  { 0x05, 0x05, "05/005", "Co hoc sinh tren ghe chua quet the", NULL },
  { 0x05, 0x06, "05/006", "Xuong xe hang loat tai truong, da khop so luong (buoi sang - nhac kiem tra xe)", NULL },
  { 0x05, 0x07, "05/007", "Len xe hang loat tai truong (bulk, khop so luong) - chieu ve", NULL },
  { 0x06, 0x01, "06/001", "Khuon mat tai xe khong khop luc kiem tra dinh ky (3 phut)", NULL },
  { 0x06, 0x02, "06/002", "Phu xe check-in sau khi da co hoc sinh tren xe", NULL },
  { 0x06, 0x03, "06/003", "Phu xe khong co mat tai ghe (> 3 phut)", NULL },
  { 0x07, 0x01, "07/001", "Nhan nut SOS khan cap", NULL },
  { 0x08, 0x01, "08/001", "CANH BAO NGUY HIEM: Co hoc sinh tren xe nhung khong co TX/PX (bo quen tre)", NULL }
};
const uint8_t TOTAL_EVENTS = sizeof(EVENT_LIST) / sizeof(EVENT_LIST[0]);

// ======================================================================================
// IN MENU TƯƠNG TÁC LÊN TERMINAL MÁY TÍNH (KHỚP 100% ĐẶC TẢ V1.3.0 MỤC 3.2)
// ======================================================================================
void printInteractiveMenu() {
  Serial.println(F("\n========================================================================================"));
  Serial.println(F("         ESP32 RASPBERRY PI SIMULATOR - BẢNG SỰ KIỆN CHUẨN ĐẶC TẢ V1.3.0 (MỤC 3.2)       "));
  Serial.println(F("========================================================================================"));
  Serial.println(F("  [NHÓM 01: ĐĂNG NHẬP TÀI XẾ]"));
  Serial.println(F("    1  : [01/001] Tài xế login thành công"));
  Serial.println(F("    2  : [01/002] Tài xế login thất bại"));
  Serial.println(F("  [NHÓM 02: ĐĂNG XUẤT TÀI XẾ]"));
  Serial.println(F("    3  : [02/001] Tài xế logout thành công"));
  Serial.println(F("    4  : [02/002] Tài xế logout thất bại"));
  Serial.println(F("  [NHÓM 03: ĐĂNG NHẬP PHỤ XE]"));
  Serial.println(F("    5  : [03/001] Phụ xe login thành công"));
  Serial.println(F("    6  : [03/002] Phụ xe login thất bại"));
  Serial.println(F("  [NHÓM 04: ĐĂNG XUẤT PHỤ XE]"));
  Serial.println(F("    7  : [04/001] Phụ xe logout thành công"));
  Serial.println(F("    8  : [04/002] Phụ xe logout thất bại"));
  Serial.println(F("  [NHÓM 05: ĐIỂM DANH HỌC SINH & LỘ TRÌNH TUYẾN]"));
  Serial.println(F("    9  : [05/001] Học sinh quẹt thẻ thành công"));
  Serial.println(F("    10 : [05/002] Mã RFID không hợp lệ / lỗi quẹt thẻ"));
  Serial.println(F("    11 : [05/003] Học sinh cuối cùng đã lên xe (chiều đón về trường)"));
  Serial.println(F("    12 : [05/004] Học sinh cuối cùng xuống xe tại nhà (chiều trả - nhắc kiểm tra xe)"));
  Serial.println(F("    13 : [05/005] Có học sinh trên ghế chưa quẹt thẻ"));
  Serial.println(F("    14 : [05/006] Xuống xe hàng loạt tại trường (buổi sáng - nhắc kiểm tra xe)"));
  Serial.println(F("    15 : [05/007] Lên xe hàng loạt tại trường (buổi chiều về)"));
  Serial.println(F("  [NHÓM 06: GIÁM SÁT VI PHẠM / BẤT THƯỜNG]"));
  Serial.println(F("    16 : [06/001] Khuôn mặt tài xế không khớp định kỳ (3 phút)"));
  Serial.println(F("    17 : [06/002] Phụ xe check-in sau khi đã có học sinh trên xe"));
  Serial.println(F("    18 : [06/003] Phụ xe không có mặt tại ghế (> 3 phút)"));
  Serial.println(F("  [NHÓM 07: BÁO ĐỘNG SOS KHẨN CẤP]"));
  Serial.println(F("    19 : [07/001] Nhấn nút SOS khẩn cấp"));
  Serial.println(F("  [NHÓM 08: CẢNH BÁO NGUY HIỂM TÍNH MẠNG - BỎ QUÊN HỌC SINH!]"));
  Serial.println(F("    20 : [08/001] 🚨 CẢNH BÁO: BỎ QUÊN TRẺ TRÊN XE (Còi hú lặp lại liên tục)"));
  Serial.println(F("  [LỆNH ĐẶC BIỆT MỤC 3.4: HỦY BÁO ĐỘNG NGUY HIỂM 08/001]"));
  Serial.println(F("    c  : [0xF4]  🛑 HỦY CẢNH BÁO NGUY HIỂM 08/001 (Tắt còi hú lặp lại)"));
  Serial.println(F("    m  : In lại bảng Menu hướng dẫn này"));
  Serial.println(F("----------------------------------------------------------------------------------------"));
  Serial.println(F("  * CÁCH NHẬP:"));
  Serial.println(F("    - Gõ số thứ tự: từ 1 đến 20, hoặc 'c' để gửi lệnh."));
  Serial.println(F("    - HOẶC gõ trực tiếp mã sự kiện: '01/001', '05/006', '08/001', 'F4'..."));
  Serial.println(F("    - HOẶC gõ kèm Payload (mục 3.3): ví dụ '01/001 GV0012' hoặc '05/001 639E74E4'"));
  Serial.println(F("========================================================================================\n"));
}

// ======================================================================================
// GIẢI MÃ VÀ HIỂN THỊ GÓI TIN NHẬN ĐƯỢC TỪ STM32 MASTER
// ======================================================================================
void processReceivedFrame(uint8_t main_evt, uint8_t sub_evt, const uint8_t *data, uint8_t len) {
  totalPacketsRecv++;
  digitalWrite(LED_BUILTIN, HIGH);

  char payloadStr[150];
  uint8_t strLen = (len < sizeof(payloadStr) - 1) ? len : sizeof(payloadStr) - 1;
  memcpy(payloadStr, data, strLen);
  payloadStr[strLen] = '\0';

  Serial.println();
  Serial.print(F("[MASTER -> PI] Time: "));
  Serial.print(millis() / 1000);
  Serial.print(F("s | Packet #"));
  Serial.print(totalPacketsRecv);
  Serial.print(F(" | Type: 0x"));
  if (main_evt < 0x10) Serial.print(F("0"));
  Serial.print(main_evt, HEX);

  switch (main_evt) {
    // ----------------------------------------------------------------------------------
    // EVENT 0xF0: TỌA ĐỘ GNSS / TỐC ĐỘ XE
    // ----------------------------------------------------------------------------------
    case 0xF0: {
      gpsPacketsRecv++;
      Serial.println(F(" [GNSS TELEMETRY]"));
      Serial.print(F("   + Payload: \""));
      Serial.print(payloadStr);
      Serial.println(F("\""));

      // Tách lat, lon, speed
      char *token1 = strtok(payloadStr, ",");
      char *token2 = strtok(NULL, ",");
      char *token3 = strtok(NULL, ",");

      if (token1 && token2 && token3) {
        float lat = atof(token1);
        float lon = atof(token2);
        float speed = atof(token3);

        Serial.print(F("   + Vĩ độ (Lat) : ")); Serial.println(lat, 6);
        Serial.print(F("   + Kinh độ(Lon): ")); Serial.println(lon, 6);
        Serial.print(F("   + Vận tốc     : ")); Serial.print(speed, 1); Serial.println(F(" km/h"));

        // Kiểm tra xem đây là tọa độ mặc định hay tọa độ thực
        if (abs(lat - 21.002100) < 0.0001 && abs(lon - 105.846200) < 0.0001) {
          Serial.println(F("   --> [TRẠNG THÁI]: TỌA ĐỘ MẶC ĐỊNH (DEFAULT FALLBACK DO CHƯA CÓ GPS THẬT)"));
        } else {
          Serial.println(F("   --> [TRẠNG THÁI]: TỌA ĐỘ GPS THỰC TẾ ĐANG CẬP NHẬT TỪ THIẾT BỊ!"));
        }
      } else {
        Serial.println(F("   --> [CHÚ Ý]: Chuỗi GNSS bị trống vĩ độ/kinh độ (\",,0.0\") do Master chưa bật float printf hoặc chưa nạp bản fix!"));
      }
      break;
    }

    // ----------------------------------------------------------------------------------
    // EVENT 0xF1: TRẠNG THÁI 16 GHẾ NGỒI TRÊN XE
    // ----------------------------------------------------------------------------------
    case 0xF1: {
      seatPacketsRecv++;
      Serial.println(F(" [16 SEATS STATUS]"));
      Serial.print(F("   + Chuỗi ghế: "));
      Serial.println(payloadStr);
      
      // Đếm số ghế có người ngồi
      int occupiedCount = 0;
      char *ptr = payloadStr;
      while ((ptr = strstr(ptr, ":1")) != NULL) {
        occupiedCount++;
        ptr += 2;
      }
      Serial.print(F("   + Số vị trí có người: "));
      Serial.print(occupiedCount);
      Serial.println(F(" / 16 ghế"));
      break;
    }

    // ----------------------------------------------------------------------------------
    // EVENT 0xF2: QUẸT THẺ RFID (HỌC SINH / PHỤ XE)
    // ----------------------------------------------------------------------------------
    case 0xF2: {
      rfidPacketsRecv++;
      Serial.println(F(" [RFID CARD SCANNED] 💳"));
      Serial.print(F("   + Mã thẻ (UID): [ "));
      Serial.print(payloadStr);
      Serial.println(F(" ]"));
      Serial.println(F("   --> Pi thật sẽ đối chiếu CSDL: Học sinh lên/xuống xe hoặc Phụ xe điểm danh."));
      break;
    }

    // ----------------------------------------------------------------------------------
    // EVENT 0xF3: CẢNH BÁO NÚT BẤM KHẨN CẤP SOS
    // ----------------------------------------------------------------------------------
    case 0xF3: {
      sosPacketsRecv++;
      uint8_t sos_state = (rxLen > 0) ? rxData[0] : 1;
      if (sos_state == 1) {
        Serial.println(F(" [SOS ALERT TRIGGERED!] 🚨🚨🚨"));
        Serial.println(F("   + NÚT BẤM KHẨN CẤP TRÊN XE VỪA ĐƯỢC KÍCH HOẠT!"));
        Serial.println(F("   --> Mô phỏng Pi: Tự động gửi lệnh 07/001 xuống Master để kích hoạt loa DFPlayer..."));
        sendFrameToMaster(0x07, 0x01);
      } else {
        Serial.println(F(" [SOS ALERT CANCELLED] 🟢"));
        Serial.println(F("   + NÚT KHẨN CẤP ĐÃ ĐƯỢC NHẤN LẦN 2 ĐỂ HỦY BÁO ĐỘNG (CÒI SLAVE ĐÃ TẮT)!"));
      }
      break;
    }

    // ----------------------------------------------------------------------------------
    // EVENT 0xF5: NHIỆT ĐỘ & ĐỘ ẨM DHT11
    // ----------------------------------------------------------------------------------
    case 0xF5: {
      Serial.println(F(" [DHT11 SENSOR] 🌡️"));
      Serial.print(F("   + Nhiệt độ & Độ ẩm: "));
      Serial.println(payloadStr);
      break;
    }

    // ----------------------------------------------------------------------------------
    // EVENT 0x08: CẢNH BÁO TỪ SLAVE
    // ----------------------------------------------------------------------------------
    case 0x08: {
      if (sub_evt == 0x02) {
        Serial.println(F(" [SLAVE NODE OFFLINE] ⚠️"));
        Serial.println(F("   + CẢNH BÁO: Mất tín hiệu nhịp tim Heartbeat từ hộp Slave (CAN bus)!"));
      } else {
        Serial.print(F(" [ALARM EVENT] Sub=0x"));
        Serial.println(sub_evt, HEX);
      }
      break;
    }

    // CÁC SỰ KIỆN KHÁC
    default: {
      Serial.println(F(" [OTHER EVENT]"));
      Serial.print(F("   + Sub: 0x")); Serial.println(sub_evt, HEX);
      Serial.print(F("   + Data Hex: "));
      for (uint8_t i = 0; i < len; i++) {
        if (data[i] < 0x10) Serial.print(F("0"));
        Serial.print(data[i], HEX);
        Serial.print(F(" "));
      }
      Serial.println();
      break;
    }
  }

  digitalWrite(LED_BUILTIN, LOW);
}

// ======================================================================================
// XỬ LÝ NHẬN TỪNG BYTE TỪ UART SERIAL2 (STATE MACHINE)
// ======================================================================================
void parseIncomingByte(uint8_t b) {
  switch (rxState) {
    case STATE_STX:
      if (b == PI_FRAME_STX) { // 0xAA
        rxState = STATE_MAIN;
      }
      break;

    case STATE_MAIN:
      rxMain = b;
      rxState = STATE_SUB;
      break;

    case STATE_SUB:
      rxSub = b;
      rxState = STATE_LEN;
      break;

    case STATE_LEN:
      rxLen = b;
      rxDataIdx = 0;
      if (rxLen > 0 && rxLen <= 128) {
        rxState = STATE_DATA;
      } else if (rxLen == 0) {
        rxState = STATE_CHECKSUM;
      } else {
        rxState = STATE_STX; // Chiều dài không hợp lệ
      }
      break;

    case STATE_DATA:
      rxData[rxDataIdx++] = b;
      if (rxDataIdx >= rxLen) {
        rxState = STATE_CHECKSUM;
      }
      break;

    case STATE_CHECKSUM:
      rxExpectedChk = b;
      rxState = STATE_ETX;
      break;

    case STATE_ETX:
      if (b == PI_FRAME_ETX) { // 0x55
        // Kiểm tra Checksum XOR
        uint8_t calcChk = rxMain ^ rxSub ^ rxLen;
        for (uint8_t i = 0; i < rxLen; i++) {
          calcChk ^= rxData[i];
        }
        if (calcChk == rxExpectedChk) {
          processReceivedFrame(rxMain, rxSub, rxData, rxLen);
        } else {
          Serial.println();
          Serial.print(F(">>> [LỖI CHECKSUM] Tính toán: 0x"));
          Serial.print(calcChk, HEX);
          Serial.print(F(" != Nhận được: 0x"));
          Serial.println(rxExpectedChk, HEX);
        }
      }
      rxState = STATE_STX;
      break;

    default:
      rxState = STATE_STX;
      break;
  }
}

// ======================================================================================
// XỬ LÝ LỆNH TỪ BÀN PHÍM SERIAL MONITOR (HỖ TRỢ ĐỦ 20 SỰ KIỆN ĐẶC TẢ V1.3.0)
// ======================================================================================
void processCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  // 1. In lại Menu
  if (cmd.equalsIgnoreCase("m") || cmd.equalsIgnoreCase("help") || cmd.equalsIgnoreCase("?")) {
    printInteractiveMenu();
    return;
  }

  // 2. Lệnh HỦY báo động nguy hiểm 08/001 (Mục 3.4 đặc tả: EVENT_MAIN = 0xF4)
  if (cmd.equalsIgnoreCase("c") || cmd.equalsIgnoreCase("0") || 
      cmd.equalsIgnoreCase("f4") || cmd.equalsIgnoreCase("0xf4")) {
    Serial.println(F("\n🛑 [LỆNH 0xF4] HỦY CẢNH BÁO NGUY HIỂM 08/001 (TẮT CÒI LẶP LẠI)"));
    sendFrameToMaster(0xF4, 0x00);
    return;
  }

  // 3. Người dùng nhập số thứ tự [1] -> [20]
  int numIdx = cmd.toInt();
  if (numIdx >= 1 && numIdx <= TOTAL_EVENTS) {
    const EventDef &ev = EVENT_LIST[numIdx - 1];
    Serial.print(F("\n[CHỌN SỐ "));
    Serial.print(numIdx);
    Serial.print(F("] "));
    Serial.print(ev.codeStr);
    Serial.print(F(" - "));
    Serial.println(ev.desc);

    const uint8_t *payload = (ev.defaultPayload != NULL) ? (const uint8_t*)ev.defaultPayload : NULL;
    uint8_t payloadLen = (ev.defaultPayload != NULL) ? strlen(ev.defaultPayload) : 0;
    sendFrameToMaster(ev.main_evt, ev.sub_evt, payload, payloadLen);
    return;
  }

  // 4. Người dùng nhập mã sự kiện trực tiếp (VD: "01/001", "05/006", "08/001")
  // Hoặc kèm chuỗi dữ liệu Payload (Mục 3.3 đặc tả): VD "01/001 GV0012", "05/001 639E74E4"
  int spaceIdx = cmd.indexOf(' ');
  String codePart = (spaceIdx > 0) ? cmd.substring(0, spaceIdx) : cmd;
  String payloadPart = (spaceIdx > 0) ? cmd.substring(spaceIdx + 1) : "";
  codePart.trim();
  payloadPart.trim();

  for (uint8_t i = 0; i < TOTAL_EVENTS; i++) {
    if (codePart.equalsIgnoreCase(EVENT_LIST[i].codeStr)) {
      const EventDef &ev = EVENT_LIST[i];
      Serial.print(F("\n[KHỚP MÃ "));
      Serial.print(ev.codeStr);
      Serial.print(F("] "));
      Serial.println(ev.desc);

      const uint8_t *payload = NULL;
      uint8_t payloadLen = 0;
      if (payloadPart.length() > 0) {
        payload = (const uint8_t*)payloadPart.c_str();
        payloadLen = payloadPart.length();
        Serial.print(F("   + Dữ liệu kèm theo (Payload): \""));
        Serial.print(payloadPart);
        Serial.println(F("\""));
      } else if (ev.defaultPayload != NULL) {
        payload = (const uint8_t*)ev.defaultPayload;
        payloadLen = strlen(ev.defaultPayload);
      }
      sendFrameToMaster(ev.main_evt, ev.sub_evt, payload, payloadLen);
      return;
    }
  }

  // 5. Hỗ trợ nhập định dạng bất kỳ (VD: "05/006", "1/2", "8/1")
  int slashIdx = codePart.indexOf('/');
  if (slashIdx > 0) {
    uint8_t m = (uint8_t)strtol(codePart.substring(0, slashIdx).c_str(), NULL, 0);
    uint8_t s = (uint8_t)strtol(codePart.substring(slashIdx + 1).c_str(), NULL, 0);
    Serial.print(F("\n[LỆNH TÙY CHỌN] Main=0x"));
    if (m < 0x10) Serial.print(F("0"));
    Serial.print(m, HEX);
    Serial.print(F(", Sub=0x"));
    if (s < 0x10) Serial.print(F("0"));
    Serial.println(s, HEX);

    const uint8_t *payload = (payloadPart.length() > 0) ? (const uint8_t*)payloadPart.c_str() : NULL;
    uint8_t payloadLen = payloadPart.length();
    sendFrameToMaster(m, s, payload, payloadLen);
    return;
  }

  // Không nhận diện được
  Serial.print(F("\n[?] Lệnh '"));
  Serial.print(cmd);
  Serial.println(F("' không hợp lệ!"));
  Serial.println(F("    -> Gõ từ '1' đến '20', hoặc gõ mã như '05/006', '08/001', 'c', hoặc 'm' để xem Menu."));
}

// Biến đệm tích lũy ký tự từ Serial USB
static String serialInputBuffer = "";
static unsigned long lastCharTime = 0;

void handleKeyboardInput() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      serialInputBuffer.trim();
      if (serialInputBuffer.length() > 0) {
        processCommand(serialInputBuffer);
        serialInputBuffer = "";
      }
    } else {
      serialInputBuffer += c;
      lastCharTime = millis();
    }
  }

  // Hỗ trợ chế độ "No line ending" trên Serial Monitor (tự xử lý sau 300ms nếu người dùng gõ phím)
  if (serialInputBuffer.length() > 0 && (millis() - lastCharTime > 300)) {
    serialInputBuffer.trim();
    if (serialInputBuffer.length() > 0) {
      processCommand(serialInputBuffer);
      serialInputBuffer = "";
    }
  }
}

// ======================================================================================
// SETUP
// ======================================================================================
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Khởi tạo cổng USB Serial để giao tiếp với Terminal máy tính (115200 baud)
  Serial.begin(115200);

  // Khởi tạo cổng UART phần cứng Serial2 nối với STM32 Master (115200 baud)
  Serial2.begin(PI_UART_BAUDRATE, SERIAL_8N1, PIN_RX2, PIN_TX2);

  delay(500);

  Serial.println(F("\n\n"));
  Serial.println(F("************************************************************"));
  Serial.println(F("*         ESP32 RASPBERRY PI EMULATOR / RECEIVER           *"));
  Serial.println(F("*       KẾT NỐI VỚI STM32 MASTER TRÊN XE SCHOOLBUS         *"));
  Serial.println(F("************************************************************"));
  Serial.println(F("Sơ đồ kết nối phần cứng:"));
  Serial.println(F("  - ESP32 GPIO 16 (RX2)  <---  STM32 PA9 (USART1_TX)"));
  Serial.println(F("  - ESP32 GPIO 17 (TX2)  --->  STM32 PA10 (USART1_RX)"));
  Serial.println(F("  - ESP32 GND            <-->  STM32 GND (Chung mass)"));
  Serial.println(F("************************************************************"));

  printInteractiveMenu();
}

// ======================================================================================
// LOOP CHÍNH
// ======================================================================================
void loop() {
  // 1. Đọc và giải mã dữ liệu đến từ STM32 Master qua Serial2
  while (Serial2.available() > 0) {
    uint8_t byteIn = Serial2.read();
    parseIncomingByte(byteIn);
  }

  // 2. Xử lý bàn phím máy tính gõ từ USB Serial để gửi lệnh xuống STM32 Master
  handleKeyboardInput();
}
