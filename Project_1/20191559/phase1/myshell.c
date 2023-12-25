/* $begin shellmain */
#include "csapp.h"
#include<errno.h>

#define MAXARGS   128
#define HISTORY_MAX 1000

#define FOR(i, n) for(int i=0; i<n; i++)

char *history[HISTORY_MAX];
int history_count = 0;

/* File to store history */
#define HISTORY_FILE ".myshell_h_20191559"
FILE *save_fp = NULL;

/* Save command to history file */
void save_to_history(char *cmd) {
    // FILE *save_fp;
    if (save_fp == NULL) {
        printf("Error: Failed to open history file.\n");
        return;
    }
    fprintf(save_fp, "%s", cmd);
}

/* Load history from file */
void load_history() {
    char line[MAXLINE];
    FILE *fp;
    fp = fopen(HISTORY_FILE, "r");
    if (fp == NULL) { // make file if not exist
        fp = fopen(HISTORY_FILE, "w");
    }
    while (fgets(line, MAXLINE, fp) != NULL) {
        history[history_count % HISTORY_MAX] = strdup(line);
        // printf("%d\t%s", history_count, line);
        ++history_count;
    }
    fclose(fp);
}


/* Function prototypes */
void eval(char *cmdline);

int parseline(char *buf, char **argv);

int builtin_command(char **argv);

// START SIGNAL
volatile pid_t pid;
volatile pid_t child_pid;
sigset_t mask, prev;

void sigchld_handler(int s) {
    int olderrno = errno, status;
    pid = Wait(&status);
    errno = olderrno;
}

void sigint_handler(int s) {
    if (child_pid > 0) {
        kill(child_pid, SIGINT);
        Waitpid(child_pid, NULL, 0);
    }

    printf("\n");
    exit(1);
}
// END SIGNAL

int main() {
    char cmdline[MAXLINE]; /* Command line */
    load_history();

    // SIGNAL HANDLER
    Signal(SIGCHLD, sigchld_handler);
    Signal(SIGINT, sigint_handler);
    Sigemptyset(&mask);
    Sigaddset(&mask, SIGCHLD);

    // END SIGNAL HANDLER

    save_fp = fopen(HISTORY_FILE, "a");

    while (1) {
        /* Read */
        printf("CSE4100-MP-P1> ");
        fgets(cmdline, MAXLINE, stdin);
        if (feof(stdin))
            exit(0);

        /* Evaluate */
        eval(cmdline);
    }
    free(save_fp);
}
/* $end shellmain */


/* $begin find_command_path */
char *find_command_path(char *cmd) {
    char *paths[] = {"/bin/", "/usr/bin/"};
    // int num_paths = sizeof(paths) / sizeof(paths[0]);
    char *path = (char *) Malloc(MAXLINE * sizeof(char));
    struct stat st;

    FOR(i, 2) {
        strcpy(path, paths[i]);
        strcat(path, cmd);
        if (!stat(path, &st)) return path;
    }
    Free(path);
    return NULL;
}
/* $end find_command_path */


/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) {
    char *argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXLINE];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */
    // pid_t pid;           /* Process id */
    // pid_t child_pid;     /* Child process id */

    /* TODO
     * exit() function should be implemented
     */
    strcpy(buf, cmdline);
    bg = parseline(buf, argv);

    // handle_history(argv);
    // Add command to history

    if (strlen(cmdline) > 1 && argv[0][0] != '!') {
        // Save only when command is different from previous one
        if (history_count > 0 && strcmp(cmdline, history[(history_count - 1) % HISTORY_MAX]) == 0) {
            // printf("same command\n");
        } else {
            history[history_count % HISTORY_MAX] = strdup(cmdline);
            ++history_count;
            save_to_history(cmdline);
        }
    }
    if (argv[0] == NULL)
        return;   /* Ignore empty lines */

    if (!builtin_command(argv)) {
        sigprocmask(SIG_BLOCK, &mask, &prev);

        if ((child_pid = Fork()) == 0) {
            char *cmd_path = find_command_path(argv[0]);
            if (cmd_path != NULL) {
                if (execve(cmd_path, argv, environ) < 0) {
                    printf("%s: Command not found.\n", argv[0]);
                    exit(0);
                }
                Free(cmd_path);
            } else {
                printf("%s: Command not found.\n", argv[0]);
                exit(0);
            }
        }
        /*
        else if (child_pid > 0) {
            if (!bg) {
                int status;
                Waitpid(child_pid, &status, 0);
            } else {
                printf("%d %s", child_pid, cmdline);
            }
        } else {
            unix_error("fork error!\n");
        }
        */
        pid = 0;
        while (!pid) {
            sigsuspend(&prev);
        }
        sigprocmask(SIG_SETMASK, &prev, NULL);

    }
    return;
}


