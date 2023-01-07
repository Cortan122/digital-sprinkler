#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>
#include <glob.h>
#include <getopt.h>

#include "util.h"

#pragma comment(option, "-Wno-unused-function")
#pragma comment(option, "-Wno-sign-compare")
#pragma comment(dir, "https://github.com/nothings/stb")
#define STB_DS_IMPLEMENTATION
#include <stb_ds.h>

#define ERROR "\x1b[31mERROR\x1b[0m: \x1b[93msprinkler\x1b[0m: "
#define WARNING "\x1b[95mWARNING\x1b[0m: \x1b[93msprinkler\x1b[0m: "
#define INFO "\x1b[36mINFO\x1b[0m: \x1b[93msprinkler\x1b[0m: "

#ifndef __DIR__
  #define __DIR__ "."
#endif

struct option longOptionRom[] = {
  {"config", required_argument, 0, 'i'},
  {"scripts", required_argument, 0, 's'},
  {"output", required_argument, 0, 'o'},
  {"help", no_argument, 0, 'h'},
  {0, 0, 0, 0}
};

typedef struct Command {
  char* script_path;
  char* input_path;
  char* output_path;
} Command;

typedef struct ConfigLine {
  char* filter;
  char* repo;
  char* path_in_repo;
  char* output;
  char* src_path;
} ConfigLine;

typedef struct RepoList {
  char* key;
  ConfigLine* value;
  char* git_path;
  char* tree_path;
  bool do_full_clone;
} RepoList;

char* nextField(char** datap, const char* substr){
  if(*datap == NULL)return NULL;

  char* res = *datap;
  while(*res == ' ')res++;

  char* tab = strstr(*datap, substr);
  if(tab){
    *datap = tab+1;
    *(tab--) = '\0';
    while(*tab == ' ')*(tab--) = '\0';
  }else{
    *datap = NULL;
  }

  return res;
}

RepoList* parseConfig(char* data){
  RepoList* res = NULL;

  int i = 0;
  while(data){
    i++;
    char* line = nextField(&data, "\n");
    if(strlen(line) == 0)continue;
    if(line[0] == '#')continue;
    if(i == 1)continue;

    char* filter = nextField(&line, "\t");
    char* repo = nextField(&line, "\t");
    char* path_in_repo = nextField(&line, "\t");
    char* output = nextField(&line, "\t");
    char* all = nextField(&line, "\t");
    bool do_full_clone = all && (strcmp(all, "1") == 0 || strcasecmp(all, "true") == 0);

    if(line != NULL){
      fprintf(stderr, WARNING"extra text \x1b[33m'%s'\x1b[0m on line %d\n", line, i);
    }

    if(filter == NULL || repo == NULL || path_in_repo == NULL || output == NULL){
      fprintf(stderr, ERROR"missing fields on line %d\n", i);
      continue;
    }

    if(strstr(path_in_repo, "..") || strstr(output, "..")){
      fprintf(stderr, ERROR"paths contain '..' on line %d\n", i);
      continue;
    }

    RepoList* entry = shgetp_null(res, repo);
    if(entry == NULL){
      RepoList tmp = {0};
      tmp.key = repo;
      shputs(res, tmp);
      entry = shgetp_null(res, repo);
    }
    arrpush(entry->value, ((ConfigLine){filter, repo, path_in_repo, output, NULL}));
    entry->do_full_clone |= do_full_clone;
  }

  return res;
}

void freeConfig(RepoList* arr){
  for(int i = 0; i < shlen(arr); i++){
    for(int j = 0; j < arrlen(arr[i].value); j++){
      free(arr[i].value[j].src_path);
    }
    arrfree(arr[i].value);
    free(arr[i].git_path);
    free(arr[i].tree_path);
  }
  shfree(arr);
}

void freeCommands(Command* commands){
  for(int i = 0; i < arrlen(commands); i++){
    free(commands[i].script_path);
    free(commands[i].input_path);
    free(commands[i].output_path);
  }
  arrfree(commands);
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
  for(int i = 1; file_path[i]; i++){
    if(file_path[i] == '/'){
      file_path[i] = '\0';
      mkdir_safe(file_path);
      file_path[i] = '/';
    }
  }
}

void partialCheckout(RepoList* repo){
  char* cmd[8] = {"git", "--work-tree", repo->tree_path, "--git-dir", repo->git_path, "checkout", "master", NULL};

  char** extra_files = NULL;
  for(int j = 0; j < arrlen(repo->value); j++){
    ConfigLine* line = &repo->value[j];
    line->src_path = concatStrings((char*[]){repo->tree_path, "/", line->path_in_repo, NULL});
    if(!repo->do_full_clone && access(line->src_path, R_OK) != 0){
      arrpush(extra_files, line->path_in_repo);
    }
  }

  if(repo->do_full_clone){
    execFileSync("git", cmd);
    return;
  }

  if(arrlen(extra_files)){
    arrinsn(extra_files, 0, 7);
    memcpy(extra_files, cmd, 7*sizeof(char*));
    arrpush(extra_files, NULL);
    execFileSync("git", extra_files);
  }
  arrfree(extra_files);
}

