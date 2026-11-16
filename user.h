#pragma once
#include "common.h"
struct sysret {
    int a0;
    int a1;
    int a2;
};
void putchar(char ch);
int getchar(void);
int readfile(const char *filename, char *buf, int len);
int writefile(const char *filename, const char *buf, int len);
int mkdir(const char *pathname);
int listdir(const char *path, char *buf, int len);
int remove(const char *pathname);
int remove_r(const char *pathname);
__attribute__((noreturn)) void exit(void);
__attribute__((noreturn)) void shutdown(void);
__attribute__((noreturn)) void reboot(void);
