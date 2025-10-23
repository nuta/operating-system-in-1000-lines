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
int addfile(const char *filename);
int readf(const char *filename);
int addf(const char *filename);
int writef(const char *filename, const char *buf, int len);
int ls(void);

__attribute__((noreturn)) void exit(void);
