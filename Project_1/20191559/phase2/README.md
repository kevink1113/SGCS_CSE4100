# Multicore Programming Project 1

## Phase 2

### Basic internal shell commands

Start by creating a new process for each command in the pipeline and making the parent wait for the last command. This will allow running simple commands such as “ls -al | grep filename”. The key idea is; passing the output of one process as input to another. 

There can be multiple chains of pipes as command line argument.

Commands are executed by the child process created via forking by the parent process except cd and history.

Following shell commands with piping can be evaluated, e.g.,

* ls | grep filename
* cat filename | less
* cat filename | grep -i "abc" | sort -r

csapp.{c,h}
        CS:APP3e functions

myshell.c
        shell functions implemented 