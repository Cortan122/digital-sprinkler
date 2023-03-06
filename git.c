#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util.h"

#pragma comment(dir, "https://github.com/nothings/stb")
#include <stb_ds.h>

#pragma comment(lib, "z")
#include <zlib.h>

#define FPRINTF_REPO_INFO(goc) fprintf(stderr, \
  "\u2570\u2500\u2500"INFO"in repository %s:\x1b[32m%s\x1b[0m[%s]\n", (goc)->domain, (goc)->name, (goc)->branch);

#define SSH_PERSIST "1m"
#define SSH_TIMEOUT_ARGS "-o", "BatchMode=yes", "-o", "ConnectTimeout=5s", "-o", "ServerAliveInterval=5s"
#define SSH_MASTER_ARGS "-o", "ControlPersist="SSH_PERSIST, "-o", "ControlMaster=auto", SSH_TIMEOUT_ARGS
#define GIT_HASH_LEN 40

typedef struct Process {
  pid_t pid;
  FILE* input_pipe;
  FILE* output_pipe;
  char* name;
} Process;

#define PACK_SIGNATURE 0x5041434b
#define PACK_VERSION 2
typedef struct GitPackHeader {
  uint32_t signature;
  uint32_t version;
  uint32_t entries;
} GitPackHeader;

#define DEFLATE_BUFFER_SIZE 4096
typedef struct DeflateBuffer {
  FILE* file;
  size_t offset;
  size_t size;
  uint8_t buff[DEFLATE_BUFFER_SIZE];
} DeflateBuffer;

const char* git_object_names[] = {"none", "commit", "tree", "blob", "tag", "reserved", "ofs_delta", "ref_delta"};
enum GitObjectType {
  OBJ_NONE = 0,
  OBJ_COMMIT = 1,
  OBJ_TREE = 2,
  OBJ_BLOB = 3,
  OBJ_TAG = 4,
  /* 5 for future expansion */
  OBJ_OFS_DELTA = 6,
  OBJ_REF_DELTA = 7,
};

typedef struct GitObject {
  char* key;
  uint8_t* data;
  size_t length;
  enum GitObjectType type;
} GitObject;

typedef struct GitDelta {
  enum GitObjectType type;
  bool resolved;
  uint8_t* data;
  size_t length;
  union {
    uint8_t ref_hash[GIT_HASH_LEN/2];
    uintmax_t offset;
  };
} GitDelta;

typedef struct WantedObject {
  char* hash;
  char* path;
  bool is_needed;
} WantedObject;

typedef struct GitObjectCollection {
  char last_commit[GIT_HASH_LEN+1];
  char* domain;
  char* name;
  char* branch;
  char* filename;
  char* socket;
  char* treepath;
  GitObject* hashmap;
  GitDelta* delta_list;
  WantedObject* want_list;
} GitObjectCollection;

pid_t execFilePipe(char* name, char** arr, int pipes[2]){
  pid_t pid = fork();
  arr[0] = name;

  if(pid == -1){
    perror("fork");
    exit(1);
  }else if(pid > 0){
    return pid;
  }else{
    dup2(pipes[0], STDIN_FILENO);
    dup2(pipes[1], STDOUT_FILENO);
    close(pipes[0]);
    close(pipes[1]);

    execvp(name, arr);
    perror("execvp");
    fprintf(stderr, ERROR"can't run %s\n", name);
    exit(1);
  }
}

Process doublePopen(char* name, char** arr){
  int input_pipe[2];
  int output_pipe[2];
  pipe(input_pipe);
  pipe(output_pipe);

  Process res;
  res.name = name;
  res.pid = execFilePipe(name, arr, (int[]){input_pipe[0], output_pipe[1]});
  res.input_pipe = fdopen(input_pipe[1], "w");
  res.output_pipe = fdopen(output_pipe[0], "r");

  setvbuf(res.input_pipe, NULL, _IOLBF, 0);
  setvbuf(res.output_pipe, NULL, _IOLBF, 0);

  close(input_pipe[0]);
  close(output_pipe[1]);
  return res;
}

