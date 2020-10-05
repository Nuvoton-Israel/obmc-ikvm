project(
    'obmc-ikvm',
    'c',
    version: '1.0',
    license: 'GPLv2',
    default_options: [
        'cpp_std=c++17',
        'werror=true',
    ],
)

executable(
    'obmc-ikvm',
    [
        'rfbusbhid.c',
        'rfbnpcm750.c',
        'obmc-ikvm.c',
    ],
    dependencies: [
        dependency('libvncserver'),
        dependency('phosphor-logging'),
        dependency('phosphor-dbus-interfaces'),
        dependency('sdbusplus'),
        dependency('threads'),
    ],
    install: true
)

conf_data = configuration_data()

if get_option('keyevent').enabled()
  conf_data.set('KEYBOARD_EVENT', true, description : 'Enabled Keyboard Event')
endif

configure_file(output : 'config.h', configuration : conf_data)

configure_file(
    input: 'start-ipkvm.service',
    output: 'start-ipkvm.service',
    copy: true,
    install_dir:
        dependency('systemd').get_pkgconfig_variable(
            'systemdsystemunitdir')
)
