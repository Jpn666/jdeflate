project('jdeflate', 'c', default_options: ['c_std=c99', 'warning_level=3'], version: run_command('tools/version.py', check: true).stdout().strip(), meson_version: '>=1.3.0')


subdir('jdeflate/config')


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


projectsources = []

if target_machine.cpu_family() == 'x86_64'
  supported = false

  iswindows = false
  isunix = false
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
  'src/version.c',
  'src/inflator.c',
  'src/deflator.c',
  'src/zstrm.c',
]

headerfiles = [
  'jdeflate/inflator.h',
  'jdeflate/deflator.h',
  'jdeflate/zstrm.h',
]


lib = both_libraries(meson.project_name(), install: true, pic: true, sources: projectsources, dependencies: deps, soversion: versionarray[0], c_args: ['-DJDEFLATE_BUILDDLL', '-DJDEFLATE_DLL'])

extraargs = []
if target_machine.system() in ['windows', 'cygwin']
  extraargs += ['-DJDEFLATE_DLL']
endif

common = {'include_directories': include_directories('.'), 'dependencies': dep, 'compile_args': extraargs}
if meson.is_subproject()
  if meson.get_option('default_library') == 'static'
    jdeflate_dep = declare_dependency(link_with: lib.get_static_lib(), kwargs: common)
  endif
  if meson.get_option('default_library') == 'shared'
    jdeflate_dep = declare_dependency(link_with: lib.get_shared_lib(), kwargs: common)
  endif
endif

if not is_variable('jdeflate_dep')
  jdeflate_dep = declare_dependency(link_with: lib, kwargs: common)
endif

pkg = import('pkgconfig')
pkg.generate(libraries: jdeflate_dep, version: meson.project_version(), name: meson.project_name(), filebase: meson.project_name(), description: 'Just another deflate compression library')

install_headers(headerfiles, preserve_path: true)
