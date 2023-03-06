#define _POSIX_C_SOURCE 200112L
#include "util.h"
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>

#pragma comment(lib, "m")
#include <math.h>

#pragma comment(option, "-Wno-deprecated-declarations")
#pragma comment(lib, "crypto")
#include <openssl/sha.h>
#include <openssl/evp.h>

MmapedFile readFile(char* path, bool doMmap){
  int fd = open(path, O_RDONLY);
  if(fd < 0){
    perror(path);
    exit(1);
  }
  struct stat stat;
  if(fstat(fd, &stat)){
    perror(path);
    exit(1);
  }
  size_t len = stat.st_size;

  char* data = NULL;
  if(doMmap){
    data = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
  }else{
    data = calloc(len+1, 1);
    read(fd, data, len);
    close(fd);
    fd = 0;
  }
  return (MmapedFile){fd,data,len};
}

void closeFile(MmapedFile file){
  if(file.fd){
    munmap(file.data, file.len);
    close(file.fd);
  }else{
    free(file.data);
  }
}

long timems(){
  struct timespec spec;
  clock_gettime(CLOCK_REALTIME, &spec);
  return spec.tv_sec*1000 + round(spec.tv_nsec / 1.0e6);
}

static char* prevTimerName = NULL;
static long prevTimerStart = 0;

void timer(char* name){
  long time = timems();
  if(prevTimerName){
    printf("%10s took %3ldms\n", prevTimerName, time-prevTimerStart);
  }
  prevTimerName = name;
  prevTimerStart = time;
}

char* getTimeString(){
  time_t rawtime = time(NULL);
  struct tm* timeinfo = localtime(&rawtime);
  static char result[30];
  strftime(result, sizeof(result), "\e[36m%d.%m.%Y %T\e[0m", timeinfo);
  return result;
}

int execFileSync_status(char* name, char** arr){
  pid_t pid = fork();
  arr[0] = name;

  if(pid == -1){
    perror("fork");
    exit(1);
  }else if(pid > 0){
    int status;
    waitpid(pid, &status, 0);
    return status;
  }else{
    execvp(name, arr);
    perror("execvp");
    fprintf(stderr, "can't run %s\n", name);
    exit(1);
  }
}

void execFileSync(char* name, char** arr){
  int status = execFileSync_status(name, arr);
  if(status){
    fprintf(stderr, "%s exited with code %d\n", name, status);
    exit(1);
  }
}

static char* sha1base64(uint8_t* hash){
  static char base64[4*((SHA_DIGEST_LENGTH+2)/3)+1];
  EVP_EncodeBlock((uint8_t*)base64, hash, SHA_DIGEST_LENGTH);

  for(size_t i = 0; i < sizeof(base64); i++){
    if(base64[i] == '/')base64[i] = '_';
    if(base64[i] == '+')base64[i] = '-';
    if(base64[i] == '=')base64[i] = '\0';
  }

  return base64;
}

char* base64sha1string(char* path){
  uint8_t hash[SHA_DIGEST_LENGTH];
  SHA1((uint8_t*)path, strlen(path), hash);
  return sha1base64(hash);
}

char* base64sha1file(char* path){
  MmapedFile file = readFile(path, false);
  uint8_t hash[SHA_DIGEST_LENGTH];
  SHA1((uint8_t*)file.data, file.len, hash);
  closeFile(file);
  return sha1base64(hash);
}

char* sha1tohex(uint8_t* hash){
  static char hex[SHA_DIGEST_LENGTH*2+1];
  for(int i = 0; i < SHA_DIGEST_LENGTH; i++){
    sprintf(hex + i*2, "%02x", hash[i]);
  }
  hex[SHA_DIGEST_LENGTH*2] = '\0';
  return hex;
}

char* hexsha1git(const char* prefix, uint8_t* data, size_t len){
  uint8_t hash[SHA_DIGEST_LENGTH];
  SHA_CTX sha1context = {0};
  char buff[22] = {0};
  snprintf(buff, sizeof(buff), " %lu", len);

  SHA1_Init(&sha1context);
  SHA1_Update(&sha1context, prefix, strlen(prefix));
  SHA1_Update(&sha1context, buff, strlen(buff)+1);
  SHA1_Update(&sha1context, data, len);
  SHA1_Final(hash, &sha1context);

  return sha1tohex(hash);
}

bool isOlderThen(const char* file1, const char* file2){
  struct stat b1, b2;
  if(stat(file1, &b1) || stat(file2, &b2))return true; // true, since at least one stat failed
  return b1.st_mtime < b2.st_mtime;
}

char* concatStrings(char* const* arr){
  size_t total_len = 0;
  for(int i = 0; arr[i]; i++){
    total_len += strlen(arr[i]);
  }

  char* res = malloc(total_len+1);
  res[0] = '\0';
  for(int i = 0; arr[i]; i++){
    strcat(res, arr[i]);
  }
  return res;
}

void mkdir_safe(const char* dir){
  if(mkdir(dir, 0755)){
    if(errno != EEXIST){
      perror(dir);
      exit(1);
    }
  }
}

void mkdir_parents(char* file_path){
  // todo: check existence only once
  for(int i = 1; file_path[i]; i++){
    if(file_path[i] == '/'){
      file_path[i] = '\0';
      mkdir_safe(file_path);
      file_path[i] = '/';
    }
  }
}
