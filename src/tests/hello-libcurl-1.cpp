/**
 * A very simple demonstration of using libcurl to register an ASL node.
 * Bruce MacKinnon KC1FSZ
 */
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
 
// One-time setup:
//   sudo apt install libcurl4-gnutls-dev
// Building command
//   g++ -Wall hello-libcurl-1.cpp -lcurl -o hello-libcurl-1

const char* NODE_NUMBER = "61057";
const char* PASSWORD = "xxxxx";

// TODO: Understand what the port element means.
const char *JSON_TEMPLATE = 
"{\"port\": 7777,\"data\": {\"nodes\": {\"%s\": {\"node\": \"%s\",\"passwd\": \"%s\",\"remote\": 0}}}}";

int main(int argc, const char** argv) {

  CURL *curl;
 
  CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
  if(res)
    return (int)res;
 
  curl = curl_easy_init();
  if (curl) {
    
    curl_easy_setopt(curl, CURLOPT_URL, "https://register.allstarlink.org");
 
    /* cache the CA cert bundle in memory for a week */
    curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);

    struct curl_slist *headers = 0;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    char json_data[256];
    snprintf(json_data, 256, JSON_TEMPLATE, NODE_NUMBER, NODE_NUMBER, PASSWORD);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json_data));

    // Perform the request synchronously
    res = curl_easy_perform(curl);

    /* Check for errors */
    if(res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
 
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
  }

  curl_global_cleanup();
 
  return (int)res;
}
