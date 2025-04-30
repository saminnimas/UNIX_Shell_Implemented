#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>


#define MAX_PATH 1024

#define MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_CMDS 10

#define MAX_HISTORY 100
#define MAX_INPUT 1024

typedef struct {
    char* commands[MAX_HISTORY];
    int count;
    int oldest;
} History;


typedef struct {
    char* args[MAX_ARGS];
    char* input_file;
    char* output_file;
    int append;
} Command;

void display_prompt();
char* read_input();

int parse_command(char* cmd_str, Command* cmd);
int parse_piped_input(char* input, Command* cmds, int* cmd_count);

void execute_command(Command* cmds, int cmd_count, int* last_status, pid_t shell_pgid);
int execute_builtin(Command* cmd, History* hist);

void init_history(History* hist);
void add_history(History* hist, const char* cmd);
void print_history(History* hist);
void free_history(History* hist);


int main() {
    Command cmds[MAX_CMDS];
    int cmd_count;
    History hist;
    init_history(&hist);

    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    const int signals[] = {SIGINT, SIGTSTP, SIGTTIN, SIGTTOU};
    for (int i = 0; i < 4; i++) {
        if (sigaction(signals[i], &sa, NULL) == -1) {
            perror("sigaction failed");
            return 1;
        }
    }

    // setting up shell's process group
    pid_t shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) == -1) {
        perror("shell_pid failed");
        return 1;
    }
    // set shell as foreground
    if (tcsetpgrp(STDIN_FILENO, shell_pgid) == -1) {
        perror("tcsetpgrp shell failed");
        return 1;
    }
    // printf("Shell initialized: PGID=%d\n", shell_pgid);

    while (1) {
        char* input = read_input();
        if (input == NULL) {
            // EOF
            break;
        }
        if (strlen(input) == 0) {
            continue;
        }

        add_history(&hist, input);

        char* input_copy = strdup(input);
        if (!input_copy) {
            perror("strdup failed");
            continue;
        }
        char* and_group = input_copy;
        char* and_sep = strstr(and_group, "&&");
        int continue_execution = 1;

        while (and_group != NULL && continue_execution) {
            if (and_sep != NULL) {
                *and_sep = '\0';
                and_sep += 2;
            }

            char* semi_group = and_group;
            char* semicolon = strchr(semi_group, ';');

            while (semi_group != NULL && continue_execution) {
                if (semicolon != NULL) {
                    *semicolon = '\0';
                }

                while (*semi_group == ' ' || *semi_group == '\t') {
                    semi_group++;
                }
                char* group_end = semi_group + strlen(semi_group) - 1;
                while (group_end >= semi_group && (*group_end == ' ' || *group_end == '\t')) {
                    *group_end = '\0';
                    group_end--;
                }

                if (strlen(semi_group) > 0) {
                    // printf("Executing Semicoloned group: '%s'\n", semi_group);
                    if (parse_piped_input(semi_group, cmds, &cmd_count)) {
                        if (cmd_count == 1 && execute_builtin(&cmds[0], &hist)) {
                            for (int i = 0; cmds[0].args[i] != NULL; i++) {
                                free(cmds[0].args[i]);
                            }
                            if (cmds[0].input_file) free(cmds[0].input_file);
                            if (cmds[0].output_file) free(cmds[0].output_file);
                        } else {
                            int last_status = 0;
                            execute_command(cmds, cmd_count, &last_status, shell_pgid);
                            continue_execution = (last_status == 0);
                            for (int j = 0; j < cmd_count; j++) {
                                for (int i = 0; cmds[j].args[i] != NULL; i++) {
                                    free(cmds[j].args[i]);
                                }
                                if (cmds[j].input_file) free(cmds[j].input_file);
                                if (cmds[j].output_file) free(cmds[j].output_file);
                            }
                        }
                    }
                }

                if (semicolon != NULL) {
                    semi_group = semicolon + 1;
                    semicolon = strchr(semi_group, ';');
                } else {
                    semi_group = NULL;
                }
            }

            if (and_sep != NULL) {
                and_group = and_sep;
                and_sep = strstr(and_group, "&&");
            } else {
                and_group = NULL;
            }
        }

        free(input_copy);
    }

    free_history(&hist);
    return 0;
}


void display_prompt() {
    char cwd[MAX_PATH];
    char* home = getenv("HOME");
    char final_cwd[MAX_PATH + 10];

    if (getcwd(cwd, MAX_PATH) == NULL) {
        strcpy(final_cwd, "sh> ");
    }
    else {
        if (home != NULL && strstr(cwd, home) == cwd) {
            snprintf(final_cwd, MAX_PATH + 10, "~%s sh> ", cwd + strlen(home));
        }
        else {
            snprintf(final_cwd, MAX_PATH + 10, "%s sh> ", cwd);
        }
    }
    printf("%s", final_cwd);
    fflush(stdout);
}


