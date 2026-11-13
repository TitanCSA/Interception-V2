param([string]$Manifest,[string]$Fixture)
$ErrorActionPreference="Stop"
Import-Module $Manifest
Test-ModuleManifest $Manifest|Out-Null
$c=Connect-InterceptionV2SDK
try{$rejected=$false;try{Invoke-InterceptionV2SDK -Client $c -Operation check|Out-Null}catch{$rejected=$true};if(-not $rejected){throw "Invalid request accepted"};if($Fixture){$tests=Get-Content -Raw -LiteralPath 'D:\GithubRepo\Interception-V2\Interception-V2-PowerShell-gallery\docs\contract.json'|ConvertFrom-Json -AsHashtable;foreach($test in $tests){$result=Invoke-InterceptionV2SDK -Client $c -Operation $test.operation -Arguments ([hashtable]$test.arguments);$value=$result.($test.expected_key);if($test.ContainsKey("expected_value") -and $value -cne $test.expected_value){throw "Wrong result"};if($test.ContainsKey("minimum_length") -and @($value).Count -lt $test.minimum_length){throw "Array missing"}}}}finally{Disconnect-InterceptionV2SDK -Client $c}
