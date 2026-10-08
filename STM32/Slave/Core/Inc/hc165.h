#ifndef INC_HC165_H_
#define INC_HC165_H_

#include "main.h"
#include <stdint.h>

#define DEBOUNCE_DELAY_MS 10000 // 10 seconds debounce per spec section 2.5

void HC165_Init(void);
uint16_t HC165_ReadRaw16(void);
uint8_t HC165_UpdateDebounce(uint16_t *out_debounced_bitmap);

#endif /* INC_HC165_H_ */
