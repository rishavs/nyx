#ifndef NIPPY_LIST_H
#define NIPPY_LIST_H

#include <stdbool.h>
#include <stddef.h>

/* A contiguous, owning list of fixed-size values. */
typedef struct {
    void *items;
    size_t length;
    size_t capacity;
    size_t item_size;
} List;

void list_init(List *list, size_t item_size);
bool list_push(List *list, const void *item);
bool list_pop(List *list, void *out_item);
const void *list_get(const List *list, size_t index);
void list_free(List *list);

#endif
