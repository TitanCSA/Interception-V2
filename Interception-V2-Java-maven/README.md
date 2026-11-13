# Interception-V2 Java / Kotlin SDK

Maven coordinate `io.github.titancsa:interception-v2:1.0.0`. Java22+ Windows x64. Start with `--enable-native-access=ALL-UNNAMED`.

```java
try(var client=new titancsa.products.interception_v2.Client()){
  String response=client.call("check", "{\"device\":1}");
}
```

Call returns the JSON envelope `{ok,result}` or `{ok:false,code,message}`. Inspect ok/code in your JSON parser. Kotlin can use the same Client/AutoCloseable API. Native resources are extracted into a SHA-checked product cache. [Schema](docs/API.md). Original driver/license requirements unchanged.
