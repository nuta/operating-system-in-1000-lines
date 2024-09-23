---
title: Outro
layout: chapter
lang: en
---

> [!NOTE]
>
> **Translation of this English version is in progress.**

This is where the book ends. Although it's less than 1000 lines, it must have been quite challenging.

For those who are not satisfied yet and wants to continue with something, here are somew next steps:

## Read HinaOS or xv6

Now that you've learned about implementing unique features like paging and exception handling, which are not seen in application development, the most recommended next step is to "read the implementation of existing OSes." Comparing your implementation with others and learning "how others implement it" is very educational.

One of examples is [RISC-V version of xv6](https://github.com/mit-pdos/xv6-riscv). This is a UNIX-like OS for educational purposes, and it comes with an [explanatory book (in English)](https://pdos.csail.mit.edu/6.828/2022/). It's recommended for those who want to learn about UNIX-specific features like `fork(2)`.

## Add New Features

In this book, we implemented the basic functions of a kernel. However, there are still many features that can be implemented. For example, it would be interesting to implement the following features:

- A proper memory allocator
- Interrupts
- A full-fledged file system
- Network communication (TCP/IP)

Happy your endless OS programming!
