# libKontur - Minimalist Network Redirector

Native Android library for Unity IL2CPP games that redirects network requests from `konturinf.net` to `kntrmod.ru`.

## Features

- Hooks `UnityWebRequest.set_url` at runtime
- Transparent URL redirection
- ARM64 and ARMv7 support
- Zero dependencies (pure C++)
- Logs activity to logcat for debugging

## Building

### Automatic (GitHub Actions)

Push to GitHub and the workflow will automatically build both architectures. Download artifacts from Actions tab.

### Manual (requires Android NDK)

```bash
cd libKontur
ndk-build -j8
```

Output: `libs/arm64-v8a/libKontur.so` and `libs/armeabi-v7a/libKontur.so`

## Installation

1. Decompile target APK with apktool
2. Copy `libKontur.so` to `lib/arm64-v8a/` and/or `lib/armeabi-v7a/`
3. Rebuild and sign APK

Or use Zygisk/LSPosed for rootless injection.

## Debugging

```bash
adb logcat -s KonturMod
```

You'll see:
- `=== KonturMod loaded ===` on initialization
- `UnityWebRequest.set_url called: <url>` for each request
- `Redirected to: <new_url>` when domain is replaced

## Technical Details

- Uses IL2CPP reflection API to locate UnityWebRequest class
- Inline ARM64 hook (branch instruction patching)
- Modifies `Il2CppString` in-place for zero allocation
- JNI_OnLoad entry point for automatic loading

## License

MIT
