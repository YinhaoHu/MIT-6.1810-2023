#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char* argv[]) {
  int pipes[2];

  pipe(pipes);
  int fork_res = fork();
  int pid = getpid();
  if (fork_res > 0) {
    // Parent
    char ping_msg[16] = "ping";
    write(pipes[1], ping_msg, 16);

    char pong_msg[16] = {'\0'};
    read(pipes[0], pong_msg, 16);
    printf("%d: received %s\n", pid, pong_msg);
  } else if (fork_res == 0) {
    // Child.
    char ping_msg[16] = {'\0'};
    read(pipes[0], ping_msg, 16);
    printf("%d: received %s\n", pid, ping_msg);

    char pong_msg[16] = "pong";
    write(pipes[1], pong_msg, 16);
  } else {
    printf("error in fork\n");
    exit(1);
  }

  exit(0);
}