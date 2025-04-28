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

/*
int main()
{


    // Read SQL from QUERY_STRING
    char *qs = getenv("QUERY_STRING");
    printf("Content-Type: text/html\r\n\r\n");
    if (!qs) {
        printf("<p>ERROR: no query string provided</p>\n");
        return 1;
    }


    // Dispatch based on leading keyword
    if (strncmp(qs, "CREATE ", 7) == 0)
        handle_create(qs);
    else if (strncmp(qs, "INSERT ", 7) == 0)
        handle_insert(qs);
    else if (strncmp(qs, "SELECT ", 7) == 0)
        handle_select(qs);
    else if (strncmp(qs, "UPDATE ", 7) == 0)
        handle_update(qs);
    else if (strncmp(qs, "DELETE ", 7) == 0)
        handle_delete(qs);
    else
        printf("<p>ERROR: unknown command</p>\n");

    return 0;
}
*/

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
        for (int i = 0; i < 13; i++)
            qs[i] = toupper((unsigned char)qs[i]); // turn into upper case
        handle_create(qs);
    }
    else if (strncasecmp(qs, "INSERT INTO ", 12) == 0)
    {
        for (int i = 0; i < 7; i++)
            qs[i] = toupper((unsigned char)qs[i]);
        handle_insert(qs);
    }
    else if (strncasecmp(qs, "SELECT ", 7) == 0)
    {
        for (int i = 0; i < 7; i++)
            qs[i] = toupper((unsigned char)qs[i]);
        handle_select(qs);
    }
    else if (strncasecmp(qs, "UPDATE ", 7) == 0)
    {
        for (int i = 0; i < 7; i++)
            qs[i] = toupper((unsigned char)qs[i]);
        handle_update(qs);
    }
    else if (strncasecmp(qs, "DELETE FROM", 11) == 0)
    {
        for (int i = 0; i < 7; i++)
            qs[i] = toupper((unsigned char)qs[i]);
        handle_delete(qs);
    }
    else
    {
        printf("<p>ERROR: unknown command</p> %s\n",qs);
    }

    return 0;
}

// CREATE TABLE (good)
void handle_create(char *qs)
{
    // Parse name and cols: "CREATE TABLE name(col:type, ...)"
    char tbl[64], cols[512];
    if (sscanf(qs, " CREATE TABLE %63[^(] (%511[^)])",tbl, cols) != 2) // format before was wrong so it didnt take the mixed cases
    {
        printf("<p>ERROR: bad CREATE syntax</p>%s\n",qs);
        return;
    }

    // build schema filename

    //getting rid of the whitespace between filename and .schema
    
    size_t len = strlen(tbl);
    if (len > 0 && tbl[len - 1] == ' ') {
    tbl[len - 1] = '\0';  
    }

    char schemafile[80];
    snprintf(schemafile, sizeof(schemafile), "%s.schema", tbl);

    // Check if the table already exists in schema file
    struct stat st;
    if (stat(schemafile, &st) == 0)
    {
        // file exists
        printf("<p>ERROR: table <b>%s</b> already exists</p>\n", tbl);
        exit(1);
    }

    // Pack into one 256-byte block
    char buf[256] = {0};
    // store the blocks
    snprintf(buf, sizeof(buf), "%s|%s;", tbl, cols);

    // write to blockio
    int b = alloc_block(schemafile);
    write_block(schemafile, b, buf);
    set_next_block(schemafile, b, -1);

    printf("<p>Created table <b>%s</b></p>\n", tbl);
}

// INSERT (scanf needs modification)
void handle_insert(char *qs)
{
    // ie "INSERT INTO name VALUES(v1,v2,...)"
    char tbl[64], vals[512];
    if (sscanf(qs, "INSERT INTO %63s VALUES(%511[^)])", tbl, vals) != 2)
    {
        printf("<p>ERROR: bad INSERT</p>");
        return;
    }

    // verify the table(schema) exists
    char schemafile[80];
    snprintf(schemafile, sizeof(schemafile), "%s.schema", tbl);
    struct stat st;
    if (stat(schemafile, &st) < 0)
    {
        // file not found or zero-size
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        exit(1);
    }

    // build the data file
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);

    // pack values into one block, e.g. "v1|v2|v3;"
    char buf[256] = {0};
    snprintf(buf, sizeof(buf), "%s;", vals);

    // if data file is missing or empty, insert first
    if (stat(datafile, &st) < 0 || st.st_size == 0)
    {
        // no blocks yet
        int nb = alloc_block(datafile);
        write_block(datafile, nb, buf);
        set_next_block(datafile, nb, -1);
    }
    else
    {
        // goto tail and append
        int b = 0;
        while (1)
        {
            int32_t nx = get_next_block(datafile, b);
            if (nx == -1)
                break;
            b = nx;
        }
        // append new block
        int nb = alloc_block(datafile);
        write_block(datafile, nb, buf);
        set_next_block(datafile, nb, -1);
        set_next_block(datafile, b, nb);
    }

    printf("<p>Inserted into <b>%s</b></p>\n", tbl);
}