void closeProcess(Process* process){
  fclose(process->input_pipe);
  fclose(process->output_pipe);

  int status;
  waitpid(process->pid, &status, 0);
  if(status){
    fprintf(stderr, ERROR"%s exited with code %d\n", process->name, WEXITSTATUS(status));
    exit(1);
  }
}

char* readPktLine(FILE* f){
  char length[5] = {0};
  if(fread(length, 1, 4, f) != 4){
    fprintf(stderr, ERROR"EOF encountered while reading protocol lines: %m\n");
    return NULL;
  }
  int len = strtol(length, NULL, 16);
  if(len <= 4)return NULL;

  // todo: free!! and catch fread errors
  char* res = malloc(len+1);
  fread(res, 1, len-4, f);
  res[len-4] = '\0';
  if(res[len-5] == '\n')res[len-5] = '\0';
  return res;
}

void readPktLinesUntil(FILE* f, const char* filter){
  char* line;
  while((line = readPktLine(f))){
    bool res = filter && strcmp(line, filter) == 0;
    free(line);
    if(res)return;
  }
}

char* selectGitBranch(FILE* f, const char* filter){
  static char res[GIT_HASH_LEN+1];
  char* line;
  while((line = readPktLine(f))){
    if(strstr(line, filter))memcpy(res, line, GIT_HASH_LEN);
    free(line);
  }

  res[GIT_HASH_LEN] = '\0';
  return res;
}

void sendPktLine(FILE* f, const char* data){
  if(data == NULL || *data == '\0'){
    fputs("0000", f);
    fflush(f);
    return;
  }

  size_t len = strlen(data);
  fprintf(f, "%04x", (uint16_t)(len+4));
  fwrite(data, 1, len, f);
}

void printfPktLine(FILE* f, const char* format, const char* hash){
  size_t len = strlen(format) + strlen(hash) - 2;
  fprintf(f, "%04x", (uint16_t)(len+4));
  fprintf(f, format, hash);
}

uint8_t DeflateBuffer_getc(DeflateBuffer* dfb){
  if(dfb->size == 0){
    int res = fgetc(dfb->file);
    if(res < 0){
      fprintf(stderr, ERROR"DeflateBuffer: no more bytes available: %m\n");
      exit(1);
    }
    return res;
  }

  uint8_t res = dfb->buff[dfb->offset];
  dfb->offset++;
  dfb->size--;
  return res;
}

void DeflateBuffer_run(DeflateBuffer* dfb, uint8_t* mem, size_t size){
  z_stream zlib = {0};
  zlib.avail_out = size;
  zlib.next_out = mem;

  size_t prev_total = 0;
  inflateInit(&zlib);
  while(true){
    if(dfb->size == 0){
      dfb->size = fread(dfb->buff, 1, DEFLATE_BUFFER_SIZE, dfb->file);
      dfb->offset = 0;
    }
    zlib.avail_in = dfb->size;
    zlib.next_in = dfb->buff + dfb->offset;
    inflate(&zlib, Z_NO_FLUSH);
    if(zlib.msg){
      fprintf(stderr, ERROR"zlib error: %s\n", zlib.msg);
      exit(1);
    }
    dfb->offset += zlib.total_in - prev_total;
    dfb->size -= zlib.total_in - prev_total;
    if(prev_total == zlib.total_in)break;
    prev_total = zlib.total_in;
  }
  inflateEnd(&zlib);
}

