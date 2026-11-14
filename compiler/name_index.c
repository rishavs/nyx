#include "name_index.h"
#include "alloc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t name_hash(const char *text, size_t length)
{
    size_t hash = (size_t)1469598103934665603ULL;
    size_t i;
    for (i = 0; i < length; i++) {
        hash ^= (unsigned char)text[i];
        hash *= (size_t)1099511628211ULL;
    }
    return hash;
}

void name_index_init(NameIndex *index)
{
    index->slots = NULL;
    index->capacity = 0;
}

void name_index_free(NameIndex *index)
{
    free(index->slots);
    index->slots = NULL;
    index->capacity = 0;
}

void name_index_rebuild(NameIndex *index, const List *entries)
{
    size_t i;

    if (index->capacity == 0) return;
    memset(index->slots, 0, index->capacity * sizeof(size_t));
    for (i = 0; i < entries->length; i++) {
        const char *name = list_get(entries, i);
        size_t slot = name_hash(name, strlen(name)) & (index->capacity - 1);

        while (index->slots[slot] != 0) {
            slot = (slot + 1) & (index->capacity - 1);
        }
        index->slots[slot] = i + 1;
    }
}

size_t name_index_find(const NameIndex *index, const List *entries, const char *name,
                       size_t length)
{
    size_t slot;
    if (index->capacity == 0) {
        return (size_t)-1;
    }
    slot = name_hash(name, length) & (index->capacity - 1);
    while (index->slots[slot] != 0) {
        size_t item = index->slots[slot] - 1;
        const char *existing = list_get(entries, item);
        if (existing != NULL && strlen(existing) == length &&
            memcmp(existing, name, length) == 0) {
            return item;
        }
        slot = (slot + 1) & (index->capacity - 1);
    }
    return (size_t)-1;
}

bool name_index_insert(NameIndex *index, const List *entries, size_t entry)
{
    size_t slot;
    const char *name = list_get(entries, entry);
    if (name == NULL) {
        return false;
    }
    if (index->capacity == 0 || entries->length > index->capacity / 2) {
        size_t capacity = index->capacity == 0 ? 32 : index->capacity * 2;
        size_t *slots;
        size_t i;
        if (capacity < index->capacity || capacity > SIZE_MAX / sizeof(size_t)) {
            return false;
        }
        slots = nippy_malloc(capacity * sizeof(size_t));
        if (slots == NULL) {
            return false;
        }
        memset(slots, 0, capacity * sizeof(size_t));
        for (i = 0; i < entries->length; i++) {
            const char *item = list_get(entries, i);
            slot = name_hash(item, strlen(item)) & (capacity - 1);
            while (slots[slot] != 0) {
                slot = (slot + 1) & (capacity - 1);
            }
            slots[slot] = i + 1;
        }
        free(index->slots);
        index->slots = slots;
        index->capacity = capacity;
        return true;
    }
    slot = name_hash(name, strlen(name)) & (index->capacity - 1);
    while (index->slots[slot] != 0) {
        slot = (slot + 1) & (index->capacity - 1);
    }
    index->slots[slot] = entry + 1;
    return true;
}
