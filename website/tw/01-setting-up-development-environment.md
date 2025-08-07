---
title: 開始
---

# 開始

本書假設你使用的是 UNIX 或類似 UNIX 的作業系統，例如 macOS 或 Ubuntu。如果你使用的是 Windows，請安裝適用於 Linux 的 Windows 子系統（WSL2）並按照 Ubuntu 說明進行操作。

## 安裝開發工具

### macOS

透過 [Homebrew](https://brew.sh) 安裝並執行此指令以取得你需要的所有工具：

```
brew install llvm lld qemu
```

此外，你需要將 LLVM binutils 添加到 PATH 中：

```
$ export PATH="$PATH:$(brew --prefix)/opt/llvm/bin"
$ which llvm-objcopy
/opt/homebrew/opt/llvm/bin/llvm-objcopy
```

### Ubuntu

使用 `apt` 安裝套件：

```
sudo apt update && sudo apt install -y clang llvm lld qemu-system-riscv32 curl
```

並下載 OpenSBI（將其視為 PC 的 BIOS/UEFI）：

```
curl -LO https://github.com/qemu/qemu/raw/v8.0.4/pc-bios/opensbi-riscv32-generic-fw_dynamic.bin
```

> [!WARNING]
>
> 當你執行 QEMU 時，請確保 `opensbi-riscv32-generic-fw_dynamic.bin` 位於目前的目錄中。如果不是，你將看到以下錯誤：
>
> ```
> qemu-system-riscv32: Unable to load the RISC-V firmware "opensbi-riscv32-generic-fw_dynamic.bin"
> ```

### 其他 OS 的使用者

如果你使用的是其他作業系統，請取得以下工具：

- `bash`: shell。通常它是預設安裝的。
- `tar`: 通常是預設安裝的。請使用 GNU 而不是 BSD 版本。
- `clang`: C 編譯器。請確保它支援 32 位 RISC-V CPU（見下文）。
- `lld`: LLVM 連結器（linker），是用來將已編譯的目標檔（object files）整合成一個可執行檔的工具。
- `llvm-objcopy`: 目標檔編輯器（Object file editor），隨 LLVM 套件一同提供（通常在 `llvm` 套件中）。
- `llvm-objdump`: 一個反組譯器（Disassembler），功能類似於 `llvm-objcopy`。
- `llvm-readelf`: 一個 ELF 檔案讀取工具，功能也類似於 `llvm-objcopy`。
- `qemu-system-riscv32`: 32 位元 RISC-V 處理器模擬器，是 QEMU 套件的一部分（通常包含在 `qemu` 套件中）。

> [!TIP]
>
> 如果要檢查你的 `clang` 是否支援 32 位元的 RISC-V CPU，執行以下命令：
>
> ```
> $ clang -print-targets | grep riscv32
>     riscv32     - 32-bit RISC-V
> ```
>
> 你應該看到 `riscv32`。請注意：macOS 預設安裝的 clang 不會顯示此結果，因此你需要另外安裝 Homebrew 版本的 `llvm` 套件。

## 設定 Git 儲存庫（可選）

如果你正在使用 Git 儲存庫，請使用以下 `.gitignore` 檔案來忽略不必要的檔案：

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

你已經準備就緒了！讓我們開始建構你的第一個作業系統吧！
