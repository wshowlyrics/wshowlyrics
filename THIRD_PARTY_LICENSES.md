# Third-Party Licenses

wshowlyrics is licensed under the GNU General Public License v3.0 or later
(see [LICENSE](LICENSE)). It incorporates code from the projects below; their
copyright notices and license terms are reproduced here as those licenses
require.

| Files | Origin | License | Notice |
|---|---|---|---|
| `src/`, `fuzz/`, `tools/` (overall project) | [wshowkeys](https://git.sr.ht/~sircmpwn/wshowkeys) — Copyright (C) 2019 Drew DeVault | GPL-3.0 | [LICENSE](LICENSE), README "License" section |
| `src/utils/shm/shm.c` (`buffer_release`, `create_buffer`, `destroy_buffer`, `get_next_buffer`), `src/utils/shm/shm.h` (`struct pool_buffer`) | [sway](https://github.com/swaywm/sway) `client/pool-buffer.c`, `include/pool-buffer.h` — Copyright (c) 2016-2017 Drew DeVault | MIT | [below](#sway-mit), file header of `shm.c` |
| `src/utils/shm/shm.c` (`randname`, `create_shm_file`, `allocate_shm_file`) | [The Wayland Protocol](https://git.sr.ht/~sircmpwn/wayland-book) book, "Allocating a shared memory pool" — Drew DeVault | Public domain / CC0-1.0 | None required; origin noted in `shm.c` |
| `protocols/wlr-layer-shell-unstable-v1.xml` | [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) — Copyright © 2017 Drew DeVault | HPND-sell-variant (MIT-style) | `<copyright>` block preserved in the file |

Build and runtime dependencies (cairo, pango, gdk-pixbuf, libcurl, json-c,
wayland, ...) are linked, not vendored, and are distributed under their own
licenses.

## sway (MIT)

```
Copyright (c) 2016-2017 Drew DeVault

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
