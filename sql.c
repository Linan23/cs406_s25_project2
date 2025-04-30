#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "io_helper.h"
#include "blockio.h"
#include <ctype.h>

#define SCHEMA_FILE "schema.db"
#define MAXQS 8192
#define MAXSQL 1024
#define MAXTOK 256

// this is bc 4 (id) + 30 (title as char(n)) + 8 (length) + 1 (padding)
// roughly 256/43 ~ 5 entries per block
#define RECORD_SIZE 43

// functions that is used for sql commands
void handle_create(char *qs);
void handle_insert(char *qs);
void handle_select(char *qs);
void handle_update(char *qs);
void handle_delete(char *qs);
void handle_dump(char *qs);

/*
Loads a table schema from schema.db
This will return true if the file is found, else false
*/

int load_schema_from_file(const char *tbl, char *out, size_t outsize)
{
    FILE *fp = fopen(SCHEMA_FILE, "r");
    if (!fp)
        return 0;

    char line[512];
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, tbl, strlen(tbl)) == 0 && line[strlen(tbl)] == '|')
        {
            char *schema_part = strchr(line, '|');
            if (schema_part)
            {
                strncpy(out, schema_part + 1, outsize);
                char *semi = strchr(out, ';');
                if (semi)
                    *semi = '\0'; // chop off semicolon
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0; // not found
}

// decodes a url encoded string
void url_decode(char *dst, const char *src)
{
    while (*src)
    {
        if (*src == '%' &&
            isxdigit((unsigned char)src[1]) &&
            isxdigit((unsigned char)src[2]))
        {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        }
        else if (*src == '+')
        {
            *dst++ = ' ';
            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

int main()
{
    // grab the raw QUERY_STRING
    char *raw_qs = getenv("QUERY_STRING");

    // CGI header
    printf("Content-Type: text/html\r\n\r\n");

    if (!raw_qs)
    {
        printf("<p>ERROR: no query string provided</p>\n");
        return 1;
    }

    // decode it into buffer
    char qs_buf[MAXQS];
    url_decode(qs_buf, raw_qs);
    char *qs = qs_buf;

    // decoded commands, should turn case insensitive using strncasecmp
    if (strncasecmp(qs, "CREATE TABLE ", 13) == 0)
    {
        handle_create(qs);
    }
    else if (strncasecmp(qs, "INSERT INTO ", 12) == 0)
    {
        handle_insert(qs);
    }
    else if (strncasecmp(qs, "SELECT ", 7) == 0)
    {
        handle_select(qs);
    }
    else if (strncasecmp(qs, "UPDATE ", 7) == 0)
    {
        handle_update(qs);
    }
    else if (strncasecmp(qs, "DELETE FROM", 11) == 0)
    {
        handle_delete(qs);
    }
    else if (strncasecmp(qs, "DUMP FROM ", 10) == 0)
    {
        handle_dump(qs);
    }
    else
    {
        printf("<p>ERROR: unknown command</p>\n");
    }

    return 0;
}

// CREATE TABLE

void handle_create(char *qs)
{
    char tbl[64], cols[512];

    if (sscanf(qs, "CREATE TABLE %63[^ (]", tbl) != 1)
    {
        printf("<p>ERROR: bad CREATE syntax</p>\n");
        return;
    }

    char *expected = strstr(qs, "CREATE TABLE ") + strlen("CREATE TABLE ");
    if (expected[strlen(tbl)] != '(')
    {
        printf("<p>ERROR: bad CREATE syntax</p>\n");
        return;
    }

    char *p = strchr(qs, '(');
    char *q = strrchr(qs, ')');
    if (!p || !q || p > q)
    {
        printf("<p>ERROR: bad CREATE syntax</p>\n");
        return;
    }

    size_t len = q - (p + 1);
    if (len >= sizeof(cols))
    {
        printf("<p>ERROR: column list too long</p>\n");
        return;
    }
    memcpy(cols, p + 1, len);
    cols[len] = '\0';

    char *tmp = cols;
    while (*tmp && isspace((unsigned char)*tmp))
        tmp++;
    if (*tmp == '\0')
    {
        printf("<p>ERROR: no columns specified</p>\n");
        return;
    }

    for (char *p = tbl; *p; p++)
    {
        if (!isalnum((unsigned char)*p) && *p != '_')
        {
            printf("<p>ERROR: invalid table name</p>\n");
            return;
        }
    }

    char cols_copy[512];
    strncpy(cols_copy, cols, sizeof(cols_copy));
    cols_copy[511] = '\0';

    char *col = strtok(cols_copy, ",");
    while (col)
    {
        char column_def[128];
        strncpy(column_def, col, sizeof(column_def));
        column_def[127] = '\0';

        char *ptr = column_def;
        while (*ptr && isspace((unsigned char)*ptr))
            ptr++;

        char *colon = strchr(ptr, ':');
        if (!colon)
        {
            printf("<p>ERROR: bad CREATE syntax</p>\n");
            return;
        }

        *colon = '\0';
        char *name = ptr;
        char *type = colon + 1;

        for (char *p = name; *p; p++)
        {
            if (!isalnum((unsigned char)*p) && *p != '_')
            {
                printf("<p>ERROR: invalid column name</p>\n");
                return;
            }
        }

        if (strcmp(type, "smallint") == 0 || strcmp(type, "integer") == 0)
        {
            // ok
        }
        else if (strncmp(type, "char(", 5) == 0)
        {
            char *start = type + 5;
            char *end = strchr(start, ')');
            if (!end)
            {
                printf("<p>ERROR: bad type format in char(n)</p>\n");
                return;
            }
            for (char *d = start; d < end; d++)
            {
                if (!isdigit((unsigned char)*d))
                {
                    printf("<p>ERROR: bad char(n) format</p>\n");
                    return;
                }
            }
        }
        else
        {
            printf("<p>ERROR: unsupported type '%s'</p>\n", type);
            return;
        }

        col = strtok(NULL, ",");
    }

    FILE *fp = fopen(SCHEMA_FILE, "r");
    if (fp)
    {
        char line[512];
        while (fgets(line, sizeof(line), fp))
        {
            if (strncmp(line, tbl, strlen(tbl)) == 0 && line[strlen(tbl)] == '|')
            {
                printf("<p>ERROR: table <b>%s</b> already exists</p>\n", tbl);
                fclose(fp);
                return;
            }
        }
        fclose(fp);
    }

    FILE *out = fopen(SCHEMA_FILE, "a");
    if (!out)
    {
        printf("<p>ERROR: could not open schema file</p>\n");
        return;
    }
    fprintf(out, "%s|%s;\n", tbl, cols);
    fclose(out);

    printf("<p>Created table <b>%s</b></p>\n", tbl);

    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);

    // fix: use block 0 for head, block 1 for first data
    int head = alloc_block(datafile);  // block 0
    int first = alloc_block(datafile); // block 1
    set_next_block(datafile, head, first);
    set_next_block(datafile, first, -1);
}

// Insert
/*
Insert sql command
first confirm table and schema exist, check value count matches schema,
then formats and inserts the record into a free slot in a data block,
if block is full, make a new block
*/

// Insert into table
void handle_insert(char *qs)
{
    char tbl[64], vals[512];

    if (sscanf(qs, "INSERT INTO %63[^ ] VALUES(%511[^)])", tbl, vals) != 2)
    {
        printf("<p>ERROR: bad INSERT syntax</p>\n");
        return;
    }

    char schema[512];
    if (!load_schema_from_file(tbl, schema, sizeof(schema)))
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    struct stat st;
    if (stat(datafile, &st) < 0)
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // Count expected fields
    int expected_fields = 1;
    for (char *p = schema; *p; p++)
    {
        if (*p == ',')
            expected_fields++;
    }

    int provided_fields = 1;
    for (char *p = vals; *p; p++)
    {
        if (*p == ',')
            provided_fields++;
    }

    if (provided_fields != expected_fields)
    {
        printf("<p>ERROR: expected %d values, got %d</p>\n", expected_fields, provided_fields);
        return;
    }

    int id, length;
    char title[128];
    if (sscanf(vals, "%d,%127[^,],%d", &id, title, &length) != 3)
    {
        printf("<p>ERROR: bad INSERT values</p>\n");
        return;
    }

    // Format record
    char record[RECORD_SIZE] = {0};

    char idstr[5];
    snprintf(idstr, sizeof(idstr), "%04d", id);
    memcpy(record, idstr, 4);

    char titlestr[30];
    memset(titlestr, ' ', sizeof(titlestr));
    strncpy(titlestr, title, sizeof(titlestr));
    memcpy(record + 4, titlestr, 30);

    char lengthstr[9];
    snprintf(lengthstr, sizeof(lengthstr), "%08d", length);
    memcpy(record + 34, lengthstr, 8);

    // Follow chaining starting from block 0
    int b = 0;
    while (1)
    {
        char buf[BLOCK_SIZE];
        read_block(datafile, b, buf);

        // Try to find empty spot
        for (int off = 0; off + RECORD_SIZE <= BLOCK_SIZE - 4; off += RECORD_SIZE)
        {
            if (buf[off] == '\0')
            {
                memcpy(buf + off, record, RECORD_SIZE);
                write_block(datafile, b, buf);
                printf("<p>Inserted into <b>%s</b></p>\n", tbl);
                return;
            }
        }

        // No space in this block, check next
        int next = get_next_block(datafile, b);
        if (next == -1)
        {
            // No next block, need to allocate
            int newb = alloc_block(datafile);
            set_next_block(datafile, b, newb);

            char newbuf[BLOCK_SIZE] = {0};
            memcpy(newbuf, record, RECORD_SIZE);
            write_block(datafile, newb, newbuf);
            set_next_block(datafile, newb, -1);

            printf("<p>Inserted into <b>%s</b></p>\n", tbl);
            return;
        }

        b = next; // Follow to next block
    }
}

// SELECT
/*
Will do SELECT WHERE sql command
validates requested columns, parses conditions,
and displays matching records in HTML
*/

void handle_select(char *qs)
{
    // buffers
    char cols[128], tbl[64], cond[128];
    char header_cols[128], data_cols[128];

    // query strings
    if (sscanf(qs, "SELECT %127[^ ] FROM %63[^ ] WHERE %127[^\r\n]", cols, tbl, cond) != 3)
    {
        printf("<p>ERROR: bad SELECT syntax</p>\n");
        return;
    }

    // If SELECT * , default to all columns
    if (strcmp(cols, "*") == 0)
    {
        strcpy(cols, "id,title,length");
    }

    // backup the column list for header and data output
    strncpy(header_cols, cols, sizeof(header_cols));
    strncpy(data_cols, cols, sizeof(data_cols));

    // validate table name (only allow alphabets and '_')
    for (char *p = tbl; *p; p++)
    {
        if (!isalnum((unsigned char)*p) && *p != '_')
        {
            printf("<p>ERROR: invalid table name</p>\n");
            return;
        }
    }

    // load schema for table
    char schema[512];
    if (!load_schema_from_file(tbl, schema, sizeof(schema)))
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // check datafile
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    struct stat st;
    if (stat(datafile, &st) < 0)
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // parse and store valid field names from schema into array
    char schema_copy[512];
    strncpy(schema_copy, schema, sizeof(schema_copy));
    char *valid_fields[16];
    int fcount = 0;
    char *tok = strtok(schema_copy, ",");
    while (tok && fcount < 16)
    {
        char *colon = strchr(tok, ':');
        if (colon)
            *colon = '\0';
        valid_fields[fcount++] = tok;
        tok = strtok(NULL, ",");
    }

    // validate that requested columns exist in schema
    char sel_copy[128];
    strncpy(sel_copy, cols, sizeof(sel_copy));
    char *col = strtok(sel_copy, ",");
    while (col)
    {
        while (*col == ' ')
            col++;
        int found = 0;
        for (int i = 0; i < fcount; i++)
        {
            if (strcmp(col, valid_fields[i]) == 0)
            {
                found = 1;
                break;
            }
        }
        if (!found)
        {
            printf("<p>ERROR: unknown column '%s'</p>\n", col);
            return;
        }
        col = strtok(NULL, ",");
    }

    // html output for table
    printf("<table><tr>");
    col = strtok(header_cols, ",");
    while (col)
    {
        while (*col == ' ')
            col++;
        printf("<th>%s</th>", col);
        col = strtok(NULL, ",");
    }
    printf("</tr>\n");

    // detect and parse WHERE operator and operands
    char *op = NULL;
    if (strstr(cond, "!="))
        op = "!=";
    else if (strstr(cond, "<"))
        op = "<";
    else if (strstr(cond, ">"))
        op = ">";
    else if (strstr(cond, "="))
        op = "=";
    else
    {
        printf("<p>ERROR: unknown operator</p>\n");
        return;
    }

    // parse WHERE condition(field and value)
    char field[64], value[64];
    if (strcmp(op, "!=") == 0)
        sscanf(cond, "%63[^!]!=%63s", field, value);
    else
        sscanf(cond, "%63[^<>=]%*c%63s", field, value);
    int where_target = atoi(value); // assume numeric comparison

    // iterate through all blocks(start from block 0)
    int b = 0;
    while (b != -1)
    {
        char buf[BLOCK_SIZE];
        read_block(datafile, b, buf);

        for (int offset = 0; offset + RECORD_SIZE <= BLOCK_SIZE - 4; offset += RECORD_SIZE)
        {
            char idstr[5] = {0};
            strncpy(idstr, buf + offset, 4);
            if (idstr[0] == '\0')
                continue;

            char titlestr[31] = {0};
            char lengthstr[9] = {0};
            strncpy(titlestr, buf + offset + 4, 30);
            strncpy(lengthstr, buf + offset + 34, 8);

            int id = atoi(idstr);
            int length = atoi(lengthstr);

            int match = 0;
            if (strcmp(field, "id") == 0)
            {
                if (!strcmp(op, "="))
                    match = (id == where_target);
                else if (!strcmp(op, "<"))
                    match = (id < where_target);
                else if (!strcmp(op, ">"))
                    match = (id > where_target);
                else if (!strcmp(op, "!="))
                    match = (id != where_target);
            }
            else if (strcmp(field, "length") == 0)
            {
                if (!strcmp(op, "="))
                    match = (length == where_target);
                else if (!strcmp(op, "<"))
                    match = (length < where_target);
                else if (!strcmp(op, ">"))
                    match = (length > where_target);
                else if (!strcmp(op, "!="))
                    match = (length != where_target);
            }

            if (!match)
                continue;

            // trim spaces from title
            for (int i = strlen(titlestr) - 1; i >= 0; i--)
            {
                if (titlestr[i] == ' ')
                    titlestr[i] = '\0';
                else
                    break;
            }

            // print matching row
            printf("<tr>");
            char data_cols_copy[128];
            strncpy(data_cols_copy, data_cols, sizeof(data_cols_copy));
            char *col = strtok(data_cols_copy, ",");
            while (col)
            {
                while (*col == ' ')
                    col++;
                if (!strcmp(col, "id"))
                    printf("<td>%d</td>", id);
                else if (!strcmp(col, "title"))
                    printf("<td>%s</td>", titlestr);
                else if (!strcmp(col, "length"))
                    printf("<td>%d</td>", length);
                col = strtok(NULL, ",");
            }
            printf("</tr>\n");
        }

        // move to next block in the chain
        b = get_next_block(datafile, b);
    }
    printf("</table>\n");
}

// UPDATE

/*
Handles an UPDATE SET  WHERE SQL Command
will parse condition, validates column, and updates field values in-place-
if records match the WHERE clause
*/
void handle_update(char *qs)
{
    // Parsing query string into table name, SET condition, and WHERE condition
    char tbl[64], setp[128], cond[128];
    if (sscanf(qs, "UPDATE %63s SET %127[^ ] WHERE %127[^\r\n]", tbl, setp, cond) != 3)
    {
        printf("<p>ERROR: bad UPDATE syntax</p>\n");
        return;
    }

    // Parse the SET condition into the field name and new value
    char field[64], newval[128];
    if (sscanf(setp, "%63[^=]=%127s", field, newval) != 2)
    {
        printf("<p>ERROR: bad SET syntax</p>\n");
        return;
    }

    // load the table schema to validate if fields exists
    char schema[512];
    if (!load_schema_from_file(tbl, schema, sizeof(schema)))
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // copy schema for parsing valid fields
    char schema_copy[512];
    strncpy(schema_copy, schema, sizeof(schema_copy));
    char *valid_fields[16];
    int fcount = 0;
    char *tok = strtok(schema_copy, ",");
    while (tok && fcount < 16)
    {
        char *colon = strchr(tok, ':');
        if (colon)
            *colon = '\0';
        valid_fields[fcount++] = tok;
        tok = strtok(NULL, ",");
    }

    // Validate that the field exists in schema

    int valid = 0;
    for (int i = 0; i < fcount; i++)
    {
        if (strcmp(field, valid_fields[i]) == 0)
        {
            valid = 1;
            break;
        }
    }
    if (!valid)
    {
        printf("<p>ERROR: unknown column '%s'</p>\n", field);
        return;
    }

    // makes sure the tables data file exists
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    struct stat st;
    if (stat(datafile, &st) < 0)
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // WHERE condition to extract field and target value

    char *op = NULL;
    if (strstr(cond, "!="))
        op = "!=";
    else if (strstr(cond, "<"))
        op = "<";
    else if (strstr(cond, ">"))
        op = ">";
    else if (strstr(cond, "="))
        op = "=";
    else
    {
        printf("<p>ERROR: unknown operator</p>\n");
        return;
    }

    char where_field[64], where_value[64];
    if (strcmp(op, "!=") == 0)
        sscanf(cond, "%63[^!]!=%63s", where_field, where_value);
    else
        sscanf(cond, "%63[^<>=]%*c%63s", where_field, where_value);

    int where_target = atoi(where_value);

    // iterate through all blocks(start from block 0)
    int b = 0;
    while (b != -1)
    {
        char buf[BLOCK_SIZE];
        read_block(datafile, b, buf);
        int dirty = 0;

        for (int offset = 0; offset + RECORD_SIZE <= BLOCK_SIZE - 4; offset += RECORD_SIZE)
        {
            char idstr[5] = {0};
            strncpy(idstr, buf + offset, 4);
            if (idstr[0] == '\0')
                continue;

            char titlestr[31] = {0};
            char lengthstr[9] = {0};
            strncpy(titlestr, buf + offset + 4, 30);
            strncpy(lengthstr, buf + offset + 34, 8);

            int id = atoi(idstr);
            int length = atoi(lengthstr);

            int match = 0;
            if (strcmp(where_field, "id") == 0)
            {
                if (!strcmp(op, "="))
                    match = (id == where_target);
                else if (!strcmp(op, "<"))
                    match = (id < where_target);
                else if (!strcmp(op, ">"))
                    match = (id > where_target);
                else if (!strcmp(op, "!="))
                    match = (id != where_target);
            }
            else if (strcmp(where_field, "length") == 0)
            {
                if (!strcmp(op, "="))
                    match = (length == where_target);
                else if (!strcmp(op, "<"))
                    match = (length < where_target);
                else if (!strcmp(op, ">"))
                    match = (length > where_target);
                else if (!strcmp(op, "!="))
                    match = (length != where_target);
            }

            if (!match)
                continue;

            if (strcmp(field, "title") == 0)
            {
                char padded[31];
                memset(padded, ' ', 30);
                padded[30] = '\0';
                strncpy(padded, newval, strlen(newval));
                memcpy(buf + offset + 4, padded, 30);
                dirty = 1;
            }
            else if (strcmp(field, "length") == 0)
            {
                char len_update[9];
                snprintf(len_update, sizeof(len_update), "%08d", atoi(newval));
                memcpy(buf + offset + 34, len_update, 8);
                dirty = 1;
            }
            else
            {
                printf("<p>ERROR: unknown column %s</p>\n", field);
                return;
            }
        }

        if (dirty)
            write_block(datafile, b, buf);

        b = get_next_block(datafile, b); // move to next block
    }

    printf("<p>Update done on <b>%s</b></p>\n", tbl);
}

// DELETE
/*
Do DELETE FROM  WHERE  command
Finds and zero out records matching the condition in data blocks
*/
void handle_delete(char *qs)
{
    char tbl[64], cond[128];

    // parse query string
    if (sscanf(qs, "DELETE FROM %63s WHERE %127[^\r\n]", tbl, cond) != 2)
    {
        printf("<p>ERROR: bad DELETE syntax</p>\n");
        return;
    }

    // check table exist in schema
    char schema[512];
    if (!load_schema_from_file(tbl, schema, sizeof(schema)))
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // check for data file
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    struct stat st;
    if (stat(datafile, &st) < 0)
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // WHERE condition
    char *op = NULL;
    if (strstr(cond, "!="))
        op = "!=";
    else if (strstr(cond, "<"))
        op = "<";
    else if (strstr(cond, ">"))
        op = ">";
    else if (strstr(cond, "="))
        op = "=";
    else
    {
        printf("<p>ERROR: unknown operator</p>\n");
        return;
    }

    // Extract field and value from WHERE conditon

    char where_field[64], where_value[64];
    if (strcmp(op, "!=") == 0)
        sscanf(cond, "%63[^!]!=%63s", where_field, where_value);
    else
        sscanf(cond, "%63[^<>=]%*c%63s", where_field, where_value);

    int where_target = atoi(where_value); // convert string to integer for comparison

    // iterate through all data blocks in the table file (start from block 0)
    int b = 0;
    while (b != -1)
    {
        char buf[BLOCK_SIZE];
        int dirty = 0;
        read_block(datafile, b, buf);

        for (int off = 0; off + RECORD_SIZE <= BLOCK_SIZE - 4; off += RECORD_SIZE)
        {
            char idstr[5] = {0};
            strncpy(idstr, buf + off, 4);
            if (idstr[0] == '\0')
                continue;

            char lengthstr[9] = {0};
            strncpy(lengthstr, buf + off + 34, 8);

            int id = atoi(idstr);
            int length = atoi(lengthstr);

            int match = 0;
            if (strcmp(where_field, "id") == 0)
            {
                if (!strcmp(op, "="))
                    match = (id == where_target);
                else if (!strcmp(op, "<"))
                    match = (id < where_target);
                else if (!strcmp(op, ">"))
                    match = (id > where_target);
                else if (!strcmp(op, "!="))
                    match = (id != where_target);
            }
            else if (strcmp(where_field, "length") == 0)
            {
                if (!strcmp(op, "="))
                    match = (length == where_target);
                else if (!strcmp(op, "<"))
                    match = (length < where_target);
                else if (!strcmp(op, ">"))
                    match = (length > where_target);
                else if (!strcmp(op, "!="))
                    match = (length != where_target);
            }

            if (match)
            {
                memset(buf + off, 0, RECORD_SIZE);
                dirty = 1;
            }
        }

        if (dirty)
            write_block(datafile, b, buf);

        b = get_next_block(datafile, b); // follow the chain
    }

    printf("<p>Deleted matching rows in <b>%s</b></p>\n", tbl);
}

/*
DUMP FROM command
Will output the table structure and block contents
*/
void handle_dump(char *qs)
{   

    // parse command

    char tbl[64];
    if (sscanf(qs, "DUMP FROM %63s", tbl) != 1)
    {
        printf("<p>ERROR: bad DUMP syntax</p>\n");
        return;
    }

    // load schema
    char schema[512];
    if (!load_schema_from_file(tbl, schema, sizeof(schema)))
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // header infos
    printf("<h2>System Dump for Table: <b>%s</b></h2>\n", tbl);
    printf("<pre>\n");
    printf("Schema: %s\n", schema);

    // check if data exists
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);

    struct stat st;
    if (stat(datafile, &st) < 0)
    {
        printf("ERROR: could not stat data file\n</pre>");
        return;
    }

    // walk the block chain through starting at block 0 
    int b = 0;
    while (b != -1)
    {
        char buf[BLOCK_SIZE];
        read_block(datafile, b, buf);
        printf("Block #%d:\n", b);

        // record the record in the block
        for (int off = 0; off + RECORD_SIZE <= BLOCK_SIZE - 4; off += RECORD_SIZE)
        {
            // empty slot check
            if (buf[off] == '\0')
            {
                printf("  [empty slot]\n");
                continue;
            }

            // buffers for field strings
            char idstr[5] = {0};
            char titlestr[31] = {0};
            char lengthstr[9] = {0};

            // copy from buffer
            strncpy(idstr, buf + off, 4);
            strncpy(titlestr, buf + off + 4, 30);
            strncpy(lengthstr, buf + off + 34, 8);

            // trimming
            for (int i = strlen(titlestr) - 1; i >= 0; i--)
            {
                if (titlestr[i] == ' ')
                    titlestr[i] = '\0';
                else
                    break;
            }
            // print content
            printf("  ID: %s, Title: %s, Length: %s\n", idstr, titlestr, lengthstr);
        }

        // move to the next block

        int next = get_next_block(datafile, b);
        printf("Next block: %d\n\n", next);
        b = next;
    }

    printf("</pre>\n");
}
