/**NCTYPING**********************************************
 * Ncurses program for typing along with files.         *
 * written by: Andrew Farabee (pasca1)                  *
 *             me@andrewfarabee.com                     *
 *                                                      *
 * INSTALL using "gcc -o nctyping nctyping.c -lncurses" *
 *                                                      *
 * ISSUES: no scrolling down page                       *
 *         stdin not working from pipe                  *
 *         time() is nonmonotonic and inaccurate        *
 *         should recognize and skip code comments      *
 *         bug when character 80 is a newline           *
 *         tab characters are being treated as spaces   *
 *         wrapped lines are displayed and stored wrong *
 *         can't backspace past a comment               *
 ********************************************************
 */

#include <ncurses.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>


//these flags are used as a mask in *flags to monitor buffer states
enum flags {
    TYPED = 1,
    COMMENT = 2,
    ERROR = 4,
    MISTAKE1 = 8,
    MISTAKE2 = 16,
    NEWLINE = 32
};

//Can be used with bitwise operators to assign a mask to each file extension
enum CommentMask {
    DOUBLESLASHINLINE = 1,
    SINGLEHASHINLINE = 2,
    SLASHSTARBLOCK = 256
};

//struct for storing comment syntax for each file
struct comment {
    char *ext;
    unsigned short int syntax;
};

//structure for returning results of each "typing"
struct scoring {
    int right;
    int wrong;
    int time;
};

void commentParsing(const char *buffer, char *flags, int size, struct comment *co) {
    char* pos;
    int i;
    int comment_len = 0;
    for (i = 0; i < size; i++) {
        if (co->syntax & DOUBLESLASHINLINE && !comment_len) {
            if (!strncmp(buffer + i, "//", 2)) {
                //This is the start of an inline comment
                pos = strchr((buffer + i), '\n');
                if (pos) {
                    comment_len = (pos + 1) - (buffer + i);
                }
                //get rid of whitespace after comment
                comment_len += strspn(buffer + i + comment_len, " \t\n");
                //get rid of whitespace before comment
                while (buffer[i - 1] == ' ' || buffer[i - 1] == '\n') {
                    i--;
                    comment_len++;
                }
                if (buffer[i] == '\n') {
                    comment_len--;
                    i++;
                }

            }
        }
        if (co->syntax & SINGLEHASHINLINE && !comment_len) {
            if (buffer[i] == '#') {
                pos = strchr((buffer + i), '\n');
                if (pos) {
                    comment_len = (pos + 1) - (buffer + i);
                }
                //get rid of whitespace after comment
                comment_len += strspn(buffer + i + comment_len, " \t\n");
                //get rid of whitespace before comment
                while (buffer[i - 1] == ' ' || buffer[i - 1] == '\n') {
                    i--;
                    comment_len++;
                }
                if (buffer[i] == '\n') {
                    comment_len--;
                    i++;
                }
            }
        }
        if (co->syntax & SLASHSTARBLOCK && !comment_len) {
            if (!strncmp(buffer + i, "/*", 2)) {
                pos = strstr(buffer + i, "*/");
                if (pos) {
                    comment_len = (pos + 2) - (buffer + i);
                }
                //get rid of whitespace after comment
                comment_len += strspn(buffer + i + comment_len, " \t\n");
                //get rid of whitespace before comment
                while (buffer[i - 1] == ' ' || buffer[i - 1] == '\n') {
                    i--;
                    comment_len++;
                }
                if (buffer[i] == '\n') {
                    comment_len--;
                    i++;
                }
            }
        }
        if (comment_len) {
            flags[i] |= COMMENT;
            comment_len--;
        }
        /*
        if (!comment_len && buffer[i] == '\n') {
            comment_len = strspn(buffer + (i + 1), " \n\t");
        }
        if (!comment_len && buffer[i] == ' ') {
            comment_len = strspn(buffer + (i + 1), " \n\t") + 1;
        }
        */
    }
}

//populates the comment structure
//buffer needs to be passed in to look for "#!/bin/bash",etc. at the top
void commentType(char *filename, char* buffer, struct comment *co) {
    char *ext = filename + strlen(filename) - 1;


    while (ext > filename && *ext != '/') ext--;
    while (*ext != '.' && *ext) ext++;

    if (*ext == '.') ext++;
    co->ext = ext;

    /* Syntax mask so far:
     * 0-bit = // inline
     * 1-bit = # inline
     * 8-bit = / * to * / block (without spaces)
     */
    if (!strcmp(ext, "c") || !strcmp(ext, "h") || !strcmp(ext, "cc") ||
        !strcmp(ext, "cpp") || !strcmp(ext, "cxx") || !strcmp(ext, "hpp")) {
        co->syntax = DOUBLESLASHINLINE | SLASHSTARBLOCK;
    } else {
        co->syntax = 0;
    }
}

