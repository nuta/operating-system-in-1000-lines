---
title: Overview
layout: chapter
lang: en
---

> [!NOTE] Translation of this English version is in progress.

## Features in 1K LoC OS

In this book, we will implement the following major features:

- **Multitasking**: Switch between processes, making multiple programs appear to run simultaneously.
- **Exception handler**: Handle events requiring OS intervention, such as runtime errors.
- **Paging**: Independent memory spaces for each process.
- **System call**: Allow applications to call OS features.
- **Device drivers**: Abstract hardware functionalities, such as reading and writing to disks.
- **File system**: Manage files on disk.
- **Command-line shell**: User interface for humans.

## Features not implemented

Conversely, the following major features are omitted:

- **Interrupt handling**: Implemented using a polling method (periodically check for new data on devices), aka busy waiting.
- **Timer processing**: Preemptive multitasking is not implemented. Cooperative multitasking is implemented where each process voluntarily yields CPU.
- **Inter-process communication**: Such as pipe, UNIX domain socket, etc.
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
> In this book, _"user land"_ is sometimes abbreviated as _"user"_. Consider it as _applciations_.
