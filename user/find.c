#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"

void
find(const char* path, const char* target);

int
main(int argc, char* argv[]) {
  if (argc != 3) {
    printf("usage: %s <path> <target>\n", argv[0]);
    exit(1);
  }

  char* path = argv[1];
  char* target = argv[2];
  find(path, target);

  exit(0);
}

void
find(const char* path, const char* target) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("error: find open %s\n", path);
    return;
  }

  struct stat stat;
  int res = fstat(fd, &stat);
  if (res < 0) {
    printf("error: find fstat %s\n", path);
    return;
  }

  int last_slash_idx = 0;
  for (int i = 0;; ++i) {
    if (path[i] == '\0') {
      break;
    }
    if (path[i] == '/') {
      last_slash_idx = i;
    }
  }
  const char* basename = path + last_slash_idx + 1;

  if (strcmp(basename, target) == 0) {
    printf("%s\n", path);
  }

  if (stat.type == T_DIR) {
    struct dirent dir_entry;

    while (read(fd, &dir_entry, sizeof(dir_entry)) == sizeof(dir_entry)) {
      if (strcmp(".", dir_entry.name) == 0 || strcmp("..", dir_entry.name) == 0 || dir_entry.inum == 0) {
        continue;
      }

      char new_path[512];
      int path_len = strlen(path);
      strcpy(new_path, path);
      strcpy(new_path + path_len, "/");
      strcpy(new_path + path_len + 1, dir_entry.name);

      find(new_path, target);
    }
  }

  close(fd);
}