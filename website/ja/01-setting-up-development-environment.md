---
title: 開発環境
---

# 開発環境

本書では、macOSとUbuntuといったUNIX系OSを想定しています。Windowsをお使いの場合は、Windows Subsystem for Linux (WSL2) をインストールしたのち、Ubuntuの手順に従ってください。

## ソフトウェアのインストール

### macOS

[Homebrew](https://brew.sh/index_ja) をインストールしたのち、次のコマンドで必要なパッケージをインストールします。

```
brew install llvm lld qemu
```

また、LLVMのユーティリティを使えるように `PATH` を設定します。

```
$ export PATH="$PATH:$(brew --prefix)/opt/llvm/bin"
$ which llvm-objcopy
/opt/homebrew/opt/llvm/bin/llvm-objcopy
```

### Ubuntu

次のコマンドで必要なパッケージをインストールします。他のLinuxディストリビューションをお使いの場合は、適宜読み替えてください。

```
sudo apt update && sudo apt install -y clang llvm lld qemu-system-riscv32 curl
```

加えてOpenSBI (PCでいうBIOS/UEFI) を作業用ディレクトリにダウンロードしておきます。

```
cd <開発用ディレクトリ>
curl -LO https://github.com/qemu/qemu/raw/v8.0.4/pc-bios/opensbi-riscv32-generic-fw_dynamic.bin
```

> [!WARNING]
>
> QEMUを実行する際に、 `opensbi-riscv32-generic-fw_dynamic.bin` がカレントディレクトリにある必要があります。別の場所にある場合、次の「ファイルが見当たらない」エラーが出ます。
>
> ```
> qemu-system-riscv32: Unable to load the RISC-V firmware "opensbi-riscv32-generic-fw_dynamic.bin"
> ```

### その他のOS

どうしても他のOSを使いたい場合は、次のコマンドを頑張ってインストールしてください。

- `bash`: コマンドラインシェル。UNIX系OSには基本的に最初から入っている。
- `tar`: tarアーカイブ操作ツール。UNIX系OSには基本的に最初から入っている。GNU版の`tar`がおすすめ。
- `clang`: Cコンパイラ。32ビットRISC-V CPUに対応していること (下記参照)。
> `lld`: LLVMリンカー。`clang`でコンパイルしたファイルたちを一つの実行ファイルにまとめる。
- `llvm-objcopy`: オブジェクトファイル編集ツール。よくLLVMパッケージに入っている。(GNU binutilsの`objcopy`でも代用可)。
- `llvm-objdump`: 逆アセンブラ。`llvm-objcopy`と同様。
- `llvm-readelf`: ELFファイル解析ツール。`llvm-objcopy`と同様。
- `qemu-system-riscv32`: 32ビットRISC-V CPUのエミュレータ。QEMUパッケージに入っている。

> [!TIP]
>
> お使いのclangが32ビットRISC-Vに対応しているかは、次のコマンドで確認できます。
>
> ```
> $ clang -print-targets | grep riscv32
>     riscv32     - 32-bit RISC-V
> ```
>
> `riscv32`が表示されればOKです。表示されない代表例としては、macOS標準のclangがあります。上記の手順では、Homebrewの全部入りclang (`llvm`パッケージ) を代わりにインストールしています。

## Gitリポジトリの用意 (任意)

もしGitリポジトリ下で作っていく場合は、次の`.gitignore`をあらかじめ用意しておくと便利です。

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
