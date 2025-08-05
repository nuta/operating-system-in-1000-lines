# 我們將實作哪些功能

在開始打造作業系統之前，讓我們快速了解一下本書要實作的功能項目。

## 這本書中的 1000 行作業系統會包含哪些功能？

我們將實作以下主要功能：

- **多工（Multitasking）**: 實作行程切換，讓多個應用程式可以共用 CPU。
- **例外處理器（Exception handler）**: 處理像非法指令這類需要作業系統介入的事件。
- **分頁機制（Paging）**: 為每個應用程式提供獨立的記憶體位址空間。
- **系統呼叫（System calls）**: 讓應用程式能夠呼叫核心功能。
- **裝置驅動程式（Device drivers）**: 抽象化硬體功能，例如磁碟的讀寫。
- **檔案系統（File system）**: 管理磁碟上的檔案內容。
- **命令列 Shell**: 提供人類操作的使用者介面。

## 本書中未實作的功能

以下是本書不會實作的功能：

- **中斷處理（Interrupt handling）**: 改用輪詢（polling）方式，也就是忙碌等待（busy waiting），定期檢查裝置是否有新資料。
- **定時器處理（Timer processing）**: 不實作「搶佔式多工」，而是採用「合作式多工」，每個行程自行決定何時讓出 CPU。
- **行程間通訊（Inter-process communication, IPC）**: 例如管線（pipe）、UNIX domain socket、共享記憶體等功能將不包含在內。
- **多處理器支援（Multi-processor support）**: 本系統僅支援單一處理器。

## 原始碼結構

我們將從零開始、循序漸進地建構系統，最終檔案架構如下：

```
├── disk/     - 檔案系統內容
├── common.c  - 核心／使用者通用函式：printf、memset 等
├── common.h  - 核心／使用者通用定義：結構與常數定義
├── kernel.c  - 核心程式碼：行程管理、系統呼叫、驅動程式、檔案系統
├── kernel.h  - 核心專用定義檔：結構與常數定義
├── kernel.ld - 核心的 linker script（記憶體配置）
├── shell.c   - 命令列 Shell
├── user.c    - 使用者程式庫：系統呼叫介面
├── user.h    - 使用者程式庫定義：結構與常數
├── user.ld   - 使用者程式的 linker script（記憶體配置）
└── run.sh    - 建置腳本（Build script）
```

> [!TIP]
>
> 在本書中，會使用「user」或「user land」表示「使用者空間程式」，即應用程式。請不要將其與「使用者帳號（user account）」混淆！
