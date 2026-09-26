# CampusEats 备份
#
# 把 E:\CampusEats 复制到 E:\CampusEats-yyyyMMdd-HHmm，
# 排除 cmake-build-debug（能重建）和 .idea（IDE 配置）。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File E:\CampusEats\BACKUP.ps1

$ErrorActionPreference = 'Stop'

$src = 'E:\CampusEats'
$dst = 'E:\CampusEats-' + (Get-Date -Format 'yyyyMMdd-HHmm')

if (-not (Test-Path $src)) {
    Write-Host ('源目录不存在：' + $src) -ForegroundColor Red
    exit 1
}

Write-Host ('备份 ' + $src + '  ->  ' + $dst) -ForegroundColor Cyan

# /E        连空目录一起复制
# /XD       排除这两个目录（按名字匹配）
# /NFL /NDL 不逐条列文件、目录，输出干净点
robocopy $src $dst /E /XD cmake-build-debug .idea /NFL /NDL

# robocopy 退出码 0~7 都算成功，8 以上才是真出错
if ($LASTEXITCODE -ge 8) {
    Write-Host ('失败：robocopy 退出码 ' + $LASTEXITCODE) -ForegroundColor Red
    exit 1
}

$n = (Get-ChildItem $dst -Recurse -File -ErrorAction SilentlyContinue).Count
Write-Host ('完成：' + $n + ' 个文件') -ForegroundColor Green