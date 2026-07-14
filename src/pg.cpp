#include "pg.h"
#include <libpq-fe.h>

void* pgOpen(const std::string& connStr) {
  PGconn* conn = PQconnectdb(connStr.c_str());
  if (PQstatus(conn) != CONNECTION_OK) {
    std::string err = PQerrorMessage(conn);
    PQfinish(conn);
    throw std::runtime_error("PostgreSQL connection failed: " + err);
  }
  return conn;
}

static PgResult pgResult(PGresult* res) {
  PgResult r{};
  if (!res) { r.ok = false; r.error = "Null result"; return r; }
  ExecStatusType status = PQresultStatus(res);
  if (status == PGRES_COMMAND_OK) {
    r.ok = true;
    r.affectedRows = std::atoi(PQcmdTuples(res));
    return r;
  }
  if (status != PGRES_TUPLES_OK) {
    r.ok = false;
    r.error = PQresultErrorMessage(res);
    return r;
  }
  r.ok = true;
  int rows = PQntuples(res);
  int cols = PQnfields(res);
  r.rows.reserve(rows);
  for (int i = 0; i < rows; i++) {
    std::map<std::string, std::string> row;
    for (int j = 0; j < cols; j++) {
      std::string colName = PQfname(res, j);
      if (PQgetisnull(res, i, j))
        row[colName] = "";
      else
        row[colName] = PQgetvalue(res, i, j);
    }
    r.rows.push_back(row);
  }
  return r;
}

PgResult pgExec(void* conn, const std::string& sql) {
  PGresult* res = PQexec((PGconn*)conn, sql.c_str());
  PgResult r = pgResult(res);
  PQclear(res);
  return r;
}

PgResult pgQuery(void* conn, const std::string& sql) {
  PGresult* res = PQexec((PGconn*)conn, sql.c_str());
  PgResult r = pgResult(res);
  PQclear(res);
  return r;
}

bool pgClose(void* conn) {
  if (conn) { PQfinish((PGconn*)conn); return true; }
  return false;
}
