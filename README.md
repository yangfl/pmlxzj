# 《屏幕录像专家》视频转换

功能：
* 无损视频转换
* 鼠标轨迹插帧
* 解除“编辑锁定”
* 去密码/设密码

遇问题可至 Issues 留言反馈。

## 视频转换

提取《屏幕录像专家》视频（不含水印、时间），输出 [APNG](https://developer.mozilla.org/en-US/docs/Web/Media/Formats/Image_types#apng_animated_portable_network_graphics) 文件。输出的 `video.apng` 文件可通过 [ffmpeg](https://ffmpeg.org/) 转换成任意格式。

```sh
# 显示视频信息
./pmlxzj video.exe

# 提取视频，叠加鼠标轨迹，插帧至 FPS 30
./pmlxzj video.exe -e

# 无损提取原始视频，不含光标
./pmlxzj video.exe -e -r -m

# 提取所有资源，`cursors.txt` 和 `clicks.txt` 分别为光标轨迹/点击事件
./pmlxzj video.exe -a -r -m
```

### ffmpeg 处理方法

使用 ffmpeg 处理输出文件示例（程序提取完成后将自动提示）：

* x264: `ffmpeg [-i audio.wav] -i video.apng -r 5 -c:v libx264 -pix_fmt yuv444p10le -tune stillimage -preset veryslow video.mp4`
* VP9: `ffmpeg [-i audio.wav] -i video.apng -r 5 -c:v libvpx-vp9 -pix_fmt yuv444p12le -row-mt 1 video.webm`
* FLAC: `ffmpeg -i audio.wav -c:a flac -compression_level 12 audio.flac`

### 大小比较

无插帧下（`-p 0`）：

* 原始文件: 3.7 MB
* APNG: 4.0 MB
* APNG（RAW + modified）: 2.9 MB
* x264 (CRF = 23): 3.0 MB
* VP9 (CRF = 32): 4.0 MB
* AV1 (CRF = 32): 1.1 MB

## 编译

```sh
sudo apt install gcc make libpng-dev zlib1g-dev
make
```

## 后记

屏幕录像专家存储的是 rgb565 图片，已经压缩过一次了，自己就不是无损的……

x264 CRF 23 几乎没有压缩残影，可以作为发布格式使用。

## 待开发功能

重写播放器？

## 致谢

基于 爱飞的猫@52pojie.cn 工作，特别参考了帧解析格式。

> https://github.com/FlyingRainyCats/pmlxzj_unlocker

> https://www.52pojie.cn/thread-1952469-1-1.html
