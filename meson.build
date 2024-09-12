project(
  'plzj', 'c',
  default_options: [
    'c_std=c11',
    'warning_level=everything',
  ],
)

src = [
  'lib/platform/c11threads_win32.c',
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

cc = meson.get_compiler('c')

# Suppress all compiler warnings when we're a subproject
if meson.is_subproject()
  if cc.get_id() == 'msvc'
    add_project_arguments('/w', language: ['c', 'cpp'])
  else
    add_project_arguments('-w', language: ['c', 'cpp'])
  endif
endif

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
      language: ['c', 'cpp'])
  endif
  if get_option('debug')
    add_project_arguments(
      '-fanalyzer',
      '-Wno-analyzer-tainted-allocation-size', '-Wno-analyzer-too-complex',
      language: ['c', 'cpp'])
  endif
  add_project_arguments('-fstack-protector-strong', language: ['c', 'cpp'])
  add_project_link_arguments(
    '-Wl,--gc-sections', '-Wl,-z,relro', language: ['c', 'cpp'])
endif

add_project_arguments('-D_DEFAULT_SOURCE', language: ['c', 'cpp'])

m_dep = cc.find_library('m', required: false)
iconv_dep = dependency('iconv', required: false)
libpng_dep = dependency('libpng')
thread_dep = dependency('threads', required: false)
zlib_dep = dependency('zlib')

if not iconv_dep.found()
  warning('iconv not found, program will only work on GBK encoding.')
endif

if not thread_dep.found()
  warning('threads not found, force single thread.')
else
  add_project_arguments('-DHAVE_THREADS', language: ['c', 'cpp'])

  if cc.has_function(
      'pthread_setname_np',
      args: ['-D_GNU_SOURCE'],
      dependencies: thread_dep,
  )
    add_project_arguments('-DHAVE_PTHREAD_NAME', language: ['c', 'cpp'])
  endif
endif


plzj_cpp_args = []

if host_machine.system() in ['windows', 'cygwin']
  plzj_cpp_args += '-DPLZJ_BUILDING_DLL'
endif

executable(
  'plzj', src,
  dependencies: [m_dep, iconv_dep, libpng_dep, thread_dep, zlib_dep],
  c_args: plzj_cpp_args,
  pie: true,
  gnu_symbol_visibility: 'hidden',
)
