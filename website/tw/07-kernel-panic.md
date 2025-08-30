# 核心恐慌（Kernel Panic）

當作業系統核心遇到無法恢復的錯誤時，就會發生所謂的核心恐慌（kernel panic），這和 Go 或 Rust 語言中的 `panic` 概念類似。你有看過 Windows 的藍底白字畫面嗎？我們可以在自己的核心裡實作類似的機制來處理致命錯誤。

以下這個 `PANIC` 巨集（macro）就是我們的 kernel panic 實作：

```c [kernel.h]
#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}                                                           \
    } while (0)
```

這段巨集會印出發生 panic 的位置，然後進入一個無窮迴圈來停止後續執行。我們之所以把它寫成巨集，是因為這樣才能正確顯示發生 panic 的原始檔名（`__FILE__`）與行號（`__LINE__`）。如果你把它寫成一般函式，那麼 `__FILE__` 和 `__LINE__` 只會顯示該函式定義的地方，而不是你實際呼叫 `PANIC` 的位置。

這個巨集用了兩個常見技巧：

1. `do-while` 敘述：  
  由於條件是 `while (0)`，這個區塊只會執行一次。這是 C 語言中定義多行巨集時的常見寫法。如果你只單純用 `{ ... }` 包起來，在某些情況下（例如搭配 `if` 使用）可能會造成語法邏輯錯誤。請參考[這篇](https://www.jpcert.or.jp/sc-rules/c-pre10-c.html)清楚的範例說明。另外注意，每一行結尾的 `\` 是為了讓 C 預處理器把整個巨集當成單一行處理，讓換行不會被當成巨集的結尾。

2. `##__VA_ARGS__` 的技巧：  
  這是 GCC 提供的語法擴充，用來撰寫接受不定參數的巨集（參見 [GCC 官方說明](https://gcc.gnu.org/onlinedocs/gcc/Variadic-Macros.html)）。其中 `##` 是為了在沒有傳入任何額外參數時，自動移除前面的逗號 `,`，這樣如 `PANIC("booted!");` 這種只傳一個參數的寫法也能被正確編譯。

## Let's try it

讓我們來嘗試使用 `PANIC`，你可以如 `printf` 一樣使用它：

```c [kernel.c] {4-5}
void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    PANIC("booted!");
    printf("unreachable here!\n");
}
```

在 QEMU 中試跑看看，確認是否有正確顯示發生 panic 的檔案名稱與行號，而且 `PANIC` 之後的程式碼沒有被執行（也就是 `"unreachable here!"` 沒有出現）：

```
$ ./run.sh
PANIC: kernel.c:46: booted!
```

Windows 的藍底白字（Blue Screen）、Linux 的 kernel panic 畫面看起來雖然很可怕，但對你自己寫的 kernel 來說，這其實是個非常實用又貼心的功能！

它提供一種「優雅當機」的機制，能用人類可讀的錯誤訊息來提示錯誤發生的地方，比完全沒輸出、靜悄悄地掛掉要好太多了。
