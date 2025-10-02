// Syphax-Web - Ougi Washi

#ifndef SW_ARRAY_H
#define SW_ARRAY_H

#include "sw_types.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

// This approach is inspired by arena allocators but per array instead of being block-based to avoid fragmentation
// while offering a simple array handling interface.

#define SW_DEFINE_ARRAY(_type, _array, _size) \
    typedef struct { \
        _type data[_size]; \
        sz size; \
    } _array; \
    static void _array##_init(_array* array) { \
        memset(array, 0, sizeof(_type) * _size); \
        array->size = 0; \
    } \
    static _type* _array##_increment(_array* array) { \
        if (array->size == _size) { \
            return NULL; \
        } \
        array->size++; \
        return &array->data[array->size - 1]; \
    } \
    static _type* _array##_add(_array* array, _type value) { \
        sw_assertf(array->size < _size, "sw_array_add :: Array is full\n"); \
        _type* new_element = _array##_increment(array); \
        *new_element = value; \
        return new_element; \
    } \
    static sz _array##_find(_array* array, _type* value) { \
        for (sz i = 0; i < array->size; i++) { \
            if (&array->data[i] == value) { \
                return i; \
            } \
        } \
        return -1; \
    } \
    static sz _array##_find_last(_array* array, _type* value) { \
        sz index = array->size; \
        while (index > 0 && index <= array->size) { \
            index--; \
            if (&array->data[index] == value) { \
                return index; \
            } \
        } \
        return -1; \
    } \
    static void _array##_remove_at(_array* array, const size_t index) { \
        if (index >= array->size) return; \
        memmove(&array->data[index], &array->data[index + 1], sizeof(_type) * (array->size - index - 1)); \
        array->size--; \
    } \
    static void _array##_remove(_array* array, _type* value) { \
        const sz index = _array##_find_last(array, value); \
        if (index >= 0) { \
            _array##_remove_at(array, index); \
        } \
    } \
    static _type* _array##_get(_array* array, const sz index) { \
        sw_assertf(index >= 0 && index < array->size, "sw_array_get :: Index out of bounds %zu\n", index); \
        return &array->data[index]; \
    } \
    static _type* _array##_get_last(_array* array) { \
        sw_assertf(array->size > 0, "sw_array_get_last :: Array is empty\n"); \
        return &array->data[array->size - 1]; \
    } \
    static void _array##_set(_array* array, const sz index, _type* value) { \
        sw_assertf(index < array->size, "sw_array_set :: Index out of bounds %zu\n", index); \
        array->data[index] = *value; \
    } \
    static void _array##_clear(_array* array) { \
        memset(array->data, 0, sizeof(_type) * array->size); \
        array->size = 0; \
    } \
    static sz _array##_get_size(const _array* array) { \
        return array->size; \
    } 

#define sw_foreach(_array_type, _array, _it) \
    for (sz _it = 0; _it < _array_type##_get_size(_array); _it++)

#define sw_foreach_reverse(_array_type, _array, _it) \
    for (sz _it = _array_type##_get_size(_array); _it-- > 0;)

#define sw_remove_if(_array_type, _array, _current_value, _condition) \
    sw_foreach(_array_type, (_array), _it) { \
        if (_condition) { \
            _array_type##_remove_at((_array), _it); \
            break; \
        } \
    }

#define sw_remove_all(_array_type, _array, _type, _current_value, _condition) \
    if (_array_type##_get_size(_array) <= 0) { \
        return; \
    }  \
    sw_foreach_reverse(_array_type, (_array), _it) { \
        _type* _current_value = _array_type##_get((_array), _it); \
        if (_condition) { \
            _array_type##_remove_at((_array), _it); \
        } \
    }

#endif // SW_ARRAY_H
