
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
 *
 * $Id$
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "logwarn.h"

// Definitions
#ifndef DEFAULT_ROTPAT
#define DEFAULT_ROTPAT          "^(-[[:digit:]]{8}|\\.[01])(\\.(gz|bz2))?$"
#endif

#ifndef DEFAULT_STATE_DIR
#define DEFAULT_STATE_DIR       "/var/lib/logwarn"
#endif

struct repat {
    const char      *string;
    regex_t         regex;
    u_char          negate;
};

// Global variables
static const char   *state_dir;
static char         *state_file;
static struct repat log_pattern;
static struct repat rot_pattern;
static int          default_match = 1;
static int          auto_initialize;
static int          num_match_patterns;
static int          read_from_beginning;
static int          quiet;
static int          any_matches;
static struct repat *match_patterns;

// Internal functions
static void scan_file(const char *file, struct scan_state *state);
static void parse_pattern(struct repat *pat, const char *string);
static void version(void);
static void usage(void);

int
main(int argc, char **argv)
{
    struct scan_state state;
    const char *logfile;
    struct stat sb;
    const char *rotpat = DEFAULT_ROTPAT;
    int ignore_nonexistent = 0;
    int initialize = 0;
    int i;

    // Parse command line
    while ((i = getopt(argc, argv, "ad:f:him:npqr:vz")) != -1) {
        switch (i) {
        case 'a':
            auto_initialize = 1;
            break;
        case 'd':
            state_dir = optarg;
            break;
        case 'f':
            state_file = optarg;
            break;
        case 'm':
            parse_pattern(&log_pattern, optarg);
            break;
        case 'r':
            rotpat = optarg;
            break;
        case 'h':
            usage();
            exit(EXIT_OK);
        case 'i':
            initialize = 1;
            break;
        case 'n':
            ignore_nonexistent = 1;
            break;
        case 'p':
            default_match = 0;
            break;
        case 'q':
            quiet = 1;
            break;
        case 'z':
            read_from_beginning = 1;
            break;
        case 'v':
            version();
            exit(EXIT_OK);
        case '?':
        default:
            usage();
            exit(EXIT_ERROR);
        }
    }
    argv += optind;
    argc -= optind;
    switch (argc) {
    case 0:
        usage();
        exit(EXIT_ERROR);
    default:
        logfile = argv[0];
        if (strcmp(logfile, "-") == 0)
            logfile = NULL;
        argv++;
        argc--;
        num_match_patterns = argc;
        if (initialize && num_match_patterns > 0) {
            usage();
            exit(EXIT_ERROR);
        }
        if (num_match_patterns > 0) {
            if ((match_patterns = malloc(num_match_patterns * sizeof(*match_patterns))) == NULL)
                err(EXIT_ERROR, "malloc");
            memset(match_patterns, 0, num_match_patterns * sizeof(*match_patterns));
            for (i = 0; i < num_match_patterns; i++) {
                struct repat *const pat = &match_patterns[i];
                const char *patstr = argv[i];

                if (*patstr == '!') {
                    patstr++;
                    pat->negate = 1;
                }
                parse_pattern(pat, patstr);
            }
        }
        break;
    }

    // Check "-d" vs. "-f" and determine state file
    if (state_dir != NULL && state_file != NULL)
        errx(EXIT_ERROR, "specify only one of `-d' and `-f'");
    if (state_file == NULL) {
        if (state_dir == NULL)
            state_dir = DEFAULT_STATE_DIR;
        if ((state_file = malloc(PATH_MAX)) == NULL)
            err(EXIT_ERROR, "malloc");
        state_file_name(state_dir, logfile, state_file, PATH_MAX);
    }

    // Parse rotated file pattern
    parse_pattern(&rot_pattern, rotpat);

    // Initialize state
    memset(&state, 0, sizeof(state));
    state.line = 1;

    // Check if logfile exists
    if (logfile != NULL && stat(logfile, &sb) == -1) {
        switch (errno) {
        case ENOENT:
        case ENOTDIR:
        case ENAMETOOLONG:
            if (ignore_nonexistent)
                exit(EXIT_OK);
            // FALLTHROUGH
        default:
            err(EXIT_ERROR, "%s", logfile);
        }
    }

    // Handle explicit initialization case
    if (initialize) {
        init_state_from_logfile(logfile, &state);
        save_state(state_file, logfile, &state);
        exit(EXIT_OK);
    }

    // Load state, but handle implicit initialization on first "real"
    // run after explicit initialization if logfile previously did not
    // exist (in which case we would not have created a saved state file).
    // Also avoids repeats when we can't save our state for some reason.
    if (load_state(state_file, &state) == -1 && auto_initialize)
        init_state_from_logfile(logfile, &state);

    // Read from beginning?
    if (read_from_beginning) {
        state.line = 1;
        state.pos = 0;
    }

    // Has log file rotated since we last checked?
    // If so, scan the rotated file first
    if (logfile != NULL && sb.st_ino != state.inode) {
        char *rotated = NULL;
        char *dname;
        char *bname;
        char *temp;
        DIR *dir;

        // Get directory and filename of file containing logfile
        if ((temp = strdup(logfile)) == NULL || (bname = strdup(basename(temp))) == NULL)
            err(EXIT_ERROR, "strdup");
        free(temp);
        if ((temp = strdup(logfile)) == NULL || (dname = strdup(dirname(temp))) == NULL)
            err(EXIT_ERROR, "strdup");
        free(temp);

        // Search for rotated version in same directory
        if ((dir = opendir(dname)) != NULL) {
            const size_t bnamelen = strlen(bname);
            struct dirent *ent;

            for (ent = readdir(dir); ent != NULL; ent = readdir(dir)) {

                // Rotated file must have logfile name as prefix
                if (strncmp(ent->d_name, bname, bnamelen) != 0)
                    continue;

                // Skip the file itself
                if (ent->d_name[bnamelen] == '\0')
                    continue;

                // Compare rotated file against pattern
                if (regexec(&rot_pattern.regex, ent->d_name + bnamelen, 0, NULL, 0) != 0)
                    continue;

                // It's a candidate. Pick the first one in sorting order.
                if (rotated == NULL || strcmp(ent->d_name, rotated) < 0) {
                    if ((rotated = realloc(rotated, strlen(ent->d_name) + 1)) == NULL)
                        err(EXIT_ERROR, "realloc");
                    strcpy(rotated, ent->d_name);
                }
            }
            closedir(dir);
        }

        // Scan rotated file first, assuming it's the previous version
        if (rotated != NULL) {
            char buf[PATH_MAX];

            snprintf(buf, sizeof(buf), "%s/%s", dname, rotated);
            scan_file(buf, &state);
        }

        // Clean up
        free(dname);
        free(bname);
        free(rotated);

        // Update state for new file
        state.inode = sb.st_ino;
        state.line = 1;
        state.pos = 0;
    }

    // Now scan the logfile itself
    scan_file(logfile, &state);

    // Save state
    save_state(state_file, logfile, &state);

    // Done
    exit(any_matches ? EXIT_MATCHES : EXIT_OK);
}

