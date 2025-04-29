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
    if (strncmp(qs, "CREATE TABLE ", 13) == 0)
    {
        handle_create(qs);
    }
    else if (strncmp(qs, "INSERT ", 7) == 0)
    else if (strncasecmp(qs, "INSERT INTO ", 12) == 0)
    {
        handle_insert(qs);
    }
    else if (strncmp(qs, "SELECT ", 7) == 0)
    {
        handle_select(qs);
    }
    else if (strncmp(qs, "UPDATE ", 7) == 0)
    {
        handle_update(qs);
    }
    else if (strncmp(qs, "DELETE ", 7) == 0)
    else if (strncasecmp(qs, "DELETE FROM", 11) == 0)
    {
        handle_delete(qs);
    }
    else
    {
        printf("<p>ERROR: unknown command</p> %s\n",qs);
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
    if (sscanf(qs, " CREATE TABLE %63[^(] (%511[^)])",tbl, cols) != 2) // format before was wrong so it didnt take the mixed cases
    {
        printf("<p>ERROR: bad CREATE syntax</p>%s\n",qs);
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
    // build schema filename

    //getting rid of the whitespace between filename and .schema
    
    size_t len = strlen(tbl);
    if (len > 0 && tbl[len - 1] == ' ') {
    tbl[len - 1] = '\0';  
    }

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

void handle_insert(char *qs) {
    char tbl[64], vals[512];
    
    // parsing
    if (sscanf(qs, "INSERT INTO %63[^ ] VALUES(%511[^)])", tbl, vals) != 2) {
        printf("<p>ERROR: bad INSERT syntax</p>\n");
        return;
    }

    // Confirm the table exists
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    struct stat st;
    if (stat(datafile, &st) < 0) {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // Check comma counts 
    int commas = 0;
    for (char *p = vals; *p; p++) {
        if (*p == ',') commas++;
    }
    if (commas != 2) {
        printf("<p>ERROR: bad INSERT values</p>\n");
        return;
    }

    // Parse the fields
    int id, length;
    char title[128];
    if (sscanf(vals, "%d,%127[^,],%d", &id, title, &length) != 3) {
        printf("<p>ERROR: bad INSERT values</p>\n");
        return;
    }

    // Format the fields
    char idstr[5];
    snprintf(idstr, sizeof(idstr), "%04d", id);

    char titlestr[31];
    memset(titlestr, ' ', 30);
    titlestr[30] = '\0';
    strncpy(titlestr, title, strlen(title));

    char lengthstr[9];
    snprintf(lengthstr, sizeof(lengthstr), "%08d", length);

    // Write to block
    char buf[256] = {0};
    snprintf(buf, sizeof(buf), "%s%s%s", idstr, titlestr, lengthstr);

    int b = alloc_block(datafile);
    write_block(datafile, b, buf);
    set_next_block(datafile, b, -1);

    printf("<p>Inserted into <b>%s</b></p>\n", tbl);
}




// SELECT 

void handle_select(char *qs) {
    char cols[128], tbl[64], cond[128];
    char header_cols[128], data_cols[128];

    // Parse this: SELECT col1,col2[...] FROM table WHERE cond
    if (sscanf(qs, "SELECT %127[^ ] FROM %63[^ ] WHERE %127[^\r\n]",
               cols, tbl, cond) != 3) {
        printf("<p>ERROR: bad SELECT syntax</p>\n");
    //just checking those with a where clause for now 
    if (sscanf(qs, "SELECT %127[^ ] FROM %63s WHERE %127[^\r\n]", cols, tbl, cond) != 3)
    {
        printf("<p>ERROR: bad SELECT</p> %s\n",qs);
        return;
    }

/*trying to make another sscanf but this time of form SELECT * FROM 
*/



    // select all

    if (strcmp(cols, "*") == 0) {
        strcpy(cols, "id,title,length");
    }

    // copies to use because strtok() will destroy them
    strncpy(header_cols, cols, sizeof(header_cols));
    strncpy(data_cols,   cols, sizeof(data_cols));

    // Validate table name
    for (char *p = tbl; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') {
            printf("<p>ERROR: invalid table name</p>\n");
            return;
        }
    }

    // Confirm table exists
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    struct stat st;
    if (stat(datafile, &st) < 0) {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // Print the header row(<th> )
    printf("<table><tr>");
    char *col = strtok(header_cols, ",");
    while (col) {
        while (*col == ' ') col++;
        printf("<th>%s</th>", col);
        col = strtok(NULL, ",");
    }
    printf("</tr>\n");

    // Parse the WHERE clause into field, op, and integer target
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

    // check the block amount in the file
    int nblocks = st.st_size / BLOCK_SIZE;

    // Scan blocks
    for (int b = 1; b < nblocks; b++) {
        char buf[BLOCK_SIZE];
        exit(1);
    }


    //trying to find the right column to look at 
    char col_1[128],op[3], comp[64];
    if(sscanf(cond, "%127[^<=>]%2[<=>]%63s",col_1, op, comp)!=3){
        printf("<p>ERROR: bad SELECT in the second scan f for where</p> %s\n",qs);
        return;
    } 
    //trying to find the column in our original parsed query 
    // that we are looking for 
    int index=0;
    char colsCopy[128];
    strcpy(colsCopy,cols);

    char *col = strtok(colsCopy, ","); 
    int i=0;
    //iterate over the columns find one matching condition 
    while(col!=NULL){
        if(strcmp(col,col_1)==0){
              index =i;
              break;
        }   
        col = strtok(colsCopy, ",");
    }



    for (int b = 0; b != -1; b = get_next_block(datafile, b))
    {
        // read block by block
        char buf[256];
        read_block(datafile, b, buf);

        // check empty block
        if (buf[0] == '\0') continue;

        // unpack fixed width fields
        char idstr[5]     = {0};
        char titlestr[31] = {0};
        char lengthstr[9] = {0};
        strncpy(idstr,     buf,       4);
        strncpy(titlestr,  buf + 4,  30);
        strncpy(lengthstr, buf + 34,  8);
        int id     = atoi(idstr);
        int length = atoi(lengthstr);

        // applying where
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

        // trim spaces off the title
        for (int i = strlen(titlestr) - 1; i >= 0; i--) {
            if (titlestr[i] == ' ') titlestr[i] = '\0';
            else break;
        }

        // Print the row
        if (buf[0] == '\0')
            break; // no more blocks

        // unpack
        char tmp[256];
        strcpy(tmp, buf);
        char *fields[64];
        int nf = 0;
        for (char *f = strtok(tmp, "|;"); f; f = strtok(NULL, "|;"))
            fields[nf++] = f;

            //need to change this make it index instead of hardcode
        // WHERE on field #2
        int v = atoi(fields[index]);
        if (strstr(op, "<") && v >= atoi(strchr(op, '<') + 1))
            continue;
        if (strstr(op, ">") && v <= atoi(strchr(op, '>') + 1))
            continue;

        printf("<tr>");
        char data_cols_copy[128];
        strncpy(data_cols_copy, data_cols, sizeof(data_cols_copy));
        col = strtok(data_cols_copy, ",");
        while (col) {
            while (*col == ' ') col++;
            if      (!strcmp(col, "id"))     printf("<td>%d</td>",   id);
            else if (!strcmp(col, "title"))  printf("<td>%s</td>", titlestr);
            else if (!strcmp(col, "length")) printf("<td>%d</td>", length);
            col = strtok(NULL, ",");
        }
        printf("</tr>\n");
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

    // Split SET
    char field[64], newval[128];
    if (sscanf(setp, "%63[^=]=%127s", field, newval) != 2) {
        printf("<p>ERROR: bad SET syntax</p>\n");
        return;
    }

    // Confirm table exists
    // split "col=value" into col and value
    char *eq = strchr(setp, '=');
    *eq = '\0';
    //char *col = setp;
    char *val = eq + 1;

    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);
    struct stat st;
    if (stat(datafile, &st) < 0) {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        return;
    }

    // Parse WHERE condition
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

    // start processing from block 1
    int nblocks = st.st_size / BLOCK_SIZE;
    for (int b = 1; b < nblocks; b++) {
        char buf[BLOCK_SIZE];
        read_block(datafile, b, buf);

        if (buf[0] == '\0') continue;

        // extract fields
        char idstr[5] = {0};
        char titlestr[31] = {0};
        char lengthstr[9] = {0};
        strncpy(idstr, buf, 4);
        strncpy(titlestr, buf + 4, 30);
        strncpy(lengthstr, buf + 34, 8);

        int id = atoi(idstr);
        int length = atoi(lengthstr);

        // where conditions
        int match = 0;
        if (strcmp(where_field, "id") == 0) {
            if      (!strcmp(op, "="))  match = (id == where_target);
            else if (!strcmp(op, "<"))  match = (id <  where_target);
            else if (!strcmp(op, ">"))  match = (id >  where_target);
            else if (!strcmp(op, "!=")) match = (id != where_target);
        } else if (strcmp(where_field, "length") == 0) {
            if      (!strcmp(op, "="))  match = (length == where_target);
            else if (!strcmp(op, "<"))  match = (length <  where_target);
            else if (!strcmp(op, ">"))  match = (length >  where_target);
            else if (!strcmp(op, "!=")) match = (length != where_target);
        }

        if (!match) continue;

        // Update field
        if (strcmp(field, "title") == 0) {
            char padded[31];
            memset(padded, ' ', 30);
            padded[30] = '\0';
            strncpy(padded, newval, strlen(newval));
            memcpy(buf + 4, padded, 30);
        } else if (strcmp(field, "length") == 0) {
            char len_update[9];
            snprintf(len_update, sizeof(len_update), "%08d", atoi(newval));
            memcpy(buf + 34, len_update, 8);
        } else {
            printf("<p>ERROR: unknown column %s</p>\n", field);
            return;
        }

        write_block(datafile, b, buf);
    }

    printf("<p>Update done on <b>%s</b></p>\n", tbl);
}



// DELETE (scanf needs modification)

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

    // Parse where
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

    // start from block 1
    int prev = -1;
    for (int b = 1; b < nblocks; ) {
        char buf[BLOCK_SIZE];
        read_block(datafile, b, buf);

        if (buf[0] == '\0') {
            b++;
            continue;
        }

        char idstr[5] = {0};
        char lengthstr[9] = {0};
        strncpy(idstr, buf, 4);
        strncpy(lengthstr, buf + 34, 8);

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
            // set block to 0
            memset(buf, 0, sizeof(buf));
            write_block(datafile, b, buf);
        }

        b++;
    }

    printf("<p>Deleted matching rows in <b>%s</b></p>\n", tbl);
}
