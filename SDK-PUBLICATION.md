# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | interception-v2 |
| CPP/conan | published_custom_source_index | [interception-v2](https://github.com/TitanCSA/sdk-packages/tree/089bc73095aa8eda89bacbe10d0b9e58394b0207/conan-index) [Source](packaging/native/) |
| CPP/nuget | paused_pending_owner_email_confirmation | Interception-V2.Native |
| NET/nuget | built_verified_requires_trusted_publishing_policy | InterceptionV2.SDK [Source](Interception-V2-NET-nuget/) |
| Python/pypi | built_fixture_and_production_verified | interception-v2 [Source](Interception-V2-Python-pypi/) |
| Python/conda-forge | custom_channel_install_verified_requires_conda_forge_review | [interception-v2](https://raw.githubusercontent.com/TitanCSA/sdk-packages/089bc73095aa8eda89bacbe10d0b9e58394b0207/conda-channel) [Source](packaging/conda/) |
| Rust/cargo | published_install_verified | [interception-v2](https://crates.io/crates/interception-v2) [Source](Interception-V2-Rust-cargo/) |
| JavaScript/npm | awaiting_browser_publication_authentication | [interception-v2](https://www.npmjs.com/package/interception-v2) [Source](Interception-V2-JavaScript-npm/) |
| Java/maven | published_install_verified | [interception-v2](https://central.sonatype.com/artifact/io.github.titancsa/interception-v2/1.0.0) [Source](Interception-V2-Java-maven/) |
| Java/jitpack | published_install_verified | [interception-v2](https://jitpack.io/#TitanCSA/Interception-V2/java-sdk-v1.0.0) [Source](Interception-V2-Java-maven/) |
| Go/modules | published_install_verified | [interception-v2](https://pkg.go.dev/github.com/TitanCSA/Interception-V2/Interception-V2-Go-modules@v1.0.0) [Source](Interception-V2-Go-modules/) |
| Go/pkg-go-dev | indexed_verified | [Documentation](https://pkg.go.dev/github.com/TitanCSA/Interception-V2/Interception-V2-Go-modules@v1.0.0) |
| PowerShell/gallery | published_install_verified | [InterceptionV2](https://www.powershellgallery.com/packages/InterceptionV2/1.0.0) [Source](Interception-V2-PowerShell-gallery/) |
| Dart/pub-dev | built_fixture_and_production_verified | interception_v2 [Source](Interception-V2-Dart-pub-dev/) |
| Ruby/rubygems | published_install_verified | [interception-v2](https://rubygems.org/gems/interception-v2) [Source](Interception-V2-Ruby-rubygems/) |
| PHP/packagist | composer_source_and_tag_published | [titancsa/interception-v2](https://packagist.org/packages/titancsa/interception-v2) |

Native SDK source and CMake/Conan/vcpkg/NuGet recipes share [`packaging/native`](packaging/native/LAYOUT.md). Previous release archives and tags remain unchanged.

Application and driver prerequisites and original licensing remain those documented by each SDK. No driver installer is bundled with these source packages.
