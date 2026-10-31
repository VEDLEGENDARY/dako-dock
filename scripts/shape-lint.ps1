function Get-ShapeRadiusViolations {
    param([string]$Source, [string]$Name = '<source>')
    # Mask comments and quoted strings while retaining offsets and newlines.
    $mask = [regex]::Replace($Source, '(?s)/\*.*?\*/|//[^\r\n]*|"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*''', {
        param($match)
        [regex]::Replace($match.Value, '[^\r\n]', ' ')
    })
    $positions = @{ Fill = @(2); FocusRing = @(2); RoundedRect = @(1,2); 'graphics::Rounded' = @(4); controlShape = @(1);
                    CreateRoundRectRgn = @(4,5); Attached = @(4,5) }
    $calls = '(?i)\b(graphics::Rounded|Fill|FocusRing|RoundedRect|CreateRoundRectRgn|Attached|controlShape)\s*(?:<[^>]+>)?\s*\('
    foreach ($call in [regex]::Matches($mask, $calls)) {
        $start = $call.Index + $call.Length
        $depth = 0
        $arguments = @()
        $argumentStart = $start
        for ($offset = $start; $offset -lt $mask.Length; $offset++) {
            $character = $mask[$offset]
            if ($character -in @('(', '{', '[')) { $depth++ }
            elseif ($character -eq ')' -and $depth -eq 0) {
                $arguments += $mask.Substring($argumentStart, $offset - $argumentStart).Trim()
                break
            } elseif ($character -in @(')', '}', ']')) { $depth-- }
            elseif ($character -eq ',' -and $depth -eq 0) {
                $arguments += $mask.Substring($argumentStart, $offset - $argumentStart).Trim()
                $argumentStart = $offset + 1
            }
        }
        foreach ($position in $positions[$call.Groups[1].Value]) {
            if ($position -ge $arguments.Count) { continue }
            $argument = $arguments[$position]
            if ($argument -notmatch 'shape::' -and $argument -match '^[-+]?\s*\d+(?:\.\d*)?f?(?:\s|$|[*/+\-])') {
                $line = 1 + ([regex]::Matches($mask.Substring(0, $call.Index), '\n')).Count
                [pscustomobject]@{ File = $Name; Line = $line; Call = $call.Groups[1].Value; Radius = $argument }
            }
        }
    }
}
