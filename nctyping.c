 /*NCTYPING**********************************************
 * Ncurses program for typing along with files.         *
 * written by: Andrew Farabee (pasca1)                  *
 *             me@andrewfarabee.com                     *
 *                                                      *
 * INSTALL using "gcc -o nctyping nctyping.c -lncurses" *
 *                                                      *
 * ISSUES: stdin not working from pipe                  *
 *         time() is nonmonotonic and inaccurate        *
 *         wrapped lines are displayed and stored wrong *
 *         bug when character @ width is a newline      *
 *         tab characters are being treated as spaces   *
 *         no newline if inline co follows typed text   *
 *******************************************************/

#include <ncurses.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>


/* these flags are used as a mask in *flags to monitor buffer states */
enum flags {
    TYPED = 1,
    COMMENT = 2,
    ERROR = 4,
    MISTAKE1 = 8,
    MISTAKE2 = 16,
    NEWLINE = 32
};

/* Can be used with bitwise operators to assign masks to each file extension */
enum CommentMask {
    DOUBLESLASHINLINE = 1,
    SINGLEHASHINLINE = 2,
    SLASHSTARBLOCK = 256,
    ANGLEHASHBLOCK = 512,
    TRIPLESQUOTEBLOCK = 1024,
    TRIPLEDQUOTEBLOCK = 2048
};

/* structure for returning results of each "typing" */
struct scoring {
    int right;
    int wrong;
    int time;
};

/* Used my markComments to know length of each comment field and whitespace */
int commentLength(const char *buffer, int *i, const char *open,
                  const char *close) {
    int comment_len = 0;
    char *pos;
    if (!strncmp(buffer + (*i), open, strlen(open))) {
        /* This is the start of an inline comment */
        pos = strstr((buffer + (*i) + 1), close);
        if (pos) {
            comment_len = (pos + strlen(close)) - (buffer + (*i));
        }
        /* mark whitespace after comment as part of the comment */
        comment_len += strspn(buffer + (*i) + comment_len, " \t\n");
        /* mark  whitespace before comment as part of the comment */
        while (buffer[(*i) - 1] == ' ' || buffer[(*i) - 1] == '\n') {
            (*i)--;
            comment_len++;
        }
        if (buffer[*i] == '\n') {
            comment_len--;
            (*i)++;
        }
    }
    return comment_len;
}

/* estimates the comment syntax for a file based on filename and contents */
unsigned short int commentType(char *filename, const char *buffer) {
    unsigned short int syntax = 0;

    char *ext = filename;
    while (*ext != '.' && *ext) ext++;

    if (*ext == '.') ext++;

    /* Syntax mask so far:
     * 0-bit = // inline
     * 1-bit = # inline
     * 8-bit = / * to * / block (without spaces)
     * 9-bit = <# to #> block
     * 10-bit = ''' block
     * 11-bit = """ block
     */
    if (!strcmp(ext, "c") || !strcmp(ext, "h") || !strcmp(ext, "cc") ||
        !strcmp(ext, "cpp") || !strcmp(ext, "cxx") || !strcmp(ext, "hpp") ||
        !strcmp(ext, "c++") || !strcmp(ext, "cs") || !strcmp(ext, "java") ||
        !strcmp(ext, "rs") || !strcmp(ext, "rlib") || !strcmp(ext, "d") ||
        !strcmp(ext, "js")) {
        syntax = DOUBLESLASHINLINE | SLASHSTARBLOCK;
    } else if (!strncmp(buffer, "#!/usr/bin/env py",
                        strlen("#!/usr/bin/env py")) ||
               !strcmp(ext, "py") || !strcmp(ext, "pyc") ||
               !strcmp(ext, "pyd") || !strcmp(ext, "pyo") ||
               !strcmp(ext, "pyw") || !strcmp(ext, "pyz")) {
        syntax = SINGLEHASHINLINE | TRIPLESQUOTEBLOCK | TRIPLEDQUOTEBLOCK;
    } else if (!strncmp(buffer, "#!/usr/bin/env php",
                        strlen("#!/usr/bin/env php")) ||
               !strcmp(ext, "php") || !strcmp(ext, "phtml") ||
               !strcmp(ext, "php3") || !strcmp(ext, "php4") ||
               !strcmp(ext, "php5") || !strcmp(ext, "php7") ||
               !strcmp(ext, "phps")) {
        syntax = DOUBLESLASHINLINE | SINGLEHASHINLINE | SLASHSTARBLOCK;
    } else if (!strncmp(buffer, "#!/bin/bash", strlen("#!/bin/bash")) ||
               !strncmp(buffer, "#!/bin/sh", strlen("#!/bin/sh")) ||
               !strncmp(buffer, "#!/bin/csh", strlen("#!/bin/csh")) ||
               !strncmp(buffer, "#!/usr/bin/awk", strlen("#!/usr/bin/awk")) ||
               !strcmp(ext, "bash") || !strcmp(ext, "tcl") ||
               !strcmp(ext, "csh") || !strcmp(ext, "mpl") ||
               !strcmp(ext, "mla") || !strcmp(ext, "ps1") ||
               !strcmp(ext, "m") || !strcmp(ext, "r") || !strcmp(ext, "sh") ||
               !strncmp(buffer, "#!/usr/bin/Rscript",
                        strlen("#!/usr/bin/Rscript"))) {
        syntax = SINGLEHASHINLINE | ANGLEHASHBLOCK;
    } else {
        syntax = 0;
    }
    return syntax;
}

