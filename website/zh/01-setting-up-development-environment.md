---
title: 入门
---

# 入门

本书假设你正在使用 UNIX 或类 UNIX 的操作系统，例如 macOS 或者 Ubuntu。如果你用的是 Windows，那么你需要安装 Windows Subsystem for Linux (WSL2)，然后按照 Ubuntu 的说明做。

## 安装开发工具

### macOS

安装 [Homebrew](https://brew.sh) 然后运行下面这个命令去获取所有需要用到的工具：

```
brew install llvm lld qemu
```

### Ubuntu

使用 `apt` 安装所需的包：

```
sudo apt update && sudo apt install -y clang llvm lld qemu-system-riscv32 curl
```

然后下载 OpenSBI（把它理解成是 PC 的 BIOS/UEFI）：

```
curl -LO https://github.com/qemu/qemu/raw/v8.0.4/pc-bios/opensbi-riscv32-generic-fw_dynamic.bin
```

> [!WARNING]
>
> 当你运行 QEMU 的时候，确保 `opensbi-riscv32-generic-fw_dynamic.bin` 是在当前目录的。否则你将会看到这个错误：
>
> ```
> qemu-system-riscv32: Unable to load the RISC-V firmware "opensbi-riscv32-generic-fw_dynamic.bin"
> ```

### 其他操作系统的用户

如果你用的是其他操作系统，你需要准备好下面的工具：

- `bash`：命令行工具。通常是操作系统预装的。
- `tar`：通常是操作系统预装的。推荐 GUN 版而不是 BSD 版。
- `clang`：C 编译器。确保它支持 32 位 RISC-V CPU（参考后续说明）。
- `lld`： LLVM 链接器，将编译后的目标文件链接成可执行文件。
- `llvm-objcopy`：目标文件编辑器。附带了 LLVM 包（通常是 `llvm` 包）。
- `llvm-objdump`：反汇编程序。与 `llvm-objcopy` 一样。
- `llvm-readelf`：ELF 文件阅读器。与 `llvm-objcopy` 一样。
- `qemu-system-riscv32`：32 位的 RISC-V CPU 模拟器。它是 QEMU 包的一部分（通常是 `qemu` 包）。

> [!TIP]
>
> 用下面这个命令检查你的 `clang` 是否支持 32 位 RISC-V CPU：
>
> ```
> $ clang -print-targets | grep riscv32
>     riscv32     - 32-bit RISC-V
> ```
>
> 你应该能看到 `riscv32`。注意，macOS 自带的 clang 不会显示。这就是为什么你需要用 Homebrew 安装 `llvm` 包中的另一个 `clang`。

## 设置 Git 仓库（可选）

如果你在用 Git 仓库管理代码，添加下面的 `.gitignore` 文件：

```gitignore [.gitignore]
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

到这里你已经做好准备了，接下来请开始构建你的第一个操作系统！
