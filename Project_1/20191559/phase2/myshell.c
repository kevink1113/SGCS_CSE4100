#include "csapp.h"
#include<errno.h>

#define MAXARGS   128
#define HISTORY_MAX 1000

#define FOR(i, n) for(int i=0; i<n; i++)
#define DIFF(A, B) if ((A) != (B))
#define SAME(A, B) if ((A) == (B))

char *history[HISTORY_MAX];
int history_count = 0;

char *find_command_path(char *cmd);

/* File to store history */
#define HISTORY_FILE ".myshell_h_20191559"
FILE *save_fp = NULL;

/**
 * @brief save command to history file
 * @param cmd command line input to save
 */
void save_to_history(char *cmd) {
    // FILE *save_fp;
    SAME (save_fp, NULL) {
        printf("Error: Failed to open history file.\n");
        return;
    }
    fprintf(save_fp, "%s", cmd);
}

/**
 * @brief Load history from file
 */
void load_history() {
    char line[MAXLINE];
    FILE *fp;
    fp = fopen(HISTORY_FILE, "r");
    SAME (fp, NULL) { // make file if not exist
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

/**
 * @brief Execute pipe commands by recursive calling
 * @param idx index of pipe
 * @param pipeNum number of pipe
 * @param comand command to execute
 * @param cmdline command line input
 */
void exec_pipe(int idx, int pipeNum, char comand[][8192], char *cmdline) {
    int file_descriptor[MAXARGS][2] = {0}; // file descriptor for pipe
    char *argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXLINE];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */
    volatile pid_t PID;

    pipe(file_descriptor[idx]);

    strcpy(buf, comand[idx]);
    bg = parseline(buf, argv);

    SAME (argv[0], NULL) return;    /* Ignore empty lines */

    if (!builtin_command(argv)) {   //quit -> exit(0), & -> ignore, other -> run
        if (!(PID = Fork())) {      // If it is child process
            DIFF (idx, pipeNum) {   // when there is pipe left to handle
                Close(file_descriptor[idx][0]);
                Dup2(file_descriptor[idx][1], STDOUT_FILENO);
                Close(file_descriptor[idx][1]);
            }
            char *cmd_path = find_command_path(argv[0]);
            DIFF (cmd_path, NULL) {
                Execve(cmd_path, argv, environ);
                Free(cmd_path);
            } else {
                printf("%s: Command not found.\n", argv[0]);
                Free(cmd_path);
                exit(0);
            }
        }/* Parent */
        else {
            /* Parent waits for foreground job to terminate */
            if (!bg) {
                int status;
                /*
                if (waitpid(PID, &status, 0) < 0)
                    unix_error("waitfg: waitpid error");
                */
                DIFF (pipeNum, idx) {
                    Close(file_descriptor[idx][1]);
                    Dup2(file_descriptor[idx][0], STDIN_FILENO);
                    Close(file_descriptor[idx][0]);
                    exec_pipe(++idx, pipeNum, comand, cmdline);
                } else {
                    if (!bg) {
                        int status;
                        while ((child_pid = waitpid(-1, &status, 0)) > 0);
                    }
                    return;
                }
            } else // when there is background process
                printf("%d %s", PID, cmdline);
        }
    }
    return;
}

/**
 * @brief Handle pipe command
 * @param cmdline command line input
 */
void handle_pipe(char *cmdline) {
    int cnt_pipe = 0;                   // number of pipe
    char cmd_tmp[MAXLINE];              // backup of cmdline
    char cmd_test[MAXLINE];             // backup of cmdline
    char cmd[MAXARGS][MAXLINE] = {};    // commands divided by pipe

    FOR(i, strlen(cmdline)) {
        if (cmdline[i] == '|') cnt_pipe++;
    }
    strcpy(cmd_tmp, cmdline); // copy cmdline to cmd_tmp (backup)

    FOR(i, cnt_pipe) {
        strncpy(cmd[i], cmd_tmp, strchr(cmd_tmp, '|') - cmd_tmp);
        // printf("result of strchr: %s\n", strchr(cmd_tmp, '|') + 1);
        strcpy(cmd_test, strchr(cmd_tmp, '|') + 1);
        strcpy(cmd_tmp, cmd_test);
        strcat(cmd[i], " ");
    }
    strcpy(cmd[cnt_pipe], cmd_tmp);

    exec_pipe(0, cnt_pipe, cmd, cmdline);
    return;
}


void check_child_processes() {
    int status;
    pid_t child_pid;
    while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Do any necessary cleanup for the terminated child process
    }
}

