project(
  'zip-unzip',
  'c',
  version: '6.0-custom',
  default_options: [
    'c_std=c99',
    'warning_level=2',
    'optimization=2'
  ]
)

bz2_dep = dependency('bzip2', required: true)

# common flags for both targets
common_defs = [
  '-DUNIX',
  '-DUSE_BZIP2',
  '-D_XOPEN_SOURCE=700',
  '-D_DEFAULT_SOURCE'
]

# unzip flags
unzip_defs = common_defs + [
  '-DUNZIP',
  '-DCRYPT',
  '-DUNICODE_SUPPORT',
  '-DNO_ZIPINFO',
  '-DNO_ZIPGREP',
  '-DNO_LCHOWN',
  '-DNO_LCHMOD',
  '-DDYNALLOC_CRCTAB',
  '-include', 'utime.h'
]

# zip flags
# do not pass -DZIP if zip/zip.h already defines it
zip_defs = common_defs + [
  '-DCRYPT',
  '-DUNICODE_SUPPORT'
]

inc_unzip = include_directories('unzip')
inc_zip   = include_directories('zip')
inc_common = include_directories('common')

unzip_sources = files(
  'unzip/unzip.c',
  'unzip/extract.c',
  'unzip/process.c',
  'unzip/fileio.c',
  'unzip/list.c',
  'unzip/match.c',
  'unzip/inflate.c',
  'unzip/explode.c',
  'unzip/unreduce.c',
  'unzip/unshrink.c',
  'unzip/crypt.c',
  'common/crc32.c',
  'unzip/envargs.c',
  'unzip/globals.c',
  'unzip/timezone.c',
  'unzip/unix.c',
  'unzip/ubz2err.c',
  'common/ttyio.c'
)

zip_sources = files(
  'zip/zip.c',
  'zip/zipfile.c',
  'zip/zipup.c',
  'zip/fileio.c',
  'zip/util.c',
  'zip/globals.c',
  'zip/crypt.c',
  'common/crc32.c',
  'zip/zbz2err.c',
  'zip/unix.c',
  'zip/deflate.c',
  'zip/trees.c',
  'common/ttyio.c'
)

unzip_exe = executable(
  'unzip',
  unzip_sources,
  include_directories: [inc_unzip, inc_common],
  dependencies: [bz2_dep],
  c_args: unzip_defs,
  link_args: ['-Wl,--as-needed', '-Wl,--no-undefined'],
  install: true
)

zip_exe = executable(
  'zip',
  zip_sources,
  include_directories: [inc_zip, inc_common],
  dependencies: [bz2_dep],
  c_args: zip_defs,
  link_args: ['-Wl,--as-needed', '-Wl,--no-undefined'],
  install: true
)

install_data('man/unzip.1', install_dir: get_option('mandir') / 'man1')
install_data('man/zip.1',   install_dir: get_option('mandir') / 'man1')
