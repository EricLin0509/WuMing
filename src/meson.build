wuming_sources = [
  'main.c',
  'wuming-application.c',
  'wuming-window.c',
  'ring-buffer.c',
  'systemd-control.c',
  'update-signature.c',
  'update-signature-page.c',
  'scan-page.c',
]

wuming_deps = [
  dependency('libclamav'),
  dependency('dbus-1'),
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
]

wuming_sources += gnome.compile_resources('wuming-resources',
  'wuming.gresource.xml',
  c_name: 'wuming'
)

executable('wuming', wuming_sources,
  dependencies: wuming_deps,
       install: true,
)
