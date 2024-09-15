---
title: Intro
layout: chapter
lang: en
---

Hey there! In this book, we're going to build a small operating system from scratch, step by step.

Now, don't get intimidated when you hear "OS"! The basic functions of an OS (especially the kernel) are surprisingly simple. Even Linux, which is often cited as a huge open-source software, was only 8,413 lines in version 0.01. Today's Linux kernel is overwhelmingly large, but it started with a tiny codebase, just like your hobby project.

In this online book, we'll implement basic context switching, paging, user mode, a command-line shell, a disk device driver, and file read/write operations in C. Despite all these features, the total code is less than 1000 lines!

You might think you could do that in a day! However, it'll probably take at least three days. The tricky part of creating your own OS is debugging. You can't do `printf` debugging until you implement it! You'll need to learn different debugging techniques and skills compared to application development. Especially when starting "from scratch," you'll encounter challenging parts like boot processing and paging right at the beginning. So, we'll also learn "how to debug a homemade OS" together.

But remember, the tougher the debugging, the more satisfying it is when it works! So, dive in and experience the exciting world of OS development through this book.

- You can download the implementation examples from [GitHub](https://github.com/nuta/operating-system-in-1000-lines).
- This book is available under the [CC BY 4.0 license](https://creativecommons.jp/faq). The implementation examples and source code in the text are under the [MIT license](https://opensource.org/licenses/MIT).
- The prerequisites are "familiarity with C language" and "being familiar with UNIX-like command-line shells."
- This book was written as an appendix of ["Design and Implementation of Microkernels Learned Through Homemade OS"](https://www.shuwasystem.co.jp/book/9784798068718.html).

Happy OS hacking!
