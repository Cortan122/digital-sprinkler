#pragma once

#include <stdbool.h>
#include <stddef.h>

bool pullObjectCollection(char* url, char** paths, size_t length, size_t stride);
bool pullObjectCollection_cursed(char* url, void** opaque_stbarr, size_t elemsize, char** path_in, char** path_out);