void markComments(char *filename, char *buffer, char *flags, int size,
                  struct comment *co) {
    commentType(filename, buffer, co);
    commentParsing(buffer, flags, size, co);
}

//returns the distance from one newline to the next on the previous line
//buffer[i] should be a newline character
//RETURN + 1 can be used as the x coordinate for buffer[i]
int previousLineLength(int i, int width, int begin, const char *buffer) {
    if (buffer[i] != '\n') {
        printw(0, 0, "ERROR in previousLineLength");
        return -1;
    } else {
        int k = i;
        while (k >= begin && buffer[k] != '\n') {
            k--;
        }
        return (i - k) % width;
    }
}

//fills the screen with the char filler
//could also be used to draw rectangles if we add two more arguments
//only uses black and white, current_color is used to maintain color state
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

//returns color pair for typed chars based on the time it took to type them
int colortiming(int diff) {
    if (diff > 2) {
        return 6;
    } else if (diff > 1) {
        return 5;
    } else {
        return 4;
    }
}

//reads typeable content from a file and populates the buffer for typing()
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

    //getting the file size
    if (strcmp(filename, "/dev/stdin")) {
        fseek(fd, 0L, SEEK_END);
        size = ftell(fd);
        fseek(fd, 0L, SEEK_SET);
    } else {
    //we need a constant size for a stdin buffer because fseek isn't possible
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
        //tabs are treated as spaces for simplicity
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

//Where almost all the action happens, displays a screen from the buffer and
//collects results as the user types along with it
//
//buffer: a single string of the entire file
//flags: a string of flags corresponding to the chars at that index in buffer
//size: size of buffer string
//begin: where to start typing in buffer
//height, width: useable screen dimensional
//filename: a string containing the name of the file in buffer
//score: structure that is used to return typing stats (right, wrong, time)
//RETURNS: how much of the buffer was completed before moving to next screen
int typing(const char *buffer, char *flags, int size, int begin, int height, int width,
           char* filename, struct scoring *score, struct comment *co) {
    //start is when first key is typed, last is time() of last correct keystroke
    time_t start, last;
    bool isStarted = false;

    //x, y mark the user cursor
    //xt, yt are for secondary drawing when x, y can't move
    //i marks where the user cursor is in the file buffer
    //j is for temporarily exploring the file buffer without moving i
    int x, y, xt, yt, i, j;
    char sub;       //stores the user inputted character from keyboard
    int streak = 0; //how far the user has to backspace to correct typo
    int right = 0;  //total correct keystrokes
    int wrong = 0;  //total incorrect keystrokes
    int used = 0;   //how many characters must be typed on this screen to end


    //Initializing the ncurses screen
    initscr();
    cbreak();
    noecho();

    //Initializing color schemes
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK); //to be typed
    init_pair(2, COLOR_BLACK, COLOR_MAGENTA);  //typing cursor
    init_pair(3, COLOR_BLACK, COLOR_RED);   //mistake hilight
    init_pair(4, COLOR_CYAN, COLOR_BLACK);  //fast match
    init_pair(5, COLOR_GREEN, COLOR_BLACK); //medium match
    init_pair(6, COLOR_YELLOW, COLOR_BLACK);//slow match
    init_pair(7, COLOR_BLACK, COLOR_WHITE); //newline char
    init_pair(8, COLOR_BLUE, COLOR_BLACK);  //commmented code

    x = 1;
    y = 0;

    //Draw start of buffer
    attron(COLOR_PAIR(1));
    i = begin;
    while (i < size && y < height - 3) {
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
        } else {
            move(y, x);
        }
        if (flags[i] & COMMENT) {
            attroff(COLOR_PAIR(8));
            attron(COLOR_PAIR(1));
        }
        i++;
    }
    used = i;
    attroff(COLOR_PAIR(1));

    //draw bottom border
    y = height - 2;
    attron(COLOR_PAIR(2));
    for (x = 0; x < width; x++) {
        mvaddch(y, x, ACS_CKBOARD);
    }
    mvprintw(y, (width - strlen(filename)) / 2, "%s", filename);
    attroff(COLOR_PAIR(2));

    x = 1;
    y = 0;
    move(y, x);

    i = begin;
    //Check if user types key associated with cursor char
    //  if not, draw that character with red background
    while (i < used || streak) {
        while (flags[i] & COMMENT) {
            if (flags[i] & NEWLINE) {
                y++;
                x = 1;
            } else {
                x++;
            }
            i++;
        }
        //draw typing cursor
        if (!streak && !(flags[i] & COMMENT)) {
            attron(COLOR_PAIR(flags[i] & NEWLINE ? 7 : 2));
            mvaddch(y, x, flags[i] & NEWLINE ? 182 | A_ALTCHARSET : buffer[i]);
            attroff(COLOR_PAIR(flags[i] & NEWLINE ? 7 : 2));
            move(height - 1, 0);
        }
        sub = getch();
        //If user pressed ESCAPE, we collect results for results()
        if (sub == 27) break;
        if (!isStarted) {
            isStarted = true;
            start = time(NULL);
            last = time(NULL);
        }
        //handle backspace
        if (sub == 127) {
            if (i > begin) {
                if (streak > 0)
                    streak--;
                attron(COLOR_PAIR(1));
                mvaddch(y, x, buffer[i]);
                attroff(COLOR_PAIR(1));
                i--;

                //backspace over leading whitespaces
                j = i;
                xt = x - 1;
                while (xt >= 1 && (buffer[j] == ' ')) {
                    j--;
                    xt--;
                }
                if (!xt) {
                    x = previousLineLength(j, width, begin, buffer) - 1;
                    i = j;
                    attron(COLOR_PAIR(streak ? 2 : 7));
                    mvaddch(y, x, buffer[i]);
                    attroff(COLOR_PAIR(streak ? 2 : 7));
                }

                //backspace over redundant newlines (need to implement for ' ')
                while (y > 0 && buffer[i] == '\n' && buffer[i-1] == '\n') {
                    i--;
                    y--;
                }
                if (x > 1) {
                    x--;
                    if (streak > 0) {
                        attron(COLOR_PAIR(1));
                        mvaddch(y, x, buffer[i]);
                        attroff(COLOR_PAIR(1));
                    } else {
                        attron(COLOR_PAIR(2));
                        mvaddch(y, x, buffer[i]);
                        attroff(COLOR_PAIR(2));
                    }
                //backspace onto previous row (this can be simplified
                //                             with previousLineLength())
                } else if (y != 0) {
                    //previous row break was caused by newline character
                    if (buffer[i] == '\n') {
                        //count chars in previous line before \n % width
                        j = i - 1;
                        while (j >= 0 && buffer[j] != '\n') {
                            j--;
                        }
                        x = (i - j) % width;
                        // draw cursor
                        y--;
                        attron(COLOR_PAIR(7));
                        mvaddch(y, x, 182 | A_ALTCHARSET);
                        attroff(COLOR_PAIR(7));
                    // row break was caused by x >= width
                    } else {
                        x = width - 1;
                        y--;
                        attron(COLOR_PAIR(2));
                        mvaddch(y, x, buffer[i]);
                        attroff(COLOR_PAIR(2));
                    }
                }
            }
        //handle normal chars
        } else if (i < used - 1 || !streak) {
            //correct keystroke
            if (sub == buffer[i] && streak == 0) {
                right++;
                attron(COLOR_PAIR(colortiming(time(NULL) - last)));
                mvaddch(y, x, buffer[i]);
                attroff(COLOR_PAIR(colortiming(time(NULL) - last)));
                last = time(NULL);
            //wrong keystroke
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
            x++;
            i++;
            //move cursor down a row when appropriate
            if (buffer[i - 1] == '\n' || x >= width) {
                x = 1;
                y++;
                while (buffer[i] == '\n') {
                    i++;
                    y++;
                }
                while (buffer[i] == ' ') {
                    i++;
                    x++;
                }
            }
        } else {
            //here we aren't allowing users to finish with a streak of errors
            //so lets redraw the bottom border in red to alert them
            yt = height - 2;
            attron(COLOR_PAIR(3));
            for (xt = 0; xt < width; xt++) {
                mvaddch(yt, xt, ACS_CKBOARD);
            }
            mvprintw(yt, (width - strlen("FIX ERRORS TO CONTINUE")) / 2,
                     "FIX ERRORS TO CONTINUE");
            attroff(COLOR_PAIR(3));
        }
        move(height - 1, 0);
        printw("WPM: %3.2f\t\tAccuracy: %3.2f%%\t\tTime: %d:%02d",
               ((double)right / 5) / ((double)(time(NULL)-start) / 60),
               ((double)right/(right+wrong))*100, (time(NULL) - start) / 60,
               (time(NULL) - start) % 60);
        move(height - 1, width - 1);

    }

    //Exit TUI window
    endwin();
    score->right = right;
    score->wrong = wrong;
    score->time = time(NULL) - start;
    return i;
}

