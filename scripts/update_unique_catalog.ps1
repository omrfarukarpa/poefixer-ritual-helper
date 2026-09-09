param(
    [string]$League = 'Runes of Aldur',
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\config\UniqueCatalog.h')
)

$categories = @('weapon', 'armour', 'accessory', 'jewel', 'flask', 'sanctum')
$rows = [System.Collections.Generic.List[object]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
$encodedLeague = [System.Uri]::EscapeDataString($League)

foreach ($category in $categories) {
    $page = 1
    do {
        $uri = "https://poe2scout.com/api/poe2/Leagues/$encodedLeague/Uniques/ByCategory?Category=$category&PerPage=250&Page=$page"
        $response = Invoke-RestMethod -Uri $uri -TimeoutSec 30
        foreach ($item in @($response.Items)) {
            if ($null -eq $item -or [string]::IsNullOrWhiteSpace($item.Name) -or [string]::IsNullOrWhiteSpace($item.Type)) { continue }
            if ($seen.Add([string]$item.Name)) {
                $rows.Add([pscustomobject]@{
                    Category = "unique $category"
                    Name = [string]$item.Name
                    BaseType = [string]$item.Type
                })
            }
        }
        $pages = [int]$response.Pages
        $page++
    } while ($page -le $pages)
}

function CppString([string]$value) {
    return '"' + $value.Replace('\', '\\').Replace('"', '\"') + '"'
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('#pragma once')
$lines.Add('')
$lines.Add('#include <cstddef>')
$lines.Add('')
$lines.Add('namespace RitualHelper {')
$lines.Add('')
$lines.Add('struct UniqueCatalogEntry { const char* category; const char* name; const char* baseType; };')
$lines.Add('')
$lines.Add('inline constexpr UniqueCatalogEntry kUniqueCatalog[] = {')
foreach ($row in $rows) {
    $lines.Add(('    {{{0}, {1}, {2}}},' -f (CppString $row.Category), (CppString $row.Name), (CppString $row.BaseType)))
}
$lines.Add('};')
$lines.Add('')
$lines.Add('inline constexpr size_t kUniqueCatalogCount = sizeof(kUniqueCatalog) / sizeof(kUniqueCatalog[0]);')
$lines.Add('')
$lines.Add('}')

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($resolvedOutput)) | Out-Null
[System.IO.File]::WriteAllText($resolvedOutput, ($lines -join "`n") + "`n", $utf8NoBom)
Write-Output "Wrote $($rows.Count) unique entries to $OutputPath"



