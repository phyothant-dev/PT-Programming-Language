#include "ptcurl.h"
#include <curl/curl.h>
#include <algorithm>

static struct CurlGlobalInit {
  CurlGlobalInit() { curl_global_init(CURL_GLOBAL_ALL); }
  ~CurlGlobalInit() { curl_global_cleanup(); }
} curlInit;

static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* body = static_cast<std::string*>(userdata);
  body->append(ptr, size * nmemb);
  return size * nmemb;
}

static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
  auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
  size_t totalSize = size * nitems;
  std::string line(buffer, totalSize);

  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line.empty()) return totalSize;

  auto colon = line.find(':');
  if (colon != std::string::npos) {
    std::string key = line.substr(0, colon);
    std::string val = line.substr(colon + 1);
    while (!val.empty() && val[0] == ' ') val.erase(0, 1);
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    (*headers)[key] = val;
  }

  return totalSize;
}


static HttpResponse doRequest(const std::string& url, const std::string& method,
                               const std::string& body = "",
                               const std::string& contentType = "") {
  HttpResponse resp{};
  CURL* curl = curl_easy_init();
  if (!curl) {
    resp.status = 0;
    resp.body = "Failed to initialize curl";
    return resp;
  }

  std::string responseBody;
  std::map<std::string, std::string> responseHeaders;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "PT-HTTP-Client/1.0");

  struct curl_slist* headers = nullptr;

  if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    if (!contentType.empty()) {
      std::string ct = "Content-Type: " + contentType;
      headers = curl_slist_append(headers, ct.c_str());
    }
  } else if (method == "PUT") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    if (!contentType.empty()) {
      std::string ct = "Content-Type: " + contentType;
      headers = curl_slist_append(headers, ct.c_str());
    }
  } else if (method == "DELETE") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  }

  if (headers) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  CURLcode res = curl_easy_perform(curl);

  if (res == CURLE_OK) {
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    resp.status = (int)statusCode;
    resp.body = responseBody;
    resp.headers = responseHeaders;
  } else {
    resp.status = 0;
    resp.body = curl_easy_strerror(res);
  }

  if (headers) curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return resp;
}

HttpResponse httpGet(const std::string& url) {
  return doRequest(url, "GET");
}

HttpResponse httpPost(const std::string& url, const std::string& body, const std::string& contentType) {
  return doRequest(url, "POST", body, contentType);
}

HttpResponse httpPut(const std::string& url, const std::string& body, const std::string& contentType) {
  return doRequest(url, "PUT", body, contentType);
}

HttpResponse httpDelete(const std::string& url) {
  return doRequest(url, "DELETE");
}
