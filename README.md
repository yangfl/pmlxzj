# 《屏幕录像专家》视频转换

功能：
* 无损视频转换
* 光标轨迹插帧
* 音频提取
* 解除“编辑锁定”
* 清除/设置播放密码

支持以下设置的 EXE / LXE 视频：
* 图像压缩：无损压缩，中度无损压缩，高度无损压缩
* 图像质量：高，低
* 声音压缩：不压缩，无损压缩，有损压缩(MP3)，有损压缩(TrueSpeech)，有损压缩(AAC)
* EXE 加密：编辑加密，播放加密
* 多节视频

遇问题可至 Issues 留言反馈。

## 视频转换

提取《屏幕录像专家》视频（不含水印、时间），输出 [APNG](https://developer.mozilla.org/en-US/docs/Web/Media/Formats/Image_types#apng_animated_portable_network_graphics) 文件。输出的 `video.apng` 文件可通过 [ffmpeg](https://ffmpeg.org/) 转换成任意格式。

《屏幕录像专家》默认使用 5 FPS 录制，但以 5 FPS 输出视频时，光标移动有明显撕裂感。不指定 `-r` 的情况下，本程序使用平滑算法将光标移动轨迹插帧至 30 FPS，但 30 FPS 可能会增加最终视频大小。可以使用 `-r <fps>` 指定 FPS，或使用 `-r 0` 恢复视频原始 FPS。

```sh
# 显示视频信息
./plzj video.exe

# 提取视频，叠加光标轨迹，插帧至 30 FPS
./plzj video.exe -e

# 插帧至 60 FPS
./plzj video.exe -e -r 60

# 使用视频原始帧率（一般为 5）
./plzj video.exe -e -r 0

# 无损提取原始视频，不含光标
./plzj video.exe -e -x -m

# 提取所有资源，`cursors.txt` 和 `clicks.txt` 分别为光标轨迹/点击事件
./plzj video.exe -a -x -m
```

### ffmpeg 处理方法

使用 ffmpeg 处理输出文件示例（程序提取完成后将自动提示）：

* x264: `ffmpeg [-i audio.wav] -i video.apng -vf fps=30 -c:v libx264 -pix_fmt yuv444p10le -tune stillimage -preset veryslow video.mp4`
* VP9: `ffmpeg [-i audio.wav] -i video.apng -vf fps=30 -c:v libvpx-vp9 -row-mt 1 video.webm`
* AV1: `ffmpeg [-i audio.wav] -i video.apng -vf fps=30 -c:v libsvtav1 -preset 5 -g 1800 video.mp4`
* FLAC: `ffmpeg -i audio.wav -c:a flac -compression_level 12 audio.flac`

请将上面 ffmpeg 的 `fps=` 选项替换为程序提示的值（或其因数），否则可能产生顿挫感。

### 大小比较

无插帧时（`-r 0`，FPS = 5）：

* 原始文件: 3.7 MB
* APNG: 4.0 MB
* APNG (RAW `-x` + modified `-m`): 2.9 MB
* x264 (CRF = 23): 3.0 MB
* VP9 (CRF = 32): 4.0 MB
* AV1 (CRF = 35): 1.3 MB

## 编译

本地编译：
```sh
sudo apt install gcc make libpng-dev zlib1g-dev
make
```

交叉编译：
```sh
meson wrap install zlib
meson wrap install libpng
meson setup build-mingw --cross-file meson/i686-w64-mingw32.txt
ninja -C build-mingw
```

## FAQ

**Q: 是无损转换吗？**

**A:** 本程序直接读取 EXE 视频数据，无损转换为 APNG 视频文件，非录屏、截屏转换，无需依赖桌面环境即可运行。

但是，《屏幕录像专家》的无损图像压缩却并非无损。即使在高图像质量设置下，《屏幕录像专家》也会将 24 位真彩色重采样为 16 位 rgb565 格式颜色。低图像质量更会将图片压缩至 64 色，造成严重失真。

**Q: LXE 和 EXE 视频文件有什么区别？**

**A:** LXE 仅仅是将 EXE 的可执行代码部分用 zlib 压缩后存储，以规避杀毒软件查杀，LXE 播放器也只是解压这部分可执行代码后执行。LXE 和 EXE 文件的视频数据区完全相同，本程序可以识别任意一种格式。没有必要将已存储的 LXE 全部转换为 EXE 视频，或反之。

**Q: 是否有必要转换所有存储的 EXE / LXE 视频？**

**A:** 目前似乎没有一款主流播放器能支持 APNG 跳转播放。而且，嵌入光标后就不再是无损转换，因此仍建议储存原始 EXE / LXE 视频文件观看。

**Q: 哪种视频编码比较合适？**

**A:** 追求转码速度可考虑 x264，追求压缩比则可考虑 AV1。

**Q: 为什么选择 10 bit 像素格式（`-pix_fmt yuv444p10le`）？**

**A:** 事实上对于 x264 / x265，在相同质量参数下，即使是 8 bit 内容，10 bit 编码压缩率也比 8 bit 好，参见 https://www.reddit.com/r/handbrake/comments/13r4apr/ 。

但 VP9 / AV1 则不存在这种现象。

**Q: 中文密码？**

**A:** 本程序会自动在终端编码（UTF-8）和程序编码（GBK）之间转换。

**Q: 是否应调整压缩级别（`-c, --compression`）？**

**A:** 没有必要。压缩是多线程进行的，对转换速度影响非常有限。

**Q: 如何破解播放密码？**

**A:** 可参考 https://www.52pojie.cn/thread-583714-1-1.html 。

理论上，通过仔细分析图像流可以有更好的破解方法，限于时间关系未深入研究，如有需要可进一步联系。

**Q: 天狼星加密和播放加密有什么关系？**

**A:** 没有关系。网络上有部分资料将“播放加密”讹传为天狼星加密系统，其实并不是同一个技术。据官网介绍，天狼星加密为定制系统，一机一码，防软件、硬件翻录；而《屏幕录像专家》的播放加密为固定密码，很容易暴力破解，也不存在任何防软件、硬件翻录机制。网络上也找不到可以运行的天狼星加密样本文件研究。

## 致谢

基于 爱飞的猫@52pojie.cn 工作，特别参考了帧解析格式。

> https://github.com/FlyingRainyCats/pmlxzj_unlocker

> https://www.52pojie.cn/thread-1952469-1-1.html