char* read_input() {
    static char input[MAX_INPUT];
    display_prompt();

    if (fgets(input, MAX_INPUT, stdin) == NULL) {
        return NULL; // EOF
    }

    input[strcspn(input, "\n")] = '\0';
    
    return input;
}


int parse_command(char* cmd_str, Command* cmd) {
    int i = 0;
    char* cmd_copy = strdup(cmd_str);
    if (!cmd_copy) {
        perror("strdup failed");
        return 0;
    }
    char* token = strtok(cmd_copy, " \t");
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->append = 0;

    while (token != NULL && i < MAX_ARGS - 1) {
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t");
            if (token != NULL) {
                cmd->input_file = strdup(token);
            }
            token = strtok(NULL, " \t");
            continue;
        }
        else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t");
            if (token != NULL) {
                cmd->output_file = strdup(token);
                cmd->append = 0;
            }
            token = strtok(NULL, " \t");
            continue;
        }
        else if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, " \t");
            if (token != NULL) {
                cmd->output_file = strdup(token);
                cmd->append = 1;
            }
            token = strtok(NULL, " \t");
            continue;
        }
        cmd->args[i] = strdup(token);
        if (!cmd->args[i]) {
            perror("strdup failed");
            while (i > 0) {
                free(cmd->args[--i]);
            }
            free(cmd_copy);
            return 0;
        }
        i++;
        token = strtok(NULL, " \t");
    }
    cmd->args[i] = NULL;
    free(cmd_copy);
    return i > 0;
}


int parse_piped_input(char* input, Command* cmds, int* cmd_count) {
    *cmd_count = 0;
    char* input_copy = strdup(input);
    if (!input_copy) {
        perror("strdup failed");
        return 0;
    }

    char* start = input_copy;
    char* end = input_copy;
    while (*end && *cmd_count < MAX_CMDS) {
        if (*end == '|') {
            *end = '\0';
            while (*start == ' ' || *start == '\t') {
                start++;
            }
            char* cmd_end = end - 1;
            while (cmd_end >= start && (*cmd_end == ' ' || *cmd_end == '\t')) {
                *cmd_end = '\0';
                cmd_end--;
            }

            if (strlen(start) > 0) {
                // initialize commands (cmd) array
                for (int i = 0; i < MAX_ARGS; i++) {
                    cmds[*cmd_count].args[i] = NULL;
                }
                if (parse_command(start, &cmds[*cmd_count])) {
                    (*cmd_count)++;
                }
            }
            start = end + 1;
        }
        end++;
    }

    // for processing last command or only command if no "pipe"
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    char* cmd_end = start + strlen(start) - 1;
    while (cmd_end >= start && (*cmd_end == ' ' || *cmd_end == '\t')) {
        *cmd_end = '\0';
        cmd_end--;
    }

    if (strlen(start) > 0) {
        for (int i = 0; i < MAX_ARGS; i++) {
            cmds[*cmd_count].args[i] = NULL;
        }
        if (parse_command(start, &cmds[*cmd_count])) {
            (*cmd_count)++;
        }
    }

    free(input_copy);
    return *cmd_count > 0;
}


int execute_builtin(Command* cmd, History* hist) {
    if (cmd->args[0] == NULL) {
        return 0;
    }

    struct sigaction sa_builtin, sa_old;
    sa_builtin.sa_handler = SIG_DFL;
    sigemptyset(&sa_builtin.sa_mask);
    sa_builtin.sa_flags = 0;
    if (sigaction(SIGINT, &sa_builtin, &sa_old) == -1) {
        perror("sigaction SIGINT builtin failed");
        return 0;
    }

    int result = 0;
    if (strcmp(cmd->args[0], "cd") == 0) {
        char* dir = cmd->args[1];
        if (dir == NULL) {
            dir = getenv("HOME");
        }
        if (chdir(dir) != 0) {
            perror("cd failed");
        }
        result = 1;
    }
    else if (strcmp(cmd->args[0], "exit") == 0) {
        exit(0);
    }
    else if (strcmp(cmd->args[0], "history") == 0) {
        print_history(hist);
        result = 1;
    }

    if (sigaction(SIGINT, &sa_old, NULL) == -1) {
        perror("sigaction SIGINT restore failed");
    }

    return result;
}


