# 唯一验证入口
# 用法：
#   powershell -File scripts/verify.ps1
#   powershell -File scripts/verify.ps1 --scope unit
#   powershell -File scripts/verify.ps1 --change 0001-harmony-loop-kernel
param(
    [string]$Scope = "all",
    [string]$Change = "local"
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$artifacts = Join-Path $root "artifacts"
if (-not (Test-Path $artifacts)) { New-Item -ItemType Directory -Path $artifacts | Out-Null }
$log = Join-Path $artifacts "verify-$Change-$stamp.log"

function Write-Log {
    param([string]$Text)
    $Text | Tee-Object -FilePath $log -Append
}

Write-Log "== verify =="
Write-Log "change: $Change  scope: $Scope  log: $log"

$failed = 0
$ran = 0

function Invoke-Step {
    param([string]$Name, [scriptblock]$Action)
    $script:ran++
    Write-Log ""
    Write-Log "--- [$Name] ---"
    try {
        & $Action 2>&1 | Tee-Object -FilePath $log -Append
        if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
            $script:failed++
            Write-Log "RESULT: FAIL ($Name) exit=$LASTEXITCODE"
        } else {
            Write-Log "RESULT: PASS ($Name)"
        }
    } catch {
        $script:failed++
        Write-Log "RESULT: FAIL ($Name) exception=$($_.Exception.Message)"
    }
}

# ---------- 1. 规范校验（唯一可脱离鸿蒙工具链执行的层） ----------
if ($Scope -eq "all" -or $Scope -eq "spec") {
    Invoke-Step "openspec validate --all --strict" {
        openspec validate --all --strict
    }
}

# ---------- 2. 合成 fixture 生成与相位连续性检查 ----------
if ($Scope -eq "all" -or $Scope -eq "fixtures") {
    Invoke-Step "make-fixtures.py" {
        python scripts/make-fixtures.py
    }
}

# ---------- 3. C++ 单元测试（需 CMake + 本机编译器） ----------
if ($Scope -eq "all" -or $Scope -eq "unit") {
    $nativeDir = Join-Path $root "entry\src\main\cpp"
    if (Test-Path (Join-Path $nativeDir "tests")) {
        Invoke-Step "native unit tests (cmake + ctest)" {
            $buildDir = Join-Path $artifacts "native-build"
            cmake -S $nativeDir -B $buildDir -DBUILD_TESTS=ON
            if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
            cmake --build $buildDir --config Release
            if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
            ctest --test-dir $buildDir -C Release --output-on-failure
        }
    } else {
        Write-Log ""
        Write-Log "--- [native unit tests] ---"
        Write-Log "SKIP: 尚未创建 entry/src/main/cpp/tests，登记为 NOT RUN"
    }
}

# ---------- 4. HAP 构建（需 DevEco Studio） ----------
if ($Scope -eq "all" -or $Scope -eq "build") {
    $hvigorw = Join-Path $root "hvigorw.bat"
    if (-not (Test-Path $hvigorw)) {
        $deveco = "D:\program\DevEco Studio"
        $candidate = Join-Path $deveco "tools\hvigor\bin\hvigorw.bat"
        if (Test-Path $candidate) {
            $hvigorw = $candidate
            if (-not $env:DEVECO_SDK_HOME) { $env:DEVECO_SDK_HOME = Join-Path $deveco "sdk" }
        }
    }
    if (Test-Path $hvigorw) {
        Invoke-Step "hvigorw assembleHap" {
            & $hvigorw --mode module -p product=default assembleHap
        }
    } else {
        Write-Log ""
        Write-Log "--- [hvigorw assembleHap] ---"
        Write-Log "SKIP: 未找到 hvigorw（工程根目录与 DevEco Studio 安装目录均无），登记为阻塞"
    }
}

# ---------- 5. 真机听测（人工，仅提示） ----------
if ($Scope -eq "all" -or $Scope -eq "device") {
    Write-Log ""
    Write-Log "--- [真机听测] ---"
    Write-Log "MANUAL: T15 / T16 / T05 需人工听测，结果回填 docs/verify/$Change.md"
}

Write-Log ""
Write-Log "== summary =="
Write-Log "steps run: $ran  failed: $failed"
Write-Log "下一步：回填 docs/verify/$Change.md，未执行项登记 docs/verification-debt.md"

if ($failed -gt 0) { exit 1 }
exit 0
