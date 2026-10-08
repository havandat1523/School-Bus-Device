#include "gps_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void GPS_Init(void) {
    // Initializer if needed
}

static void format_gps_payload(char *buf, uint16_t max_len, double lat, double lon, float speed) {
    long lat_int = (long)lat;
    double lat_rem = (lat >= 0) ? (lat - lat_int) : (-lat + lat_int);
    long lat_frac = (long)(lat_rem * 1000000.0 + 0.5);

    long lon_int = (long)lon;
    double lon_rem = (lon >= 0) ? (lon - lon_int) : (-lon + lon_int);
    long lon_frac = (long)(lon_rem * 1000000.0 + 0.5);

    long spd_int = (long)speed;
    long spd_frac = (long)((speed - spd_int) * 10.0 + 0.5);

    snprintf(buf, max_len, "%ld.%06ld,%ld.%06ld,%ld.%ld",
             lat_int, lat_frac, lon_int, lon_frac, spd_int, spd_frac);
}

void GPS_GetDefaultPayload(char *buf, uint16_t max_len) {
    snprintf(buf, max_len, "21.002100,105.846200,0.0");
}

// Parser for NMEA RMC sentences ($GNRMC, $GPRMC, $BDRMC)
// Format: $GNRMC,hhmmss.ss,status,lat,N,lon,E,speed,track,date,,,mode*hh
uint8_t GPS_ParseNMEA(const char *nmea, GPS_Data_t *out_data) {
    if (!nmea || !out_data) return 0;
    if (strstr(nmea, "RMC") == NULL) return 0;

    char nmea_copy[GPS_BUF_SIZE];
    strncpy(nmea_copy, nmea, GPS_BUF_SIZE - 1);
    nmea_copy[GPS_BUF_SIZE - 1] = '\0';

    char *fields[15];
    int field_count = 0;
    char *ptr = nmea_copy;

    fields[field_count++] = ptr;
    while (*ptr != '\0') {
        if (*ptr == ',' || *ptr == '*') {
            *ptr = '\0';
            fields[field_count++] = ptr + 1;
            if (field_count >= 15) break;
        }
        ptr++;
    }

    // RMC fields: 0=$GNRMC, 1=UTC, 2=Status ('A'=Valid, 'V'=Void), 3=Lat, 4=N/S, 5=Lon, 6=E/W, 7=Speed (knots)
    if (field_count > 7 && fields[2][0] == 'A' && strlen(fields[3]) > 4 && strlen(fields[5]) > 5) {
        char *raw_lat = fields[3];
        char lat_dir = fields[4][0];
        char *raw_lon = fields[5];
        char lon_dir = fields[6][0];
        char *raw_speed = fields[7];

        // Convert DDMM.MMMM to decimal degrees
        char deg_lat[3] = {raw_lat[0], raw_lat[1], '\0'};
        double mins_lat = atof(raw_lat + 2);
        double lat_dd = atof(deg_lat) + (mins_lat / 60.0);
        if (lat_dir == 'S') lat_dd = -lat_dd;

        char deg_lon[4] = {raw_lon[0], raw_lon[1], raw_lon[2], '\0'};
        double mins_lon = atof(raw_lon + 3);
        double lon_dd = atof(deg_lon) + (mins_lon / 60.0);
        if (lon_dir == 'W') lon_dd = -lon_dd;

        // Convert speed from knots to km/h
        float speed_kmh = (float)(atof(raw_speed) * 1.852);

        out_data->lat = lat_dd;
        out_data->lon = lon_dd;
        out_data->speed_kmh = speed_kmh;
        out_data->fix_valid = 1;

        format_gps_payload(out_data->payload, sizeof(out_data->payload),
                           lat_dd, lon_dd, speed_kmh);
        return 1;
    } else {
        // Fallback default coordinates
        out_data->lat = DEFAULT_LAT;
        out_data->lon = DEFAULT_LON;
        out_data->speed_kmh = 0.0f;
        out_data->fix_valid = 0;
        GPS_GetDefaultPayload(out_data->payload, sizeof(out_data->payload));
        return 0;
    }
}
