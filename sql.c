#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "io_helper.h"
#include "blockio.h"
#include <ctype.h>

#define MAXQS 8192
#define MAXSQL 1024
#define MAXTOK 256

#define RECORD_SIZE 43

void handle_create(char *qs);
void handle_insert(char *qs);
void handle_select(char *qs);
void handle_update(char *qs);
void handle_delete(char *qs);

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

    // decode it into qs_buf
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

    // Grab table name (up to first '(')
    if (sscanf(qs, "CREATE TABLE %63[^ (]", tbl) != 1)
    {
        printf("<p>ERROR: bad CREATE syntax</p>\n");
        return;
    }

    // Make sure no space between table name and '('
    char *expected = strstr(qs, "CREATE TABLE ") + strlen("CREATE TABLE ");
    if (expected[strlen(tbl)] != '(')
    {
        printf("<p>ERROR: bad CREATE syntax</p>\n");
        return;
    }

    // Find '(' and the last ')'
    char *p = strchr(qs, '(');
    char *q = strrchr(qs, ')');
    if (!p || !q || p > q)
    {
        printf("<p>ERROR: bad CREATE syntax</p>\n");
        return;
    }

    // Checlk columns between ( and )
    size_t len = q - (p + 1);
    if (len >= sizeof(cols))
    {
        printf("<p>ERROR: column list too long</p>\n");
        return;
    }
    memcpy(cols, p + 1, len);
    cols[len] = '\0';

    // Check for empty column list
    char *tmp = cols;
    while (*tmp && isspace((unsigned char)*tmp))
        tmp++;
    if (*tmp == '\0')
    {
        printf("<p>ERROR: no columns specified</p>\n");
        return;
    }

    // Validate table name
    for (char *p = tbl; *p; p++)
    {
        if (!isalnum((unsigned char)*p) && *p != '_')
        {
            printf("<p>ERROR: invalid table name</p>\n");
            return;
        }
    }

    // Validate each column
    char cols_copy[512];
    strncpy(cols_copy, cols, sizeof(cols_copy));
    cols_copy[511] = '\0'; // just in case

    char *col = strtok(cols_copy, ",");
    while (col)
    {
        // make local copy of the column definition
        char column_def[128];
        strncpy(column_def, col, sizeof(column_def));
        column_def[127] = '\0';

        // trim leading spaces
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

        // Validate name
        for (char *p = name; *p; p++)
        {
            if (!isalnum((unsigned char)*p) && *p != '_')
            {
                printf("<p>ERROR: invalid column name</p>\n");
                return;
            }
        }

        // Validate type
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

    // Check table existence
    char schemafile[80];
    snprintf(schemafile, sizeof(schemafile), "%s.schema", tbl);
    struct stat st;
    if (stat(schemafile, &st) == 0)
    {
        printf("<p>ERROR: table <b>%s</b> already exists</p>\n", tbl);
        return;
    }

    // Pack and store table(schema)
    char buf[256] = {0};
    snprintf(buf, sizeof(buf), "%s|%s;", tbl, cols);

    int b = alloc_block(schemafile);
    write_block(schemafile, b, buf);
    set_next_block(schemafile, b, -1);

    printf("<p>Created table <b>%s</b></p>\n", tbl);

    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    int b2 = alloc_block(datafile);
    set_next_block(datafile, b2, -1);
}

// Insert

