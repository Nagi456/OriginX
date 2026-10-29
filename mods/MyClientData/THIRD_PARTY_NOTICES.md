# Third-party notices

MyClientLauncher uses **nlohmann/json v3.12.0 (MIT)** to read installation and
version metadata. The unmodified single header is vendored at
`launcher/vendor/nlohmann/json.hpp`, from the pinned JSON dependency below.
Its MIT license is supplied beside that header and in `licenses/nlohmann-json/`.
MyClientLauncher also uses **TinyXML2 11.0.0 (zlib license)** for the original
MicrosoftGame.config XML. Its unmodified source/license are vendored at
`launcher/vendor/tinyxml2/`, with source archive and file hashes recorded in
`launcher/runtime.lock.json`. The license is also supplied in `licenses/tinyxml2/`.

Standalone setup downloads the official, unmodified runtime dependencies at the
URLs and SHA256 hashes in `launcher/runtime.lock.json`: LeviLamina Client 26.51.6,
PreLoader 1.16.3, PeEditor 3.9.1, bedrock-runtime-data 26.51.1-client.7,
CrashLogger 1.3.3 and levilamina-loc 1.6.4. No such runtime binaries or Minecraft
files are bundled. The placements and PeEditor command follow their official
package manifests. PreLoader is proprietary software governed by its upstream
terms; PeEditor is GPL-3.0-or-later and is executed as a separate downloaded tool.
Upstream source/package references are included in the lockfile. No PreLoader,
PeEditor, lip or LeviLauncher implementation source was incorporated into the
MyClient launcher.

MyClientData dynamically links to **LeviLamina Client v26.51.6**, maintained by LiteLDev:

- Source: https://github.com/LiteLDev/LeviLamina/tree/32fcaa02baa38371b705358801c7d185c284233e
- License: LGPL-3.0 (COPYING.LESSER and accompanying GPL terms in COPYING).
- The SDK is used without source modifications. Its headers and required memory-operator definitions are compiled into the mod; the loader DLL is installed separately and is not bundled here.
- Full MyClient adapter source and rebuild instructions are supplied. You can rebuild/relink the adapter with a modified SDK. The exact loader version check in `client/MyClientData.cpp` and manifest must agree with your compatible development loader.
- Nothing in this distribution restricts reverse engineering for debugging modifications to the LGPL-covered library.

The SDK's header dependencies are downloaded from their upstream repositories at the revisions and SHA-256 hashes in `tools/client-sdk.lock.json`. Their license files are included in the binary distribution's `mods/MyClientData/licenses/` directory. They include EnTT (MIT), GLM (MIT), Microsoft GSL (MIT), LevelDB (BSD-3-Clause), RapidJSON (MIT and bundled notices), type_safe/debug_assert (MIT), expected-lite (BSL-1.0), fmt (MIT), ConcurrentQueue (BSD-2-Clause), stb (public domain/MIT), parallel-hashmap (Apache-2.0), nlohmann/json (MIT), PCG (Apache-2.0/MIT), Boost.PFR (BSL-1.0), magic_enum (MIT), and CTRE (Apache-2.0).

No Latite, Lamium, or MCBE_DX12_ModBase source was incorporated. Those repositories were consulted during SDK research. The integration uses the official LeviLamina API directly; no third-party project license is imposed on the existing MyClient UI by copying its implementation.

Microsoft's runtime and Windows SDK are build prerequisites subject to their own license terms; they are not included in this distribution. LLVM and xwin are build tools, not bundled runtime dependencies.
