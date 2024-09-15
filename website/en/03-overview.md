---
title: Overview
layout: chapter
lang: en
---

## Features to be implemented in this book

The OS we'll build in this book will have the following major features:

- **Multitasking**: A function that switches between processes, making multiple programs appear to run simultaneously
- **Exception Handler**: A function that handles events requiring OS intervention, such as runtime errors
- **Paging**: Realizes independent memory spaces for each process
- **System Calls**: A function that allows applications to call OS features
- **Device Drivers**: Functions that operate hardware, such as reading and writing to disks
- **File System**: A function that manages files on disk
- **Command-line Shell**: A function that allows users to input commands to call OS features

## Features not implemented in this book

Conversely, the following major features are omitted:

- **Interrupt Handling**: Implemented using a polling method that "periodically checks for new data on devices"
- **Timer Processing**: Preemptive multitasking is not implemented. Cooperative multitasking is implemented where each process voluntarily yields CPU
- **Inter-process Communication**: Functions for exchanging data between processes are not implemented
- **Multi-processor Support**: Only single processor is supported

Of course, these features can be implemented later. After completing the implementation in this book, it would be good to try implementing them while referring to projects like [HinaOS](https://github.com/nuta/microkernel-book).

## Source Code Structure

We'll build from scratch gradually, but the final file structure will look like this:

```
├── disk/     - File system contents
├── common.c  - Kernel/user common library: functions like printf, memset, etc.
├── common.h  - Kernel/user common library: definitions of structures, constants, etc.
├── kernel.c  - Kernel: process management, system calls, device drivers, file system
├── kernel.h  - Kernel: definitions of structures, constants, etc.
├── kernel.ld - Kernel: linker script (memory layout definition)
├── shell.c   - Command-line shell
├── user.c    - User library: functions for system calls, etc.
├── user.h    - User library: definitions of structures, constants, etc.
├── user.ld   - User: linker script (memory layout definition)
└── run.sh    - Build script
```

> [!TIP]
>
> In this book, _"user land"_ is sometimes abbreviated as _"user"_. Consider it as _applciations_.
