#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "utils.h"

int parse_flags(const char *flag_str, char *out_flags) {
    if (strcmp(flag_str,"-r")==0) { strcpy(out_flags,"r"); return 0; }
    if (strcmp(flag_str,"-w")==0) { strcpy(out_flags,"w"); return 0; }
    if (strcmp(flag_str,"-rw")==0 || strcmp(flag_str,"-wr")==0) { strcpy(out_flags,"rw"); return 0; }
    return -1;
}

void trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return;
    end = str + strlen(str) -1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1]=0;
    memmove(str, str, strlen(str)+1);
}

void to_upper(char *str) {
    for (;*str;str++) *str = toupper((unsigned char)*str);
}

void format_name_11(const char *input, char output[11]) {
    memset(output, ' ', 11);
    int len = strlen(input);
    if (len>11) len=11;
    for (int i=0; i<len; i++) {
        output[i] = toupper((unsigned char)input[i]);
    }
}

int validate_filename(const char *filename) {
    if (strlen(filename)==0 || strlen(filename)>11) return -1;
    return 0;
}

void print_error(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
}

void print_hex_dump(const uint8_t *data, uint32_t length) {
    for (uint32_t i=0; i<length; i++) {
        printf("%02X ", data[i]);
        if ((i+1)%16==0) printf("\n");
    }
    printf("\n");
}

