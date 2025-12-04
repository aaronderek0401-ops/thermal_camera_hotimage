#include "save.h"
#include "console.h"
#include "dispcolor.h"
#include "driver_MLX90640.h"
#include "messagebox.h"
#include "sd_task.h"
#include "settings.h"
#include "thermalimaging.h"
#include <dirent.h>
#include <esp_psram.h>
#include <esp_spi_flash.h>
#include <esp_spiffs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SAVE_CSV_WINDOW_WIDTH 200 // 消息串口 宽度
#define SAVE_BMP_WINDOW_WIDTH 200

// 预估文件大小（用于空间检查）
#define BMP_24BIT_SIZE  (320 * 240 * 4 + 100)   // ~307KB for 24-bit BMP
#define BMP_16BIT_SIZE  (320 * 240 * 2 + 100)   // ~154KB for 16-bit BMP
#define MIN_FREE_SPACE  (10 * 1024)              // 保留 10KB 安全空间

#define WORD uint16_t
#define DWORD uint32_t
#define LONG int32_t

static int16_t GetStringF(char* pOut, const char* args, ...)
{
    char StrBuff[32];

    va_list ap;
    va_start(ap, args);
    vsnprintf(StrBuff, sizeof(StrBuff), args, ap);
    va_end(ap);

    strcpy(pOut, StrBuff);
    return strlen(StrBuff);
}
// 保存路径：使用内置 SPIFFS 存储
#define SAVE_MOUNT_POINT "/spiffs"
#define SPIFFS_PARTITION_LABEL "storage"

/**
 * @brief 检查 SPIFFS 剩余空间是否足够
 *
 * @param requiredSize 需要的空间大小（字节）
 * @return int 0=空间足够, -1=空间不足, -2=获取信息失败
 */
static int checkSpiffsSpace(size_t requiredSize)
{
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(SPIFFS_PARTITION_LABEL, &total, &used);
    if (ret != ESP_OK) {
        printf("Failed to get SPIFFS info: %d\n", ret);
        return -2;
    }
    
    size_t freeSpace = total - used;
    printf("SPIFFS: total=%d, used=%d, free=%d, required=%d\n", 
           (int)total, (int)used, (int)freeSpace, (int)requiredSize);
    
    if (freeSpace < requiredSize + MIN_FREE_SPACE) {
        return -1;  // 空间不足
    }
    return 0;  // 空间足够
}

/**
 * @brief 获取最后的文件序号
 *
 * @param pExtensionStr 扩展名
 * @return int32_t
 */
static int32_t GetLastIndex(char* pExtensionStr)
{
    int32_t maxFileIndex = 0;

    // 打开内置 SPIFFS 存储
    DIR* dr = opendir(SAVE_MOUNT_POINT);
    if (dr == NULL) {
        // SPIFFS 未挂载或不可用
        return -1;
    }

    // 搜索名称中具有最大索引的指定扩展名文件
    struct dirent* de;
    while ((de = readdir(dr)) != NULL) {
        if (de->d_type == DT_REG && strstr(de->d_name, pExtensionStr)) {
            uint32_t FileIndex = atoi(de->d_name);
            if (FileIndex > maxFileIndex) {
                maxFileIndex = FileIndex;
            }
        }
    }

    closedir(dr);
    return maxFileIndex;
}

/**
 * @brief 写入BMP文件头
 *
 * @param f
 * @param bitsPerPixel
 * @param width
 * @param height
 */
static void WriteBmpFileHeaderCore24Bit(FILE* f, uint8_t bitsPerPixel, uint16_t width, uint16_t height)
{
    WORD uint16;
    DWORD buff32;

    if (bitsPerPixel == 15)
        bitsPerPixel = 16;
    if (bitsPerPixel == 24)
        bitsPerPixel = 32;

    uint32_t pixelDataOffset = 14 + 12;
    uint32_t fileSize = pixelDataOffset + width * height;
    fileSize *= bitsPerPixel / 8;

    // 填充 BITMAPFILEHEADER 结构
    // 文件签名 "BM"
    uint16 = 0x4D42;
    fwrite(&uint16, 2, 1, f);
    // 文件大小
    buff32 = fileSize;
    fwrite(&buff32, 4, 1, f);
    // 2 保留
    uint16 = 0;
    fwrite(&uint16, 2, 1, f);
    uint16 = 0;
    fwrite(&uint16, 2, 1, f);
    // 点数据相对于 BITMAPFILEHEADER 结构（和文件本身）开头的偏移量
    buff32 = pixelDataOffset;
    fwrite(&buff32, 4, 1, f);

    // 填充 BITMAPCOREHEADER 结构
    buff32 = 12; // 此结构的大小（定义结构的版本
    fwrite(&buff32, 4, 1, f);
    // 光栅宽度和高度
    fwrite(&width, 2, 1, f);
    fwrite(&height, 2, 1, f);
    // 平面字段（对于 BMP，始终 = 1）
    uint16 = 1;
    fwrite(&uint16, 2, 1, f);
    // 位计数字段
    uint16 = 32; // bitsPerPixel;
    fwrite(&uint16, 2, 1, f);
}

