#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

int parse_flags(const char *flag_str, char *out_flags);
void trim_whitespace(char *str);
void to_upper(char *str);
void format_name_11(const char *input, char output[11]);
int validate_filename(const char *filename);
void print_error(const char *msg);
void print_hex_dump(const uint8_t *data, uint32_t length);

#endif

