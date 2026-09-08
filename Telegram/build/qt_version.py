import sys, os

def _win32_qt5_version(arch):
    repo_root = os.path.normpath(
        os.path.join(os.path.dirname(os.path.realpath(__file__)), '../..'))
    if arch in ('x64', 'arm'):
        libs_root = os.path.join(os.path.dirname(repo_root), 'Libraries', 'win64')
    else:
        libs_root = os.path.join(os.path.dirname(repo_root), 'Libraries')
    for version in ('5.15.19', '5.15.18'):
        if os.path.isdir(os.path.join(libs_root, 'Qt-' + version)):
            return version
    return '5.15.19'

def resolve(arch):
    if sys.platform == 'darwin':
        os.environ['QT'] = '6.11.2'
    elif sys.platform == 'win32':
        if arch == 'arm' or 'qt6' in sys.argv:
            print('Choosing Qt 6.')
            os.environ['QT'] = '6.11.2'
        else:
            qt_version = _win32_qt5_version(arch)
            print('Choosing Qt 5 (' + qt_version + ').')
            os.environ['QT'] = qt_version
    return True