// SELECT (scanf needs modification)
void handle_select(char *qs)
{
    // ie "SELECT col1,col2 FROM name WHERE col3<100"
    char cols[128], tbl[64], cond[128];
    //just checking those with a where clause for now 
    if (sscanf(qs, "SELECT %127[^ ] FROM %63s WHERE %127[^\r\n]", cols, tbl, cond) != 3)
    {
        printf("<p>ERROR: bad SELECT</p> %s\n",qs);
        return;
    }

/*trying to make another sscanf but this time of form SELECT * FROM 
*/



    printf("<table><tr>");
    // header
    for (char *c = strtok(cols, ","); c; c = strtok(NULL, ","))
    {
        printf("<th>%s</th>", c);
    }
    printf("</tr>\n");

    // open data file
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);

    // makes sure the table exists
    struct stat st;
    if (stat(datafile, &st) < 0)
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
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
        for (int i = 0; i < nf; i++)
            printf("<td>%s</td>", fields[i]);
        printf("</tr>\n");
    }
    printf("</table>\n");
}

// UPDATE (scanf needs modification)
void handle_update(char *qs)
{
    // ie "UPDATE name SET col2=200 WHERE col1=5"
    char tbl[64], setp[64], cond[64];
    if (sscanf(qs, "UPDATE %63s SET %63[^ ] WHERE %63[^\r\n]", tbl, setp, cond) != 3)
    {
        printf("<p>ERROR: bad UPDATE</p>");
        return;
    }

    // split "col=value" into col and value
    char *eq = strchr(setp, '=');
    *eq = '\0';
    //char *col = setp;
    char *val = eq + 1;

    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);

    // check table exist
    struct stat st;
    if (stat(datafile, &st) < 0)
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        exit(1);
    }

    for (int b = 0; b != -1; b = get_next_block(datafile, b))
    {
        char buf[256];
        read_block(datafile, b, buf);
        if (buf[0] == '\0')
            break;

        // split fields
        char tmp[256];
        strcpy(tmp, buf);
        char *fields[64];
        int nf = 0;
        for (char *f = strtok(tmp, "|;"); f; f = strtok(NULL, "|;"))
            fields[nf++] = f;

        int key = atoi(fields[0]);
        int where_val = atoi(strchr(cond, '=') + 1);
        if (key != where_val)
            continue;

        // rewrite the record as key|val;
        int32_t nx = get_next_block(datafile, b);
        snprintf(buf, sizeof(buf), "%d|%s;", key, val);
        write_block(datafile, b, buf);
        set_next_block(datafile, b, nx);
    }
    printf("<p>Update done on <b>%s</b></p>\n", tbl);
}

// DELETE (scanf needs modification)
void handle_delete(char *qs)
{
    // ie "DELETE FROM name WHERE col1=5"
    char tbl[64], cond[64];
    if (sscanf(qs, "DELETE FROM %63s WHERE %63[^\r\n]", tbl, cond) != 2)
    {
        printf("<p>ERROR: bad DELETE</p>");
        return;
    }

    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);

    // ensure table existence
    struct stat st;
    if (stat(datafile, &st) < 0)
    {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        exit(1);
    }

    int prev = -1;
    for (int b = 0; b != -1;)
    {
        char buf[256];
        read_block(datafile, b, buf);
        if (buf[0] == '\0')
            break;

        int key = atoi(buf);
        int32_t nx = get_next_block(datafile, b);
        if (strstr(cond, "=") && key == atoi(strchr(cond, '=') + 1))
        {
            // unlink & free
            if (prev != -1)
                set_next_block(datafile, prev, nx);
            free_block(datafile, b);
            b = nx;
        }
        else
        {
            prev = b;
            b = nx;
        }
    }
    printf("<p>Deleted matching rows in <b>%s</b></p>\n", tbl);
}
