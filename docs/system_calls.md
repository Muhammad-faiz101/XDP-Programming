# Linux System Calls – Revision Notes

## What is a System Call?

A **system call** is the controlled interface through which a user-space program requests services from the Linux kernel.

Examples:

* `read()`
* `write()`
* `open()`
* `close()`
* `fork()`
* `execve()`

---

## Why Do We Need System Calls?

User programs execute in **User Mode (Ring 3)** and cannot:

* Access hardware directly
* Execute privileged CPU instructions
* Access kernel memory
* Manage processes or memory directly

The kernel executes in **Kernel Mode (Ring 0)** and provides these services safely through system calls.

---

## User Space vs Kernel Space

| User Space                      | Kernel Space                               |
| ------------------------------- | ------------------------------------------ |
| Applications                    | Linux Kernel                               |
| Limited privileges              | Full privileges                            |
| Cannot access hardware directly | Controls CPU, memory, devices, filesystems |

---

## Complete System Call Lifecycle

```text
Application
    │
    ▼
glibc wrapper (read/write/...)
    │
    ▼
Registers prepared
(RAX = syscall number,
 RDI/RSI/RDX... = arguments)
    │
    ▼
syscall instruction
    │
    ▼
CPU switches:
- Ring 3 → Ring 0
- User Stack → Kernel Stack
    │
    ▼
Kernel Entry
    │
    ▼
System Call Dispatcher
    │
    ▼
sys_read()/sys_write()/...
    │
    ▼
Return value placed in RAX
    │
    ▼
CPU returns to User Mode
    │
    ▼
glibc
    │
    ▼
Application
```

---

## Role of glibc

Applications usually call:

```c
read(fd, buf, count);
```

This is **not** the kernel function.

`glibc`:

* Provides the user-space API
* Loads syscall number into `RAX`
* Places arguments into registers
* Executes the `syscall` instruction
* Converts negative kernel error codes into `errno`

---

## Register Convention (x86-64)

### System Call Number

| Register | Purpose            |
| -------- | ------------------ |
| `RAX`    | System call number |

### Arguments

| Register | Argument |
| -------- | -------- |
| `RDI`    | 1st      |
| `RSI`    | 2nd      |
| `RDX`    | 3rd      |
| `R10`    | 4th      |
| `R8`     | 5th      |
| `R9`     | 6th      |

### Return Value

| Register | Purpose                             |
| -------- | ----------------------------------- |
| `RAX`    | Return value or negative error code |

---

## What Happens During `syscall`?

The CPU automatically:

* Switches from Ring 3 to Ring 0
* Switches from the user stack to the kernel stack
* Jumps to the kernel entry point
* Starts executing kernel code

---

## Why Switch to a Kernel Stack?

The kernel must never trust the user stack because:

* User memory is untrusted
* It may be invalid or unmapped
* It could be intentionally manipulated by malicious programs

Every process has its own dedicated kernel stack.

---

## System Call Dispatcher

The kernel entry code is generic.

Its job is to:

1. Read the syscall number from `RAX`
2. Look it up in the syscall table
3. Call the appropriate handler

Conceptually:

```c
handler = syscall_table[rax];
handler();
```

---

## `SYSCALL_DEFINE`

Linux defines system calls using macros.

Example:

```c
SYSCALL_DEFINE3(read,
                unsigned int, fd,
                char __user *, buf,
                size_t, count)
```

The number (`3`) indicates the number of arguments.

Examples:

* `SYSCALL_DEFINE0`
* `SYSCALL_DEFINE1`
* `SYSCALL_DEFINE2`
* ...

These macros generate the required boilerplate for Linux's syscall infrastructure.

---

## `__user`

Example:

```c
char __user *buf;
```

`__user` indicates that the pointer refers to **user-space memory**.

The kernel must **never** dereference it directly; it uses safe helper functions like:

* `copy_from_user()`
* `copy_to_user()`

---

## Error Handling

Inside the kernel:

```text
-EBADF
-EINVAL
-EFAULT
-ENOMEM
```

Negative values represent errors.

`glibc` converts:

```text
Kernel: -EBADF
```

into:

```c
return -1;
errno = EBADF;
```

---

## Key Components

| Component        | Responsibility                  |
| ---------------- | ------------------------------- |
| Application      | Requests kernel service         |
| glibc            | User-space wrapper              |
| `syscall`        | CPU instruction to enter kernel |
| Kernel Entry     | First kernel code executed      |
| Dispatcher       | Chooses the correct syscall     |
| `SYSCALL_DEFINE` | Implements the syscall          |
| Kernel Exit      | Returns to user mode            |

---

# Key Takeaways

* System calls are the **bridge between user space and kernel space**.
* Applications normally invoke **glibc**, not the kernel directly.
* The CPU enters the kernel using the **`syscall` instruction**.
* The kernel selects the appropriate handler using the **system call table**.
* Arguments are passed through **CPU registers**, not the stack.
* Return values are delivered in **`RAX`**.
* Kernel errors are negative values and are translated into **`errno`** by `glibc`.
* The kernel always switches to a **kernel stack** before executing privileged code.
