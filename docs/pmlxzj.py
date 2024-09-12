#!/usr/bin/env python3

import datetime
import sys
from typing import BinaryIO, Iterable, SupportsBytes, SupportsIndex, cast, TYPE_CHECKING
import zlib

if TYPE_CHECKING:
    from _typeshed import ReadableBuffer

type BytesLike = 'Iterable[SupportsIndex] | SupportsBytes | ReadableBuffer'


FPS = 5
ZLIB_MAGIC = b'\x78\x9c'
BMP_MAGIC = b'BM'


def uint32(buf: BytesLike, offset: int = 0, signed: bool = False):
    return int.from_bytes(buf[offset:offset + 4], 'little', signed=signed)


def uint16(buf: BytesLike, offset: int = 0, signed: bool = False):
    return int.from_bytes(buf[offset:offset + 2], 'little', signed=signed)


def frame(exe: BinaryIO, aux: int, has_mouse: bool = False, debug: bool = False):
    frame_offset = exe.tell()
    frame_stream_len = uint32(exe.read(0x4))
    frame_stream_uncompressed_len = uint32(exe.read(0x4))
    if debug:
        print(f'Frame offset: 0x{frame_offset:08x}  Stream length: 0x{frame_stream_len:x} ({
              frame_stream_len})  Uncompressed: {frame_stream_uncompressed_len}')
    frame_stream = exe.read(frame_stream_len - 0x4)
    assert frame_stream.startswith(ZLIB_MAGIC)
    frame_id = exe.read(0x4)
    if has_mouse:
        frame_unk_coord = exe.read(0x8)
        frame_unk_stream = exe.read(uint32(exe.read(0x4)))
    print(frame_info.hex(' '))
    print(frame_unk_coord.hex(' '))