void handle_insert(char *qs) {
    char tbl[64], vals[512];

    // parse input
    if (sscanf(qs, "INSERT INTO %63[^ ] VALUES(%511[^)])", tbl, vals) != 2) {
        printf("<p>ERROR: bad INSERT syntax</p>\n");
        return;
    }

    // confirm table exists
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    struct stat st;
    if (stat(datafile, &st) < 0) {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // check field count, should only be 2 commas for 3 values
    int commas = 0;
    for (char *p = vals; *p; p++) {
        if (*p == ',') commas++;
    }
    if (commas != 2) {
        printf("<p>ERROR: bad INSERT values</p>\n");
        return;
    }

    // parse fields
    int id, length;
    char title[128];
    if (sscanf(vals, "%d,%127[^,],%d", &id, title, &length) != 3) {
        printf("<p>ERROR: bad INSERT values</p>\n");
        return;
    }

    // format record
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

    // scan blocks for empty spot
    int nblocks = st.st_size / BLOCK_SIZE;
    int written = 0;
    for (int b = 1; b < nblocks; b++) {
        char buf[BLOCK_SIZE];
        read_block(datafile, b, buf);

        for (int off = 0; off + RECORD_SIZE <= BLOCK_SIZE; off += RECORD_SIZE) {
            if (buf[off] == '\0') {
                memcpy(buf + off, record, RECORD_SIZE);
                write_block(datafile, b, buf);
                written = 1;
                break;
            }
        }
        if (written) break;
    }

    // if no space, allocate a new block
    if (!written) {
        int b = alloc_block(datafile);
        char buf[BLOCK_SIZE] = {0};
        memcpy(buf, record, RECORD_SIZE);
        write_block(datafile, b, buf);
        set_next_block(datafile, b, -1);
    }

    printf("<p>Inserted into <b>%s</b></p>\n", tbl);
}





// SELECT 

void handle_select(char *qs) {
    char cols[128], tbl[64], cond[128];
    char header_cols[128], data_cols[128];

    if (sscanf(qs, "SELECT %127[^ ] FROM %63[^ ] WHERE %127[^\r\n]", cols, tbl, cond) != 3) {
        printf("<p>ERROR: bad SELECT syntax</p>\n");
        return;
    }

    if (strcmp(cols, "*") == 0) {
        strcpy(cols, "id,title,length");
    }

    strncpy(header_cols, cols, sizeof(header_cols));
    strncpy(data_cols,   cols, sizeof(data_cols));

    for (char *p = tbl; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') {
            printf("<p>ERROR: invalid table name</p>\n");
            return;
        }
    }

    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    struct stat st;
    if (stat(datafile, &st) < 0) {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    printf("<table><tr>");
    char *col = strtok(header_cols, ",");
    while (col) {
        while (*col == ' ') col++;
        printf("<th>%s</th>", col);
        col = strtok(NULL, ",");
    }
    printf("</tr>\n");

    char *op = NULL;
    if (strstr(cond, "!=")) op = "!=";
    else if (strstr(cond, "<")) op = "<";
    else if (strstr(cond, ">")) op = ">";
    else if (strstr(cond, "=")) op = "=";
    else {
        printf("<p>ERROR: unknown operator</p>\n");
        return;
    }

    char field[64], value[64];
    if (strcmp(op, "!=") == 0)
        sscanf(cond, "%63[^!]!=%63s", field, value);
    else
        sscanf(cond, "%63[^<>=]%*c%63s", field, value);
    int where_target = atoi(value);

    int nblocks = st.st_size / BLOCK_SIZE;
    for (int b = 1; b < nblocks; b++) {
        char buf[BLOCK_SIZE];
        read_block(datafile, b, buf);

        for (int offset = 0; offset + 42 <= BLOCK_SIZE; offset += 43) {
            char idstr[5] = {0};
            strncpy(idstr, buf + offset, 4);
            if (idstr[0] == '\0') continue;

            char titlestr[31] = {0};
            char lengthstr[9] = {0};
            strncpy(titlestr, buf + offset + 4, 30);
            strncpy(lengthstr, buf + offset + 34, 8);

            int id = atoi(idstr);
            int length = atoi(lengthstr);

            int match = 0;
            if      (strcmp(field, "id") == 0) {
                if      (!strcmp(op, "="))  match = (id     == where_target);
                else if (!strcmp(op, "<"))  match = (id     <  where_target);
                else if (!strcmp(op, ">"))  match = (id     >  where_target);
                else if (!strcmp(op, "!=")) match = (id     != where_target);
            }
            else if (strcmp(field, "length") == 0) {
                if      (!strcmp(op, "="))  match = (length == where_target);
                else if (!strcmp(op, "<"))  match = (length <  where_target);
                else if (!strcmp(op, ">"))  match = (length >  where_target);
                else if (!strcmp(op, "!=")) match = (length != where_target);
            }

            if (!match) continue;

            for (int i = strlen(titlestr) - 1; i >= 0; i--) {
                if (titlestr[i] == ' ') titlestr[i] = '\0';
                else break;
            }

            printf("<tr>");
            char data_cols_copy[128];
            strncpy(data_cols_copy, data_cols, sizeof(data_cols_copy));
            col = strtok(data_cols_copy, ",");
            while (col) {
                while (*col == ' ') col++;
                if      (!strcmp(col, "id"))     printf("<td>%d</td>", id);
                else if (!strcmp(col, "title"))  printf("<td>%s</td>", titlestr);
                else if (!strcmp(col, "length")) printf("<td>%d</td>", length);
                col = strtok(NULL, ",");
            }
            printf("</tr>\n");
        }
    }
    printf("</table>\n");
}

// UPDATE 

void handle_update(char *qs) {
    char tbl[64], setp[128], cond[128];
    if (sscanf(qs, "UPDATE %63s SET %127[^ ] WHERE %127[^\r\n]", tbl, setp, cond) != 3) {
        printf("<p>ERROR: bad UPDATE syntax</p>\n");
        return;
    }

    char field[64], newval[128];
    if (sscanf(setp, "%63[^=]=%127s", field, newval) != 2) {
        printf("<p>ERROR: bad SET syntax</p>\n");
        return;
    }

    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    struct stat st;
    if (stat(datafile, &st) < 0) {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    char *op = NULL;
    if (strstr(cond, "!=")) op = "!=";
    else if (strstr(cond, "<")) op = "<";
    else if (strstr(cond, ">")) op = ">";
    else if (strstr(cond, "=")) op = "=";
    else {
        printf("<p>ERROR: unknown operator</p>\n");
        return;
    }

    char where_field[64], where_value[64];
    if (strcmp(op, "!=") == 0)
        sscanf(cond, "%63[^!]!=" "%63s", where_field, where_value);
    else
        sscanf(cond, "%63[^<>=]%*c%63s", where_field, where_value);

    int where_target = atoi(where_value);

    int nblocks = st.st_size / BLOCK_SIZE;
    for (int b = 1; b < nblocks; b++) {
        char buf[BLOCK_SIZE];
        read_block(datafile, b, buf);

        int dirty = 0;
        for (int offset = 0; offset + 42 <= BLOCK_SIZE; offset += 43) {
            char idstr[5] = {0};
            strncpy(idstr, buf + offset, 4);
            if (idstr[0] == '\0') continue;

            char titlestr[31] = {0};
            char lengthstr[9] = {0};
            strncpy(titlestr, buf + offset + 4, 30);
            strncpy(lengthstr, buf + offset + 34, 8);

            int id = atoi(idstr);
            int length = atoi(lengthstr);

            int match = 0;
            if      (strcmp(where_field, "id") == 0) {
                if      (!strcmp(op, "="))  match = (id     == where_target);
                else if (!strcmp(op, "<"))  match = (id     <  where_target);
                else if (!strcmp(op, ">"))  match = (id     >  where_target);
                else if (!strcmp(op, "!=")) match = (id     != where_target);
            }
            else if (strcmp(where_field, "length") == 0) {
                if      (!strcmp(op, "="))  match = (length == where_target);
                else if (!strcmp(op, "<"))  match = (length <  where_target);
                else if (!strcmp(op, ">"))  match = (length >  where_target);
                else if (!strcmp(op, "!=")) match = (length != where_target);
            }

            if (!match) continue;

            if (strcmp(field, "title") == 0) {
                char padded[31];
                memset(padded, ' ', 30);
                padded[30] = '\0';
                strncpy(padded, newval, strlen(newval));
                memcpy(buf + offset + 4, padded, 30);
                dirty = 1;
            } else if (strcmp(field, "length") == 0) {
                char len_update[9];
                snprintf(len_update, sizeof(len_update), "%08d", atoi(newval));
                memcpy(buf + offset + 34, len_update, 8);
                dirty = 1;
            } else {
                printf("<p>ERROR: unknown column %s</p>\n", field);
                return;
            }
        }

        if (dirty) write_block(datafile, b, buf);
    }

    printf("<p>Update done on <b>%s</b></p>\n", tbl);
}



// DELETE 

void handle_delete(char *qs) {
    char tbl[64], cond[128];

    if (sscanf(qs, "DELETE FROM %63s WHERE %127[^\r\n]", tbl, cond) != 2) {
        printf("<p>ERROR: bad DELETE syntax</p>\n");
        return;
    }

    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);

    struct stat st;
    if (stat(datafile, &st) < 0) {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    char *op = NULL;
    if (strstr(cond, "!=")) op = "!=";
    else if (strstr(cond, "<")) op = "<";
    else if (strstr(cond, ">")) op = ">";
    else if (strstr(cond, "=")) op = "=";
    else {
        printf("<p>ERROR: unknown operator</p>\n");
        return;
    }

    char where_field[64], where_value[64];
    if (strcmp(op, "!=") == 0)
        sscanf(cond, "%63[^!]!=%63s", where_field, where_value);
    else
        sscanf(cond, "%63[^<>=]%*c%63s", where_field, where_value);

    int where_target = atoi(where_value);

    int nblocks = st.st_size / BLOCK_SIZE;
    for (int b = 1; b < nblocks; b++) {
        char buf[BLOCK_SIZE];
        int dirty = 0;
        read_block(datafile, b, buf);

        for (int off = 0; off + RECORD_SIZE <= BLOCK_SIZE; off += RECORD_SIZE) {
            char idstr[5] = {0};
            strncpy(idstr, buf + off, 4);
            if (idstr[0] == '\0') continue;

            char lengthstr[9] = {0};
            strncpy(lengthstr, buf + off + 34, 8);

            int id = atoi(idstr);
            int length = atoi(lengthstr);

            int match = 0;
            if (strcmp(where_field, "id") == 0) {
                if (strcmp(op, "=") == 0) match = (id == where_target);
                else if (strcmp(op, "<") == 0) match = (id < where_target);
                else if (strcmp(op, ">") == 0) match = (id > where_target);
                else if (strcmp(op, "!=") == 0) match = (id != where_target);
            } else if (strcmp(where_field, "length") == 0) {
                if (strcmp(op, "=") == 0) match = (length == where_target);
                else if (strcmp(op, "<") == 0) match = (length < where_target);
                else if (strcmp(op, ">") == 0) match = (length > where_target);
                else if (strcmp(op, "!=") == 0) match = (length != where_target);
            }

            if (match) {
                memset(buf + off, 0, RECORD_SIZE);
                dirty = 1;
            }
        }

        if (dirty) write_block(datafile, b, buf);
    }

    printf("<p>Deleted matching rows in <b>%s</b></p>\n", tbl);
}

