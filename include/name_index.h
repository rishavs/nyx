#ifndef NIPPY_NAME_INDEX_H
#define NIPPY_NAME_INDEX_H

#include "list.h"

/* Hash slots store list indices plus one; zero marks an empty slot. */
typedef struct {
    size_t *slots;
    size_t capacity;
} NameIndex;

void name_index_init(NameIndex *index);
void name_index_free(NameIndex *index);
size_t name_index_find(const NameIndex *index, const List *entries,
                       const char *name, size_t length);
bool name_index_insert(NameIndex *index, const List *entries, size_t entry);
/* Rebuild after removing entries from the end of the list. */
void name_index_rebuild(NameIndex *index, const List *entries);

#endif
