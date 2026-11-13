# Interception-V2 Python SDK

`pip install interception-v2`

Windows x64 Python3.10+. No driver is installed by this package.

```python
from interception_v2 import Client
with Client() as client:
    info = client.call("check", {"device": 1})
```

[Operation schema and API](docs/API.md). Check errors and the product licensing state before device creation.
