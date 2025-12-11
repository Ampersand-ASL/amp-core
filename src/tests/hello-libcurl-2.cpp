/**
 * A very simple demonstration of using libcurl ASYNCHRONOUSLY to register 
 * an ASL node.
 *
  * Bruce MacKinnon KC1FSZ
 */
#include <cstdio>
#include <cstring>
#include <iostream>
#include <curl/curl.h>

using namespace std;

// Good:
//
// {"ipaddr":"108.20.174.63","port":4569,"refresh":179,"data":["61057 successfully registered @108.20.174.63:4569."]}
//
// Bad:
//
// {"ipaddr":"108.20.174.63","port":4569,"refresh":179,"data":["61057 failed authentication. Please check your password and node number."]}
//
// NOTE: Both return HTTP code of 200

// One-time setup:
//   sudo apt install libcurl4-gnutls-dev
// Building command
//   g++ -Wall hello-libcurl-2.cpp -lcurl -o hello-libcurl-2

const char* NODE_NUMBER = "61057";
const char* PASSWORD = "xxxxxx";

// TODO: Understand what the port element means.
const char *JSON_TEMPLATE = 
"{\"port\": 7777,\"data\": {\"nodes\": {\"%s\": {\"node\": \"%s\",\"passwd\": \"%s\",\"remote\": 0}}}}";

// Callback function to handle received data
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    printf("%.*s\n", (int)realsize, (char*)contents); 
    return realsize;
}

int main(int argc, const char** argv) {

  printf("libcurl version: %s\n", curl_version());

  CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
  if (res)
    return (int)res;

  CURLM *multi_handle = curl_multi_init();
  CURL *curl = curl_easy_init();

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_multi_add_handle(multi_handle, curl);

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

  // Perform transfers asynchronously
  int still_running = 1;
  unsigned cycleCount = 0;

  do {
    //timespec t1;
    //clock_gettime(CLOCK_MONOTONIC, &t1);
    //cout << "A " << t1.tv_sec << "," << t1.tv_nsec / 1000 << endl;

    CURLMcode mc = curl_multi_perform(multi_handle, &still_running);

    if (mc == CURLM_OK && still_running) {
      // Wait for activity on any of the easy handles
      
      // The built-in way:
      mc = curl_multi_wait(multi_handle, NULL, 0, 1000, NULL);
      
      // Or we could do the select() way:
      fd_set read_fds, write_fds, exc_fds;
      int max_fd = -1;
      FD_ZERO(&read_fds);
      FD_ZERO(&write_fds);
      FD_ZERO(&exc_fds);
      mc = curl_multi_fdset(multi_handle, &read_fds, &write_fds, &exc_fds, &max_fd);
      // And then select on the the FDs that we got ...
    }
    if (mc != CURLM_OK) {
        fprintf(stderr, "curl_multi_perform or curl_multi_wait failed: %s\n", 
          curl_multi_strerror(mc));
        break;
    }
    cycleCount++;
  } while (still_running);
 
  // This is important: get the HTTP result
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  printf("HTTP code %ld\n", http_code);

  printf("Cycle count %u\n", cycleCount);

  curl_multi_remove_handle(multi_handle, curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  curl_multi_cleanup(multi_handle);
  curl_global_cleanup();
}
