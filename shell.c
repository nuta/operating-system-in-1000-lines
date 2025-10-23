#include "user.h"

void main(void) {
    while (1) {
    prompt:
        printf("> ");
        char cmdline[128];
        for (int i = 0;; i++) {
            char ch = getchar();
            putchar(ch);
            if (i == sizeof(cmdline) - 1) {
                printf("command line too long\n");
                goto prompt;
            } else if (ch == '\r' || ch == '\n') {  // ENTER tuşunu hem \r hem \n olarak kontrol et
                printf("\n");
                cmdline[i] = '\0';
                break;
            } else {
                cmdline[i] = ch;
            }
        }

        if (strcmp(cmdline, "hello") == 0) {
            printf("Hello world from shell!\n");
        } else if (strcmp(cmdline, "exit") == 0) {
            exit();
        }
	else if (strcmp(cmdline, "ls") == 0) {
   		 ls();
	}
       	else if (strncmp(cmdline, "readf ", 6) == 0) {
            char *filename = cmdline + 6;
            int ret = readf(filename);
            if (ret < 0)
                printf("file not found: %s\n", filename);
        } else if (strncmp(cmdline, "addf ", 5) == 0) {
            char *filename = cmdline + 5;
            int ret = addf(filename);
            if (ret == 0)
                printf("file \"%s\" added successfully!\n", filename);
            else
                printf("failed to add file \"%s\"\n", filename);
        } else if (strncmp(cmdline, "writef ", 7) == 0) {
            char *filename = cmdline + 7;
            char buf[128];
            printf("Enter content (end with ENTER): ");
            int i = 0;
            for (;;) {
                char ch = getchar();
                if (ch == '\r' || ch == '\n' || i == sizeof(buf) - 1) {
                    buf[i] = '\0';
                    break;
                }
                putchar(ch);
                buf[i++] = ch;
            }
            int ret = writef(filename, buf, i);
            if (ret >= 0)
                printf("written %d bytes to %s\n", ret, filename);
            else
                printf("failed to write to %s\n", filename);
        } else {
            printf("unknown command: %s\n", cmdline);
        }
    }
}