void readPackFile(FILE* f, GitObjectCollection* res){
  if(!res->hashmap)sh_new_arena(res->hashmap);

  GitPackHeader hdr;
  fread(&hdr, sizeof(GitPackHeader), 1, f);
  assert(ntohl(hdr.signature) == PACK_SIGNATURE);
  assert(ntohl(hdr.version) == PACK_VERSION);

  DeflateBuffer buf = {.file = f};
  uint32_t count = ntohl(hdr.entries);
  for(uint32_t i = 0; i < count; i++){
    uint8_t byte = DeflateBuffer_getc(&buf);
    uintmax_t length = byte&0x0f;
    uint8_t type = (byte&0x70) >> 4;
    for(int j = 4; byte&0x80; j += 7){
      byte = DeflateBuffer_getc(&buf);
      length |= (byte&0x7f) << j;
    }

    uint8_t git_hash[GIT_HASH_LEN/2];
    uintmax_t offset = 0;
    if(type == OBJ_REF_DELTA){
      for(int j = 0; j < GIT_HASH_LEN/2; j++){
        git_hash[j] = DeflateBuffer_getc(&buf);
      }
    }else if(type == OBJ_OFS_DELTA){
      do{
        byte = DeflateBuffer_getc(&buf);
        offset <<= 7;
        offset |= byte&0x7f;
      }while(byte&0x80);
    }

    uint8_t* mem = malloc(length);
    DeflateBuffer_run(&buf, mem, length);
    if(type < OBJ_OFS_DELTA){
      GitObject tmp = {.data = mem, .length = length, .type = type};
      tmp.key = hexsha1git(git_object_names[type], mem, length);
      GitObject* old = shgetp_null(res->hashmap, tmp.key);
      if(old)free(old->data);
      shputs(res->hashmap, tmp);
    }else{
      GitDelta tmp = {.data = mem, .length = length, .type = type};
      if(type == OBJ_OFS_DELTA)tmp.offset = offset;
      else memcpy(tmp.ref_hash, git_hash, sizeof(git_hash));
      arrput(res->delta_list, tmp);
    }
  }
}

void deleteObjectCollection(GitObjectCollection* goc){
  free(goc->domain);
  free(goc->name);
  free(goc->branch);
  free(goc->filename);
  free(goc->socket);
  free(goc->treepath);

  for(int i = 0; i < arrlen(goc->delta_list); i++){
    free(goc->delta_list[i].data);
  }
  for(int i = 0; i < shlen(goc->hashmap); i++){
    free(goc->hashmap[i].data);
  }
  arrfree(goc->delta_list);
  shfree(goc->hashmap);

  for(int i = 0; i < arrlen(goc->want_list); i++){
    free(goc->want_list[i].hash);
    free(goc->want_list[i].path);
  }
  arrfree(goc->want_list);
}

void printGitObject(GitObject* o){
  if(o->type == OBJ_COMMIT){
    printf("commit = '%.*s'\n\n", (int)o->length, o->data);
  }else if(o->type == OBJ_BLOB){
    int len = (int)o->length > 30 ? 30 : (int)o->length;
    printf("blob = '%.*s'\n\n", len, o->data);
  }else if(o->type == OBJ_TREE){
    char* str = (char*)o->data;
    while(str < (char*)o->data + o->length){
      printf("dirent = %s ", str);
      uint8_t* data = (uint8_t*)str + strlen(str) + 1;
      for(int i = 0; i < GIT_HASH_LEN/2; i++){
        printf("%02x", data[i]);
      }
      printf("\n");
      str += strlen(str) + 1 + GIT_HASH_LEN/2;
    }
  }else{
    printf("o->type = %d %s\n", o->type, git_object_names[o->type]);
  }
}

