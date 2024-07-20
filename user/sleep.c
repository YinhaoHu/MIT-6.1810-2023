#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char* argv[]) {
  if (argc != 2) {
    char err[64] = "usage: sleep <n_sec>";
    write(1, err, 64);
    return 1;
  }

  int n_sec = atoi(argv[1]);
  sleep(n_sec);

  exit(0);
}