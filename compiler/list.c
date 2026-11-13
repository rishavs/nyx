#include "list.h"

#include "alloc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LIST_MIN_CAPACITY 4

#ifdef NIPPY_POISON_LISTS
/* Debug build (`make test-poison`). A pointer into a list is only valid until
   the next push, because the array may move. Normally it moves rarely, so a
   pointer held across a push usually keeps working and the bug hides. Here
   every push and pop moves the array to a fresh block and fills the old one
   with 0xDD before freeing it, so such a pointer reads garbage or faults at
   once instead of working by luck. */
static bool list_relocate(List *list, size_t new_capacity)
{
    void *moved = nippy_malloc(new_capacity * list->item_size);

    if (moved == NULL) {
        return false;
    }
    if (list->length > 0) {
        memcpy(moved, list->items, list->length * list->item_size);
    }
    if (list->items != NULL) {
        memset(list->items, 0xDD, list->capacity * list->item_size);
        free(list->items);
    }
    list->items = moved;
    list->capacity = new_capacity;
    return true;
}
#endif

void list_init(List *list, size_t item_size)
{
    list->items = NULL;
    list->length = 0;
    list->capacity = 0;
    list->item_size = item_size;
}

bool list_push(List *list, const void *item)
{
    if (list == NULL || item == NULL || list->item_size == 0) {
        return false;
    }

#ifdef NIPPY_POISON_LISTS
    if (!list_relocate(list, list->length + 1)) {
        return false;
    }
#else
    if (list->length == list->capacity) {
        size_t new_capacity;
        void *new_items;
        size_t max_capacity = SIZE_MAX / list->item_size;

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
#endif

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

#ifdef NIPPY_POISON_LISTS
    /* Shrinking is best effort, as in the normal build. */
    (void)list_relocate(list, list->length);
    (void)new_capacity;
#else
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

#endif

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
