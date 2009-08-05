//===-- klee-replay.c -----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee-replay.h"

#include "klee/Internal/ADT/KTest.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/wait.h>

static void __emit_error(const char *msg);

static KTest* input;
static unsigned obj_index;

static const char *progname = 0;
static unsigned monitored_pid = 0;    
static unsigned monitored_timeout;

static void stop_monitored(int process) {
  fprintf(stderr, "TIMEOUT: ATTEMPTING GDB EXIT\n");
  int pid = fork();
  if (pid < 0) {
    fprintf(stderr, "ERROR: gdb_exit: fork failed\n");
  } else if (pid == 0) {
    /* Run gdb in a child process. */
    const char *gdbargs[] = {
      "/usr/bin/gdb",
      "--pid", "",
      "-q",
      "--batch",
      "--eval-command=call exit(1)",
      0,
      0
    };
    char pids[64];
    sprintf(pids, "%d", process);

    gdbargs[2] = pids;
    /* Make sure gdb doesn't talk to the user */
    close(0);

    fprintf(stderr, "RUNNING GDB: ");
    unsigned i;
    for (i = 0; i != 5; ++i)
      fprintf(stderr, "%s ", gdbargs[i]);
    fprintf(stderr, "\n");

    execvp(gdbargs[0], (char * const *) gdbargs);
    perror("execvp");
    _exit(66);
  } else {
    /* Parent process, wait for gdb to finish. */
    int res, status;
    do {
      res = waitpid(pid, &status, 0);
    } while (res < 0 && errno == EINTR);

    if (res < 0) {
      perror("waitpid");
      _exit(66);
    }
  }
}

static void int_handler(int signal) {
  fprintf(stderr, "%s: Received signal %d.  Killing monitored process(es)\n", 
          progname, signal);
  if (monitored_pid) {
    stop_monitored(monitored_pid);
    /* Kill the process group of monitored_pid.  Since we called
       setpgrp() for pid, this will not kill us, or any of our
       ancestors */
    kill(-monitored_pid, SIGKILL);
  } else {
    _exit(99);
  }
}
static void timeout_handler(int signal) {
  fprintf(stderr, "%s: EXIT STATUS: TIMED OUT (%d seconds)\n", progname, 
          monitored_timeout);
  if (monitored_pid) {
    stop_monitored(monitored_pid);
    /* Kill the process group of monitored_pid.  Since we called
       setpgrp() for pid, this will not kill us, or any of our
       ancestors */
    kill(-monitored_pid, SIGKILL);
  } else {
    _exit(88);
  }
}

void process_status(int status,
		    time_t elapsed, 
		    const char *pfx) {
  fprintf(stderr, "%s: ", progname);
  if (pfx)
    fprintf(stderr, "%s: ", pfx);
  if (WIFSIGNALED(status)) {
    fprintf(stderr, "EXIT STATUS: CRASHED signal %d (%d seconds)\n",
            WTERMSIG(status), (int) elapsed);
    _exit(77);
  } else if (WIFEXITED(status)) {
    int rc = WEXITSTATUS(status);

    char msg[64];
    if (rc == 0) {
      strcpy(msg, "NORMAL");
    } else {
      sprintf(msg, "ABNORMAL %d", rc);
    }
    fprintf(stderr, "EXIT STATUS: %s (%d seconds)\n", msg, (int) elapsed);
    _exit(rc);
  } else {
    fprintf(stderr, "EXIT STATUS: NONE (%d seconds)\n", (int) elapsed);
    _exit(0);
  }
}

static void run_monitored(char *executable, int argc, char **argv) {
  int pid;
  const char *t = getenv("KLEE_REPLAY_TIMEOUT");  
  if (!t)
    t = "10000000";  
  monitored_timeout = atoi(t);
  
  if (monitored_timeout==0) {
    fprintf(stderr, "ERROR: invalid timeout (%s)\n", t);
    _exit(1);
  }

  /* Kill monitored process(es) on SIGINT and SIGTERM */
  signal(SIGINT, int_handler);
  signal(SIGTERM, int_handler);
  
  signal(SIGALRM, timeout_handler);
  pid = fork();
  if (pid < 0) {
    perror("fork");
    _exit(66);
  } else if (pid == 0) {
    /* This process actually executes the target program.
     *  
     * Create a new process group for pid, and the process tree it may spawn. We
     * do this, because later on we might want to kill pid _and_ all processes
     * spawned by it and its descendants.
     */
    setpgrp();

    execv(executable, argv);
    perror("execv");
    _exit(66);
  } else {
    /* Parent process which monitors the child. */
    int res, status;
    time_t start = time(0);
    sigset_t masked;

    sigemptyset(&masked);
    sigaddset(&masked, SIGALRM);

    monitored_pid = pid;
    alarm(monitored_timeout);
    do {
      res = waitpid(pid, &status, 0);
    } while (res < 0 && errno == EINTR);

    if (res < 0) {
      perror("waitpid");
      _exit(66);
    }
    
    /* Just in case, kill the process group of pid.  Since we called setpgrp()
       for pid, this will not kill us, or any of our ancestors */
    kill(-pid, SIGKILL);
    process_status(status, time(0) - start, 0);
  }
}

