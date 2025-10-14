#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>

int main(int argc, char *argv[]){
	pid_t pid;
	int status;
	
	// Check if test program is provided
	if (argc < 2) {
		printf("Usage: %s <test_program>\n", argv[0]);
		return 1;
	}
	
	printf("Process start to fork\n");
	
	/* fork a child process */
	pid = fork();
	
	if (pid == -1) {
		perror("fork failed");
		return 1;
	} else if (pid == 0) {
		// Child process
		printf("I'm the Child Process, my pid = %d\n", getpid());
		printf("Child process start to execute test program:\n");
		
		/* execute test program */ 
		execvp(argv[1], &argv[1]);
		
		// If execvp returns, it means an error occurred
		perror("execvp failed");
		exit(1);
	} else {
		// Parent process
		printf("I'm the Parent Process, my pid = %d\n", getpid());
		
		/* wait for child process terminates */
		waitpid(pid, &status, WUNTRACED);
		
		printf("Parent process receives SIGCHLD signal\n");
		
		/* check child process' termination status */
		if (WIFEXITED(status)) {
			// Normal termination
			printf("Normal termination with EXIT STATUS = %d\n", WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			// Terminated by a signal
			int sig = WTERMSIG(status);
			switch (sig) {
				case SIGABRT:
					printf("child process get SIGABRT signal\n");
					break;
				case SIGFPE:
					printf("child process get SIGFPE signal\n");
					break;
				case SIGILL:
					printf("child process get SIGILL signal\n");
					break;
				case SIGINT:
					printf("child process get SIGINT signal\n");
					break;
				case SIGKILL:
					printf("child process get SIGKILL signal\n");
					break;
				case SIGPIPE:
					printf("child process get SIGPIPE signal\n");
					break;
				case SIGQUIT:
					printf("child process get SIGQUIT signal\n");
					break;
				case SIGSEGV:
					printf("child process get SIGSEGV signal\n");
					break;
				case SIGTERM:
					printf("child process get SIGTERM signal\n");
					break;
				case SIGTRAP:
					printf("child process get SIGTRAP signal\n");
					break;
				case SIGBUS:
					printf("child process get SIGBUS signal\n");
					break;
				case SIGALRM:
					printf("child process get SIGALRM signal\n");
					break;
				case SIGHUP:
					printf("child process get SIGHUP signal\n");
					break;
				default:
					printf("child process get signal %d\n", sig);
					break;
			}
		} else if (WIFSTOPPED(status)) {
			// Stopped by a signal
			int sig = WSTOPSIG(status);
			switch (sig) {
				case SIGSTOP:
					printf("child process get SIGSTOP signal\n");
					break;
				case SIGTSTP:
					printf("child process get SIGTSTP signal\n");
					break;
				default:
					printf("child process stopped by signal %d\n", sig);
					break;
			}
		}
	}
	
	return 0;
}
