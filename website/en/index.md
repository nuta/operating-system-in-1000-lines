# Operating System in 1,000 Lines

Hey there! In this book, we're going to build a small operating system from scratch, step by step.

You might get intimidated when you hear OS or kernel development, the basic functions of an OS (especially the kernel) are surprisingly simple. Even Linux, which is often cited as a huge open-source software, was only 8,413 lines in version 0.01. Today's Linux kernel is overwhelmingly large, but it started with a tiny codebase, just like your hobby project.

We'll implement basic context switching, paging, user mode, a command-line shell, a disk device driver, and file read/write operations in C. Sounds like a lot, however, it's only 1,000 lines of code!

One thing you should remember is, it's not as easy as it sounds. The tricky part of creating your own OS is debugging. You can't do `printf` debugging until you implement it. You'll need to learn different debugging techniques and skills you've never needed in application development. Especially when starting "from scratch", you'll encounter challenging parts like boot process and paging. But don't worry! We'll also learn "how to debug an OS" too!

The tougher the debugging, the more satisfying it is when it works. Let's dive into exciting world of OS development!

- You can download the implementation examples from [GitHub](https://github.com/nuta/operating-system-in-1000-lines).
- This book is available under the [CC BY 4.0 license](https://creativecommons.jp/faq). The implementation examples and source code in the text are under the [MIT license](https://opensource.org/licenses/MIT).
- The prerequisites are familiarity with the C language and with a UNIX-like environment. If you've run `gcc hello.c && ./a.out`, you're good to go!
- This book was originally written as an appendix of my book *[Design and Implementation of Microkernels](https://www.shuwasystem.co.jp/book/9784798068718.html)* (written in Japanese).

Happy OS hacking!
