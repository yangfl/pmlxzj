project(
  'plzj', 'c', version: '0.0.0',
  meson_version: '>= 0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=everything',
  ],
)

src = [
  'lib/platform/c11threads_win32.c',
  'lib/platform/nowide.c',
  'lib/platform/nproc.c',
  'lib/alg.c',
  'lib/audio.c',
  'lib/err.c',
  'lib/image.c',
  'lib/iter.c',
  'lib/log.c',
  'lib/parser.c',
  'lib/threadname.c',
  'lib/threadpool.c',
  'lib/txts.c',
  'lib/utils.c',
  'lib/video.c',
  'src/debug.c',
  'src/plzj.c',
]

fs = import('fs')
cc = meson.get_compiler('c')
in_debug = get_option('debug')
on_windows = host_machine.system() in ['windows', 'cygwin']

cpp_args = []
plzj_cpp_args = []

# diagnostic flags
if meson.is_subproject()
  # suppress all compiler warnings when we're a subproject
  if cc.get_id() == 'msvc'
    add_project_arguments('/w', language: ['c', 'cpp'])
  else
    add_project_arguments('-w', language: ['c', 'cpp'])
  endif
else
  # enable compiler warnings
  if cc.get_id() != 'msvc'
    if get_option('warning_level') in ['3', 'everything']
      add_project_arguments(
        '-Werror=format-security',
        '-Wno-cast-function-type', '-Wno-cast-qual', '-Wno-cast-align',
        '-Wno-missing-field-initializers', '-Wno-packed', '-Wno-padded',
        '-Wno-conversion', '-Wno-unsuffixed-float-constants',
        '-Wno-vla', '-Wno-stack-protector',
        '-Wno-double-promotion',
        '-Wno-unused-macros',
        '-Wno-c++-compat',
        '-Wno-switch-default',
        '-Wno-format-nonliteral',
        '-Wno-inline',
        language: ['c', 'cpp'])
    endif
    if in_debug
      add_project_arguments(
        '-fanalyzer',
        '-Wno-analyzer-tainted-allocation-size', '-Wno-analyzer-too-complex',
        language: ['c', 'cpp'])
      if not on_windows
        add_project_link_arguments('-rdynamic', language: ['c', 'cpp'])
      endif
    endif
  endif
endif

# hardening
if cc.get_id() != 'msvc'
  cpp_args += '-D_FORTIFY_SOURCE=2'
  add_project_arguments(
    '-fstack-protector-strong', '-fstack-clash-protection',
    language: ['c', 'cpp'])
  if host_machine.cpu_family() in ['x86', 'x86_64']
    add_project_arguments('-fcf-protection', language: ['c', 'cpp'])
  endif
  add_project_link_arguments('-Wl,--gc-sections', language: ['c', 'cpp'])
  if not on_windows
    add_project_link_arguments(
      '-Wl,-z,relro', '-Wl,-z,now',
      language: ['c', 'cpp'])
  endif
endif

# macro definitions
cpp_args += '-D_DEFAULT_SOURCE'

if in_debug
  cpp_args += '-D_DEBUG'
endif

has_include_supported = cc.compiles('''
#ifndef __has_include
#error "No __has_include"
#endif
''', name: '__has_include')
if not has_include_supported
  if cc.has_header('byteswap.h')
    cpp_args += '-DHAVE_BYTESWAP_H'
  endif
  if cc.has_header('endian.h')
    cpp_args += '-DHAVE_ENDIAN_H'
  endif
  if cc.has_header('execinfo.h')
    cpp_args += '-DHAVE_EXECINFO_H'
  endif
  if cc.has_header('pthread_np.h')
    cpp_args += '-DHAVE_PTHREAD_NP_H'
  endif
  if cc.has_header('stdbit.h')
    cpp_args += '-DHAVE_STDBIT_H'
  endif
  if cc.has_header('sys/endian.h')
    cpp_args += '-DHAVE_SYS_ENDIAN_H'
  endif
  if cc.has_header('threads.h')
    cpp_args += '-DHAVE_THREADS_H'
  endif
endif

if cc.has_function('copy_file_range', args: ['-D_GNU_SOURCE'])
  cpp_args += ['-D_FILE_OFFSET_BITS=64', '-DHAVE_COPY_FILE_RANGE']
endif

if on_windows
  cpp_args += ['-DUNICODE', '-D_UNICODE']
  cpp_args += '-DSC_LOG_NO_COLOR'
endif

plzj_cpp_args += '-DPLZJ_BUILDING_STATIC'

# dependencies
m_dep = cc.find_library('m', required: false)
thread_dep = []
iconv_dep = []
zlib_dep = dependency('zlib', required: false, allow_fallback: false)
if not zlib_dep.found()
  zlib_dep = dependency('zlib', static: true)
endif
libpng_dep = dependency('libpng', required: false, allow_fallback: false)
if not libpng_dep.found()
  libpng_dep = dependency('libpng', static: true)
endif

if not on_windows
  thread_dep = dependency('threads', required: false)
  if not thread_dep.found()
    warning('threads not found, force single thread.')
    cpp_args += '-DNO_THREADS'
  else
    if cc.has_function(
        'pthread_setname_np',
        args: ['-D_GNU_SOURCE'],
        dependencies: thread_dep)
      cpp_args += '-DHAVE_PTHREAD_NAME'
    endif
  endif

  iconv_dep = dependency('iconv', required: false)
  if not iconv_dep.found()
    warning('iconv not found, program will only work on GBK encoding.')
  else
    if not has_include_supported
      cpp_args += '-DHAVE_ICONV_H'
    endif
  endif
endif

plzj_deps = [m_dep, thread_dep, iconv_dep, zlib_dep, libpng_dep]

# executable
add_project_arguments(cpp_args, language: ['c', 'cpp'])

if on_windows
  rc_args = [
    '-DPROJECT_NAME=\'"' + meson.project_name() + '"\'',
    '-DPROJECT_VERSION=\'"' + meson.project_version() + '"\'',
  ]
  rc_depend_files = []
  if fs.is_file('resources/plzj.ico')
    rc_depend_files += 'resources/plzj.ico'
  endif

  windows = import('windows')
  src += windows.compile_resources(
    'resources/plzj.rc', args: cpp_args + plzj_cpp_args + rc_args,
    depend_files: rc_depend_files)
endif

executable(
  'plzj', src,
  c_args: plzj_cpp_args,
  dependencies: plzj_deps,
  pie: true,
  gnu_symbol_visibility: 'hidden',
)
