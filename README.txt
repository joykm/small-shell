** Instructions on how to compile and run smallsh.c
	1. Compile smallsh.c using "gcc --std=c99 -o smallsh smallsh.c"
	2. Run smallsh with "./smallsh"


** Features:
	1. Manually implemented commands:
		a. “cd” - Supports absolute and relative paths, supports "cd", "cd .”, “cd ..", "cd -", "cd ~", "cd /", "cd ../../dir" 
		b. “status” - displays the exit status of the last run command
		c. “exit” - exits the program (since the CTRL-C signal has been replaced)
	2. All other commands (such as ls) are implemented with execvp()
	3. Any command beginning with # character will be ignored. I.e. “# this will be ignored”
	4. Variable expansion: 
		a. All instances of $$ will be replaced with the parent process id. i.e. “ls > junk$$” will redirect output to a file called junk### where ### is the parent process id.
	5. Input and Output Redirection using > and < characters
	6. Executing commands in the background using the & suffix (separated by white space). i.e. “sleep 50 &”.
	7. Signal Handling:
		a. The default functionality of CTRL-C will be ignored using signal handling. It will be replaced with a function that only terminates child processes running in the foreground. i.e. “sleep 50”. 
		b. The default functionality of CTRL-Z will be ignored using signal handling. It will be replaced with a function to ignore or allow background process commands. While ignoring background process commands, any command with the & suffix will instead be run as a foreground process. i.e. “sleep 50 &” will instead be interpreted as “sleep 50”. 


** Example Usage:

	// Note: Run smallsh
	$ ./smallsh

	// Note: Example of a comment line
	: # this command will be ignored
	:

	: ls
	junk   smallsh    smallsh.c

	// Note: Example of output redirection
	: ls > junk
	: status
	exit value 0

	: cat junk
	junk
	smallsh
	smallsh.c

	// Note: Example of input and output redirection
	: wc < junk > junk2
	: wc < junk
	       3       3      23

	: test -f badfile
	: status
	exit value 1

	: wc < badfile
	cannot open badfile for input

	: status
	exit value 1

	: badfile
	badfile: no such file or directory

	: sleep 5
	^Cterminated by signal 2

	: status
	terminated by signal 2


	// Note: Example of background process.
	: sleep 15 &
	background pid is 4923

	: ps
	  PID TTY          TIME CMD
	 4923 pts/0    00:00:00 sleep
	 4564 pts/0    00:00:03 bash
	 4867 pts/0    00:01:32 smallsh
	 4927 pts/0    00:00:00 ps

	: echo testing
	testing

	// Note: Example of variable expansion.
	: echo $$
	4867e

	// Note: Press enter after the sleep 15 background process has completed. This will trigger a waitpid() command and will reap the zombie process.
	:
	background pid 4923 is done: exit value 0

	: sleep 30 &
	background pid is 4941

	// Note: Example of manually killing a background process.
	: kill -15 4941
	background pid 4941 is done: terminated by signal 15

	: pwd
	/nfs/stak/users/chaudhrn/CS344/prog3

	: cd
	: pwd
	/nfs/stak/users/chaudhrn

	: cd CS344
	: pwd
	/nfs/stak/users/chaudhrn/CS344

	)
	// Note: Example of signal handling.
	: ^Z
	Entering foreground-only mode (& is now ignored)

	: date
	 Mon Jan  2 11:24:33 PST 2017

	// Note: Sleep will run in the foreground.
	: sleep 5 &
	: date
	 Mon Jan  2 11:24:38 PST 2017

	: ^Z
	Exiting foreground-only mode

	: date
	 Mon Jan  2 11:24:39 PST 2017

	: sleep 5 &
	background pid is 4963

	: date
	 Mon Jan 2 11:24:39 PST 2017

	: sleep 100 &
	background pid is 5121

	// Note: Example of killing and reaping background processes prior to exiting program.
	: exit
	pid 5121 killed and reaped
	$

