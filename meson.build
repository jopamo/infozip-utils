project(
  'unzip',
  'c',
  version: '6.0-custom',
  default_options: ['c_std=c99', 'warning_level=2', 'optimization=2']
)

inc = include_directories('src')
bz2_dep = dependency('bzip2', required: true)

cdefs = [
  '-DUNIX',
  '-DNO_ZIPINFO',
  '-DNO_ZIPGREP',
  '-DUSE_BZIP2',
  '-D_XOPEN_SOURCE=700',
  '-D_DEFAULT_SOURCE',
  '-DNO_LCHOWN',
  '-DNO_LCHMOD',
  '-include', 'utime.h'  # ensure utime prototype is visible
]

sources = [
  'src/unzip.c',
  'src/extract.c',
  'src/process.c',
  'src/fileio.c',
  'src/list.c',
  'src/match.c',
  'src/ttyio.c',
  'src/inflate.c',
  'src/explode.c',
  'src/unreduce.c',
  'src/unshrink.c',
  'src/crypt.c',
  'src/crc32.c',
  'src/envargs.c',
  'src/globals.c',
  'src/timezone.c',
  'src/unix.c'
]

executable(
  'unzip',
  sources,
  include_directories: inc,
  dependencies: [bz2_dep],
  c_args: cdefs,
  install: true
)

install_data('man/unzip.1', install_dir: get_option('mandir') / 'man1')
