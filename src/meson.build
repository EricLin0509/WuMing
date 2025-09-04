wuming_sources = [
  'main.c',
  'wuming-application.c',
  'wuming-window.c',
  'update-signature-page.c',
  'scan-page.c',
]

wuming_deps = [
  dependency('libclamav'),
  dependency('dbus-1'),
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
]

subdir('libs')

# configure the `wuming-unlinkat-helper` path
helper_path = get_option('prefix') / get_option('bindir') / 'wuming-unlinkat-helper'
add_project_arguments(['-DHELPER_PATH="@0@"'.format(helper_path)], language: 'c')

# wuming-unlinkat-helper
wuming_unlink_helper_sources = [
  'libs/file-security.c',
  'libs/wuming-unlinkat-helper.c',
]

executable('wuming-unlinkat-helper', wuming_unlink_helper_sources,
  dependencies: [dependency('glib-2.0'), dependency('gio-2.0')],
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
       install: true,
)
