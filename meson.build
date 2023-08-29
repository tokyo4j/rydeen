project(
    'rydeen',
    'c',
    default_options: [
        'c_std=gnu17',
        'warning_level=3',
    ],
)

add_global_arguments(['-Wno-unused-parameter'], language: 'c')

if get_option('buildtype') == 'debug'
    add_global_arguments(['-DDEBUG'], language: 'c')
endif

dependencies = [
    dependency('libinput'),
    dependency('xkbcommon'),
    dependency('libevdev'),
    dependency('libudev'),
    dependency('yaml-0.1'),
    meson.get_compiler('c').find_library('ev', has_headers: ['ev.h']),
]

subdir('src')

executable(
    meson.project_name(),
    rydeen_sources,
    dependencies: dependencies,
    install: true,
    install_dir: '/usr/bin',
)

install_data('config.yml', install_dir: '/etc/rydeen')
install_data('rydeen.service', install_dir: '/usr/lib/systemd/system')
