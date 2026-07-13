#include "http.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <algorithm>

int httpCreateServer(int port) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) return -1;

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(server_fd);
    return -1;
  }

  if (listen(server_fd, 128) < 0) {
    close(server_fd);
    return -1;
  }

  return server_fd;
}

static std::string toLower(const std::string& s) {
  std::string r = s;
  std::transform(r.begin(), r.end(), r.begin(), ::tolower);
  return r;
}

HttpRequest httpParseRequest(const std::string& raw) {
  HttpRequest req;
  std::istringstream stream(raw);
  std::string line;

  if (!std::getline(stream, line)) return req;
  if (!line.empty() && line.back() == '\r') line.pop_back();

  std::istringstream reqLine(line);
  reqLine >> req.method >> req.path >> req.version;

  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) break;

    auto colon = line.find(':');
    if (colon != std::string::npos) {
      std::string key = line.substr(0, colon);
      std::string val = line.substr(colon + 1);
      while (!val.empty() && val[0] == ' ') val.erase(0, 1);
      req.headers[toLower(key)] = val;
    }
  }

  std::string remaining;
  while (std::getline(stream, line)) {
    if (!remaining.empty()) remaining += "\n";
    remaining += line;
  }
  req.body = remaining;

  return req;
}

std::string httpReadRequest(int fd) {
  std::string raw;
  char buf[4096];
  int totalRead = 0;

  while (totalRead < 65536) {
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) break;
    buf[n] = '\0';
    raw.append(buf, n);
    totalRead += n;

    if (raw.find("\r\n\r\n") != std::string::npos) break;
  }

  auto hdrEnd = raw.find("\r\n\r\n");
  if (hdrEnd == std::string::npos) return raw;

  std::string headers = raw.substr(0, hdrEnd);
  std::string body = raw.substr(hdrEnd + 4);

  std::istringstream hStream(headers);
  std::string line;
  int contentLength = 0;
  while (std::getline(hStream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto colon = line.find(':');
    if (colon != std::string::npos) {
      std::string key = toLower(line.substr(0, colon));
      std::string val = line.substr(colon + 1);
      while (!val.empty() && val[0] == ' ') val.erase(0, 1);
      if (key == "content-length") contentLength = std::stoi(val);
    }
  }

  while ((int)body.size() < contentLength) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 0) break;
    body.append(buf, n);
  }

  return raw.substr(0, hdrEnd + 4) + body;
}

void httpSendResponse(int fd, int status, const std::string& statusText,
                      const std::string& contentType, const std::string& body) {
  std::string resp;
  resp += "HTTP/1.1 " + std::to_string(status) + " " + statusText + "\r\n";
  resp += "Content-Type: " + contentType + "\r\n";
  resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  resp += "Access-Control-Allow-Origin: *\r\n";
  resp += "Connection: close\r\n";
  resp += "\r\n";
  resp += body;

  const char* ptr = resp.c_str();
  int remaining = (int)resp.size();
  while (remaining > 0) {
    ssize_t n = write(fd, ptr, remaining);
    if (n <= 0) break;
    ptr += n;
    remaining -= n;
  }
}

void httpClose(int fd) { close(fd); }