/**
 * @brief 写入BMP文件头
 *
 * @param f 句柄
 * @param bitsPerPixel BMP位数
 * @param width
 * @param height
 */
static void WriteBmpFileHeaderCore16Bit(FILE* f, uint8_t bitsPerPixel, uint16_t width, uint16_t height)
{
    WORD uint16;
    DWORD uint32;
    LONG int32;

    if (bitsPerPixel == 15)
        bitsPerPixel = 16;
    else if (bitsPerPixel == 24)
        bitsPerPixel = 32;

    uint32_t pixelDataOffset = 14 + 40;
    uint32_t fileSize = pixelDataOffset + width * height;
    fileSize *= bitsPerPixel / 8;

    // Заполнение структуры BITMAPFILEHEADER
    // Сигнатура файла "BM"
    uint16 = 0x4D42;
    fwrite(&uint16, 2, 1, f);
    // Размер файла
    uint32 = fileSize;
    fwrite(&uint32, 4, 1, f);
    // 2 резервных слова
    uint16 = 0;
    fwrite(&uint16, 2, 1, f);
    uint16 = 0;
    fwrite(&uint16, 2, 1, f);
    // Смещение данных о точках относительно начала структуры BITMAPFILEHEADER (и самого файла)
    uint32 = pixelDataOffset;
    fwrite(&uint32, 4, 1, f);

    // Заполнение структуры BITMAPCOREHEADER
    uint32 = 40; // Размер этой структуры (определяет версию структуры)
    fwrite(&uint32, 4, 1, f);
    // Поле biWidth
    int32 = width;
    fwrite(&int32, 4, 1, f);
    // Поле biHeight
    int32 = height;
    fwrite(&int32, 4, 1, f);
    // 平面字段（始终 = 1 为 BMP）
    uint16 = 1;
    fwrite(&uint16, 2, 1, f);
    // bi比特计数
    uint16 = 16;
    fwrite(&uint16, 2, 1, f);
    // 双压缩
    uint32 = 0; // 无压缩
    fwrite(&uint32, 4, 1, f);
    // biSizeImage
    uint32 = 0; // 0 如果未使用压缩
    fwrite(&uint32, 4, 1, f);
    // 字段 biXPelsPerMeter
    int32 = 0;
    fwrite(&int32, 4, 1, f);
    // 字段 biYPelsPerMeter
    int32 = 0;
    fwrite(&int32, 4, 1, f);
    // 字段 biClrUsed
    uint32 = 0;
    fwrite(&uint32, 4, 1, f);
    // biClr导入字段
    uint32 = 0;
    fwrite(&uint32, 4, 1, f);
}

/**
 * @brief 写入BMP数据15Bit
 *
 * @param f
 * @param width
 * @param pBuffRgb565
 */
static void WriteBmpRow_15bit(FILE* f, uint16_t width, uint16_t* pBuffRgb565)
{
    for (int col = 0; col < width; col++) {
        uRGB565 color;
        color.value = *(pBuffRgb565++);

        WORD buff16 = color.rgb_color.b;
        buff16 |= (color.rgb_color.g >> 1) << 5;
        buff16 |= color.rgb_color.r << 10;

        fwrite(&buff16, 2, 1, f);
    }
}

/**
 * @brief 写入BMP数据24Bit
 *
 * @param f
 * @param width
 * @param pBuffRgb565
 */
