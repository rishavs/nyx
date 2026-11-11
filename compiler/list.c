#include "list.h"

#include "alloc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LIST_MIN_CAPACITY 4

void list_init(List *list, size_t item_size)
{
    list->items = NULL;
    list->length = 0;
    list->capacity = 0;
    list->item_size = item_size;
}

bool list_push(List *list, const void *item)
{
    size_t max_capacity;

    if (list == NULL || item == NULL || list->item_size == 0) {
        return false;
    }

    if (list->length == list->capacity) {
        size_t new_capacity;
        void *new_items;

        max_capacity = SIZE_MAX / list->item_size;
        if (list->capacity == 0) {
            new_capacity = max_capacity < LIST_MIN_CAPACITY
                               ? max_capacity
                               : LIST_MIN_CAPACITY;
        } else if (list->capacity > max_capacity / 2) {
            new_capacity = max_capacity;
        } else {
            new_capacity = list->capacity * 2;
        }
        if (new_capacity <= list->length) {
            return false;
        }

        new_items = nippy_realloc(list->items, new_capacity * list->item_size);
        if (new_items == NULL) {
            return false;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    memcpy((unsigned char *)list->items + list->length * list->item_size,
           item, list->item_size);
    list->length++;
    return true;
}

bool list_pop(List *list, void *out_item)
{
    size_t new_capacity;

    if (list == NULL || out_item == NULL || list->length == 0 ||
        list->item_size == 0) {
        return false;
    }

    memcpy(out_item,
           (unsigned char *)list->items + (list->length - 1) * list->item_size,
           list->item_size);
    list->length--;

    if (list->length == 0) {
        free(list->items);
        list->items = NULL;
        list->capacity = 0;
        return true;
    }

    if (list->capacity > LIST_MIN_CAPACITY &&
        list->length <= list->capacity / 4) {
        new_capacity = list->capacity / 2;
        if (new_capacity < LIST_MIN_CAPACITY) {
            new_capacity = LIST_MIN_CAPACITY;
        }
        if (new_capacity < list->length) {
            new_capacity = list->length;
        }
        {
            void *new_items = nippy_realloc(list->items,
                                            new_capacity * list->item_size);
            if (new_items != NULL) {
                list->items = new_items;
                list->capacity = new_capacity;
            }
        }
    }

    return true;
}

const void *list_get(const List *list, size_t index)
{
    if (list == NULL || index >= list->length || list->item_size == 0) {
        return NULL;
    }
    return (const unsigned char *)list->items + index * list->item_size;
}

void list_free(List *list)
{
    if (list == NULL) {
        return;
    }
    free(list->items);
    list->items = NULL;
    list->length = 0;
    list->capacity = 0;
    list->item_size = 0;
}