/* toggles flags for comment fields based on interpretation of the file lang */
void markComments(char *filename, const char *buffer, char *flags, int size,
                  bool ignoreComments) {
    unsigned short int syntax = commentType(filename, buffer);
    char* pos;
    int i;
    int comment_len = 0;
    for (i = 0; i < size; i++) {
        if (!ignoreComments) {
            if (syntax & DOUBLESLASHINLINE && !comment_len) {
                comment_len = commentLength(buffer, &i, "//", "\n");
            }
            if (syntax & SINGLEHASHINLINE && !comment_len) {
                comment_len = commentLength(buffer, &i, "#", "\n");
            }
            if (syntax & SLASHSTARBLOCK && !comment_len) {
                comment_len = commentLength(buffer, &i, "/*", "*/");
            }
            if (syntax & ANGLEHASHBLOCK && !comment_len) {
                comment_len = commentLength(buffer, &i, "<#", "#>");
            }
            if (syntax & TRIPLESQUOTEBLOCK && !comment_len) {
                comment_len = commentLength(buffer, &i, "'''", "'''");
            }
            if (syntax & TRIPLEDQUOTEBLOCK && !comment_len) {
                comment_len = commentLength(buffer, &i, "\"\"\"", "\"\"\"");
            }
        }
        if (comment_len) {
            flags[i] |= COMMENT;
            comment_len--;
        }
        /* Mark isolated white space as comments as well */
        if (!comment_len && buffer[i] == '\n') {
            comment_len = strspn(buffer + (i + 1), " \n\t");
        }
        if (!comment_len && buffer[i] == ' ') {
            comment_len = strspn(buffer + (i + 1), " \n\t");
        }
    }
}

/* fills the screen with the char filler, current_color only maintains state */
void clearscreen(int height, int width, int current_color, int filler) {
    if (current_color)
        attroff(COLOR_PAIR(current_color));
    int x, y;
    for (x = 0; x < width; x++) {
        for (y = 0; y < height; y++) {
            mvaddch(y, x, filler);
        }
    }
    if (current_color)
        attron(COLOR_PAIR(current_color));
}

/* returns color pair for typed chars based on the time it took to type them */
int colortiming(int diff) {
    if (diff > 2) {
        return 6;
    } else if (diff > 1) {
        return 5;
    } else {
        return 4;
    }
}

/* reads typeable content from a file and populates the buffer for typing() */
int file_pop(char *filename, char **buffer, char **flags) {
    FILE *fd;
    int i = 0;
    char sub;
    int size;
    fd = fopen(filename, "r");
    if (fd == NULL) {
        perror("Error opening file");
        return i;
    }

    /* getting the file size */
    if (strcmp(filename, "/dev/stdin")) {
        fseek(fd, 0L, SEEK_END);
        size = ftell(fd);
        fseek(fd, 0L, SEEK_SET);
    } else {
    /* need a constant size for a stdin buffer because fseek isn't possible */
        size = 1024 * 1024;
    }

    *buffer = malloc(size);
    *flags = malloc(size);
    if (!(*buffer) || !(*flags)) {
        perror("Error allocating memory for file buffer");
        return i;
    }

    memset(*flags, 0, size);

    while (!feof(fd) && i < size) {
        sub = fgetc(fd);
        if (sub == '\n' || (sub > 31 && sub < 127)) {
            if (sub == '\n') (*flags)[i] |= NEWLINE;
            (*buffer)[i] = sub;
            i++;
        /* tabs are treated as spaces for simplicity */
        } else if (sub == '\t') {
            (*buffer)[i] = ' ';
            i++;
        }
    }
    if (fclose(fd) == EOF) {
        perror("Error closing file");
    }
    return i;
}