static void WriteBmpRow_24bit(FILE* f, uint16_t width, uint16_t* pBuffRgb565)
{
    for (int col = 0; col < width; col++) {
        uRGB565 color;
        color.value = *(pBuffRgb565++);
        DWORD buff32 = color.rgb_color.b << 3;
        buff32 |= (color.rgb_color.g << 8) << 2;
        buff32 |= (color.rgb_color.r << 16) << 3;

        fwrite(&buff32, 4, 1, f);
    }
}

/**
 * @brief 写入一行CSV数据
 *
 * @param f 句柄
 * @param width 数据个数
 * @param pValues 值缓存
 */
static void WriteCsvRow_float(FILE* f, uint16_t width, float* pValues)
{
    for (int col = 0; col < width; col++) {
        if (col == width - 1)
            fprintf(f, "%f\r\n", pValues[col]); // 最后一个数据 要换行
        else
            fprintf(f, "%f, ", pValues[col]); // 写入数据
    }
}

static void WriteCsvRow_int8(FILE* f, uint16_t width, int8_t* pValues)
{
    for (int col = 0; col < width; col++) {
        if (col == width - 1)
            fprintf(f, "%d\r\n", pValues[col]); // 最后一个数据 要换行
        else
            fprintf(f, "%d, ", pValues[col]); // 写入数据
    }
}

static void WriteCsvRow_uint16(FILE* f, uint16_t width, uint16_t* pValues)
{
    for (int col = 0; col < width; col++) {
        if (col == width - 1)
            fprintf(f, "%d\r\n", pValues[col]); // 最后一个数据 要换行
        else
            fprintf(f, "%d, ", pValues[col]); // 写入数据
    }
}

static void WriteCsvRow_int16(FILE* f, uint16_t width, int16_t* pValues)
{
    for (int col = 0; col < width; col++) {
        if (col == width - 1)
            fprintf(f, "%d\r\n", pValues[col]); // 最后一个数据 要换行
        else
            fprintf(f, "%d, ", pValues[col]); // 写入数据
    }
}

/**
 * @brief 将当前热图保存到内置 SPIFFS 的功能（CSV 格式，值分隔符 - 逗号）
 *
 * @return int
 */
int save_ImageCSV(void)
{
    int ret = 0;
    FILE* f = NULL;
    float* pValues = NULL;
    char fileExtension[] = ".CSV";

    // 获取最大文件名
    int32_t maxFileIndex = GetLastIndex(fileExtension);
    if (maxFileIndex < 0) {
        // 无法打开 SPIFFS
        message_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "SPIFFS Access Error", RED, 1, 1000);
        printf("SPIFFS Access Error\r\n");
        ret = 1;
        goto error;
    }
    maxFileIndex++;

    // 分配内存中的临时缓冲区以存储值
    // 优先使用 PSRAM，如果没有则使用内部内存
    pValues = heap_caps_malloc((THERMALIMAGE_RESOLUTION_WIDTH * THERMALIMAGE_RESOLUTION_HEIGHT) << 2, MALLOC_CAP_SPIRAM);
    if (!pValues) {
        pValues = malloc((THERMALIMAGE_RESOLUTION_WIDTH * THERMALIMAGE_RESOLUTION_HEIGHT) << 2);
    }
    if (!pValues) {
        message_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "Out of Memory !", RED, 1, 1000);
        printf("Out of Memory !\r\n");
        ret = 1;
        goto error;
    }

    // 获取热成像数据
    GetThermoData(pValues);

    // 提示
    char message[32];
    GetStringF(message, "Save To File %05d%s", maxFileIndex, fileExtension);
    progress_start_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Temperature Map", message, GREEN, 0, 0);
    printf("%s\r\n", message);

    // 文件名（内置 SPIFFS 存储）
    char fileName[128];
    GetStringF(fileName, "%s/%05d%s", SAVE_MOUNT_POINT, maxFileIndex, fileExtension);

    // 打开文件
    f = fopen(fileName, "w");
    if (f == NULL) {
        message_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "Error Writing File!", RED, 1, 1000);
        ret = 1;
        goto error;
    }

    // 写入文件
    for (uint8_t step = 0; step < THERMALIMAGE_RESOLUTION_HEIGHT; step++) {
        WriteCsvRow_float(f, THERMALIMAGE_RESOLUTION_WIDTH, &pValues[step * THERMALIMAGE_RESOLUTION_WIDTH]);
        progress_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Temperature Map", message, GREEN, step + 1, 24);
    }

    GetStringF(message, "File %05d%s Saved Successfully", maxFileIndex, fileExtension);
    message_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Temperature Map", message, GREEN, 0, 1000);
    printf("%s\r\n", message);
    ret = 0;

