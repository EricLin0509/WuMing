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

wuming_sources += gnome.compile_resources('wuming-resources',
  'wuming.gresource.xml',
  c_name: 'wuming'
)

executable('wuming', wuming_sources,
  dependencies: wuming_deps,
       install: true,
)