/* Where almost all the action happens, displays a screen from the buffer and
 * collects results as the user types along with it
 *
 * buffer: a single string of the entire file
 * flags: a string of flags corresponding to the chars at that index in buffer
 * size: size of buffer string
 * begin: where to start typing in buffer
 * height, width: useable screen dimensional
 * filename: a string containing the name of the file in buffer
 * score: structure that is used to return typing stats (right, wrong, time)
 * RETURNS: how much of the buffer was completed before moving to next screen
 */
int typing(const char *buffer, char *flags, int size, int begin, int height,
           int width, char* filename, struct scoring *score) {
    /* start: time first key is typed, last: time of last correct keystroke */
    time_t start, last;
    bool isStarted = false;
    char *xs;

    if (height * width < size - begin) {
        xs = malloc(height * width);
        memset(xs, 0, height * width);
    } else {
        xs = malloc(size - begin);
        memset(xs, 0, size - begin);
    }

    /* x, y mark the user cursor
     * xt, is for secondary drawing when x, y can't move
     * i marks where the user cursor is in the file buffer
     * screen_start follows where the start of the screen was drawn */
    int x, y, xt, i, screen_start;
    char sub;       /* stores the user inputted character from keyboard */
    int streak = 0; /* how far the user has to backspace to correct typo */
    int right = 0;  /* total correct keystrokes */
    int wrong = 0;  /* total incorrect keystrokes */
    int used = 0;   /* # chars must be typed on this screen TODO remove */

    /* Initializing the ncurses screen */
    initscr();
    cbreak();
    noecho();

    /* Initializing color schemes */
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);    /* to be typed */
    init_pair(2, COLOR_BLACK, COLOR_MAGENTA);  /* typing cursor */
    init_pair(3, COLOR_BLACK, COLOR_RED);      /* mistake hilight */
    init_pair(4, COLOR_CYAN, COLOR_BLACK);     /* fast match */
    init_pair(5, COLOR_GREEN, COLOR_BLACK);    /* medium match */
    init_pair(6, COLOR_YELLOW, COLOR_BLACK);   /* slow match */
    init_pair(7, COLOR_BLACK, COLOR_WHITE);    /* newline char */
    init_pair(8, COLOR_BLUE, COLOR_BLACK);     /* commmented code */

    x = 1;
    y = 0;

    /* Draw start of buffer */
    attron(COLOR_PAIR(1));
    i = begin;
    screen_start = begin;
    while (i < size && y < height - 3) {
        if (i == begin && flags[i] & COMMENT) {
            begin++;
        }
        xs[i - screen_start] = x;
        if (flags[i] & COMMENT) {
            attroff(COLOR_PAIR(1));
            attron(COLOR_PAIR(8));
        }
        if (buffer[i] == '\n' || x >= width) {
            x = 1;
            y++;
        }
        if (buffer[i] != '\n') {
            mvaddch(y, x, buffer[i]);
            x++;
        }
        if (flags[i] & COMMENT) {
            attroff(COLOR_PAIR(8));
            attron(COLOR_PAIR(1));
        }
        i++;
    }
    used = i;
    attroff(COLOR_PAIR(1));

    /* draw bottom border */
    y = height - 2;
    attron(COLOR_PAIR(2));
    for (x = 0; x < width; x++) {
        mvaddch(y, x, ACS_CKBOARD);
    }
    mvprintw(y, (width - strlen(filename)) / 2, "%s", filename);
    attroff(COLOR_PAIR(2));

    x = 1;
    y = 0;

    i = screen_start;
    /* Check if user types key associated with cursor char
     *  if not, draw that character with red background */
    while (i < used || streak) {
        /* Skip over comments and whitespace */
        while (flags[i] & COMMENT) {
            i++;
            if (xs[i - screen_start] <= xs[(i - screen_start) - 1]) y++;
            x = xs[i - screen_start];

            /* handle issue with trailing typeable space after comments */
            if (!(i < used || streak)) {
                free(xs);
                endwin();
                score->right = right;
                score->wrong = wrong;
                score->time = time(NULL) - start;
                return i;
            }
        }

        /* draw typing cursor */
        if (!streak && !(flags[i] & COMMENT)) {
            attron(COLOR_PAIR(flags[i] & NEWLINE ? 7 : 2));
            mvaddch(y, x, flags[i] & NEWLINE ? 182 | A_ALTCHARSET : buffer[i]);
            attroff(COLOR_PAIR(flags[i] & NEWLINE ? 7 : 2));
            move(height - 1, width - 1);
        }

        /* GET USER INPUT */
        sub = getch();

        /* If user pressed ESCAPE, we collect results for results() */
        if (sub == 27) break;
        if (!isStarted) {
            isStarted = true;
            start = time(NULL);
            last = time(NULL);
        }
        /* handle backspace */
        if (sub == 127) {
            if (i > begin) {
                if (streak > 0) {
                    streak--;
                } else {
                    /* color correct text erased white */
                    attron(COLOR_PAIR(1));
                    mvaddch(y, x, buffer[i]);
                    attroff(COLOR_PAIR(1));
                }

                /* Move x and y, counting for newline */
                if (xs[(i - screen_start) - 1] >= xs[i - screen_start]) y--;
                x = xs[(i - screen_start) - 1];

                i--;

                /* Skip over comments */
                while (flags[i] & COMMENT) {
                    if (xs[(i - screen_start) - 1] >= xs[i - screen_start]) y--;
                    x = xs[(i - screen_start) - 1];
                    i--;
                }
                if (streak) {
                    /* Color wrong text erased white */
                    attron(COLOR_PAIR(1));
                    mvaddch(y, x, buffer[i]);
                    attroff(COLOR_PAIR(1));
                }
            }
        /* handle normal chars */
        } else if (i < used - 1 || !streak) {
            /* correct keystroke */
            if (sub == buffer[i] && streak == 0) {
                right++;
                attron(COLOR_PAIR(colortiming(time(NULL) - last)));
                mvaddch(y, x, buffer[i]);
                attroff(COLOR_PAIR(colortiming(time(NULL) - last)));
                last = time(NULL);
            /* wrong keystroke */
            } else {
                streak++;
                wrong++;
                attron(COLOR_PAIR(3));
                if (buffer[i] == '\n')
                    mvaddch(y, x, 182 | A_ALTCHARSET);
                else
                    mvaddch(y, x, buffer[i]);
                attroff(COLOR_PAIR(3));
            }
            i++;
            if (xs[i - screen_start] <= xs[(i - screen_start) - 1]) {
                y++;
            }
            x = xs[i - screen_start];
        } else {
            /* here we aren't allowing users to finish with a streak of errors
             * so lets redraw the bottom border in red to alert them */
            attron(COLOR_PAIR(3));
            for (xt = 0; xt < width; xt++) {
                mvaddch(height - 2, xt, ACS_CKBOARD);
            }
            move(height - 2, (width - strlen("FIX ERRORS TO CONTINUE")) / 2);
            printw("FIX ERRORS TO CONTINUE");
            attroff(COLOR_PAIR(3));
        }
        /* print stats at the bottom of the screen */
        move(height - 1, 0);
        printw("WPM: %3.2f\t\tAccuracy: %3.2f%%\t\tTime: %d:%02d",
               ((double)right / 5) / ((double)(time(NULL)-start) / 60),
               ((double)right/(right+wrong))*100, (time(NULL) - start) / 60,
               (time(NULL) - start) % 60);
        move(height - 1, width - 1);
    }
    free(xs);

    /* Exit TUI window */
    endwin();
    score->right = right;
    score->wrong = wrong;
    score->time = time(NULL) - start;
    return i;
}