static void usage(void) {
  fprintf(stderr, "Usage: %s <executable> { <ktest-files> }\n", progname);
  fprintf(stderr, "   or: %s --create-files-only <ktest-file>\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "Set KLEE_REPLAY_TIMEOUT environment variable to set a timeout (in seconds).\n");
  exit(1);
}

int main(int argc, char** argv) {
  int prg_argc;
  char ** prg_argv;  

  progname = argv[0];

  if (argc < 3)
    usage();

  /* Special case hack for only creating files and not actually executing the
   * program.
   */
  if (strcmp(argv[1], "--create-files-only") == 0) {
    if (argc != 3)
      usage();

    char* input_fname = argv[2];

    input = kTest_fromFile(input_fname);
    if (!input) {
      fprintf(stderr, "%s: error: input file %s not valid.\n", progname,
              input_fname);
      exit(1);
    }
    
    prg_argc = input->numArgs;
    prg_argv = input->args;
    prg_argv[0] = argv[1];
    klee_init_env(&prg_argc, &prg_argv);
    
    replay_create_files(&__exe_fs);
    return 0;
  }

  /* Normal execution path ... */

  char* executable = argv[1];
  
  /* Verify the executable exists. */
  FILE *f = fopen(executable, "r");
  if (!f) {
    fprintf(stderr, "Error: executable %s not found.\n", executable);
    exit(1);
  }
  fclose(f);

  int idx = 0;
  for (idx = 2; idx != argc; ++idx) {
    char* input_fname = argv[idx];
    unsigned i;
    
    input = kTest_fromFile(input_fname);
    if (!input) {
      fprintf(stderr, "%s: error: input file %s not valid.\n", progname, 
              input_fname);
      exit(1);
    }
    
    obj_index = 0;
    prg_argc = input->numArgs;
    prg_argv = input->args;
    prg_argv[0] = argv[1];
    klee_init_env(&prg_argc, &prg_argv);

    if (idx > 2)
      fprintf(stderr, "\n");
    fprintf(stderr, "%s: TEST CASE: %s\n", progname, input_fname);
    fprintf(stderr, "%s: ARGS: ", progname);
    for (i=0; i != (unsigned) prg_argc; ++i) {
      char *s = prg_argv[i];
      if (s[0]=='A' && s[1] && !s[2]) s[1] = '\0';
      fprintf(stderr, "\"%s\" ", prg_argv[i]); 
    }
    fprintf(stderr, "\n");

    /* Run the test case machinery in a subprocess, eventually this parent
       process should be a script or something which shells out to the actual
       execution tool. */
    int pid = fork();
    if (pid < 0) {
      perror("fork");
      _exit(66);
    } else if (pid == 0) {
      /* Create the input files, pipes, etc., and run the process. */
      replay_create_files(&__exe_fs);
      run_monitored(executable, prg_argc, prg_argv);
      _exit(0);
    } else {
      /* Wait for the test case. */
      int res, status;

      do {
        res = waitpid(pid, &status, 0);
      } while (res < 0 && errno == EINTR);
      
      if (res < 0) {
        perror("waitpid");
        _exit(66);
      }
    }
  }

  return 0;
}


/* Klee functions */

int __fputc_unlocked(int c, FILE *f) {
  return fputc_unlocked(c, f);
}

int __fgetc_unlocked(FILE *f) {
  return fgetc_unlocked(f);
}

int klee_get_errno() {
  return errno;
}

void klee_warning(char *name) {
  fprintf(stderr, "WARNING: %s\n", name);
}

void klee_warning_once(char *name) {
  fprintf(stderr, "WARNING: %s\n", name);
}

int klee_assume(int x) {
  if (!x) {
    fprintf(stderr, "WARNING: klee_assume(0)!\n");
  }
  return 0;
}

int klee_is_symbolic(int x) {
  return 0;
}

void klee_prefer_cex(void *buffer, unsigned condition) {
  ;
}

void klee_make_symbolic(void *addr, unsigned nbytes, const char *name) {
  /* XXX remove model version code once new tests gen'd */
  if (obj_index >= input->numObjects) {
    if (strcmp("model_version", name) == 0) {
      assert(nbytes == 4);
      *((int*) addr) = 0;
    } else {
      __emit_error("ran out of appropriate inputs");
    }
  } else {
    KTestObject *boo = &input->objects[obj_index];

    if (strcmp("model_version", name) == 0 &&
        strcmp("model_version", boo->name) != 0) {
      assert(nbytes == 4);
      *((int*) addr) = 0;
    } else {
      if (boo->numBytes != nbytes) {
	fprintf(stderr, "make_symbolic mismatch, different sizes: "
		"%d in input file, %d in code\n", boo->numBytes, nbytes);
	exit(1);
      } else {
        memcpy(addr, boo->bytes, nbytes);
        obj_index++;
      }
    }
  }
}

/* Redefined here so that we can check the value read. */
int klee_range(int min, int max, const char* name) {
  int r;  
  klee_make_symbolic(&r, sizeof r, name); 

  if (r < min || r >= max) {
    fprintf(stderr, "klee_range(%d, %d, %s) returned invalid result: %d\n", 
	    min, max, name, r);
    exit(1);
  }  
  return r;
}

void klee_report_error(const char *file, int line, 
		       const char *message, const char *suffix) {
  __emit_error(message);
}

void klee_mark_global(void *object) {
  ;
}

/*** HELPER FUNCTIONS ***/

static void __emit_error(const char *msg) {
  fprintf(stderr, "ERROR: %s\n", msg);
  exit(1);
}
