# Multicore Programming Project 1

## Phase 1

### Basic internal shell commands

Execute basic internal shell commands such as, 

* cd: navigate the directories in your shell
* ls: list the directory contents
* mkdir, rmdir: create and remove directory using your shell
* touch, cat, echo: creating, reading and printing the contents of a file
* history: tracks shell commands executed since your shell started
* exit: terminate all the child processes and quit the shell

Commands are executed by the child process created via forking by the parent process except cd and history.

### History

The history command is executed as a built-in command. The history command keep track of commands executed since your shell was executed. The history command of the default shell provides many functions, but in this project, only two functions need to be implemented.

- !! : Print the latest executed command. then, Executes the command. (!! command doesn’t update history log.)
- !# : Print the command on the # line. then, Executes the command. 

csapp.{c,h}
        CS:APP3e functions

myshell.c
        shell functions implemented 