/* updates the number associated with a filename in the save file */
int update_save(const char *filename, int newpos, const char *savepath) {
    FILE *fd;
    char subfile[256];
    fd = fopen(savepath, "r+");
    if (!fd) {
        return -1;
    }
    while (!feof(fd)) {
        fscanf(fd, "%s", subfile);
        if (!strncmp(filename, subfile + 1, strlen(filename))) {
            fprintf(fd, " %d\n", newpos);
            fflush(fd);
            close(fd);
            return 1;
        }
    }
    close(fd);
    return -1;
}

/* searches the save file ~/.nctyping-restore for an entry for "filename"
 * and returns the position associated with that entry.
 */
int search_save(const char *filename, const char *savepath) {
    FILE *fd;
    char subfile[256];
    char position[16];
    fd = fopen(savepath, "r");
    if (!fd) {
        return -1;
    }
    while (!feof(fd)) {
        fscanf(fd, "%s", subfile);
        fscanf(fd, "%s", position);
        if (!strncmp(filename, subfile + 1, strlen(filename))) {
            close(fd);
            return atoi(position);
        }
    }
    close(fd);
    return -1;
}

/* saves progress to the file ~/.nctyping-restore in the format
 * "filename" position
 * Currently filenames are local to the directory they are in, meaning
 * that files with the same name in different directories will be loaded
 * at different positions in the save file.
 */
