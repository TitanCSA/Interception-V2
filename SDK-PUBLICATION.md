# Interception-V2 — SDK et installation

Projet : [Interception-V2](https://github.com/TitanCSA/Interception-V2). [Sources et documentation des SDK](https://github.com/TitanCSA/sdk-packages/tree/main/projects/Interception-V2).

Choisis le paquet a installer. Installe son outil habituel (Node.js/npm, Python/pip, .NET, etc.), puis execute la commande dans le dossier de ton application. Les pilotes et applications requis restent a installer separement selon le README du projet.

Les SDK natifs sont destines a Windows x64. Java utilise Java22+ (Kotlin passe par le SDK Java). Flutter utilise le SDK Dart ; le chargement de la DLL doit etre configure dans l application. AutoHotInterceptor .NET utilise .NET Framework4.8.

| Registre | Paquet | Installation / utilisation |
| --- | --- | --- |
| vcpkg | `interception-v2` | `vcpkg install interception-v2:x64-windows` |
| Conan | `interception-v2` | `conan install --requires=interception-v2/2.0.0 -r titancsa --build=missing -s compiler.cppstd=17` |
| NuGet | `Interception-V2.Native` | `Install-Package Interception-V2.Native -Version 2.0.0` |
| NuGet | `InterceptionV2.SDK` | `dotnet add package InterceptionV2.SDK --version 1.0.0` |
| PyPI | `interception_v2` | Commande prevue apres publication : `python -m pip install interception-v2==1.0.0` |
| Conda | `interception-v2` | `conda install --override-channels -c https://raw.githubusercontent.com/TitanCSA/sdk-packages/089bc73095aa8eda89bacbe10d0b9e58394b0207/conda-channel -c conda-forge interception-v2` (canal TitanCSA) |
| crates.io | `interception_v2` | `cargo add interception-v2@1.0.0` |
| npm | `interception_v2` | Commande prevue apres publication : `npm install interception-v2@1.0.0` |
| Maven Central | `io.github.titancsa:interception-v2` | `io.github.titancsa:interception-v2:1.0.0 (pom.xml / Gradle)` |
| JitPack | `com.github.TitanCSA:Interception-V2` | `com.github.TitanCSA:Interception-V2:java-sdk-v1.0.0 (avec le depot JitPack)` |
| Go Modules | `github.com/TitanCSA/Interception-V2/Interception-V2-Go-modules` | `go get github.com/TitanCSA/Interception-V2/Interception-V2-Go-modules@v1.0.0` |
| pkg.go.dev | `github.com/TitanCSA/Interception-V2/Interception-V2-Go-modules` | `Documentation : https://pkg.go.dev/github.com/TitanCSA/Interception-V2/Interception-V2-Go-modules@v1.0.0` |
| PowerShell Gallery | `interception_v2` | `Install-Module InterceptionV2 -RequiredVersion 1.0.0 -Scope CurrentUser` |
| pub.dev | `interception_v2` | Commande prevue apres publication : `dart pub add interception_v2:1.0.0` |
| RubyGems | `interception_v2` | `gem install interception-v2 -v 1.0.0` |
| Packagist | `interception_v2` | Commande prevue apres publication : `composer require titancsa/interception-v2:1.0.0` |

## Configuration vcpkg et Conan

Les sources et headers C / C++ sont dans le dossier central de ce projet. Les headers C se trouvent dans les dossiers `*-C-ffi` ou `*-C-json` (Interceptor-MultiLang : `native/`). Les paquets natifs C++ et la disponibilite du bridge C sont distincts ; consulte leur README avant de lier une application C.

Pour vcpkg, ajoute un fichier `vcpkg-configuration.json` a cote de ton manifeste `vcpkg.json` :

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/TitanCSA/vcpkg-registry",
      "baseline": "9a4b8cb920d4cfc97ce6d7af461bcc7508392a68",
      "packages": [
        "interception-v2",
        "interception-v2-sdk"
      ]
    }
  ]
}
```

Pour Conan2, configure une seule fois l index source :

```powershell
git clone https://github.com/TitanCSA/sdk-packages.git
conan remote add titancsa ./sdk-packages/conan-index -t local-recipes-index
```

Execute ensuite la commande Conan du tableau. Ce registre construit les bibliotheques depuis leurs sources ; il est distinct de ConanCenter. La commande NuGet native s execute dans la console du gestionnaire de packages de Visual Studio.

## Configuration Maven et JitPack

Dans le bloc `<dependencies>` de ton `pom.xml` :

```xml
<dependency>
  <groupId>io.github.titancsa</groupId>
  <artifactId>interception-v2</artifactId>
  <version>1.0.0</version>
</dependency>
```

Avec Gradle Kotlin :

```kotlin
implementation("io.github.titancsa:interception-v2:1.0.0")
```

Pour JitPack, ajoute aussi le depot `https://jitpack.io`, puis utilise les coordonnees du tableau. Les versions Go et JitPack deja publiees continuent de pointer vers leurs anciens tags immuables dans le depot du projet : ces tags restent disponibles. Leurs sources de travail sont maintenant centralisees ici ; aucun ancien import Go n est renomme.

## Etat et documentation

Les commandes marquees « prevues » ne sont pas encore des installations disponibles. Le canal Conda TitanCSA n est pas le catalogue officiel conda-forge. Les versions et licences existantes ne sont pas remplacees par cette reorganisation.

Les guides API et exemples de chaque langage se trouvent dans leurs dossiers SDK, a cote des sources. Les bindings peuvent exposer une partie de l API C++ ; les limites restent documentees dans chaque SDK.

[Retour au catalogue central](../../README.md)
