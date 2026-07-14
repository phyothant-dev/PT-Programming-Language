#pragma once
#include <string>
#include <vector>
#include <map>

struct PgResult {
  bool ok;
  std::string error;
  std::vector<std::map<std::string, std::string>> rows;
  int affectedRows;
};

void* pgOpen(const std::string& connStr);
PgResult pgExec(void* conn, const std::string& sql);
PgResult pgQuery(void* conn, const std::string& sql);
bool pgClose(void* conn);
