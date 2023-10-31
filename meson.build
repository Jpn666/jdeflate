project('deflate', 'c', default_options: ['c_std=c99', 'buildtype=release', 'warning_level=3'], version: run_command('python', join_paths(meson.current_source_dir(), 'tools/version.py'), check: true).stdout().strip())


deps = []
deps += [dependency('', fallback: ['ctoolbox', 'lib'])]


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


projectsources = [
  'src/inflator.c', 'src/deflator.c', 'src/zstrm.c'
]


static_build = static_library('deflate', sources: projectsources, pic: true, install: true,  dependencies: deps)

lib = declare_dependency(
  link_with: static_build, include_directories: include_directories('.'), dependencies: deps
)
