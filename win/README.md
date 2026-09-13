# Building RBC with MSVC

Run Nmake from `win` in an **x64 Native Tools Command Prompt for Visual Studio
2022**, with the Desktop development with C++ workload and a recent Windows SDK.
This is a separate build from the MSYS2/UCRT64 makefile. Autoconf is not needed.

## Dependencies

- Tcl 9 and Tk 9 **source trees**, built with MSVC for the same architecture and
  build configuration. RBC uses private Tk headers and Windows entry points;
  public installed headers alone are insufficient. Keep the runtime DLLs matched
  to these sources. Do not use MinGW `.dll.a` libraries for this build.
- Optional: MSVC-built Cairo 1.12 or newer with Win32 surfaces. A shared x64
  vcpkg installation is a convenient source:

  ```bat
  C:\vcpkg\vcpkg.exe install cairo:x64-windows
  ```

  Use `C:\vcpkg\installed\x64-windows` as `CAIRODIR`. The default layout is
  `include\cairo`, `lib\cairo.lib`, and `bin`. All Cairo runtime dependencies
  must remain available, not only the Cairo DLL. Static Cairo needs an explicit
  complete library list and is not covered by the commands below.

Build and install Tcl/Tk first if needed. For example, from their respective
`win` directories in the same developer prompt:

```bat
cd /d C:\src\tcl9.0.3\win
nmake /nologo /f makefile.vc INSTALLDIR=C:\Tcl9
nmake /nologo /f makefile.vc INSTALLDIR=C:\Tcl9 install

cd /d C:\src\tk9.0.3\win
nmake /nologo /f makefile.vc TCLDIR=C:\src\tcl9.0.3 INSTALLDIR=C:\Tcl9
nmake /nologo /f makefile.vc TCLDIR=C:\src\tcl9.0.3 INSTALLDIR=C:\Tcl9 install
```

Adapt version numbers and directories to your installation. The Nmake support
rules locate the corresponding libraries in the Tcl/Tk source build trees.
Explicit `TCLSTUBLIB`, `TKSTUBLIB`, `TKIMPLIB`, and `TCLSH` overrides are available
when those artifacts are elsewhere; they must still match the selected headers.

## Build, test and install

Set the common arguments once in the developer prompt:

```bat
cd /d C:\src\rbc-tk9\win
set "RBC_ARGS=TCLDIR=C:\src\tcl9.0.3 TKDIR=C:\src\tk9.0.3 INSTALLDIR=C:\Tcl9 CAIRO=1 CAIRODIR=C:\vcpkg\installed\x64-windows"
set "PATH=C:\Tcl9\bin;C:\vcpkg\installed\x64-windows\bin;%PATH%"

nmake /nologo /f makefile.vc %RBC_ARGS%
nmake /nologo /f makefile.vc %RBC_ARGS% test
nmake /nologo /f makefile.vc %RBC_ARGS% install
```

Install the Tcl dependencies listed in the main README (including those required
by `graphtoolbar`) in the Tcl interpreter used for testing.

A targeted test run uses the same arguments:

```bat
nmake /nologo /f makefile.vc %RBC_ARGS% test "TESTFLAGS=-file RBC.graph.renderer.A.test -verbose start"
```

`test` builds `rbccapitest.dll` as a separate public-API consumer and exposes it
through `RBC_CAPI_TEST_LIB`. Automated tests retain their native-renderer default;
renderer tests select their own backends. Demos and ordinary applications use
Cairo by default when `CAIRO=1`.

Use `shell` to start Tcl with the uninstalled RBC package on its search path:

```bat
nmake /nologo /f makefile.vc %RBC_ARGS% shell
```

Then verify the selected backend:

```tcl
package require rbc
::rbc::graph .g
puts [.g cget -renderer]
destroy .g
```

The build stages Tcl scripts, PostScript prologues, and bitmap resources beside
its generated package index and DLL. Installation also includes public headers,
the RBC stub library and source, demos, and available generated HTML documents.
Cairo DLLs are **not** copied into the installation: keep their `bin` directory
on `PATH` when launching applications.

## Options

| Nmake macro | Meaning |
|---|---|
| `CAIRO=0` | Native-only build; the default. |
| `CAIRO=1` | Compile and link Cairo; missing headers/backend cause a build error. |
| `CAIRODIR=path` | MSVC Cairo installation prefix. |
| `CAIRO_INCLUDES=flags` | Override Cairo compiler include flags. |
| `CAIRO_LIBS=libs` | Override the complete Cairo linker input list. |
| `CAIRO_BINDIR=path` | Runtime DLL directory prepended to `PATH` by `test` and `shell`. |
| `OPTS=symbols` | Debug build, using the standard Tcl Nmake rules; use matching dependency builds. |
| `TESTFLAGS=arguments` | Arguments for `tests/all.tcl`. |
| `INSTALLDIR=path` | Tcl installation/package destination, following the standard rules. |

Native and Cairo builds use separate output/object directories (`-cairo` suffix
for Cairo). After changing dependency paths or custom compiler/linker flags,
run `clean` with the same build arguments before rebuilding. A loadable RBC DLL
is the supported target; `OPTS=static` is rejected.

## Build support provenance

`rules-ext.vc`, `rules.vc`, `targets.vc`, and `nmakehlp.c` were refreshed from
[the tcl-cffi MSVC build support](https://github.com/apnadkarni/tcl-cffi/tree/main/win).
The bootstrap can use a newer compatible copy supplied by Tcl itself. These
rules handle Tcl 9's unversioned stub library names and `tcl9` DLL prefix.
Existing copyright and license notices are retained.

References: [MSVC C11 support](https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version),
[vcpkg Cairo port](https://github.com/microsoft/vcpkg/tree/master/ports/cairo).
