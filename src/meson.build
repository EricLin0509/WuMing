wuming_sources = [
  'main.c',
  'wuming-application.c',
  'wuming-window.c',
  'update-signature-page.c',
  'updating-page.c',
  'scan-page.c',
  'scanning-page.c',
  'threat-page.c',
  'security-overview-page.c',
]

wuming_deps = [
  dependency('dbus-1'),
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
]

subdir('libs')

# configure the `wuming-unlinkat-helper` path
helper_path = get_option('prefix') / get_option('bindir') / 'wuming-unlinkat-helper'
add_project_arguments(['-DHELPER_PATH="@0@"'.format(helper_path)], language: 'c')

# file-security library
file_security_lib = static_library('file-security',
  ['libs/file-security.c', 'libs/path-operations.c'],
  dependencies: [dependency('glib-2.0'), dependency('gio-2.0')],
)

# wuming-unlinkat-helper
executable('wuming-unlinkat-helper',
  'libs/wuming-unlinkat-helper.c',
  dependencies: [dependency('glib-2.0'), dependency('gio-2.0')],
  link_with: [file_security_lib],
  install: true,
  install_dir: get_option('bindir'),
  build_by_default: true,
)

# wuming
wuming_sources += gnome.compile_resources('wuming-resources',
  'wuming.gresource.xml',
  c_name: 'wuming'
)

executable('wuming', wuming_sources,
  dependencies: wuming_deps,
  link_with: [file_security_lib],
       install: true,
)
