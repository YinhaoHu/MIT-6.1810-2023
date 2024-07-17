#include "kernel/types.h"
#include "user/user.h"

#define ARRAY_LEN 34

static const int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31};
static const unsigned long primes_len = sizeof(primes) / sizeof(int);
/// @brief Checkes whether the `num` is the prime in this lab.
/// @param num number to check.
/// @return negative if not prime, positive if prime.
static int
is_prime(int num);

int
main(int argc, char* argv[]) {
  int fds[2];

  int array[ARRAY_LEN];
  int array_size = ARRAY_LEN;
  for (int i = 2; i <= 35; ++i) {
    array[i - 2] = i;
  }
  goto init;

child_start:
  int readn;

  for (;;) {
    readn = read(fds[0], &(array[array_size]), sizeof(int));
    if (readn == 0) {
      break;
    }
    ++array_size;
  }

  // Child process received the prime as the first entry in the array.
  printf("prime %d\n", array[0]);

init:
  // Checkes whether the current process is the last process.
  if (array_size > 1) {
    pipe(fds);
    int pid = fork();

    if (pid == 0) {
      array_size = 0;
      close(fds[1]);
      goto child_start;
    } else if (pid < 0) {
      printf("fork error.\n");
      exit(1);
    } else {
      close(fds[0]);
      int idx = 1;
      for (; idx < array_size; ++idx) {
        if (is_prime(array[idx]) >= 0) {
          break;
        }
      }
      for (; idx < array_size; ++idx) {
        write(fds[1], &(array[idx]), sizeof(int));
      }
      close(fds[1]);
    }

    int _child_pid;
    wait(&_child_pid);
  }
  exit(0);
}

static int
is_prime(int num) {
  for (int i = 0; i < primes_len; ++i) {
    if (primes[i] == num) {
      return i;
    }
  }
  return -1;
}