# Third-party notices

This repository's own code is under the [MIT License](LICENSE). The binaries
built from it (`companion.dol`, `host.dol`, `client.gba`) also contain the
following third-party code. Their notices are reproduced or referenced here
as their licenses require for source and binary distribution.

| Component | Where | License |
|---|---|---|
| LinkCube (`gba-link-connection`) by Rodrigo Alfonso | `third_party/gba-link-connection/`, linked into `client.gba`. Its IRQ queue scheme is also ported to C in the pokeemerald-expansion connector (`src/gc_companion_link.c`), which carries the full notice. | MIT |
| libogc 3.1.0 (devkitPro) | Linked into `companion.dol` and `host.dol` | zlib-style; parts derived from RTEMS under a BSD-style license (reproduced below) |
| newlib C library (devkitPPC) | Linked into `companion.dol` and `host.dol` | Various permissive licenses, see [COPYING.NEWLIB](https://sourceware.org/newlib/COPYING.NEWLIB) |
| libgcc (devkitPPC / devkitARM, GCC) | Linked into all binaries | GPL with the GCC Runtime Library Exception (no obligations for the resulting binaries) |
| libgba (devkitPro) | Linked into `client.gba` (Phase 1 test client, not part of the public test build) | LGPL with devkitPro's static-linking exception; see the acknowledgement below |

The libogc console font (`console_font_8x16`) is part of libogc and is used
for the GameCube's debug text.

No Nintendo, Game Freak or The Pokémon Company code, graphics, sound or
fonts are included in this repository or in the binaries built from it.
Pokémon pictures, names and battle text that appear on the TV are read at
run time from the GBA ROM the user supplies.

## LinkCube (gba-link-connection)

See [third_party/gba-link-connection/LICENSE](third_party/gba-link-connection/LICENSE)
(MIT License, Copyright (c) 2020-2025 Rodrigo Alfonso). Source:
[afska/gba-link-connection](https://github.com/afska/gba-link-connection),
commit cc9b0b69aa16768b9b7a118f02de1b79618a87f4, unmodified.

## libogc

```
  libogc copyright (C) 2004 - 2025
    Michael Wiedenbauer (shagkur)
    Dave Murphy (WinterMute)

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any
  damages arising from the use of this software.

  Permission is granted to anyone to use this software for any
  purpose, including commercial applications, and to alter it and
  redistribute it freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you
     must not claim that you wrote the original software. If you use
     this software in a product, an acknowledgment in the product
     documentation would be appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and
     must not be misrepresented as being the original software.
  3. This notice may not be removed or altered from any source
     distribution.

Portions of this library, notably the threading code, were inspired by and
derived from code written for the RTEMS project (https://www.rtems.org/) by
On-Line Applications Research Corporation.

  COPYRIGHT (c) 1989-1999 On-Line Applications Research Corporation (OAR).
  Copyright (C) 1999  Eric Valette (eric.valette@free.fr)


  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
```

## libgba acknowledgement

The Phase 1 test client (`client.gba`) is based in part on the work of the
devkitPro project (https://devkitpro.org).
