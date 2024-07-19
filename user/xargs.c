#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define BUF_LEN (16)

char**
allocate_array(uint n, uint element_size);

void
run_subprocess(char* executable, char** argv);

int
main(int argc, char* argv[]) {
  char* executable = argv[1];
  // To make code clean and it does matter, no free here.
  char** arguments = allocate_array(MAXARG, BUF_LEN);

  strcpy(arguments[0], executable);
  int arguments_len = 1;
  for (; arguments_len < (argc - 1); ++arguments_len) {
    strcpy(arguments[arguments_len], argv[arguments_len + 1]);
  }

  char* buf = malloc(BUF_LEN);
  int buf_idx = 0;
  char ch;
  while (read(0, &ch, 1) > 0) {
    if (ch == ' ' || ch == '\n' || ch == '\0') {
      if (buf_idx != 0) {
        strcpy(arguments[arguments_len], buf);
        ++arguments_len;
        buf_idx = 0;
        memset(buf, 0, BUF_LEN);
      }
      continue;
    }
    buf[buf_idx] = ch;
    ++buf_idx;
  }
  free(buf);

  arguments[arguments_len] = NULL;
  run_subprocess(executable, arguments);

  exit(0);
}

void
run_subprocess(char* executable, char** argv) {
  int pid = fork();
  if (pid == 0) {
    int res = exec(executable, (char**)argv);
    if (res < 0) {
      printf("error: exec\n");
      exit(1);
    }
    exit(0);
  } else if (pid > 0) {
    int res = wait(&pid);
    if (res < 0) {
      printf("error: wait\n");
      exit(1);
    }
  } else {
    printf("error: fork\n");
    exit(1);
  }
}

char**
allocate_array(uint n, uint element_size) {
  char** array = malloc(sizeof(char*) * n);
  for (int i = 0; i < n; ++i) {
    array[i] = (char*)malloc(element_size);
    memset(array[i], 0, element_size);
  }
  return array;
}
