param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$ArtifactRoot,
    [Parameter(Mandatory = $true)][string]$InstallRoot,
    [Parameter(Mandatory = $true)][string]$RealSaltsRoot,
    [Parameter(Mandatory = $true)][string]$XmlOnlyFixture,
    [Parameter(Mandatory = $true)][string]$CMetaFixture,
    [Parameter(Mandatory = $true)][string]$VcpkgPrefix,
    [Parameter(Mandatory = $true)][string]$CMakeCommand
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    Write-Host "[package-isolation] $Label"
    & $CMakeCommand @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Invoke-ExpectedFailure {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$ExpectedPattern
    )
    Write-Host "[package-isolation] $Label (expected failure)"
    $output = (& $CMakeCommand @Arguments 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE
    Write-Host $output
    if ($exitCode -eq 0) {
        throw "$Label unexpectedly succeeded"
    }
    if ($output -notmatch $ExpectedPattern) {
        throw "$Label did not report expected evidence: $ExpectedPattern"
    }
}

function Invoke-WithEnvironment {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Values,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )
    $saved = @{}
    foreach ($name in $Values.Keys) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable(
            $name, $Values[$name], 'Process')
    }
    try {
        & $Action
    } finally {
        foreach ($name in $saved.Keys) {
            [Environment]::SetEnvironmentVariable(
                $name, $saved[$name], 'Process')
        }
    }
}

function Install-OptionalProfile {
    param(
        [Parameter(Mandatory = $true)][string]$Preset,
        [Parameter(Mandatory = $true)][string]$InstallPreset,
        [Parameter(Mandatory = $true)][string]$InstallDir,
        [string[]]$Options = @()
    )
    $configureArguments = @(
        '--fresh', '--preset', $Preset,
        '-DBUILD_TESTING=OFF',
        "-DCMAKE_INSTALL_PREFIX:PATH=$InstallDir") + $Options
    Invoke-Checked "$Preset configure" $configureArguments
    Invoke-Checked "$Preset build" @(
        '--build', '--preset', $Preset, '--parallel')
    Invoke-Checked "$Preset temporary install" @(
        '--build', '--preset', $InstallPreset, '--parallel')
}

