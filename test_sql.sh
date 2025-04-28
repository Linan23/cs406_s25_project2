#!/bin/bash
set -e

BASE="http://localhost:8888/sql.cgi?"
urlencode() {
  echo "$1" | sed \
    -e 's/ /%20/g'    \
    -e 's/|/%7C/g'    \
    -e 's/=/%3D/g'    \
    -e 's/,/%2C/g'    \
    -e 's/(/%28/g'    \
    -e 's/)/%29/g'
}

check() {
  expect="$1"; shift
  qs=$(urlencode "$*")
  resp=$(curl -s "${BASE}${qs}")
  echo "$resp" | grep -q "$expect" \
    && echo "PASS: $*" \
    || { echo "FAIL: $*"; echo "$resp"; exit 1; }
}

# 1) CREATE
check "Created table <b>foo</b>"       "CREATE TABLE foo(id:smallint,title:char(10))"
check "already exists"                "CREATE TABLE foo(id:smallint,title:char(10))"

# 2) INSERT
check "Inserted into <b>foo</b>"      "INSERT INTO foo VALUES(1,Hello)"
check "does not exist"                "INSERT INTO bar VALUES(1,X)"


# 3) SELECT
check "<th>id</th><th>title</th>"     "SELECT id,title FROM foo WHERE id<5"
check "does not exist"                "SELECT id FROM baz WHERE id<5"

# 4) UPDATE
check "Update done on <b>foo</b>"     "UPDATE foo SET title=World WHERE id=1"
check "<td>World</td>"                "SELECT id,title FROM foo WHERE id=1"


# 5) DELETE
check "Deleted matching rows"         "DELETE FROM foo WHERE id=1"

# verify delete removed rows by counting <tr>:
resp=$(curl -s "${BASE}$(urlencode "SELECT id FROM foo WHERE id<5")")
if echo "$resp" | grep -q "<tr><td>"; then
  echo "FAIL: delete didn’t remove rows"
  exit 1
else
  echo "PASS: delete removed rows"
fi

echo "ALL TESTS PASSED"
