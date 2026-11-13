# Interception V2 - Windows Keyboard and Mouse Kernel Driver

Keyboard and mouse interception with the familiar [oblitum/Interception](https://github.com/oblitum/Interception) C SDK. Capture an input, change it, and forward it to Windows using the original `interception_*` functions.

<!-- full-version-contact:start -->
> [!IMPORTANT]
> **Get the full version without demo restrictions**
>
> **Discord username: `titancsa`**
>
> Open Discord, choose **Friends > Add Friend**, enter **`titancsa`**, then send a direct message. Ask for the **full version without the demo time limit**, and mention **Interception V2**.
>
> Tell me whether you need personal use, business integration or OEM redistribution, and ask for pricing and license / activation instructions.
<!-- full-version-contact:end -->

Interception V2 uses the keyboard/mouse input backend from [Interceptor Keyboard & Mouse](https://github.com/TitanCSA/Interceptor-Keyboard-Mouse), with a kernel compatibility layer for the original SDK. Demo/evaluation driver source is documented in [driver/README.md](driver/README.md); the original library and all seven sample projects are included here.

**Demo: one hour per Windows boot, starting with the first successful context creation.** Closing an application, reinstalling, or changing the clock does not renew this period. When it expires, interception and injection stop and physical input returns to passthrough. Restart Windows to start another demo period. For an unrestricted edition, contact **titancsa** on Discord.

## Install

Windows 10 version 1709 or later and Windows 11, **x64**. Applications can be 32-bit or 64-bit. Supported sources are HID keyboard and mouse devices handled by the Interceptor keyboard/mouse backend, including compatible USB and Bluetooth HID devices. PS/2 and arbitrary vendor-specific input protocols are not covered.

[Download Interception-V2.zip](https://github.com/TitanCSA/Interception-V2/releases/latest/download/Interception-V2.zip) - Microsoft-signed driver, self-contained command line installer, x86/x64 SDK and all seven compiled sample programs with their source.

The kernel driver is signed by **Microsoft Windows Hardware Compatibility Publisher**. Test signing is not required.

1. Uninstall the original Interception driver, or another Interceptor keyboard/mouse edition, and restart Windows. These editions must not share the same input devices or legacy endpoint names.
2. Extract the Interception V2 ZIP and open **Command Prompt as administrator**.
3. Run the familiar installer command:

   ```bat
   "command line installer\install-interception.exe" /install
   ```

4. Restart Windows if requested. Run your application as administrator.

To uninstall:

```bat
"command line installer\install-interception.exe" /uninstall
```

Restart if requested to finish restoring Windows drivers and removing in-use packages. Running the installer without arguments displays its help. It refuses conflicting editions rather than overwriting their endpoints.

## Use an existing application

Keep its original `interception.dll`. V2 implements the device endpoints and IOCTL data layout used by that DLL, including 32-bit applications on 64-bit Windows. Choose the DLL matching your **application's architecture** when building a new application.

The API, header, DLL filename and installer switches retain the original names. Device IDs are **1-10 for keyboards** and **11-20 for mouse devices**. Use `interception_get_hardware_id()` or the `hardwareid` sample to identify a source; do not assume an ID matches a particular USB port across boots.

Multiple contexts can use per-device filters and priorities. Higher precedence receives first; equal precedence follows context opening order. Forward with `interception_send()` to pass an input to the next matching context, then to Windows. Queues and context counts are bounded; applications must drain their queues promptly. Overflow revokes capture to restore input.

API compatibility does not guarantee compatibility with every existing application or device. Applications relying on undocumented original-driver behavior need their own validation. V2 also preserves an emergency escape: **physical Ctrl+Alt+Delete revokes interception**. The original `cadstop` sample is included for source continuity, but cannot suppress this escape in V2.

## Receive, change, forward

```cpp
#include "interception.h"

int main()
{
    auto context = interception_create_context();
    if (!context) return 1; // Install, elevate, and check the demo period.

    interception_set_filter(context, interception_is_keyboard,
                            INTERCEPTION_FILTER_KEY_ALL);

    InterceptionKeyStroke key{};
    while (auto device = interception_wait(context)) {
        if (interception_receive(context, device,
                reinterpret_cast<InterceptionStroke*>(&key), 1) != 1) break;

        if (key.code == 0x3A) key.code = 0x01; // Caps Lock -> Escape.
        if (interception_send(context, device,
                reinterpret_cast<const InterceptionStroke*>(&key), 1) != 1) break;
    }
    interception_destroy_context(context);
}
```

Link against the matching `interception.lib` and place `interception.dll` beside your EXE. Receiving removes inputs from your queue; forwarding sends them onward. An input you receive and do not forward is blocked. Always release the context when finished.

The unchanged original SDK has no V2-specific error or demo-status API. A failed context creation returns `nullptr`; send/receive return zero when unavailable. After expiry, a waiting context wakes so the application can leave its receive loop. Creating another context then requires a Windows restart.

## Files and samples

| Folder | Contents |
| --- | --- |
| `library/` | Original `interception.h`, library implementation and resource/build files |
| `library/x86/` and `library/x64/` in the release ZIP | Matching `interception.dll` and `interception.lib` |
| `samples/` | Original sample source projects and shared sample utilities |
| `samples/x86/` and `samples/x64/` in the release ZIP | All seven compiled examples, with their matching DLL |
| `tools/` | Original WDK build helpers |
| `licenses/` | Original Interception license documents and V2 driver demo terms |
| `command line installer/` in the release ZIP | Self-contained `install-interception.exe` |

The installer contains the signed keyboard/mouse packages, control endpoint and per-boot demo clock, plus its installation and recovery helpers. No separate driver files need to be copied or installed manually. The demo clock is a kernel-only component used to preserve the one-hour budget when the main driver reloads; it remains resident until Windows restarts. Setup restores Windows input drivers and removes its own packages during uninstall, finishing in-use cleanup after a restart when required.

| Sample | Demonstration |
| --- | --- |
| `axes` | Mouse axis transformation |
| `cadstop` | Original Ctrl+Alt+Delete sample; see V2 safety limitation above |
| `caps2esc` | Remap Caps Lock to Escape |
| `hardwareid` | Print hardware identifiers |
| `identify` | Identify the keyboard or mouse producing input |
| `mathpointer` | Generate mouse motion from mathematical curves |
| `x2y` | Remap a keyboard key |

## Build the SDK and samples

Demo/evaluation driver source is available; see [driver/README.md](driver/README.md) for
build instructions and separate evaluation-source terms. Those terms apply to
Titan-authored driver code. Third-party source licenses and existing application/SDK
licenses remain applicable. Full/commercial implementation and private signing or
activation material are excluded.

Install Visual Studio 2022 with **Desktop development with C++**, the Windows SDK and CMake, then run:

```powershell
.\Build.ps1
```

This builds x86 and x64 DLLs, import libraries and all seven examples into `bin/SDK/`. It does not compile or install a kernel driver. The original WDK 7.1 build files remain included; the added CMake build supports current Visual Studio.

## Attribution and terms

The SDK, original samples and build tools are preserved from [oblitum/Interception, commit 39eecbb](https://github.com/oblitum/Interception/tree/39eecbbc46a52e0402f783b872ef62b0254a896a). Their original copyrights and [license documents](licenses/) remain in force. V2 is an independent project and is not endorsed by Francisco Lopes or Microsoft. Microsoft driver signing authenticates the package; it is not a promise of application compatibility.

The separately developed V2 driver remains a time-limited demo; its source is documented in [driver/README.md](driver/README.md); see [driver terms](licenses/V2-DRIVER-DEMO.txt). These terms do not replace or expand rights to upstream Interception code. Commercial use of upstream assets must comply with their applicable license.

Use the software only on devices you own or are authorized to manage. It is provided as-is, without warranties, to the extent permitted by applicable law. You are responsible for your use; the authors do not authorize unlawful activity or accept responsibility for misuse. Keep a way to restart or recover the machine while testing input interception.

<!-- central-sdk-packages:start -->
## SDK : langages et installation

Les sources, licences, guides API et exemples des SDK sont regroupes dans [le depot dedie](https://github.com/TitanCSA/sdk-packages/tree/main/projects/Interception-V2). Choisis ton langage ci-dessous, puis execute sa commande dans le dossier de ton application.

| Langage / registre | Disponibilite | Installation / utilisation |
| --- | --- | --- |
| C++ / vcpkg | Registre TitanCSA disponible (configuration ci-dessous) | `vcpkg install interception-v2:x64-windows` |
| C++ / Conan | Registre TitanCSA disponible (configuration ci-dessous) | `conan install --requires=interception-v2/2.0.0 -r titancsa --build=missing -s compiler.cppstd=17` |
| C++ / NuGet natif | Publie et verifie | `Install-Package Interception-V2.Native -Version 2.0.0` |
| C# / .NET | Publie et verifie | `dotnet add package InterceptionV2.SDK --version 1.0.0` |
| Python / pip | Pas encore disponible / validation en cours | Commande prevue apres publication : `python -m pip install interception-v2==1.0.0` |
| Python / Conda | Canal TitanCSA disponible ; acceptation conda-forge encore en attente | `conda install --override-channels -c https://raw.githubusercontent.com/TitanCSA/sdk-packages/089bc73095aa8eda89bacbe10d0b9e58394b0207/conda-channel -c conda-forge interception-v2` |
| Rust / Cargo | Publie et verifie | `cargo add interception-v2@1.0.0` |
| JavaScript et TypeScript / npm | Pas encore disponible / validation en cours | Commande prevue apres publication : `npm install interception-v2@1.0.0` |
| Java et Kotlin / Maven Central | Publie et verifie | `io.github.titancsa:interception-v2:1.0.0 (pom.xml / Gradle)` |
| Java et Kotlin / JitPack | Publie et verifie | `com.github.TitanCSA:Interception-V2:java-sdk-v1.0.0 (avec le depot JitPack)` |
| Go / Go Modules | Publie et verifie | `go get github.com/TitanCSA/Interception-V2/Interception-V2-Go-modules@v1.0.0` |
| Go / pkg.go.dev | Publie et verifie | `Documentation : https://pkg.go.dev/github.com/TitanCSA/Interception-V2/Interception-V2-Go-modules@v1.0.0` |
| PowerShell / Gallery | Publie et verifie | `Install-Module InterceptionV2 -RequiredVersion 1.0.0 -Scope CurrentUser` |
| Dart et Flutter / pub.dev | Pas encore disponible / validation en cours | Commande prevue apres publication : `dart pub add interception_v2:1.0.0` |
| Ruby / RubyGems | Publie et verifie | `gem install interception-v2 -v 1.0.0` |
| PHP / Composer | Pas encore disponible / validation en cours | Commande prevue apres publication : `composer require titancsa/interception-v2:1.0.0` |

Pour vcpkg et Conan, configure d abord le registre TitanCSA avec [ces instructions](https://github.com/TitanCSA/sdk-packages/tree/main/projects/Interception-V2/README.md#c-et-c). Pour Java/Kotlin, le guide donne le bloc Maven/Gradle a ajouter. Windows x64 est requis pour les bindings natifs ; les pilotes/applications restent a installer separement.

Les commandes indiquees comme prevues ne sont pas encore disponibles. Les versions Go/JitPack existantes utilisent les anciens tags conserves ; aucun ancien import Go n est renomme.

[Guide complet, prerequis et exemples](https://github.com/TitanCSA/sdk-packages/tree/main/projects/Interception-V2/README.md).
<!-- central-sdk-packages:end -->
