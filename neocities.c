#include "neocities.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "util.h"

NeocitiesClient neocities_init(){
  NeocitiesClient neo = {0};

  neo.curl = curl_easy_init();
  neo.form = curl_mime_init(neo.curl);
  neo.headers = NULL;

  return neo;
}

void neocities_addfile(NeocitiesClient* neo, const char* remote_name, const char* local_filepath){
  curl_mimepart* field = curl_mime_addpart(neo->form);
  curl_mime_name(field, remote_name);
  curl_mime_filedata(field, local_filepath);
  neo->count_files++;
}

int neocities_perform(NeocitiesClient* neo){
  if(neo->count_files == 0){
    // do nothing!!
    return 0;
  }

  char* apikey = getenv("NEOCITIES_KEY");
  if(apikey == NULL){
    fprintf(stderr, ERROR"no $NEOCITIES_KEY was found\n");
    return 1;
  }
  size_t header_len = 25 + strlen(apikey);

  char* header_buf = calloc(header_len, 1);
  snprintf(header_buf, header_len, "Authorization: Bearer %s", apikey);
  neo->headers = curl_slist_append(neo->headers, header_buf);
  curl_easy_setopt(neo->curl, CURLOPT_HTTPHEADER, neo->headers);
  free(header_buf);

  curl_easy_setopt(neo->curl, CURLOPT_MIMEPOST, neo->form);
  curl_easy_setopt(neo->curl, CURLOPT_URL, "https://neocities.org/api/upload");
  curl_easy_setopt(neo->curl, CURLOPT_TIMEOUT, 100L);

  CURLcode retcode = curl_easy_perform(neo->curl);
  if(retcode){
    fprintf(stderr, ERROR"curl returned with code %s\n", curl_easy_strerror(retcode));
    return 1;
  }else{
    fprintf(stderr, INFO"uploaded %d files to neocities on %s\n", neo->count_files, getTimeString());
    return 0;
  }
}

void neocities_free(NeocitiesClient* neo){
  curl_easy_cleanup(neo->curl);
  curl_mime_free(neo->form);
  curl_slist_free_all(neo->headers);

  neo->curl = NULL;
  neo->form = NULL;
  neo->headers = NULL;
}