function Test-BaseCMetaMissing {
    param([Parameter(Mandatory = $true)][string]$InstallDir)
    Invoke-WithEnvironment @{
        TURBOSCXML_ROOT = $InstallDir
        SALTS_ROOT = $XmlOnlyFixture
        SALTS_XML_ONLY_REAL_ROOT = $RealSaltsRoot
        CMAKE_PREFIX_PATH = $XmlOnlyFixture
    } {
        Invoke-ExpectedFailure 'base required VoiceXMLCMeta component' @(
            '--fresh',
            '-S', (Join-Path $SourceDir 'tests/install_consumer'),
            '-B', (Join-Path $ArtifactRoot 'base-cmeta-required'),
            '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release',
            '-DTURBOSCXML_INSTALL_CONSUMER_VOICEXML_CMETA_ONLY=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$XmlOnlyFixture") `
            'does not include the VoiceXMLCMeta component'
        Configure-Consumer 'base optional VoiceXMLCMeta probe' `
            (Join-Path $ArtifactRoot 'base-cmeta-missing') @(
                '-DTURBOSCXML_INSTALL_CONSUMER_EXPECT_VOICEXML_CMETA_MISSING=ON',
                "-DCMAKE_PREFIX_PATH:PATH=$XmlOnlyFixture")
    }
}

function Test-CMetaIsolation {
    param([Parameter(Mandatory = $true)][string]$InstallDir)
    Invoke-WithEnvironment @{
        TURBOSCXML_ROOT = $InstallDir
        SALTS_ROOT = $XmlOnlyFixture
        SALTS_XML_ONLY_REAL_ROOT = $RealSaltsRoot
        SALTS_CMETA_REAL_ROOT = $null
        CMAKE_PREFIX_PATH = $XmlOnlyFixture
    } {
        $optionalBuild = Join-Path $ArtifactRoot 'cmeta-optional-missing-salts'
        Configure-Consumer 'CMeta optional missing Salts targets' `
            $optionalBuild @(
                '-DTURBOSCXML_INSTALL_CONSUMER_EXPECT_VOICEXML_CMETA_MISSING=ON',
                '-DTURBOSCXML_INSTALL_CONSUMER_FORBID_QJS_DISCOVERY=ON',
                "-DCMAKE_PREFIX_PATH:PATH=$XmlOnlyFixture")
        Run-Consumers 'CMeta optional missing Salts targets' $optionalBuild @(
            'turboscxml_voicexml_install_consumer',
            'turboscxml_voicexml_install_consumer_cpp')

        Invoke-ExpectedFailure 'CMeta missing Salts targets' @(
            '--fresh',
            '-S', (Join-Path $SourceDir 'tests/install_consumer'),
            '-B', (Join-Path $ArtifactRoot 'cmeta-missing-salts'),
            '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release',
            '-DTURBOSCXML_INSTALL_CONSUMER_VOICEXML_CMETA_ONLY=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$XmlOnlyFixture") `
            'Salts::CMeta, Salts::QueryVM'
    }

    $consumerBuild = Join-Path $ArtifactRoot 'cmeta-enabled'
    Invoke-WithEnvironment @{
        TURBOSCXML_ROOT = $InstallDir
        SALTS_ROOT = $CMetaFixture
        SALTS_CMETA_REAL_ROOT = $RealSaltsRoot
        SALTS_XML_ONLY_REAL_ROOT = $null
        CMAKE_PREFIX_PATH = $CMetaFixture
        PATH = "$(Join-Path $RealSaltsRoot 'bin');$env:PATH"
    } {
        Configure-Consumer 'CMeta-enabled VoiceXML' $consumerBuild @(
            '-DTURBOSCXML_INSTALL_CONSUMER_VOICEXML_CMETA_ONLY=ON',
            '-DTURBOSCXML_INSTALL_CONSUMER_FORBID_QJS_DISCOVERY=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$CMetaFixture")
        Run-Consumers 'CMeta-enabled VoiceXML' $consumerBuild @(
            'turboscxml_voicexml_cmeta_install_consumer',
            'turboscxml_voicexml_cmeta_install_consumer_cpp')
    }
}

function Configure-Consumer {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$BuildDir,
        [string[]]$Options = @()
    )
    $arguments = @(
        '--fresh',
        '-S', (Join-Path $SourceDir 'tests/install_consumer'),
        '-B', $BuildDir,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release') + $Options
    Invoke-Checked "$Label configure" $arguments
    Invoke-Checked "$Label build" @('--build', $BuildDir, '--parallel')
}

function Run-Consumers {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$BuildDir,
        [Parameter(Mandatory = $true)][string[]]$Names
    )
    foreach ($name in $Names) {
        $executable = Join-Path $BuildDir "$name.exe"
        if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
            throw "$Label did not build $executable"
        }
        Write-Host "[package-isolation] $Label run $name"
        & $executable
        if ($LASTEXITCODE -ne 0) {
            throw "$Label executable $name failed with exit code $LASTEXITCODE"
        }
    }
}

function Test-VoiceOnlyIsolation {
    param(
        [Parameter(Mandatory = $true)][string]$Profile,
        [Parameter(Mandatory = $true)][string]$InstallDir
    )
    $consumerBuild = Join-Path $ArtifactRoot "$Profile-voice-only"
    Invoke-WithEnvironment @{
        TURBOSCXML_ROOT = $InstallDir
        SALTS_ROOT = $XmlOnlyFixture
        SALTS_XML_ONLY_REAL_ROOT = $RealSaltsRoot
        CMAKE_PREFIX_PATH = $XmlOnlyFixture
        PATH = "$(Join-Path $RealSaltsRoot 'bin');$env:PATH"
    } {
        Configure-Consumer "$Profile VoiceXML-only" $consumerBuild @(
            '-DTURBOSCXML_INSTALL_CONSUMER_VOICEXML_ONLY=ON',
            '-DTURBOSCXML_INSTALL_CONSUMER_FORBID_QJS_DISCOVERY=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$XmlOnlyFixture")
        Run-Consumers "$Profile VoiceXML-only" $consumerBuild @(
            'turboscxml_voicexml_install_consumer',
            'turboscxml_voicexml_install_consumer_cpp')
    }
}

function Test-QuickJsDiscovery {
    param([Parameter(Mandatory = $true)][string]$InstallDir)
    $fixtureEnvironment = @{
        TURBOSCXML_ROOT = $InstallDir
        SALTS_ROOT = $XmlOnlyFixture
        SALTS_XML_ONLY_REAL_ROOT = $RealSaltsRoot
        CMAKE_PREFIX_PATH = $XmlOnlyFixture
    }
    Invoke-WithEnvironment $fixtureEnvironment {
        Invoke-ExpectedFailure 'QuickJS explicit component dependency' @(
            '--fresh',
            '-S', (Join-Path $SourceDir 'tests/install_consumer'),
            '-B', (Join-Path $ArtifactRoot 'quickjs-explicit-negative'),
            '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release',
            '-DTURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS=ON',
            '-DTURBOSCXML_INSTALL_CONSUMER_FORBID_QJS_DISCOVERY=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$XmlOnlyFixture") 'qjs|disabled'
        Invoke-ExpectedFailure 'QuickJS no-component dependency' @(
            '--fresh',
            '-S', (Join-Path $SourceDir 'tests/install_consumer'),
            '-B', (Join-Path $ArtifactRoot 'quickjs-wide-negative'),
            '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release',
            '-DTURBOSCXML_INSTALL_CONSUMER_NO_COMPONENTS=ON',
            '-DTURBOSCXML_INSTALL_CONSUMER_FORBID_QJS_DISCOVERY=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$XmlOnlyFixture") 'qjs|disabled'
    }

    Invoke-WithEnvironment @{
        TURBOSCXML_ROOT = $InstallDir
        SALTS_ROOT = $RealSaltsRoot
        SALTS_XML_ONLY_REAL_ROOT = $null
        CMAKE_PREFIX_PATH = $VcpkgPrefix
        PATH = "$(Join-Path $RealSaltsRoot 'bin');$(Join-Path $VcpkgPrefix 'bin');$env:PATH"
    } {
        $explicitBuild = Join-Path $ArtifactRoot 'quickjs-explicit'
        Configure-Consumer 'QuickJS explicit component' $explicitBuild @(
            '-DTURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$VcpkgPrefix")
        Run-Consumers 'QuickJS explicit component' $explicitBuild @(
            'turboscxml_install_consumer',
            'turboscxml_ccxml_install_consumer',
            'turboscxml_ccxml_install_consumer_cpp')

        $wideBuild = Join-Path $ArtifactRoot 'quickjs-wide'
        Configure-Consumer 'QuickJS no-component package' $wideBuild @(
            '-DTURBOSCXML_INSTALL_CONSUMER_NO_COMPONENTS=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$VcpkgPrefix")
        Run-Consumers 'QuickJS no-component package' $wideBuild @(
            'turboscxml_install_consumer',
            'turboscxml_ccxml_install_consumer',
            'turboscxml_ccxml_install_consumer_cpp')
    }
}

function Test-CHttpDiscovery {
    param([Parameter(Mandatory = $true)][string]$InstallDir)
    Invoke-WithEnvironment @{
        TURBOSCXML_ROOT = $InstallDir
        SALTS_ROOT = $XmlOnlyFixture
        SALTS_XML_ONLY_REAL_ROOT = $RealSaltsRoot
        CMAKE_PREFIX_PATH = $XmlOnlyFixture
    } {
        Invoke-ExpectedFailure 'CHTTP explicit component dependency' @(
            '--fresh',
            '-S', (Join-Path $SourceDir 'tests/install_consumer'),
            '-B', (Join-Path $ArtifactRoot 'chttp-explicit-negative'),
            '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release',
            '-DTURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_RESOURCE=ON',
            '-DTURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_EVENT_IO=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$XmlOnlyFixture") 'Salts::CHTTP'
        Invoke-ExpectedFailure 'CHTTP no-component dependency' @(
            '--fresh',
            '-S', (Join-Path $SourceDir 'tests/install_consumer'),
            '-B', (Join-Path $ArtifactRoot 'chttp-wide-negative'),
            '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release',
            '-DTURBOSCXML_INSTALL_CONSUMER_NO_COMPONENTS=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$XmlOnlyFixture") 'Salts::CHTTP'
    }

    Invoke-WithEnvironment @{
        TURBOSCXML_ROOT = $InstallDir
        SALTS_ROOT = $RealSaltsRoot
        SALTS_XML_ONLY_REAL_ROOT = $null
        CMAKE_PREFIX_PATH = $VcpkgPrefix
        PATH = "$(Join-Path $RealSaltsRoot 'bin');$(Join-Path $VcpkgPrefix 'bin');$env:PATH"
    } {
        $explicitBuild = Join-Path $ArtifactRoot 'chttp-explicit'
        Configure-Consumer 'CHTTP explicit components' $explicitBuild @(
            '-DTURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_RESOURCE=ON',
            '-DTURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_EVENT_IO=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$VcpkgPrefix")
        Run-Consumers 'CHTTP explicit components' $explicitBuild @(
            'turboscxml_install_consumer',
            'turboscxml_ccxml_install_consumer',
            'turboscxml_ccxml_install_consumer_cpp',
            'turboscxml_chttp_install_consumer',
            'turboscxml_chttp_install_consumer_cpp',
            'turboscxml_chttp_event_io_consumer',
            'turboscxml_chttp_event_io_consumer_cpp')

        $wideBuild = Join-Path $ArtifactRoot 'chttp-wide'
        Configure-Consumer 'CHTTP no-component package' $wideBuild @(
            '-DTURBOSCXML_INSTALL_CONSUMER_NO_COMPONENTS=ON',
            "-DCMAKE_PREFIX_PATH:PATH=$VcpkgPrefix")
        Run-Consumers 'CHTTP no-component package' $wideBuild @(
            'turboscxml_install_consumer',
            'turboscxml_ccxml_install_consumer',
            'turboscxml_ccxml_install_consumer_cpp')
    }
}

function Reset-WorktreeDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)
    $separators = [char[]]@(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    $sourcePath = [IO.Path]::GetFullPath($SourceDir).TrimEnd($separators)
    $targetPath = [IO.Path]::GetFullPath($Path).TrimEnd($separators)
    $requiredPrefix = $sourcePath + [IO.Path]::DirectorySeparatorChar
    if (-not $targetPath.StartsWith(
            $requiredPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to reset a path outside the worktree: $targetPath"
    }
    if (Test-Path -LiteralPath $targetPath) {
        Remove-Item -LiteralPath $targetPath -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $targetPath | Out-Null
}

foreach ($requiredDirectory in @(
    $SourceDir, $RealSaltsRoot, $XmlOnlyFixture, $CMetaFixture,
    $VcpkgPrefix)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required package-isolation directory is missing: $requiredDirectory"
    }
}
Reset-WorktreeDirectory $ArtifactRoot
Reset-WorktreeDirectory $InstallRoot
$quickJsInstall = Join-Path $InstallRoot 'quickjs'
$chttpInstall = Join-Path $InstallRoot 'chttp'
$baseInstall = Join-Path $InstallRoot 'base'
$cmetaInstall = Join-Path $InstallRoot 'cmeta'

Push-Location $SourceDir
try {
    Install-OptionalProfile 'win-release-user' `
        'install-win-release-user' $baseInstall @(
            '-DTURBOSCXML_ENABLE_VOICEXML_CMETA=OFF')
    Test-VoiceOnlyIsolation 'base' $baseInstall
    Test-BaseCMetaMissing $baseInstall
    Install-OptionalProfile 'win-release-user' `
        'install-win-release-user' $cmetaInstall @(
            '-DTURBOSCXML_ENABLE_VOICEXML_CMETA=ON')
    Test-CMetaIsolation $cmetaInstall
    Install-OptionalProfile 'win-release-quickjs-user' `
        'install-win-release-quickjs-user' $quickJsInstall
    Test-VoiceOnlyIsolation 'quickjs' $quickJsInstall
    Test-QuickJsDiscovery $quickJsInstall
    Install-OptionalProfile 'win-release-chttp-user' `
        'install-win-release-chttp-user' $chttpInstall
    Test-VoiceOnlyIsolation 'chttp' $chttpInstall
    Test-CHttpDiscovery $chttpInstall
} finally {
    Pop-Location
}

Write-Host '[package-isolation] PASS: 4 isolated install profiles,' `
    '10 VoiceXML consumer runs, 2 optional CMeta probes,' `
    '6 required-dependency failures, and 16 optional/package-wide consumer runs'