//displays the results of a section of typing
//this is usually the results of a screen of text
//but may also be triggered by user pressing ESCAPE as a sort of PAUSE
void results(struct scoring *score, bool more, int height, int width) {
    initscr();
    cbreak();
    noecho();
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_CYAN);
    int x;
    int y;

    //clear screen
    clearscreen(height, width, 0, ACS_PLUS);

    //print box
    y = (height / 2) - 5;
    attron(COLOR_PAIR(1));
    for (x = (width / 2) - 20; x < (width / 2) + 20; x++) {
        mvaddch(y, x, ACS_CKBOARD);
    }
    for (y = (height / 2) - 4; y <= (height / 2) + 4; y++) {
        for (x = (width / 2) - 20; x < (width / 2) + 20; x++) {
            if (x < (width / 2) - 18 || x > (width / 2) + 17) {
                mvaddch(y, x, ACS_CKBOARD);
            } else {
                mvaddch(y, x, ' ');
            }
        }
    }
    for (x = (width / 2) - 20; x < (width / 2) + 20; x++) {
        mvaddch(y, x, ACS_CKBOARD);
    }

    //print results
    move((height / 2) - 3, (width / 2) - 13);
    printw("Words Per Minute:  %6.2f",
           ((double)score->right / 5) / ((double)score->time / 60));
    move((height / 2) - 1, (width / 2) - 13);
    printw("Accuracy        : %6.2f%%",
           ((double) score->right / (double)(score->right + score->wrong)) * 100);
    move((height / 2) + 1, (width / 2) - 13);
    printw("Total Keystrokes:  %6d", score->right + score->wrong);
    move((height / 2) + 3, (width / 2) - 13);
    if (more) {
        printw("To Continue, Press [ENTER]");
    } else {
        printw("   Press [ENTER] to Exit");
    }
    move(height - 1, width - 1);
    attroff(COLOR_PAIR(1));

    //Wait for the user to press ENTER
    char sub = getch();
    while (sub != '\n') {
        sub = getch();
    }
    //clear screen again
    clearscreen(height, width, 0, ' ');
    endwin();
}

