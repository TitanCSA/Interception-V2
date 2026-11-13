# Interception-V2 Dart / Flutter Windows SDK

`dart pub add interception_v2` or `flutter pub add interception_v2`.

```dart
final client=await Client.open(); // import package:interception_v2/interception_v2.dart
try { print(client.call("check", {"device": 1})); } finally { client.close(); }
```

Windows x64 only. Calls are synchronous; use a worker isolate for UI responsiveness. For compiled Dart or Flutter Windows deployment, explicitly copy `lib/native/InterceptionV2JsonSDK.dll` beside the executable or pass its absolute libraryPath. This is an FFI package; automatic Flutter DLL bundling is not provided. A full Flutter application has not been compiled in this validation. [API](doc/API.md). Matching driver/license required separately.
