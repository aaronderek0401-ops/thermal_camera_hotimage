// Save with 1251 encoding 
// Celsius string
// 直接使用度符号字符（确保文件以Windows-1251编码保存）
// 如果度符号不显示，可以尝试使用上标o或其他替代方案
#define CELSIUS_SYMBOL "\xB0""C"
// 华氏度符号 - 使用与CELSIUS_SYMBOL相同的编码方式（Windows-1251）
// 直接使用度符号字符（确保文件以Windows-1251编码保存）
#define FAHRENHEIT_SYMBOL "\xB0""F"