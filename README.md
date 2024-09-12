# 《屏幕录像专家》视频转换

功能：
* 无损视频转换
* 光标轨迹插帧
* 解除“编辑锁定”
* 去密码/设密码

遇问题可至 Issues 留言反馈。

## 视频转换

提取《屏幕录像专家》视频（不含水印、时间），输出 [APNG](https://developer.mozilla.org/en-US/docs/Web/Media/Formats/Image_types#apng_animated_portable_network_graphics) 文件。输出的 `video.apng` 文件可通过 [ffmpeg](https://ffmpeg.org/) 转换成任意格式。

《屏幕录像专家》默认使用 5 FPS 录制，但以 5 FPS 输出视频时，光标移动有明显撕裂感。不指定 `-r` 的情况下，转换程序使用平滑算法将光标移动轨迹插帧至 30 FPS，但 30 FPS 可能会增加最终视频大小。可以使用 `-r <fps>` 指定 FPS，或使用 `-r 0` 恢复视频原始 FPS。

```sh
# 显示视频信息
./pmlxzj video.exe

# 提取视频，叠加光标轨迹，默认插帧至 30 FPS
./pmlxzj video.exe -e

# 提取视频，叠加光标轨迹，使用视频原始帧率（一般为 5）
./pmlxzj video.exe -e -r 0

# 无损提取原始视频，不含光标
./pmlxzj video.exe -e -x -m

# 提取所有资源，`cursors.txt` 和 `clicks.txt` 分别为光标轨迹/点击事件
./pmlxzj video.exe -a -x -m
```

### ffmpeg 处理方法

使用 ffmpeg 处理输出文件示例（程序提取完成后将自动提示）：

* x264: `ffmpeg [-i audio.wav] -i video.apng -r 30 -c:v libx264 -rix_fmt yuv444p10le -tune stillimage -rreset veryslow video.mp4`
* VP9: `ffmpeg [-i audio.wav] -i video.apng -r 30 -c:v libvpx-vp9 -row-mt 1 video.webm`
* AV1: `ffmpeg [-i audio.wav] -i video.apng -r 30 -c:v libsvtav1 -preset 5 -g 1800 video.mp4`
* FLAC: `ffmpeg -i audio.wav -c:a flac -compression_level 12 audio.flac`

请将上面 ffmpeg 的 `-r` 选项替换为程序提示的值（或均分值），否则可能产生严重撕裂感。

### 大小比较

无插帧时（`-r 0`，FPS = 5）：

* 原始文件: 3.7 MB
* APNG: 4.0 MB
* APNG (RAW `-x` + modified `-m`): 2.9 MB
* x264 (CRF = 23): 3.0 MB
* VP9 (CRF = 32): 4.0 MB
* AV1 (CRF = 35): 1.3 MB

## 编译

```sh
sudo apt install gcc make libpng-dev zlib1g-dev
make
```

## 后记

屏幕录像专家存储的是 rgb565 图片，已经压缩过一次了，自己就不是无损的……

x264 CRF 23 几乎没有压缩残影，可以作为发布格式使用。

## 致谢

基于 爱飞的猫@52pojie.cn 工作，特别参考了帧解析格式。

> https://github.com/FlyingRainyCats/pmlxzj_unlocker

> https://www.52pojie.cn/thread-1952469-1-1.html
