# Third-party source

`stb_image.h` is the unmodified **stb_image v2.30** PNG/image decoder by Sean Barrett and contributors.

- Upstream: <https://github.com/nothings/stb>
- Pinned upstream commit: `d14d46f6b44456eb88beac58cba833a4135d4c89`
- Original file: <https://raw.githubusercontent.com/nothings/stb/d14d46f6b44456eb88beac58cba833a4135d4c89/stb_image.h>
- SHA-256: `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`
- License: dual public domain / MIT, reproduced in full at the end of `stb_image.h`.

The implementation is compiled once in `source/image_data.cpp`. This project enables only PNG decoding (`STBI_ONLY_PNG`), UTF-8 Windows file paths (`STBI_WINDOWS_UTF8`), and a maximum image dimension of 8192. PNG files retain their original row order without a vertical flip.
