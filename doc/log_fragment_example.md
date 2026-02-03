# Log record fragmentation example (跨 block + trailer padding)

本例是为了“可读性”而做的缩小版示意：
- **实际实现**：`kBlockSize = 32768`，`kHeaderSize = 7`。
- **本例为了画图**：假设 `block_size = 32`（其余算法完全一致）。

下面给出生成本例 CRC/字节布局的 **示例代码** 以及 **编译/运行命令**。

## 示例代码（生成布局所用）

```cpp
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include "util/crc32c.h"
#include "util/coding.h"

struct Frag {
  int type;
  std::string payload;
  uint32_t crc;
  unsigned char header[7];
};

static uint32_t TypeCrc(int type) {
  char t = static_cast<char>(type);
  return leveldb::crc32c::Value(&t, 1);
}

static Frag MakeFrag(int type, const std::string& payload) {
  Frag f;
  f.type = type;
  f.payload = payload;
  uint32_t crc = leveldb::crc32c::Extend(TypeCrc(type), payload.data(), payload.size());
  crc = leveldb::crc32c::Mask(crc);
  f.crc = crc;
  leveldb::EncodeFixed32(reinterpret_cast<char*>(f.header), crc);
  f.header[4] = static_cast<unsigned char>(payload.size() & 0xff);
  f.header[5] = static_cast<unsigned char>((payload.size() >> 8) & 0xff);
  f.header[6] = static_cast<unsigned char>(type);
  return f;
}

int main() {
  const int kHeaderSize = 7;
  const int block_size = 32; // illustrative
  int block_offset = 30;     // illustrative: leaves 2 bytes

  std::string data = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567"; // 60 bytes

  // Simulate AddRecord logic with custom block size.
  size_t left = data.size();
  size_t pos = 0;
  bool begin = true;
  std::vector<Frag> frags;
  int pad_bytes = 0;

  while (true) {
    int leftover = block_size - block_offset;
    if (leftover < kHeaderSize) {
      if (leftover > 0) {
        pad_bytes = leftover; // only once in this scenario
      }
      block_offset = 0;
    }

    size_t avail = block_size - block_offset - kHeaderSize;
    size_t frag_len = left < avail ? left : avail;

    bool end = (left == frag_len);
    int type = 0;
    if (begin && end) type = 1;        // kFullType
    else if (begin) type = 2;          // kFirstType
    else if (end) type = 4;            // kLastType
    else type = 3;                     // kMiddleType

    std::string payload = data.substr(pos, frag_len);
    frags.push_back(MakeFrag(type, payload));

    pos += frag_len;
    left -= frag_len;
    block_offset += kHeaderSize + frag_len;
    begin = false;

    if (left == 0) break;
  }

  printf("pad_bytes=%d\n", pad_bytes);
  for (size_t i = 0; i < frags.size(); ++i) {
    const Frag& f = frags[i];
    printf("frag%zu type=%d len=%zu crc=0x%08x\n", i, f.type, f.payload.size(), f.crc);
    printf("header: ");
    for (int j = 0; j < 7; ++j) printf("%02x ", f.header[j]);
    printf("\npayload: ");
    for (unsigned char c : f.payload) printf("%02x ", c);
    printf("\n\n");
  }
  return 0;
}
```

## 编译与运行命令

```bash
g++ -std=c++11 -DLEVELDB_PLATFORM_POSIX -I. -Iinclude \
  /tmp/log_layout_example.cc util/crc32c.cc util/coding.cc -pthread \
  -o /tmp/log_layout_example

/tmp/log_layout_example
```

我们从一个“非对齐”的文件末尾开始写入：
- 初始 `block_offset = 30`（当前 block 只剩 2 字节）。
- 写入的逻辑记录为 60 字节：
  ```
  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567"
  ```
  这 60 字节将被拆分成 3 个片段：
  - First: 25 bytes
  - Middle: 25 bytes
  - Last: 10 bytes

> 说明：CRC 使用 `crc32c::Extend(type_crc, payload)` 后再 `Mask`，并用 `EncodeFixed32` 以小端写入 header。

---

## 1) block 尾部 padding（trailer）

`block_offset = 30`，`block_size = 32`，剩余 2 字节 < header(7)，所以先补 0：

```
前一个 block (仅示意尾部 2 字节)
offset 30..31: 00 00   // trailer padding
```

然后开始新 block 写入第一片段。

---

## 2) Fragment #0 (kFirstType = 2, len=25)

Header + payload 共 32 字节，刚好填满一个 block。

- CRC(masked) = `0xf65e56e4`
- Header(hex) = `e4 56 5e f6 19 00 02`
- Payload(hex) = `61 62 ... 79` (a..y)

```
Block 1 (size 32)
00: e4 56 5e f6 19 00 02 61 62 63 64 65 66 67 68
10: 69 6a 6b 6c 6d 6e 6f 70 71 72 73 74 75 76 77
20: 78 79
```

> 注：这里按行展示，`00` 代表该行起始偏移。整块 32 字节已写满。

---

## 3) Fragment #1 (kMiddleType = 3, len=25)

- CRC(masked) = `0x7d8c8e6f`
- Header(hex) = `6f 8e 8c 7d 19 00 03`
- Payload(hex) = `7a 41 42 ... 58` (z A..X)

```
Block 2 (size 32)
00: 6f 8e 8c 7d 19 00 03 7a 41 42 43 44 45 46 47
10: 48 49 4a 4b 4c 4d 4e 4f 50 51 52 53 54 55 56
20: 57 58
```

---

## 4) Fragment #2 (kLastType = 4, len=10)

- CRC(masked) = `0x6aed0c8a`
- Header(hex) = `8a 0c ed 6a 0a 00 04`
- Payload(hex) = `59 5a 30 31 32 33 34 35 36 37` (Y Z 0..7)

```
Block 3 (size 32)
00: 8a 0c ed 6a 0a 00 04 59 5a 30 31 32 33 34 35
10: 36 37
```

该 block 只写了 17 字节，剩余 15 字节未使用。
**注意**：只有当“剩余空间 < 7”时，才会写 trailer padding；此处 `32 - 17 = 15` ≥ 7，所以不会自动补 0。

---

## 总结
- trailer padding 发生在“block 尾部不足 header”时。
- 逻辑记录被拆成 `First / Middle / Last`，每片都有独立 header+CRC。
- CRC 只覆盖 “type + payload”，不包括 header 本身。 
