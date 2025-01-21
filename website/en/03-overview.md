# What we will implement

Before starting to build an OS, let's quickly get an overview of the features we will implement.

## Features in 1K LoC OS

In this book, we will implement the following major features:

- **Multitasking**: Switch between processes to allow multiple applications to share the CPU.
- **Exception handler**: Handle events requiring OS intervention, such as illegal instructions.
- **Paging**: Provide an isolated memory address space for each application.
- **System calls**: Allow applications to call kernel features.
- **Device drivers**: Abstract hardware functionalities, such as disk read/write.
- **File system**: Manage files on disk.
- **Command-line shell**: User interface for humans.

## Features not implemented

The following major features are not implemented in this book:

- **Interrupt handling**: Instead, we will use a polling method (periodically check for new data on devices), also known as busy waiting.
- **Timer processing**: Preemptive multitasking is not implemented. We'll use cooperative multitasking, where each process voluntarily yields the CPU.
- **Inter-process communication**: Features such as pipe, UNIX domain socket, and shared memory are not implemented.
- **Multi-processor support**: Only single processor is supported.

## Source code structure

We'll build from scratch incrementally, and the final file structure will look like this:

```
├── disk/     - File system contents
├── common.c  - Kernel/user common library: printf, memset, ...
├── common.h  - Kernel/user common library: definitions of structs and constants
├── kernel.c  - Kernel: process management, system calls, device drivers, file system
├── kernel.h  - Kernel: definitions of structs and constants
├── kernel.ld - Kernel: linker script (memory layout definition)
├── shell.c   - Command-line shell
├── user.c    - User library: functions for system calls
├── user.h    - User library: definitions of structs and constants
├── user.ld   - User: linker script (memory layout definition)
└── run.sh    - Build script
```

> [!TIP]
>
> In this book, "user land" is sometimes abbreviated as "user". Consider it as "applications", and do not confuse it with "user account"!
