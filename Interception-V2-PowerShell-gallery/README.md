# Interception-V2 PowerShell SDK

PowerShell7.4+ Windows x64. `Install-Module InterceptionV2 -Scope CurrentUser`.

```powershell
$client=Connect-InterceptionV2SDK
try { Invoke-InterceptionV2SDK -Client $client -Operation check -Arguments @{device=1} } finally { Disconnect-InterceptionV2SDK -Client $client }
```

Use -Arguments with a hashtable matching [schema](docs/API.md). Native errors retain their Windows code through the inner exception. Matching driver installed separately.
