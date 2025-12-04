#ifndef MAIN_SAVE_SAVE_H_
#define MAIN_SAVE_SAVE_H_
#include "esp_system.h"

int save_ImageCSV(void);
int save_ImageBMP(uint8_t bits);
int save_MLX90640Params(void);

// 截图管理功能
int save_listBmpFiles(char fileList[][32], int maxFiles);
int save_deleteBmpFile(const char* fileName);
int save_deleteAllBmpFiles(void);
int save_viewBmpFile(const char* fileName);

#endif /* MAIN_SAVE_SAVE_H_ */
