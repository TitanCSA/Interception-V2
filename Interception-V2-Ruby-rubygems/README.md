# Interception-V2 Ruby SDK

`gem install interception-v2`

Windows x64 Ruby3.1+.

```ruby
require "interception_v2"
InterceptionV2SDK::Client.open { |client| p client.call("check", {"device" => 1}) }
```

[API and operation schema](docs/API.md). Matching driver installed separately.