int save_progress(const char *filename, int position, const char *savepath) {
    FILE *fd;
    if (update_save(filename, position, savepath) == -1) {
        /* need to add an entry for that filename to the end of the file */
        fd = fopen(savepath, "a");
        if (!fd) {
            return 0;
        }
        fprintf(fd, "\"%s\" %d\n", filename, position);
        fflush(fd);
        close(fd);
    }
    return 1;
}

/* displays the results of a section of typing
 * this is usually the results of a screen of text
 * but may also be triggered by user pressing ESCAPE as a sort of PAUSE
 */
void results(struct scoring *score, bool more, int height, int width,
             const char *filename, int begin, const char *savepath) {
    initscr();
    cbreak();
    noecho();
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_CYAN);
    int x;
    int y;
    char options[] = "[ENTER] Continue   [s] Save   [ESC] Exit";

    /* clear screen */
    clearscreen(height, width, 0, ACS_PLUS);

    /* print box */
    y = (height / 2) - 5;
    attron(COLOR_PAIR(1));
    for (x = (width / 2) - 30; x < (width / 2) + 30; x++) {
        mvaddch(y, x, ACS_CKBOARD);
    }
    for (y = (height / 2) - 4; y <= (height / 2) + 4; y++) {
        for (x = (width / 2) - 30; x < (width / 2) + 30; x++) {
            if (x < (width / 2) - 28 || x > (width / 2) + 27) {
                mvaddch(y, x, ACS_CKBOARD);
            } else {
                mvaddch(y, x, ' ');
            }
        }
    }
    for (x = (width / 2) - 30; x < (width / 2) + 30; x++) {
        mvaddch(y, x, ACS_CKBOARD);
    }

    /* print results */
    move((height / 2) - 3, (width / 2) - 13);
    printw("Words Per Minute:  %6.2f",
           ((double)score->right / 5) / ((double)score->time / 60));
    move((height / 2) - 1, (width / 2) - 13);
    printw("Accuracy        : %6.2f%%",
           ((double) score->right / (double)(score->right + score->wrong)) * 100);
    move((height / 2) + 1, (width / 2) - 13);
    printw("Total Keystrokes:  %6d", score->right + score->wrong);
    if (more) {
        move((height / 2) + 3, (width - strlen(options)) / 2);
        printw("%s", options);
    } else {
        move((height / 2) + 3, (width - strlen("Press [ENTER] to Exit")) / 2);
        printw("Press [ENTER] to Exit");
    }
    move(height - 1, width - 1);

    /* Wait for the user to press ENTER to continue */
    char sub = getch();
    while (sub != '\n') {
        /* User is trying to save */
        if (sub == 's') {
            move((height / 2) + 3, (width - strlen(options)) / 2);
            if (save_progress(filename, begin, savepath)) {
                strncpy(options + strlen("[ENTER] Continue   "), "Saved!!!", 8);
                printw("%s", options);
            } else {
                strncpy(options + strlen("[ENTER] Continue   "), "Failed!!", 8);
                printw("%s", options);
            }
            move(height - 1, width - 1);
        } else if (sub == 27) {
            /* exit on escape, will need a better way to do this since
             * allocated memory needs to be free()d */
            attroff(COLOR_PAIR(1));
            clearscreen(height, width, 0, ' ');
            endwin();
            exit(1);
        }
        sub = getch();
    }
    /* clear screen again */
    attroff(COLOR_PAIR(1));
    clearscreen(height, width, 0, ' ');
    endwin();
}

/* creates an absolute and unique filepath based on filename and working dir
 * file: envp entry for working directory (PWD) + filename, result stored here
 */
