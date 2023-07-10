#pragma once

#pragma comment(lib, "curl")
#include <curl/curl.h>

typedef struct NeocitiesClient {
  CURL* curl;
  curl_mime* form;
  struct curl_slist* headers;
  int count_files;
} NeocitiesClient;

NeocitiesClient neocities_init();
void neocities_addfile(NeocitiesClient* neo, const char* remote_name, const char* local_filepath);
int neocities_perform(NeocitiesClient* neo);
void neocities_free(NeocitiesClient* neo);
