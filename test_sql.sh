#!/bin/bash
set -e

# base url we are using(port 15000)
BASE="http://localhost:15000/sql.cgi?"


# Function to URL encode query strings

urlencode() {
  echo "$1" | sed \
    -e 's/ /%20/g' \
    -e 's/|/%7C/g' \
    -e 's/=/%3D/g' \
    -e 's/,/%2C/g' \
    -e 's/(/%28/g' \
    -e 's/)/%29/g'
}

# function to perform a request and check for expected output

check() {
  expect="$1"; shift
  qs="$*"
  qs_encoded=$(urlencode "$qs")
  resp=$(curl -s "${BASE}${qs_encoded}")
  echo "$resp" | grep -q "$expect" \
    && echo "PASS: $qs" \
    || { echo "FAIL: $qs"; echo "  expected: $expect"; echo "  got: $resp"; exit 1; }
}

echo " Cleaning state" 
rm -f *.schema *.data # Remove all old data/schema files to reset

echo
echo " CREATE TABLE tests to ensure basic functions work"
check "Created table <b>movies</b>" "CREATE TABLE movies(id:smallint,title:char(20),length:integer)"
check "ERROR: table <b>movies</b> already exists" "CREATE TABLE movies(id:smallint,title:char(20),length:integer)"

echo
echo " INSERT tests to ensure basic functions work"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(1,Avatar,162)"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(2,Titanic,195)"

echo
echo " SELECT tests to ensure basic functions work"
check "<th>id</th><th>title</th><th>length</th>" "SELECT id,title,length FROM movies WHERE id<5"
check "<td>Avatar</td>" "SELECT id,title FROM movies WHERE id=1"
check "<td>195</td>" "SELECT id,length FROM movies WHERE id=2"

echo
echo " Some block overflow test to make sure blocks work correctly "

# Insert additional records to check for new block allocation
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(10,Movie10,100)"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(11,Movie11,101)"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(12,Movie12,102)"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(13,Movie13,103)"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(14,Movie14,104)"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(15,Movie15,105)"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(16,Movie16,106)"  # should land in new block
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(16,Movie17,107)"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(16,Movie18,108)"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(16,Movie19,109)"
check "Inserted into <b>movies</b>" "INSERT INTO movies VALUES(16,Movie20,110)"

# Verify earliest and latest insert made it
check "<td>Movie10</td>" "SELECT title FROM movies WHERE id=10"
check "<td>Movie16</td>" "SELECT title FROM movies WHERE id=16"

echo
echo " UPDATE tests to ensure basic functions work"
check "Update done on <b>movies</b>" "UPDATE movies SET title=Titanic3D WHERE id=2"
check "<td>Titanic3D</td>" "SELECT id,title FROM movies WHERE id=2"
check "Update done on <b>movies</b>" "UPDATE movies SET length=200 WHERE id=2"
check "<td>200</td>" "SELECT id,length FROM movies WHERE id=2"

echo
echo " DELETE tests to ensure basic functions work"
check "Deleted matching rows in <b>movies</b>" "DELETE FROM movies WHERE length=162"

# confirm that deletion is actually working
resp=$(curl -s "${BASE}$(urlencode "SELECT id,title,length FROM movies WHERE length=162")")
if echo "$resp" | grep -q "<td>"; then
  echo "FAIL: DELETE did not remove rows with length=162"
  exit 1
else
  echo "PASS: DELETE removed rows with length=162"
fi

echo
echo " Additional sanity checks to make sure advanced features work"

check "<td>2</td>" "SELECT id,title,length FROM movies WHERE id=2"
check "<td>2</td>" "SELECT id,title FROM movies WHERE id>1"
check "<td>2</td>" "SELECT id,title FROM movies WHERE id!=1"
check "<td>Titanic3D</td>" "SELECT title FROM movies WHERE id=2"
check "<td>200</td>" "SELECT length FROM movies WHERE id=2"

echo

# invalid CREATE TABLE tests
check "ERROR: bad CREATE syntax" "CREATE TABLE movies(id smallint,title char(20))"
check "ERROR: invalid table name" "CREATE TABLE mo@vies(id:smallint,title:char(20))"
check "ERROR: no columns specified" "CREATE TABLE blank()"

# invalid INSERT tests
check "ERROR: table <b>nosuch</b> does not exist" "INSERT INTO nosuch VALUES(1,X)"
check "ERROR: expected 3 values, got 1" "INSERT INTO movies VALUES(1Avatar)"
check "ERROR: expected 3 values, got 2" "INSERT INTO movies VALUES(,Avatar)"

# Comma/field/spacing error tests
check "ERROR: expected 3 values, got 4" "INSERT INTO movies VALUES(1,Avatar,162,999)"
check "ERROR: expected 3 values, got 1" "INSERT INTO movies VALUES(1 Avatar 162)"
check "ERROR: expected 3 values, got 4" "INSERT INTO movies VALUES(1,Avatar,162,)"
check "ERROR: expected 3 values, got 4" "INSERT INTO movies VALUES(,1,Avatar,162)"
check "ERROR: expected 3 values, got 2" "INSERT INTO movies VALUES(1,Avatar)"
check "ERROR: expected 3 values, got 1" "INSERT INTO movies VALUES(1)"
check "ERROR: bad INSERT syntax" "INSERT INTO movies VALUES()"
check "ERROR: bad INSERT syntax" "INSERT INTO movies VALUES 1 Avatar 162"
check "ERROR: expected 3 values, got 4" "INSERT INTO movies VALUES(1,,Avatar,162)"
check "ERROR: bad INSERT values" "INSERT INTO movies VALUES('1','Avatar','162')"

# invalid SELECT tests
check "ERROR: bad SELECT syntax" "SELECT id title FROM movies WHERE id<5"
check "ERROR: bad SELECT syntax" "SELECT id,title movies WHERE id<5"
check "ERROR: bad SELECT syntax" "SELECT id,title FROM movies id<5"
check "ERROR: table <b>nosuch</b> does not exist" "SELECT id FROM nosuch WHERE id<5"

# invalid UPDATE tests
check "ERROR: bad UPDATE syntax" "UPDATE movies title=X WHERE id=1"
check "ERROR: table <b>nosuch</b> does not exist" "UPDATE nosuch SET title=Y WHERE id=1"
check "ERROR: bad UPDATE syntax" "UPDATE movies SET title Titanic WHERE id=1"

# invalid DELETE tests

check "Deleted matching rows in <b>movies</b>" "DELETE FROM movies WHERE id=2"
resp=$(curl -s "${BASE}$(urlencode "SELECT id FROM movies WHERE id=2")")
if echo "$resp" | grep -q "<td>"; then
  echo "FAIL: DELETE did not remove movie with id=2"
  exit 1
else
  echo "PASS: DELETE removed movie with id=2"
fi

echo
echo " Checking System Dump"
resp=$(curl -s "${BASE}$(urlencode "DUMP FROM movies")")
echo "$resp"


echo
echo " ALL TESTS PASSED!!!!!!!!! "
