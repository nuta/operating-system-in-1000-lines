---
title: 환경설정
---

# 개발환경

이 책은 기본적으로 UNIX 계열 운영체제(예: macOS, Ubuntu)에서의 작업을 가정합니다. Windows를 사용한다면, 먼저 Windows Subsystem for Linux(WSL2)를 설치한 뒤, 아래의 Ubuntu 절차를 따르는 것을 추천합니다.

## 개발 툴 설치

### macOS 

1. [Homebrew](https://brew.sh) 를 설치합니다.
2. 터미널에서 다음 명령어를 실행하여 필요한 프로그램들을 설치합니다.

```
brew install llvm lld qemu
```

### Ubuntu

1. apt 패키지 관리자 업데이트 후, 필요한 패키지들을 설치합니다.


```
sudo apt update && sudo apt install -y clang llvm lld qemu-system-riscv32 curl
```

2. 추가로, OpenSBI 펌웨어를 다운로드합니다. 이는 PC의 BIOS/UEFI에 해당하는 역할을 합니다.

```
curl -LO https://github.com/qemu/qemu/raw/v8.0.4/pc-bios/opensbi-riscv32-generic-fw_dynamic.bin
```

> [!WARNING]
>
> QEMU를 실행할 때, opensbi-riscv32-generic-fw_dynamic.bin 파일이 현재 디렉터리에 반드시 있어야 합니다. 파일이 없으면 아래와 같은 오류가 발생합니다:
>
> ```
> qemu-system-riscv32: Unable to load the RISC-V firmware "opensbi-riscv32-generic-fw_dynamic.bin"
> ```

### 그 외의 운영체제

꼭 다른 OS를 써야 한다면, 다음 프로그램들을 직접 설치해야 합니다.

- `bash`: 일반적인 명령어 쉘 (대부분 기본 설치되어 있음)
- `tar`: tar 아카이브 툴 (대부분 기본 설치됨, BSD가 아닌 GNU버전 tar 권장)
- `clang`: C 컴파일러 (반드시 32비트 RISC-V를 지원해야 함, 아래 참조)
- `lld`: LLVM 링커 (여러 오브젝트 파일을 한 실행 파일로 묶음)
- `llvm-objcopy`: 오브젝트 파일 편집 툴 (주로 LLVM 패키지에 포함, GNU binutils의 objcopy도 대체 가능)
- `llvm-objdump`: 역어셈블러 (위 LLVM 패키지와 함께 제공) 
- `llvm-readelf`: ELF 파일 분석 툴 (역시 LLVM 패키지에 포함)
- `qemu-system-riscv32`: 32비트 RISC-V CPU 에뮬레이터 (QEMU 패키지에 포함)


> [!TIP]
>
> 사용 중인 `clang`이 32비트 RISC-V를 지원하는지 확인하려면 아래 명령어를 써보세요.
>
> ```
> $ clang -print-targets | grep riscv32
>     riscv32     - 32-bit RISC-V
> ```
>
> 여기서 riscv32 항목이 나오면 지원된다는 뜻입니다. 예를 들어, macOS 기본 clang은 32비트 RISC-V를 지원하지 않는 경우가 많기 때문에, Homebrew를 통해 설치한 llvm 패키지를 사용하는 것입니다.

## Git 레포지토리 설정 (선택사항)

프로젝트를 Git으로 관리하는 경우, 아래 .gitignore 파일을 만들어두면 여러모로 편리합니다.

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

이제 끝났습니다! 이제 바로 OS 만들기의 첫 여정을 시작해봅시다!
