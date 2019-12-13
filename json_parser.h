#ifndef JSON_PARSER_H_
#define JSON_PARSER_H_

#include <stddef.h>

struct json_value;
struct json_value* parse_json(const char* data, const size_t len);

#endif
