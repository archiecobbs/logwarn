
/*
 * Logwarn - Utility for finding interesting messages in log files
 *
 * Copyright (C) 2010-2011 Archie L. Cobbs. All rights reserved.
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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "logwarn.h"

// Definitions
#define INODENUM_NAME       "INODENUM"
#define LINENUM_NAME        "LINENUM"
#define POSITION_NAME       "POSITION"
#define MATCHING_NAME       "MATCHING"
#define REPEAT_PREFIX       "REPEAT_OCCURRENCES_"
#define REPEAT_PREFIX_LEN   (sizeof(REPEAT_PREFIX) - 1)
#define STDIN_LOGFILE_NAME  "_stdin"

int
load_state(const char *state_file, struct scan_state *state)
{
    char buf[1024];
    struct stat sb;
    FILE *fp;

    reset_state(state);
    state->line = 1;
    if (stat(state_file, &sb) == -1 || S_ISDIR(sb.st_mode))
        return -1;
    if ((fp = fopen(state_file, "r")) == NULL)
        return -1;
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        const char *s = buf;
        unsigned long value;
        const char *fname;
        char *fvalue;
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

        // Handle repeat lines
        if (strncmp(fname, REPEAT_PREFIX, REPEAT_PREFIX_LEN) == 0) {
            struct repeat *repeat;
            unsigned int hash;
            unsigned int rnum = 0;
            char *saveptr;
            char *token;

            if (sscanf(fname + REPEAT_PREFIX_LEN, "%x", &hash) != 1)
                continue;
            if ((repeat = find_repeat(state, hash)) == NULL)
                continue;
            for (token = strtok_r(fvalue, " ", &saveptr); token != NULL; token = strtok_r(NULL, " ", &saveptr)) {
                unsigned long timestamp;
                unsigned int rcount;
                char *slash;
                int i;

                // Parse optional timestamp repeat count
                rcount = 1;
                if ((slash = strchr(token, '/')) != NULL) {
                    (void)sscanf(slash + 1, "%u", &rcount);
                    *slash = '\0';
                }

                // Parse timestamp itself
                if (sscanf(token, "%lu", &timestamp) != 1)
                    break;

                // Add repeat occurrence(s)
                for (i = 0; i < rcount && rnum < repeat->num; i++)
                    repeat->occurrences[rnum++] = timestamp;
            }
            continue;
        }

        // Handle "false" and "true"
        if (strcmp(fvalue, "false") == 0)
            strcpy(fvalue, "0");
        else if (strcmp(fvalue, "true") == 0)
            strcpy(fvalue, "1");

        // Decode numerical value
        if (((value = strtoul(fvalue, &eptr, 10)) == ULONG_MAX && errno == ERANGE) || *eptr != '\0') {
            fprintf(stderr, "%s: can't decode value \"%s\" for \"%s\"", PACKAGE, fvalue, fname);
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
    (void)fclose(fp);
    return 0;
}

struct repeat *
find_repeat(struct scan_state *state, unsigned int hash) {
    int i;

    for (i = 0; i < state->num_repeats; i++) {
        struct repeat *const repeat = &state->repeats[i];

        if (hash == repeat->hash)
            return repeat;
    }
    return NULL;
}

void
reset_state(struct scan_state *state)
{
    unsigned int num_repeats_save;
    struct repeat *repeats_save;

    // Reset state
    num_repeats_save = state->num_repeats;
    repeats_save = state->repeats;
    memset(state, 0, sizeof(*state));
    state->num_repeats = num_repeats_save;
    state->repeats = repeats_save;
}

void
save_state(const char *state_file, const char *logfile, const struct scan_state *state)
{
    FILE *fp;

    if ((fp = fopen(state_file, "w")) == NULL) {
        fprintf(stderr, "%s: %s: %s\n", PACKAGE, state_file, strerror(errno));
        exit(EXIT_ERROR);
    }
    dump_state(fp, logfile, state);
    if (fclose(fp) == EOF) {
        fprintf(stderr, "%s: %s: %s\n", PACKAGE, state_file, strerror(errno));
        exit(EXIT_ERROR);
    }
}

void
dump_state(FILE *fp, const char *logfile, const struct scan_state *state)
{
    struct scan_state stdin_state;
    int i;

    if (logfile == NULL) {
        logfile = STDIN_LOGFILE_NAME;
        stdin_state = *state;
        stdin_state.inode = 0;
        state = &stdin_state;
    }
    fprintf(fp, "# %s %s state for \"%s\"\n", PACKAGE_TARNAME, PACKAGE_VERSION, logfile);
    fprintf(fp, "%s=\"%lu\"\n", INODENUM_NAME, (unsigned long)state->inode);
    fprintf(fp, "%s=\"%lu\"\n", LINENUM_NAME, state->line);
    fprintf(fp, "%s=\"%lu\"\n", POSITION_NAME, state->pos);
    fprintf(fp, "%s=\"%s\"\n", MATCHING_NAME, state->matching ? "true" : "false");
    for (i = 0; i < state->num_repeats; i++) {
        const struct repeat *repeat = &state->repeats[i];
        unsigned int rcount;
        unsigned int rnum;

        if (repeat->occurrences[0] == 0)
            continue;
        fprintf(fp, "%s%08x=\"", REPEAT_PREFIX, repeat->hash);
        for (rnum = 0; rnum < repeat->num && repeat->occurrences[rnum] != 0; rnum += rcount) {

            // Output the next timestamp
            if (rnum > 0)
                fputc(' ', fp);
            fprintf(fp, "%lu", (unsigned long)repeat->occurrences[rnum]);

            // Compress repetitions of the same timestamp into one token
            rcount = 1;
            while (rnum + rcount < repeat->num && repeat->occurrences[rnum + rcount] == repeat->occurrences[rnum])
                rcount++;
            if (rcount > 1)
                fprintf(fp, "/%u", rcount);
        }
        fprintf(fp, "\"\n");
    }
}

void
init_state_from_logfile(const char *logfile, struct scan_state *state)
{
    struct stat sb;
    FILE *fp;
    int ch;

    // Read state file
    reset_state(state);
    state->line = 1;
    if (logfile == NULL)
        return;
    if (stat(logfile, &sb) == -1) {
        fprintf(stderr, "%s: %s: %s\n", PACKAGE, logfile, strerror(errno));
        exit(EXIT_ERROR);
    }
    if (S_ISDIR(sb.st_mode & S_IFMT)) {
        fprintf(stderr, "%s: %s: %s\n", PACKAGE, logfile, strerror(EISDIR));
        exit(EXIT_ERROR);
    }
    state->inode = sb.st_ino;
    if ((fp = fopen(logfile, "r")) == NULL) {
        fprintf(stderr, "%s: %s: %s\n", PACKAGE, logfile, strerror(errno));
        exit(EXIT_ERROR);
    }
    while ((ch = getc(fp)) != EOF) {
        state->pos++;
        if (ch == '\n')
            state->line++;
    }
    (void)fclose(fp);
}

void
state_file_name(const char *state_dir, const char *logfile, char *buf, size_t max)
{
    int i;

    if (logfile == NULL) {
        snprintf(buf, max, "%s", STDIN_LOGFILE_NAME);
        return;
    }
    snprintf(buf, max, "%s/", state_dir);
    while (*logfile == '/')
        logfile++;
    for (i = strlen(buf); i < max - 1 && *logfile != '\0'; ) {
        char ch = *logfile++;

        if (ch == '/')
            ch = '_';
        buf[i++] = ch;
        buf[i] = '\0';
    }
}