int main() {
    char cmdline[MAXLINE]; /* Command line */
    int tmp_stdin = dup(STDIN_FILENO);
    int tmp_stdout = dup(STDOUT_FILENO);

    load_history();

    // SIGNAL HANDLER
    // Signal(SIGTSTP, SIG_IGN); ////
    // Signal(SIGINT, sigint_handler); ////

    Signal(SIGCHLD, sigchld_handler);
    Signal(SIGINT, sigint_handler);
    Sigemptyset(&mask);
    Sigaddset(&mask, SIGCHLD);

    // END SIGNAL HANDLER

    save_fp = Fopen(HISTORY_FILE, "a");

    while (1) {
        // check_child_processes();
        /* Read */
        printf("CSE4100-MP-P1> ");
        Fgets(cmdline, MAXLINE, stdin);
        if (feof(stdin)) {
            printf("Maybe you typed too long command line.\n");
            exit(0);
        }
        /* Evaluate */
        eval(cmdline);

        /* File Descriptor Rollback */
        Dup2(tmp_stdin, STDIN_FILENO);
        Dup2(tmp_stdout, STDOUT_FILENO);
    }
    free(save_fp);
}
/* $end shellmain */

/**
 * @brief Find command path from /bin/ or /usr/bin/
 * @param cmd command name
 * @return path of command
 */
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
    char *argv[MAXARGS]; /* Argument list */
    char buf[MAXLINE];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */
    // pid_t pid;           /* Process id */
    volatile pid_t child_pid;     /* Child process id */
    strcpy(buf, cmdline);
    bg = parseline(buf, argv);

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
    if (argv[0] == NULL) return;    // Ignore empty lines

    if (!builtin_command(argv)) {
        if (strstr(cmdline, "|")) { // Check if pipe exists
            handle_pipe(cmdline);
            return;
        }
        Sigprocmask(SIG_BLOCK, &mask, &prev); ////

        if (!(child_pid = Fork())) {
            char *cmd_path = find_command_path(argv[0]);
            if (cmd_path != NULL) {
                Execve(cmd_path, argv, environ);
                Free(cmd_path);
            } else {
                printf("%s: Command not found.\n", argv[0]);
                Free(cmd_path);
                exit(0);
            }
        }
        /*
        pid = 0;
        while(!child_pid) {
            sigsuspend(&prev);
        }
        sigprocmask(SIG_SETMASK, &prev, NULL);
         */

        if (!bg) {
            int status;
            Waitpid(child_pid, &status, 0);
        } else {
            printf("%d %s", child_pid, cmdline);
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
        /*
        pid = 0;
        while (!pid) {
            sigsuspend(&prev);
        }
        sigprocmask(SIG_SETMASK, &prev, NULL);
        */
    }
    return;
}
/* $end eval */

/**
 * @brief Check if command(first arg) is builtin command
 * @param argv command name
 * @return 1 if command is builtin command, 0 otherwise
 */
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

    if (!strcmp(argv[0], "quit")) /* quit command */
        exit(0);
    if (!strcmp(argv[0], "exit")) exit(0); /* exit command */
    if (!strcmp(argv[0], "&"))    /* Ignore singleton & */
        return 1;
    return 0;                     /* Not a builtin command */
}

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv) {
    char *qt;            /* Single/Double quote position */
    char *delim;         /* Points to first space delimiter */
    int argc;            /* Number of args */
    int bg;              /* Background job? */

    buf[strlen(buf) - 1] = ' ';  /* Replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* Ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    while ((delim = strchr(buf, ' '))) {
        qt = NULL;
        SAME (*buf, '\'') {
            buf++;
            qt = strchr(buf, '\'');
        }
        SAME (*buf, '\"') {
            buf++;
            qt = strchr(buf, '\"');
        }
        delim = qt ? qt : delim; // if qt is not NULL, delim = qt
        argv[argc++] = buf;
        *delim = '\0';
        buf = 1 + delim;
        while (*buf && (*buf == ' ')) { /* Ignore spaces */
            buf++;
        }
    } // making front space skip
    argv[argc] = NULL;

    if (!argc)  /* Ignore blank line */
        return 1;

    /* Should the job run in the background? */
    DIFF ((bg = (*argv[argc - 1] == '&')), 0)argv[--argc] = NULL;

    return bg;
}
/* $end parseline */