void resolveDeltas(GitObjectCollection* goc){
  for(int i = 0; i < arrlen(goc->delta_list); i++){
    GitDelta delta = goc->delta_list[i];
    if(delta.resolved)continue;

    GitObject base;
    if(delta.type == OBJ_REF_DELTA){
      char* hash = sha1tohex(delta.ref_hash);
      base = shgets(goc->hashmap, hash);
    }else{
      fprintf(stderr, ERROR"resoving '%s' delta is not implemented yet\n", git_object_names[delta.type]);
      continue;
    }

    uint8_t* mem = delta.data;
    uintmax_t basesize = 0;
    uintmax_t newsize = 0;
    for(int j = 0;; j += 7){
      basesize |= (*mem&0x7f) << j;
      if(!(*(mem++)&0x80))break;
    }
    for(int j = 0;; j += 7){
      newsize |= (*mem&0x7f) << j;
      if(!(*(mem++)&0x80))break;
    }
    assert(base.length == basesize);

    GitObject res = base;
    uint8_t* newmem = malloc(newsize);
    res.data = newmem;
    res.length = newsize;
    for(; mem < delta.data + delta.length; mem++){
      uint8_t byte = *mem;
      if(byte&0x80){
        uint32_t offset = 0;
        uint32_t size = 0;
        if(byte&0x01)offset |= (*++mem) << 0*8;
        if(byte&0x02)offset |= (*++mem) << 1*8;
        if(byte&0x04)offset |= (*++mem) << 2*8;
        if(byte&0x08)offset |= (*++mem) << 3*8;
        if(byte&0x10)size   |= (*++mem) << 0*8;
        if(byte&0x20)size   |= (*++mem) << 1*8;
        if(byte&0x40)size   |= (*++mem) << 2*8;
        if(size == 0)size = 0x10000;

        if(size > newsize)size = newsize;
        memcpy(newmem, base.data + offset, size);
        newmem += size;
        newsize -= size;
      }else{
        if(byte > newsize)byte = newsize;
        memcpy(newmem, mem+1, byte);
        newmem += byte;
        mem += byte;
        newsize -= byte;
      }
    }

    res.key = hexsha1git(git_object_names[res.type], res.data, res.length);
    shputs(goc->hashmap, res);
    delta.resolved = true;
  }
}

void writeSizedString(FILE* f, char* str){
  size_t len = strlen(str);
  fwrite(&len, sizeof(size_t), 1, f);
  fwrite(str, 1, len, f);
}

bool readSizedString(FILE* f, char** str){
  size_t len = SIZE_MAX;
  if(fread(&len, sizeof(size_t), 1, f) != 1)return false;
  if(len == SIZE_MAX)return false;
  *str = calloc(len+1, 1);
  if(*str == NULL)return false;
  return fread(*str, 1, len, f) == len;
}

void saveObjectCollection(FILE* f, GitObjectCollection* goc){
  fwrite(&goc->last_commit, sizeof(goc->last_commit), 1, f);
  writeSizedString(f, goc->domain);
  writeSizedString(f, goc->name);
  writeSizedString(f, goc->branch);
  writeSizedString(f, goc->socket);

  size_t len = shlenu(goc->hashmap);
  fwrite(&len, sizeof(size_t), 1, f);
  for(int i = 0; i < shlen(goc->hashmap); i++){
    fwrite(&goc->hashmap[i], sizeof(GitObject), 1, f);
    fwrite(goc->hashmap[i].key, 1, GIT_HASH_LEN, f);
    fwrite(goc->hashmap[i].data, 1, goc->hashmap[i].length, f);
  }
}

bool loadObjectCollection(FILE* f, GitObjectCollection* goc){
  size_t len = SIZE_MAX;
  char hash[GIT_HASH_LEN+1] = {0};

  fread(&goc->last_commit, sizeof(goc->last_commit), 1, f);
  if(!readSizedString(f, &goc->domain))return false;
  if(!readSizedString(f, &goc->name))return false;
  if(!readSizedString(f, &goc->branch))return false;
  if(!readSizedString(f, &goc->socket))return false;

  fread(&len, sizeof(size_t), 1, f);
  if(len >= UINT32_MAX)return false;

  goc->hashmap = NULL;
  goc->delta_list = NULL;
  sh_new_arena(goc->hashmap);
  for(size_t i = 0; i < len; i++){
    GitObject o;
    fread(&o, sizeof(GitObject), 1, f);
    fread(&hash, 1, GIT_HASH_LEN, f);
    o.key = hash;
    o.data = malloc(o.length);
    if(o.data == NULL)return false;
    fread(o.data, 1, o.length, f);
    shputs(goc->hashmap, o);
  }

  return true;
}

Process spawnSshProcess(GitObjectCollection* goc){
  char* ssh_command = concatStrings((char*[]){"git-upload-pack '", goc->name, "'", NULL});
  char* args[] = {"ssh", SSH_MASTER_ARGS, "-S", goc->socket, goc->domain, ssh_command, NULL};
  Process ssh = doublePopen("ssh", args);
  free(ssh_command);
  return ssh;
}