void ensureRepos(RepoList* arr){
  char* cachedir = concatStrings((char*[]){getenv("HOME"), "/.cache/sprinkler/", NULL});
  mkdir_safe(cachedir);

  for(int i = 0; i < shlen(arr); i++){
    char* sha = base64sha1string(arr[i].key);
    char* name_start = strrchr(arr[i].key, '/')+1;
    char* name_end = strstr(name_start, ".git");
    size_t name_len = name_end - name_start;
    if(name_len > 20)name_len = 20;
    memcpy(sha, name_start, name_len);

    arr[i].tree_path = concatStrings((char*[]){cachedir, sha, NULL});
    arr[i].git_path = concatStrings((char*[]){arr[i].tree_path, ".git", NULL});

    if(access(arr[i].git_path, R_OK) == 0){
      char* pull_cmd[] = {"git", "--work-tree", arr[i].tree_path, "--git-dir", arr[i].git_path, "pull", "--quiet", NULL};
      int res = execFileSync_status("git", pull_cmd);
      if(res){
        fprintf(stderr, WARNING"failed to pull repo %.*s\n", (int)name_len, name_start);
        execFileSync("rm", (char*[]){"rm", "-rf", arr[i].tree_path, arr[i].git_path, NULL});
        goto clone;
      }
    }else{
      clone:;
      execFileSync("git", (char*[]){"git", "clone", "--depth=1", "--filter=blob:none", "--bare", arr[i].key, arr[i].git_path, NULL});
      mkdir_safe(arr[i].tree_path);
    }

    partialCheckout(&arr[i]);
  }

  free(cachedir);
}

char* makeOutputWildcard(ConfigLine* line, char* input_path, char* output_dir){
  char* res = NULL;

  if(strstr(line->output, "*") == NULL){
    res = concatStrings((char*[]){output_dir, "/", line->output, NULL});
  }else{
    char* name_start = strrchr(input_path, '/')+1;
    char* name_end = strrchr(name_start, '.');
    char* output_star = strrchr(line->output, '*');

    *name_end = '\0';
    *output_star = '\0';
    res = concatStrings((char*[]){output_dir, "/", line->output, name_start, output_star+1, NULL});
    *name_end = '.';
    *output_star = '*';
  }

  mkdir_parents(res);
  return res;
}

Command* createCommands(RepoList* arr, char* scripts_dir, char* output_dir){
  Command* res = NULL;

  for(int i = 0; i < shlen(arr); i++){
    for(int j = 0; j < arrlen(arr[i].value); j++){
      ConfigLine* line = &arr[i].value[j];

      char* script_path = NULL;
      char* input_path = NULL;
      char* output_path = NULL;

      if(strcmp(line->filter, "copy") != 0){
        script_path = concatStrings((char*[]){scripts_dir, "/", line->filter, NULL});
      }

      if(strstr(line->path_in_repo, "*") == NULL){
        input_path = strdup(line->src_path);
        output_path = makeOutputWildcard(line, input_path, output_dir);
        arrpush(res, ((Command){script_path, input_path, output_path}));
      }else{
        glob_t globbuf;
        glob(line->src_path, GLOB_NOSORT, NULL, &globbuf);

        for(int k = 0; k < globbuf.gl_pathc; k++){
          input_path = strdup(globbuf.gl_pathv[k]);
          output_path = makeOutputWildcard(line, input_path, output_dir);
          arrpush(res, ((Command){script_path, input_path, output_path}));
          if(script_path)script_path = strdup(script_path);
        }

        globfree(&globbuf);
        free(script_path);
      }
    }
  }

  return res;
}

void runCommands(Command* commands){
  for(int i = 0; i < arrlen(commands); i++){
    Command* cmd = &commands[i];
    bool input_changed = isOlderThen(cmd->output_path, cmd->input_path);
    bool script_changed = cmd->script_path && isOlderThen(cmd->output_path, cmd->script_path);
    if(!input_changed && !script_changed)continue;

    char* name = strrchr(cmd->output_path, '/')+1;
    fprintf(stderr, INFO"updating %s on %s\n", name, getTimeString());

    char* exe = cmd->script_path ?: "cp";
    execFileSync(exe, (char*[]){exe, cmd->input_path, cmd->output_path, NULL});
  }
}

void sprinkle(char* config_path, char* script_path, char* output_path){
  MmapedFile file = readFile(config_path, false);
  RepoList* arr = parseConfig(file.data);
  ensureRepos(arr);
  Command* commands = createCommands(arr, script_path, output_path);
  runCommands(commands);

  freeCommands(commands);
  freeConfig(arr);
  closeFile(file);
}

int main(int argc, char** argv){
  char* config_path = __DIR__ "/config.tsv";
  char* script_path = __DIR__ "/scripts";
  char* output_path = __DIR__ "/www";

  while(1){
    int optionIndex = 0;
    int c = getopt_long(argc, argv, "hi:s:o:", longOptionRom, &optionIndex);
    if(c == -1)break;
    switch(c){
      case 0:
        fprintf(stderr, ERROR"option %s has no short version for some reason\n", longOptionRom[optionIndex].name);
        break;
      case '?':
        exit(1);
      case 'i':
        config_path = optarg;
        break;
      case 's':
        script_path = optarg;
        break;
      case 'o':
        output_path = optarg;
        break;

      case 'h':
        printf(
          "Usage: sprinkler [options]\n"
          "\n"
          "Options:\n"
          "  -i, --config <path>   Path to config file tsv\n"
          "  -s, --scripts <path>  Path to scripts directory\n"
          "  -o, --output <path>   Path to output www directory\n"
          "  -h, --help            Output usage information\n"
          // "  -V, --version       output the version number\n"
        );
        exit(0);
      default:
        fprintf(stderr, ERROR"option -%c is somewhat unexpected\n", c);
        break;
    }
  }

  sprinkle(config_path, script_path, output_path);
  return 0;
}
