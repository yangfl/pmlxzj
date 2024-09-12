# 《屏幕录像专家》EXE 播放器文件格式

前面是固定代码区，数据区直接附加在文件末尾，格式：

1. 音频偏移标记（`int32_t`）
2. 视频/音频流
3. 鼠标事件（第一个 txt），及 txt 大小（`int32_t`）
4. 关键帧偏移（第二个 txt），及 txt 大小（`int32_t`）
5. 播放器初始选项（大小：`0xb4`）
6. 数据区标记（大小：`0x2c`）


## 数据区标记

播放器启动时，会首先读取文件末尾 `0x2c` 数据，然后试图定位数据区标记。

部分偏移意义及解释：

* `0x00` (`uint32_t`)：“编辑加密”锁定时的随机值
* `0x04` (`uint32_t`)：播放密码哈希
* `0x1c` (`uint32_t`)：音频偏移标记的偏移
* `0x20` (`char[0x0c]`)：播放器特征码 `pmlxzjtlx`（屏幕录像专家 天狼星 http://www.tlxsoft.com/ ）

### 播放密码哈希

```c
const unsigned char *password;
unsigned int cksum = 2005;
for (unsigned int i = 0; password[i] != '\0'; i++) {
  cksum += password[i] * (i + i / 5 + 1);
}
return cksum;
```

### 播放密钥

第一个字符总是被忽略。

```c
for (unsigned int i = 0; i < 20; i++) {
  key[i] = password[20 - i];
}
```

## 播放器初始选项

存放启动播放器时的默认设置，包括是否全屏、窗口标题、是否禁用播放控件等。部分关键信息：

* `0x38` (`char[24]`)：播放器标题
* `0x50` (`uint32_t`)：视频编码方式
* `0x54` (`uint32_t`)：音频编码方式


## 鼠标事件

第一个 txt 存放鼠标事件（光标增强，`gbzq`），txt 的大小（`int32_t`）附加在 txt 后（倒序读取），每行意义:

1. 事件发生帧号
2. 事件 left 坐标
3. 事件 top 坐标
4. 事件类型，1 为左键（红圈），2 为右键（黄圈），3 为双击（双红圈）（`txtype`）
5. 事件 left 坐标偏移（之于视频流中 left 坐标）
6. 事件 top 坐标偏移（之于视频流中 top 坐标）
7. 未知（0）
8. 未知（0）

以此循环。


## 关键帧偏移

第二个 txt 存放关键帧偏移（`guanjianzhen`），用于跳转视频，每行意义:

1. 关键帧号
2. 关键帧偏移，0 帧应为 160 (`0xa0`)

以此循环。


## 音频偏移标记

音频偏移标记为有符号 `int32_t`：

* 取负数为新版本，标记后接视频流，标记取反为音频流偏移。
* 取正数为新版本，标记后接音频流，标记为视频流偏移。
* 取 0 时，不包含音频流。

部分版本可能会在音频偏移标记前附加标记（`DATASTART\0`）。此标记仅为注释，播放器不读取也不识别。


## 视频流

视频流头大小为 `0xa0`。部分偏移意义及解释：

* `0x04` (`uint32_t`)：视频宽
* `0x08` (`uint32_t`)：视频高
* `0x10` (`uint32_t`)：总帧数（FPS 一般为 5，由播放器初始值决定）
* `0x24` (`uint32_t`)：流是否包含指针数据
* `0x28` (`char[20]`)：注册码 1（扰码）
* `0x3c` (`char[20]`)：注册码 2（计算码）
* `0x50` (`char[40]`)：视频作者水印
* `0x78 - 0xa0`：视频作者水印样式信息

帧流紧随其后。

### 帧格式

首帧格式：
* 如果包含指针数据，则先存储指针 left 坐标（`int32_t`）top 坐标（`int32_t`），然后是数据长度（`uint32_t`）及 MS Windows icon resource 数据；
* 数据长度（`uint32_t`）及图像。

随后根据帧标记：

* 取正数为图像流，包含四角坐标（四个 `uint32_t`）、数据长度（`uint32_t`）及图像。
* 取非正数为指针，帧号为 1 - 帧标记，包含 left 坐标（`int32_t`）、top 坐标（`int32_t`）、数据长度（`uint32_t`，可为 0）及指针。
* 遇到 1 - 总帧数 时，流结束。

图像数据以 (`uint64_t`) -1 开头为可变流，后跟视频编码方式（`uint32_t`）、解压后长度（`uint32_t`）、流长（`uint32_t`）及流。

视频编码方式为 3 时，图像内存储解压后长度（`uint32_t`）及 zlib 数据，解压后按视频编码方式 1 处理。

视频编码方式为 1 时，首先进行解密，然后解压行程编码。

### 行程编码解压

连续的两个 (`uint16_t`) 0 后跟行程长度（`uint16_t`）及符号（`uint16_t`）。

```c
// 字符串：U1JIEYASUO1SHIBAI ("U1解压缩1失败")
// lxefileplay::jieyasuobmp()

width += width & 1;

// 估计 bmp 图像大小
unsigned int bufsize = 2 * width * height + 0x1400;
unsigned char buf[bufsize];

uint16_t *in = (void *) src;
uint16_t *out = (void *) buf;
unsigned int in_i = 0, out_i = 0;
// 最后 3 个符号一定不是行程编码
for (; in_i < srclen / 2 - 3 && out_i < dstlen / 2; ) {
  if (in[in_i] == 0 && in[in_i + 1] == 0 && in[in_i + 2] != 0) {
    for (unsigned int i = 0; i < le16toh(in[in_i + 2]); i++) {
      out[out_i] = in[in_i + 3];
      out_i++;
    }
    in_i += 4;
  } else {
    out[out_i] = in[in_i];
    in_i++;
    out_i++;
  }
}
for (; in_i < srclen && out_i < dstlen / 2; in_i++, out_i++) {
  out[out_i] = in[in_i];
}

// 用 bmp 头检查解压长度
return 2 * out_i == le16toh(out[1]);
```

### 注册验证算法

```c
int code1 = -100;
for (int i = 0; s_code1[i] != '\0'; i++) {
  code1 += s_code1[i];
}
code1 /= 1.5432;
code1 += 1234;
code1 *= 3121.1415926;

for (int i = 0; s_code2[i] != '\0'; i++) {
  s_code2[i] += - 20 - 10 * (i % 2) + (i / 3);
}
int code2 = atoi(s_code2);
code2 /= 124;

return code1 == code2;
```

若未注册，则在视频正中显示“屏幕录像专家 未注册”，且不显示作者水印信息。

### 水印解密算法

```c
for (unsigned int i = 0; i < sizeof(infotext); i++) {
  infotext[i] ^= 100 - 4 * i;
}
```

只要把第一个字符改为 `d` 就能去掉水印。