bool updateObjectCollection(GitObjectCollection* goc){
  Process ssh = spawnSshProcess(goc);

  char* branch = selectGitBranch(ssh.output_pipe, goc->branch);
  if(feof(ssh.output_pipe) || strcmp(branch, goc->last_commit) == 0){
    // todo?: closing the process here synchronously costs another 100ms
    closeProcess(&ssh);
    return false;
  }else{
    fprintf(stderr, INFO"updating repository %s:\x1b[32m%s\x1b[0m[%s]\n", goc->domain, goc->name, goc->branch);
    memcpy(goc->last_commit, branch, sizeof(goc->last_commit));
  }

  printfPktLine(ssh.input_pipe, "want %s multi_ack filter no-progress", branch);
  sendPktLine(ssh.input_pipe, "deepen 1");
  sendPktLine(ssh.input_pipe, "filter blob:none");
  sendPktLine(ssh.input_pipe, NULL);
  for(int i = 0; i < shlen(goc->hashmap); i++){
    if(goc->hashmap[i].type != OBJ_TREE)continue;
    printfPktLine(ssh.input_pipe, "have %s", goc->hashmap[i].key);
  }
  sendPktLine(ssh.input_pipe, NULL);
  sendPktLine(ssh.input_pipe, "done\n");

  readPktLinesUntil(ssh.output_pipe, NULL);
  readPktLinesUntil(ssh.output_pipe, "NAK");
  free(readPktLine(ssh.output_pipe));

  readPackFile(ssh.output_pipe, goc);
  closeProcess(&ssh);
  resolveDeltas(goc);

  return true;
}

void createObjectCollection(GitObjectCollection* goc, char* url){
  memset(goc, 0, sizeof(GitObjectCollection));

  char* cachedir = concatStrings((char*[]){getenv("HOME"), "/.cache/sprinkler/", NULL});
  mkdir_safe(cachedir);

  char* sha = base64sha1string(url);
  char* name_start = strrchr(url, '/')+1;
  char* name_end = strstr(name_start, ".git");
  size_t name_len = name_end - name_start;
  if(name_len > 20)name_len = 20;
  memcpy(sha, name_start, name_len);
  goc->treepath = concatStrings((char*[]){cachedir, sha, NULL});
  goc->filename = concatStrings((char*[]){cachedir, sha, ".goc", NULL});

  FILE* f = fopen(goc->filename, "rb");
  if(f == NULL){
    if(errno != ENOENT){
      fprintf(stderr, ERROR"can't open file '%s': %m\n", goc->filename);
    }

    char* domain_start;
    char* domain_end;
    if(strncmp(url, "ssh://", 6) == 0){
      domain_start = url+6;
      domain_end = strchr(url, '/');
    }else{
      domain_start = url;
      domain_end = strchr(url, ':');
    }

    goc->domain = strndup(domain_start, domain_end - domain_start);
    goc->name = strdup(domain_end+1);
    goc->branch = strdup("master");

    char* domain_sha = base64sha1string(goc->domain);
    goc->socket = concatStrings((char*[]){cachedir, domain_sha, ".socket", NULL});

    fprintf(stderr, INFO"creating a new file for %s:\x1b[32m%s\x1b[0m[%s]\n", goc->domain, goc->name, goc->branch);
  }else{
    if(!loadObjectCollection(f, goc)){
      fprintf(stderr, ERROR"failed to load GitObjectCollection from file '%s': %m\n", goc->filename);
      fclose(f);
      remove(goc->filename);
      createObjectCollection(goc, url);
    }else{
      fclose(f);
    }
  }

  free(cachedir);
}

