
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
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "logwarn.h"

// Definitions
#ifndef DEFAULT_ROTPAT
#define DEFAULT_ROTPAT          "^(-[[:digit:]]{8}|\\.[01])(\\.(gz|xz|bz2))?$"
#endif

#ifndef DEFAULT_STATE_DIR
#define DEFAULT_STATE_DIR       "/var/lib/logwarn"
#endif

struct repat {
    const char      *string;
    regex_t         regex;
    unsigned char   negate;
    struct repeat   *repeat;
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
static int          match_last_rotated;
static int          quiet;
static int          any_matches;
static int          line_numbers;
static unsigned int error_count;
static unsigned int line_count;
static unsigned int max_errors_processed = UINT_MAX;
static unsigned int max_errors_output = UINT_MAX;
static unsigned int max_lines_output = UINT_MAX;
static struct repat *match_patterns;

// Internal functions
static void scan_file(const char *file, struct scan_state *state);
static void parse_pattern(struct repat *pat, const char *string, int eflags);
static void version(void);
static void usage(void);

int
main(int argc, char **argv)
{
    struct scan_state state;
    const char *logfile;
    struct stat sb;
    const char *rotpat = DEFAULT_ROTPAT;
    const char *mpat = NULL;
    char *eptr;
    int ignore_nonexistent = 0;
    int eflags = 0;
    int initialize = 0;
    int envset;
    int i;

    // Initialize state
    memset(&state, 0, sizeof(state));
    state.line = 1;

    // Make getopt() stop at the first non-flag argument
    if ((envset = (getenv("POSIXLY_CORRECT") == NULL)))
        setenv("POSIXLY_CORRECT", "", 1);

    // Parse command line
    while ((i = getopt(argc, argv, "acd:f:hilL:m:M:N:npqRr:tvz")) != -1) {
        switch (i) {
        case 'a':
            auto_initialize = 1;
            break;
        case 'c':
            eflags |= REG_ICASE;
            break;
        case 'd':
            state_dir = optarg;
            break;
        case 'f':
            state_file = optarg;
            break;
        case 'm':
            mpat = optarg;
            break;
        case 'l':
            line_numbers = 1;
            break;
        case 'L':
            max_lines_output = (unsigned int)strtoul(optarg, &eptr, 10);
            if (*optarg == '\0' || *eptr != '\0') {
                fprintf(stderr, "%s: invalid argument `%s' to `-%c' flag\n", PACKAGE, optarg, i);
                exit(EXIT_ERROR);
            }
            break;
        case 'M':
            max_errors_output = (unsigned int)strtoul(optarg, &eptr, 10);
            if (*optarg == '\0' || *eptr != '\0') {
                fprintf(stderr, "%s: invalid argument `%s' to `-%c' flag\n", PACKAGE, optarg, i);
                exit(EXIT_ERROR);
            }
            break;
        case 'N':
            max_errors_processed = (unsigned int)strtoul(optarg, &eptr, 10);
            if (*optarg == '\0' || *eptr != '\0' || max_errors_processed == 0) {
                fprintf(stderr, "%s: invalid argument `%s' to `-%c' flag\n", PACKAGE, optarg, i);
                exit(EXIT_ERROR);
            }
            break;
        case 'r':
            rotpat = optarg;
            break;
        case 'R':
            match_last_rotated = 1;
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
    if (envset)
        unsetenv("POSIXLY_CORRECT");
    if (mpat != NULL)
        parse_pattern(&log_pattern, mpat, eflags);
    argv += optind;
    argc -= optind;
    switch (argc) {
    case 0:
        usage();
        exit(EXIT_ERROR);
    default:

        // Get log file
        logfile = argv[0];
        if (strcmp(logfile, "-") == 0)
            logfile = NULL;
        argv++;
        argc--;

        // If initializing, no patterns should be given
        if (initialize) {
            if (argc > 0) {
                usage();
                exit(EXIT_ERROR);
            }
            break;
        }

        // Allocate pattern array
        if ((match_patterns = malloc(argc * sizeof(*match_patterns))) == NULL
          || (state.repeats = malloc(argc * sizeof(*state.repeats))) == NULL) {
            fprintf(stderr, "%s: %s: %s\n", PACKAGE, "malloc", strerror(errno));
            exit(EXIT_ERROR);
        }
        memset(match_patterns, 0, argc * sizeof(*match_patterns));
        memset(state.repeats, 0, argc * sizeof(*state.repeats));

        // Parse patterns and `-T' flags
        for (i = 0; i < argc; i++) {
            struct repat *const pat = &match_patterns[num_match_patterns];
            char *patstr = argv[i];

            // Add new repeat?
            if (strcmp(patstr, "-T") == 0) {
                struct repeat *const repeat = &state.repeats[state.num_repeats];

                if (++i >= argc || sscanf(argv[i], "%u/%u", &repeat->num, &repeat->secs) != 2) {
                    usage();
                    exit(EXIT_ERROR);
                }
                if (repeat->num == 0) {
                    fprintf(stderr, "%s: invalid zero repeat count in \"-T %s\"", PACKAGE, argv[i]);
                    exit(EXIT_ERROR);
                }
                state.num_repeats++;
                if ((repeat->occurrences = malloc(repeat->num * sizeof(repeat->occurrences))) == NULL) {
                    fprintf(stderr, "%s: %s: %s\n", PACKAGE, "malloc", strerror(errno));
                    exit(EXIT_ERROR);
                }
                memset(repeat->occurrences, 0, repeat->num * sizeof(*repeat->occurrences));
                continue;
            }

            // It's a new pattern
            num_match_patterns++;

            // Check for negation
            if (*patstr == '!') {
                patstr++;
                pat->negate = 1;
            }

            // Add (positive) pattern to the current repeat, if any
            if (!pat->negate && state.num_repeats > 0) {
                struct repeat *const current_repeat = &state.repeats[state.num_repeats - 1];
                unsigned int hash = 0;
                const char *s;

                for (s = patstr; *s != '\0'; s++)
                    hash = hash * 37 + (unsigned char)*s;
                current_repeat->hash ^= hash;
                pat->repeat = current_repeat;
            }

            // Parse pattern
            parse_pattern(pat, patstr, eflags);
        }
        break;
    }

    // Check "-d" vs. "-f" and determine state file
    if (state_dir != NULL && state_file != NULL) {
        fprintf(stderr, "%s: specify only one of `-d' and `-f'\n", PACKAGE);
        exit(EXIT_ERROR);
    }
    if (state_file == NULL) {
        if (state_dir == NULL)
            state_dir = DEFAULT_STATE_DIR;
        if ((state_file = malloc(PATH_MAX)) == NULL) {
            fprintf(stderr, "%s: %s: %s\n", PACKAGE, "malloc", strerror(errno));
            exit(EXIT_ERROR);
        }
        state_file_name(state_dir, logfile, state_file, PATH_MAX);
    }

    // Parse rotated file pattern
    parse_pattern(&rot_pattern, rotpat, 0);

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
            fprintf(stderr, "%s: %s: %s\n", PACKAGE, logfile, strerror(errno));
            exit(EXIT_ERROR);
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
        if ((temp = strdup(logfile)) == NULL || (bname = strdup(basename(temp))) == NULL) {
            fprintf(stderr, "%s: %s: %s\n", PACKAGE, "strdup", strerror(errno));
            exit(EXIT_ERROR);
        }
        free(temp);
        if ((temp = strdup(logfile)) == NULL || (dname = strdup(dirname(temp))) == NULL) {
            fprintf(stderr, "%s: %s: %s\n", PACKAGE, "strdup", strerror(errno));
            exit(EXIT_ERROR);
        }
        free(temp);

        // Search for rotated version in same directory
        if ((dir = opendir(dname)) != NULL) {
            const size_t bnamelen = strlen(bname);
            struct dirent *ent;

            for (ent = readdir(dir); ent != NULL; ent = readdir(dir)) {
                int prefer;
                int diff;

                // Rotated file must have logfile name as prefix
                if (strncmp(ent->d_name, bname, bnamelen) != 0)
                    continue;

                // Skip the file itself
                if (ent->d_name[bnamelen] == '\0')
                    continue;

                // Compare rotated file against pattern
                if (regexec(&rot_pattern.regex, ent->d_name + bnamelen, 0, NULL, 0) != 0)
                    continue;

                // It's a candidate. Pick the first (or last) one in sorting order.
                if (rotated == NULL)
                    prefer = 1;
                else {
                    diff = strcmp(ent->d_name, rotated);
                    prefer = match_last_rotated ? diff > 0 : diff < 0;
                }
                if (prefer) {
                    if ((rotated = realloc(rotated, strlen(ent->d_name) + 1)) == NULL) {
                        fprintf(stderr, "%s: %s: %s\n", PACKAGE, "realloc", strerror(errno));
                        exit(EXIT_ERROR);
                    }
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

    // Check whether the file has been truncated in place
    if (logfile != NULL && state.pos > (long)sb.st_size) {
        state.line = 1;
        state.pos = 0;
        state.matching = 0;
    }

    // Now scan the logfile itself
    scan_file(logfile, &state);

    // Done
    exit(any_matches ? EXIT_MATCHES : EXIT_OK);
}

static void
scan_file(const char *logfile, struct scan_state *state)
{
    unsigned char compressed = 0;
    char cmdbuf[PATH_MAX];
    char *line = NULL;
    FILE *fp;
    int ch;
    int i;

    // Open file
    if (logfile == NULL)
        fp = stdin;
    else if ((fp = fopen(logfile, "r")) == NULL) {
        fprintf(stderr, "%s: %s: %s\n", PACKAGE, logfile, strerror(errno));
        exit(EXIT_ERROR);
    }

    // Check for compressed file and if so decode gzip/xz/bzip2 on the fly
    if (logfile != NULL) {
        const char *cmdfmt = NULL;
        unsigned char magic[6];

        if (fread(magic, sizeof(magic), 1, fp) == 1) {
            if (magic[0] == 0x1f && magic[1] == 0x8b)
                cmdfmt = "gunzip -c '%s'";
            else if (magic[0] == 'B' && magic[1] == 'Z' && magic[2] == 'h')
                cmdfmt = "bunzip2 -c '%s'";
            else if (magic[0] == 0xfd && magic[1] == 0x37 && magic[2] == 0x7a
              && magic[3] == 0x58 && magic[4] == 0x5a && magic[5] == 0x00)
                cmdfmt = "unxz -c '%s'";
            if (cmdfmt != NULL) {
                (void)fclose(fp);
                snprintf(cmdbuf, sizeof(cmdbuf), cmdfmt, logfile);
                if ((fp = popen(cmdbuf, "r")) == NULL) {
                    fprintf(stderr, "%s: can't invoke \"%s\": %s\n", PACKAGE, cmdbuf, strerror(errno));
                    exit(EXIT_ERROR);
                }
                compressed = 1;
            }
        } else if (ferror(fp)) {
            fprintf(stderr, "%s: %s: %s\n", PACKAGE, logfile, strerror(errno));
            exit(EXIT_ERROR);
        }
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
    if ((line = malloc(MAX_LINE_LENGTH)) == NULL) {
        fprintf(stderr, "%s: %s: %s\n", PACKAGE, "malloc", strerror(errno));
        exit(EXIT_ERROR);
    }

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
        if (len == 0 || (len < MAX_LINE_LENGTH - 1 && !newline))
            break;

        // Is this a new log entry or a continuation line?
        continuation = log_pattern.string != NULL && regexec(&log_pattern.regex, line, 0, NULL, 0) != 0;

        // If this is not a continuation, check if we have reached our limit on the number of errors processed
        if (!continuation && error_count >= max_errors_processed)
            break;

        // Bump position and number of lines read
        state->pos += len;
        state->line++;

        // Does this line match? New log entries lines only.
        if (!continuation) {
            int matches = -1;

            // Determine if this line matches
            for (i = 0; i < num_match_patterns; i++) {
                const struct repat *const pat = &match_patterns[i];
                struct repeat *const repeat = pat->repeat;

                if (regexec(&pat->regex, line, 0, NULL, 0) == 0) {

                    // Check for repeat suppression
                    if (repeat != NULL) {
                        time_t now;
                        int count;

                        // Update timestamps by adding the current timestamp to the front of the array
                        time(&now);
                        memmove(repeat->occurrences + 1, repeat->occurrences, (repeat->num - 1) * sizeof(*repeat->occurrences));
                        repeat->occurrences[0] = (unsigned long)now;

                        // Check whether the repeat threshold has been exceeded
                        for (count = 0; count < repeat->num && repeat->occurrences[count] != 0; count++) {
                            const unsigned int age = repeat->occurrences[0] - repeat->occurrences[count];

                            if (age > repeat->secs)
                                break;
                        }

                        // If not, treat like a non-matching line
                        if (count < repeat->num) {
                            matches = 0;
                            break;
                        }

                        // If so, reset occurrence history for this pattern group
                        memset(repeat->occurrences, 0, repeat->num * sizeof(*repeat->occurrences));
                    }

                    // No repeat suppression
                    matches = pat->negate ? 0 : 1;
                    break;
                }
            }
            if (matches == -1)
                matches = default_match;

            // Update error count
            if (matches)
                error_count++;

            // Update matching state
            state->matching = matches;
        }

        // Reset line counter
        if (!continuation)
            line_count = 0;

        // Output line if it matches
        if (state->matching) {

            // Update flag
            any_matches = 1;

            // Output line if appropriate
            if (!quiet && line_count < max_lines_output && error_count <= max_errors_output) {
                if (line_numbers)
                    printf("%ld:", state->line - 1);
                printf("%s\n",line);
            }

            // Update line and error counters
            line_count++;
        }
    }

    // Save updated state
    save_state(state_file, logfile, state);

    // Free buffer
    free(line);

    // Close file
    if (compressed) {
        if (pclose(fp) == -1) {
            fprintf(stderr, "%s: %s: %s: %s\n", PACKAGE, "pclose", logfile, strerror(errno));
            exit(EXIT_ERROR);
        }
    } else {
        if (logfile != NULL && fclose(fp) == EOF) {
            fprintf(stderr, "%s: %s: %s: %s\n", PACKAGE, "fclose", logfile, strerror(errno));
            exit(EXIT_ERROR);
        }
    }
}

static void
parse_pattern(struct repat *pat, const char *string, int eflags)
{
    char ebuf[1024];
    int r;

    if ((r = regcomp(&pat->regex, string, REG_EXTENDED|REG_NOSUB|eflags)) != 0) {
        regerror(r, &pat->regex, ebuf, sizeof(ebuf));
        fprintf(stderr, "%s: invalid regular expression \"%s\": %s", PACKAGE, string, ebuf);
        exit(EXIT_ERROR);
    }
    pat->string = string;
}

static void
usage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  logwarn [-d dir | -f file] [-m firstpat] [-r sufpat] [-L maxlines]\n");
    fprintf(stderr, "          [-M maxprint] [-N maxerrors] [-achlnqpvz] logfile [-T num/secs] [!]pattern ...\n");
    fprintf(stderr, "  logwarn [-d dir | -f file] -i logfile\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -a    Auto-init: force `-i' if no state file exists\n");
    fprintf(stderr, "  -c    Match patterns (and firstpat) case-insensitively\n");
    fprintf(stderr, "  -d    Specify state directory; default \"%s\"\n", DEFAULT_STATE_DIR);
    fprintf(stderr, "  -f    Specify state file directly\n");
    fprintf(stderr, "  -h    Output this help message and exit\n");
    fprintf(stderr, "  -i    Initialize state as `up to date' (implies -n)\n");
    fprintf(stderr, "  -L    Specify maximum number of lines to output per log message\n");
    fprintf(stderr, "  -l    Prefix each output line with the line number from the log file\n");
    fprintf(stderr, "  -m    Enable multi-line support; first lines start with firstpat\n");
    fprintf(stderr, "  -M    Specify maximum number of log messages to output\n");
    fprintf(stderr, "  -N    Specify maximum number of log messages to process\n");
    fprintf(stderr, "  -n    A nonexistent log file is not an error; treat as empty\n");
    fprintf(stderr, "  -q    Don't output the matched log messages\n");
    fprintf(stderr, "  -r    Specify rotated file suffix pattern; default \"%s\"\n", DEFAULT_ROTPAT);
    fprintf(stderr, "  -T    Suppress until `num' occurrences within `secs' seconds\n");
    fprintf(stderr, "  -v    Output version information and exit\n");
    fprintf(stderr, "  -z    Always read from the beginning of the input\n");
    fprintf(stderr, "A logfile of `-' means read from standard input (typically used with `-z')\n");
}

static void
version(void)
{
    fprintf(stderr, "%s version %s (%s)\n", PACKAGE, VERSION, logwarn_version);
    fprintf(stderr, "Copyright (C) 2010-2016 Archie L. Cobbs\n");
    fprintf(stderr, "This is free software; see the source for copying conditions.  There is NO\n");
    fprintf(stderr, "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
}

