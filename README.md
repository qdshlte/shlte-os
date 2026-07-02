# Shlte OS

一个仿 Linux 内核的 ARM64 微操作系统。

## 概述

Shlte OS 是一个从零开始的 ARM64 微内核操作系统，具有以下特性：

- **ARM64 (AArch64)** 架构支持
- **GRUB** 引导加载器
- **MMU/页表管理** - 支持虚拟内存
- **中断控制器** - GICv2/v3 支持
- **内核打印** - 通过 UART 输出
- **进程管理** - 基本进程创建和调度
- **系统调用** - 用户态到内核态的接口
- **根文件系统** - 基于 init 脚本和 sh/bash

## 项目结构

```
shlte-os/
├── Makefile              # 主构建文件
├── linker.lds            # 内核链接脚本
├── boot/
│   └── boot.S            # 引导扇区（汇编入口）
├── arch/
│   └── arm64/
│       ├── kernel.c      # 内核主入口
│       ├── mm/
│       │   └── mmu.c     # MMU/页表管理
│       ├── interrupt/
│       │   ├── interrupt.c   # GIC 中断控制器
│       │   └── exception.c   # 异常处理
│       └── fs/
│           └── fs.c      # 文件系统接口
├── lib/
│   ├── printk.c          # 内核打印/UART 驱动
│   ├── string.c          # 字符串/内存操作
│   ├── mm.c              # 内存管理（简单分配器）
│   ├── process.c         # 进程管理
│   ├── syscall.c         # 系统调用
│   ├── interrupt_dispatch.c  # IRQ 分发
│   └── include/shlte/    # 内核头文件
│       ├── types.h
│       ├── printk.h
│       ├── string.h
│       ├── mm.h
│       ├── interrupt.h
│       ├── fs.h
│       ├── stdio.h
│       └── process.h
├── rootfs/               # 根文件系统
│   ├── bin/
│   ├── sbin/
│   ├── etc/
│   │   ├── init.sh       # 初始化脚本
│   │   ├── environment
│   │   └── passwd
│   ├── proc/
│   ├── dev/
│   └── tmp/
├── scripts/
│   └── build.sh          # 构建辅助脚本
└── README.md
```

## 构建要求

### 必需工具

- **GCC 交叉编译工具链** (aarch64-linux-gnu-)
- **QEMU** (用于模拟运行)
- **GNU Make**

### 安装依赖 (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install gcc-aarch64-linux-gnu qemu-system-arm make genisoimage
```

### 安装依赖 (Fedora/RHEL)

```bash
sudo dnf install aarch64-linux-gnu-gcc qemu-system-arm make cdrkit
```

### 安装依赖 (Arch Linux)

```bash
sudo pacman -S gcc-aarch64-linux-gnu qemu make cdrkit
```

## 构建

### 完整构建（内核 + ISO）

```bash
make
```

### 仅构建内核

```bash
make kernel
```

### 仅构建 ISO

```bash
make iso
```

### 清理

```bash
make clean
```

## 运行

### 在 QEMU 中运行

```bash
make run
```

### 调试模式（等待 GDB 连接）

```bash
make debug
```

然后在另一个终端连接 GDB：

```bash
aarch64-linux-gnu-gdb build/kernel.elf
(gdb) target remote localhost:1234
(gdb) continue
```

### 使用构建脚本

```bash
./scripts/build.sh build    # 构建
./scripts/build.sh run      # 运行
./scripts/build.sh debug    # 调试
./scripts/build.sh clean    # 清理
```

## 技术细节

### 引导流程

1. **QEMU** 加载 kernel.bin 到物理地址 0x80000
2. **boot.S** 执行：
   - 保存设备树指针
   - 设置异常向量表
   - 从 EL2 降级到 EL1
   - 清除 BSS 段
   - 启用 MMU 和缓存
   - 跳转到 C 代码 `kernel_main()`
3. **kernel.c** 执行：
   - 打印内核横幅
   - 初始化 MMU
   - 初始化中断控制器
   - 初始化文件系统
   - 创建 init 进程
4. **init.sh** 启动用户空间

### 内存布局

```
0x00000000          - 保留
0x08000000          - GIC 寄存器 (virt machine)
0x09000000          - UART (virt machine)
0x00000080000       - 内核加载地址
0x00000010000000    - 内核堆起始 (1MB)
```

### 异常向量表

```
EL1t:  同步、IRQ、FIQ、错误
EL0t:  同步、IRQ、FIQ、错误
```

### 中断控制

- **GICv2** 分发器和 CPU 接口
- 支持 1020 个中断源
- 动态注册/注销中断处理程序

## 当前状态

这是一个**早期开发版本**，以下功能为 stub/占位符：

- [x] 内核入口和启动
- [x] UART 输出
- [x] 内核打印 (printk)
- [x] MMU 初始化
- [x] 页表管理（基本）
- [x] GIC 中断控制器
- [x] 异常处理
- [x] 简单内存分配器
- [x] 进程创建（基本）
- [x] 系统调用框架
- [x] 根文件系统
- [ ] 真正的文件系统（ext2/squashfs）
- [ ] 完整的进程调度
- [ ] 用户态内存管理
- [ ] 网络协议栈
- [ ] 多核支持
- [ ] 电源管理

## 未来计划

- 实现 ext2 文件系统支持
- 添加网络协议栈（TCP/IP）
- 支持更多 ARM64 平台（Not-So-Virtual、Real hardware）
- 实现用户态 C 库
- 添加动态链接器
- 实现信号机制
- 添加 IPC（进程间通信）
- 支持容器化

## 许可证

MIT License

## 贡献

欢迎提交 Issue 和 Pull Request！

---

**Shlte OS** - 从零开始的 ARM64 操作系统
