#include <stdio.h>
#include <wchar.h>
#include <locale.h>
#include <string.h>

int main() {
    // 设置C语言本地化环境为使用UTF-16
    setlocale(LC_ALL, "");

    // UTF-16字符串，以L前缀表示宽字符串
    char utf16_str[255]={0x64,0x00,0x6F,0x00,0x77, 0x00,0x6E,0x00,0x6C,0x00}; // 示例UTF-16字符串

    // 使用wprintf输出宽字符串
    wprintf(L"%ls\n", utf16_str);
    char str[32]="what---is--a--cat";
    char* token = strtok(str, "-");

    /* 继续获取其他的子字符串 */
    while (token != NULL)
    {
        printf("%s\n", token);

        token = strtok(NULL, "-");
    }
    return 0;
}
