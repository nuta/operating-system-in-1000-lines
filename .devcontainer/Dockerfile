FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    clang \
    lld \
    qemu-system-riscv32 \
    curl \
    gnupg && \
    apt-get clean && rm -rf /var/lib/apt/lists/*
