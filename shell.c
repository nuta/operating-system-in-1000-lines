#include "user.h"
#define W 40
#define H 20
int px[W*H],py[W*H],pl,fx,fy,dx,dy,score;
void cls(){printf("\033[2J\033[H");}
void draw_snake(){int i;cls();for(int y=0;y<H;y++){for(int x=0;x<W;x++){int is_snake=0;for(i=0;i<pl;i++)if(px[i]==x&&py[i]==y)is_snake=1;if(x==fx&&y==fy)printf("@");else if(is_snake)printf(i==0?"O":"o");else if(x==0||x==W-1||y==0||y==H-1)printf("#");else printf(" ");}printf("\n");}printf("Score: %d | Arrows to move, Q to quit\n",score);}
void spawn_food(){int i;do{fx=1+(rand()%(W-2));fy=1+(rand()%(H-2));}while(fx==0||fy==0);for(i=0;i<pl;i++)if(px[i]==fx&&py[i]==fy)spawn_food();}
void move_snake(){int nx=px[0]+dx,ny=py[0]+dy;if(nx<=0||nx>=W-1||ny<=0||ny>=H-1){printf("Game Over! Score: %d\n",score);return;}for(int i=0;i<pl;i++)if(px[i]==nx&&py[i]==ny){printf("Game Over! Score: %d\n",score);return;}if(nx==fx&&ny==fy){score++;px[pl]=nx;py[pl]=ny;pl++;spawn_food();}else{for(int i=pl;i>0;i--){px[i]=px[i-1];py[i]=py[i-1];}px[0]=nx;py[0]=ny;}}
void snake_game(){while(1){srand(123);pl=3;dx=1;dy=0;score=0;px[0]=5;py[0]=5;px[1]=4;py[1]=5;px[2]=3;py[2]=5;spawn_food();while(1){draw_snake();int ch=getchar();if(ch=='\x1b'){getchar();ch=getchar();if(ch=='A'&&dy!=1){dx=0;dy=-1;}else if(ch=='B'&&dy!=-1){dx=0;dy=1;}else if(ch=='D'&&dx!=1){dx=-1;dy=0;}else if(ch=='C'&&dx!=-1){dx=1;dy=0;}}else if(ch=='q'||ch=='Q'){printf("Final Score: %d\n",score);cls();return;}int nx=px[0]+dx,ny=py[0]+dy;if(nx<=0||nx>=W-1||ny<=0||ny>=H-1){printf("Game Over! Score: %d\n",score);printf("Press any key to restart or Q to quit\n");int ch2=getchar();if(ch2=='q'||ch2=='Q'){cls();return;}else break;}for(int i=0;i<pl;i++)if(px[i]==nx&&py[i]==ny){printf("Game Over! Score: %d\n",score);printf("Press any key to restart or Q to quit\n");int ch2=getchar();if(ch2=='q'||ch2=='Q'){cls();return;}else break;}if(nx==fx&&ny==fy){score++;px[pl]=nx;py[pl]=ny;pl++;spawn_food();}else{for(int i=pl;i>0;i--){px[i]=px[i-1];py[i]=py[i-1];}px[0]=nx;py[0]=ny;}}}}
void main(void) {
    char cwd[128] = "/";
    while (1) {
prompt:
        printf("> ");
        char cmdline[128];
        size_t i = 0;
        for (;;) {
            char ch = getchar();
            if (ch == '\x1b') { getchar(); getchar(); continue; }
            if (ch == '\033') { getchar(); getchar(); continue; }
            if ((ch == '\b' || ch == 127) && i > 0) {
                i--;
                printf("\b \b");
                continue;
            }
            if (ch == '\t') {
                cmdline[i] = '\0';
                char *space = strrchr(cmdline, ' ');
                char *start = space ? space + 1 : cmdline;
                const char *commands[] = {"help", "hello", "exit", "pwd", "cd", "mkdir", "ls", "cat", "touch", "echo", "clear", "shutdown", "reboot", "rm"};
                char *match = NULL;
                for (int c = 0; c < 14; c++) {
                    if (!strncmp(start, commands[c], strlen(start))) {
                        if (match) {
                            match = (char*)-1;
                            break;
                        }
                        match = (char*)commands[c];
                    }
                }
                if (match && match != (char*)-1) {
                    for (size_t j = 0; j < strlen(match + strlen(start)); j++) {
                        cmdline[i++] = match[strlen(start) + j];
                        putchar(match[strlen(start) + j]);
                    }
                }
                continue;
            }
            if (ch != '\r' && ch != '\b' && ch != 127 && ch != '\t') {
                cmdline[i++] = ch;
                putchar(ch);
            }
            if (i >= sizeof(cmdline)) {
                printf("\ncommand line too long\n");
                goto prompt;
            } else if (ch == '\r') {
                cmdline[i] = '\0';
                putchar('\n');
                break;
            }
        }
        char *cmd = cmdline, *arg = NULL; for (char *p = cmdline; *p; p++) if (*p == ' ') { *p = '\0'; arg = p + 1; while (*arg == ' ') arg++; break; }
        if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
            printf("Commands: help, hello, exit, pwd, cd, mkdir, ls, cat, touch, rm, echo, clear, snake, shutdown, reboot\n"); else if (strcmp(cmd, "clear") == 0) printf("\033[2J\033[H");
        else if (strcmp(cmd, "hello") == 0) printf("Hello world from shell!\n");
        else if (strcmp(cmd, "exit") == 0) exit();
        else if (strcmp(cmd, "pwd") == 0) printf("%s\n", cwd);
        else if (strcmp(cmd, "cd") == 0) { if (!arg) printf("%s\n", cwd); else { char path[128]; if (arg[0] == '/') strcpy(path, arg); else if (strcmp(arg, "..") == 0) { if (strcmp(cwd, "/") != 0) { char *last_slash = strrchr(cwd, '/'); if (last_slash && last_slash != cwd) { int len = last_slash - cwd; memcpy(path, cwd, len); path[len] = '\0'; } else strcpy(path, "/"); } else strcpy(path, "/"); } else { strcpy(path, cwd); if (strcmp(cwd, "/") != 0) { int len = strlen(path); path[len] = '/'; path[len + 1] = '\0'; } strcpy(path + strlen(path), arg); } int path_len = strlen(path); if (path_len > 1 && path[path_len - 1] == '/') path[path_len - 1] = '\0'; char buf[256]; int len = listdir(path, buf, sizeof(buf)); if (len < 0) { printf("directory not found: %s\n", path); continue; } strcpy(cwd, path); } }
        else if (strcmp(cmd, "mkdir") == 0) { if (!arg) printf("usage: mkdir <path>\n"); else { char path[128]; if (arg[0] == '/') strcpy(path, arg); else { strcpy(path, cwd); if (strcmp(cwd, "/") != 0) strcpy(path + strlen(path), "/"); strcpy(path + strlen(path), arg); } int path_len = strlen(path); if (path_len > 1 && path[path_len - 1] == '/') path[path_len - 1] = '\0'; if (mkdir(path) < 0) printf("failed to create: %s\n", path); else printf("created: %s\n", path); } }
        else if (strcmp(cmd, "ls") == 0) { char path[128]; strcpy(path, arg ? arg : cwd); if (arg && arg[0] != '/') { strcpy(path, cwd); if (strcmp(cwd, "/") != 0) strcpy(path + strlen(path), "/"); strcpy(path + strlen(path), arg); } int path_len = strlen(path); if (path_len > 1 && path[path_len - 1] == '/') path[path_len - 1] = '\0'; char buf[256]; int len = listdir(path, buf, sizeof(buf)); if (len < 0) printf("failed to list: %s\n", path); else if (len > 0) printf("%s\n", buf); else printf("(empty)\n"); }
        else if (strcmp(cmd, "cat") == 0) { if (!arg) { printf("usage: cat <file>\n"); continue; } char path[128]; if (arg[0] == '/') strcpy(path, arg); else { strcpy(path, cwd); if (strcmp(cwd, "/") != 0) strcpy(path + strlen(path), "/"); strcpy(path + strlen(path), arg); } int path_len = strlen(path); if (path_len > 1 && path[path_len - 1] == '/') path[path_len - 1] = '\0'; char buf[128]; int len = readfile(path, buf, sizeof(buf)); if (len < 0) printf("file not found: %s\n", path); else { buf[len] = '\0'; printf("%s\n", buf); } }
        else if (strcmp(cmd, "touch") == 0) { if (!arg) { printf("usage: touch <file>\n"); continue; } char path[128]; if (arg[0] == '/') strcpy(path, arg); else { strcpy(path, cwd); if (strcmp(cwd, "/") != 0) strcpy(path + strlen(path), "/"); strcpy(path + strlen(path), arg); } int path_len = strlen(path); if (path_len > 1 && path[path_len - 1] == '/') path[path_len - 1] = '\0'; if (writefile(path, "", 0) < 0) printf("failed to create: %s\n", path); else printf("created: %s\n", path); }
        else if (strcmp(cmd, "rm") == 0) { if (!arg) printf("usage: rm [-r] <path>\n"); else { bool recursive = false; char *path_arg = arg; if (arg[0] == '-') { char *space = strchr(arg, ' '); if (space) { *space = '\0'; if (strcmp(arg, "-r") == 0 || strcmp(arg, "-rf") == 0 || strcmp(arg, "-fr") == 0) recursive = true; path_arg = space + 1; while (*path_arg == ' ') path_arg++; } else if (strcmp(arg, "-r") == 0) recursive = true; } if (!path_arg || *path_arg == '-') printf("usage: rm [-r] <path>\n"); else { char path[128]; if (path_arg[0] == '/') strcpy(path, path_arg); else { strcpy(path, cwd); if (strcmp(cwd, "/") != 0) strcpy(path + strlen(path), "/"); strcpy(path + strlen(path), path_arg); } int path_len = strlen(path); if (path_len > 1 && path[path_len - 1] == '/') path[path_len - 1] = '\0'; if ((recursive ? remove_r(path) : remove(path)) < 0) printf("failed to remove: %s\n", path); else printf("removed: %s\n", path); } } }
        else if (strcmp(cmd, "echo") == 0) { if (arg) printf("%s\n", arg); }
        else if (strcmp(cmd, "snake") == 0) { snake_game(); }
        else if (strcmp(cmd, "shutdown") == 0) shutdown();
        else if (strcmp(cmd, "reboot") == 0) reboot();
        else printf("unknown command: %s\n", cmd);
    }
}
