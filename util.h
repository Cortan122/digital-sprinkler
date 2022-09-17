#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct MmapedFile {
  int fd;
  char* data;
  size_t len;
} MmapedFile;

MmapedFile readFile(char* path, bool doMmap);
void closeFile(MmapedFile file);
long timems();
void timer(char* name);
char* getTimeString();
int execFileSync_status(char* name, char** arr);
void execFileSync(char* name, char** arr);
char* base64sha1string(char* path);
char* base64sha1file(char* path);
bool isOlderThen(const char* file1, const char* file2);
