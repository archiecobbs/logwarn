
/*
 * Logwarn - Utility for finding interesting messages in log files
 *
 * Copyright (C) 2010 Archie L. Cobbs. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * $Id$
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "logwarn.h"

// Definitions
#define INODENUM_NAME   "INODENUM"
#define LINENUM_NAME    "LINENUM"
#define POSITION_NAME   "POSITION"
#define MATCHING_NAME   "MATCHING"

// Internal functions
static void cache_file(const char *cache_dir, const char *file, char *buf, size_t max);

int
load_state(const char *cache_dir, const char *file, struct scan_state *state)
{
    char buf[PATH_MAX];
    struct stat sb;
    FILE *fp;

    memset(state, 0, sizeof(*state));
    state->line = 1;
    cache_file(cache_dir, file, buf, sizeof(buf));
    if (stat(buf, &sb) == -1 || S_ISDIR(sb.st_mode))
        return -1;
    if ((fp = fopen(buf, "r")) == NULL)
        return -1;
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        const char *s = buf;
        unsigned long value;
        const char *fname;
        const char *fvalue;
        char *eptr;
        char *t;

        // Ignore blank lines and comments
        while (isspace(*s))
            s++;
        if (*s == '\0' || *s == '#')
            continue;

        // Parse name and value
        if ((t = strchr(s, '=')) == NULL)
            continue;
        fname = s;
        *t++ = '\0';
        if (*t != '"')
            continue;
        fvalue = ++t;
        if ((t = strchr(t, '"')) == NULL)
            continue;
        *t = '\0';

        // Handle "false" and "true"
        if (strcmp(fvalue, "false") == 0)
            fvalue = "0";
        else if (strcmp(fvalue, "true") == 0)
            fvalue = "1";

        // Decode numerical value
        if (((value = strtoul(fvalue, &eptr, 10)) == ULONG_MAX
           && errno == ERANGE)
          || *eptr != '\0') {
            warnx("can't decode value \"%s\" for \"%s\"", fvalue, fname);
            continue;
        }

        // Update state
        if (strcmp(fname, INODENUM_NAME) == 0)
            state->inode = value;
        else if (strcmp(fname, LINENUM_NAME) == 0)
            state->line = value;
        else if (strcmp(fname, POSITION_NAME) == 0)
            state->pos = value;
        else if (strcmp(fname, MATCHING_NAME) == 0)
            state->matching = value != 0;
    }
    fclose(fp);
    return 0;
}

int
save_state(const char *cache_dir, const char *file, const struct scan_state *state)
{
    char buf[PATH_MAX];
    FILE *fp;

    cache_file(cache_dir, file, buf, sizeof(buf));
    if ((fp = fopen(buf, "w")) == NULL)
        return -1;
    dump_state(fp, file, state);
    return 0;
}

void
dump_state(FILE *fp, const char *file, const struct scan_state *state)
{
    fprintf(fp, "# logscanner saved state for \"%s\"\n", file);
    fprintf(fp, "%s=\"%lu\"\n", INODENUM_NAME, (unsigned long)state->inode);
    fprintf(fp, "%s=\"%lu\"\n", LINENUM_NAME, state->line);
    fprintf(fp, "%s=\"%lu\"\n", POSITION_NAME, state->pos);
    fprintf(fp, "%s=\"%s\"\n", MATCHING_NAME, state->matching ? "true" : "false");
    fclose(fp);
}

void
init_state_from_logfile(const char *file, struct scan_state *state)
{
    struct stat sb;
    FILE *fp;
    int ch;

    memset(state, 0, sizeof(*state));
    state->line = 1;
    if (stat(file, &sb) == -1)
        err(EXIT_ERROR, "%s", file);
    if (S_ISDIR(sb.st_mode & S_IFMT)) {
        errno = EISDIR;
        err(EXIT_ERROR, "%s", file);
    }
    state->inode = sb.st_ino;
    if ((fp = fopen(file, "r")) == NULL)
        err(EXIT_ERROR, "%s", file);
    while ((ch = getc(fp)) != EOF) {
        state->pos++;
        if (ch == '\n')
            state->line++;
    }
    fclose(fp);
}

static void
cache_file(const char *cache_dir, const char *file, char *buf, size_t max)
{
    int i;

    snprintf(buf, max, "%s/", cache_dir);
    while (*file == '/')
        file++;
    for (i = strlen(buf); i < max - 1 && *file != '\0'; ) {
        char ch = *file++;

        if (ch == '/')
            ch = '_';
        buf[i++] = ch;
        buf[i] = '\0';
    }
}

