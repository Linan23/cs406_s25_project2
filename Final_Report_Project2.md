# 1. Description of Implemented Software:

This is a mini CGI-based SQL-like system server, paving path for a concurrent sql server
It supports CREATE, INSERT, SELECT, UPDATE, and DELETE operations 
using a flat file format. Table schemas are stored in schema.db 
and each table has a .data file with fixed-size records.
Each block is 256 bytes and uses chaining to link blocks when full and will allocate
a new block once a previous block is full

# 2. Algorithms used and their implementation:

## Record layout + packing:
    - Each Record is fixed-length(43 bytes)
    - 4 bytes(id) int, 30 bytes(title: Char(n)), 8 bytes int(length), 1 byte for padding/alignment
    - Data is stored in 256 byte blocks as per the prompt
    ~ Hold about 5 records in each block + 4 byte for pointers to next block

## Block Chaining & Block Allocation:

- each data file is a list of blocks
- Pointers to next block(last 4 bytes) thus creating a singly linked list of blocks for each table
- -1(0xFFFF) will be set if there is no next block after the current
- Overflow handling, will allocate a new block once the current one is filled up(5 records)
- Refer to blockio.h/c for more detailed info on each function

## SQL Commands & System Dump Command:

- Implemented the basic SQL queries(CREATE, INSERT, SELECT, UPDATE, DELETE), along with
compiling with chaining & block allocation to ensure everything works according as planned
- DUMP FROM command to check information about block structure, content, and table data

## String Formatting: 

- Some fields are padded to be a fixed length to prevent unnecessary lengths
- utilized trimming of spaces, and strcmp or atoi to make sure that comparisons are functional 
and working at the best to make sure the parsed records are "Normal"
- url decoding to decode cgi queries


# 3. Verification plan and demonstration of that plan

We first started by writing test_sql.sh, a bash script that automates test queries by sending
cgi style SQL queries through http and checking its output. The script covers all the required SQL commands,
including edge cases and error handling. We also tested block overflow through inserting large amounts of records
to trigger block allocation is working properly, also made sure data was formatted correctly, deleted records cleared, and 
chained blocks worked as expectedly, all verified through looking at the data file after the tests.

# 4. Usage documentation, describing how to run the system

## To run the test script:

1. Open up terminal

2. Compiling: Make clean -> Make 

3. Start the Web Server: Use the command "./wserver -d . -p 8888" then open up another terminal

4. Run Tests: Use the Command "bash test_sql.sh" 

    (Optional: Test through a browser, eg: http://localhost:8888/sql.cgi?INSERT%20INTO%20movies%20VALUES(1,Avatar,162) )

5. Data Inspection: Use the Command "xxd movies.data" to check the block structures and contents
  or "DUMP FROM movies" 


