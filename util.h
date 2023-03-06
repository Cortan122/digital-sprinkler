#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PROGRAM_NAME
#define PROGRAM_NAME "sprinkler"
#endif

#ifndef INFO
#define ERROR "\x1b[31mERROR\x1b[0m: \x1b[93m"PROGRAM_NAME"\x1b[0m: "
#define WARNING "\x1b[95mWARNING\x1b[0m: \x1b[93m"PROGRAM_NAME"\x1b[0m: "
#define INFO "\x1b[36mINFO\x1b[0m: \x1b[93m"PROGRAM_NAME"\x1b[0m: "
#endif

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
char* sha1tohex(uint8_t* hash);
char* hexsha1git(const char* prefix, uint8_t* data, size_t len);
bool isOlderThen(const char* file1, const char* file2);
char* concatStrings(char* const* arr);
void mkdir_safe(const char* dir);
void mkdir_parents(char* file_path);