void simplify_filename(char *file) {
    char *j, *k;

    j = file; /* j keeps track of reading (forward) */
    k = file; /* k keeps track of writing (backward) */
    while (*j) {
        /* pre and post condition:
         * *j == '/' && *k == '/'
         * j's '/' is not written to k at the end of loop */
        if (!strncmp(j, "/../", 4)) {
            j += 3;
            if (k > file) k--;
            while (*k != '/') k--;
        } else if (!strncmp(j, "/./", 3)) {
            j += 2;
        } else if (!strncmp(j, "//", 2)) {
            j += 1;
        } else {
            do {
                *k = *j;
                k++; j++;
            } while (*j != '/' && *j);
        }
    }
    *k = '\0';
}

/* Just a wrapper function for handling splitting the buffer up into screens */
void running(int argc, char **argv, char **envp) {
    struct winsize w;
    struct scoring score;
    char *buffer, *flags, *filename, *savepath;
    int size, res;
    int pwd = -1;
    int i = 0;
    bool ignoreComments = false;

    /* this loop finds the HOME option in **envp to find paths */
    while (envp[i] && (savepath[0] == 0 || pwd == -1)) {
        /* find absolute path for filename based on pwd */
        if (!strncmp("PWD=", envp[i], 4)) {
            pwd = i;
        /* use the home directory to find where to create a save file */
        } else if (!strncmp("HOME=", envp[i], 5)) {
            savepath = malloc(strlen(envp[i]) + strlen("/.nctyping-restore"));
            strcpy(savepath, envp[i] + 5);
            strcpy(savepath + strlen(savepath), "/.nctyping-restore");
        }
        i++;
    }
    /* if we can't create a save path, try /dev/null */
    if (!savepath) {
        perror("envp HOME entry missing, saving not possible");
        savepath = malloc(strlen("/dev/null"));
        strcpy(savepath, "/dev/null");
    }

    i = 0;
    /* this for loop will take us through each file to be typed */
    for (i = 1; i < argc; i++) {
        /* check if we want to avoid comment syntax recognition */
        if (!strcmp(argv[i], "-c")) {
            ignoreComments = true;
            if (i < argc - 1) {
                i++;
            } else {
                return;
            }
        }
        /* check if first arg was '-s' */
        if (!strcmp(argv[i], "-s")) {
            size = file_pop("/dev/stdin", &buffer, &flags);
            filename = malloc(strlen("/dev/stdin"));
            strcpy(filename, "/dev/stdin");
        } else {
            size = file_pop(argv[i], &buffer, &flags);
            /* if PWD was used in filename, append filename to the end.
             * assume PWD didn't have any /../ or /./ entries */
            if (pwd == -1 || argv[i][0] == '/') {
                filename = malloc(strlen(argv[i]) + 1);
                strcpy(filename, argv[i]);
            } else {
                filename = malloc(strlen(envp[pwd]) + strlen(argv[i]) - 2);
                strcpy(filename, envp[pwd] + 4);
                strcat(filename, "/");
                strcat(filename, argv[i]);
            }
            simplify_filename(filename);
        }

        /* Search for start position from save file */
        res = search_save(filename, savepath);
        if (res == -1) res = 0;

        markComments(filename, buffer, flags, size, ignoreComments);

        ioctl(0,TIOCGWINSZ,&w);
        res = typing(buffer, flags, size, res, w.ws_row,
                         w.ws_col > 255 ? 256 : w.ws_col, filename, &score);
        while (res < size - 1) {
            ioctl(0,TIOCGWINSZ,&w);
            results(&score, true, w.ws_row, w.ws_col > 255 ? 256 : w.ws_col,
                    filename, res, savepath);
            ioctl(0,TIOCGWINSZ,&w);
            res = typing(buffer, flags, size, res, w.ws_row,
                         w.ws_col > 255 ? 256 : w.ws_col, filename, &score);
        }
        results(&score, i < argc - 1, w.ws_row, w.ws_col > 255 ? 256 : w.ws_col,
                filename, res, savepath);
        free(buffer);
        free(filename);
        ignoreComments = false;
    }
    free(savepath);
}

int main(int argc, char **argv, char **envp) {
    if (argc < 2) {
        printf("Usage: %s [-s] [filename] ... [filename]\n", argv[0]);
        return 0;
    }
    running(argc, argv, envp);

    return 0;
}
