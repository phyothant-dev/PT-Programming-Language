#pragma once
#include <string>
#include <map>

struct HttpResponse {
  int status;
  std::string body;
  std::map<std::string, std::string> headers;
};

HttpResponse httpGet(const std::string& url);
HttpResponse httpPost(const std::string& url, const std::string& body, const std::string& contentType = "application/json");
HttpResponse httpPut(const std::string& url, const std::string& body, const std::string& contentType = "application/json");
HttpResponse httpDelete(const std::string& url);
