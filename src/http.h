#pragma once
#include <string>
#include <unordered_map>

struct HttpRequest {
  std::string method;
  std::string path;
  std::string version;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

int httpCreateServer(int port);
HttpRequest httpParseRequest(const std::string& raw);
std::string httpReadRequest(int fd);
void httpSendResponse(int fd, int status, const std::string& statusText,
                      const std::string& contentType, const std::string& body);
void httpClose(int fd);
