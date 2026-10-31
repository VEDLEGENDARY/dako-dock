$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'shape-lint.ps1')
$bad = @'
Fill({1, 2, 3, 4}, Mix(a, b, .5f), 14);
fill(rect, color, 28);
graphics::Rounded(target, factory, cache, rect, 14, brush);
controlShape(rect, 28, color);
FocusRing(id, rect, 9.0f);
D2D1::RoundedRect(rect,
  28 * scale, 28 * scale);
CreateRoundRectRgn(0, 0, 400, 300, 56, 56);
dock::Attached<24>(0, 0, 800, 400, 22, 24);
'@
if (@(Get-ShapeRadiusViolations $bad).Count -ne 11) { throw 'Numeric or multiline radii escaped the shape lint.' }
$good = @'
Fill(rect, color, shape::Card);
Fill(rect, color, shape::Pill(height));
D2D1::RoundedRect(rect, radius, shape::Inner(shape::Surface, inset));
CreateRoundRectRgn(0, 0, w, h, shape::Surface * scale * 2, shape::Surface * scale * 2);
// Fill(rect, color, 14);
const char* text = "RoundedRect(rect, 9, 9)";
'@
if (@(Get-ShapeRadiusViolations $good).Count) { throw 'Shape lint rejected tokens, dynamic radii or comments.' }
Write-Output 'PASS shape radius lint catches nested/multiline literals and permits tokens and dynamic geometry.'
