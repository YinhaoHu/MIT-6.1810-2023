struct stat;

// system calls
int
fork(void);
int
exit(int) __attribute__((noreturn));
int
wait(int*);
/// @brief Create a pipe.
/// @param  fds An array of two fds(read_fd, write_fd).
/// @return
int
pipe(int* fds);
int
write(int, const void*, int);
int
read(int, void*, int);
int
close(int);
int
kill(int);
int
exec(const char*, char**);
int
open(const char* path, int mode);
int
mknod(const char*, short, short);
int
unlink(const char*);
int
fstat(int fd, struct stat*);
int
link(const char*, const char*);
int
mkdir(const char*);
int
chdir(const char*);
int
dup(int);
int
getpid(void);
char*
sbrk(int);
int
sleep(int);
int
uptime(void);

// ulib.c
int
stat(const char*, struct stat*);
char*
strcpy(char* dest, const char* src);
void*
memmove(void*, const void*, int);

/// @brief Finds th
/// @param str
/// @param c
/// @return
char*
strchr(const char* str, char c);
int
strcmp(const char*, const char*);
void
fprintf(int, const char*, ...);
void
printf(const char*, ...);
char*
gets(char*, int max);
uint
strlen(const char*);
void*
memset(void*, int, uint);
void* malloc(uint);
void
free(void*);
int
atoi(const char*);
int
memcmp(const void*, const void*, uint);
void*
memcpy(void*, const void*, uint);

#define NULL ((void*)0)