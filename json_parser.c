#include "json.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

struct json_parser_state {
    const char* data;
    size_t len;
    size_t pos;
    bool error;
};

static void free_json_object(struct json_object* object);
static void free_json_array(struct json_array* array);
static struct json_value* parse_value(struct json_parser_state* state);
static char* parse_string(struct json_parser_state* state);

static bool check_end(struct json_parser_state* state) {
    state->error = state->pos >= state->len;
    return !state->error;
}

static bool expect_char(struct json_parser_state* state, char check) {
    if (!check_end(state))
        return false;
    state->error = state->data[state->pos] != check;
    if (!state->error)
        state->pos++;
    return !state->error;
}

static void parse_ws(struct json_parser_state* state) {
    while (state->pos < state->len && isspace(state->data[state->pos])) {
        state->pos++;
    }
}

static struct json_value* parse_element(struct json_parser_state* state) {
    parse_ws(state);
    struct json_value* value = parse_value(state);
    if (state->error) {
        free_json_value(value);
        return NULL;
    }
    parse_ws(state);
    return value;
}

static struct json_object* parse_member(struct json_parser_state* state) {
    parse_ws(state);
    char* key = parse_string(state);
    if (state->error) {
        free(key);
        return NULL;
    }
    parse_ws(state);
    if (!expect_char(state, ':')) {
        free(key);
        return NULL;
    }
    struct json_value* value = parse_element(state);
    if (state->error) {
        free(key);
        free_json_value(value);
        return NULL;
    }
    struct json_object* object = malloc(sizeof(struct json_object));
    object->key = key;
    object->value = value;
    object->next = NULL;
    return object;
}

static struct json_object* parse_object(struct json_parser_state* state) {
    if (!expect_char(state, '{')) {
        return NULL;
    }
    parse_ws(state);
    if (!check_end(state))
        return NULL;
    if (state->data[state->pos] == '}') {
        // This is an empty object
        state->pos++; // }
        return NULL;
    }
    // Otherwise, we have a head member
    struct json_object* head = parse_member(state);
    if (state->error) {
        free_json_object(head);
        return NULL;
    }
    struct json_object* curr = head;
    while(state->data[state->pos] != '}') {
        if (!expect_char(state, ',')) {
            free_json_object(head);
            return NULL;
        }
        // get the next member, and append to the list
        curr->next = parse_member(state);
        if (state->error) {
            free_json_object(head);
            return NULL;
        }
        curr = curr->next;
    }
    if (!expect_char(state, '}')) {
        free_json_object(head);
        return NULL;
    }
    return head;
}

static struct json_array* parse_array(struct json_parser_state* state) {
    if (!expect_char(state, '[')) {
        return NULL;
    }
    parse_ws(state);
    if (!check_end(state))
        return NULL;
    
    struct json_array* array = malloc(sizeof *array);
    array->values = NULL;
    array->size = 0;
    if (state->data[state->pos] == ']') {
        // This is an empty array
        state->pos++; // }
        return array;
    }
    // Otherwise, array is non-empty, initialize
    size_t capacity = 16;
    array->values = calloc(capacity, sizeof *array->values);
    
    // get the first element
    array->values[0] = parse_element(state);
    array->size = 1;
    if (state->error) {
        free_json_array(array);
        return NULL;
    }

    while(state->data[state->pos] != ']') {
        if (!expect_char(state, ',')) {
            free_json_array(array);
            return NULL;
        }
        // get the next element
        struct json_value* next = parse_element(state);
        if (state->error) {
            free_json_value(next);
            free_json_array(array);
            return NULL;
        }

        // reallocate if we need more space
        if (array->size == capacity) {
            capacity *= 2;
            struct json_value** new_values = calloc(capacity, sizeof *new_values);
            memcpy(new_values, array->values, array->size * sizeof *array->values);
            free(array->values);
            array->values = new_values;
        }
        array->values[array->size] = next;
        array->size++;
    }
    if (!expect_char(state, ']')) {
        free_json_array(array);
        return NULL;
    }
    return array;
}

