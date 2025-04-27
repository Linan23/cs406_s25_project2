#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "io_helper.h"
#include "blockio.h"

#define MAXQS 8192
#define MAXSQL 1024
#define MAXTOK 256

void handle_create(char *qs);
void handle_insert(char *qs);
void handle_select(char *qs);
void handle_update(char *qs);
void handle_delete(char *qs);

int main() {
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

// CREATE TABLE name(col:type, ...)
void handle_create(char *qs) {
    // Parse name and cols: "CREATE TABLE name(col:type, ...)"
    char tbl[64], cols[512];
    if (sscanf(qs, "CREATE TABLE %63[^'('](%511[^')'])", tbl, cols) != 2) {
        printf("<p>ERROR: bad CREATE syntax</p>"); return;
    }

    // Build schema filename: "<tbl>.schema"
    char schemafile[80];
    snprintf(schemafile, sizeof(schemafile), "%s.schema", tbl);

    // Check if the table already exists in schema file
    struct stat st;
    if (stat(schemafile, &st) == 0) {
        // file exists
        printf("<p>ERROR: table <b>%s</b> already exists</p>\n", tbl);
        exit(1);
    }

    // Pack into one 256-byte block
    char buf[256] = {0};
    // store "name|col:type,col:type;” 
    snprintf(buf, sizeof(buf), "%s|%s;", tbl, cols);

    // Write via blockio
    int b = alloc_block(schemafile);
    write_block(schemafile, b, buf);
    set_next_block(schemafile, b, -1); 

    printf("<p>Created table <b>%s</b></p>\n", tbl);
}


// INSERT 
void handle_insert(char *qs) {
    // "INSERT INTO name VALUES(v1,v2,...)"
    char tbl[64], vals[512];
    if (sscanf(qs, "INSERT INTO %63s VALUES(%511[^)])", tbl, vals) != 2) {
        printf("<p>ERROR: bad INSERT</p>"); 
        return;
    }

    // build data filename
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);

    // Check table existence
    struct stat st;
    if (stat(datafile, &st) < 0) {
        // file not found or zero-size
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        exit(1);
    }

    // pack values into one block, e.g. "v1|v2|v3;"
    char buf[256] = {0};
    snprintf(buf, sizeof(buf), "%s;", vals);

     // see if file exists/has blocks
     struct stat st;
     if (stat(datafile, &st) < 0 || st.st_size == 0) {
         // no blocks yet
         int nb = alloc_block(datafile);
         write_block(datafile, nb, buf);
         set_next_block(datafile, nb, -1);
     } else {
         // walk to tail
         int b = 0;
         while (1) {
             int32_t nx = get_next_block(datafile, b);
             if (nx == -1) break;
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


// SELECT 
void handle_select(char *qs) {
    // e.g. qs="SELECT col1,col2 FROM name WHERE col3<100"
    char cols[128], tbl[64], cond[128];
    if (sscanf(qs, "SELECT %127[^ ] FROM %63s WHERE %127[^\r\n]", cols, tbl, cond) != 3) {
        printf("<p>ERROR: bad SELECT</p>"); return;
    }

    printf("<table><tr>");
    // header
    for (char *c=strtok(cols, ","); c; c=strtok(NULL, ",")) {
        printf("<th>%s</th>", c);
    }
    printf("</tr>\n");

    // open data file
    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);

    // makes sure the table exists
    struct stat st;
    if (stat(datafile, &st) < 0) {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        exit(1);
    }

    for (int b = 0; b != -1; b = get_next_block(datafile, b)) {
        // read block by block
        char buf[256];
        read_block(datafile, b, buf);
        if (buf[0] == '\0') 
            break;        // no more blocks
         
        // unpack
         char tmp[256];
         strcpy(tmp, buf);
         char *fields[64];
         int nf = 0;
         for (char *f = strtok(tmp, "|;"); f; f = strtok(NULL, "|;"))
             fields[nf++] = f;
 
         // WHERE on field #2
         int v = atoi(fields[2]);
         if (strstr(cond, "<") && v >= atoi(strchr(cond,'<')+1)) continue;
         if (strstr(cond, ">") && v <= atoi(strchr(cond,'>')+1)) continue;
 
         printf("<tr>");
         for (int i = 0; i < nf; i++) printf("<td>%s</td>", fields[i]);
         printf("</tr>\n");
    }
    printf("</table>\n");
}


// UPDATE 
void handle_update(char *qs) {
    // e.g. "UPDATE name SET col2=200 WHERE col1=5"
    char tbl[64], setp[64], cond[64];
    if (sscanf(qs, "UPDATE %63s SET %63[^ ] WHERE %63[^\r\n]", tbl, setp, cond)!=3) {
        printf("<p>ERROR: bad UPDATE</p>"); 
        return;
    }

    int newval = atoi(strchr(setp,'=')+1);

    char datafile[80];
    snprintf(datafile, sizeof(datafile), "%s.data", tbl);


     // check table exist
     struct stat st;
     if (stat(datafile, &st) < 0) {
         printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
         exit(1);
     }

    for (int b = 0; b != -1; b = get_next_block(datafile, b)) {
        char buf[256];
        read_block(datafile, b, buf);
        if (buf[0] == '\0') break;

        int key = atoi(buf);
        if (strstr(cond, "=") && key != atoi(strchr(cond,'=')+1)) continue;

        // preserve next pointer
        int32_t nx = get_next_block(datafile, b);
        // rewrite just “key|newval;…”
        snprintf(buf, sizeof(buf), "%d|%d;", key, newval);
        write_block(datafile, b, buf);
        set_next_block(datafile, b, nx);
    }
    printf("<p>Update done on <b>%s</b></p>\n", tbl);
}


// DELETE 
void handle_delete(char *qs) {
    // "DELETE FROM name WHERE col1=5"
    char tbl[64], cond[64];
    if (sscanf(qs, "DELETE FROM %63s WHERE %63[^\r\n]", tbl, cond)!=2) {
        printf("<p>ERROR: bad DELETE</p>"); return;
    }

    char datafile[80];
    snprintf(datafile,sizeof(datafile), "%s.data", tbl);


    // ensure table existence
    struct stat st;
    if (stat(datafile, &st) < 0) {
        printf("<p>ERROR: table <b>%s</b> does not exist</p>\n", tbl);
        exit(1);
    }


    int prev = -1;
    for (int b = 0; b != -1; ) {
        char buf[256];
        read_block(datafile, b, buf);
        if (buf[0] == '\0') break;

        int key = atoi(buf);
        int32_t nx  = get_next_block(datafile, b);
        if (strstr(cond, "=") && key == atoi(strchr(cond,'=')+1)) {
            // unlink & free
            if (prev != -1) set_next_block(datafile, prev, nx);
            free_block(datafile, b);
            b = nx;
        } else {
            prev = b;
            b    = nx;
        }
    }
    printf("<p>Deleted matching rows in <b>%s</b></p>\n", tbl);
}