/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv) {
    if (!strcmp(argv[0], "cd")) { /* cd command */
        if (argv[1] == NULL) {
            chdir(getenv("HOME"));
        } else if (chdir(argv[1]) < 0)
            printf("Could not find directory named %s\n", argv[1]);
        return 1;
    }

    /* $begin history */
    if (!strcmp(argv[0], "history")) { /* history command */
        FOR(i, history_count) {
            printf("%d\t%s", i + 1, history[i]);
        }
        return 1;
    }
    if (!strncmp(argv[0], "!!", 2)) { // use of strncmp to firmly check '!!', not '!'
        if (history_count == 0) {
            printf("No commands in history\n");
        } else {
            char *new_cmd = (char *) Malloc(MAXLINE * sizeof(char));
            strcpy(new_cmd, history[(history_count - 1) % HISTORY_MAX]);
            new_cmd[strlen(new_cmd) - 1] = '\0';
            strcat(new_cmd, argv[0] + 2);
            // eval(new_cmd); // 여기서 eval을 호출하는 것이 아니라 NULL일때까지 new_cmd를 계속해서 붙여서 eval을 호출해야 한다.
            for (int i = 1; argv[i] != NULL; i++) {
                strcat(new_cmd, " ");
                strcat(new_cmd, argv[i]);
            }
            strcat(new_cmd, "\n");
            printf("%s", new_cmd);
            eval(new_cmd);

            Free(new_cmd);
        }
        return 1;
    }
    if (argv[0][0] == '!') {
        int cmd_num = atoi(&argv[0][1]);
        if (history_count <= 1) {
            printf("No commands in history\n");
        } else {
            if (cmd_num > 0 && cmd_num <= history_count) {
                char *new_cmd = (char *) Malloc(MAXLINE * sizeof(char));
                strcpy(new_cmd, history[(cmd_num - 1) % HISTORY_MAX]);
                // printf("%s", history[(cmd_num - 1) % HISTORY_MAX]);
                new_cmd[strlen(new_cmd) - 1] = '\0';
                for (int i = 1; argv[i] != NULL; i++) {
                    strcat(new_cmd, " ");
                    strcat(new_cmd, argv[i]);
                }
                strcat(new_cmd, "\n");
                printf("%s", new_cmd);
                eval(new_cmd);

                Free(new_cmd);
            } else {
                printf("Invalid command number\n");
            }
        }

        return 1;
    }
    /* $end history */

    if (!strcmp(argv[0], "exit")) exit(0); /* exit command */
    if (!strcmp(argv[0], "quit")) /* quit command */
        exit(0);
    if (!strcmp(argv[0], "&"))    /* Ignore singleton & */
        return 1;
    return 0;                     /* Not a builtin command */
}
/* $end eval */

/* $begin parseline */
/**
 * @brief Parse the command line and build the argv array
 * @param buf modified command line input
 * @param argv List of arg strings
 * @return bg: Background job or not
 */
int parseline(char *buf, char **argv) {
    char *delim;         /* Points to first space delimiter */
    // char *quote;         /* Points to a single or double quote */
    int argc;            /* Number of args */
    int bg;              /* Background job? */

    buf[strlen(buf) - 1] = ' ';  /* Replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* Ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    while ((delim = strchr(buf, ' '))) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* Ignore spaces */
            buf++;
    } // making front space skip
    argv[argc] = NULL;

    if (argc == 0)  /* Ignore blank line */
        return 1;

    /* Should the job run in the background? */
    if ((bg = (*argv[argc - 1] == '&')) != 0)
        argv[--argc] = NULL;

    return bg;
}
/* $end parseline */