static char* parse_string(struct json_parser_state* state) {
    if (!expect_char(state, '"'))
        return NULL;

    // first count the chars
    size_t start_pos = state->pos;
    size_t count;
    for (count = 0;
         state->pos < state->len && state->data[state->pos] != '"';
         state->pos++, count++) {
        if (state->data[state->pos] == '\\') {
            // skip escaped chars
            state->pos++;
        }
    }

    if (!check_end(state)) {
        return NULL;        
    }

    // rewind to the start of the string
    state->pos = start_pos;

    char* string = malloc(sizeof(char) * (count + 1));
    for (size_t str_pos = 0;
         state->data[state->pos] != '"';
         state->pos++, str_pos++) {
        if (state->data[state->pos] == '\\') {
            // TODO: Add proper escape character handling
            // Just skip backslashes for now
            state->pos++;
        }
        string[str_pos] = state->data[state->pos];
    }
    string[count] = '\0';
    if (!expect_char(state, '"')) {
        free(string);
        return NULL;
    }
    return string;
}

static int64_t parse_int(struct json_parser_state* state) {
    // TODO: Use stdlib to parse this?
    if (!check_end(state))
        return 0;
    bool negative = state->data[state->pos] == '-';
    if (negative)
        state->pos++; // -

    if (!check_end(state))
        return 0;
    if (!isdigit(state->data[state->pos])) {
        state->error = true;
        return 0;
    }

    int64_t num = 0;
    for (; state->pos < state->len && isdigit(state->data[state->pos]); state->pos++) {
        num *= 10;
        num += state->data[state->pos] - '0';
    }
    if (negative)
        num *= -1;
    return num;
}

static struct json_value* create_json_value() {
    struct json_value* value = malloc(sizeof(struct json_value));
    value->type = JSON_ERROR;
    return value;
}

static struct json_value* parse_value(struct json_parser_state* state) {
    if (!check_end(state))
        return NULL;
    struct json_value* value = create_json_value();
    char next_char = state->data[state->pos];
    // object: '{'
    // array: '['
    // string: '"'
    // true
    // false
    // null
    // number: else
    switch (next_char) {
        case '{':
            value->type = JSON_OBJECT;
            value->object = parse_object(state);
            break;
        case '[':
            value->type = JSON_ARRAY;
            value->array = parse_array(state);
            break;
        case '"':
            value->type = JSON_STRING;
            value->string = parse_string(state);
            break;
        case 't':
            value->type = JSON_TRUE;
            state->pos += 4;
            break;
        case 'f':
            value->type = JSON_FALSE;
            state->pos += 5;
            break;
        case 'n':
            value->type = JSON_NULL;
            state->pos += 4;
            break;
        default:
            value->type = JSON_INT;
            value->integer = parse_int(state);
    }
    
    if (state->error) {
        free_json_value(value);
        return NULL;
    }
    return value;
}


struct json_value* parse_json(const char* data, const size_t len) {
    struct json_parser_state state = {
        .data = data,
        .len = len,
        .pos = 0,
        .error = false,
    };
    struct json_value* result = parse_value(&state);

    if (!state.error)
        return result;

    free_json_value(result);
    return create_json_value(JSON_ERROR);
}

static void free_json_object(struct json_object* object) {
    if (object == NULL) return;
    free(object->key);
    free_json_value(object->value);
    free_json_object(object->next);
    free(object);
}

static void free_json_array(struct json_array* array) {
    if (array == NULL) return;

    for (size_t i = 0; i < array->size; i++) {
        free_json_value(array->values[i]);
    }
    free(array->values);

    free(array);
}

void free_json_value(struct json_value* value) {
    if (value == NULL) return;
    switch (value->type) {
        case JSON_OBJECT:
            free_json_object(value->object);
            break;
        case JSON_ARRAY:
            free_json_array(value->array);
            break;
        case JSON_STRING:
            free(value->string);
            break;
        default:
            break;
    }
    free(value);
}

struct json_value* json_object_lookup(struct json_value* object_value, const char* key) {
    if (object_value->type != JSON_OBJECT) {
        return NULL;
    }
    for (struct json_object* curr = object_value->object; curr != NULL; curr = curr->next) {
        if (strcmp(curr->key, key) == 0)
            return curr->value;
    }
    return NULL;
}
