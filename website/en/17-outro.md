# Outro

Congratulations! You've completed the book. You've learned how to implement a simple OS kernel from scratch. You've learned about the basic concepts of operating systems, such as booting CPU, context switching, page table, user mode, system call, disk I/O, and file system.

Although it's less than 1000 lines, it must have been quite challenging. This is because you built the core of the core of the core of a kernel.

For those who are not satisfied yet and want to continue with something, here are some next steps:

## Add new features

In this book, we implemented the basic features of a kernel. However, there are still many features that can be implemented. For example, it would be interesting to implement the following features:

- A proper memory allocator that allows freeing memory.
- Interrupt handling. Do not busy-wait for disk I/O.
- A full-fledged file system. Implementing ext2 would be a good start.
- Network communication (TCP/IP). It's not hard to implement UDP/IP (TCP is somewhat advanced). Virtio-net is very similar to virtio-blk!

## Read other OS implementations

The most recommended next step is to read the implementation of existing OSes. Comparing your implementation with others is very educational.

My favorite is this [RISC-V version of xv6](https://github.com/mit-pdos/xv6-riscv). This is a UNIX-like OS for educational purposes, and it comes with an [explanatory book (in English)](https://pdos.csail.mit.edu/6.828/2022/). It's recommended for those who want to learn about UNIX-specific features like `fork(2)`.

Another one is my project [Starina](https://starina.dev), a microkernel-based OS written in Rust. This is still very experimental, but would be interesting for those who want to learn about microkernel architecture and how Rust shines in OS development.

## Feedback is very welcome!

If you have any questions or feedback, please feel free to ask on [GitHub](https://github.com/nuta/operating-system-in-1000-lines/issues), or [send me an email](https://seiya.me) if you prefer. Happy your endless OS programming!
