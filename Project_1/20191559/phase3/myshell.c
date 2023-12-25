#include "csapp.h"
#include<errno.h>

#define MAXARGS   128
#define HISTORY_MAX 1000

#define FOR(i, n) for(int i=0; i<n; i++)
#define DIFF(A, B) if ((A) != (B))
#define SAME(A, B) if ((A) == (B))
/////////////////////////// 20191559 ///////////////////////////
typedef enum {
    RUNNING, STOPPED, FOREGROUND, BACKGROUND
};

struct JOB {
    pid_t pid;
    int running;
    int state;
    int job_idx;
    char cmdline[MAXLINE];
    struct JOB *next;
};

sigset_t prev_mask, mask;
struct JOB *jobs = NULL;
int nxt_job = 1;
int job_cnt = 0;

void dlt_job(struct JOB *jobs, pid_t pid) {
    if (pid < 1 || job_cnt <= 0) return;
    struct JOB *prev = NULL, *curr = jobs;
    while (curr != NULL) {
        if (curr->pid == pid) {
            if (prev != NULL) prev->next = curr->next;
            else jobs = curr->next;

            free(curr);
            job_cnt--;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
    printf("No Such Job\n");
}

void add_job(struct JOB *jobs, pid_t pid, char *argv, int running, int st) {
    if (pid < 1) return;
    struct JOB *new_node = malloc(sizeof(struct JOB));
    new_node->running = running;
    new_node->state = st;
    new_node->pid = pid;
    new_node->job_idx = (st == BACKGROUND) ? nxt_job++ : -1;
    strcpy(new_node->cmdline, argv);
    new_node->next = NULL;

    struct JOB *current = jobs;
    while (current->next != NULL) current = current->next;
    current->next = new_node;
    ++job_cnt;
}

void listjobs(struct JOB *jobs) {
    struct JOB *current_job = jobs->next;
    while (current_job != NULL) {
        if (current_job->state == BACKGROUND) {
            printf("[%d] ", current_job->job_idx);
            switch (current_job->running) {
                case RUNNING:
                    printf("running ");
                    break;
                case STOPPED:
                    printf("suspended ");
                    break;
            }
            printf("%s\n", current_job->cmdline);
        }

        current_job = current_job->next;
    }
}
/////////////////////////// 20191559 ///////////////////////////



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

void sigchld_handler(int sig) {
    pid_t pid;
    int saved_errno = errno;
    int status;
    struct JOB *current_job;
    sigset_t mask_all, prev_all;

    sigfillset(&mask_all);
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
            dlt_job(jobs, pid);
            sigprocmask(SIG_SETMASK, &prev_all, NULL);
        }
        if (WIFSTOPPED(status)) {
            current_job = jobs->next;
            while (current_job) {
                SAME (current_job->pid, pid) {
                    current_job->state = BACKGROUND;
                    current_job->running = STOPPED;
                    current_job->job_idx = nxt_job++;
                    break;
                }
                current_job = current_job->next;
            }
        }
    }
    errno = saved_errno;
}

void sigint_handler(int sig) {
    pid_t pid = -1;
    struct JOB *tmp = jobs->next;
    while (tmp) {
        SAME (tmp->state, FOREGROUND) {
            pid = tmp->pid;
            break;
        }
        tmp = tmp->next;
    }
    if (pid > 0) kill(pid, SIGINT);
}

void sigtstp_handler(int sig) {
    pid_t pid = -1;
    struct JOB *tmp = jobs->next;
    while (tmp) {
        SAME(tmp->state, FOREGROUND) {
            pid = tmp->pid;
            break;
        }
        tmp = tmp->next;
    }
    if (pid > 0) kill(pid, SIGTSTP);
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

int main() {
    char cmdline[MAXLINE]; /* Command line */
    int tmp_stdin = dup(STDIN_FILENO);
    int tmp_stdout = dup(STDOUT_FILENO);
    jobs = (struct JOB *) malloc(sizeof(struct JOB));

    load_history();
    save_fp = Fopen(HISTORY_FILE, "a");

    // SIGNAL HANDLER
    Signal(SIGCHLD, sigchld_handler);
    Signal(SIGTSTP, sigtstp_handler);
    Signal(SIGINT, sigint_handler);

    Sigemptyset(&mask);
    Sigaddset(&mask, SIGTSTP);
    Sigaddset(&mask, SIGINT);
    Sigprocmask(SIG_BLOCK, &mask, &prev_mask);
    // END SIGNAL HANDLER

    jobs->next = NULL;
    jobs->pid = 0;
    jobs->job_idx = 0;
    jobs->running = -1;
    jobs->state = 0;

    while (1) {
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
    int bg;/* Should the job run in bg or fg? */
    volatile pid_t pid;           /* Process id */
    volatile sigset_t mask_prv, mask, masks;

    Sigfillset(&masks);
    Sigemptyset(&mask);
    Sigaddset(&mask, SIGCHLD);
    Sigaddset(&mask, SIGINT);
    Sigaddset(&mask, SIGTSTP);


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
        Sigprocmask(SIG_BLOCK, &mask, &mask_prv);
        if (strstr(cmdline, "|")) { // Check if pipe exists
            handle_pipe(cmdline);
            return;
        }
        Sigprocmask(SIG_BLOCK, &mask, &prev); ////

        if (!(pid = Fork())) {
            Signal(SIGTSTP, sigtstp_handler);
            if (!bg) Sigprocmask(SIG_UNBLOCK, &mask, NULL);
            if (execvp(argv[0], argv) < 0) {
                printf("%s: Command not found.\n", argv[0]);
                exit(0);
            }
        }
        Sigprocmask(SIG_SETMASK, &masks, NULL);
        if (!bg) add_job(jobs, pid, argv[0], RUNNING, FOREGROUND);
        else add_job(jobs, pid, argv[0], RUNNING, BACKGROUND);
        Sigprocmask(SIG_SETMASK, &mask_prv, NULL);

        if (!bg) {
            volatile sigset_t mask;
            Sigfillset(&mask);
            Sigdelset(&mask, SIGCHLD);
            Sigdelset(&mask, SIGTSTP);
            Sigsuspend(&mask);
        } // else printf("%d %s", pid, cmdline);
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
    } /* $end history */
    if (!strcmp(argv[0], "fg") || !strcmp(argv[0], "bg") || !strcmp(argv[0], "kill")) {
        int job_idx = atoi(argv[1] + 1);
        struct JOB *current_job = jobs->next;
        pid_t target_pid = -1;

        while (current_job != NULL) {
            if (current_job->job_idx == job_idx) {
                target_pid = current_job->pid;
                break;
            }
            current_job = current_job->next;
        }
        if (target_pid == -1) {
            printf("No Such Job\n");
            return 1;
        }
        if (!strcmp(argv[0], "fg")) {
            kill(target_pid, SIGCONT);
            current_job->running = RUNNING;
            current_job->state = FOREGROUND;
            printf("[%d] running %s\n", current_job->job_idx, current_job->cmdline);
            int status;
            Waitpid(target_pid, &status, 0);
        } else if (!strcmp(argv[0], "bg")) {
            Kill(target_pid, SIGCONT);
            current_job->running = RUNNING;
            printf("[%d] running %s\n", current_job->job_idx, current_job->cmdline);
        } else if (!strcmp(argv[0], "kill")) {
            Kill(target_pid, SIGINT);
            dlt_job(jobs, target_pid);
        }
        return 1;
    } else if (!strcmp(argv[0], "jobs")) {
        listjobs(jobs);
        return 1;
    }
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