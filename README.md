# 🌐 CMPSC 311 – Assignment 5: mdadm Networking Layer (Fall 2024)

This repository contains my solution for **Assignment 5** of CMPSC 311: Systems Programming at Penn State. In this lab, I implemented a **networked version** of the `mdadm` storage interface. Instead of performing local disk operations, the client now communicates with a remote JBOD server via TCP using a custom binary protocol.

---

## 📦 Objectives

- ✅ Replace all calls to `jbod_operation()` with `jbod_client_operation()`
- ✅ Implement `net.c` to handle network connection, sending, and receiving protocol messages
- ✅ Maintain the existing interface while introducing networking support
- ✅ Ensure correctness via trace-based testing
- ✅ Reuse existing cache logic from Assignment 4

---

## 🔗 Protocol Overview

Communication between client and server is based on a **fixed-format binary protocol**:

| Bytes  | Field       | Description                                  |
|--------|-------------|----------------------------------------------|
| 1–4    | Opcode      | JBOD operation (as defined in Lab 2)         |
| 5      | Info Code   | Bit 0: return code (0 or -1), Bit 1: payload |
| 6–261  | Payload     | Block data (if required by the command)      |

Both request and response messages follow this format.

---

## 🧩 Functions Implemented

### In `net.c`:

- `int jbod_connect(const char *ip, uint16_t port)`
  - Establishes a TCP socket connection to the JBOD server.

- `int jbod_disconnect(void)`
  - Closes the open socket connection cleanly.

- `int jbod_client_operation(uint32_t op, uint8_t *block)`
  - Core networked JBOD interface replacing `jbod_operation`.
  - Sends request packets, waits for response, processes results.

- `int nread(int fd, int len, uint8_t *buf)`
  - Helper to reliably read `len` bytes from socket `fd`.

- `int nwrite(int fd, int len, uint8_t *buf)`
  - Helper to reliably write `len` bytes to socket `fd`.

- `int send_packet(int fd, uint32_t op, uint8_t *block)`
  - Constructs and sends a JBOD request packet.

- `int recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block)`
  - Receives and decodes a JBOD response packet.
./tester -w traces/linear-input -s 1024 >x
./tester -w traces/random-input -s 1024 >x

---

## 🧪 Testing Instructions

### 1️⃣ Start the JBOD Server (Terminal 1)
```bash
./jbod_server
# or for verbose mode:
./jbod_server -v


./tester -w traces/simple-input -s 1024 >x
diff x traces/simple-expected-output

