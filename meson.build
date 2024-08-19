project('FFT3DFilter', 'cpp',
  default_options : ['buildtype=release', 'b_ndebug=if-release', 'cpp_std=c++17'],
  license : 'GPL2',
  meson_version : '>=0.49.0',
  version : '2'
)

cc = meson.get_compiler('cpp')

sources = [
  'FFT3DFilter.cpp',
  'FFT3DFilter.h',
  'fft3dfilter_c.cpp',
  'FFT3DFilterTransform.cpp',
  'Plugin.cpp'
]

if build_machine.system() == 'windows'
  fftw3f_dep = dependency('fftwf')
  deps = [fftw3f_dep]
  install_dir = 'installed'  # dummy
else
  vapoursynth_dep = dependency('vapoursynth')
  fftw3f_dep = dependency('fftw3f')
  deps = [vapoursynth_dep, fftw3f_dep]
  install_dir = join_paths(vapoursynth_dep.get_pkgconfig_variable('libdir'), 'vapoursynth')
  if host_machine.cpu_family().startswith('x86')
    add_project_arguments('-mfpmath=sse', '-msse2', language : 'cpp')
  elif host_machine.cpu_family().startswith('arm') or host_machine.cpu_family() == 'aarch64'
    add_project_arguments('-Ofast', '-ftree-vectorize', '-fno-math-errno', '-fno-trapping-math', language: 'cpp')
  endif
endif

code = '''
#include <fftw3.h>
int main() {
    fftwf_init_threads();
    return 0;
}
'''

# Try finding fftw3f_threads library
fftw3f_threads_dep = cc.find_library('fftw3f_threads', required: false)
if not cc.links(code, dependencies : fftw3f_dep)
  deps += fftw3f_threads_dep
endif

# MacOS homebrew installed fftw3f do not have pkgconfig for fftw3f_threads
if not fftw3f_threads_dep.found() and host_machine.system() == 'darwin'
  message('fftw3f_threads not found in default locations, trying Homebrew directory.')
  fftw3f_threads_dep = cc.find_library('fftw3f_threads', dirs: ['/opt/homebrew/lib'], required: false)
endif

if fftw3f_threads_dep.found()
  message('fftw3f_threads library found.')
  deps += fftw3f_threads_dep
else
  error('fftw3f_threads library not found.')
endif

shared_module('fft3dfilter', sources,
  dependencies : deps,
  install : true,
  install_dir : install_dir,
  gnu_symbol_visibility : 'hidden'
)