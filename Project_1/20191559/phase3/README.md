# Multicore Programming Project 1

## Phase 3

### Run Processes in Background

Enable shell to run processes in the background. Linux shells support the notion of job control, which allows users to move jobs back and forth between background and foreground, and to change the process state (running, stopped, or terminated) of the processes in a job.

Start a command in the background if an ‘&’ is given in the command line arguments. Besides, shell must also provide various built-in commands that support job control.

Following shell commands with piping can be evaluated, e.g., 

* jobs: List the running and stopped background jobs.
* bg ⟨job⟩: Change a stopped background job to a running background job.
* fg ⟨job⟩: Change a stopped or running a background job to a running in the foreground.
* kill ⟨job⟩: Terminate a job

csapp.{c,h}
        CS:APP3e functions

myshell.c
        shell functions implemented 