bool matchWildcard(const char* name, const char* pattern){
  // based on https://github.com/gcc-mirror/gcc/blob/master/libiberty/fnmatch.c
  // pattern is terminated by '/' or '\0'

  for(const char* p = pattern; *p != '\0' && *p != '/'; p++){
    if(*p == '*'){
      char c = *++p; // char after wildcard
      if(c == '\0' || c == '/')return true;
      for(; *name; name++){
        if(*name == c && matchWildcard(name, p))return true;
      }
      return false;
    }else{
      if(*name != *p)return false;
    }

    name++;
  }

  return *name == '\0';
}

int findBlobByPath(GitObjectCollection* goc, const char* path, const char* tree, char** prefix_buf){
  // the last two arguments are used for recursion and should left be NULL
  char hash[GIT_HASH_LEN+1] = {0};
  int res = 0;
  char* prefix_arr = NULL;
  if(prefix_buf == NULL){
    arrpush(prefix_arr, '\0');
    prefix_buf = &prefix_arr;
  }

  if(tree == NULL){
    GitObject* commit = shgetp(goc->hashmap, goc->last_commit);
    assert(commit->type == OBJ_COMMIT);
    assert(memcmp(commit->data, "tree ", 5) == 0);
    memcpy(hash, commit->data+5, GIT_HASH_LEN);
    tree = hash;
  }
  GitObject* tree_obj = shgetp(goc->hashmap, tree);
  assert(tree_obj->type == OBJ_TREE);

  char* tree_data = (char*)tree_obj->data;
  char* str = tree_data;
  while(str < tree_data + tree_obj->length){
    long file_mode = strtol(str, &str, 8);
    bool is_dir = file_mode == 040000;
    if(matchWildcard(++str, path)){
      uint8_t* data = (uint8_t*)str + strlen(str) + 1;
      memcpy(hash, sha1tohex(data), GIT_HASH_LEN);
      char* next = strchr(path, '/');
      if(next){
        if(!is_dir){
          fprintf(stderr, ERROR"%s/\x1b[32m%s\x1b[0m is not a directory\n", *prefix_buf, str);
          FPRINTF_REPO_INFO(goc);
          continue;
        }
        size_t prevlen = arrlenu(*prefix_buf);
        (*prefix_buf)[arrlen(*prefix_buf)-1] = '/';
        memcpy(arraddnptr(*prefix_buf, strlen(str)), str, strlen(str));
        arrput(*prefix_buf, '\0');
        res += findBlobByPath(goc, next+1, hash, prefix_buf);
        arrsetlen(*prefix_buf, prevlen);
      }else{
        if(is_dir){
          fprintf(stderr, ERROR"%s/\x1b[32m%s\x1b[0m is a directory\n", *prefix_buf, str);
          FPRINTF_REPO_INFO(goc);
          continue;
        }
        WantedObject tmp = {.hash = strdup(hash)};
        tmp.is_needed = shgetp_null(goc->hashmap, hash) == NULL;
        tmp.path = concatStrings((char*[]){*prefix_buf, "/", str, NULL});
        arrpush(goc->want_list, tmp);
        res++;
      }
    }

    str += strlen(str) + 1 + GIT_HASH_LEN/2;
  }

  if(prefix_arr && res == 0){
    fprintf(stderr, WARNING"no files matched pathspec \x1b[32m%s\x1b[0m\n", path);
    FPRINTF_REPO_INFO(goc);
  }
  arrfree(prefix_arr);
  return res;
}

bool fetchWantedBlobs(GitObjectCollection* goc){
  int count = 0;
  for(int i = 0; i < arrlen(goc->want_list); i++){
    count += goc->want_list[i].is_needed;
  }
  if(count == 0)return false;

  Process ssh = spawnSshProcess(goc);
  char* branch = selectGitBranch(ssh.output_pipe, goc->branch);
  if(feof(ssh.output_pipe)){
    closeProcess(&ssh);
    return false;
  }else if(strcmp(branch, goc->last_commit) != 0){
    fprintf(stderr, WARNING"branch changed while we weren't looking\n");
    FPRINTF_REPO_INFO(goc);
  }

  bool is_first = true;
  for(int i = 0; i < arrlen(goc->want_list); i++){
    if(!goc->want_list[i].is_needed)continue;

    if(is_first){
      printfPktLine(ssh.input_pipe, "want %s no-progress", goc->want_list[i].hash);
      is_first = false;
    }else{
      printfPktLine(ssh.input_pipe, "want %s", goc->want_list[i].hash);
    }
  }
  sendPktLine(ssh.input_pipe, NULL);
  sendPktLine(ssh.input_pipe, "done\n");

  readPktLinesUntil(ssh.output_pipe, "NAK");

  readPackFile(ssh.output_pipe, goc);
  closeProcess(&ssh);
  resolveDeltas(goc);

  return true;
}