def u1jieyasuo1(buf: BytesLike, width: int, height: int, *, bufsize: int = 0):
    # U1JIEYASUO1SHIBAI ("U1解压缩1失败")
    size = 4 * height * ((width+1) >> 1) + 0x1400
    assert buf[:2] == BMP_MAGIC
    ret = bytearray(size)
    ret[:2] = BMP_MAGIC

    if bufsize <= 0:
        bufsize = len(buf)

    uncompressed_size_half = bufsize // 2
    uncompressed_size_half3 = uncompressed_size_half - 3

    uncompress_buf_ptr = 1

    i = 1
    while i < uncompressed_size_half3:
        if uint16(buf, 2 * i) or uint16(buf, 2 * (i + 1)) or not uint16(buf, 2 * (i + 2)):
            ret[2 * uncompress_buf_ptr:2 *
                (uncompress_buf_ptr + 1)] = buf[2*i:2*(i+1)]
            uncompress_buf_ptr += 1
        else:
            v35 = 0
            while v35 < uint16(buf, 2 * (i + 2)):
                ret[2*uncompress_buf_ptr:2 *
                    (uncompress_buf_ptr+1)] = buf[2*(i+3):2*(i+4)]
                uncompress_buf_ptr += 1
                v35 += 1
            i += 3
        i += 1

    assert uncompressed_size_half == i
    ret[2*uncompress_buf_ptr:2 *
        (uncompress_buf_ptr+uncompressed_size_half - i)] = buf[2*i:2*uncompressed_size_half]
    uncompress_buf_ptr += uncompressed_size_half - i

    assert uncompress_buf_ptr <= size // 2
    assert uint32(ret, 2) == uncompress_buf_ptr * 2
    return ret[:uint32(ret, 2)]


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description='提取 屏幕录像专家 视频')
    parser.add_argument(
        '-d', '--debug', action='store_true',
        help='debug')
    parser.add_argument(
        '-f', '--force', action='store_true',
        help='force action, skip all internal checks')
    parser.add_argument(
        'exe', metavar='<video.exe>', type=argparse.FileType('rb'),
        help='video.exe to use')
    # parser.add_argument(
    #    'output', metavar='<output file>', help='output file')

    args = parser.parse_args()
    exe = cast(BinaryIO, args.exe)

    # info
    exe.seek(-0xe0, 2)
    player = exe.read(0xb4)
    title = player[0x38:].rstrip(b'\x00').decode('gbk')

    footer = exe.read(0x2c)
    header_offset = uint32(footer, 0x1c)

    if footer[0x20:0x29] != b'pmlxzjtlx':
        print('ERROR: not a 屏幕录像专家 exe file', file=sys.stderr)
        return 1

    # header
    exe.seek(header_offset)
    if args.debug:
        print(f'Header offset: 0x{header_offset:08x}')
        print()
    audio_variant = exe.read(0x4)
    video = exe.read(0xa0)
    width = uint32(video, 0x4)
    height = uint32(video, 0x8)
    frames_cnt = uint32(video, 0xc)
    has_cursor = uint32(video, 0x24)

    elapsed_seconds = frames_cnt / FPS
    print('File Info:')
    print(f'  Title: {title}')
    print(f'  Resolution: {width}x{height}')
    print(f'  Elapsed: {str(datetime.timedelta(seconds=elapsed_seconds)).rstrip(
        '0')}, {elapsed_seconds} s, {frames_cnt} frames')

    if has_cursor:
        frame_unk_coord = exe.read(0x8)
        frame_unk_stream_len = uint32(exe.read(0x4))
        print(exe.tell(), 'frame_unk_stream_len', frame_unk_stream_len)
        frame_unk_stream = exe.read(frame_unk_stream_len)

    # frames
    frame_offset = exe.tell()
    frame_stream_len = uint32(exe.read(0x4))
    frame_stream_uncompressed_len = uint32(exe.read(0x4))
    print(f'Frame offset: 0x{frame_offset:08x}  Stream length: 0x{frame_stream_len:x} ({
          frame_stream_len})  uncompressed: {frame_stream_uncompressed_len}')
    frame_stream = exe.read(frame_stream_len - 0x4)
    assert frame_stream.startswith(ZLIB_MAGIC)

    uncompressed_date = zlib.uncompress(frame_stream)
    assert frame_stream_uncompressed_len == len(uncompressed_date)
    # with open('frame.fbmp', 'wb') as f:
    #    f.write(uncompressed_date)
    # with open('frame.bmp', 'wb') as f:
    #    f.write(u1jieyasuo1(uncompressed_date, width, height))

    last_frame_id = -(frames_cnt - 1)
    while (frame_id := uint32(exe.read(0x4), 0, True)) != last_frame_id:
        if frame_id <= 0:
            left = uint32(exe.read(0x4))
            top = uint32(exe.read(0x4))
            frame_unk_stream_len = uint32(exe.read(0x4))
            print(f'{1 - frame_id} Cursor  Offset: 0x{frame_offset:08x}',
                  frame_unk_stream_len, frame_unk_coord.hex(' '))
            frame_unk_stream = exe.read(frame_unk_stream_len)
        else:
            frame_offset = exe.tell()
            left = uint32(exe.read(0x4))
            top = uint32(exe.read(0x4))
            right = uint32(exe.read(0x4))
            bottom = uint32(exe.read(0x4))
            # print(exe.tell(),'frame_rect',left,top,right,bottom)
            frame_stream_len = uint32(exe.read(0x4))
            assert frame_stream_len
            frame_stream_uncompressed_len = uint32(exe.read(0x4))
            frame_stream = exe.read(frame_stream_len - 0x4)
            assert frame_stream.startswith(ZLIB_MAGIC)
            print(f'{frame_id} Image   Offset: 0x{frame_offset:08x}  Stream length: 0x{
                  frame_stream_len:x} ({frame_stream_len})  uncompressed: {frame_stream_uncompressed_len}')

            uncompressed_date = zlib.uncompress(frame_stream)
            assert frame_stream_uncompressed_len == len(uncompressed_date)
            # u1jieyasuo1(uncompressed_date, right-left, bottom-top)


if __name__ == '__main__':
    try:
        exit(main())
    except KeyboardInterrupt:
        print('\nERROR: Keyboard interrupt', file=sys.stderr)
        exit(255)