static void
scan_file(const char *logfile, struct scan_state *state)
{
    unsigned char compressed = 0;
    unsigned char buf[3];
    char cmdbuf[PATH_MAX];
    char *line = NULL;
    FILE *fp;
    int ch;
    int i;

    // Open file
    if (logfile == NULL)
        fp = stdin;
    else if ((fp = fopen(logfile, "r")) == NULL)
        err(EXIT_ERROR, "%s", logfile);

    // Check for compressed file and if so decode gzip/bzip2 on the fly
    if (logfile != NULL) {
        if (fread(buf, 3, 1, fp) == 1) {
            if ((buf[0] == 0x1f && buf[1] == 0x8b) || (buf[0] == 'B' && buf[1] == 'Z' && buf[2] == 'h')) {
                const char *cmd = buf[0] == 0x1f ? "gunzip" : "bunzip2";

                (void)fclose(fp);
                snprintf(cmdbuf, sizeof(cmdbuf), "%s -c '%s'", cmd, logfile);
                if ((fp = popen(cmdbuf, "r")) == NULL)
                    err(EXIT_ERROR, "can't invoke \"%s\"", cmdbuf);
                compressed = 1;
            }
        } else if (ferror(fp))
            err(EXIT_ERROR, "%s", logfile);
    }

    // Rewind to the beginning
    if (logfile != NULL && !compressed)
        rewind(fp);

    // Skip past lines already scanned
    if (state->pos != 0) {
        if (fseek(fp, state->pos, SEEK_CUR) == -1) {
            for (i = 1; i < state->line; i++) {
                while ((ch = getc(fp)) != EOF) {
                    if (ch == '\n')
                        break;
                }
                if (ch == EOF)
                    break;
            }
        }
    }

    // Allocate line buffer
    if ((line = malloc(MAX_LINE_LENGTH)) == NULL)
        err(EXIT_ERROR, "malloc: %d bytes", MAX_LINE_LENGTH);

    // Scan lines
    while (1) {
        unsigned char continuation;
        int newline = 0;
        int len;

        // Read next line, or up to MAX_LINE_LENGTH of it
        for (len = 0; len < MAX_LINE_LENGTH - 1; ) {
            if ((ch = getc(fp)) == -1) {
                line[len] = '\0';
                break;
            }
            if (ch == '\n') {
                line[len++] = '\0';     // count newline but omit from buffer
                newline = 1;
                break;
            }
            line[len++] = ch;
        }

        // End of file?
        if (len == 0 || (len < MAX_LINE_LENGTH - 1 && !newline)) {
            save_state(state_file, logfile, state);
            break;
        }

        // Bump position and number of lines read
        state->pos += len;
        state->line++;

        // Is this a new log entry or a continuation line?
        continuation = log_pattern.string != NULL && regexec(&log_pattern.regex, line, 0, NULL, 0) != 0;

        // Does this line match? New log entries only.
        if (!continuation) {
            int matches = -1;

            for (i = 0; i < num_match_patterns; i++) {
                const struct repat *const pat = &match_patterns[i];

                if (regexec(&pat->regex, line, 0, NULL, 0) == 0) {
                    matches = pat->negate ? 0 : 1;
                    break;
                }
            }
            if (matches == -1)
                matches = default_match;
            state->matching = matches;
        }

        // Output line if it matches
        if (state->matching) {
            any_matches = 1;
            if (!quiet)
                printf("%s\n", line);
        }
    }

    // Free buffer
    free(line);

    // Close file
    if (compressed) {
        if (pclose(fp) == -1)
            err(EXIT_ERROR, "pclose: %s", logfile);
    } else {
        if (logfile != NULL && fclose(fp) == EOF)
            err(EXIT_ERROR, "fclose: %s", logfile);
    }
}

