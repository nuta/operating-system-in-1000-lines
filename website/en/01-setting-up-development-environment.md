---
title: Getting Started
layout: chapter
lang: en
---

This book assumes you're using a UNIX-like OS such as macOS or Ubuntu. If you're on Windows, no worries! Just install Windows Subsystem for Linux (WSL2) and follow the Ubuntu instructions.

## Installing development tools

### For macOS users

First things first, let's get [Homebrew](https://brew.sh) installed. Once that's done, run this command to get all the goodies you need:

```
brew install llvm qemu
```

### For Ubuntu fans

If you're on Ubuntu, just run this command to get everything set up.

```
sudo apt update && sudo apt install -y clang llvm lld qemu-system-riscv32 curl
```

Now, let's grab OpenSBI (think of it as BIOS/UEFI for PCs) and pop it in your working directory:

```
cd <your development directory>
curl -LO https://github.com/qemu/qemu/raw/v8.0.4/pc-bios/opensbi-riscv32-generic-fw_dynamic.bin
```

> [!WARNING]
>
> When you're running QEMU, make sure `opensbi-riscv32-generic-fw_dynamic.bin` is in your current directory. If it's not, you'll see this pesky error:
>
> ```
> qemu-system-riscv32: Unable to load the RISC-V firmware "opensbi-riscv32-generic-fw_dynamic.bin"
> ```

### Other OS users

If you're using a different OS and feeling brave, try to install these commands:

- `bash`: Your friendly neighborhood command-line shell. Usually comes pre-installed on UNIX-like OSes.
- `tar`: For handling those tar archives. Usually pre-installed on UNIX-like OSes too. Prefer GNU version, not BSD.
- `clang`: Your C compiler. Make sure it's ready for 32-bit RISC-V CPU (more on this below).
- `llvm-objcopy`: For editing object files. Often comes with the LLVM package. (You can use GNU binutils' `objcopy` in a pinch).
- `llvm-objdump`: Your trusty disassembler. Same deal as `llvm-objcopy`.
- `llvm-readelf`: For analyzing ELF files. Again, same as `llvm-objcopy`.
- `qemu-system-riscv32`: This emulates a 32-bit RISC-V CPU. It's part of the QEMU package.

> [!TIP]
>
> Want to check if your clang is ready for 32-bit RISC-V? Try this command:
>
> ```
> $ clang -print-targets | grep riscv32
>     riscv32     - 32-bit RISC-V
> ```
>
> You should see `riscv32`. The standard clang on macOS might not show this. That's why we're getting the full-featured clang (`llvm` package) from Homebrew in the steps above.

## Setting up a Git repository (optional)

If you're using a Git repository, here's a handy `.gitignore` file:

```plain:.gitignore
/disk/*
!/disk/.gitkeep
*.map
*.tar
*.o
*.elf
*.bin
*.log
*.pcap
```

You're all set! Let's start building that awesome OS of yours!
