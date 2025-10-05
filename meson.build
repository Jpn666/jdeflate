project('jdeflate', 'c', default_options: ['c_std=c99', 'warning_level=3'], version: run_command('tools/version.py', check: true).stdout().strip(), meson_version: '>=1.1.0')


deps = []
deps += [dependency('ctoolbox', fallback: ['ctoolbox', 'ctoolbox_dep'])]


if get_option('buildtype').startswith('release')
  add_project_arguments('-DNDEBUG', language : 'c')
endif

cc = meson.get_compiler('c')
if cc.get_id() == 'msvc'
  add_project_arguments('-D__MSVC__', language: 'c')
else
  if cc.get_id() == 'gcc' or cc.get_id() == 'clang'
    compilerflags = [
      '-Wconversion',
      '-Wcast-qual',
      '-Wunused-parameter',
      '-Wshadow',
      '-Wswitch',
      '-Wcast-align',
      '-Wdouble-promotion'
    ]
    add_project_arguments(compilerflags, language: 'c')
  endif
endif


# major, minor and path
versionarray = [0, 0, 0]

mmp = meson.project_version().split('.')
if mmp.length() == 3
  versionarray = [
    mmp[0].to_int(),
    mmp[1].to_int(),
    mmp[2].to_int()
  ]
endif


# Configuration
conf = configuration_data()

# build the version string
v0 = versionarray[0]
v1 = versionarray[1]
v2 = versionarray[2]
version = f'@v0@.@v1@.@v2@'


conf.set('version', version)

conf.set('version_major', v0)
conf.set('version_minor', v1)
conf.set('version_rpath', v2)


projectsources = []

if target_machine.cpu_family() == 'x86_64'
  supported = false

  iswindows = false
  isunix   = false
  if target_machine.system() in ['windows', 'cygwin']
    iswindows = true
  else
    if target_machine.system() in ['android', 'darwin', 'dragonfly', 'freebsd', 'gnu', 'linux', 'netbsd', 'openbsd', 'sunos']
      isunix = true
    endif
  endif

  if isunix
    supported = true
  endif
  if iswindows
    supported = true
  endif

  path = 'src/asm'
  if supported
    if add_languages('nasm', required: false, native: false)
      if isunix
        if host_machine.system() in ['darwin', 'ios', 'tvos']
          nasmformat = 'macho64'
        else
          nasmformat = 'elf64'
        endif
      endif
      if iswindows
        nasmformat = 'win64'
      endif

      add_project_arguments('-f', nasmformat, language: 'nasm')
      projectsources += [path / 'x86-64-adler32.asm']
      projectsources += [path /   'x86-64-crc32.asm']

      add_project_arguments(['-DZSTRM_ADLER32EXTERNALASM', '-DZSTRM_CRC32EXTERNALASM'], language: 'c')
    else
      warning('NASM not found, using C fallback code')
    endif
  endif
endif


projectsources += [
  'src/inflator.c',
  'src/deflator.c',
  'src/zstrm.c',
]

headerfiles = [
  'jdeflate/inflator.h',
  'jdeflate/deflator.h',
  'jdeflate/zstrm.h',
]

configfile = configure_file(input: 'jdeflateconfig.h.in', output: 'jdeflateconfig.h', configuration: conf)
headerfiles += [configfile]


static_build = static_library(meson.project_name(), sources: projectsources, pic: true, install: true, dependencies: deps)
lib = declare_dependency(
  link_with: static_build, include_directories: include_directories('.'), dependencies: deps
)
jdeflate_dep = lib


pkg = import('pkgconfig')
pkg.generate(libraries: [static_build], version: meson.project_version(), name: meson.project_name(), filebase: meson.project_name(), description: 'Just another deflate compression library')

install_headers(headerfiles, preserve_path: true)
