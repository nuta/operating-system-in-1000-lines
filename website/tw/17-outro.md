# Outro

恭喜你！你已經完成了這本書。你學會了如何從零開始實作一個簡單的作業系統核心，並且學習了作業系統的基本概念，例如 CPU 啟動、上下文切換、頁面表、使用者模式、系統呼叫、磁碟 I/O 和檔案系統。

雖然整體不到 1000 行，但這過程一定相當有挑戰性。因為你所打造的是核心中之核心 ― 作業系統核心的核心。

對於仍覺得意猶未盡，想繼續挑戰的人，以下是一些建議的下一步：

## Add new features

在這本書中，我們實作了核心的基本功能。但還有很多其他功能可以擴充。例如，以下這些會是有趣的挑戰：

- 一個可以釋放記憶體的完整記憶體配置器。
- 中斷處理機制，避免對磁碟 I/O 使用 busy-wait。
- 一個完整的檔案系統，例如從實作 ext2 開始。
- 網路通訊（TCP/IP）。實作 UDP/IP 並不難（TCP 則較進階）。Virtio-net 與 virtio-blk 非常相似！

## Read other OS implementations

最推薦的下一步是閱讀現有作業系統的實作。將你的實作與其他系統比較，是非常有收穫的學習方式。

我最喜歡的是 [xv6 的 RISC-V 版本](https://github.com/mit-pdos/xv6-riscv)。這是一個為教育用途設計的類 UNIX 作業系統，並附有一份 [英文說明書](https://pdos.csail.mit.edu/6.828/2022/)。非常適合想學習 UNIX 特有功能（例如 `fork(2)`）的讀者。

另一個是我自己的專案 [Starina](https://starina.dev)，一個以 Rust 撰寫的微核心作業系統。它仍處於實驗階段，但對於想了解微核心架構及 Rust 在 OS 開發中優勢的人來說，會是個很好的參考。

## Feedback is very welcome!

如果你有任何問題或回饋，歡迎在 [GitHub 上發問](https://github.com/nuta/operating-system-in-1000-lines/issues)，或者如果你喜歡，也可以[寄信給我](https://seiya.me)。祝你在 OS 開發的旅程中持續快樂前行！
