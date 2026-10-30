# Windows Macro Hygiene Adversarial Stress Suite Runner
# Uses Clang-cl (targeting MSVC ABI / Windows SDK)

$ErrorActionPreference = "Continue"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path "$ScriptDir\.."
$SharedDir = "$RepoRoot\shared"
$TestFile = "$ScriptDir\test_windows_hygiene_stress.cpp"
$OutExe = "$ScriptDir\test_windows_hygiene_stress.exe"
$ClangCl = "C:\Users\bhogl\scoop\apps\llvm\current\bin\clang-cl.exe"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "Windows Macro Hygiene Adversarial Stress Suite" -ForegroundColor Cyan
Write-Host "Compiler: $ClangCl" -ForegroundColor Gray
Write-Host "Test File: $TestFile" -ForegroundColor Gray
Write-Host "==========================================================" -ForegroundColor Cyan

$script:Passed = 0
$script:Total = 0

function Test-CleanMode {
    param([string]$Name, [string]$Define)
    $script:Total++
    Write-Host "[$($script:Total)] $Name..." -NoNewline
    Remove-Item $OutExe -ErrorAction SilentlyContinue
    
    $args = @("/std:c++latest", "/EHsc", "/W4", "-I$SharedDir", $TestFile, "/Fe:$OutExe", "/Fo:$ScriptDir\")
    if ($Define) { $args = @($Define) + $args }
    
    $out = (& $ClangCl $args 2>&1) -join "`n"
    if ($LASTEXITCODE -eq 0 -and (Test-Path $OutExe)) {
        $runOut = (& $OutExe) -join "`n"
        if ($LASTEXITCODE -eq 0 -and $runOut -match "\[PASS\]") {
            Write-Host " PASS" -ForegroundColor Green
            $script:Passed++
        } else {
            Write-Host " RUN FAIL ($LASTEXITCODE)" -ForegroundColor Red
            Write-Host $runOut -ForegroundColor Yellow
        }
        Remove-Item $OutExe -ErrorAction SilentlyContinue
    } else {
        Write-Host " COMPILE FAIL" -ForegroundColor Red
        Write-Host $out -ForegroundColor Yellow
    }
}

function Test-TriggerMode {
    param([string]$Name, [string]$Define, [string]$ExpectedPattern)
    $script:Total++
    Write-Host "[$($script:Total)] Adversarial Trigger: $Name..." -NoNewline
    Remove-Item $OutExe -ErrorAction SilentlyContinue
    
    $args = @("/std:c++latest", "/EHsc", "/W4", "-I$SharedDir", $Define, $TestFile, "/Fe:$OutExe", "/Fo:$ScriptDir\")
    $out = (& $ClangCl $args 2>&1) -join "`n"
    
    if ($LASTEXITCODE -ne 0 -and ($out -match $ExpectedPattern)) {
        Write-Host " EXPECTED FAILURE (Caught: $ExpectedPattern)" -ForegroundColor Green
        $script:Passed++
    } else {
        Write-Host " UNEXPECTED RESULT (ExitCode: $LASTEXITCODE)" -ForegroundColor Red
        Write-Host $out -ForegroundColor Yellow
    }
    Remove-Item $OutExe -ErrorAction SilentlyContinue
}

# 1. Clean Mode Scenarios with shared/util/windows-hygiene.hpp
Test-CleanMode "Clean Mode Scenario 1 (Hygiene header BEFORE Windows headers)" ""
Test-CleanMode "Clean Mode Scenario 2 (Hygiene header AFTER Windows headers)" "-DTEST_ORDER_HYGIENE_AFTER_WINDOWS"
Test-CleanMode "Clean Mode Scenario 3 (Hygiene header INTERLEAVED with Windows headers)" "-DTEST_ORDER_INTERLEAVED"

# 2. Adversarial Failure Triggers WITHOUT windows-hygiene.hpp
Test-TriggerMode "Missing NOMINMAX (std::max & limits)" "-DTRIGGER_NOMINMAX_COLLISION" "expected unqualified-id|too few arguments"
Test-TriggerMode "Missing #undef small (rpcndr.h char collision)" "-DTRIGGER_SMALL_COLLISION" "cannot combine with previous 'int'"
Test-TriggerMode "Missing #undef near/far (minwindef.h frustum float collision)" "-DTRIGGER_NEARFAR_COLLISION" "declaration does not declare anything"
Test-TriggerMode "Missing #undef interface (basetyps.h struct collision)" "-DTRIGGER_INTERFACE_COLLISION" "expected identifier|expected"
Test-TriggerMode "Missing #undef ERROR (wingdi.h 0 collision)" "-DTRIGGER_ERROR_COLLISION" "expected identifier|expected"
Test-TriggerMode "Missing #undef RGB (wingdi.h macro collision)" "-DTRIGGER_RGB_COLLISION" "expected unqualified-id|too few arguments"

# Clean up temporary obj files
Get-ChildItem -Path $ScriptDir -Filter "*.obj" | Remove-Item -Force -ErrorAction SilentlyContinue

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "Final Stress Test Results: $($script:Passed) / $($script:Total) passed." -ForegroundColor $(if ($script:Passed -eq $script:Total) { "Green" } else { "Red" })
Write-Host "==========================================================" -ForegroundColor Cyan

if ($script:Passed -eq $script:Total) {
    exit 0
} else {
    exit 1
}