error:
    if (f != NULL) {
        fflush(f);
        fclose(f);
    }

    if (NULL != pValues) {
        free(pValues);
        pValues = NULL;
    }

    return ret;
}

/**
 * @brief 保存MLX90640 EEPROM信息
 *
 * @return int
 */
int save_MLX90640Params(void)
{
    int ret = 0;
    FILE* f = NULL;
    paramsMLX90640* pValues = NULL;
    char fileExtension[] = ".PAR";

    // 获取最大文件名
    int32_t maxFileIndex = GetLastIndex(fileExtension);
    if (maxFileIndex < 0) {
        // 无法打开 SPIFFS
        message_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "SPIFFS Access Error", RED, 1, 1000);
        printf("SPIFFS Access Error\r\n");
        ret = 1;
        goto error;
    }
    maxFileIndex++;

    // 分配内存中的临时缓冲区以存储值
    // 优先使用 PSRAM，如果没有则使用内部内存
    pValues = heap_caps_malloc(sizeof(paramsMLX90640), MALLOC_CAP_SPIRAM);
    if (!pValues) {
        pValues = malloc(sizeof(paramsMLX90640));
    }
    if (!pValues) {
        message_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "Out of Memory !", RED, 1, 1000);
        printf("Out of Memory !\r\n");
        ret = 1;
        goto error;
    }

    // 获取MLX90640参数
    GetThermoParams(pValues);

    // 提示
    char message[32];
    GetStringF(message, "Save To File %05d%s", maxFileIndex, fileExtension);
    progress_start_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Temperature Map", message, GREEN, 0, 0);
    printf("%s\r\n", message);

    // 文件名（内置 SPIFFS 存储）
    char fileName[128];
    GetStringF(fileName, "%s/%05d%s", SAVE_MOUNT_POINT, maxFileIndex, fileExtension);

    // 打开文件
    f = fopen(fileName, "w");
    if (f == NULL) {
        message_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "Error Writing File!", RED, 1, 1000);
        ret = 1;
        goto error;
    }

    // 写入文件
    fprintf(f, "%d\r\n", pValues->kVdd);
    fprintf(f, "%d\r\n", pValues->vdd25);
    fprintf(f, "%f\r\n", pValues->KvPTAT);
    fprintf(f, "%f\r\n", pValues->KtPTAT);
    fprintf(f, "%d\r\n", pValues->vPTAT25);
    fprintf(f, "%f\r\n", pValues->alphaPTAT);
    fprintf(f, "%d\r\n", pValues->gainEE);
    fprintf(f, "%f\r\n", pValues->tgc);
    fprintf(f, "%f\r\n", pValues->cpKv);
    fprintf(f, "%f\r\n", pValues->cpKta);
    fprintf(f, "%d\r\n", pValues->resolutionEE);
    fprintf(f, "%d\r\n", pValues->calibrationModeEE);
    fprintf(f, "%f\r\n", pValues->KsTa);
    WriteCsvRow_float(f, 5, pValues->ksTo);
    WriteCsvRow_int16(f, 5, pValues->ct);
    WriteCsvRow_uint16(f, 768, pValues->alpha);
    fprintf(f, "%d\r\n", pValues->alphaScale);
    WriteCsvRow_int16(f, 768, pValues->offset);
    WriteCsvRow_int8(f, 768, pValues->kta);
    fprintf(f, "%d\r\n", pValues->ktaScale);
    WriteCsvRow_int8(f, 768, pValues->kv);
    fprintf(f, "%d\r\n", pValues->kvScale);
    WriteCsvRow_float(f, 2, pValues->cpAlpha);
    WriteCsvRow_int16(f, 2, pValues->cpOffset);
    WriteCsvRow_float(f, 3, pValues->ilChessC);
    WriteCsvRow_uint16(f, 5, pValues->brokenPixels);
    WriteCsvRow_uint16(f, 5, pValues->outlierPixels);

    GetStringF(message, "File %05d%s Saved Successfully", maxFileIndex, fileExtension);
    message_show(SAVE_CSV_WINDOW_WIDTH, FONTID_6X8M, "Save Temperature Map", message, GREEN, 0, 1000);
    printf("%s\r\n", message);
    ret = 0;

