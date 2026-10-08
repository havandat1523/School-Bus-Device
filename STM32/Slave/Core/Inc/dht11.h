#ifndef INC_DHT11_H_
#define INC_DHT11_H_

#include "main.h"
#include <stdint.h>

typedef struct {
    uint8_t temp_int;
    uint8_t temp_dec;
    uint8_t hum_int;
    uint8_t hum_dec;
} DHT11_Data_t;

void DHT11_Init(void);
uint8_t DHT11_Read(DHT11_Data_t *out_data);

#endif /* INC_DHT11_H_ */
