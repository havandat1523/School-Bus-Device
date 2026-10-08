import os

# Manual .env parsing in case python-dotenv is not installed
env_vars = {}
env_path = os.path.join(os.path.dirname(__file__), ".env")
if os.path.exists(env_path):
    with open(env_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, val = line.split("=", 1)
                env_vars[key.strip()] = val.strip()

# Set DISPLAY environment variable for X11 GUI if not already set (e.g. over SSH remote)
if "DISPLAY" not in os.environ:
    os.environ["DISPLAY"] = env_vars.get("DISPLAY", os.getenv("DISPLAY", ":0"))

class Config:
    BASE_DIR = os.path.dirname(os.path.abspath(__file__))
    VEHICLE_ID = env_vars.get("VEHICLE_ID", os.getenv("VEHICLE_ID", "29B-123.45"))
    CHASSIS_NUMBER = env_vars.get("CHASSIS_NUMBER", os.getenv("CHASSIS_NUMBER", "RE123456789"))
    MQTT_BROKER_HOST = env_vars.get("MQTT_BROKER_HOST", os.getenv("MQTT_BROKER_HOST", "broker.hivemq.com"))
    MQTT_BROKER_PORT = int(env_vars.get("MQTT_BROKER_PORT", os.getenv("MQTT_BROKER_PORT", "1883")))
    MQTT_USERNAME = env_vars.get("MQTT_USERNAME", os.getenv("MQTT_USERNAME", "pi_29B12345"))
    MQTT_PASSWORD = env_vars.get("MQTT_PASSWORD", os.getenv("MQTT_PASSWORD", "changeme"))
    SERVER_API_BASE_URL = env_vars.get("SERVER_API_BASE_URL", os.getenv("SERVER_API_BASE_URL", "http://192.168.137.1:8000"))
    # Default UART port: auto-fallback to Linux serial ports if on Linux/Raspberry Pi
    default_uart = "COM3" if os.name == "nt" else "/tmp/ttyVIRTUAL_PI"
    UART_MASTER_PORT = env_vars.get("UART_MASTER_PORT", os.getenv("UART_MASTER_PORT", default_uart))
    if os.name != "nt" and UART_MASTER_PORT.startswith("COM"):
        # Automatically use virtual PTY or Linux serial port if port was configured for Windows
        if os.path.exists("/tmp/ttyVIRTUAL_PI"):
            UART_MASTER_PORT = "/tmp/ttyVIRTUAL_PI"
        elif os.path.exists("/dev/ttyAMA0"):
            UART_MASTER_PORT = "/dev/ttyAMA0"
        elif os.path.exists("/dev/ttyUSB0"):
            UART_MASTER_PORT = "/dev/ttyUSB0"
        else:
            UART_MASTER_PORT = "/tmp/ttyVIRTUAL_PI"

    UART_MASTER_BAUDRATE = int(env_vars.get("UART_MASTER_BAUDRATE", os.getenv("UART_MASTER_BAUDRATE", "115200")))
    FACE_VECTOR_DIM = int(env_vars.get("FACE_VECTOR_DIM", os.getenv("FACE_VECTOR_DIM", "128")))
    PRESENCE_CHECK_INTERVAL_SEC = int(env_vars.get("PRESENCE_CHECK_INTERVAL_SEC", os.getenv("PRESENCE_CHECK_INTERVAL_SEC", "180")))
    LOG_LEVEL = env_vars.get("LOG_LEVEL", os.getenv("LOG_LEVEL", "INFO"))
    DEVICE_PSK = env_vars.get("DEVICE_PSK", os.getenv("DEVICE_PSK", "b7e2f1a9c4d8e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0"))
    
    SCHOOL_GEOFENCE_LAT = float(env_vars.get("SCHOOL_GEOFENCE_LAT", os.getenv("SCHOOL_GEOFENCE_LAT", "21.0021")))
    SCHOOL_GEOFENCE_LON = float(env_vars.get("SCHOOL_GEOFENCE_LON", os.getenv("SCHOOL_GEOFENCE_LON", "105.8462")))
    SCHOOL_GEOFENCE_RADIUS_M = float(env_vars.get("SCHOOL_GEOFENCE_RADIUS_M", os.getenv("SCHOOL_GEOFENCE_RADIUS_M", "100")))

    # F2: RFID pending queue TTL (seconds)
    RFID_PENDING_TTL_SEC = int(env_vars.get("RFID_PENDING_TTL_SEC", os.getenv("RFID_PENDING_TTL_SEC", "300")))

    # F3: Module SIM A7680s & SOS Nâng cao
    SIM_UART_PORT = env_vars.get("SIM_UART_PORT", os.getenv("SIM_UART_PORT", "/dev/ttyAMA1" if os.name != "nt" else "COM4"))
    SIM_UART_BAUDRATE = int(env_vars.get("SIM_UART_BAUDRATE", os.getenv("SIM_UART_BAUDRATE", "115200")))
    DEFAULT_PHONE_NUMBER = env_vars.get("DEFAULT_PHONE_NUMBER", os.getenv("DEFAULT_PHONE_NUMBER", "0987654321"))
    SOS_RETRY_MAX = int(env_vars.get("SOS_RETRY_MAX", os.getenv("SOS_RETRY_MAX", "5")))
    SOS_RETRY_INTERVAL_SEC = int(env_vars.get("SOS_RETRY_INTERVAL_SEC", os.getenv("SOS_RETRY_INTERVAL_SEC", "60")))
    SOS_CALL_INTERVAL_SEC = int(env_vars.get("SOS_CALL_INTERVAL_SEC", os.getenv("SOS_CALL_INTERVAL_SEC", "120")))
    SOS_CALL_MAX = int(env_vars.get("SOS_CALL_MAX", os.getenv("SOS_CALL_MAX", "4")))

    # F4: Nút GPIO Vật lý
    GPIO_LOGIN_PIN = int(env_vars.get("GPIO_LOGIN_PIN", os.getenv("GPIO_LOGIN_PIN", "17")))
    GPIO_SOS_PIN = int(env_vars.get("GPIO_SOS_PIN", os.getenv("GPIO_SOS_PIN", "27")))
    GPIO_SOS_HOLD_SEC = float(env_vars.get("GPIO_SOS_HOLD_SEC", os.getenv("GPIO_SOS_HOLD_SEC", "3")))

config = Config()
