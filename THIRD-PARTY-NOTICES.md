# Third-party notices

Glass76 itself is MIT licensed — see [LICENSE](LICENSE). It is built on, and
ships, the components below. Their notices are reproduced here because two of
them require it of binary redistributions, and the installer copies this file
alongside the plug-in for that reason.

---

## VST 3 SDK

Steinberg Media Technologies GmbH · https://github.com/steinbergmedia/vst3sdk
Version pinned by this project: **3.8.1 (`v3.8.1_build_84`)**
**MIT License**, since SDK 3.7.9.

> Copyright (c) 2026, Steinberg Media Technologies GmbH
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

The SDK is *not* vendored in this repository. `CMakeLists.txt` locates or
clones a checkout at build time; see [docs/BUILDING.md](docs/BUILDING.md).

**VST is a registered trademark of Steinberg Media Technologies GmbH.** Glass76
is not affiliated with or endorsed by Steinberg. If you distribute a build of
this plug-in you are the one shipping a VST 3 product, and Steinberg's
[VST 3 usage guidelines](https://www.steinberg.net/developers/) apply to you —
in short, register as a developer and do not put "VST" in the product name.

---

## VSTGUI 4

Steinberg Media Technologies GmbH · shipped as a submodule of the VST 3 SDK
**BSD 3-Clause License**. Statically linked into `Glass76.vst3`, which is why
this notice ships with the binary.

> VSTGUI LICENSE
> (c) 2022, Steinberg Media Technologies, All Rights Reserved
>
> Redistribution and use in source and binary forms, with or without
> modification, are permitted provided that the following conditions are met:
>
> * Redistributions of source code must retain the above copyright notice, this
>   list of conditions and the following disclaimer.
> * Redistributions in binary form must reproduce the above copyright notice,
>   this list of conditions and the following disclaimer in the documentation
>   and/or other materials provided with the distribution.
> * Neither the name of the Steinberg Media Technologies nor the names of its
>   contributors may be used to endorse or promote products derived from this
>   software without specific prior written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
> AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
> IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
> ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
> LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
> CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
> SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
> INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
> CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
> ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
> POSSIBILITY OF SUCH DAMAGE.

The full, current text is in `vstgui4/LICENSE` of your SDK checkout.

---

## Inter and Inter Display

The Inter Project Authors · https://github.com/rsms/inter
**SIL Open Font License 1.1**

Four faces ship inside the plug-in bundle, under
`Contents/Resources/Fonts/`, together with the licence text as
`Inter-LICENSE.txt` — the OFL requires the licence to travel with the font.
The sources are in [`resource/Fonts/`](resource/Fonts).

Inter stands in for SF Pro, which is licensed for use on Apple platforms only
and cannot be embedded in a Windows binary. See the *Fonts* section of the
[README](README.md) for how the bundle-local font collection is loaded.

---

## Allura

The Allura Project Authors · https://github.com/googlefonts/allura
**SIL Open Font License 1.1**

Ships inside the plug-in bundle under `Contents/Resources/Fonts/`, together
with the licence text as `Allura-LICENSE.txt`, the same way and for the same
OFL reason as Inter above. The source is in
[`resource/Fonts/`](resource/Fonts).

Used for the "Glass76 Signature" wordmark, so the script face renders
identically on every machine instead of falling back to whichever cursive
system font happens to be installed. See the *Fonts* section of the
[README](README.md).

---

## Design reference

The interface follows macOS 27 metrics measured from Apple's published UI kit.
No Apple code, artwork, font, or asset is included in this repository or in the
built plug-in. "macOS" and "SF Pro" are trademarks of Apple Inc.; Glass76 is
not affiliated with or endorsed by Apple.

---

## Build-time only, not redistributed

- **NSIS 3** (zlib/libpng license) builds `installer/Glass76.nsi`. The stub it
  links into the resulting installer `.exe` carries its own permissive licence.
- **CMake** (BSD 3-Clause) and the **MSVC toolchain** build the plug-in.
- **Pillow** (MIT-CMU) regenerates `installer/assets/Glass76.ico`. Only needed
  if you change the icon.
