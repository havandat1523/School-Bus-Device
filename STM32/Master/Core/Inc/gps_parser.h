#ifndef INC_GPS_PARSER_H_
#define INC_GPS_PARSER_H_

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define GPS_BUF_SIZE 128
#define DEFAULT_LAT  21.002100
#define DEFAULT_LON  105.846200

typedef struct {
    double lat;
    double lon;
    float speed_kmh;
    uint8_t fix_valid;
    char payload[80]; // Formatted "lat,lon,speed"
} GPS_Data_t;

void GPS_Init(void);
uint8_t GPS_ParseNMEA(const char *nmea, GPS_Data_t *out_data);
void GPS_GetDefaultPayload(char *buf, uint16_t max_len);

#endif /* INC_GPS_PARSER_H_ */
