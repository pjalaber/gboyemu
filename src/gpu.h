#ifndef _GPU_H_
#define _GPU_H_

#define GB_SCREEN_WIDTH  160
#define GB_SCREEN_HEIGHT 144

#define GPU_RPT_MODE_1 10

#define GPU_CYCLES_MODE_0 203
#define GPU_CYCLES_MODE_1 456
#define GPU_CYCLES_MODE_2 80
#define GPU_CYCLES_MODE_3 173

#define GPU_ZOOM_DEFAULT 3
#define GPU_ZOOM_MAX 4

#define GPU_CYCLES_FULL (((GPU_CYCLES_MODE_0 + GPU_CYCLES_MODE_3 + GPU_CYCLES_MODE_2) * GB_SCREEN_HEIGHT) + (GPU_CYCLES_MODE_1 * GPU_RPT_MODE_1))

int32_t gpu_init(uint32_t zoom);

uint8_t gpu_read_ly(void);
void gpu_write_ly(uint8_t value8);

uint8_t gpu_read_lycmp(void);
void gpu_write_lycmp(uint8_t value8);

void gpu_write_bgp(uint8_t bgp);
uint8_t gpu_read_bgp(void);

void gpu_write_lcdctrl(uint8_t lcdctrl);
uint8_t gpu_read_lcdctrl(void);

uint8_t gpu_read_lcdstatus(void);
void gpu_write_lcdstatus(uint8_t lcdstatus);

uint8_t gpu_read_scrollx(void);
uint8_t gpu_read_scrolly(void);

void gpu_write_scrolly(uint8_t scrolly);
void gpu_write_scrollx(uint8_t scrollx);

void gpu_write_windowx(uint8_t windowx);
uint8_t gpu_read_windowx(void);

void gpu_write_windowy(uint8_t windowy);
uint8_t gpu_read_windowy(void);

void gpu_write_objpal0(uint8_t objpal0);
void gpu_write_objpal1(uint8_t objpal1);

uint8_t gpu_read_objpal0(void);
uint8_t gpu_read_objpal1(void);

void gpu_write_vram(uint16_t addr, uint8_t value);
uint8_t gpu_read_vram(uint16_t addr);

void gpu_write_oam(uint16_t addr, uint8_t value);
uint8_t gpu_read_oam(uint16_t addr);

void gpu_run(uint32_t cycles, uint32_t frame_skip);

void gpu_start_dma(uint8_t value);

uint32_t gpu_get_zoom(void);
void gpu_set_zoom(uint32_t zoom);

int32_t gpu_dump(FILE *file);
int32_t gpu_restore(FILE *file);

#endif