error:
    if (f != NULL) {
        fflush(f);
        fclose(f);
    }

    if (NULL != pValues) {
        free(pValues);
        pValues = NULL;
    }
    return ret;
}

/**
 * @brief 保存BMP（逐行写入，节省内存）
 *
 * @param bits 保存BMP位数
 * @return int
 */
int save_ImageBMP(uint8_t bits)
{
    uint16_t screenWidth = dispcolor_getWidth();
    uint16_t screenHeight = dispcolor_getHeight();
    char fileExtension[] = ".BMP";

    // 计算预估文件大小并检查空间
    size_t estimatedSize = (bits == 24) ? BMP_24BIT_SIZE : BMP_16BIT_SIZE;
    int spaceCheck = checkSpiffsSpace(estimatedSize);
    if (spaceCheck == -1) {
        message_show(SAVE_BMP_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "SPIFFS Full!", RED, 1, 1500);
        printf("SPIFFS space insufficient for BMP file\n");
        return -1;
    } else if (spaceCheck == -2) {
        message_show(SAVE_BMP_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "SPIFFS Error", RED, 1, 1000);
        return -1;
    }

    // 获取最大文件序号
    int32_t maxFileIndex = GetLastIndex(fileExtension);
    if (maxFileIndex < 0) {
        message_show(SAVE_BMP_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "SPIFFS Access Error", RED, 1, 1000);
        return -1;
    }

    maxFileIndex++;

    // 只分配一行的缓冲区（节省内存：320×2 = 640 字节）
    uint16_t* pRowBuffer = malloc(screenWidth * sizeof(uint16_t));
    if (!pRowBuffer) {
        message_show(SAVE_BMP_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "Out of Memory !", RED, 1, 1000);
        return -1;
    }

    // 显示弹窗
    char message[32];
    GetStringF(message, "Save To File %05d%s", maxFileIndex, fileExtension);
    progress_start_show(SAVE_BMP_WINDOW_WIDTH, FONTID_6X8M, "Save Temperature Map", message, GREEN, 0, 0);

    // 拼接路径和文件名
    char fileName[64];
    GetStringF(fileName, "%s/%05d%s", SAVE_MOUNT_POINT, maxFileIndex, fileExtension);

    // 打开文件
    FILE* f = fopen(fileName, "wb");
    if (f == NULL) {
        message_show(SAVE_BMP_WINDOW_WIDTH, FONTID_6X8M, "Save Error", "Error Writing File!", RED, 1, 1000);
        free(pRowBuffer);
        return -1;
    }

    // 写入BMP头
    if (bits == 15 || bits == 16) {
        WriteBmpFileHeaderCore16Bit(f, 16, screenWidth, screenHeight);

        // 逐行写入BMP数据（从底部到顶部，BMP格式要求）
        for (int row = screenHeight - 1; row >= 0; row--) {
            dispcolor_getRowData(row, pRowBuffer);
            WriteBmpRow_15bit(f, screenWidth, pRowBuffer);
            if ((screenHeight - 1 - row) % 10 == 0) {
                progress_show(SAVE_BMP_WINDOW_WIDTH, FONTID_6X8M, "Save Temperature Map", message, GREEN, 
                              (screenHeight - 1 - row) / 10 + 1, 24);
            }
        }

    } else if (bits == 24) {
        WriteBmpFileHeaderCore24Bit(f, 24, screenWidth, screenHeight);

        // 逐行写入BMP数据（从底部到顶部）
        for (int row = screenHeight - 1; row >= 0; row--) {
            dispcolor_getRowData(row, pRowBuffer);
            WriteBmpRow_24bit(f, screenWidth, pRowBuffer);
            if ((screenHeight - 1 - row) % 10 == 0) {
                progress_show(SAVE_BMP_WINDOW_WIDTH, FONTID_6X8M, "Save Temperature Map", message, GREEN,
                              (screenHeight - 1 - row) / 10 + 1, 24);
            }
        }
    }

    // 保存文件成功
    GetStringF(message, "File %05d%s Saved Successfully", maxFileIndex, fileExtension);
    message_show(SAVE_BMP_WINDOW_WIDTH, FONTID_6X8M, "Save Temperature Map", message, GREEN, 0, 1000);

    fflush(f);
    fclose(f);
    free(pRowBuffer);

    return 0;
}