//Just a wrapper function for handling splitting the buffer up into screens
void running(int argc, char **argv) {
    struct winsize w;
    struct comment co;
    struct scoring score;
    char *buffer, *flags;
    char filename[255];
    int size;
    int i = 0;

    //this for loop will take us through each file to be typed
    for (i = 1; i < argc; i++) {
        //check if first arg was '-s'
        if (!strcmp(argv[i], "-s")) {
            size = file_pop("/dev/stdin", &buffer, &flags);
            strcpy(filename, "stdin");
        } else {
            size = file_pop(argv[i], &buffer, &flags);
            strcpy(filename, argv[i]);
        }

        markComments(filename, buffer, flags, size, &co);

        ioctl(0,TIOCGWINSZ,&w);
        int res = typing(buffer, flags, size, 0, w.ws_row, w.ws_col, filename, &score, &co);
        while (res < size - 1) {
            ioctl(0,TIOCGWINSZ,&w);
            results(&score, true, w.ws_row, w.ws_col);
            ioctl(0,TIOCGWINSZ,&w);
            res = typing(buffer, flags, size, res, w.ws_row, w.ws_col, filename, &score, &co);
        }
        results(&score, i != argc - 1, w.ws_row, w.ws_col);
        free(buffer);
    }
}

//Don't really need envp at the moment, but hopefully in the future I
//can use it to find default files for when the user doesn't specify one
int main(int argc, char **argv, char **envp) {
    if (argc < 2) {
        printf("Usage: %s [-s] [filename] ... [filename]\n", argv[0]);
        return 0;
    }
    running(argc, argv);

    return 0;
}