void checkoutWantedBlobs(GitObjectCollection* goc){
  for(int i = 0; i < arrlen(goc->want_list); i++){
    char* path = concatStrings((char*[]){goc->treepath, "/", goc->want_list[i].path, NULL});
    if(goc->want_list[i].is_needed || access(path, R_OK) != 0){
      GitObject* o = shgetp_null(goc->hashmap, goc->want_list[i].hash);
      assert(o->type == OBJ_BLOB);

      mkdir_parents(path);
      FILE* file = fopen(path, "wb");
      if(file == NULL){
        fprintf(stderr, ERROR"failed to open file \x1b[32m%s\x1b[0m: %m\n", goc->want_list[i].path);
        fprintf(stderr, "\u2570"INFO"full name: %s\n", path);
      }else{
        fwrite(o->data, o->length, 1, file);
        fclose(file);
      }
    }
    free(path);
  }
}

bool pullObjectCollection(char* url, char** paths, size_t length, size_t stride){
  GitObjectCollection goc = {0};

  createObjectCollection(&goc, url);
  bool res = updateObjectCollection(&goc);
  // todo: short circuit here based on the change date of the config?
  for(size_t i = 0; i < length; i++){
    findBlobByPath(&goc, *paths, NULL, NULL);
    paths = (void*)paths + stride;
  }

  res |= fetchWantedBlobs(&goc);
  if(res){
    checkoutWantedBlobs(&goc);
    FILE* f = fopen(goc.filename, "wb");
    saveObjectCollection(f, &goc);
    fclose(f);
  }

  deleteObjectCollection(&goc);
  return res;
}

bool pullObjectCollection_cursed(char* url, void** opaque_stbarr, size_t elemsize, char** path_in, char** path_out){
  GitObjectCollection goc = {0};

  createObjectCollection(&goc, url);
  bool res = updateObjectCollection(&goc);

  size_t length = arrlenu(*opaque_stbarr);
  ptrdiff_t path_in_off = (char*)path_in - (char*)*opaque_stbarr;
  ptrdiff_t path_out_off = (char*)path_out - (char*)*opaque_stbarr;
  for(size_t i = 0; i < length; i++){
    int count = findBlobByPath(&goc, *(char**)(*opaque_stbarr + elemsize*i + path_in_off), NULL, NULL);

    for(int j = 0; j < count; j++){
      char* out_path = goc.want_list[j + arrlen(goc.want_list) - count].path;
      char* full_path = concatStrings((char*[]){goc.treepath, "/", out_path, NULL});
      if(j == 0){
        *(char**)(*opaque_stbarr + elemsize*i + path_out_off) = full_path;
      }else{
        size_t prevlen = arrlenu(*opaque_stbarr);
        *opaque_stbarr = stbds_arrgrowf(*opaque_stbarr, elemsize, 1, 0);
        memcpy(*opaque_stbarr + elemsize*prevlen, *opaque_stbarr + elemsize*i, elemsize);
        *(char**)(*opaque_stbarr + elemsize*prevlen + path_out_off) = full_path;
      }
      // todo: if path_out_loc != NULL ==> write memory url
    }
  }

  res |= fetchWantedBlobs(&goc);
  if(res){
    checkoutWantedBlobs(&goc);
    FILE* f = fopen(goc.filename, "wb");
    saveObjectCollection(f, &goc);
    fclose(f);
  }

  deleteObjectCollection(&goc);
  return res;
}
