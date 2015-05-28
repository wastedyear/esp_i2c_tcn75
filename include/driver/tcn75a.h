
int ICACHE_FLASH_ATTR tcn75_init(void);

uint16_t ICACHE_FLASH_ATTR *tcn75a_read(uint8_t *pre);
float ICACHE_FLASH_ATTR tcn75_get_temp(uint16_t data);

#define INVALID_READING 1
#define INVALID_READING_MASK 1