void execute_command(Command* cmds, int cmd_count, int* last_status, pid_t shell_pgid) {
    int pipefds[2 * (cmd_count - 1)];
    pid_t pids[MAX_CMDS];
    pid_t pgid = 0;

    // create pipes per child_process
    for (int i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipefds + i * 2) == -1) {
            perror("pipe failed");
            *last_status = 1;
            return;
        }
    }

    // fork for each command
    for (int i = 0; i < cmd_count; i++) {
        pids[i] = fork();
        if (pids[i] == -1) {
            perror("fork failed");
            for (int j = 0; j < 2 * (cmd_count - 1); j++) {
                close(pipefds[j]);
            }
            *last_status = 1;
            return;
        }
        else if (pids[i] == 0) {
            if (i == 0) {
                pgid = getpid();
            }
            if (setpgid(0, pgid) == -1) {
                perror("setpgid child failed");
                exit(EXIT_FAILURE);
            }

            // reestore default SIGINT handler
            struct sigaction sa;
            sa.sa_handler = SIG_DFL;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            if (sigaction(SIGINT, &sa, NULL) == -1) {
                perror("sigaction SIGINT child failed");
                exit(EXIT_FAILURE);
            }

            // closing unused pipes; i.e. (pipfds) NOT '|' this pip
            for (int j = 0; j < cmd_count - 1; j++) {
                if (i == 0 && j == 0) {
                    close(pipefds[0]);
                }
                else if (i == cmd_count - 1 && j == cmd_count - 2) {
                    close(pipefds[2 * (cmd_count - 2) + 1]);
                }
                else if (i == j + 1) {
                    close(pipefds[2 * j + 1]);
                }
                else if (i == j) {
                    close(pipefds[2 * j]);
                }
                else {
                    close(pipefds[2 * j]);
                    close(pipefds[2 * j + 1]);
                }
            }

            // INPUT section
            if (i > 0) {
                if (dup2(pipefds[(i - 1) * 2], STDIN_FILENO) == -1) {
                    perror("dup2 failed for pipe input");
                    exit(EXIT_FAILURE);
                }
            }
            else if (cmds[i].input_file != NULL) {
                int fd = open(cmds[i].input_file, O_RDONLY);
                if (fd == -1) {
                    perror("cannot open input file");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDIN_FILENO) == -1) {
                    perror("dup2 failed for input");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            }

            // OUTPUT section
            if (i < cmd_count - 1) {
                if (dup2(pipefds[i * 2 + 1], STDOUT_FILENO) == -1) {
                    perror("dup2 failed for pipe output");
                    exit(EXIT_FAILURE);
                }
            }
            else if (cmds[i].output_file != NULL) {
                int flags = O_WRONLY | O_CREAT;
                if (cmds[i].append) {
                    flags |= O_APPEND;
                }
                else {
                    flags |= O_TRUNC;
                }
                int fd = open(cmds[i].output_file, flags, 0644);
                if (fd == -1) {
                    perror("cannot open output file");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    perror("dup2 failed for output");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            }

            // execute command/s
            if (execvp(cmds[i].args[0], cmds[i].args) == -1) {
                fprintf(stderr, "exec failed for command: %s\n", cmds[i].args[0]);
                exit(EXIT_FAILURE);
            }
        }
    }

    for (int i = 0; i < 2 * (cmd_count - 1); i++) {
        close(pipefds[i]);
    }

    // set foreground to child process
    if (cmd_count > 0) {
        pgid = pids[0];
        if (setpgid(pgid, pgid) == -1 && errno != EACCES) {
            perror("setpgid parent failed");
        }
        // printf("Parent: Setting FG PGID=%d\n", pgid); // Debug
        if (tcsetpgrp(STDIN_FILENO, pgid) == -1) {
            perror("tcsetpgrp parent failed");
        }
    }

    // Wait for all children
    int status;
    *last_status = 0;
    for (int i = 0; i < cmd_count; i++) {
        if (waitpid(pids[i], &status, 0) == -1) {
            perror("waitpid failed");
            *last_status = 1;
        }
        if (i == cmd_count - 1 && WIFEXITED(status)) {
            *last_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT) {
            *last_status = 130;
        }
    }

    // restore my shell as foreground
    if (tcsetpgrp(STDIN_FILENO, shell_pgid) == -1) {
        perror("tcsetpgrp restore shell failed");
    }
}


void init_history(History* hist) {
    hist->count = 0;
    hist->oldest = 0;
    for (int i = 0; i < MAX_HISTORY; i++) {
        hist->commands[i] = NULL;
    }
}

void add_history(History* hist, const char* cmd) {
    if (cmd == NULL || strlen(cmd) == 0) {
        return;
    }

    if (hist->count == MAX_HISTORY) {
        free(hist->commands[hist->oldest]);
        hist->commands[hist->oldest] = NULL;
    }

    hist->commands[hist->oldest] = strdup(cmd);
    if (!hist->commands[hist->oldest]) {
        perror("strdup failed");
        return;
    }

    if (hist->count < MAX_HISTORY) {
        hist->count++;
    }
    hist->oldest = (hist->oldest + 1) % MAX_HISTORY;
}

void print_history(History* hist) {
    int index = 1;
    int start = (hist->count < MAX_HISTORY) ? 0 : hist->oldest;
    int count = (hist->count < MAX_HISTORY) ? hist->count : MAX_HISTORY;

    for (int i = 0; i < count; i++) {
        int pos = (start + i) % MAX_HISTORY;
        if (hist->commands[pos] != NULL) {
            printf("%d: %s\n", index++, hist->commands[pos]);
        }
    }
}

void free_history(History* hist) {
    for (int i = 0; i < MAX_HISTORY; i++) {
        if (hist->commands[i] != NULL) {
            free(hist->commands[i]);
        }
    }
}