static void
parse_pattern(struct repat *pat, const char *string)
{
    char ebuf[1024];
    int r;

    if ((r = regcomp(&pat->regex, string, REG_EXTENDED|REG_NOSUB)) != 0) {
        regerror(r, &pat->regex, ebuf, sizeof(ebuf));
        warnx("invalid regular expression \"%s\": %s", string, ebuf);
        exit(EXIT_ERROR);
    }
    pat->string = string;
}

static void
usage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  logwarn [-d dir | -f file] [-m firstpat] [-r sufpat] [-ahnqpv] logfile [!]pattern ...\n");
    fprintf(stderr, "  logwarn [-d dir | -f file] -i logfile\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -a    Auto-init: force `-i' if no state file exists\n");
    fprintf(stderr, "  -d    Specify state directory; default \"%s\"\n", DEFAULT_STATE_DIR);
    fprintf(stderr, "  -f    Specify state file directly\n");
    fprintf(stderr, "  -h    Output this help message and exit\n");
    fprintf(stderr, "  -i    Initialize state as `up to date' (implies -n)\n");
    fprintf(stderr, "  -m    Enable multi-line support; first lines start with firstpat\n");
    fprintf(stderr, "  -n    A nonexistent log file is not an error; treat as empty\n");
    fprintf(stderr, "  -q    Don't output the matched log messages\n");
    fprintf(stderr, "  -r    Specify rotated file suffix pattern; default \"%s\"\n", DEFAULT_ROTPAT);
    fprintf(stderr, "  -v    Output version information and exit\n");
    fprintf(stderr, "  -z    Always read from the beginning of the input\n");
    fprintf(stderr, "A logfile of `-' means read from standard input (typically used with `-z')\n");
}

static void
version(void)
{
    fprintf(stderr, "%s version %s (r%s)\n", PACKAGE_TARNAME, PACKAGE_VERSION, logwarn_svnrev);
    fprintf(stderr, "Copyright (C) 2010-2011 Archie L. Cobbs\n");
    fprintf(stderr, "This is free software; see the source for copying conditions.  There is NO\n");
    fprintf(stderr, "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
}

